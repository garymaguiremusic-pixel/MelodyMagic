#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../Shared/VirusComponents.h"

//==============================================================================
/**
    Knobs and buttons for one phrase generator (303 Random, Chord / Bass or Mutate).
    Knobs move in whole steps; turning one re-runs the generator with the same random
    draw, Regen makes a new draw. Right-click a knob to lock it against Randomize.
*/
class GeneratorPanel : public juce::Component
{
public:
    explicit GeneratorPanel (MelodyMagicProcessor&);

    void setKind (mm::gen::Kind);
    mm::gen::Kind getKind() const noexcept   { return kind; }
    /** Reload values and locks from the processor (after Reset, Randomize or a state load). */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebuild();

    MelodyMagicProcessor& proc;
    mm::gen::Kind kind = mm::gen::Kind::Random303;
    juce::OwnedArray<VirusKnob> knobs;
    juce::OwnedArray<juce::TextButton> toggles;
    std::vector<juce::Component*> cells;   // in parameter order
    juce::TextButton regenButton { "Regen" }, randomButton { "Randomize" }, resetButton { "Reset" },
                     mutateButton { "Mutate" }, snapshotButton { "Snapshot" };
    bool updating = false;
};
