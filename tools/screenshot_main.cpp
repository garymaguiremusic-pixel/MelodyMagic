// Renders the editor to a PNG (docs/screenshot.png) without a host:
//   MelodyMagicScreenshot <out.png> [libraryFolder]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "mm/MidiFile.h"
#include <cstdlib>

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File out (File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshot.png"));

    auto proc = std::make_unique<MelodyMagicProcessor>();
    if (argc > 2 && String (argv[2]).isNotEmpty())
        proc->setLibraryFolder (File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    auto set = [&] (const char* id, float v) { if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (mmp::ParamIDs::scaleLock, 1.0f);
    set (mmp::ParamIDs::scaleIndex, 1.0f);   // Natural Minor
    set (mmp::ParamIDs::scaleKey, 9.0f);     // A
    set (mmp::ParamIDs::arpMode, 2.0f);      // Up & Down

    // a held chord through the plugin, so the monitor and the playhead have something to show
    AudioBuffer<float> buffer (2, 512);
    MidiBuffer midi;
    midi.addEvent (MidiMessage::noteOn (1, 57, (uint8) 100), 0);
    midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 100), 0);
    midi.addEvent (MidiMessage::noteOn (1, 64, (uint8) 100), 0);
    for (int i = 0; i < 60; ++i)
    {
        proc->processBlock (buffer, midi);
        midi.clear();
    }

    MessageManager::getInstance()->runDispatchLoopUntil (1500);   // library loads in the background
    {
        auto pt = proc->getPattern();
        pt.bars = 4;
        proc->setPattern (pt);
    }
    proc->generateNewPattern();
    std::unique_ptr<AudioProcessorEditor> editor (proc->createEditor());
    editor->setSize (MelodyMagicPanel::kBaseWidth, MelodyMagicPanel::kBaseHeight);
    for (int i = 0; i < 20; ++i)
    {
        midi.clear();
        proc->processBlock (buffer, midi);
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    // self-checks: Drag MIDI rendering and state save / restore
    const auto midiFile = proc->renderMidiFile();
    std::printf ("drag midi: %s (%d bytes)\n", midiFile.getFullPathName().toRawUTF8(), (int) midiFile.getSize());
    MemoryBlock state;
    proc->getStateInformation (state);
    {
        MelodyMagicProcessor copy;
        copy.setStateInformation (state.getData(), (int) state.getSize());
        const bool same = copy.getPattern() == proc->getPattern()
                       && copy.apvts.getRawParameterValue (mmp::ParamIDs::scaleKey)->load() == 9.0f;
        std::printf ("state round trip: %s (%d bytes)\n", same ? "OK" : "MISMATCH", (int) state.getSize());
    }

    // My MIDI: import a file, learn from it, randomise with its style, save
    if (std::getenv ("MM_TEST_IMPORT") != nullptr)
    {
        mm::MidiClip clip;
        clip.ppq = 96;
        const int line[] = { 69, 72, 76, 74, 72, 71, 69, 64 };
        for (int b = 0; b < 2; ++b)
            for (int i = 0; i < 8; ++i)
                clip.notes.push_back ({ line[(i + b) % 8], 100, 1, b * 384.0 + i * 48.0, b * 384.0 + i * 48.0 + 36.0 });
        const auto src = File::getSpecialLocation (File::tempDirectory).getChildFile ("mm_test_line.mid");
        mm::writeMidiFile (src.getFullPathName().toStdString(), clip);
        String error;
        const bool ok = proc->addMidiToLibrary (src, error);
        std::printf ("import: %s %s, file style %s, %d notes, key %s\n", ok ? "OK" : "FAILED", error.toRawUTF8(),
                     proc->fileStyleActive() ? "on" : "off", proc->numFileNotes(), proc->loadedLibraryKey.toRawUTF8());
        proc->randomiseNotes();
        String notes;
        for (auto& n : proc->getPattern().notes)
            notes << mm::keyName (9 + n.offset) << "@" << n.start / 24 << " ";
        std::printf ("randomised with file style: %s\n", notes.toRawUTF8());
        const auto saved = proc->savePatternToLibrary ("Test Save");
        std::printf ("saved: %s\n", saved.getFullPathName().toRawUTF8());
        MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    out.deleteFile();
    FileOutputStream stream (out);
    PNGImageFormat png;
    const bool ok = stream.openedOk() && png.writeImageToStream (image, stream);
    editor.reset();
    proc.reset();
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
