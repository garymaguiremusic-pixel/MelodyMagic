#include <catch2/catch_test_macros.hpp>

#include "mm/ArpProcessor.h"
#include "mm/Pattern.h"

#include <algorithm>

using namespace mm;

namespace
{
    Pattern steps (std::initializer_list<int> offsets, int stepTicks = 24, int gate = 12)
    {
        Pattern p;
        int i = 0;
        for (int o : offsets)
        {
            p.notes.push_back ({ i * stepTicks, i * stepTicks + gate, o, 100 });
            ++i;
        }
        return p;
    }

    std::vector<int> pitches (const std::vector<TimedNote>& notes)
    {
        std::vector<int> v;
        for (auto& n : notes) v.push_back (n.note);
        return v;
    }

    ArpSettings settings (ArpOrder order = ArpOrder::Up)
    {
        ArpSettings s;
        s.order = order;
        return s;
    }

    std::vector<TimedNote> render (const Pattern& p, std::vector<int> chord, const ArpSettings& s, double ticks = 384.0)
    {
        return renderPattern (makePlayPattern (p, nullptr, 0.0, 0.0, false), chord, 127, s, 120.0, ticks, nullptr);
    }
}

TEST_CASE ("Arp orders", "[arp]")
{
    CHECK (arpSequence (4, ArpOrder::Up) == std::vector<int> { 0, 1, 2, 3 });
    CHECK (arpSequence (4, ArpOrder::Down) == std::vector<int> { 3, 2, 1, 0 });
    CHECK (arpSequence (4, ArpOrder::UpDown) == std::vector<int> { 0, 1, 2, 3, 2, 1 });
    CHECK (arpSequence (4, ArpOrder::DownUp) == std::vector<int> { 3, 2, 1, 0, 1, 2 });
    CHECK (arpSequence (1, ArpOrder::UpDown) == std::vector<int> { 0 });
    CHECK (arpSequence (2, ArpOrder::DownUp) == std::vector<int> { 1, 0 });
    CHECK (arpSequence (0, ArpOrder::Up).empty());
}

TEST_CASE ("One held key plays the pattern as a melody", "[arp]")
{
    const auto out = render (steps ({ 0, 2, 4, 7, 12, -5 }), { 60 }, settings());
    CHECK (pitches (out) == std::vector<int> { 60, 62, 64, 67, 72, 55 });
    CHECK (out[1].start == 24.0);
    CHECK (out[1].end == 36.0);
}

TEST_CASE ("A held chord is arpeggiated in each order", "[arp]")
{
    const auto p = steps ({ 0, 0, 0, 0, 0, 0, 0, 0 });
    CHECK (pitches (render (p, { 64, 60, 67 }, settings (ArpOrder::Up)))     == std::vector<int> { 60, 64, 67, 60, 64, 67, 60, 64 });
    CHECK (pitches (render (p, { 60, 64, 67 }, settings (ArpOrder::Down)))   == std::vector<int> { 67, 64, 60, 67, 64, 60, 67, 64 });
    CHECK (pitches (render (p, { 60, 64, 67 }, settings (ArpOrder::UpDown))) == std::vector<int> { 60, 64, 67, 64, 60, 64, 67, 64 });
    CHECK (pitches (render (p, { 60, 64, 67 }, settings (ArpOrder::DownUp))) == std::vector<int> { 67, 64, 60, 64, 67, 64, 60, 64 });
}

TEST_CASE ("Offsets transpose the arp notes", "[arp]")
{
    const auto out = render (steps ({ 0, 12, 0, 12 }), { 60, 64 }, settings());
    CHECK (pitches (out) == std::vector<int> { 60, 76, 60, 76 });
}

TEST_CASE ("The pattern restarts its arp order every loop", "[arp]")
{
    const auto out = render (steps ({ 0, 0, 0 }), { 60, 64 }, settings(), 768.0);   // 1-bar pattern, 2 bars rendered
    CHECK (pitches (out) == std::vector<int> { 60, 64, 60, 60, 64, 60 });
}

TEST_CASE ("Scale Lock moves every note into the key", "[arp][scalelock]")
{
    auto s = settings();
    s.scaleLock = true;
    s.scale = ScaleContext::make (Major, 0);
    const auto out = render (steps ({ 0, 1, 3, 6, 8, 10 }), { 60 }, s);
    for (auto& n : out)
        CHECK (s.scale.inScale (n.note));
    CHECK (pitches (out) == std::vector<int> { 60, 60, 62, 65, 67, 69 });
}

TEST_CASE ("Velocity follows the note and the key", "[arp]")
{
    CHECK (ArpProcessor::velocityOf (100, 127, 0, nullptr) == 100);
    CHECK (ArpProcessor::velocityOf (100, 64, 0, nullptr) == 50);
    CHECK (ArpProcessor::velocityOf (1, 1, 0, nullptr) == 1);
    JuceLcg r (3);
    for (int i = 0; i < 200; ++i)
    {
        const int v = ArpProcessor::velocityOf (100, 127, 30, &r);
        CHECK (v >= 70);
        CHECK (v <= 127);
    }
}

TEST_CASE ("Stopped host: the first key starts the pattern", "[arp]")
{
    ArpProcessor proc;
    const auto pp = makePlayPattern (steps ({ 0, 7 }), nullptr, 0, 0, false);
    TransportInfo t;   // stopped
    t.sampleRate = 48000;
    std::vector<Event> out;
    proc.process (t, 512, settings(), &pp, {}, out);
    CHECK (out.empty());

    proc.process (t, 512, settings(), &pp, { Event::on (100, 48, 127) }, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::On && out[0].note == 48 && out[0].time == 100));

    // releasing every key ends the note at once
    out.clear();
    proc.process (t, 512, settings(), &pp, { Event::off (5, 48) }, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].note == 48 && out[0].time == 5));
}

TEST_CASE ("Transport stop and bypass leave nothing hanging", "[arp]")
{
    ArpProcessor proc;
    Pattern p;
    p.notes = { { 0, 300, 0, 100 } };
    const auto pp = makePlayPattern (p, nullptr, 0, 0, false);
    TransportInfo t;
    t.playing = true;
    t.hasPosition = true;
    t.sampleRate = 48000;
    std::vector<Event> out;
    proc.process (t, 256, settings(), &pp, { Event::on (0, 60, 100) }, out);
    REQUIRE (out.size() == 1);

    out.clear();
    t.playing = false;
    proc.process (t, 256, settings(), &pp, {}, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].note == 60));

    auto s = settings();
    s.bypassed = true;
    out.clear();
    proc.process (t, 256, s, &pp, { Event::on (3, 72, 90) }, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::On && out[0].note == 72));
    s.bypassed = false;
    out.clear();
    proc.process (t, 256, s, &pp, {}, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].note == 72));
}

TEST_CASE ("Groove quantize moves pattern notes", "[arp][groove]")
{
    Groove g { "test", 96, 96, { 0, 30 } };
    Pattern p;
    p.notes = { { 20, 40, 0, 100 } };
    const auto pp = makePlayPattern (p, &g, 1.0, 0.0, false);
    REQUIRE (pp.clip.notes.size() == 1);
    CHECK (pp.clip.notes[0].start == 30.0);
    CHECK (pp.clip.notes[0].end == 40.0);
    CHECK (quantizeTick (20.0, 0.5, g) == 25.0);
}

//==============================================================================
TEST_CASE ("Mouse wheel steps: semitones, or scale notes with a scale", "[edit]")
{
    CHECK (stepOffset (0, 1, nullptr, 0) == 1);
    CHECK (stepOffset (0, -1, nullptr, 0) == -1);
    const auto c = ScaleContext::make (Major, 0);
    CHECK (stepOffset (0, 1, &c, 0) == 2);    // C -> D
    CHECK (stepOffset (4, 1, &c, 0) == 5);    // E -> F
    CHECK (stepOffset (0, -1, &c, 0) == -1);  // C -> B
    const auto a = ScaleContext::make (MinorPentatonic, 9);
    CHECK (stepOffset (0, 1, &a, 9) == 3);    // A -> C
    CHECK (stepOffset (kMaxOffset, 1, nullptr, 0) == kMaxOffset);
}

TEST_CASE ("Generated patterns are in the scale and inside the length", "[generate][scalelock]")
{
    const auto scale = ScaleContext::make (Phrygian, 7);
    for (std::int64_t seed : { 1, 2, 3, 42 })
    {
        JuceLcg r (seed);
        GenerateSettings g;
        g.range = 12;
        const auto p = generatePattern (2, g, scale, 7, r);
        CHECK (p.bars == 2);
        REQUIRE (! p.notes.empty());
        for (auto& n : p.notes)
        {
            CHECK (scale.inScale (7 + n.offset));
            CHECK (std::abs (n.offset) <= 12);
            CHECK (n.start % 24 == 0);
            CHECK (n.end > n.start);
            CHECK (n.end <= 768);
        }
    }
    JuceLcg a (9), b (9);
    CHECK (generatePattern (1, {}, scale, 7, a) == generatePattern (1, {}, scale, 7, b));

    GenerateSettings full;
    full.density = 100;
    JuceLcg r (4);
    CHECK (generatePattern (1, full, scale, 7, r).notes.size() == 16);
}

TEST_CASE ("Randomise notes keeps the rhythm and stays in the scale", "[generate][scalelock]")
{
    Pattern p = steps ({ 0, 1, 2, 3, 4, 5, 6, 7 }, 48, 30);
    p.notes[3].velocity = 55;
    const auto before = p;
    const auto scale = ScaleContext::make (Blues, 2);
    JuceLcg r (5);
    randomiseNotes (p, 7, scale, 2, r);
    REQUIRE (p.notes.size() == before.notes.size());
    for (size_t i = 0; i < p.notes.size(); ++i)
    {
        CHECK (p.notes[i].start == before.notes[i].start);
        CHECK (p.notes[i].end == before.notes[i].end);
        CHECK (p.notes[i].velocity == before.notes[i].velocity);
        CHECK (scale.inScale (2 + p.notes[i].offset));
        CHECK (std::abs (p.notes[i].offset) <= 7);
    }
}

TEST_CASE ("Shuffle keeps the rhythm and uses exactly the file's notes", "[import]")
{
    Pattern p = steps ({ 0, 3, 7, 10, 12, 3 });
    std::vector<int> pool;
    for (auto& n : p.notes) pool.push_back (n.offset);
    const auto before = p;
    JuceLcg r (6);
    shuffleNotes (p, pool, r);

    std::vector<int> got;
    for (size_t i = 0; i < p.notes.size(); ++i)
    {
        CHECK (p.notes[i].start == before.notes[i].start);
        CHECK (p.notes[i].end == before.notes[i].end);
        got.push_back (p.notes[i].offset);
    }
    CHECK (got != pool);   // moved
    std::sort (got.begin(), got.end());
    std::sort (pool.begin(), pool.end());
    CHECK (got == pool);   // same notes
}

TEST_CASE ("MIDI import keeps the notes and rhythm", "[import]")
{
    MidiClip c;
    c.ppq = 480;
    // a line in A minor starting in bar 2: A3 C4 E4 G4 (57 60 64 67)
    c.notes = { { 57, 100, 1, 1920, 2160 }, { 60, 90, 1, 2400, 2640 }, { 64, 80, 1, 2880, 3360 }, { 67, 70, 1, 3360, 3600 } };
    std::vector<int> fileNotes;
    const auto p = patternFromMidi (c, 9, "line", &fileNotes);
    CHECK (p.bars == 1);
    REQUIRE (p.notes.size() == 4);
    CHECK ((p.notes[0].start == 0 && p.notes[0].end == 48 && p.notes[0].offset == 0 && p.notes[0].velocity == 100));
    CHECK ((p.notes[1].start == 96 && p.notes[1].offset == 3));
    CHECK ((p.notes[2].start == 192 && p.notes[2].end == 288 && p.notes[2].offset == 7));
    CHECK (p.notes[3].offset == 10);
    CHECK (fileNotes == std::vector<int> { 0, 3, 7, 10 });

    // holding the key's root plays the file's notes back
    const auto out = render (p, { 57 }, settings());
    CHECK (pitches (out) == std::vector<int> { 57, 60, 64, 67 });
}

TEST_CASE ("Library phrases become patterns", "[import]")
{
    Phrase ph;
    ph.notes = { { 50, 0, 100, 0, 24 }, { 61, 0, 90, 96, 120 }, { 42, 0, 80, 192, 216 } };
    const auto p = patternFromPhrase (ph);
    CHECK (p.bars == 1);
    REQUIRE (p.notes.size() == 3);
    CHECK (p.notes[0].offset == 0);
    CHECK (p.notes[1].offset == 12);
    CHECK (p.notes[2].offset == -12);
    CHECK (p.notes[1].velocity == 90);
}
