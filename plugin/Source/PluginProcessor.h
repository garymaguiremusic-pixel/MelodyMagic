#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "Parameters.h"
#include "mm/Generators.h"
#include "mm/Library.h"
#include "mm/PhraseProcessor.h"

#include <array>
#include <atomic>
#include <optional>

//==============================================================================
/** Note events for the MIDI monitor (audio thread -> editor). */
struct MonitorEvent
{
    int note = 0, vel = 0, channel = 1;
    bool on = true;
};

//==============================================================================
class MelodyMagicProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,   // model changed (phrase, groove, key map, library...)
                             private juce::Timer
{
public:
    MelodyMagicProcessor();
    ~MelodyMagicProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return JucePlugin_Name; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return true; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override            { return 0.0; }

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // Phrase (message thread)
    const mm::Phrase& getPhrase() const noexcept           { return phrase; }
    const juce::String& getPhraseSource() const noexcept   { return phraseSource; }

    /** Replaces the phrase (undoable). */
    void setPhrase (const mm::Phrase&, const juce::String& source, bool pushUndo = true);
    /** Loads a phrase from the library or a file (undoable; ends any generator run). */
    void loadPhrase (const mm::Phrase& p, const juce::String& source)   { lastGenerated = -1; setPhrase (p, source, true); }
    /** Call before a series of editor changes (one undo step), then updatePhrase() as often as needed. */
    void beginPhraseEdit();
    void updatePhrase (const mm::Phrase&);

    bool canUndo() const noexcept   { return ! undoStack.empty(); }
    bool canRedo() const noexcept   { return ! redoStack.empty(); }
    void undo();
    void redo();

    bool loadPhraseFile (const juce::File&, juce::String& error);
    bool savePhraseFile (const juce::File&);

    // Groove / key map / chord clip
    void setGroove (std::optional<mm::Groove>, const juce::String& name);
    const std::optional<mm::Groove>& getGroove() const noexcept     { return groove; }
    const juce::String& getGrooveName() const noexcept              { return grooveName; }

    void setKeyMap (std::optional<mm::KeyMap>, const juce::String& name);
    const std::optional<mm::KeyMap>& getKeyMap() const noexcept     { return keyMap; }
    const juce::String& getKeyMapName() const noexcept              { return keyMapName; }

    void setChordClip (std::optional<mm::MidiClip>, const juce::String& name);
    const std::optional<mm::MidiClip>& getChordClip() const noexcept { return chordClip; }
    const juce::String& getChordClipName() const noexcept           { return chordClipName; }

    std::uint16_t getCustomMask() const noexcept    { return customMask.load(); }
    void setCustomMask (std::uint16_t);

    /** The Scale Lock key and scale as they are set now. */
    mm::ScaleContext currentScale() const;
    int currentKey() const;
    int currentScaleIndex() const;

    //==============================================================================
    // Generators
    struct GenState
    {
        std::vector<int> values;
        std::vector<bool> locks;
        std::int64_t seed = 1;
    };
    GenState& generator (mm::gen::Kind k)               { return gens[(size_t) k]; }
    void setGeneratorValue (mm::gen::Kind, int index, int value);   // re-runs the generator
    void runGenerator (mm::gen::Kind, bool newSeed);
    void run303Mutator (bool newSeed);
    void randomizeGenerator (mm::gen::Kind);
    void resetGenerator (mm::gen::Kind);
    void takeSnapshot();
    bool hasSnapshot() const noexcept                   { return snapshot.has_value(); }

    //==============================================================================
    // Drag MIDI export
    int exportChords = mmp::kExportLastChord;
    int exportBarsIndex = 3;   // 4 bars
    /** Renders the arpeggiated pattern to a .mid file in the temp folder. */
    juce::File renderMidiFile();
    juce::String describeExportChords() const;

    //==============================================================================
    // Library
    std::shared_ptr<const mm::Library> getLibrary() const;
    juce::File getLibraryFolder() const                 { return libraryFolder; }
    void setLibraryFolder (const juce::File&);
    void rescanLibrary();
    bool isLibraryLoading() const noexcept              { return libraryLoading.load(); }

    //==============================================================================
    // Editor state and live info
    std::atomic<int> editorWidth { 0 };
    int activeTab = 0;
    std::atomic<double> phrasePosition { -1.0 };   // ticks, -1 when not playing

    /** Held chord as the arpeggiator sees it (audio thread writes, editor reads). */
    std::vector<int> getHeldChord() const;
    int readMonitor (MonitorEvent* dest, int max);

private:
    void timerCallback() override;
    void rebuildPlayPhrase();
    void publishKeyMap();
    void pushUndo();
    void loadGlobalSettings();
    void saveGlobalSettings();
    mm::CoreSettings readSettings() const;

    // model
    mm::Phrase phrase;
    juce::String phraseSource;
    std::vector<mm::Phrase> undoStack, redoStack;
    std::optional<mm::Groove> groove;
    juce::String grooveName;
    std::optional<mm::KeyMap> keyMap;
    juce::String keyMapName;
    std::optional<mm::MidiClip> chordClip;
    juce::String chordClipName;
    std::atomic<std::uint16_t> customMask { 0x0fff };
    std::array<GenState, mm::gen::kNumKinds> gens;
    std::int64_t mutatorSeed = 1;
    std::optional<mm::Phrase> snapshot;
    int lastGenerated = -1;
    int lastQStart = -1, lastQEnd = -1, lastQLink = -1;

    // live objects handed to the audio thread (std::atomic_load / atomic_store)
    std::shared_ptr<const mm::PlayPhrase> livePhrase;
    std::shared_ptr<const mm::KeyMap> liveKeyMap;
    std::vector<std::shared_ptr<const void>> graveyard;   // freed on the message thread

    // audio thread
    mm::PhraseProcessor core;
    mm::JuceLcg rng;
    std::vector<mm::Event> hostEvents, outEvents;
    juce::MidiBuffer otherMidi;
    double currentSampleRate = 44100.0;
    std::atomic<double> lastBpm { 120.0 };
    std::array<std::atomic<int>, 16> heldChord {};
    std::atomic<int> heldChordCount { 0 };
    std::array<std::atomic<int>, 16> lastChord {};
    std::atomic<int> lastChordCount { 0 };

    juce::AbstractFifo monitorFifo { 1024 };
    std::array<MonitorEvent, 1024> monitorBuffer {};

    // library
    std::shared_ptr<const mm::Library> library;
    juce::File libraryFolder;
    std::atomic<bool> libraryLoading { false };
    juce::ThreadPool loaderPool { 1 };
    std::unique_ptr<juce::PropertiesFile> globalSettings;

    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MelodyMagicProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyMagicProcessor)
};
