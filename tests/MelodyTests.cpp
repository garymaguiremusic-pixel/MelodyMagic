#include <catch2/catch_test_macros.hpp>

#include "mm/Melody.h"

#include <cmath>
#include <set>

using namespace mm;

namespace
{
    bool chordTone (int offset, int chordDegree, const ScaleContext& sc, int key)
    {
        const int n = sc.notesPerOctave;
        const int d = offsetToDegree (offset, sc, key);
        const int r = floorMod (d - chordDegree, n);
        return r == 0 || r == 2 || r == 4;
    }

    std::vector<int> barPositions (const Pattern& p, int bar)
    {
        std::vector<int> v;
        for (auto& n : p.notes)
            if (n.start / kTicksPerBar == bar)
                v.push_back (n.start % kTicksPerBar);
        return v;
    }
}

TEST_CASE ("Scale degrees and chord names", "[melody]")
{
    const auto am = ScaleContext::make (NaturalMinor, 9);
    CHECK (degreeToOffset (0, am, 9) == 0);
    CHECK (degreeToOffset (2, am, 9) == 3);    // C
    CHECK (degreeToOffset (7, am, 9) == 12);
    CHECK (degreeToOffset (-1, am, 9) == -2);  // G below
    for (int d = -10; d <= 14; ++d)
        CHECK (offsetToDegree (degreeToOffset (d, am, 9), am, 9) == d);
    CHECK (offsetToDegree (1, am, 9) == 0);    // A# is nearest A (ties down)

    CHECK (chordName (0, am, 9) == "Am");
    CHECK (chordName (5, am, 9) == "F");
    CHECK (chordName (2, am, 9) == "C");
    CHECK (chordName (6, am, 9) == "G");
    CHECK (chordName (1, am, 9) == "Bdim");
    const auto c = ScaleContext::make (Major, 0);
    CHECK (chordName (4, c, 0) == "G");
    CHECK (chordName (5, c, 0) == "Am");

    CHECK (progressionDegrees (0, am, 9) == std::vector<int> { 0, 5, 2, 6 });   // Am F C G
    CHECK (progressionDegrees (0, c, 0) == std::vector<int> { 5, 3, 0, 4 });    // Am F C G in C major
}

TEST_CASE ("Melodies follow the rules: in key, chord tones on the beat, a repeated hook", "[melody]")
{
    const int key = 9;
    const auto sc = ScaleContext::make (NaturalMinor, key);
    const auto style = defaultMelodyStyle (sc);
    MelodySettings m;
    const auto chords = progressionDegrees (0, sc, key);

    int steps = 0, small = 0;
    for (std::int64_t seed = 1; seed <= 40; ++seed)
    {
        JuceLcg r (seed);
        const auto p = makeMelody (4, m, style, sc, key, r);
        REQUIRE (p.bars == 4);
        REQUIRE (! p.notes.empty());

        // the hook's rhythm repeats in bars 1-3
        CHECK (barPositions (p, 1) == barPositions (p, 0));
        CHECK (barPositions (p, 2) == barPositions (p, 0));

        int prevDegree = 0;
        bool first = true;
        for (auto& n : p.notes)
        {
            CHECK (sc.inScale (key + n.offset));
            CHECK (n.end > n.start);
            CHECK (n.end <= 4 * kTicksPerBar);
            const int bar = n.start / kTicksPerBar;
            const int pos = n.start % kTicksPerBar;
            if (pos % kPhrasePpq == 0)   // on the beat
                CHECK (chordTone (n.offset, chords[(size_t) bar % chords.size()], sc, key));

            const int d = offsetToDegree (n.offset, sc, key);
            if (! first)
            {
                ++steps;
                if (std::abs (d - prevDegree) <= 2 || std::abs (d - prevDegree) == 7)
                    ++small;
            }
            prevDegree = d;
            first = false;
        }
    }
    // mostly stepwise (or octave) motion, like a sung line
    CHECK (small > steps * 6 / 10);
}

TEST_CASE ("Melodies are deterministic for a seed and use every length", "[melody]")
{
    const auto sc = ScaleContext::make (HarmonicMinor, 2);
    const auto style = defaultMelodyStyle (sc);
    MelodySettings m;
    for (int bars : { 1, 2, 4, 8 })
    {
        JuceLcg a (77), b (77);
        const auto p1 = makeMelody (bars, m, style, sc, 2, a);
        const auto p2 = makeMelody (bars, m, style, sc, 2, b);
        CHECK (p1 == p2);
        CHECK (p1.bars == bars);
        for (auto& n : p1.notes)
            CHECK (sc.inScale (2 + n.offset));
    }
}

TEST_CASE ("Range limits how far the melody goes", "[melody]")
{
    const auto sc = ScaleContext::make (Major, 0);
    const auto style = defaultMelodyStyle (sc);
    MelodySettings m;
    m.range = 7;
    for (std::int64_t seed = 1; seed <= 20; ++seed)
    {
        JuceLcg r (seed);
        const auto p = makeMelody (4, m, style, sc, 0, r);
        int lo = 100, hi = -100;
        for (auto& n : p.notes) { lo = std::min (lo, n.offset); hi = std::max (hi, n.offset); }
        CHECK (hi - lo <= 9);
    }
}

TEST_CASE ("Randomise Notes keeps the rhythm and writes a melody", "[melody]")
{
    const int key = 7;
    const auto sc = ScaleContext::make (Dorian, key);
    Pattern p;
    p.bars = 2;
    for (int i = 0; i < 2; ++i)
        for (int s : { 0, 3, 6, 8, 11, 14 })
            p.notes.push_back ({ i * 384 + s * 24, i * 384 + s * 24 + 40, 0, 100 - s });
    const auto before = p;

    JuceLcg r (12);
    melodiseNotes (p, {}, defaultMelodyStyle (sc), sc, key, r);
    REQUIRE (p.notes.size() == before.notes.size());
    std::set<int> pitches;
    for (size_t i = 0; i < p.notes.size(); ++i)
    {
        CHECK (p.notes[i].start == before.notes[i].start);
        CHECK (p.notes[i].end == before.notes[i].end);
        CHECK (p.notes[i].velocity == before.notes[i].velocity);
        CHECK (sc.inScale (key + p.notes[i].offset));
        pitches.insert (p.notes[i].offset);
    }
    CHECK (pitches.size() >= 3);   // an actual line, not one repeated note
}

TEST_CASE ("Stacked notes stay a chord when randomised", "[melody]")
{
    const auto sc = ScaleContext::make (NaturalMinor, 9);
    Pattern p;
    p.notes = { { 0, 48, 0, 100 }, { 0, 48, 3, 100 }, { 0, 48, 7, 100 }, { 96, 144, 0, 100 } };
    JuceLcg r (3);
    melodiseNotes (p, {}, defaultMelodyStyle (sc), sc, 9, r);
    std::set<int> first;
    for (auto& n : p.notes)
        if (n.start == 0)
            first.insert (n.offset);
    CHECK (first.size() == 3);
}

TEST_CASE ("Melodies learn from an imported file", "[melody][import]")
{
    const int key = 9;
    const auto sc = ScaleContext::make (NaturalMinor, key);
    // a file: 2 bars, tresillo rhythm, notes between E (7) and the A above (12)
    Pattern file;
    file.bars = 2;
    const int offs[] = { 7, 8, 10, 12, 10, 8 };
    int i = 0;
    for (int b = 0; b < 2; ++b)
        for (int s : { 0, 3, 6, 8, 11, 14 })
        {
            file.notes.push_back ({ b * 384 + s * 24, b * 384 + s * 24 + 60, offs[i % 6], 110 });
            ++i;
        }

    const auto style = styleFromPattern (file, sc, key);
    CHECK (style.fromFile);
    CHECK (style.rhythmA.size() == 6);
    CHECK (style.rhythmB.size() == 6);
    CHECK (style.lowDegree <= offsetToDegree (7, sc, key));
    CHECK (style.highDegree >= offsetToDegree (12, sc, key));

    for (std::int64_t seed = 1; seed <= 10; ++seed)
    {
        JuceLcg r (seed);
        const auto p = makeMelody (4, {}, style, sc, key, r);
        CHECK (barPositions (p, 0) == std::vector<int> { 0, 72, 144, 192, 264, 336 });   // the file's rhythm
        for (auto& n : p.notes)
        {
            CHECK (sc.inScale (key + n.offset));
            CHECK (offsetToDegree (n.offset, sc, key) >= style.lowDegree);
            CHECK (offsetToDegree (n.offset, sc, key) <= style.highDegree);
        }
    }
}

TEST_CASE ("Keep Melody: same notes in order, new rhythm", "[melody][keep]")
{
    const auto sc = ScaleContext::make (NaturalMinor, 9);
    Pattern p;
    p.bars = 2;
    const int line[] = { 0, 3, 7, 5, 3, 2, 0, -2 };
    for (int b = 0; b < 2; ++b)
        for (int i = 0; i < 8; ++i)
            p.notes.push_back ({ b * 384 + i * 48, b * 384 + i * 48 + 40, line[(i + b * 3) % 8], 100 });

    for (std::int64_t seed = 1; seed <= 20; ++seed)
    {
        auto q = p;
        JuceLcg r (seed);
        rhythmiseMelody (q, {}, defaultMelodyStyle (sc), r);
        CHECK (q.bars == 2);
        REQUIRE (! q.notes.empty());
        for (int b = 0; b < 2; ++b)
        {
            // the bar's notes come from the same bar of the original, in order
            std::vector<int> original, now;
            for (auto& n : p.notes) if (n.start / 384 == b) original.push_back (n.offset);
            for (auto& n : q.notes) if (n.start / 384 == b) now.push_back (n.offset);
            size_t k = 0;
            for (int o : now)
            {
                while (k < original.size() && original[k] != o) ++k;
                CHECK (k < original.size());   // a subsequence (with repeats) of the original
            }
        }
    }
}

TEST_CASE ("Keep Both: bar 1 stays, the rest is re-written around it", "[melody][keep]")
{
    const int key = 9;
    const auto sc = ScaleContext::make (NaturalMinor, key);
    JuceLcg g (5);
    const auto p = makeMelody (4, {}, defaultMelodyStyle (sc), sc, key, g);

    int changed = 0;
    for (std::int64_t seed = 1; seed <= 10; ++seed)
    {
        auto q = p;
        JuceLcg r (seed);
        melodiseNotes (q, {}, defaultMelodyStyle (sc), sc, key, r, true);
        REQUIRE (q.notes.size() == p.notes.size());
        for (size_t i = 0; i < q.notes.size(); ++i)
        {
            CHECK (q.notes[i].start == p.notes[i].start);
            CHECK (q.notes[i].end == p.notes[i].end);
            if (q.notes[i].start < 384)
                CHECK (q.notes[i].offset == p.notes[i].offset);
            else if (q.notes[i].offset != p.notes[i].offset)
                ++changed;
            CHECK (sc.inScale (key + q.notes[i].offset));
        }
    }
    CHECK (changed > 0);
}

TEST_CASE ("New Melody stays in the pattern's register", "[melody][register]")
{
    const int key = 9;
    const auto sc = ScaleContext::make (NaturalMinor, key);
    MelodySettings m;   // Range 14 semitones
    for (int base : { -12, 0, 12 })
    {
        Pattern p;
        for (int i = 0; i < 16; ++i)
            p.notes.push_back ({ i * 24, i * 24 + 18, base, 100 });
        for (std::int64_t seed = 1; seed <= 10; ++seed)
        {
            auto q = p;
            JuceLcg r (seed);
            melodiseNotes (q, m, defaultMelodyStyle (sc), sc, key, r);
            for (auto& n : q.notes)
            {
                CHECK (n.offset >= base);          // never below the pattern's bottom note
                CHECK (n.offset <= base + 16);     // no octave jump: within Range above it
            }
        }
    }
}

TEST_CASE ("Top notes only keeps the bottom notes", "[melody][top]")
{
    const int key = 9;
    const auto sc = ScaleContext::make (NaturalMinor, key);

    // stabs: stacked A C E chords plus a bass note on the beats
    Pattern stabs;
    for (int i = 0; i < 8; ++i)
    {
        const int t = i * 48;
        if (i % 2 == 0) stabs.notes.push_back ({ t, t + 40, -12, 110 });
        else
            for (int o : { 0, 3, 7 })
                stabs.notes.push_back ({ t, t + 40, o, 100 });
    }
    stabs.sort();
    for (std::int64_t seed = 1; seed <= 10; ++seed)
    {
        auto q = stabs;
        JuceLcg r (seed);
        melodiseNotes (q, {}, defaultMelodyStyle (sc), sc, key, r, false, true);
        REQUIRE (q.notes.size() == stabs.notes.size());
        for (int i = 0; i < 8; ++i)
        {
            std::vector<int> before, after;
            for (auto& n : stabs.notes) if (n.start == i * 48) before.push_back (n.offset);
            for (auto& n : q.notes)     if (n.start == i * 48) after.push_back (n.offset);
            std::sort (after.begin(), after.end());
            if (i % 2 == 0)
                CHECK (after == before);            // the bass note stays
            else
            {
                CHECK (after[0] == 0);              // lower chord notes stay
                CHECK (after[1] == 3);
                CHECK (after[2] > -12);             // the top note moves, above the bottom
                CHECK (sc.inScale (key + after[2]));
            }
        }
    }

    // a 16ths arp all on the root: the beats stay as a pedal, the notes between become the melody
    Pattern arp;
    for (int i = 0; i < 16; ++i)
        arp.notes.push_back ({ i * 24, i * 24 + 18, 0, 100 });
    int moved = 0;
    for (std::int64_t seed = 1; seed <= 5; ++seed)
    {
        auto q = arp;
        JuceLcg r (seed);
        melodiseNotes (q, {}, defaultMelodyStyle (sc), sc, key, r, false, true);
        for (auto& n : q.notes)
        {
            if (n.start % 96 == 0) CHECK (n.offset == 0);
            else
            {
                CHECK (n.offset > 0);
                if (n.offset != 0) ++moved;
            }
        }
    }
    CHECK (moved > 0);
}
