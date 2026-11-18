#include <catch2/catch_test_macros.hpp>

#include "mm/Scales.h"

using namespace mm;

namespace
{
    // Reference table (1 = in scale, C..B)
    std::uint16_t fromString (const char* bits)
    {
        std::uint16_t m = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (bits[pc] == '1')
                m = (std::uint16_t) (m | (1u << pc));
        return m;
    }
}

TEST_CASE ("Scale masks", "[scales]")
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

    const auto fs = ScaleContext::make (Major, 6);   // F# major has no C: snapping C down goes below 0
    CHECK (fs.snap (0, false) == fs.lowestPc);
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

TEST_CASE ("Moving a melody between keys keeps its shape", "[scales][import]")
{
    const auto gm = ScaleContext::make (NaturalMinor, 7), am = ScaleContext::make (NaturalMinor, 9);
    // G minor line G Bb D C -> A minor: A C E D (up a tone, same degrees)
    CHECK (transposeBetweenKeys (67, gm, 7, am, 9) == 69);
    CHECK (transposeBetweenKeys (70, gm, 7, am, 9) == 72);
    CHECK (transposeBetweenKeys (74, gm, 7, am, 9) == 76);
    CHECK (transposeBetweenKeys (72, gm, 7, am, 9) == 74);
    // F minor -> C minor: the shortest move is down a fourth (-5), not up a fifth
    const auto fm = ScaleContext::make (NaturalMinor, 5), cm = ScaleContext::make (NaturalMinor, 0);
    CHECK (transposeBetweenKeys (65, fm, 5, cm, 0) == 60);
    // A minor -> C major: degree for degree (the minor third becomes the major third)
    const auto cmaj = ScaleContext::make (Major, 0);
    CHECK (transposeBetweenKeys (69, am, 9, cmaj, 0) == 72);   // A (1) -> C (1), up a third
    CHECK (transposeBetweenKeys (72, am, 9, cmaj, 0) == 76);   // C (3) -> E (3)
    for (int n = 50; n < 80; ++n)
        if (am.inScale (n))
            CHECK (cmaj.inScale (transposeBetweenKeys (n, am, 9, cmaj, 0)));
}
