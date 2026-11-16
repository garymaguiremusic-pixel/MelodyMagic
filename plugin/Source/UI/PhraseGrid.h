#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
    Phrase editor: octave lanes x rows (chord tones) against time, with a velocity lane.
    Number = lane * 10 + row; the centre lane (5) plays the chord as held.
      click empty = add, drag = move, drag the right edge = length,
      right-click / double-click = delete, mouse wheel over a note = Note_Offset,
      Alt = no snap. Drag in the velocity lane to set velocities.
*/
class PhraseGrid : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    explicit PhraseGrid (MelodyMagicProcessor&);

    void setSnapTicks (int ticks)               { snap = juce::jmax (1, ticks); }
    int getSelected() const noexcept            { return selected; }
    void clearSelection()                       { selected = -1; repaint(); if (onSelectionChanged) onSelectionChanged(); }
    /** Changes the selected note's Note_Offset (or every note's when none is selected). */
    void nudgeOffset (int delta);
    void modelChanged();

    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Move, Resize, Velocity };

    void timerCallback() override;
    void computeLanes();
    int numRows() const noexcept                 { return (int) lanes.size() * rowsPerLane; }
    int numberForRowIndex (int index) const noexcept;
    int rowIndexForNumber (int number) const noexcept;
    float xForTick (double tick) const noexcept;
    double tickForX (float x) const noexcept;
    int rowIndexForY (float y) const noexcept;
    juce::Rectangle<float> noteRect (const mm::PhraseNote&) const;
    int noteAt (juce::Point<float>) const;
    int velocityNoteAt (float x) const;
    void setVelocityFromY (int index, float y);
    void ensureEditStarted();
    void commit (mm::Phrase&);

    MelodyMagicProcessor& proc;
    juce::Rectangle<float> rulerArea, labelArea, gridArea, velArea;
    std::vector<int> lanes;   // top to bottom
    int rowsPerLane = 3;
    int length = mm::kTicksPerBar;
    int snap = mm::kPhrasePpq / 4;
    int selected = -1;

    Drag drag = Drag::None;
    bool editStarted = false;
    mm::PhraseNote dragOriginal;
    double dragStartTick = 0.0;
    int dragStartRow = 0;
    double playhead = -1.0;
};
