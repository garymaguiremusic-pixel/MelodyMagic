#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPhrases.h"
#include "mm/MidiFile.h"
#include "mm/Progressions.h"

using namespace juce;

namespace
{
    constexpr int kMaxUndo = 100;

    // mm::vt::Tree <-> juce::ValueTree, so phrase / groove / key map are saved with their
    // original type and property names (SECUENCIA, Note_Info, Time_Start ...).
    ValueTree toJuce (const mm::vt::Tree& t)
    {
        ValueTree v (Identifier (t.type.c_str()));
        for (auto& [name, value] : t.props)
        {
            using T = mm::vt::Var::Type;
            var x;
            switch (value.type)
            {
                case T::Int:       x = (int) value.i; break;
                case T::Int64:     x = (int64) value.i; break;
                case T::BoolTrue:  x = true; break;
                case T::BoolFalse: x = false; break;
                case T::Double:    x = value.d; break;
                case T::String:    x = String::fromUTF8 (value.s.c_str(), (int) value.s.size()); break;
                case T::Void:
                case T::Array:
                case T::Binary:    continue;
            }
            v.setProperty (Identifier (name.c_str()), x, nullptr);
        }
        for (auto& c : t.children)
            v.appendChild (toJuce (c), nullptr);
        return v;
    }

    mm::vt::Tree fromJuce (const ValueTree& v)
    {
        mm::vt::Tree t (v.getType().toString().toStdString());
        for (int i = 0; i < v.getNumProperties(); ++i)
        {
            const auto name = v.getPropertyName (i);
            const auto& x = v.getProperty (name);
            mm::vt::Var value;
            if (x.isBool())         value = mm::vt::Var::ofBool ((bool) x);
            else if (x.isInt())     value = mm::vt::Var::ofInt ((int) x);
            else if (x.isInt64())   value = mm::vt::Var::ofInt64 ((int64) x);
            else if (x.isDouble())  value = mm::vt::Var::ofDouble ((double) x);
            else                    value = mm::vt::Var::ofString (x.toString().toStdString());
            t.props.emplace_back (name.toString().toStdString(), value);
        }
        for (const auto& c : v)
            t.children.push_back (fromJuce (c));
        return t;
    }

    template <typename T>
    void publish (std::shared_ptr<const T>& slot, std::shared_ptr<const T> next, std::vector<std::shared_ptr<const void>>& graveyard)
    {
        auto old = std::atomic_exchange (&slot, std::move (next));
        if (old != nullptr)
            graveyard.push_back (std::move (old));
    }

    String joinInts (const std::vector<int>& v)
    {
        StringArray s;
        for (int x : v) s.add (String (x));
        return s.joinIntoString (",");
    }

    std::vector<int> splitInts (const String& text)
    {
        std::vector<int> v;
        for (auto& s : StringArray::fromTokens (text, ",", ""))
            if (s.isNotEmpty())
                v.push_back (s.getIntValue());
        return v;
    }
}

//==============================================================================
MelodyMagicProcessor::MelodyMagicProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", mmp::createParameterLayout())
{
    bypassParam = dynamic_cast<AudioParameterBool*> (apvts.getParameter (mmp::ParamIDs::bypassed));
    core.setRng (&rng);

    for (int k = 0; k < mm::gen::kNumKinds; ++k)
    {
        auto& g = gens[(size_t) k];
        g.values = mm::gen::defaults ((mm::gen::Kind) k);
        g.locks = mm::gen::defaultLocks ((mm::gen::Kind) k);
        g.seed = mm::JuceLcg::timeSeed() + k;
    }

    phrase = mmp::defaultPhrase();
    phraseSource = "Factory/Arps/Up Down";
    library = std::make_shared<mm::Library>();

    loadGlobalSettings();
    rebuildPlayPhrase();
    publishKeyMap();
    rescanLibrary();
    startTimerHz (20);
}

MelodyMagicProcessor::~MelodyMagicProcessor()
{
    stopTimer();
    loaderPool.removeAllJobs (true, 5000);
}

//==============================================================================
bool MelodyMagicProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono() || out.isDisabled();
}

void MelodyMagicProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    hostEvents.reserve (1024);
    outEvents.reserve (2048);
    otherMidi.ensureSize (4096);
}

AudioProcessorParameter* MelodyMagicProcessor::getBypassParameter() const
{
    return bypassParam;
}

mm::CoreSettings MelodyMagicProcessor::readSettings() const
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    mm::CoreSettings s;
    s.engine.staticMode = raw (mmp::ParamIDs::staticMode) > 0.5f;
    s.engine.ignoreHostVelocities = raw (mmp::ParamIDs::ignoreHostVel) > 0.5f;
    s.engine.randomVel = jlimit (0, 127, roundToInt (raw (mmp::ParamIDs::randomVel)));
    s.engine.scale = currentScale();
    s.engine.scaleLock = raw (mmp::ParamIDs::scaleLock) > 0.5f;
    s.speed = mmp::speedValue (roundToInt (raw (mmp::ParamIDs::speed)));
    s.forceInternalSync = raw (mmp::ParamIDs::internalSync) > 0.5f;
    s.bypassed = raw (mmp::ParamIDs::bypassed) > 0.5f;
    s.keyMapEnabled = raw (mmp::ParamIDs::keyMapEnabled) > 0.5f;
    s.inputChannel = mmp::channelValue (roundToInt (raw (mmp::ParamIDs::inputChannel)));
    s.outputChannel = mmp::channelValue (roundToInt (raw (mmp::ParamIDs::outputChannel)));
    return s;
}

int MelodyMagicProcessor::currentKey() const
{
    return jlimit (0, 11, roundToInt (apvts.getRawParameterValue (mmp::ParamIDs::scaleKey)->load()));
}

int MelodyMagicProcessor::currentScaleIndex() const
{
    return jlimit (0, mm::kNumScales - 1, roundToInt (apvts.getRawParameterValue (mmp::ParamIDs::scaleIndex)->load()));
}

mm::ScaleContext MelodyMagicProcessor::currentScale() const
{
    return mm::ScaleContext::make (currentScaleIndex(), currentKey(), customMask.load());
}

//==============================================================================
void MelodyMagicProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    mm::TransportInfo t;
    t.sampleRate = currentSampleRate;
    t.bpm = lastBpm.load();
    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            t.playing = pos->getIsPlaying();
            if (auto bpm = pos->getBpm())
                t.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition())
            {
                t.hasPosition = true;
                t.ppqPosition = *ppq;
            }
        }
    }
    if (t.bpm > 1.0)
        lastBpm.store (t.bpm);

    hostEvents.clear();
    otherMidi.clear();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int time = jlimit (0, jmax (0, numSamples - 1), meta.samplePosition);
        if (m.isNoteOn())
            hostEvents.push_back (mm::Event::on (time, m.getNoteNumber(), (int) m.getVelocity(), m.getChannel()));
        else if (m.isNoteOff())
            hostEvents.push_back (mm::Event::off (time, m.getNoteNumber(), m.getChannel()));
        else
            otherMidi.addEvent (m, time);   // controllers, pitch bend... pass straight through
    }

    const auto settings = readSettings();
    const auto phrasePtr = std::atomic_load (&livePhrase);
    const auto keyMapPtr = std::atomic_load (&liveKeyMap);

    outEvents.clear();
    core.process (t, numSamples, settings, phrasePtr.get(), keyMapPtr.get(), hostEvents, outEvents);

    midi.clear();
    midi.addEvents (otherMidi, 0, -1, 0);
    for (auto& e : outEvents)
    {
        const int time = jlimit (0, jmax (0, numSamples - 1), e.time);
        const int ch = jlimit (1, 16, e.channel);
        if (e.type == mm::Event::On)
            midi.addEvent (MidiMessage::noteOn (ch, e.note, (uint8) jlimit (1, 127, e.vel)), time);
        else if (e.type == mm::Event::Off)
            midi.addEvent (MidiMessage::noteOff (ch, e.note), time);

        if (e.type == mm::Event::On || e.type == mm::Event::Off)
        {
            const auto scope = monitorFifo.write (1);
            if (scope.blockSize1 > 0)
                monitorBuffer[(size_t) scope.startIndex1] = { e.note, e.vel, e.channel, e.type == mm::Event::On };
        }
    }

    // live info for the editor
    const auto& chord = core.getChord();
    const int n = jmin ((int) chord.size(), (int) heldChord.size());
    for (int i = 0; i < n; ++i)
        heldChord[(size_t) i].store (chord[(size_t) i].note);
    heldChordCount.store (n);
    if (n > 0)
    {
        for (int i = 0; i < n; ++i)
            lastChord[(size_t) i].store (chord[(size_t) i].note);
        lastChordCount.store (n);
    }
    phrasePosition.store (core.getPhrasePosition());
}

std::vector<int> MelodyMagicProcessor::getHeldChord() const
{
    std::vector<int> v;
    const int n = heldChordCount.load();
    for (int i = 0; i < n; ++i)
        v.push_back (heldChord[(size_t) i].load());
    return v;
}

int MelodyMagicProcessor::readMonitor (MonitorEvent* dest, int max)
{
    const auto scope = monitorFifo.read (jmin (max, monitorFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = monitorBuffer[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = monitorBuffer[(size_t) (scope.startIndex2 + i)];
    return n;
}

//==============================================================================
// Phrase model
//==============================================================================
void MelodyMagicProcessor::pushUndo()
{
    undoStack.push_back (phrase);
    if ((int) undoStack.size() > kMaxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void MelodyMagicProcessor::setPhrase (const mm::Phrase& p, const String& source, bool shouldPushUndo)
{
    if (shouldPushUndo)
        pushUndo();
    phrase = p;
    phraseSource = source;
    rebuildPlayPhrase();
    sendChangeMessage();
}

void MelodyMagicProcessor::beginPhraseEdit()
{
    pushUndo();
    lastGenerated = -1;
}

void MelodyMagicProcessor::updatePhrase (const mm::Phrase& p)
{
    phrase = p;
    rebuildPlayPhrase();
    sendChangeMessage();
}

void MelodyMagicProcessor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (phrase);
    phrase = undoStack.back();
    undoStack.pop_back();
    lastGenerated = -1;
    rebuildPlayPhrase();
    sendChangeMessage();
}

void MelodyMagicProcessor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (phrase);
    phrase = redoStack.back();
    redoStack.pop_back();
    lastGenerated = -1;
    rebuildPlayPhrase();
    sendChangeMessage();
}

bool MelodyMagicProcessor::loadPhraseFile (const File& f, String& error)
{
    mm::Phrase p;
    std::string err;
    if (! mm::loadPhraseFile (f.getFullPathName().toStdString(), p, &err))
    {
        error = err;
        return false;
    }
    loadPhrase (p, f.getFileName());
    return true;
}

bool MelodyMagicProcessor::savePhraseFile (const File& f)
{
    auto p = phrase;
    p.name = f.getFileNameWithoutExtension().toStdString();
    return mm::savePhraseFile (f.getFullPathName().toStdString(), p);
}

void MelodyMagicProcessor::rebuildPlayPhrase()
{
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    lastQStart = roundToInt (raw (mmp::ParamIDs::quantizeStart));
    lastQEnd = roundToInt (raw (mmp::ParamIDs::quantizeEnd));
    lastQLink = raw (mmp::ParamIDs::quantizeLink) > 0.5f ? 1 : 0;

    auto pp = std::make_shared<mm::PlayPhrase> (mm::makePlayPhrase (phrase, groove ? &*groove : nullptr,
                                                                      lastQStart / 100.0, lastQEnd / 100.0, lastQLink != 0));
    publish<mm::PlayPhrase> (livePhrase, std::move (pp), graveyard);
}

void MelodyMagicProcessor::publishKeyMap()
{
    std::shared_ptr<const mm::KeyMap> km;
    if (keyMap)
        km = std::make_shared<mm::KeyMap> (*keyMap);
    publish<mm::KeyMap> (liveKeyMap, std::move (km), graveyard);
}

void MelodyMagicProcessor::setGroove (std::optional<mm::Groove> g, const String& name)
{
    groove = std::move (g);
    grooveName = groove ? name : String();
    rebuildPlayPhrase();
    sendChangeMessage();
}

void MelodyMagicProcessor::setKeyMap (std::optional<mm::KeyMap> k, const String& name)
{
    keyMap = std::move (k);
    keyMapName = keyMap ? name : String();
    publishKeyMap();
    sendChangeMessage();
}

void MelodyMagicProcessor::setChordClip (std::optional<mm::MidiClip> c, const String& name)
{
    chordClip = std::move (c);
    chordClipName = chordClip ? name : String();
    if (chordClip)
        exportChords = mmp::kExportClip;
    sendChangeMessage();
}

void MelodyMagicProcessor::setCustomMask (std::uint16_t m)
{
    customMask.store ((std::uint16_t) (m & 0x0fff));
    sendChangeMessage();
}

void MelodyMagicProcessor::timerCallback()
{
    // quantizer knobs moved (from the editor or host automation): rebuild the playback phrase
    auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    if (roundToInt (raw (mmp::ParamIDs::quantizeStart)) != lastQStart
        || roundToInt (raw (mmp::ParamIDs::quantizeEnd)) != lastQEnd
        || (raw (mmp::ParamIDs::quantizeLink) > 0.5f ? 1 : 0) != lastQLink)
        rebuildPlayPhrase();

    // objects the audio thread no longer uses are freed here, never on the audio thread
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const std::shared_ptr<const void>& p) { return p.use_count() <= 1; }),
                     graveyard.end());
}

//==============================================================================
// Generators
//==============================================================================
void MelodyMagicProcessor::setGeneratorValue (mm::gen::Kind k, int index, int value)
{
    auto& g = gens[(size_t) k];
    if (index < 0 || index >= (int) g.values.size())
        return;
    g.values[(size_t) index] = value;

    // 303 mutator knobs only apply when Mutate is pressed
    if (k == mm::gen::Kind::Random303 && index >= mm::gen::indexOf (k, "mShow"))
        return;
    runGenerator (k, false);
}

void MelodyMagicProcessor::runGenerator (mm::gen::Kind k, bool newSeed)
{
    auto& g = gens[(size_t) k];
    if (newSeed)
        g.seed = mm::JuceLcg::timeSeed();
    mm::JuceLcg r (g.seed);

    const mm::gen::Layout layout { phrase.lanesShown(), phrase.rowsPerLane() };
    mm::Phrase p;
    switch (k)
    {
        case mm::gen::Kind::Random303: p = mm::gen::generate303 (g.values, layout, r); break;
        case mm::gen::Kind::ChordBass: p = mm::gen::generateChordBass (g.values, layout, r); break;
        case mm::gen::Kind::Mutate:
            if (! snapshot)
                snapshot = phrase;
            p = mm::gen::mutate (*snapshot, g.values, currentScale(), r);
            break;
    }

    const bool undoable = newSeed || lastGenerated != (int) k;
    lastGenerated = (int) k;
    setPhrase (p, mm::gen::kindName (k), undoable);
}

void MelodyMagicProcessor::run303Mutator (bool newSeed)
{
    if (newSeed)
        mutatorSeed = mm::JuceLcg::timeSeed();
    if (! snapshot)
        snapshot = phrase;
    mm::JuceLcg r (mutatorSeed);
    const auto p = mm::gen::mutate303 (*snapshot, gens[(size_t) mm::gen::Kind::Random303].values, r);
    lastGenerated = -1;
    setPhrase (p, "303 Mutator", true);
}

void MelodyMagicProcessor::randomizeGenerator (mm::gen::Kind k)
{
    auto& g = gens[(size_t) k];
    mm::JuceLcg r (mm::JuceLcg::timeSeed());
    mm::gen::randomize (k, g.values, g.locks, r);
    runGenerator (k, true);
}

void MelodyMagicProcessor::resetGenerator (mm::gen::Kind k)
{
    auto& g = gens[(size_t) k];
    g.values = mm::gen::defaults (k);
    g.locks = mm::gen::defaultLocks (k);
    sendChangeMessage();
}

void MelodyMagicProcessor::takeSnapshot()
{
    snapshot = phrase;
    sendChangeMessage();
}

//==============================================================================
// Drag MIDI export
//==============================================================================
String MelodyMagicProcessor::describeExportChords() const
{
    if (exportChords == mmp::kExportClip)
        return chordClip ? "Clip: " + chordClipName : "No chord clip loaded: plays the key chord (I)";
    if (exportChords == mmp::kExportLastChord)
    {
        const int n = lastChordCount.load();
        if (n == 0)
            return "No chord played yet: uses the key chord (I)";
        StringArray names;
        for (int i = 0; i < n; ++i)
            names.add (mmp::noteName (lastChord[(size_t) i].load()));
        return "Chord: " + names.joinIntoString (" ");
    }
    return String (mm::keyName (currentKey())) + " " + mm::scaleName (currentScaleIndex()) + ": "
         + mm::progressionNames()[(size_t) jlimit (0, (int) mm::progressionNames().size() - 1, exportChords - mmp::kExportFirstProgression)];
}

File MelodyMagicProcessor::renderMidiFile()
{
    const int phraseBars = jmax (1, phrase.lengthTicks() / mm::kTicksPerBar);
    const int bars = mmp::exportBars (exportBarsIndex, phraseBars);
    const auto scale = currentScale();
    const int key = currentKey();
    const int root = 60 + key - (key >= 7 ? 12 : 0);   // keeps the root between G2 and F#3

    int maxRow = 0;
    for (auto& n : phrase.notes)
        maxRow = jmax (maxRow, n.row());
    const int chordSize = jlimit (3, 7, maxRow + 1);

    std::vector<mm::TimedNote> input;
    if (exportChords == mmp::kExportClip && chordClip && ! chordClip->notes.empty())
    {
        const double clipLen = jmax ((double) mm::kTicksPerBar,
                                     std::ceil (chordClip->lengthTicks() / mm::kTicksPerBar - 1.0e-9) * mm::kTicksPerBar);
        for (double offset = 0.0; offset < bars * (double) mm::kTicksPerBar; offset += clipLen)
            for (auto n : chordClip->notes)
            {
                n.start += offset;
                n.end += offset;
                n.channel = 1;
                input.push_back (n);
            }
    }
    else if (exportChords == mmp::kExportLastChord && lastChordCount.load() > 0)
    {
        std::vector<int> notes;
        for (int i = 0; i < lastChordCount.load(); ++i)
            notes.push_back (lastChord[(size_t) i].load());
        input = mm::holdChord (notes, bars);
    }
    else
    {
        const int prog = exportChords >= mmp::kExportFirstProgression ? exportChords - mmp::kExportFirstProgression : 0;
        input = mm::buildProgression (prog, scale, root, bars, chordSize);
    }

    mm::RenderOptions o;
    o.settings = readSettings();
    o.settings.bypassed = false;
    o.settings.forceInternalSync = false;
    o.settings.inputChannel = 0;
    if (o.settings.outputChannel < 0)
        o.settings.outputChannel = 0;
    o.bpm = lastBpm.load();
    o.lengthTicks = bars * (double) mm::kTicksPerBar;

    mm::JuceLcg r (mm::JuceLcg::timeSeed());
    const auto play = mm::makePlayPhrase (phrase, groove ? &*groove : nullptr,
                                          lastQStart / 100.0, lastQEnd / 100.0, lastQLink != 0);
    mm::MidiClip clip;
    clip.ppq = mm::kPhrasePpq;
    clip.bpm = o.bpm;
    clip.notes = mm::renderOffline (play, o.settings.keyMapEnabled && keyMap ? &*keyMap : nullptr, input, o, &r);

    const bool lock = o.settings.engine.scaleLock;
    String name = "MelodyMagic " + String (phrase.name);
    if (lock)
        name << " " << mm::keyName (key) << " " << mm::scaleName (currentScaleIndex());
    name << " " << bars << (bars == 1 ? " bar" : " bars") << " " << roundToInt (o.bpm) << " BPM";

    auto folder = File::getSpecialLocation (File::tempDirectory).getChildFile ("MelodyMagic");
    folder.createDirectory();
    auto file = folder.getChildFile (File::createLegalFileName (name) + ".mid");
    if (! mm::writeMidiFile (file.getFullPathName().toStdString(), clip, phrase.name))
        return {};
    return file;
}

//==============================================================================
// Library
//==============================================================================
std::shared_ptr<const mm::Library> MelodyMagicProcessor::getLibrary() const
{
    return std::atomic_load (&library);
}

void MelodyMagicProcessor::setLibraryFolder (const File& folder)
{
    libraryFolder = folder;
    saveGlobalSettings();
    rescanLibrary();
}

void MelodyMagicProcessor::rescanLibrary()
{
    libraryLoading.store (true);
    sendChangeMessage();

    const auto folder = libraryFolder.getFullPathName().toStdString();
    WeakReference<MelodyMagicProcessor> weak (this);
    loaderPool.addJob ([folder, weak]
    {
        auto lib = std::make_shared<mm::Library>();
        lib->scanFolder (folder);
        MessageManager::callAsync ([weak, lib]
        {
            if (auto* self = weak.get())
            {
                std::atomic_store (&self->library, std::shared_ptr<const mm::Library> (lib));
                self->libraryLoading.store (false);
                self->sendChangeMessage();
            }
        });
    });
}

void MelodyMagicProcessor::loadGlobalSettings()
{
    PropertiesFile::Options options;
    options.applicationName = "MelodyMagic";
    options.folderName = "MelodyMagic";
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    globalSettings = std::make_unique<PropertiesFile> (options);

    const auto defaultFolder = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("MelodyMagic").getChildFile ("Library");
    libraryFolder = File (globalSettings->getValue ("libraryFolder", defaultFolder.getFullPathName()));
    if (libraryFolder == defaultFolder)
        defaultFolder.createDirectory();
}

void MelodyMagicProcessor::saveGlobalSettings()
{
    if (globalSettings != nullptr)
    {
        globalSettings->setValue ("libraryFolder", libraryFolder.getFullPathName());
        globalSettings->saveIfNeeded();
    }
}

//==============================================================================
// State
//==============================================================================
void MelodyMagicProcessor::getStateInformation (MemoryBlock& destData)
{
    ValueTree root ("MELODYMAGIC");
    root.setProperty ("Version", 1, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    ValueTree phraseState ("PHRASE");
    phraseState.setProperty ("Source", phraseSource, nullptr);
    phraseState.appendChild (toJuce (mm::phraseToTrees (phrase)[1]), nullptr);
    root.appendChild (phraseState, nullptr);

    if (groove)
    {
        ValueTree g ("GROOVE");
        g.setProperty ("Name", grooveName, nullptr);
        g.appendChild (toJuce (mm::grooveToTrees (*groove)[1]), nullptr);
        root.appendChild (g, nullptr);
    }
    if (keyMap)
    {
        ValueTree k ("KEY_MAP");
        k.setProperty ("Name", keyMapName, nullptr);
        k.appendChild (toJuce (mm::keyMapToTrees (*keyMap)[1]), nullptr);
        root.appendChild (k, nullptr);
    }
    if (chordClip)
    {
        ValueTree c ("CHORD_CLIP");
        c.setProperty ("Name", chordClipName, nullptr);
        const auto bytes = mm::writeMidi (*chordClip);
        c.setProperty ("Midi", Base64::toBase64 (bytes.data(), bytes.size()), nullptr);
        root.appendChild (c, nullptr);
    }
    if (snapshot)
    {
        ValueTree s ("SNAPSHOT");
        s.appendChild (toJuce (mm::phraseToTrees (*snapshot)[1]), nullptr);
        root.appendChild (s, nullptr);
    }

    ValueTree gensTree ("GENERATORS");
    for (int k = 0; k < mm::gen::kNumKinds; ++k)
    {
        const auto& g = gens[(size_t) k];
        ValueTree t ("GENERATOR");
        t.setProperty ("Kind", k, nullptr);
        t.setProperty ("Values", joinInts (g.values), nullptr);
        std::vector<int> locks;
        for (bool b : g.locks) locks.push_back (b ? 1 : 0);
        t.setProperty ("Locks", joinInts (locks), nullptr);
        t.setProperty ("Seed", (int64) g.seed, nullptr);
        gensTree.appendChild (t, nullptr);
    }
    root.appendChild (gensTree, nullptr);

    ValueTree ui ("UI");
    ui.setProperty ("EditorWidth", editorWidth.load(), nullptr);
    ui.setProperty ("ActiveTab", activeTab, nullptr);
    ui.setProperty ("ExportChords", exportChords, nullptr);
    ui.setProperty ("ExportBars", exportBarsIndex, nullptr);
    ui.setProperty ("Custom_Mask", (int) customMask.load(), nullptr);
    root.appendChild (ui, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void MelodyMagicProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;
    const auto root = ValueTree::fromXml (*xml);
    if (! root.hasType ("MELODYMAGIC"))
        return;

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    try
    {
        const auto ph = root.getChildWithName ("PHRASE");
        const auto seq = ph.getChildWithName ("SECUENCIA");
        if (seq.isValid())
        {
            phrase = mm::phraseFromTrees ({ fromJuce (seq) });
            phraseSource = ph.getProperty ("Source").toString();
        }

        groove.reset();
        const auto g = root.getChildWithName ("GROOVE");
        if (g.isValid())
        {
            groove = mm::grooveFromTrees ({ fromJuce (g.getChildWithName ("QUANTIZER")) });
            grooveName = g.getProperty ("Name").toString();
        }

        keyMap.reset();
        const auto k = root.getChildWithName ("KEY_MAP");
        if (k.isValid())
        {
            keyMap = mm::keyMapFromTrees ({ fromJuce (k.getChildWithName ("HARMONIZER")) });
            keyMapName = k.getProperty ("Name").toString();
            keyMap->name = keyMapName.toStdString();
        }

        snapshot.reset();
        const auto s = root.getChildWithName ("SNAPSHOT").getChildWithName ("SECUENCIA");
        if (s.isValid())
            snapshot = mm::phraseFromTrees ({ fromJuce (s) });
    }
    catch (const std::exception&)
    {
        // keep whatever loaded before the bad part
    }

    chordClip.reset();
    const auto c = root.getChildWithName ("CHORD_CLIP");
    if (c.isValid())
    {
        MemoryOutputStream out;
        if (Base64::convertFromBase64 (out, c.getProperty ("Midi").toString()))
        {
            const auto* bytes = static_cast<const std::uint8_t*> (out.getData());
            mm::MidiClip clip;
            if (mm::readMidi (std::vector<std::uint8_t> (bytes, bytes + out.getDataSize()), clip))
            {
                chordClip = clip.rescaled (mm::kPhrasePpq);
                chordClipName = c.getProperty ("Name").toString();
            }
        }
    }

    for (const auto& t : root.getChildWithName ("GENERATORS"))
    {
        const int kind = t.getProperty ("Kind", -1);
        if (kind < 0 || kind >= mm::gen::kNumKinds)
            continue;
        auto& gs = gens[(size_t) kind];
        auto values = splitInts (t.getProperty ("Values").toString());
        auto locks = splitInts (t.getProperty ("Locks").toString());
        const auto& ps = mm::gen::params ((mm::gen::Kind) kind);
        for (size_t i = 0; i < ps.size() && i < values.size(); ++i)
            gs.values[i] = jlimit (ps[i].min, ps[i].max, values[i]);
        for (size_t i = 0; i < ps.size() && i < locks.size(); ++i)
            gs.locks[i] = locks[i] != 0;
        gs.seed = (int64) t.getProperty ("Seed", (int64) gs.seed);
    }

    const auto ui = root.getChildWithName ("UI");
    if (ui.isValid())
    {
        editorWidth.store ((int) ui.getProperty ("EditorWidth", 0));
        activeTab = (int) ui.getProperty ("ActiveTab", 0);
        exportChords = jlimit (0, mmp::getExportChordNames().size() - 1, (int) ui.getProperty ("ExportChords", 0));
        exportBarsIndex = jlimit (0, 5, (int) ui.getProperty ("ExportBars", 3));
        customMask.store ((std::uint16_t) ((int) ui.getProperty ("Custom_Mask", 0x0fff) & 0x0fff));
    }

    undoStack.clear();
    redoStack.clear();
    lastGenerated = -1;
    rebuildPlayPhrase();
    publishKeyMap();
    sendChangeMessage();
}

//==============================================================================
AudioProcessorEditor* MelodyMagicProcessor::createEditor()
{
    return new MelodyMagicEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodyMagicProcessor();
}
