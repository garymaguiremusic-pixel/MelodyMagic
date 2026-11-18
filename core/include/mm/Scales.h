#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mm
{
    /**
        Scale list. The first 16 match ArpSampler (same names, same order), then Persian,
        Chromatic and Custom (a user mask that ignores the key).
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
        int notesPerOctave = 12;
        std::array<int, 12> up {};    // nearest in-scale pitch >= pc (may be >= 12)
        std::array<int, 12> down {};  // nearest in-scale pitch <= pc (may be < 0)
        int lowestPc = 0, highestPc = 11;

        /** customMask: bit n = pitch class n (C = 0), used only for ScaleIndex::Custom. */
        static ScaleContext make (int scaleIndex, int key, std::uint16_t customMask = 0xfff) noexcept;
        static ScaleContext fromAbsoluteMask (std::uint16_t absMask) noexcept;

        bool inScale (int pitch) const noexcept   { return in[(size_t) floorMod (pitch, 12)]; }

        /** Nearest in-scale pitch at/above (up) or at/below (down); results below 0 give the
            lowest in-scale pitch class, above 119 give 108 + the highest. */
        int snap (int pitch, bool upwards) const noexcept;

        /** Scale Lock: nearest in-scale note (ties go down), kept within 0..127. */
        int quantise (int note) const noexcept;

        /** Scale degree 'steps' above 'root' (root is snapped down into the scale first). */
        int stepsAbove (int root, int steps) const noexcept;
    };

    /**
        Moves a note from one key / scale to another, keeping the melody's shape (as ArpSampler does):
        the key moves by the shortest step (-5..+6 semitones), and a note on scale degree n of the
        old scale lands on degree n of the new one (spread proportionally when the scales have a
        different number of notes). Notes outside the old scale just move with the key. 0..127.
    */
    int transposeBetweenKeys (int note, const ScaleContext& from, int fromKey, const ScaleContext& to, int toKey) noexcept;

}
