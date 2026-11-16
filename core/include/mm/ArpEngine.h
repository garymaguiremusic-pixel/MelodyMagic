#pragma once

#include "Rng.h"
#include "Scales.h"
#include "Types.h"

#include <array>
#include <vector>

namespace mm
{
    /** A held host note, as kept in the chord list. */
    struct ChordNote
    {
        int note = 60;
        int vel = 100;
        int channel = 1;
        int noteOffset = 0;
    };

    struct EngineSettings
    {
        bool staticMode = false;            // Static_Mode
        bool ignoreHostVelocities = false;  // Ignore_Host_Velocities
        int randomVel = 0;                  // Random_Vel 0..127
        ScaleContext scale = ScaleContext::make (Major, 0);
        bool scaleLock = false;             // quantise every output note to the scale
    };

    /**
        The phrase arpeggiator (ANALYSIS.md section 3).

        Each block: host note events + this block's phrase events (fromPhrase, note = Number)
        are merged by time (stable: host before phrase at equal times), then run through the
        Free engine (transport running, Static_Mode off) or the Static engine (otherwise), then
        zero-length note pairs are removed.

        Not thread safe: call from one thread (the audio thread).
    */
    class ArpEngine
    {
    public:
        explicit ArpEngine (Rng* rng = nullptr);

        void setRng (Rng* r) noexcept   { rng = r; }

        /** Processes one block. 'out' receives MIDI note events (time order). */
        void processBlock (std::vector<Event> hostEvents, std::vector<Event> phraseEvents,
                           bool transportRunning, bool atPositionZero,
                           const EngineSettings&, std::vector<Event>& out);

        /** Note-offs at 'time' for every phrase note currently sounding (keeps the chord
            and the active phrase list, so sounding notes simply stop). */
        void silence (int time, std::vector<Event>& out);

        /** Forgets everything (chord, active phrase notes, latches) after sending note-offs. */
        void reset (int time, std::vector<Event>& out);

        const std::vector<ChordNote>& getChord() const noexcept   { return chord; }
        const std::vector<int>& getScaleState() const noexcept   { return state; }

        //==============================================================================
        // Pure helpers (exposed for tests)

        /** Output pitch of a phrase note, or -1 when silent (row >= chord size) or out of
            range (< 0 or > 127). Scale Lock is applied when enabled. */
        static int pitchOf (int number, int noteOffset, const std::vector<ChordNote>& chord,
                            const std::vector<int>& state, const EngineSettings&);

        /** Velocity of a phrase note (consumes the RNG when Random_Vel > 0). */
        static int velocityOf (int phraseVel, int hostVel, const EngineSettings&, Rng*);

        /** Round half to even (the original's +6755399441055744.0 trick). */
        static int roundHalfEven (double v) noexcept;

        /** Removes note-on / note-off pairs with the same note and the same time. */
        static void removeZeroLength (std::vector<Event>& events);

    private:
        struct Active
        {
            int number = 50, noteOffset = 0, vel = 100;
            int pitch = -1, channel = 1;   // what this note sent (Free engine), -1 = silent
        };
        struct Slot
        {
            bool set = false;
            int pitch = -1, channel = 1;
        };

        void runFree (std::vector<Event>& events, const EngineSettings&, std::vector<Event>& out);
        void runStatic (std::vector<Event>& events, const EngineSettings&, std::vector<Event>& out);
        void applyHost (const Event&);
        void recomputeState (const EngineSettings&);
        Active* findActive (int number, int noteOffset);
        void removeActive (int number, int noteOffset);
        void emitOn (int time, Active&, const EngineSettings&, std::vector<Event>& out);
        void flushSounding (int time, std::vector<Event>& out);

        Rng* rng = nullptr;
        std::vector<ChordNote> chord;
        std::vector<int> state;
        std::vector<Active> active;
        std::array<Slot, 50> slots {};   // Static engine latches, Numbers 30..79
        int lastEngine = -1;             // 0 = Free, 1 = Static
    };
}
