#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mm
{
    /**
        Scale list. The first 16 match ArpSampler (same names, same order). Persian and
        Chromatic complete the original plugin's set (its Diatonic / Ionian / Aeolian /
        Spanish Gypsy entries are aliases of scales already listed). Custom uses a user
        mask and ignores the key.
    */
    enum ScaleIndex
    {
        Major = 0, NaturalMinor, HarmonicMinor, MelodicMinor, Dorian, Phrygian, Lydian, Mixolydian,
        Locrian, MajorPentatonic, MinorPentatonic, Blues, PhrygianDominant, HungarianMinor,
        WholeTone, JapaneseInSen, Persian, Chromatic, Custom,
        kNumScales
    };

    const char* scaleName (int scaleIndex) noexcept;
    const char* keyName (int key) noexcept;           // C, C#, ... B
    std::string noteName (int midiNote);               // C3 = 60 (ArpSampler convention)

    /** Bit n set = n semitones above the key is in the scale (Custom returns 0: use the user mask). */
    std::uint16_t scaleMask (int scaleIndex) noexcept;

    int floorDiv (int a, int b) noexcept;
    int floorMod (int a, int b) noexcept;

    //==============================================================================
    /** A scale placed in a key, with the lookup tables the arpeggiator needs. */
    struct ScaleContext
    {
        std::array<bool, 12> in {};   // absolute pitch classes (C = 0)
        int notesPerOctave = 12;      // N
        std::array<int, 12> up {};    // nearest in-scale pitch >= pc (may be >= 12)
        std::array<int, 12> down {};  // nearest in-scale pitch <= pc (may be < 0)
        int lowestPc = 0, highestPc = 11;

        /** customMask: bit n = pitch class n (C = 0), used only for ScaleIndex::Custom. */
        static ScaleContext make (int scaleIndex, int key, std::uint16_t customMask = 0xfff) noexcept;
        static ScaleContext fromAbsoluteMask (std::uint16_t absMask) noexcept;

        bool inScale (int pitch) const noexcept   { return in[(size_t) floorMod (pitch, 12)]; }

        /** Original SnapToScale: nearest in-scale pitch at/above (up) or at/below (down),
            with the original's clamps (< 0 -> lowest pc, > 119 -> 108 + highest pc). */
        int snap (int pitch, bool upwards) const noexcept;

        /** Scale Lock: nearest in-scale note (ties go down), kept within 0..127. */
        int quantise (int note) const noexcept;

        /** Scale degree 'steps' above 'root' (root is snapped down into the scale first). */
        int stepsAbove (int root, int steps) const noexcept;
    };

    /**
        Per unique chord pitch p0 < p1 < ...: state[0] = 0 and
        state[i] = N * ((pi - p0) / 12) + |{ x in (p0, p0 + (pi - p0) % 12] : inScale(x) }|.
        'sortedPitches' may contain duplicates; they are skipped.
    */
    std::vector<int> computeScaleState (const std::vector<int>& sortedPitches, const ScaleContext&);

    /**
        Diatonic Note_Offset (original FUN_1800040e0): move the bass by 'offset' semitones
        (snapped into the scale in the offset's direction), then rebuild the chord shape
        diatonically: k = state[row] scale steps above that new root.
    */
    int diatonicShift (int bassPlusOctave, int offset, int k, const ScaleContext&) noexcept;
}
