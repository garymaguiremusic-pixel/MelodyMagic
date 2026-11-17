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
    CHECK (arpSequence (3, ArpOrder::Melody) == std::vector<int> { 0 });
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

TEST_CASE ("MIDI import keeps the notes and rhythm, and Melody mode plays them back exactly", "[import]")
{
    MidiClip c;
    c.ppq = 480;
    // a line in A minor starting in bar 2: A3 C4 E4 G4 (69 72 76 79)
    c.notes = { { 69, 100, 1, 1920, 2160 }, { 72, 90, 1, 2400, 2640 }, { 76, 80, 1, 2880, 3360 }, { 79, 70, 1, 3360, 3600 } };
    std::vector<int> fileNotes;
    const auto p = patternFromMidi (c, 9, "line", &fileNotes);
    CHECK (p.bars == 1);
    REQUIRE (p.notes.size() == 4);
    CHECK ((p.notes[0].start == 0 && p.notes[0].end == 48 && p.notes[0].offset == 0 && p.notes[0].velocity == 100));
    CHECK ((p.notes[1].start == 96 && p.notes[1].offset == 3));
    CHECK ((p.notes[2].start == 192 && p.notes[2].end == 288 && p.notes[2].offset == 7));
    CHECK (p.notes[3].offset == 10);
    CHECK (fileNotes == std::vector<int> { 0, 3, 7, 10 });

    // Melody mode: any key from C3 to B3 plays the file at its real pitches and velocities
    auto s = settings (ArpOrder::Melody);
    s.key = 9;
    for (int held : { 60, 64, 69, 71 })
    {
        const auto out = render (p, { held }, s);
        CHECK (pitches (out) == std::vector<int> { 69, 72, 76, 79 });
        REQUIRE (out.size() == 4);
        CHECK (out[1].vel == 90);
    }
    // a key in octave 2 plays it an octave down; a chord doesn't scramble it
    CHECK (pitches (render (p, { 50 }, s)) == std::vector<int> { 57, 60, 64, 67 });
    CHECK (pitches (render (p, { 64, 67, 71 }, s)) == std::vector<int> { 69, 72, 76, 79 });
}

TEST_CASE ("Key detection", "[import]")
{
    auto clipOf = [] (std::initializer_list<int> notes)
    {
        MidiClip c;
        double t = 0;
        for (int n : notes) { c.notes.push_back ({ n, 100, 1, t, t + 48 }); t += 48; }
        return c;
    };
    int key = -1, scale = -1;
    detectKey (clipOf ({ 69, 72, 76, 74, 72, 71, 69, 67, 69, 64, 69 }), key, scale);   // A minor line
    CHECK (key == 9);
    CHECK (scale == NaturalMinor);
    detectKey (clipOf ({ 67, 71, 74, 72, 71, 69, 67, 66, 67, 62, 67 }), key, scale);   // G major line
    CHECK (key == 7);
    CHECK (scale == Major);
    detectKey (clipOf ({ 67, 70, 74, 72, 70, 69, 67, 65, 67, 62, 67 }), key, scale);   // G minor line
    CHECK (key == 7);
    CHECK (scale == NaturalMinor);
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

TEST_CASE ("Auto Play: plays the key's root with no MIDI note, only while the host plays", "[arp][auto]")
{
    auto s = settings (ArpOrder::Melody);
    s.key = 7;   // G
    const auto p = steps ({ 0, 3, 7 });
    CHECK (render (p, {}, s).empty());          // off: nothing without a note
    s.autoPlay = true;
    CHECK (pitches (render (p, {}, s)) == std::vector<int> { 67, 70, 74 });   // from G3
    s.order = ArpOrder::Up;
    CHECK (pitches (render (p, {}, s)) == std::vector<int> { 67, 70, 74 });

    // stopped host: silent even with Auto Play
    ArpProcessor proc;
    const auto pp = makePlayPattern (p, nullptr, 0, 0, false);
    TransportInfo t;
    t.sampleRate = 48000;
    std::vector<Event> out;
    for (int i = 0; i < 20; ++i)
        proc.process (t, 512, s, &pp, {}, out);
    CHECK (out.empty());

    // a played key takes over from the ghost note
    s.order = ArpOrder::Up;
    CHECK (pitches (render (p, { 50 }, s)) == std::vector<int> { 50, 53, 57 });
}

TEST_CASE ("Changing the length copies the pattern into the new bars", "[edit]")
{
    Pattern p;
    p.notes = { { 0, 24, 0, 100 }, { 96, 120, 3, 90 }, { 360, 400, 7, 80 } };   // the last note crosses the bar line
    auto two = p;
    resizePattern (two, 2);
    CHECK (two.bars == 2);
    REQUIRE (two.notes.size() == 6);
    CHECK ((two.notes[3].start == 384 && two.notes[3].offset == 0));
    CHECK ((two.notes[4].start == 480 && two.notes[4].offset == 3 && two.notes[4].velocity == 90));
    CHECK ((two.notes[5].start == 744 && two.notes[5].end == 768));   // cut at the new end

    auto four = two;
    resizePattern (four, 4);
    CHECK (four.notes.size() == 12);
    CHECK (four.notes[6].start == 768);

    auto back = four;
    resizePattern (back, 1);
    CHECK (back.notes.size() == 3);
    CHECK (back.notes[2].end == 384);
}

TEST_CASE ("Library phrase rows become chord notes of the key", "[import]")
{
    Phrase ph;
    ph.notes = { { 50, 0, 100, 0, 24 }, { 51, 0, 100, 0, 24 }, { 52, 0, 100, 0, 24 }, { 61, 0, 90, 96, 120 } };
    const auto am = ScaleContext::make (NaturalMinor, 9);
    const auto p = patternFromPhrase (ph, &am, 9);
    REQUIRE (p.notes.size() == 4);
    CHECK (p.notes[0].offset == 0);    // A
    CHECK (p.notes[1].offset == 3);    // C
    CHECK (p.notes[2].offset == 7);    // E
    CHECK (p.notes[3].offset == 15);   // C an octave up
}
