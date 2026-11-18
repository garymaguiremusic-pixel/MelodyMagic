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

TEST_CASE ("MIDI import reads notes only: no CC, pitch bend, aftertouch or program change", "[midi][import]")
{
    // format 0, 96 PPQ: CC1, CC64, program change, pitch bend, aftertouch, SysEx and two notes
    std::vector<std::uint8_t> trk {
        0x00, 0xff, 0x03, 0x04, 'L', 'e', 'a', 'd',
        0x00, 0xb0, 0x01, 0x40,             // CC1
        0x00, 0xb0, 0x40, 0x7f,             // sustain on
        0x00, 0xc0, 0x05,                   // program change
        0x00, 0xe0, 0x00, 0x50,             // pitch bend
        0x00, 0xf0, 0x03, 0x7e, 0x00, 0xf7, // SysEx
        0x00, 0x90, 0x3c, 0x64,             // C4 on
        0x30, 0xd0, 0x40,                   // channel pressure
        0x00, 0xa0, 0x3c, 0x30,             // poly aftertouch
        0x00, 0x80, 0x3c, 0x00,             // C4 off (tick 48)
        0x00, 0x90, 0x40, 0x50,             // E4 on
        0x30, 0x90, 0x40, 0x00,             // E4 off (velocity-0 note-on)
        0x00, 0xb0, 0x07, 0x10,             // CC7
        0x00, 0xff, 0x2f, 0x00 };
    std::vector<std::uint8_t> file { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k',
                                     0, 0, 0, (std::uint8_t) trk.size() };
    file.insert (file.end(), trk.begin(), trk.end());

    MidiClip c;
    REQUIRE (readMidi (file, c));
    CHECK (c.trackName == "Lead");
    REQUIRE (c.notes.size() == 2);
    CHECK ((c.notes[0].note == 60 && c.notes[0].start == 0 && c.notes[0].end == 48 && c.notes[0].vel == 100));
    CHECK ((c.notes[1].note == 64 && c.notes[1].start == 48 && c.notes[1].end == 96 && c.notes[1].vel == 80));

    // what MelodyMagic writes is notes only too
    const auto bytes = writeMidi (c, "x");
    for (size_t i = 0; i + 1 < bytes.size(); ++i)
        CHECK (((bytes[i] & 0xf0) != 0xb0 || bytes[i] == 0xff || i < 22));
}
