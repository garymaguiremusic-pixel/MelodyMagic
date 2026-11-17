#pragma once

#include "MidiFile.h"
#include "Rng.h"
#include "Scales.h"
#include "Types.h"

#include <string>
#include <vector>

namespace mm
{
    /**
        One note of a pattern. 'offset' is in semitones: added to the key the arp is
        playing (with one key held, offset 0 = that key). Ticks at 96 PPQ.
    */
    struct PatternNote
    {
        int start = 0;
        int end = 24;
        int offset = 0;
        int velocity = 100;

        bool operator== (const PatternNote& o) const noexcept
        {
            return start == o.start && end == o.end && offset == o.offset && velocity == o.velocity;
        }
    };

    struct Pattern
    {
        std::string name = "Init";
        int bars = 1;                      // 1, 2, 4 or 8
        std::vector<PatternNote> notes;    // sorted by start, then offset

        int lengthTicks() const noexcept   { return bars * kTicksPerBar; }
        void sort();

        bool operator== (const Pattern& o) const noexcept   { return name == o.name && bars == o.bars && notes == o.notes; }
    };

    constexpr int kMaxOffset = 36;     // offsets stay within -36..+36 semitones
    constexpr int kMaxBars = 8;

    //==============================================================================
    // Arp order
    enum class ArpOrder { Up = 0, Down, UpDown, DownUp };
    constexpr int kNumArpOrders = 4;
    const char* arpOrderName (ArpOrder);

    /** Indices into the held keys (sorted low to high), one cycle of the order. The turn notes
        of Up & Down / Down & Up are not repeated: 4 keys Up & Down = 0 1 2 3 2 1. */
    std::vector<int> arpSequence (int numKeys, ArpOrder);

    //==============================================================================
    // Editing helpers

    /** The offset one semitone up or down. With a scale, the next offset whose note
        (key + offset) is in the scale. Kept within +-kMaxOffset. */
    int stepOffset (int offset, int direction, const ScaleContext* scale, int key);

    /** Rounds bars to 1, 2, 4 or 8. */
    int legalBars (int bars);

    //==============================================================================
    // Shuffle

    /** Keeps the rhythm and moves the given notes (offsets) to new places: every note of
        'pool' is used, as many times as it appears (repeating the pool if the pattern is longer). */
    void shuffleNotes (Pattern&, const std::vector<int>& pool, Rng&);

    //==============================================================================
    // Import

    /** A MIDI clip (any PPQ) as a pattern: up to kMaxBars, offsets relative to the key's root in
        the octave at or below the clip's lowest note. 'fileNotes' receives every note's offset. */
    Pattern patternFromMidi (const MidiClip&, int key, const std::string& name, std::vector<int>* fileNotes = nullptr);

    /** A library phrase as a pattern: its rhythm and velocities, octave lanes as +-12. */
    Pattern patternFromPhrase (const Phrase&);
}
