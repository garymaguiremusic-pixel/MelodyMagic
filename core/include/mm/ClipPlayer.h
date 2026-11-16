#pragma once

#include "Formats.h"
#include "Types.h"

#include <vector>

namespace mm
{
    /** A phrase note ready for playback (after the quantizer), ticks at 96 PPQ. */
    struct PlayNote
    {
        int number = 50, noteOffset = 0, vel = 100;
        double start = 0.0, end = 24.0;
    };

    struct PlayPhrase
    {
        std::vector<PlayNote> notes;
        double length = kTicksPerBar;
    };

    //==============================================================================
    // Quantizer (ANALYSIS.md 4.2 - inferred; keep the rule in these two functions)

    /** Nearest grid line to tick t (96 PPQ). Grid = groove ticks * 96 / Global_PPQ, repeated every Total_PPQ. */
    double nearestGridTick (double t, const Groove&);

    /** t' = t + strength * (nearestGrid(t) - t). */
    double quantizeTick (double t, double strength, const Groove&);

    /**
        Builds the playback phrase: each note's start moves by 'startStrength' (0..1) and its
        end by 'endStrength' towards the nearest grid line ('link' uses the start strength for
        both). Ends never come before start + 1 tick. Notes are clipped to the phrase length.
    */
    PlayPhrase makePlayPhrase (const Phrase&, const Groove* groove, double startStrength, double endStrength, bool link);

    //==============================================================================
    /**
        Plays a looping phrase against a tick position and produces phrase events
        (fromPhrase = true, note = Number, vel = phrase velocity) at sample offsets.
    */
    class ClipPlayer
    {
    public:
        /** Window [startTick, startTick + numSamples * ticksPerSample) in unwrapped phrase ticks.
            A jump in position (seek, loop, speed change) stops the sounding notes first. */
        void process (const PlayPhrase&, double startTick, double ticksPerSample, int numSamples,
                      int timeOffset, std::vector<Event>& out);

        /** Note-offs for every sounding phrase note. */
        void stopAll (int time, std::vector<Event>& out);

        /** Forget the expected position, so the next block is not treated as a jump. */
        void resetPosition() noexcept   { hasExpected = false; }

        bool isSounding() const noexcept { return ! sounding.empty(); }

    private:
        struct Sounding { int index, number, noteOffset; };
        std::vector<Sounding> sounding;
        double expected = 0.0;
        bool hasExpected = false;
    };
}
