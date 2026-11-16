// Renders the editor to a PNG (docs/screenshot.png) without a host:
//   MelodyMagicScreenshot <out.png> [libraryFolder] [tab 0..3] [scale]
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI init;
    const File out (File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshot.png"));

    auto proc = std::make_unique<MelodyMagicProcessor>();
    if (argc > 2 && String (argv[2]).isNotEmpty())
        proc->setLibraryFolder (File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc > 3)
        proc->activeTab = String (argv[3]).getIntValue();

    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    auto set = [&] (const char* id, float v) { if (auto* p = proc->apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (mmp::ParamIDs::scaleLock, 1.0f);
    set (mmp::ParamIDs::scaleIndex, 1.0f);   // Natural Minor
    set (mmp::ParamIDs::scaleKey, 9.0f);     // A
    set (mmp::ParamIDs::internalSync, 1.0f);
    set (mmp::ParamIDs::quantizeStart, 35.0f);

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
        const bool same = copy.getPhrase() == proc->getPhrase()
                       && copy.apvts.getRawParameterValue (mmp::ParamIDs::scaleKey)->load() == 9.0f;
        std::printf ("state round trip: %s (%d bytes)\n", same ? "OK" : "MISMATCH", (int) state.getSize());
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
