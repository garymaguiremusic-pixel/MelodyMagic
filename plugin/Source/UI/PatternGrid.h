#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
    Pattern editor: a small piano roll. Rows are semitones from the key you hold
    (labelled as notes of the Scale Lock key; offset 0 = the key's root).
      click = add, drag = move, drag the right edge = length, Alt = no snap,
      right-click / double-click = delete,
      mouse wheel over a note = up / down a semitone (a scale note when Scale Lock is on).
    Drop a .mid file on it to import it. Drag in the lower strip for velocity.
*/
class PatternGrid : public juce::Component,
                    public juce::SettableTooltipClient,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit PatternGrid (MelodyMagicProcessor&);

    void setSnapTicks (int ticks)               { snap = juce::jmax (1, ticks); }
    int getSelected() const noexcept            { return selected; }
    void clearSelection()                       { selected = -1; repaint(); if (onSelectionChanged) onSelectionChanged(); }
    void modelChanged();

    std::function<void()> onSelectionChanged;
    std::function<void (const juce::File&)> onFileDropped;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dropHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dropHighlight = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum class Drag { None, Move, Resize, Velocity };

    void timerCallback() override;
    void computeRange();
    int numRows() const noexcept                 { return top - bottom + 1; }
    float xForTick (double tick) const noexcept;
    double tickForX (float x) const noexcept;
    int offsetForY (float y) const noexcept;
    float rowHeight() const noexcept;
    juce::Rectangle<float> noteRect (const mm::PatternNote&) const;
    int noteAt (juce::Point<float>) const;
    int velocityNoteAt (float x) const;
    void setVelocityFromY (int index, float y);
    void ensureEditStarted();
    void commit (mm::Pattern&);
    int selectAfterSort (mm::Pattern&, const mm::PatternNote&);

    MelodyMagicProcessor& proc;
    juce::Rectangle<float> rulerArea, labelArea, gridArea, velArea;
    int top = 12, bottom = -12;   // offset range shown
    int length = mm::kTicksPerBar;
    int snap = mm::kPhrasePpq / 4;
    int selected = -1;

    Drag drag = Drag::None;
    bool editStarted = false, dropHighlight = false;
    mm::PatternNote dragOriginal;
    double dragStartTick = 0.0;
    int dragStartOffset = 0;
    double playhead = -1.0;
};
