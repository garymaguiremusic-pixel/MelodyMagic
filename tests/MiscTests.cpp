#include <catch2/catch_test_macros.hpp>

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
