#include <catch2/catch_test_macros.hpp>

#include "mm/Progressions.h"
#include "mm/Scales.h"

using namespace mm;

namespace
{
    // Original table (1 = in scale, C..B) from ANALYSIS.md 3.8
    std::uint16_t fromString (const char* bits)
    {
        std::uint16_t m = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (bits[pc] == '1')
                m = (std::uint16_t) (m | (1u << pc));
        return m;
    }
}

TEST_CASE ("Scale masks match the original plugin's table", "[scales]")
{
    CHECK (scaleMask (Chromatic)        == fromString ("111111111111"));
    CHECK (scaleMask (Major)            == fromString ("101011010101"));
    CHECK (scaleMask (NaturalMinor)     == fromString ("101101011010"));
    CHECK (scaleMask (HarmonicMinor)    == fromString ("101101011001"));
    CHECK (scaleMask (MelodicMinor)     == fromString ("101101010101"));
    CHECK (scaleMask (MajorPentatonic)  == fromString ("101010010100"));
    CHECK (scaleMask (MinorPentatonic)  == fromString ("100101010010"));
    CHECK (scaleMask (Mixolydian)       == fromString ("101011010110"));
    CHECK (scaleMask (Dorian)           == fromString ("101101010110"));
    CHECK (scaleMask (Phrygian)         == fromString ("110101011010"));
    CHECK (scaleMask (PhrygianDominant) == fromString ("110011011010"));
    CHECK (scaleMask (Lydian)           == fromString ("101010110101"));
    CHECK (scaleMask (Locrian)          == fromString ("110101101010"));
    CHECK (scaleMask (Persian)          == fromString ("110011101001"));
    CHECK (scaleMask (Blues)            == fromString ("100101110010"));
}

TEST_CASE ("ArpSampler scale names and order are kept", "[scales]")
{
    CHECK (std::string (scaleName (0)) == "Major");
    CHECK (std::string (scaleName (1)) == "Natural Minor");
    CHECK (std::string (scaleName (13)) == "Hungarian Minor");
    CHECK (std::string (scaleName (15)) == "Japanese (In Sen)");
    CHECK (std::string (scaleName (Custom)) == "Custom");
}

TEST_CASE ("The key rotates the mask; Custom ignores the key", "[scales]")
{
    const auto g = ScaleContext::make (Major, 7);   // G major: F#
    CHECK (g.inScale (66));
    CHECK_FALSE (g.inScale (65));
    CHECK (g.notesPerOctave == 7);

    const auto custom = ScaleContext::make (Custom, 5, 0b000010010001);   // C E G
    CHECK (custom.inScale (60));
    CHECK (custom.inScale (64));
    CHECK (custom.inScale (67));
    CHECK_FALSE (custom.inScale (65));
    CHECK (custom.notesPerOctave == 3);

    const auto empty = ScaleContext::make (Custom, 0, 0);
    CHECK (empty.notesPerOctave == 12);
}

TEST_CASE ("SnapToScale with the original's clamps", "[scales]")
{
    const auto c = ScaleContext::make (Major, 0);
    CHECK (c.snap (61, true) == 62);
    CHECK (c.snap (61, false) == 60);
    CHECK (c.snap (60, true) == 60);
    CHECK (c.snap (70, true) == 71);
    CHECK (c.snap (-5, false) == 0);        // below 0: lowest in-scale pc
    CHECK (c.snap (125, true) == 108 + 11); // above 119: 108 + highest in-scale pc

    const auto fs = ScaleContext::make (Major, 6);   // F# major: no C, B is the highest pc... (A#, B)
    CHECK (fs.snap (0, false) == fs.lowestPc);
}

TEST_CASE ("Scale state counts scale steps from the bass", "[scales]")
{
    const auto c = ScaleContext::make (Major, 0);
    CHECK (computeScaleState ({ 60, 64, 67 }, c) == std::vector<int> { 0, 2, 4 });
    CHECK (computeScaleState ({ 60, 64, 67, 72 }, c) == std::vector<int> { 0, 2, 4, 7 });
    CHECK (computeScaleState ({ 60, 60, 64 }, c) == std::vector<int> { 0, 2 });
    CHECK (computeScaleState ({ 48, 76 }, c) == std::vector<int> { 0, 7 * 2 + 2 });
    CHECK (computeScaleState ({}, c).empty());
}

TEST_CASE ("Scale Lock quantise: nearest, ties go down", "[scales][scalelock]")
{
    const auto c = ScaleContext::make (Major, 0);
    CHECK (c.quantise (61) == 60);
    CHECK (c.quantise (63) == 62);
    CHECK (c.quantise (66) == 65);
    CHECK (c.quantise (64) == 64);
    CHECK (c.quantise (127) == 127);
    const auto wt = ScaleContext::make (WholeTone, 0);
    CHECK (wt.quantise (61) == 60);
    CHECK (wt.quantise (127) == 126);
}

TEST_CASE ("Diatonic chords for MIDI export", "[scales][export]")
{
    const auto c = ScaleContext::make (Major, 0);
    CHECK (diatonicChord (c, 60, 0, 3) == std::vector<int> { 60, 64, 67 });
    CHECK (diatonicChord (c, 60, 5, 3) == std::vector<int> { 69, 72, 76 });   // vi = A minor
    CHECK (diatonicChord (c, 60, 4, 4) == std::vector<int> { 67, 71, 74, 77 }); // V7

    const auto a = ScaleContext::make (NaturalMinor, 9);
    CHECK (diatonicChord (a, 57, 0, 3) == std::vector<int> { 57, 60, 64 });

    const auto prog = buildProgression (1, c, 60, 4, 3);
    REQUIRE (prog.size() == 12);
    CHECK (prog[3].note == 67);       // bar 2: V = G B D
    CHECK (prog[3].start == 384.0);
}
