#include "PatternGrid.h"
#include "../Shared/CustomLookAndFeel.h"

using namespace juce;

PatternGrid::PatternGrid (MelodyMagicProcessor& p) : proc (p)
{
    setTooltip ("Click: add a note (locked to the Key + Scale).  Drag: move.  Drag the right edge: length.  Alt: no snap, any semitone.\n"
                "Mouse wheel over a note: up / down to the next scale note (Alt: a semitone, Ctrl: an octave).\n"
                "Ctrl+A or Select All: select every note. Shift+drag: box select, Shift+click: add / remove a note.\n"
                "Up / Down, the wheel or the arrow buttons move the whole selection; drag one to drag them all.\n"
                "Right-click or double-click: delete.  Drop a .mid file here to import it.\n"
                "Keys: Up / Down move the selected note, Left / Right select, Delete removes it.");
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
    computeRange();
    startTimerHz (30);
}

//==============================================================================
void PatternGrid::computeRange()
{
    const auto& pt = proc.getPattern();
    int lo = -12, hi = 12;
    for (auto& n : pt.notes)
    {
        lo = jmin (lo, n.offset - 2);
        hi = jmax (hi, n.offset + 2);
    }
    bottom = jmax (-mm::kMaxOffset, lo);
    top = jmin (mm::kMaxOffset, hi);
    length = pt.lengthTicks();
}

void PatternGrid::modelChanged()
{
    if (drag == Drag::None)   // keep the rows still while a note is being dragged
        computeRange();
    else
        length = proc.getPattern().lengthTicks();
    const int count = (int) proc.getPattern().notes.size();
    if (selected >= count)
        selected = -1;
    if ((int) sel.size() != count)   // a different pattern: start with nothing selected
        sel.assign ((size_t) count, false);
    repaint();
}

int PatternGrid::numSelected() const noexcept
{
    int n = 0;
    for (bool b : sel) n += b ? 1 : 0;
    return n;
}

void PatternGrid::clearSelection()
{
    selected = -1;
    sel.assign (proc.getPattern().notes.size(), false);
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PatternGrid::selectAll()
{
    sel.assign (proc.getPattern().notes.size(), true);
    selected = sel.empty() ? -1 : 0;
    grabKeyboardFocus();
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PatternGrid::selectOnly (int i)
{
    sel.assign (proc.getPattern().notes.size(), false);
    if (i >= 0 && i < (int) sel.size())
        sel[(size_t) i] = true;
    selected = i;
}

void PatternGrid::commitWithSelection (std::vector<mm::PatternNote> notes, std::vector<bool> noteFlags, int primary)
{
    // sort the way mm::Pattern::sort does, carrying each note's flags along
    std::vector<size_t> order (notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&notes] (size_t a, size_t b)
    {
        return notes[a].start != notes[b].start ? notes[a].start < notes[b].start : notes[a].offset < notes[b].offset;
    });
    auto p = proc.getPattern();
    p.notes.clear();
    sel.clear();
    selected = -1;
    for (size_t k = 0; k < order.size(); ++k)
    {
        p.notes.push_back (notes[order[k]]);
        sel.push_back (order[k] < noteFlags.size() && noteFlags[order[k]]);
        if ((int) order[k] == primary)
            selected = (int) k;
    }
    proc.updatePattern (p);
    repaint();
}

void PatternGrid::deleteSelection()
{
    auto p = proc.getPattern();
    if (numSelected() == 0 && selected >= 0)
        selectOnly (selected);
    if (numSelected() == 0)
        return;
    proc.beginEdit();
    std::vector<mm::PatternNote> keep;
    for (size_t i = 0; i < p.notes.size(); ++i)
        if (! isSelected ((int) i))
            keep.push_back (p.notes[i]);
    p.notes = keep;
    selected = -1;
    sel.assign (keep.size(), false);
    commit (p);
    if (onSelectionChanged) onSelectionChanged();
}

float PatternGrid::rowHeight() const noexcept      { return gridArea.getHeight() / (float) jmax (1, numRows()); }
float PatternGrid::xForTick (double tick) const noexcept
{
    return gridArea.getX() + (float) (tick / jmax (1, length)) * gridArea.getWidth();
}
double PatternGrid::tickForX (float x) const noexcept
{
    return (double) (x - gridArea.getX()) / jmax (1.0f, gridArea.getWidth()) * length;
}
int PatternGrid::snapToKey (int offset) const
{
    // the nearest note of the Key + Scale (ties go down)
    const auto scale = proc.currentScale();
    const int root = 60 + proc.currentKey();
    return jlimit (-mm::kMaxOffset, mm::kMaxOffset, scale.quantise (root + offset) - root);
}

int PatternGrid::offsetForY (float y) const noexcept
{
    return jlimit (bottom, top, top - (int) ((y - gridArea.getY()) / rowHeight()));
}

Rectangle<float> PatternGrid::noteRect (const mm::PatternNote& n) const
{
    const float rh = rowHeight();
    const float x0 = xForTick (n.start), x1 = jmax (x0 + 3.0f, xForTick (jmin (n.end, length)));
    return { x0, gridArea.getY() + rh * (float) (top - n.offset) + 0.5f, x1 - x0, jmax (2.0f, rh - 1.0f) };
}

int PatternGrid::noteAt (Point<float> pos) const
{
    const auto& notes = proc.getPattern().notes;
    for (int i = (int) notes.size() - 1; i >= 0; --i)
        if (noteRect (notes[(size_t) i]).expanded (1.0f, 1.0f).contains (pos))
            return i;
    return -1;
}

int PatternGrid::velocityNoteAt (float x) const
{
    const auto& notes = proc.getPattern().notes;
    int best = -1;
    float bestDist = 6.0f;
    for (int i = 0; i < (int) notes.size(); ++i)
    {
        const float d = std::abs (xForTick (notes[(size_t) i].start) + 2.0f - x);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

//==============================================================================
void PatternGrid::resized()
{
    auto b = getLocalBounds().toFloat();
    velArea = b.removeFromBottom (40.0f);
    b.removeFromBottom (4.0f);
    rulerArea = b.removeFromTop (14.0f);
    labelArea = b.removeFromLeft (40.0f);
    gridArea = b;
    rulerArea = rulerArea.withTrimmedLeft (40.0f);
    velArea = velArea.withTrimmedLeft (40.0f);
}

void PatternGrid::paint (Graphics& g)
{
    const auto& pt = proc.getPattern();
    const auto scale = proc.currentScale();
    const int key = proc.currentKey();

    CustomLookAndFeel::drawInsetBox (g, gridArea.expanded (2.0f), 3.0f);
    CustomLookAndFeel::drawInsetBox (g, velArea.expanded (2.0f), 3.0f);

    // rows: in-scale notes lighter, the key's root highlighted
    const float rh = rowHeight();
    for (int o = top; o >= bottom; --o)
    {
        const auto r = Rectangle<float> (gridArea.getX(), gridArea.getY() + rh * (float) (top - o), gridArea.getWidth(), rh);
        const bool root = mm::floorMod (o, 12) == 0;
        const bool inScale = scale.inScale (key + o);
        g.setColour (root ? Colour (0xff1f2d3b) : (inScale ? Colour (0xff1d2024) : Colour (0xff131517)));
        g.fillRect (r);
        g.setColour (Colours::black.withAlpha (root ? 0.0f : 0.45f));
        g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());

        if (rh >= 7.0f || root)
        {
            String label (mm::keyName (key + o));
            if (root && o != 0)
                label << (o > 0 ? " +" : " ") << String (o / 12);
            g.setFont (CustomLookAndFeel::uiFont (jlimit (7.0f, 10.0f, rh * 0.9f), root));
            g.setColour (root ? VirusColours::blue : (inScale ? VirusColours::labelText : VirusColours::labelText.withAlpha (0.4f)));
            g.drawText (label, Rectangle<float> (labelArea.getX(), r.getY() - 2.0f, labelArea.getWidth() - 4.0f, rh + 4.0f), Justification::centredRight);
        }
        if (root)
        {
            g.setColour (Colour (0xff3a4a5a));
            g.drawHorizontalLine ((int) r.getBottom(), r.getX(), r.getRight());
        }
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
    }

    // notes
    for (int i = 0; i < (int) pt.notes.size(); ++i)
    {
        const auto& n = pt.notes[(size_t) i];
        const auto r = noteRect (n);
        const bool isSel = isSelected (i) || i == selected;
        const float v = (float) n.velocity / 127.0f;
        const auto c1 = VirusColours::blue.withMultipliedBrightness (0.55f + 0.45f * v);
        const auto c2 = VirusColours::blueGlow.withMultipliedBrightness (0.45f + 0.4f * v);
        g.setGradientFill (ColourGradient (isSel ? c1.brighter (0.35f) : c1, 0.0f, r.getY(), isSel ? c2.brighter (0.35f) : c2, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (isSel ? Colours::white : Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (r, 2.0f, isSel ? 1.4f : 1.0f);

        const float x = xForTick (n.start);
        const float h = (velArea.getHeight() - 4.0f) * v;
        g.setColour (isSel ? Colours::white : VirusColours::blue);
        g.fillRect (Rectangle<float> (x + 1.0f, velArea.getBottom() - 2.0f - h, 3.0f, h));
    }

    if (playhead >= 0.0)
    {
        const float x = xForTick (std::fmod (playhead, (double) jmax (1, length)));
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) x, gridArea.getY(), gridArea.getBottom());
        g.setColour (VirusColours::blue.withAlpha (0.25f));
        g.fillRect (Rectangle<float> (x - 2.0f, gridArea.getY(), 4.0f, gridArea.getHeight()));
    }

    if (pt.notes.empty())
    {
        g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
        g.setColour (VirusColours::labelText.withAlpha (0.7f));
        g.drawText ("Click to add notes, press New Pattern, pick one in the Library, or drop a MIDI file here",
                    gridArea, Justification::centred);
    }
    if (drag == Drag::Marquee)
    {
        const auto r = Rectangle<float> (marqueeStart, marqueeEnd);
        g.setColour (VirusColours::blue.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (VirusColours::blue);
        g.drawRect (r, 1.0f);
    }
    if (hasKeyboardFocus (false))
    {
        g.setColour (VirusColours::blue.withAlpha (0.7f));
        g.drawRoundedRectangle (gridArea.expanded (2.0f), 3.0f, 1.5f);
    }
    if (dropHighlight)
    {
        g.setColour (VirusColours::blue.withAlpha (0.25f));
        g.fillRect (gridArea);
        g.setColour (VirusColours::blue);
        g.drawRect (gridArea, 2.0f);
        g.setFont (CustomLookAndFeel::uiFont (14.0f, true));
        g.setColour (Colours::white);
        g.drawText ("Drop to import the MIDI file", gridArea, Justification::centred);
    }
}

void PatternGrid::timerCallback()
{
    const double pos = proc.playPosition.load();
    if (std::abs (pos - playhead) > 0.01)
    {
        playhead = pos;
        repaint();
    }
}

//==============================================================================
void PatternGrid::ensureEditStarted()
{
    if (! editStarted)
    {
        proc.beginEdit();
        editStarted = true;
    }
}

void PatternGrid::commit (mm::Pattern& p)
{
    proc.updatePattern (p);
    repaint();
}

int PatternGrid::selectAfterSort (mm::Pattern& p, const mm::PatternNote& n)
{
    p.sort();
    for (int k = 0; k < (int) p.notes.size(); ++k)
        if (p.notes[(size_t) k] == n)
            return k;
    return -1;
}

void PatternGrid::mouseMove (const MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i >= 0 && std::abs (e.position.x - noteRect (proc.getPattern().notes[(size_t) i]).getRight()) < 5.0f)
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    else if (i >= 0)
        setMouseCursor (MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (MouseCursor::NormalCursor);
}

void PatternGrid::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    editStarted = false;
    drag = Drag::None;
    auto p = proc.getPattern();
    if (sel.size() != p.notes.size())
        sel.assign (p.notes.size(), false);

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
            if (! isSelected (i))
                selectOnly (i);
            deleteSelection();
        }
        return;
    }

    if (e.mods.isShiftDown())
    {
        if (i >= 0)
        {
            // Shift+click: add a note to the selection or take it out
            sel[(size_t) i] = ! sel[(size_t) i];
            selected = sel[(size_t) i] ? i : -1;
        }
        else
        {
            // Shift+drag on empty space: select the notes inside a box
            drag = Drag::Marquee;
            marqueeStart = marqueeEnd = e.position;
            marqueeBase = sel;
        }
        if (onSelectionChanged) onSelectionChanged();
        repaint();
        return;
    }

    if (i >= 0)
    {
        if (! isSelected (i))
            selectOnly (i);
        selected = i;
        dragOriginal = p.notes[(size_t) i];
        dragStartTick = tickForX (e.position.x);
        dragStartOffset = offsetForY (e.position.y);
        drag = std::abs (e.position.x - noteRect (dragOriginal).getRight()) < 5.0f ? Drag::Resize : Drag::Move;
        groupOriginal = p.notes;
        groupFlags = sel;
    }
    else
    {
        ensureEditStarted();
        const double t = tickForX (e.position.x);
        mm::PatternNote n;
        n.start = jlimit (0, jmax (0, length - 1), e.mods.isAltDown() ? (int) t : (int) (t / snap) * snap);
        n.end = jmax (n.start + 1, jmin (length, n.start + snap));
        n.offset = e.mods.isAltDown() ? offsetForY (e.position.y) : snapToKey (offsetForY (e.position.y));
        n.velocity = selected >= 0 && selected < (int) p.notes.size() ? p.notes[(size_t) selected].velocity : 100;
        p.notes.push_back (n);
        selected = selectAfterSort (p, n);
        commit (p);
        selectOnly (selected);
        dragOriginal = n;
        dragStartTick = t;
        drag = Drag::Resize;
    }
    if (onSelectionChanged) onSelectionChanged();
    repaint();
}

void PatternGrid::mouseDrag (const MouseEvent& e)
{
    if (drag == Drag::Velocity)
    {
        const int i = velocityNoteAt (e.position.x);
        if (i >= 0)
            setVelocityFromY (i, e.position.y);
        return;
    }

    if (drag == Drag::Marquee)
    {
        marqueeEnd = e.position;
        const auto box = Rectangle<float> (marqueeStart, marqueeEnd);
        const auto& notes = proc.getPattern().notes;
        sel = marqueeBase;
        sel.resize (notes.size(), false);
        for (size_t k = 0; k < notes.size(); ++k)
            if (noteRect (notes[k]).intersects (box))
                sel[k] = true;
        selected = -1;
        for (size_t k = 0; k < sel.size(); ++k)
            if (sel[k]) { selected = (int) k; break; }
        repaint();
        if (onSelectionChanged) onSelectionChanged();
        return;
    }

    const bool fine = e.mods.isAltDown();
    const double t = tickForX (e.position.x);

    if (drag == Drag::Move && ! groupOriginal.empty())
    {
        // every selected note moves together: time on the grid, pitch locked to the key
        int dt = (int) (fine ? std::round (t - dragStartTick) : std::round ((t - dragStartTick) / snap) * snap);
        int minStart = length, maxEnd = 0;
        for (size_t k = 0; k < groupOriginal.size(); ++k)
            if (k < groupFlags.size() && groupFlags[k])
            {
                minStart = jmin (minStart, groupOriginal[k].start);
                maxEnd = jmax (maxEnd, groupOriginal[k].end);
            }
        dt = jlimit (-minStart, length - maxEnd, dt);
        const int dRows = offsetForY (e.position.y) - dragStartOffset;

        auto notes = groupOriginal;
        for (size_t k = 0; k < notes.size(); ++k)
            if (k < groupFlags.size() && groupFlags[k])
            {
                notes[k].start += dt;
                notes[k].end += dt;
                if (dRows != 0)
                {
                    const int o = jlimit (-mm::kMaxOffset, mm::kMaxOffset, groupOriginal[k].offset + dRows);
                    notes[k].offset = fine ? o : snapToKey (o);
                }
            }
        if (notes == proc.getPattern().notes)
            return;
        ensureEditStarted();
        int primary = -1;
        for (size_t k = 0; k < groupOriginal.size(); ++k)
            if (groupOriginal[k] == dragOriginal) { primary = (int) k; break; }
        commitWithSelection (notes, groupFlags, primary);
        return;
    }

    if (drag != Drag::Resize || selected < 0)
        return;
    auto p = proc.getPattern();
    if (selected >= (int) p.notes.size())
        return;
    auto n = p.notes[(size_t) selected];
    const int end = fine ? (int) std::round (t) : (int) std::ceil (t / snap - 1.0e-6) * snap;
    n.end = jlimit (n.start + 1, length, end);
    if (n == p.notes[(size_t) selected])
        return;
    ensureEditStarted();
    p.notes[(size_t) selected] = n;
    commit (p);
}

void PatternGrid::mouseUp (const MouseEvent&)
{
    drag = Drag::None;
    editStarted = false;
    groupOriginal.clear();
    computeRange();
    repaint();
}

void PatternGrid::mouseDoubleClick (const MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i < 0 || e.mods.isPopupMenu() || e.mods.isShiftDown())
        return;
    selectOnly (i);
    deleteSelection();
}

void PatternGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    const int i = noteAt (e.position);
    if (i < 0)
        return;
    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;
    if (! isSelected (i))   // over a selected note the whole selection moves
        selectOnly (i);
    moveSelection (dir > 0.0f ? 1 : -1, e.mods.isAltDown(), e.mods.isCommandDown());
}

void PatternGrid::moveSelection (int direction, bool chromatic, bool octave)
{
    auto p = proc.getPattern();
    if (p.notes.empty())
        return;
    if (sel.size() != p.notes.size())
        sel.assign (p.notes.size(), false);
    auto chosen = sel;
    if (numSelected() == 0)
    {
        if (selected >= 0 && selected < (int) chosen.size())
            chosen[(size_t) selected] = true;
        else
            chosen.assign (chosen.size(), true);   // nothing selected: move everything
    }

    const auto scale = proc.currentScale();
    auto notes = p.notes;
    bool changed = false;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        if (! chosen[k])
            continue;
        int o = notes[k].offset;
        if (octave)
            o = jlimit (-mm::kMaxOffset, mm::kMaxOffset, o + 12 * direction);
        else
            o = mm::stepOffset (o, direction, chromatic ? nullptr : &scale, proc.currentKey());
        changed = changed || o != notes[k].offset;
        notes[k].offset = o;
    }
    if (! changed)
        return;
    proc.beginEdit();
    commitWithSelection (notes, chosen, selected);
    computeRange();
    if (onSelectionChanged) onSelectionChanged();
}

bool PatternGrid::keyPressed (const KeyPress& key)
{
    const int count = (int) proc.getPattern().notes.size();
    const auto code = key.getKeyCode();
    const bool cmd = key.getModifiers().isCommandDown();

    if (cmd && code == 'Z')
    {
        key.getModifiers().isShiftDown() ? proc.redo() : proc.undo();
        return true;
    }
    if (cmd && code == 'Y')
    {
        proc.redo();
        return true;
    }
    if (cmd && code == 'A')
    {
        selectAll();
        return true;
    }
    if (code == KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    if (code == KeyPress::upKey || code == KeyPress::downKey)
    {
        moveSelection (code == KeyPress::upKey ? 1 : -1, key.getModifiers().isAltDown(), cmd);
        return true;
    }
    if (code == KeyPress::leftKey || code == KeyPress::rightKey)
    {
        if (count > 0)
        {
            selectOnly (selected < 0 ? 0 : jlimit (0, count - 1, selected + (code == KeyPress::rightKey ? 1 : -1)));
            repaint();
            if (onSelectionChanged) onSelectionChanged();
        }
        return true;
    }
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey)
    {
        deleteSelection();
        return true;
    }
    return false;
}

void PatternGrid::setVelocityFromY (int index, float y)
{
    auto p = proc.getPattern();
    if (index < 0 || index >= (int) p.notes.size())
        return;
    const int vel = jlimit (1, 127, roundToInt ((velArea.getBottom() - 2.0f - y) / (velArea.getHeight() - 4.0f) * 127.0f));
    if (p.notes[(size_t) index].velocity == vel)
        return;
    ensureEditStarted();
    p.notes[(size_t) index].velocity = vel;
    proc.updatePattern (p);
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

//==============================================================================
bool PatternGrid::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return false;
}

void PatternGrid::filesDropped (const StringArray& files, int, int)
{
    dropHighlight = false;
    repaint();
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            if (onFileDropped)
                onFileDropped (File (f));
            return;
        }
}
