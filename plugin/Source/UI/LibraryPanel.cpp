#include "LibraryPanel.h"
#include "../FactoryPhrases.h"
#include "../Shared/CustomLookAndFeel.h"

using namespace juce;

namespace
{
    constexpr float kHeaderH = 18.0f;

    String lastPart (const String& key)
    {
        auto name = key.fromLastOccurrenceOf ("/", false, false);
        return name.upToLastOccurrenceOf (".", false, false).isNotEmpty() && name.containsChar ('.')
                   ? name.upToLastOccurrenceOf (".", false, false) : name;
    }

    String folderOfKey (const String& key)   { return key.upToLastOccurrenceOf ("/", false, false); }

    const char* categoryNames[] = { "Phrases", "Factory", "Grooves", "Key Maps", "Chord Clips" };
}

//==============================================================================
LibraryPanel::LibraryPanel (MelodyMagicProcessor& p) : proc (p)
{
    for (int i = 0; i < NumCategories; ++i)
    {
        auto& b = categoryButtons[(size_t) i];
        b.setButtonText (categoryNames[i]);
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { setCategory (i); };
        addAndMakeVisible (b);
    }
    categoryButtons[Phrases].setTooltip ("Phrases from your library folder (.fas)");
    categoryButtons[Factory].setTooltip ("Built-in phrases");
    categoryButtons[Grooves].setTooltip ("Quantizer / groove patterns (.qas): set the strength with Q START / Q END");
    categoryButtons[KeyMaps].setTooltip ("MIDI key maps (.has): each key you play becomes a chord (turn on KEY MAP)");
    categoryButtons[Chords].setTooltip ("Chord clips (.mid): the chords Drag MIDI plays the phrase over (EXPORT CHORDS = Chord Clip)");

    for (auto* list : { &folderList, &fileList })
    {
        list->setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        list->setColour (ListBox::outlineColourId, Colours::transparentBlack);
        list->setRowHeight (18);
        addAndMakeVisible (list);
    }

    searchBox.setTextToShowWhenEmpty ("Search...", VirusColours::labelText.withAlpha (0.6f));
    searchBox.setColour (TextEditor::backgroundColourId, VirusColours::insetTop);
    searchBox.setColour (TextEditor::outlineColourId, VirusColours::bevelDark);
    searchBox.setColour (TextEditor::focusedOutlineColourId, VirusColours::blue.withAlpha (0.6f));
    searchBox.setColour (TextEditor::textColourId, Colours::white);
    searchBox.setFont (CustomLookAndFeel::uiFont (12.0f));
    searchBox.onTextChange = [this] { rebuildFiles(); };
    addAndMakeVisible (searchBox);

    folderButton.setTooltip ("Choose the library folder: the folder holding nora_*_library.json and/or your .fas / .qas / .has / .mid files");
    folderButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Library folder", proc.getLibraryFolder());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir.isDirectory())
                                      proc.setLibraryFolder (dir);
                              });
    };
    rescanButton.setTooltip ("Read the library folder again");
    rescanButton.onClick = [this] { proc.rescanLibrary(); };
    unloadButton.onClick = [this]
    {
        if (category == Grooves)  proc.setGroove (std::nullopt, {});
        if (category == KeyMaps)  proc.setKeyMap (std::nullopt, {});
        if (category == Chords)   proc.setChordClip (std::nullopt, {});
    };
    for (auto* b : { &folderButton, &rescanButton, &unloadButton })
        addAndMakeVisible (b);

    setCategory (Phrases);
}

LibraryPanel::~LibraryPanel()
{
    folderList.setModel (nullptr);
    fileList.setModel (nullptr);
}

//==============================================================================
void LibraryPanel::refresh()
{
    const auto newLib = proc.getLibrary();
    if (newLib != lib)
    {
        lib = newLib;
        rebuildKeys();
    }
    fileList.repaint();
    repaint();
}

void LibraryPanel::setCategory (int c)
{
    category = c;
    for (int i = 0; i < NumCategories; ++i)
        categoryButtons[(size_t) i].setToggleState (i == c, dontSendNotification);
    unloadButton.setVisible (c == Grooves || c == KeyMaps || c == Chords);
    unloadButton.setTooltip (c == Grooves ? "No groove" : (c == KeyMaps ? "No key map" : "No chord clip"));
    selectedFolder = {};
    lib = proc.getLibrary();
    rebuildKeys();
    resized();
}

void LibraryPanel::rebuildKeys()
{
    allKeys.clear();
    if (category == Factory)
    {
        for (auto& [key, ph] : mmp::factoryPhrases())
            allKeys.push_back (String (key));
    }
    else if (lib != nullptr)
    {
        auto addAll = [this] (const auto& map) { for (auto& kv : map) allKeys.push_back (String::fromUTF8 (kv.first.c_str())); };
        switch (category)
        {
            case Phrases: addAll (lib->phrases); break;
            case Grooves: addAll (lib->grooves); break;
            case KeyMaps: addAll (lib->keyMaps); break;
            case Chords:  addAll (lib->clips); break;
            default: break;
        }
    }

    folders.clear();
    folders.push_back ("All");
    StringArray unique;
    for (auto& k : allKeys)
        unique.addIfNotAlreadyThere (folderOfKey (k));
    unique.sortNatural();
    for (auto& f : unique)
        folders.push_back (f);

    folderList.updateContent();
    int folderRow = 0;
    for (int i = 0; i < (int) folders.size(); ++i)
        if (folders[(size_t) i] == selectedFolder)
            folderRow = i;
    folderList.selectRow (folderRow, false, true);
    selectedFolder = folders[(size_t) folderRow];
    rebuildFiles();
}

void LibraryPanel::rebuildFiles()
{
    files.clear();
    const auto words = StringArray::fromTokens (searchBox.getText().trim(), true);
    const bool all = selectedFolder == "All" || selectedFolder.isEmpty();
    for (auto& k : allKeys)
    {
        if (! all && folderOfKey (k) != selectedFolder)
            continue;
        bool match = true;
        for (auto& w : words)
            if (! k.containsIgnoreCase (w))
                match = false;
        if (match)
            files.push_back (k);
    }
    std::sort (files.begin(), files.end(), [] (const String& a, const String& b) { return a.compareNatural (b) < 0; });
    fileList.updateContent();
    fileList.deselectAllRows();
    fileList.repaint();
    repaint();
}

String LibraryPanel::currentKeyFor (int c) const
{
    switch (c)
    {
        case Phrases:
        case Factory: return proc.getPhraseSource();
        case Grooves: return proc.getGrooveName();
        case KeyMaps: return proc.getKeyMapName();
        case Chords:  return proc.getChordClipName();
        default:      return {};
    }
}

void LibraryPanel::loadEntry (int row)
{
    if (row < 0 || row >= (int) files.size())
        return;
    const auto key = files[(size_t) row];
    const auto k = key.toStdString();

    if (category == Factory)
    {
        proc.loadPhrase (mmp::factoryPhrases().at (k), key);
        return;
    }
    if (lib == nullptr)
        return;

    switch (category)
    {
        case Phrases:
            if (auto it = lib->phrases.find (k); it != lib->phrases.end())
                proc.loadPhrase (it->second, key);
            break;
        case Grooves:
            if (auto it = lib->grooves.find (k); it != lib->grooves.end())
                proc.setGroove (it->second, key);
            break;
        case KeyMaps:
            if (auto it = lib->keyMaps.find (k); it != lib->keyMaps.end())
            {
                proc.setKeyMap (it->second, key);
                if (auto* param = proc.apvts.getParameter (mmp::ParamIDs::keyMapEnabled))
                    param->setValueNotifyingHost (1.0f);
            }
            break;
        case Chords:
            if (auto it = lib->clips.find (k); it != lib->clips.end())
                proc.setChordClip (it->second, key);
            break;
        default: break;
    }
}

//==============================================================================
int LibraryPanel::FolderModel::getNumRows()   { return (int) owner.folders.size(); }

void LibraryPanel::FolderModel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) owner.folders.size())
        return;
    if (selected)
    {
        g.setColour (Colour (0xff2a6fb8));
        g.fillRect (0, 0, w, h);
    }
    auto text = owner.folders[(size_t) row];
    // drop the common prefix ("Phrases/kick101/") so the useful part fits
    if (text != "All")
        text = text.fromFirstOccurrenceOf ("/", false, false).isNotEmpty() ? text.fromFirstOccurrenceOf ("/", false, false) : text;
    g.setFont (CustomLookAndFeel::uiFont (12.0f, row == 0));
    g.setColour (selected ? Colours::white : Colour (0xffdfe3e6));
    g.drawFittedText (text, 6, 0, w - 8, h, Justification::centredLeft, 1, 0.8f);
}

void LibraryPanel::FolderModel::selectedRowsChanged (int row)
{
    if (row < 0 || row >= (int) owner.folders.size())
        return;
    owner.selectedFolder = owner.folders[(size_t) row];
    owner.rebuildFiles();
}

int LibraryPanel::FileModel::getNumRows()   { return (int) owner.files.size(); }

void LibraryPanel::FileModel::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) owner.files.size())
        return;
    const auto& key = owner.files[(size_t) row];
    const bool loaded = key == owner.currentKeyFor (owner.category);
    if (selected)
    {
        g.setColour (Colour (0xff2a6fb8).withAlpha (0.8f));
        g.fillRect (0, 0, w, h);
    }
    g.setFont (CustomLookAndFeel::uiFont (12.0f, loaded));
    g.setColour (loaded && ! selected ? VirusColours::blue : Colour (0xffe8ebee));
    g.drawFittedText (lastPart (key), 6, 0, w / 2, h, Justification::centredLeft, 1, 0.8f);

    if (owner.selectedFolder == "All" || owner.searchBox.getText().isNotEmpty())
    {
        g.setFont (CustomLookAndFeel::uiFont (10.5f));
        g.setColour (VirusColours::labelText.withAlpha (0.7f));
        g.drawFittedText (folderOfKey (key), w / 2, 0, w / 2 - 6, h, Justification::centredRight, 1, 0.7f);
    }
}

void LibraryPanel::FileModel::listBoxItemClicked (int row, const MouseEvent&)   { owner.loadEntry (row); owner.fileList.repaint(); }
void LibraryPanel::FileModel::returnKeyPressed (int row)                        { owner.loadEntry (row); owner.fileList.repaint(); }

void LibraryPanel::FileModel::selectedRowsChanged (int row)
{
    // arrow keys audition: load as the selection moves
    if (row >= 0 && owner.fileList.hasKeyboardFocus (true))
        owner.loadEntry (row);
    owner.fileList.repaint();
}

//==============================================================================
void LibraryPanel::paint (Graphics& g)
{
    CustomLookAndFeel::drawPanel (g, getLocalBounds().toFloat(), "Library");
    CustomLookAndFeel::drawInsetBox (g, folderList.getBounds().toFloat().expanded (2.0f), 3.0f);
    CustomLookAndFeel::drawInsetBox (g, fileList.getBounds().toFloat().expanded (2.0f), 3.0f);

    String status;
    if (proc.isLibraryLoading())
        status = "Loading library...";
    else if (category != Factory && allKeys.empty())
        status = "Empty. Click Folder... and choose the folder with nora_*_library.json or your preset files.";
    else
        status = String ((int) files.size()) + " of " + String ((int) allKeys.size()) + "   |   " + proc.getLibraryFolder().getFullPathName();

    g.setFont (CustomLookAndFeel::uiFont (10.0f));
    g.setColour (VirusColours::labelText.withAlpha (0.8f));
    g.drawFittedText (status, Rectangle<int> (8, getHeight() - 15, getWidth() - 16, 13), Justification::centredLeft, 1, 0.7f);
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds().withTrimmedTop ((int) kHeaderH + 6).reduced (8, 0).withTrimmedBottom (18);

    auto top = area.removeFromTop (22);
    const int bw = 82;
    for (auto& b : categoryButtons)
    {
        b.setBounds (top.removeFromLeft (bw));
        top.removeFromLeft (4);
    }
    rescanButton.setBounds (top.removeFromRight (62));
    top.removeFromRight (4);
    folderButton.setBounds (top.removeFromRight (72));
    top.removeFromRight (4);
    if (unloadButton.isVisible())
        unloadButton.setBounds (top.removeFromRight (62));

    area.removeFromTop (6);
    auto left = area.removeFromLeft (220);
    folderList.setBounds (left.reduced (2));
    area.removeFromLeft (8);
    searchBox.setBounds (area.removeFromTop (20).reduced (2, 0));
    area.removeFromTop (4);
    fileList.setBounds (area.reduced (2));
}
