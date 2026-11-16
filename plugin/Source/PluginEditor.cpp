#include "PluginEditor.h"

using namespace juce;

namespace
{
    constexpr int kWidth = MelodyMagicPanel::kBaseWidth, kHeight = MelodyMagicPanel::kBaseHeight;
    constexpr int kTopStripH = 44;
    constexpr int kBottomBarY = 646;

    const Rectangle<float> phraseArea  { 8.0f, 50.0f, 684.0f, 332.0f };
    const Rectangle<float> tabsGroove  { 8.0f, 386.0f, 684.0f, 30.0f };
    const Rectangle<int>   tabContent  { 8, 420, 684, 180 };
    const Rectangle<float> arpArea     { 700.0f, 50.0f, 292.0f, 140.0f };
    const Rectangle<float> scaleArea   { 700.0f, 196.0f, 292.0f, 112.0f };
    const Rectangle<float> grooveArea  { 700.0f, 314.0f, 292.0f, 110.0f };
    const Rectangle<float> midiArea    { 700.0f, 430.0f, 292.0f, 84.0f };
    const Rectangle<float> exportArea  { 700.0f, 520.0f, 292.0f, 80.0f };

    const int snapTicks[] = { 96, 48, 24, 12, 32, 16 };
    const char* snapNames[] = { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" };
    const int lengthBars[] = { 0, 1, 2, 3, 4, 8 };

    StringArray helpLines()
    {
        return {
            "MelodyMagic turns the chord you hold into a phrase. Each phrase note picks a chord tone (row R1 = lowest held note) in an octave lane.",
            "PHRASE:  click to add, drag to move, drag the right edge for length, right-click / double-click to delete, Alt = no snap.",
            "NOTE OFFSET:  mouse wheel over a note (or - / +) shifts it diatonically: the chord shape is rebuilt in the Scale Lock scale.",
            "ARP MODE:  Free re-pitches sounding notes when the chord changes; Static waits for the next phrase note. Stopped transport = Static.",
            "SPEED:  phrase rate against the host tempo.  INT SYNC: the phrase restarts on the first key you press (classic arp), host stop or not.",
            "FIXED VEL:  phrase velocities only (ignore how hard you play).  RND VEL: random +/- velocity, 0-127.",
            "SCALE LOCK:  every note sent (live and Drag MIDI) is moved to the nearest note of KEY + SCALE. The generators follow it too.",
            "GROOVE:  load a groove in Library > Grooves; Q START / Q END pull note starts / ends onto its grid (Link = one strength for both).",
            "MIDI:  input / output channel filters.  KEY MAP: each key becomes a chord from the loaded key map (Library > Key Maps).",
            "GENERATORS:  303 Random, Chord / Bass, Mutate. Knobs re-run the generator; Regen = new pattern; Randomize skips locked knobs.",
            "LOCKS:  right-click a generator knob to lock it.  SNAPSHOT: the phrase the mutators start from.",
            "DRAG MIDI:  drag the blue grip onto a DAW track. It renders the arpeggio over EXPORT CHORDS (your last chord, a chord clip or a progression).",
            "LIBRARY:  Folder... = the folder with nora_*_library.json and/or .fas / .qas / .has / .mid files. Click an entry to load it.",
            "Knobs move in steps of 1 (drag or mouse wheel), Shift = slower drag, double-click = default. Drag the corner to resize.",
            "",
            "Click anywhere to close."
        };
    }
}

//==============================================================================
MelodyMagicPanel::MelodyMagicPanel (MelodyMagicProcessor& p)
    : processor (p), grid (p), generatorPanel (p), libraryPanel (p), monitor (p),
      helpOverlay ("MelodyMagic - Quick Help", helpLines())
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (kWidth, kHeight);
    createBackground();

    // Top bar ---------------------------------------------------------------------
    undoButton.setTooltip ("Undo the last phrase change");
    redoButton.setTooltip ("Redo");
    loadButton.setTooltip ("Load a phrase file (.fas)");
    saveButton.setTooltip ("Save the phrase as a .fas file (the original phrase format)");
    undoButton.onClick = [this] { processor.undo(); };
    redoButton.onClick = [this] { processor.redo(); };
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Load phrase", processor.getLibraryFolder(), "*.fas");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            String error;
            if (f.existsAsFile() && ! processor.loadPhraseFile (f, error))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Load phrase", "Could not load " + f.getFileName() + "\n" + error);
        });
    };
    saveButton.onClick = [this]
    {
        const auto name = File::createLegalFileName (String (processor.getPhrase().name));
        chooser = std::make_unique<FileChooser> ("Save phrase", processor.getLibraryFolder().getChildFile (name + ".fas"), "*.fas");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f != File() && ! processor.savePhraseFile (f.withFileExtension ("fas")))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Save phrase", "Could not write " + f.getFullPathName());
        });
    };
    for (auto* b : { &undoButton, &redoButton, &loadButton, &saveButton })
        addAndMakeVisible (b);
    attachToggle (bypassButton, mmp::ParamIDs::bypassed, "Bypass: MIDI passes through untouched");

    dragHandle.createFile = [this] { return processor.renderMidiFile(); };
    addAndMakeVisible (dragHandle);

    // Phrase toolbar --------------------------------------------------------------
    setupCombo (lanesBox, { "1 Lane", "3 Lanes", "5 Lanes" }, "Octave lanes shown (each lane is the chord an octave up or down)");
    setupCombo (rowsBox, { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, "Rows per lane: how many chord tones the phrase uses");
    setupCombo (lengthBox, { "Auto", "1 Bar", "2 Bars", "3 Bars", "4 Bars", "8 Bars" }, "Phrase length (Auto = up to the last bar with a note)");
    StringArray snaps;
    for (auto* s : snapNames) snaps.add (s);
    setupCombo (snapBox, snaps, "Editing grid");
    snapBox.setSelectedItemIndex (2, dontSendNotification);

    lanesBox.onChange = [this]
    {
        if (updating) return;
        const int lanes = lanesBox.getSelectedItemIndex() == 0 ? 1 : (lanesBox.getSelectedItemIndex() == 1 ? 3 : 5);
        editPhrase ([lanes] (mm::Phrase& ph) { ph.setLayout (lanes, ph.rowsPerLane()); });
    };
    rowsBox.onChange = [this]
    {
        if (updating) return;
        const int rows = rowsBox.getSelectedItemIndex() + 1;
        editPhrase ([rows] (mm::Phrase& ph) { ph.setLayout (ph.lanesShown(), rows); });
    };
    lengthBox.onChange = [this]
    {
        if (updating) return;
        const int bars = lengthBars[jlimit (0, 5, lengthBox.getSelectedItemIndex())];
        editPhrase ([bars] (mm::Phrase& ph)
        {
            ph.autoSize = bars == 0;
            if (bars > 0)
                ph.totalSize = bars * mm::kTicksPerBar;
        });
    };
    snapBox.onChange = [this] { grid.setSnapTicks (snapTicks[jlimit (0, 5, snapBox.getSelectedItemIndex())]); };
    grid.setSnapTicks (24);

    offsetDown.setTooltip ("Note_Offset -1 for the selected note (all notes when none is selected)");
    offsetUp.setTooltip ("Note_Offset +1 for the selected note (all notes when none is selected)");
    offsetDown.onClick = [this] { grid.nudgeOffset (-1); };
    offsetUp.onClick = [this] { grid.nudgeOffset (1); };
    clearButton.setTooltip ("Remove every note");
    clearButton.onClick = [this] { editPhrase ([] (mm::Phrase& ph) { ph.notes.clear(); }); grid.clearSelection(); };
    selectionLcd.setTooltip ("Selected note: Number (lane x 10 + row), Note_Offset and velocity");
    for (auto* c : std::initializer_list<Component*> { &offsetDown, &offsetUp, &clearButton, &selectionLcd, &grid })
        addAndMakeVisible (c);
    grid.onSelectionChanged = [this] { updateSelectionInfo(); };

    // Tabs ------------------------------------------------------------------------
    const char* tabNames[] = { "303 Random", "Chord / Bass", "Mutate", "Library" };
    for (int i = 0; i < 4; ++i)
    {
        auto& t = tabs[(size_t) i];
        t.setButtonText (tabNames[i]);
        t.onClick = [this, i] { setTab (i); };
        addAndMakeVisible (t);
    }
    addChildComponent (generatorPanel);
    addChildComponent (libraryPanel);

    // Arp -------------------------------------------------------------------------
    setupCombo (modeBox, mmp::getModeNames(), "Free: a chord change re-pitches the sounding notes at once.\nStatic: chord changes apply from the next phrase note.");
    setupCombo (speedBox, mmp::getSpeedNames(), "Phrase speed against the host tempo");
    attachCombo (modeBox, mmp::ParamIDs::staticMode);
    attachCombo (speedBox, mmp::ParamIDs::speed);
    attachToggle (syncButton, mmp::ParamIDs::internalSync, "Force internal sync: the phrase restarts on the first key after all keys were released, and runs even when the host is stopped");
    attachToggle (ignoreVelButton, mmp::ParamIDs::ignoreHostVel, "Ignore host velocities: use the phrase velocities only");
    attachKnob (randomVelKnob, mmp::ParamIDs::randomVel, [] (double v) { return "+/- " + String (roundToInt (v)); });

    // Scale lock --------------------------------------------------------------------
    setupCombo (keyBox, mmp::getKeyNames(), "Scale Lock key");
    setupCombo (scaleBox, mmp::getScaleNames(), "Scale Lock scale (also the scale Note_Offset moves in)");
    attachCombo (keyBox, mmp::ParamIDs::scaleKey);
    attachCombo (scaleBox, mmp::ParamIDs::scaleIndex);
    attachToggle (scaleLockButton, mmp::ParamIDs::scaleLock, "Scale Lock: every note MelodyMagic sends (live and Drag MIDI) moves to the nearest note of the key and scale");
    for (int i = 0; i < 12; ++i)
    {
        auto& k = customKeys[(size_t) i];
        k.setButtonText (mm::keyName (i));
        k.setClickingTogglesState (true);
        k.setTooltip ("Custom scale: " + String (mm::keyName (i)) + " in / out (custom scales ignore the key)");
        k.onClick = [this, i]
        {
            auto mask = processor.getCustomMask();
            mask = (std::uint16_t) (customKeys[(size_t) i].getToggleState() ? (mask | (1u << i)) : (mask & ~(1u << i)));
            processor.setCustomMask (mask);
        };
        addChildComponent (k);
    }

    // Groove --------------------------------------------------------------------------
    grooveLcd.setTooltip ("Groove pattern for the quantizer (Library > Grooves)");
    addAndMakeVisible (grooveLcd);
    attachKnob (qStartKnob, mmp::ParamIDs::quantizeStart, [] (double v) { return String (roundToInt (v)) + " %"; });
    attachKnob (qEndKnob, mmp::ParamIDs::quantizeEnd, [] (double v) { return String (roundToInt (v)) + " %"; });
    qStartKnob.extraTooltip = "How far note starts move to the nearest groove line";
    qEndKnob.extraTooltip = "How far note ends move to the nearest groove line";
    attachToggle (qLinkButton, mmp::ParamIDs::quantizeLink, "Link: Q Start sets both starts and ends");

    // MIDI ----------------------------------------------------------------------------
    setupCombo (inChannelBox, mmp::getChannelNames(), "MIDI input channel filter");
    setupCombo (outChannelBox, mmp::getChannelNames(), "MIDI output channel (All = the channel of the chord note)");
    attachCombo (inChannelBox, mmp::ParamIDs::inputChannel);
    attachCombo (outChannelBox, mmp::ParamIDs::outputChannel);
    attachToggle (keyMapButton, mmp::ParamIDs::keyMapEnabled, "Key map: each key you play is expanded into a chord by the loaded key map");
    keyMapLcd.setTooltip ("Key map (Library > Key Maps)");
    addAndMakeVisible (keyMapLcd);

    // Export ----------------------------------------------------------------------------
    setupCombo (exportChordsBox, mmp::getExportChordNames(), "The chords Drag MIDI plays the phrase over");
    setupCombo (exportBarsBox, mmp::getExportBarNames(), "Length of the dragged MIDI clip");
    exportChordsBox.onChange = [this] { processor.exportChords = exportChordsBox.getSelectedItemIndex(); updateFromModel(); };
    exportBarsBox.onChange = [this] { processor.exportBarsIndex = exportBarsBox.getSelectedItemIndex(); };
    exportLcd.setTooltip ("What Drag MIDI will render");
    addAndMakeVisible (exportLcd);

    // Monitor, help ------------------------------------------------------------------------
    addAndMakeVisible (monitor);
    helpButton.setTooltip ("Quick help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    processor.addChangeListener (this);
    setTab (processor.activeTab);
    updateFromModel();
    startTimerHz (8);
}

MelodyMagicPanel::~MelodyMagicPanel()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MelodyMagicPanel::setupCombo (ComboBox& box, const StringArray& items, const String& tooltip)
{
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
    box.setSelectedItemIndex (0, dontSendNotification);
    addAndMakeVisible (box);
}

void MelodyMagicPanel::attachCombo (ComboBox& box, const char* paramId)
{
    comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.apvts, paramId, box));
}

void MelodyMagicPanel::attachToggle (TextButton& b, const char* paramId, const String& tooltip)
{
    b.setClickingTogglesState (true);
    b.setTooltip (tooltip);
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, paramId, b));
}

void MelodyMagicPanel::attachKnob (VirusKnob& k, const char* paramId, std::function<String (double)> describe)
{
    k.describeValue = std::move (describe);
    knobAttachments.push_back (std::make_unique<KnobAttachment> (*processor.apvts.getParameter (paramId), k));
    addAndMakeVisible (k);
}

void MelodyMagicPanel::setTab (int tab)
{
    tab = jlimit (0, 3, tab);
    currentTab = tab;
    processor.activeTab = tab;
    for (int i = 0; i < 4; ++i)
        tabs[(size_t) i].setToggleState (i == tab, dontSendNotification);

    libraryPanel.setVisible (tab == 3);
    generatorPanel.setVisible (tab < 3);
    if (tab < 3)
        generatorPanel.setKind ((mm::gen::Kind) tab);
    if (tab == 3)
        libraryPanel.refresh();
}

void MelodyMagicPanel::editPhrase (const std::function<void (mm::Phrase&)>& fn)
{
    auto ph = processor.getPhrase();
    fn (ph);
    processor.beginPhraseEdit();
    processor.updatePhrase (ph);
}

//==============================================================================
void MelodyMagicPanel::changeListenerCallback (ChangeBroadcaster*)
{
    updateFromModel();
}

void MelodyMagicPanel::updateFromModel()
{
    const ScopedValueSetter<bool> svs (updating, true);
    const auto& ph = processor.getPhrase();

    lanesBox.setSelectedItemIndex (ph.lanesShown() == 1 ? 0 : (ph.lanesShown() == 3 ? 1 : 2), dontSendNotification);
    rowsBox.setSelectedItemIndex (ph.rowsPerLane() - 1, dontSendNotification);
    int lengthIndex = 0;
    if (! ph.autoSize)
        for (int i = 1; i < 6; ++i)
            if (ph.totalSize == lengthBars[i] * mm::kTicksPerBar)
                lengthIndex = i;
    lengthBox.setSelectedItemIndex (lengthIndex, dontSendNotification);
    if (! ph.autoSize && lengthIndex == 0)
        lengthBox.setText (String (ph.totalSize / (double) mm::kTicksPerBar, 2) + " Bars", dontSendNotification);

    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());

    grid.modelChanged();
    updateSelectionInfo();
    generatorPanel.refresh();
    libraryPanel.refresh();

    String grooveText ("Library > Grooves");
    if (const auto& gr = processor.getGroove())
        grooveText = gr->name.empty() ? processor.getGrooveName().fromLastOccurrenceOf ("/", false, false) : String (gr->name);
    grooveLcd.setContent (processor.getGroove() ? "GROOVE" : "NO GROOVE", grooveText);
    grooveLcd.setDimmed (! processor.getGroove());
    keyMapLcd.setContent (processor.getKeyMap() ? "MAP" : "NO KEY MAP",
                          processor.getKeyMap() ? processor.getKeyMapName().fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".", false, false)
                                                : String ("Library > Key Maps"));
    keyMapLcd.setDimmed (! processor.getKeyMap());

    exportChordsBox.setSelectedItemIndex (processor.exportChords, dontSendNotification);
    exportBarsBox.setSelectedItemIndex (processor.exportBarsIndex, dontSendNotification);
    exportLcd.setContent (processor.describeExportChords(), {});

    updateCustomKeys();
    repaint (phraseArea.toNearestInt());
}

void MelodyMagicPanel::updateSelectionInfo()
{
    const auto& ph = processor.getPhrase();
    const int sel = grid.getSelected();
    if (sel >= 0 && sel < (int) ph.notes.size())
    {
        const auto& n = ph.notes[(size_t) sel];
        selectionLcd.setContent ("N" + String (n.number) + "  OFS " + (n.noteOffset > 0 ? "+" : "") + String (n.noteOffset), "V" + String (n.velocity));
        selectionLcd.setDimmed (false);
    }
    else
    {
        selectionLcd.setContent (String ((int) ph.notes.size()) + " notes", {});
        selectionLcd.setDimmed (true);
    }
}

void MelodyMagicPanel::updateCustomKeys()
{
    const bool custom = processor.currentScaleIndex() == mm::Custom;
    const auto mask = processor.getCustomMask();
    for (int i = 0; i < 12; ++i)
    {
        customKeys[(size_t) i].setVisible (custom);
        customKeys[(size_t) i].setToggleState (((mask >> i) & 1u) != 0, dontSendNotification);
    }
}

void MelodyMagicPanel::timerCallback()
{
    // things that change without a model message (parameters, the last played chord)
    const int scale = processor.currentScaleIndex();
    if (scale != lastScaleIndex)
    {
        lastScaleIndex = scale;
        updateCustomKeys();
        repaint (scaleArea.toNearestInt());
    }
    exportLcd.setContent (processor.describeExportChords(), {});
    if (processor.isLibraryLoading() || currentTab == 3)
        libraryPanel.repaint();
}

//==============================================================================
void MelodyMagicPanel::resized()
{
    // top bar
    undoButton.setBounds (330, 9, 56, 26);
    redoButton.setBounds (390, 9, 56, 26);
    loadButton.setBounds (458, 9, 56, 26);
    saveButton.setBounds (518, 9, 56, 26);
    bypassButton.setBounds (588, 9, 72, 26);
    dragHandle.setBounds (790, 7, 194, 30);

    // phrase toolbar
    const int ty = 72, th = 22;
    lanesBox.setBounds (56, ty, 72, th);
    rowsBox.setBounds (166, ty, 46, th);
    lengthBox.setBounds (262, ty, 72, th);
    snapBox.setBounds (372, ty, 62, th);
    offsetDown.setBounds (488, ty, 22, th);
    offsetUp.setBounds (512, ty, 22, th);
    selectionLcd.setBounds (540, ty, 92, th);
    clearButton.setBounds (636, ty, 48, th);
    grid.setBounds (16, 100, 668, 274);

    // tabs
    const int tabW = 168;
    for (int i = 0; i < 4; ++i)
        tabs[(size_t) i].setBounds ((int) tabsGroove.getX() + 4 + i * (tabW + 2), (int) tabsGroove.getY() + 3, tabW, (int) tabsGroove.getHeight() - 6);
    generatorPanel.setBounds (tabContent);
    libraryPanel.setBounds (tabContent);

    // arp
    modeBox.setBounds (750, 74, 86, 22);
    speedBox.setBounds (900, 74, 84, 22);
    syncButton.setBounds (708, 110, 120, 24);
    ignoreVelButton.setBounds (708, 142, 120, 24);
    randomVelKnob.setBounds (898, 102, 72, 82);

    // scale lock
    keyBox.setBounds (744, 220, 62, 22);
    scaleBox.setBounds (858, 220, 126, 22);
    scaleLockButton.setBounds (708, 250, 276, 26);
    for (int i = 0; i < 12; ++i)
        customKeys[(size_t) i].setBounds (708 + i * 23, 282, 22, 20);

    // groove
    grooveLcd.setBounds (708, 338, 276, 22);
    qStartKnob.setBounds (712, 362, 62, 60);
    qEndKnob.setBounds (782, 362, 62, 60);
    qLinkButton.setBounds (862, 382, 70, 24);

    // midi
    inChannelBox.setBounds (730, 454, 62, 22);
    outChannelBox.setBounds (828, 454, 62, 22);
    keyMapButton.setBounds (900, 454, 84, 22);
    keyMapLcd.setBounds (708, 484, 276, 22);

    // export
    exportChordsBox.setBounds (760, 544, 140, 22);
    exportBarsBox.setBounds (908, 544, 76, 22);
    exportLcd.setBounds (708, 572, 276, 22);

    monitor.setBounds (8, 606, 984, 34);
    helpButton.setBounds (kWidth - 34, kBottomBarY + 6, 22, 22);
    helpOverlay.setBounds (getLocalBounds());
}

//==============================================================================
void MelodyMagicPanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x7a11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2400; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void MelodyMagicPanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Chrome "melody", blue "magic", and a swoosh (same treatment as Virus Delay).
    const float baseline = area.getBottom() - 8.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "melody", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "magic", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (VirusColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::blue.withAlpha (0.0f), sx, sy, VirusColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void MelodyMagicPanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // top bar
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);
    drawWordmark (g, { 16.0f, 2.0f, 280.0f, (float) kTopStripH - 2.0f });
    CustomLookAndFeel::drawSmallLabel (g, "Pattern", { 700.0f, 11.0f, 84.0f, 22.0f }, Justification::centredRight);

    // phrase panel
    const auto& ph = processor.getPhrase();
    String title = "Phrase  |  " + String (ph.name);
    const auto source = processor.getPhraseSource().fromLastOccurrenceOf ("/", false, false).upToLastOccurrenceOf (".fas", false, true);
    if (source.isNotEmpty() && source != String (ph.name))
        title << "   (" << source << ")";
    CustomLookAndFeel::drawPanel (g, phraseArea, title);
    CustomLookAndFeel::drawSmallLabel (g, "Lanes", { 16.0f, 72.0f, 38.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Rows", { 132.0f, 72.0f, 32.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Length", { 216.0f, 72.0f, 44.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Snap", { 338.0f, 72.0f, 32.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Offset", { 440.0f, 72.0f, 46.0f, 22.0f }, Justification::centredLeft);

    // tabs groove
    CustomLookAndFeel::drawInsetBox (g, tabsGroove, 4.0f);

    // right column
    CustomLookAndFeel::drawPanel (g, arpArea, "Arp");
    CustomLookAndFeel::drawSmallLabel (g, "Mode", { 708.0f, 74.0f, 40.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Speed", { 852.0f, 74.0f, 46.0f, 22.0f }, Justification::centredLeft);

    CustomLookAndFeel::drawPanel (g, scaleArea, "Scale Lock");
    CustomLookAndFeel::drawSmallLabel (g, "Key", { 708.0f, 220.0f, 34.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Scale", { 816.0f, 220.0f, 40.0f, 22.0f }, Justification::centredLeft);
    if (processor.currentScaleIndex() != mm::Custom)
    {
        g.setFont (CustomLookAndFeel::uiFont (10.0f));
        g.setColour (VirusColours::labelText.withAlpha (0.75f));
        g.drawFittedText ("Locks live output, Drag MIDI and the generators to the key",
                          Rectangle<int> (708, 282, 276, 20), Justification::centred, 1, 0.8f);
    }

    CustomLookAndFeel::drawPanel (g, grooveArea, "Groove / Quantize");
    CustomLookAndFeel::drawPanel (g, midiArea, "MIDI");
    CustomLookAndFeel::drawSmallLabel (g, "In", { 708.0f, 454.0f, 20.0f, 22.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Out", { 798.0f, 454.0f, 28.0f, 22.0f }, Justification::centredLeft);

    CustomLookAndFeel::drawPanel (g, exportArea, "Drag MIDI Export");
    CustomLookAndFeel::drawSmallLabel (g, "Chords", { 708.0f, 544.0f, 50.0f, 22.0f }, Justification::centredLeft);

    // bottom bar
    auto bottom = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff2b2e32), 0.0f, bottom.getY(), Colour (0xff1c1e21), 0.0f, bottom.getBottom(), false));
    g.fillRect (bottom);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kBottomBarY + 1, 0.0f, (float) kWidth);

    auto tag = Rectangle<float> (12.0f, (float) kBottomBarY + 6.0f, 96.0f, 22.0f);
    g.setGradientFill (ColourGradient (Colour (0xff6b7076), 0.0f, tag.getY(), Colour (0xff3c4045), 0.0f, tag.getBottom(), false));
    g.fillRoundedRectangle (tag, 3.0f);
    g.setColour (VirusColours::bevelDark);
    g.drawRoundedRectangle (tag, 3.0f, 1.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.5f, true));
    g.setColour (Colour (0xffdfe3e6));
    g.drawText ("PHRASE ARP", tag, Justification::centred);

    auto titleBox = Rectangle<float> (180.0f, (float) kBottomBarY + 6.0f, (float) kWidth - 360.0f, 22.0f);
    CustomLookAndFeel::drawInsetBox (g, titleBox, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffe3e6e8));
    g.drawText ("MELODYMAGIC", titleBox, Justification::centred);
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
MelodyMagicEditor::MelodyMagicEditor (MelodyMagicProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.editorWidth.load();

    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 6 / 10, kWidth * 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void MelodyMagicEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.editorWidth.store (getWidth());
}
