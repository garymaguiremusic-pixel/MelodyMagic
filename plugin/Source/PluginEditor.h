#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Shared/CustomLookAndFeel.h"
#include "Shared/VirusComponents.h"
#include "UI/DragMidiHandle.h"
#include "UI/GeneratorPanel.h"
#include "UI/LibraryPanel.h"
#include "UI/MidiMonitor.h"
#include "UI/PhraseGrid.h"

//==============================================================================
/** The whole user interface, laid out at its base size (1000 x 680). The editor
    below scales it to the window size. */
class MelodyMagicPanel : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1000, kBaseHeight = 680;

    explicit MelodyMagicPanel (MelodyMagicProcessor&);
    ~MelodyMagicPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void setupCombo (juce::ComboBox&, const juce::StringArray& items, const juce::String& tooltip);
    void attachCombo (juce::ComboBox&, const char* paramId);
    void attachToggle (juce::TextButton&, const char* paramId, const juce::String& tooltip);
    void attachKnob (VirusKnob&, const char* paramId, std::function<juce::String (double)> describe);
    void setTab (int);
    void updateFromModel();
    void updateSelectionInfo();
    void updateCustomKeys();
    void editPhrase (const std::function<void (mm::Phrase&)>&);
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);

    MelodyMagicProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    // Top bar
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, loadButton { "Load" }, saveButton { "Save" }, bypassButton { "Bypass" };
    DragMidiHandle dragHandle;

    // Phrase panel
    juce::ComboBox lanesBox, rowsBox, lengthBox, snapBox;
    juce::TextButton offsetDown { "-" }, offsetUp { "+" }, clearButton { "Clear" };
    LcdDisplay selectionLcd;
    PhraseGrid grid;

    // Tabs: generators and library
    std::array<juce::TextButton, 4> tabs;
    GeneratorPanel generatorPanel;
    LibraryPanel libraryPanel;
    int currentTab = -1;

    // Arp
    juce::ComboBox modeBox, speedBox;
    juce::TextButton syncButton { "Int Sync" }, ignoreVelButton { "Fixed Vel" };
    VirusKnob randomVelKnob { VirusKnob::Size::Small, "Rnd Vel" };

    // Scale lock
    juce::ComboBox keyBox, scaleBox;
    juce::TextButton scaleLockButton { "Scale Lock" };
    std::array<juce::TextButton, 12> customKeys;

    // Groove
    LcdDisplay grooveLcd;
    VirusKnob qStartKnob { VirusKnob::Size::Small, "Q Start" };
    VirusKnob qEndKnob   { VirusKnob::Size::Small, "Q End" };
    juce::TextButton qLinkButton { "Link" };

    // MIDI
    juce::ComboBox inChannelBox, outChannelBox;
    juce::TextButton keyMapButton { "Key Map" };
    LcdDisplay keyMapLcd;

    // Export (Drag MIDI)
    juce::ComboBox exportChordsBox, exportBarsBox;
    LcdDisplay exportLcd;

    MidiMonitor monitor;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;

    // Attachments (declared after the controls so they are destroyed first)
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<float> phrasePanel, arpPanel, scalePanel, groovePanel, midiPanel, exportPanel;
    int lastScaleIndex = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyMagicPanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 60 % .. 200 %); remembers its size. */
class MelodyMagicEditor : public juce::AudioProcessorEditor
{
public:
    explicit MelodyMagicEditor (MelodyMagicProcessor&);
    void resized() override;

private:
    MelodyMagicProcessor& processor;
    MelodyMagicPanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyMagicEditor)
};
