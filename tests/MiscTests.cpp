#include <catch2/catch_test_macros.hpp>

#include "mm/ArpEngine.h"
#include "mm/Generators.h"
#include "mm/Json.h"
#include "mm/MidiFile.h"
#include "mm/Rng.h"

#include <cmath>

using namespace mm;

TEST_CASE ("JUCE-compatible LCG sequence", "[rng]")
{
    // Reference values from the juce::Random formula (verified independently in Python).
    JuceLcg r (12345);
    const int expected[] = { 454757875, -866467965, 68972694, -1442951559, 184400468, -22376371, -664766094, 935117265 };
    for (int v : expected)
        CHECK (r.nextInt() == v);
}

TEST_CASE ("Seeded random velocity sequence (snapshot)", "[rng][engine][golden]")
{
    JuceLcg r (42);
    EngineSettings s;
    s.randomVel = 20;
    s.ignoreHostVelocities = true;
    std::vector<int> got;
    for (int i = 0; i < 12; ++i)
        got.push_back (ArpEngine::velocityOf (100, 127, s, &r));
    CHECK (got == std::vector<int> { 100, 82, 92, 96, 119, 81, 88, 92, 82, 92, 114, 114 });

    // clamping at the ends
    JuceLcg r2 (7);
    s.randomVel = 127;
    for (int i = 0; i < 200; ++i)
    {
        const int v = ArpEngine::velocityOf (120, 127, s, &r2);
        CHECK (v >= 0);
        CHECK (v <= 127);
    }
}

TEST_CASE ("MIDI files round-trip", "[midi]")
{
    MidiClip c;
    c.ppq = 96;
    c.bpm = 128.0;
    c.notes = { { 60, 100, 1, 0, 96 }, { 64, 90, 2, 0, 48 }, { 67, 80, 1, 96, 192 }, { 60, 70, 1, 96, 100 } };
    MidiClip back;
    REQUIRE (readMidi (writeMidi (c, "Test"), back));
    CHECK (back.ppq == 96);
    CHECK (std::abs (back.bpm - 128.0) < 0.01);
    REQUIRE (back.notes.size() == 4);
    CHECK ((back.notes[0].note == 60 && back.notes[0].end == 96));
    CHECK ((back.notes[1].note == 64 && back.notes[1].channel == 2));
    CHECK ((back.notes[2].note == 60 && back.notes[2].start == 96 && back.notes[2].end == 100));
    CHECK (back.rescaled (192).notes[3].end == 384);
}

TEST_CASE ("JSON parser", "[json]")
{
    const auto v = json::parse (R"({"a":[1,2.5,-3e2],"s":"x\"yé","t":true,"n":null,"o":{"k":"v"}})");
    CHECK (v["a"].a.size() == 3);
    CHECK (v["a"].a[2].asInt() == -300);
    CHECK (v["s"].asString() == "x\"y\xc3\xa9");
    CHECK (v["t"].asBool());
    CHECK (v["n"].isNull());
    CHECK (v["o"]["k"].asString() == "v");
    CHECK (v["missing"].asInt (7) == 7);
    CHECK_THROWS (json::parse ("{\"a\":}"));
}

//==============================================================================
TEST_CASE ("Generators are deterministic for a seed and stay inside the layout", "[generators]")
{
    for (int k = 0; k < gen::kNumKinds; ++k)
        CHECK (gen::defaults ((gen::Kind) k).size() == gen::params ((gen::Kind) k).size());

    const gen::Layout layout { 3, 3 };
    for (std::int64_t seed : { 1, 2, 3, 99 })
    {
        JuceLcg a (seed), b (seed);
        const auto v = gen::defaults (gen::Kind::Random303);
        const auto p1 = gen::generate303 (v, layout, a);
        const auto p2 = gen::generate303 (v, layout, b);
        CHECK (p1 == p2);
        CHECK (p1.lengthTicks() == 384);
        for (auto& n : p1.notes)
        {
            CHECK (n.row() < 3);
            CHECK (n.lane() >= 4);
            CHECK (n.lane() <= 6);
            CHECK (n.end > n.start);
            CHECK (n.end <= 384);
        }

        JuceLcg c (seed);
        auto vc = gen::defaults (gen::Kind::ChordBass);
        vc[(size_t) gen::indexOf (gen::Kind::ChordBass, "bars")] = 4;
        const auto cb = gen::generateChordBass (vc, layout, c);
        CHECK (cb.lengthTicks() == 4 * 384);
        for (auto& n : cb.notes)
        {
            CHECK (n.row() < 3);
            CHECK (n.end > n.start);
        }
    }
}

TEST_CASE ("303 with full density and no slide fills every step", "[generators]")
{
    auto v = gen::defaults (gen::Kind::Random303);
    v[(size_t) gen::indexOf (gen::Kind::Random303, "density")] = 100;
    v[(size_t) gen::indexOf (gen::Kind::Random303, "slide")] = 0;
    v[(size_t) gen::indexOf (gen::Kind::Random303, "bars")] = 2;
    JuceLcg r (5);
    const auto p = gen::generate303 (v, { 1, 4 }, r);
    CHECK (p.notes.size() == 32);
    for (auto& n : p.notes)
        CHECK (n.lane() == 5);   // one lane: no octave moves
}

TEST_CASE ("Chord/Bass macro repeats the motif", "[generators]")
{
    auto v = gen::defaults (gen::Kind::ChordBass);
    v[(size_t) gen::indexOf (gen::Kind::ChordBass, "bars")] = 4;
    v[(size_t) gen::indexOf (gen::Kind::ChordBass, "macro")] = 0;   // 1-bar motif
    v[(size_t) gen::indexOf (gen::Kind::ChordBass, "density")] = 80;
    JuceLcg r (11);
    const auto p = gen::generateChordBass (v, { 3, 4 }, r);
    std::vector<PhraseNote> bar0, bar2;
    for (auto& n : p.notes)
    {
        if (n.start < 384) bar0.push_back (n);
        else if (n.start >= 768 && n.start < 1152) { auto m = n; m.start -= 768; m.end -= 768; bar2.push_back (m); }
    }
    CHECK (! bar0.empty());
    CHECK (bar0 == bar2);
}

TEST_CASE ("Randomize leaves locked knobs alone", "[generators]")
{
    auto values = gen::defaults (gen::Kind::Random303);
    auto locks = gen::defaultLocks (gen::Kind::Random303);
    const int density = gen::indexOf (gen::Kind::Random303, "density");
    locks[(size_t) density] = true;
    values[(size_t) density] = 77;
    JuceLcg r (3);
    for (int i = 0; i < 20; ++i)
    {
        gen::randomize (gen::Kind::Random303, values, locks, r);
        CHECK (values[(size_t) density] == 77);
        CHECK (values[0] == 1);   // Bars is locked by default
        for (size_t k = 0; k < values.size(); ++k)
        {
            CHECK (values[k] >= gen::params (gen::Kind::Random303)[k].min);
            CHECK (values[k] <= gen::params (gen::Kind::Random303)[k].max);
        }
    }
}

TEST_CASE ("Mutate is scale aware and keeps notes inside the phrase", "[generators][scalelock]")
{
    Phrase src;
    for (int i = 0; i < 16; ++i)
        src.notes.push_back ({ 50 + (i % 3), 0, 100, i * 24, i * 24 + 12 });

    auto v = gen::defaults (gen::Kind::Mutate);
    v[(size_t) gen::indexOf (gen::Kind::Mutate, "pitchProb")] = 100;
    v[(size_t) gen::indexOf (gen::Kind::Mutate, "pitchAmt")] = 5;
    const auto scale = ScaleContext::make (MajorPentatonic, 0);
    JuceLcg r (8);
    const auto out = gen::mutate (src, v, scale, r);
    REQUIRE (! out.notes.empty());
    for (auto& n : out.notes)
    {
        CHECK (n.noteOffset != 0);
        CHECK (scale.inScale (60 + n.noteOffset));   // whole scale steps from the scale's root
        CHECK (n.start >= 0);
        CHECK (n.end <= src.lengthTicks());
    }
}

TEST_CASE ("Join overlapping notes", "[generators]")
{
    Phrase p;
    p.notes = { { 50, 0, 90, 0, 30 }, { 50, 0, 100, 20, 60 }, { 51, 0, 100, 20, 40 } };
    gen::joinOverlaps (p);
    REQUIRE (p.notes.size() == 2);
    CHECK ((p.notes[0].number == 50 && p.notes[0].start == 0 && p.notes[0].end == 60 && p.notes[0].velocity == 100));
}
