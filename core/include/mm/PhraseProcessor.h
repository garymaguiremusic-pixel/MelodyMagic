#pragma once

#include "ArpEngine.h"
#include "ClipPlayer.h"
#include "Formats.h"

#include <array>
#include <map>
#include <vector>

namespace mm
{
    /** Channel filter values: 0 = All, -1 = Off, 1..16 = that channel. */
    struct CoreSettings
    {
        EngineSettings engine;
        double speed = 1.0;              // Speed
        bool forceInternalSync = false;  // Force_Internal_Sync
        bool bypassed = false;           // Bypassed
        bool keyMapEnabled = false;
        int inputChannel = 0;            // Midi_Input_Channel_Filter
        int outputChannel = 0;           // Midi_Output_Channel_Filter
    };

    struct TransportInfo
    {
        bool playing = false;
        bool hasPosition = false;
        double ppqPosition = 0.0;   // quarter notes
        double bpm = 120.0;
        double sampleRate = 44100.0;
    };

    /** Counts sounding output notes so they can always be switched off. */
    class OutputTracker
    {
    public:
        void record (const Event&) noexcept;
        void flush (int time, std::vector<Event>& out);
        bool anySounding() const noexcept;

    private:
        std::array<std::array<std::uint8_t, 128>, 16> counts {};
    };

    /**
        Everything between the host's MIDI input and output for one block: channel filters,
        the optional key map, the clip player (host sync or internal sync), the arpeggiator,
        bypass and stuck-note protection. The plugin and the offline renderer both use it.
    */
    class PhraseProcessor
    {
    public:
        explicit PhraseProcessor (Rng* rng = nullptr);
        void setRng (Rng* r) noexcept   { engine.setRng (r); }

        /** hostIn: note events (time = sample offset, channel 1..16), sorted by time.
            'phrase' and 'keyMap' may be null. Output: note events sorted by time. */
        void process (const TransportInfo&, int numSamples, const CoreSettings&,
                      const PlayPhrase* phrase, const KeyMap* keyMap,
                      const std::vector<Event>& hostIn, std::vector<Event>& out);

        /** All notes off (keeps the chord). */
        void panic (int time, std::vector<Event>& out);

        /** Current phrase position in ticks (for the editor's playhead), -1 when stopped. */
        double getPhrasePosition() const noexcept    { return phrasePosition; }
        const std::vector<ChordNote>& getChord() const noexcept { return engine.getChord(); }

    private:
        ArpEngine engine;
        ClipPlayer clip;
        OutputTracker tracker;
        std::map<int, std::vector<int>> expansions;   // (channel*128 + key) -> notes sent into the chord
        int heldKeys = 0;
        double internalTick = 0.0;
        double phrasePosition = -1.0;
        bool wasPlaying = false, wasBypassed = false, wasInternal = false;
        int lastOutputChannel = 0;
        const PlayPhrase* lastPhrase = nullptr;
    };

    //==============================================================================
    /** Offline render: the same processing as the plugin, with the transport running from 0. */
    struct RenderOptions
    {
        CoreSettings settings;
        double bpm = 120.0;
        double lengthTicks = 0.0;   // 0 = longest of the input and the phrase, rounded up to bars
        double sampleRate = 48000.0;
        int blockSize = 256;
    };

    /** input / result: notes in 96-PPQ ticks. */
    std::vector<TimedNote> renderOffline (const PlayPhrase&, const KeyMap* keyMap, const std::vector<TimedNote>& input,
                                          const RenderOptions&, Rng* rng);
}
