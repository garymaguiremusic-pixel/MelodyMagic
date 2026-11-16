#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

//==============================================================================
/**
    Preset browser: phrases (library + factory), grooves, key maps and chord clips,
    listed by folder. A single click loads the entry. The library folder can hold the
    pre-converted nora_*_library.json files and/or raw .fas / .qas / .has / .mid files.
*/
class LibraryPanel : public juce::Component
{
public:
    explicit LibraryPanel (MelodyMagicProcessor&);
    ~LibraryPanel() override;

    /** Library or loaded items changed. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    enum Category { Phrases = 0, Factory, Grooves, KeyMaps, Chords, NumCategories };

    struct FolderModel : juce::ListBoxModel
    {
        LibraryPanel& owner;
        explicit FolderModel (LibraryPanel& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void selectedRowsChanged (int row) override;
    };

    struct FileModel : juce::ListBoxModel
    {
        LibraryPanel& owner;
        explicit FileModel (LibraryPanel& o) : owner (o) {}
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;
        void selectedRowsChanged (int row) override;
    };

    void setCategory (int);
    void rebuildKeys();
    void rebuildFiles();
    void loadEntry (int row);
    juce::String currentKeyFor (int category) const;

    MelodyMagicProcessor& proc;
    int category = Phrases;
    std::vector<juce::String> allKeys, folders, files;
    juce::String selectedFolder;
    std::shared_ptr<const mm::Library> lib;

    std::array<juce::TextButton, NumCategories> categoryButtons;
    FolderModel folderModel { *this };
    FileModel fileModel { *this };
    juce::ListBox folderList { "folders", &folderModel }, fileList { "files", &fileModel };
    juce::TextEditor searchBox;
    juce::TextButton folderButton { "Folder..." }, rescanButton { "Rescan" }, unloadButton { "Unload" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool loadingFromList = false;
};
