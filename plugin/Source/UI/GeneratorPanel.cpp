#include "GeneratorPanel.h"
#include "../Shared/CustomLookAndFeel.h"

using namespace juce;

namespace
{
    constexpr int kColumns = 10;
    constexpr float kHeaderH = 18.0f;
    constexpr int kButtonsW = 96;
}

GeneratorPanel::GeneratorPanel (MelodyMagicProcessor& p) : proc (p)
{
    regenButton.setTooltip ("New random pattern with the current settings");
    randomButton.setTooltip ("Random settings for every unlocked knob, then a new pattern");
    resetButton.setTooltip ("Default settings and no locks");
    snapshotButton.setTooltip ("Remember the current phrase as the starting point for the mutators");

    regenButton.onClick = [this] { proc.runGenerator (kind, true); };
    randomButton.onClick = [this] { proc.randomizeGenerator (kind); refresh(); };
    resetButton.onClick = [this] { proc.resetGenerator (kind); refresh(); };
    snapshotButton.onClick = [this] { proc.takeSnapshot(); };
    mutateButton.onClick = [this]
    {
        if (kind == mm::gen::Kind::Random303)
            proc.run303Mutator (true);
        else
            proc.runGenerator (mm::gen::Kind::Mutate, true);
    };

    for (auto* b : { &regenButton, &randomButton, &resetButton, &mutateButton, &snapshotButton })
        addAndMakeVisible (b);

    rebuild();
}

void GeneratorPanel::setKind (mm::gen::Kind k)
{
    if (k == kind)
        return;
    kind = k;
    rebuild();
}

void GeneratorPanel::rebuild()
{
    knobs.clear();
    toggles.clear();
    cells.clear();

    const auto& params = mm::gen::params (kind);
    for (int i = 0; i < (int) params.size(); ++i)
    {
        const auto& p = params[(size_t) i];
        if (p.isToggle)
        {
            auto* b = toggles.add (new TextButton (p.name));
            b->setClickingTogglesState (true);
            b->setTooltip (String (p.name) + ": on / off");
            b->onClick = [this, i, b]
            {
                if (! updating)
                    proc.setGeneratorValue (kind, i, b->getToggleState() ? 1 : 0);
            };
            addAndMakeVisible (b);
            cells.push_back (b);
            continue;
        }

        auto* k = knobs.add (new VirusKnob (VirusKnob::Size::Small, p.name));
        k->setRange (p.min, p.max, 1.0);
        k->setDefaultValue (p.def);
        k->setLockable (true);
        const String unit (p.unit);
        const String id (p.id);
        k->describeValue = [unit, id] (double v)
        {
            const int x = roundToInt (v);
            if (id == "macro")
                return String (1 << x) + (x == 0 ? " bar" : " bars");
            if (id == "rotate")
                return (x > 0 ? "+" : "") + String (x) + " steps";
            if (unit == "%")
                return String (x) + " %";
            if (unit.isEmpty())
                return String (x);
            return String (x) + " " + unit;
        };
        k->onValueChange = [this, i, k]
        {
            if (! updating)
                proc.setGeneratorValue (kind, i, roundToInt (k->getValue()));
        };
        k->onLockChanged = [this, i] (bool locked)
        {
            auto& g = proc.generator (kind);
            if (i < (int) g.locks.size())
                g.locks[(size_t) i] = locked;
        };
        if (kind == mm::gen::Kind::Random303 && i >= mm::gen::indexOf (kind, "mShow"))
            k->extraTooltip = "Mutator setting: applied when you press Mutate (to the snapshot)";
        addAndMakeVisible (k);
        cells.push_back (k);
    }

    regenButton.setVisible (kind != mm::gen::Kind::Mutate);
    mutateButton.setVisible (kind != mm::gen::Kind::ChordBass);
    snapshotButton.setVisible (kind != mm::gen::Kind::ChordBass);
    mutateButton.setTooltip (kind == mm::gen::Kind::Random303
                                 ? "303 mutator: show/hide, pitch, octave, swap, accent and slide flips on the snapshot"
                                 : "New mutation of the snapshot (taken automatically the first time)");
    refresh();
    resized();
    repaint();
}

void GeneratorPanel::refresh()
{
    const ScopedValueSetter<bool> svs (updating, true);
    const auto& g = proc.generator (kind);
    for (int i = 0; i < (int) cells.size() && i < (int) g.values.size(); ++i)
    {
        if (auto* k = dynamic_cast<VirusKnob*> (cells[(size_t) i]))
        {
            k->setValue (g.values[(size_t) i], dontSendNotification);
            k->setLocked (i < (int) g.locks.size() && g.locks[(size_t) i]);
        }
        else if (auto* b = dynamic_cast<TextButton*> (cells[(size_t) i]))
        {
            b->setToggleState (g.values[(size_t) i] != 0, dontSendNotification);
        }
    }
    snapshotButton.setButtonText (proc.hasSnapshot() ? "Snapshot *" : "Snapshot");
}

void GeneratorPanel::paint (Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds().toFloat(), String (mm::gen::kindName (kind)) + " generator");

    // divider before the buttons
    const float x = (float) getWidth() - kButtonsW - 4.0f;
    g.setColour (VirusColours::bevelDark);
    g.drawVerticalLine ((int) x, kHeaderH + 8.0f, (float) getHeight() - 8.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawVerticalLine ((int) x + 1, kHeaderH + 8.0f, (float) getHeight() - 8.0f);

    g.setFont (CustomLookAndFeel::uiFont (9.5f));
    g.setColour (VirusColours::labelText.withAlpha (0.7f));
    g.drawText ("Right-click a knob to lock it", Rectangle<float> (8.0f, (float) getHeight() - 14.0f, 300.0f, 12.0f), Justification::centredLeft);
}

void GeneratorPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop ((int) kHeaderH + 4);
    auto buttons = area.removeFromRight (kButtonsW).reduced (6, 4);
    area = area.reduced (6, 2).withTrimmedBottom (12);

    const int rowsNeeded = jmax (1, ((int) cells.size() + kColumns - 1) / kColumns);
    const int cellW = area.getWidth() / kColumns;
    const int cellH = jmin (78, area.getHeight() / rowsNeeded);
    for (int i = 0; i < (int) cells.size(); ++i)
    {
        auto cell = Rectangle<int> (area.getX() + (i % kColumns) * cellW, area.getY() + (i / kColumns) * cellH, cellW, cellH);
        if (dynamic_cast<TextButton*> (cells[(size_t) i]) != nullptr)
            cells[(size_t) i]->setBounds (cell.withSizeKeepingCentre (cellW - 6, 24));
        else
            cells[(size_t) i]->setBounds (cell.reduced (2, 1));
    }

    const int bh = 24, gap = 6;
    for (auto* b : { &regenButton, &mutateButton, &randomButton, &snapshotButton, &resetButton })
    {
        if (! b->isVisible())
            continue;
        b->setBounds (buttons.removeFromTop (bh));
        buttons.removeFromTop (gap);
    }
}
