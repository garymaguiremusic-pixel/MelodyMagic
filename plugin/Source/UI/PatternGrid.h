#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
    Pattern editor: a small piano roll. Rows are semitones from the key you hold
    (labelled as notes of the Scale Lock key; offset 0 = the key's root).
      click = add, drag = move, drag the right edge = length, Alt = no snap,
      right-click / double-click = delete,
      notes are locked to the Key + Scale (hold Alt for a free semitone),
      mouse wheel over a note = up / down to the next scale note (Alt = a semitone).
    Drop a .mid file on it to import it. Drag in the lower strip for velocity.
    Clicking it takes the keyboard focus: Up / Down move the selected note, Left / Right
    select the previous / next note, Delete removes it, Ctrl+Z / Ctrl+Y undo / redo.
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
    int numSelected() const noexcept;
    void clearSelection();
    void selectAll();
    /** Moves the selected notes (all notes when none are selected) up or down: one scale step,
        a semitone (chromatic) or an octave. */
    void moveSelection (int direction, bool chromatic, bool octave = false);
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
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override   { repaint(); }
    void focusLost (FocusChangeType) override     { repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override   { dropHighlight = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dropHighlight = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    enum class Drag { None, Move, Resize, Velocity, Marquee };

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
    bool isSelected (int i) const noexcept   { return i >= 0 && i < (int) sel.size() && sel[(size_t) i]; }
    void selectOnly (int i);
    /** Writes notes with their selection flags, sorted (the flags follow their notes). */
    void commitWithSelection (std::vector<mm::PatternNote> notes, std::vector<bool> noteFlags, int primary);
    void deleteSelection();
    int snapToKey (int offset) const;
    void ensureEditStarted();
    void commit (mm::Pattern&);
    int selectAfterSort (mm::Pattern&, const mm::PatternNote&);

    MelodyMagicProcessor& proc;
    juce::Rectangle<float> rulerArea, labelArea, gridArea, velArea;
    int top = 12, bottom = -12;   // offset range shown
    int length = mm::kTicksPerBar;
    int snap = mm::kPhrasePpq / 4;
    int selected = -1;            // the note the selection info shows (and Resize / velocity use)
    std::vector<bool> sel;        // which notes are selected, in pattern order

    Drag drag = Drag::None;
    bool editStarted = false, dropHighlight = false;
    mm::PatternNote dragOriginal;
    std::vector<mm::PatternNote> groupOriginal;   // the whole pattern when a group drag started
    std::vector<bool> groupFlags, marqueeBase;
    juce::Point<float> marqueeStart, marqueeEnd;
    double dragStartTick = 0.0;
    int dragStartOffset = 0;
    double playhead = -1.0;
};
