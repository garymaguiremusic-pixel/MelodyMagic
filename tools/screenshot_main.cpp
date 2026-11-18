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
    if (const char* tab = std::getenv ("MM_LIBRARY_VIEW"))   // e.g. "1|Phrases/kick101/Bass"
        proc->setLibraryView (String (tab).upToFirstOccurrenceOf ("|", false, false).getIntValue(),
                              String (tab).fromFirstOccurrenceOf ("|", false, false));
    {
        auto pt = proc->getPattern();
        pt.bars = 4;
        proc->setPattern (pt);
    }
    proc->newMelody();
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

    // User scenarios (MM_TEST_IMPORT=1)
    if (std::getenv ("MM_TEST_IMPORT") != nullptr)
    {
        auto rhythmOf = [] (const mm::Pattern& p)
        {
            std::vector<std::pair<int, int>> v;
            for (auto& n : p.notes) v.push_back ({ n.start, n.end });
            return v;
        };

        // 1. a library phrase, then New Melody: the pattern must stay exactly the same
        if (auto lib = proc->getLibrary(); lib != nullptr && ! lib->phrases.empty())
        {
            const auto& phrase = lib->phrases.at ("Phrases/kick101/Pluck_Stab/133.fas");
            const auto sc = proc->currentScale();
            proc->setPattern (mm::patternFromPhrase (phrase, &sc, proc->currentKey()));
            const auto before = proc->getPattern();
            bool same = true;
            for (int i = 0; i < 5; ++i)
            {
                proc->newMelody();
                same = same && rhythmOf (proc->getPattern()) == rhythmOf (before);
            }
            std::printf ("phrase + New Melody x5: pattern %s (%d notes)\n", same ? "UNCHANGED" : "CHANGED", (int) before.notes.size());

            // register: New Melody stays from the pattern's lowest note up
            int loBefore = 100, loAfter = 100, hiAfter = -100;
            for (auto& n : before.notes) loBefore = jmin (loBefore, n.offset);
            for (auto& n : proc->getPattern().notes) { loAfter = jmin (loAfter, n.offset); hiAfter = jmax (hiAfter, n.offset); }
            std::printf ("register: pattern lowest %d, new melody %d..%d\n", loBefore, loAfter, hiAfter);

            // top notes only: the bottom notes stay
            proc->setPattern (before);
            if (auto* tp = proc->apvts.getParameter (mmp::ParamIDs::genTopOnly)) tp->setValueNotifyingHost (1.0f);
            proc->newMelody();
            int keptBottom = 0, bottoms = 0;
            for (size_t i = 0; i < before.notes.size(); ++i)
                if (before.notes[i].offset == loBefore)
                {
                    ++bottoms;
                    for (auto& n : proc->getPattern().notes)
                        if (n.start == before.notes[i].start && n.offset == loBefore) { ++keptBottom; break; }
                }
            std::printf ("top notes only: %d of %d bottom notes kept\n", keptBottom, bottoms);
            if (auto* tp = proc->apvts.getParameter (mmp::ParamIDs::genTopOnly)) tp->setValueNotifyingHost (0.0f);

            // 2. New Pattern: the notes (each onset's chord) stay in order, the rhythm changes
            auto onsets = [] (const mm::Pattern& p)
            {
                std::vector<std::vector<int>> v;
                int last = -1;
                for (auto& n : p.notes)
                {
                    if (n.start != last) { v.push_back ({}); last = n.start; }
                    v.back().push_back (n.offset);
                }
                return v;
            };
            const auto before2 = proc->getPattern();
            const auto oldOnsets = onsets (before2);
            proc->newPattern();
            const auto newOnsets = onsets (proc->getPattern());
            size_t k = 0;
            bool inOrder = true;
            for (auto& o : newOnsets)
            {
                size_t j = k;
                while (j < oldOnsets.size() && oldOnsets[j] != o) ++j;
                if (j == oldOnsets.size())   // a new bar starts again from that bar's first notes
                {
                    j = 0;
                    while (j < oldOnsets.size() && oldOnsets[j] != o) ++j;
                }
                inOrder = inOrder && j < oldOnsets.size();
                k = j;
            }
            std::printf ("New Pattern: every chord/note comes from the old pattern: %s, rhythm %s\n",
                         inOrder ? "YES" : "NO", rhythmOf (proc->getPattern()) != rhythmOf (before2) ? "NEW" : "SAME");
        }

        // 3. import a G minor lead while the plugin is in A minor: it must play back exactly
        mm::MidiClip clip;
        clip.ppq = 480;
        const int line[] = { 79, 82, 86, 84, 82, 81, 79, 74, 79, 77, 75, 74, 72, 74, 75, 70 };
        for (int i = 0; i < 16; ++i)
            clip.notes.push_back ({ line[i], 80 + i * 2, 1, 1920.0 + i * 240.0, 1920.0 + i * 240.0 + 200.0 });
        const auto src = File::getSpecialLocation (File::tempDirectory).getChildFile ("mm_test_gminor.mid");
        mm::writeMidiFile (src.getFullPathName().toStdString(), clip);
        String error;
        const bool ok = proc->addMidiToLibrary (src, error);
        std::printf ("import: %s %s -> key %s %s, arp mode %s\n", ok ? "OK" : "FAILED", error.toRawUTF8(),
                     mm::keyName (proc->currentKey()), mm::scaleName (proc->currentScaleIndex()),
                     mm::arpOrderName ((mm::ArpOrder) roundToInt (proc->apvts.getRawParameterValue (mmp::ParamIDs::arpMode)->load())));
        const auto rendered = proc->renderMidiFile();
        mm::MidiClip back;
        mm::readMidiFile (rendered.getFullPathName().toStdString(), back);
        auto b96 = back.rescaled (96);
        int match = 0;
        for (int i = 0; i < 16 && i < (int) b96.notes.size(); ++i)
            if (b96.notes[(size_t) i].note == line[i] && std::abs (b96.notes[(size_t) i].start - i * 48.0) < 0.01
                && b96.notes[(size_t) i].vel == 80 + i * 2)
                ++match;
        std::printf ("import plays back: %d of 16 notes identical (pitch, time, velocity)\n", match);
        File (src).deleteFile();

        // save into a library section, change the key, load it back: same notes, its own key
        const auto before = proc->getPattern();
        const auto saved = proc->savePatternToLibrary ("Saved Arp", "Factory/Arps");
        std::printf ("saved into Factory/Arps: %s -> key %s\n", saved.existsAsFile() ? "OK" : "FAILED",
                     proc->libraryKeyForFile (saved).toRawUTF8());
        if (auto* kp = proc->apvts.getParameter (mmp::ParamIDs::scaleKey)) kp->setValueNotifyingHost (kp->convertTo0to1 (0.0f));
        if (auto* sp = proc->apvts.getParameter (mmp::ParamIDs::scaleIndex)) sp->setValueNotifyingHost (sp->convertTo0to1 (0.0f));
        String err2;
        proc->importMidiFile (saved, err2);
        std::printf ("reloaded: key %s %s, notes %s\n", mm::keyName (proc->currentKey()), mm::scaleName (proc->currentScaleIndex()),
                     proc->getPattern().notes == before.notes ? "IDENTICAL" : "DIFFERENT");
        saved.deleteFile();
        proc->getUserMidiFolder().getChildFile ("mm_test_gminor.mid").deleteFile();
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
