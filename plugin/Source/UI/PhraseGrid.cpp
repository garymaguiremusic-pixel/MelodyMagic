#include "PhraseGrid.h"
#include "../Shared/CustomLookAndFeel.h"

using namespace juce;

PhraseGrid::PhraseGrid (MelodyMagicProcessor& p) : proc (p)
{
    setTooltip ("Click: add a note.  Drag: move.  Drag the right edge: length.  Alt: no snap.\n"
                "Right-click or double-click: delete.  Mouse wheel over a note: Note_Offset (diatonic shift).\n"
                "Rows are chord tones (R1 = lowest held note), lanes are octaves. Drag in the lower strip for velocity.");
    computeLanes();
    startTimerHz (30);
}

//==============================================================================
void PhraseGrid::computeLanes()
{
    const auto& ph = proc.getPhrase();
    int lo, hi;
    switch (ph.lanesShown())
    {
        case 5:  lo = 3; hi = 7; break;
        case 3:  lo = 4; hi = 6; break;
        default: lo = 5; hi = 5; break;
    }
    rowsPerLane = ph.rowsPerLane();
    for (auto& n : ph.notes)
    {
        lo = jmin (lo, jlimit (mm::kMinLane, mm::kMaxLane, n.lane()));
        hi = jmax (hi, jlimit (mm::kMinLane, mm::kMaxLane, n.lane()));
        rowsPerLane = jmax (rowsPerLane, n.row() + 1);
    }
    lanes.clear();
    for (int l = hi; l >= lo; --l)
        lanes.push_back (l);
    length = ph.lengthTicks();
}

void PhraseGrid::modelChanged()
{
    computeLanes();
    if (selected >= (int) proc.getPhrase().notes.size())
        selected = -1;
    repaint();
}

int PhraseGrid::numberForRowIndex (int index) const noexcept
{
    index = jlimit (0, numRows() - 1, index);
    const int lane = lanes[(size_t) (index / rowsPerLane)];
    const int row = rowsPerLane - 1 - index % rowsPerLane;
    return lane * 10 + row;
}

int PhraseGrid::rowIndexForNumber (int number) const noexcept
{
    const int lane = number / 10, row = number % 10;
    for (size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i] == lane && row < rowsPerLane)
            return (int) i * rowsPerLane + (rowsPerLane - 1 - row);
    return -1;
}

float PhraseGrid::xForTick (double tick) const noexcept
{
    return gridArea.getX() + (float) (tick / jmax (1, length)) * gridArea.getWidth();
}

double PhraseGrid::tickForX (float x) const noexcept
{
    return (double) (x - gridArea.getX()) / jmax (1.0f, gridArea.getWidth()) * length;
}

int PhraseGrid::rowIndexForY (float y) const noexcept
{
    const float rowH = gridArea.getHeight() / (float) jmax (1, numRows());
    return jlimit (0, numRows() - 1, (int) ((y - gridArea.getY()) / rowH));
}

Rectangle<float> PhraseGrid::noteRect (const mm::PhraseNote& n) const
{
    const int idx = rowIndexForNumber (n.number);
    if (idx < 0)
        return {};
    const float rowH = gridArea.getHeight() / (float) jmax (1, numRows());
    const float x0 = xForTick (n.start), x1 = jmax (x0 + 3.0f, xForTick (jmin (n.end, length)));
    return { x0, gridArea.getY() + rowH * (float) idx + 1.0f, x1 - x0, rowH - 2.0f };
}

int PhraseGrid::noteAt (Point<float> pos) const
{
    const auto& notes = proc.getPhrase().notes;
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteRect (notes[(size_t) i]).expanded (1.0f, 0.0f).contains (pos))
            return i;
    return -1;
}

int PhraseGrid::velocityNoteAt (float x) const
{
    const auto& notes = proc.getPhrase().notes;
    int best = -1;
    float bestDist = 1.0e9f;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const float d = std::abs (xForTick (notes[(size_t) i].start) + 2.0f - x);
        if (d < bestDist && d < 6.0f)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

//==============================================================================
void PhraseGrid::resized()
{
    auto b = getLocalBounds().toFloat();
    velArea = b.removeFromBottom (44.0f);
    b.removeFromBottom (4.0f);
    rulerArea = b.removeFromTop (14.0f);
    labelArea = b.removeFromLeft (40.0f);
    gridArea = b;
    rulerArea = rulerArea.withTrimmedLeft (40.0f);
    velArea = velArea.withTrimmedLeft (40.0f);
}

void PhraseGrid::paint (Graphics& g)
{
    const auto& ph = proc.getPhrase();
    CustomLookAndFeel::drawInsetBox (g, gridArea.expanded (2.0f), 3.0f);
    CustomLookAndFeel::drawInsetBox (g, velArea.expanded (2.0f), 3.0f);

    const int rows = numRows();
    const float rowH = gridArea.getHeight() / (float) jmax (1, rows);

    // lane bands and row lines
    for (int i = 0; i < rows; ++i)
    {
        const int laneIndex = i / rowsPerLane;
        const int lane = lanes[(size_t) laneIndex];
        const auto r = Rectangle<float> (gridArea.getX(), gridArea.getY() + rowH * (float) i, gridArea.getWidth(), rowH);
        g.setColour (lane == mm::kCentreLane ? Colour (0xff1d2834) : (laneIndex % 2 == 0 ? Colour (0xff17191c) : Colour (0xff1b1e21)));
        g.fillRect (r);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());

        const int row = rowsPerLane - 1 - i % rowsPerLane;
        g.setFont (CustomLookAndFeel::uiFont (jmin (10.0f, rowH * 0.8f), true));
        g.setColour (lane == mm::kCentreLane ? VirusColours::labelText : VirusColours::labelText.withAlpha (0.6f));
        g.drawText ("R" + String (row + 1), Rectangle<float> (labelArea.getX(), r.getY(), labelArea.getWidth() - 4.0f, rowH), Justification::centredRight);
    }
    for (int laneIndex = 0; laneIndex < (int) lanes.size(); ++laneIndex)
    {
        const float y = gridArea.getY() + rowH * (float) (laneIndex * rowsPerLane);
        if (laneIndex > 0)
        {
            g.setColour (Colour (0xff4a5560));
            g.drawHorizontalLine ((int) y, gridArea.getX(), gridArea.getRight());
        }
        const int oct = lanes[(size_t) laneIndex] - mm::kCentreLane;
        const auto text = oct == 0 ? String ("0") : (oct > 0 ? "+" : "") + String (oct);
        g.setFont (CustomLookAndFeel::uiFont (9.0f, true));
        g.setColour (oct == 0 ? VirusColours::blue : VirusColours::labelText.withAlpha (0.8f));
        g.drawText ("OCT " + text, Rectangle<float> (labelArea.getX(), y, labelArea.getWidth() - 4.0f, 11.0f), Justification::topLeft);
    }

    // time grid
    for (int t = 0; t <= length; t += snap)
    {
        const float x = xForTick (t);
        const bool bar = t % mm::kTicksPerBar == 0, beat = t % mm::kPhrasePpq == 0;
        g.setColour (bar ? Colour (0xff59636d) : (beat ? Colour (0xff353c43) : Colour (0xff24292e)));
        g.drawVerticalLine ((int) x, gridArea.getY(), gridArea.getBottom());
        g.setColour (bar ? Colour (0xff59636d).withAlpha (0.8f) : Colour (0xff2b3035));
        g.drawVerticalLine ((int) x, velArea.getY(), velArea.getBottom());
        if (bar && t < length)
        {
            g.setFont (CustomLookAndFeel::uiFont (10.0f, true));
            g.setColour (VirusColours::labelText);
            g.drawText (String (t / mm::kTicksPerBar + 1), Rectangle<float> (x + 3.0f, rulerArea.getY(), 30.0f, rulerArea.getHeight()), Justification::centredLeft);
        }
        else if (beat && t < length)
        {
            g.setColour (VirusColours::labelText.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, rulerArea.getBottom() - 4.0f, rulerArea.getBottom());
        }
    }

    // notes
    for (int i = 0; i < (int) ph.notes.size(); ++i)
    {
        const auto& n = ph.notes[(size_t) i];
        const auto r = noteRect (n);
        if (r.isEmpty())
            continue;
        const bool sel = i == selected;
        const float v = (float) n.velocity / 127.0f;
        const auto top = VirusColours::blue.withMultipliedBrightness (0.55f + 0.45f * v);
        const auto bottom = VirusColours::blueGlow.withMultipliedBrightness (0.45f + 0.4f * v);
        g.setGradientFill (ColourGradient (sel ? top.brighter (0.3f) : top, 0.0f, r.getY(), sel ? bottom.brighter (0.3f) : bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (sel ? Colours::white : Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 2.0f, sel ? 1.4f : 1.0f);

        if (n.noteOffset != 0 && r.getWidth() > 12.0f)
        {
            g.setFont (CustomLookAndFeel::uiFont (jmin (10.0f, r.getHeight() - 1.0f), true));
            g.setColour (Colours::white);
            g.drawText ((n.noteOffset > 0 ? "+" : "") + String (n.noteOffset), r.reduced (2.0f, 0.0f), Justification::centredLeft, false);
        }

        // velocity bar
        const float x = xForTick (n.start);
        const float h = (velArea.getHeight() - 4.0f) * v;
        g.setColour (sel ? Colours::white : VirusColours::blue);
        g.fillRect (Rectangle<float> (x + 1.0f, velArea.getBottom() - 2.0f - h, 3.0f, h));
    }

    // playhead
    if (playhead >= 0.0)
    {
        const float x = xForTick (std::fmod (playhead, (double) jmax (1, length)));
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) x, gridArea.getY(), gridArea.getBottom());
        g.setColour (VirusColours::blue.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (x - 2.0f, gridArea.getY(), 4.0f, gridArea.getHeight()));
    }

    if (ph.notes.empty())
    {
        g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
        g.setColour (VirusColours::labelText.withAlpha (0.6f));
        g.drawText ("Click to add notes, pick a phrase in the Library, or use a generator below",
                    gridArea, Justification::centred);
    }
}

void PhraseGrid::timerCallback()
{
    const double pos = proc.phrasePosition.load();
    if (std::abs (pos - playhead) > 0.01)
    {
        playhead = pos;
        repaint();
    }
}

//==============================================================================
void PhraseGrid::ensureEditStarted()
{
    if (! editStarted)
    {
        proc.beginPhraseEdit();
        editStarted = true;
    }
}

void PhraseGrid::commit (mm::Phrase& p)
{
    proc.updatePhrase (p);
    computeLanes();
    repaint();
}

void PhraseGrid::mouseMove (const MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i >= 0 && std::abs (e.position.x - noteRect (proc.getPhrase().notes[(size_t) i]).getRight()) < 5.0f)
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    else if (i >= 0)
        setMouseCursor (MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (MouseCursor::NormalCursor);
}

void PhraseGrid::mouseDown (const MouseEvent& e)
{
    editStarted = false;
    drag = Drag::None;
    auto p = proc.getPhrase();

    if (velArea.contains (e.position))
    {
        const int i = velocityNoteAt (e.position.x);
        if (i >= 0)
        {
            drag = Drag::Velocity;
            selected = i;
            setVelocityFromY (i, e.position.y);
            if (onSelectionChanged) onSelectionChanged();
        }
        return;
    }
    if (! gridArea.contains (e.position))
        return;

    const int i = noteAt (e.position);
    if (e.mods.isPopupMenu())
    {
        if (i >= 0)
        {
            ensureEditStarted();
            p.notes.erase (p.notes.begin() + i);
            selected = -1;
            commit (p);
            if (onSelectionChanged) onSelectionChanged();
        }
        return;
    }

    if (i >= 0)
    {
        selected = i;
        dragOriginal = p.notes[(size_t) i];
        dragStartTick = tickForX (e.position.x);
        dragStartRow = rowIndexForY (e.position.y);
        drag = std::abs (e.position.x - noteRect (dragOriginal).getRight()) < 5.0f ? Drag::Resize : Drag::Move;
    }
    else
    {
        // add a note on the snap grid; dragging then sets its length
        ensureEditStarted();
        const double t = tickForX (e.position.x);
        const int start = jlimit (0, jmax (0, length - 1), e.mods.isAltDown() ? (int) t : (int) (t / snap) * snap);
        mm::PhraseNote n;
        n.number = numberForRowIndex (rowIndexForY (e.position.y));
        n.start = start;
        n.end = jmin (length, start + snap);
        n.velocity = selected >= 0 && selected < (int) p.notes.size() ? p.notes[(size_t) selected].velocity : 100;
        if (n.end <= n.start)
            n.end = n.start + 1;
        p.notes.push_back (n);
        p.sortNotes();
        for (int k = 0; k < (int) p.notes.size(); ++k)
            if (p.notes[(size_t) k] == n)
                selected = k;
        dragOriginal = n;
        dragStartTick = t;
        drag = Drag::Resize;
        commit (p);
    }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PhraseGrid::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::Velocity)
    {
        const int i = velocityNoteAt (e.position.x);
        if (i >= 0)
            setVelocityFromY (i, e.position.y);
        return;
    }
    if (selected < 0 || (drag != Drag::Move && drag != Drag::Resize))
        return;

    auto p = proc.getPhrase();
    if (selected >= (int) p.notes.size())
        return;
    auto n = dragOriginal;
    const bool fine = e.mods.isAltDown();
    const double t = tickForX (e.position.x);

    if (drag == Drag::Move)
    {
        double delta = t - dragStartTick;
        delta = fine ? std::round (delta) : std::round (delta / snap) * snap;
        const int len = dragOriginal.end - dragOriginal.start;
        n.start = jlimit (0, jmax (0, length - len), dragOriginal.start + (int) delta);
        n.end = n.start + len;
        const int rowDelta = rowIndexForY (e.position.y) - dragStartRow;
        const int idx = rowIndexForNumber (dragOriginal.number);
        if (idx >= 0)
            n.number = numberForRowIndex (idx + rowDelta);
    }
    else
    {
        const int end = fine ? (int) std::round (t) : (int) std::ceil (t / snap - 1.0e-6) * snap;
        n.end = jlimit (n.start + 1, length, end);
    }

    if (n == p.notes[(size_t) selected])
        return;
    ensureEditStarted();
    p.notes[(size_t) selected] = n;
    commit (p);
}

void PhraseGrid::mouseUp (const MouseEvent&)
{
    if (drag == Drag::Move || drag == Drag::Resize)
    {
        // keep the note order sorted, and keep it selected
        auto p = proc.getPhrase();
        if (selected >= 0 && selected < (int) p.notes.size())
        {
            const auto n = p.notes[(size_t) selected];
            p.sortNotes();
            for (int k = 0; k < (int) p.notes.size(); ++k)
                if (p.notes[(size_t) k] == n)
                    selected = k;
            if (editStarted)
                commit (p);
        }
    }
    drag = Drag::None;
    editStarted = false;
}

void PhraseGrid::mouseDoubleClick (const MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i < 0 || e.mods.isPopupMenu())
        return;
    auto p = proc.getPhrase();
    if (! editStarted)
        proc.beginPhraseEdit();
    p.notes.erase (p.notes.begin() + i);
    selected = -1;
    commit (p);
    if (onSelectionChanged) onSelectionChanged();
}

void PhraseGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    const int i = noteAt (e.position);
    if (i < 0)
        return;
    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * wheel.deltaY;
    if (std::abs (dir) < 1.0e-6f)
        return;
    selected = i;
    nudgeOffset (dir > 0.0f ? 1 : -1);
}

void PhraseGrid::nudgeOffset (int delta)
{
    auto p = proc.getPhrase();
    if (p.notes.empty())
        return;
    proc.beginPhraseEdit();
    if (selected >= 0 && selected < (int) p.notes.size())
        p.notes[(size_t) selected].noteOffset = jlimit (-24, 24, p.notes[(size_t) selected].noteOffset + delta);
    else
        for (auto& n : p.notes)
            n.noteOffset = jlimit (-24, 24, n.noteOffset + delta);
    commit (p);
    if (onSelectionChanged) onSelectionChanged();
}

void PhraseGrid::setVelocityFromY (int index, float y)
{
    auto p = proc.getPhrase();
    if (index < 0 || index >= (int) p.notes.size())
        return;
    const int vel = jlimit (1, 127, roundToInt ((velArea.getBottom() - 2.0f - y) / (velArea.getHeight() - 4.0f) * 127.0f));
    if (p.notes[(size_t) index].velocity == vel)
        return;
    ensureEditStarted();
    p.notes[(size_t) index].velocity = vel;
    proc.updatePhrase (p);
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}
