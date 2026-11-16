// phrasearp-render: renders a phrase over an input MIDI file offline, with exactly the
// processing the plugin uses, so the result can be diffed against MIDI recorded from the
// original plugin (see README "A/B testing").
#include "mm/Formats.h"
#include "mm/Library.h"
#include "mm/MidiFile.h"
#include "mm/PhraseProcessor.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace mm;

namespace
{
    void usage()
    {
        std::puts (
            "usage: phrasearp-render <phrase.fas | library key> <input.mid> [options]\n"
            "\n"
            "  -o, --out FILE          output MIDI file (default: out.mid)\n"
            "  --static                Static engine (default: Free)\n"
            "  --scale NAME            scale for Note_Offset / Scale Lock (default: Chromatic)\n"
            "  --key NAME              key: C, C#, D ... B (default: C)\n"
            "  --scale-lock            quantise every output note to the scale\n"
            "  --ignore-host-vel       use the phrase velocity only\n"
            "  --random-vel N          Random_Vel 0..127\n"
            "  --seed N                RNG seed for Random_Vel (default 1)\n"
            "  --speed X               phrase speed multiplier (default 1)\n"
            "  --groove FILE           quantizer pattern (.qas or library key)\n"
            "  --qstart X --qend X     quantizer strengths 0..1\n"
            "  --qlink                 the end uses the start strength\n"
            "  --keymap FILE           key map (.has or library key), enables the key-map stage\n"
            "  --in-ch N / --out-ch N  channel filters: all, off or 1..16\n"
            "  --bpm X                 tempo (default: the input file's tempo)\n"
            "  --bars N                render length in bars (default: input or phrase length)\n"
            "  --library DIR           folder with nora_*_library.json / presets, for library keys\n");
    }

    std::string lower (std::string s)
    {
        std::string out;
        for (char c : s)
            if (c != ' ' && c != '_' && c != '-' && c != '(' && c != ')')
                out += (char) std::tolower ((unsigned char) c);
        return out;
    }

    int parseScale (const std::string& name)
    {
        for (int i = 0; i < kNumScales; ++i)
            if (lower (scaleName (i)) == lower (name))
                return i;
        const auto n = lower (name);
        if (n == "minor" || n == "aeolian") return NaturalMinor;
        if (n == "ionian" || n == "diatonic") return Major;
        if (n == "spanishgypsy") return PhrygianDominant;
        std::fprintf (stderr, "unknown scale '%s'\n", name.c_str());
        std::exit (2);
    }

    int parseKey (const std::string& name)
    {
        for (int k = 0; k < 12; ++k)
            if (lower (keyName (k)) == lower (name))
                return k;
        static const char* flats[] = { "c", "db", "d", "eb", "e", "f", "gb", "g", "ab", "a", "bb", "b" };
        for (int k = 0; k < 12; ++k)
            if (lower (name) == flats[k])
                return k;
        std::fprintf (stderr, "unknown key '%s'\n", name.c_str());
        std::exit (2);
    }

    int parseChannel (const std::string& v)
    {
        if (lower (v) == "all") return 0;
        if (lower (v) == "off") return -1;
        return std::clamp (std::atoi (v.c_str()), 1, 16);
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 1;
    }

    std::string phrasePath = argv[1], inputPath = argv[2], outPath = "out.mid";
    std::string groovePath, keyMapPath, libraryDir;
    int scale = Chromatic, key = 0, bars = 0;
    double bpm = 0.0, qStart = 0.0, qEnd = 0.0;
    bool qLink = false;
    std::int64_t seed = 1;
    RenderOptions o;

    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string
        {
            if (i + 1 >= argc) { std::fprintf (stderr, "missing value for %s\n", a.c_str()); std::exit (2); }
            return argv[++i];
        };

        if (a == "-o" || a == "--out")           outPath = next();
        else if (a == "--static")                o.settings.engine.staticMode = true;
        else if (a == "--scale")                 scale = parseScale (next());
        else if (a == "--key")                   key = parseKey (next());
        else if (a == "--scale-lock")            o.settings.engine.scaleLock = true;
        else if (a == "--ignore-host-vel")       o.settings.engine.ignoreHostVelocities = true;
        else if (a == "--random-vel")            o.settings.engine.randomVel = std::clamp (std::atoi (next().c_str()), 0, 127);
        else if (a == "--seed")                  seed = std::atoll (next().c_str());
        else if (a == "--speed")                 o.settings.speed = std::atof (next().c_str());
        else if (a == "--groove")                groovePath = next();
        else if (a == "--qstart")                qStart = std::atof (next().c_str());
        else if (a == "--qend")                  qEnd = std::atof (next().c_str());
        else if (a == "--qlink")                 qLink = true;
        else if (a == "--keymap")                keyMapPath = next();
        else if (a == "--in-ch")                 o.settings.inputChannel = parseChannel (next());
        else if (a == "--out-ch")                o.settings.outputChannel = parseChannel (next());
        else if (a == "--bpm")                   bpm = std::atof (next().c_str());
        else if (a == "--bars")                  bars = std::atoi (next().c_str());
        else if (a == "--library")               libraryDir = next();
        else if (a == "-h" || a == "--help")     { usage(); return 0; }
        else { std::fprintf (stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }

    o.settings.engine.scale = ScaleContext::make (scale, key);

    Library lib;
    if (! libraryDir.empty())
        lib.scanFolder (libraryDir);

    std::string err;
    Phrase phrase;
    if (lib.phrases.count (phrasePath) != 0)
        phrase = lib.phrases[phrasePath];
    else if (! loadPhraseFile (phrasePath, phrase, &err))
    {
        std::fprintf (stderr, "cannot load phrase %s: %s\n", phrasePath.c_str(), err.c_str());
        return 1;
    }

    Groove groove;
    const Groove* groovePtr = nullptr;
    if (! groovePath.empty())
    {
        if (lib.grooves.count (groovePath) != 0)
            groove = lib.grooves[groovePath];
        else if (! loadGrooveFile (groovePath, groove, &err))
        {
            std::fprintf (stderr, "cannot load groove %s: %s\n", groovePath.c_str(), err.c_str());
            return 1;
        }
        groovePtr = &groove;
    }

    KeyMap keyMap;
    const KeyMap* keyMapPtr = nullptr;
    if (! keyMapPath.empty())
    {
        if (lib.keyMaps.count (keyMapPath) != 0)
            keyMap = lib.keyMaps[keyMapPath];
        else if (! loadKeyMapFile (keyMapPath, keyMap, &err))
        {
            std::fprintf (stderr, "cannot load key map %s: %s\n", keyMapPath.c_str(), err.c_str());
            return 1;
        }
        keyMapPtr = &keyMap;
        o.settings.keyMapEnabled = true;
    }

    MidiClip input;
    if (! readMidiFile (inputPath, input, &err))
    {
        std::fprintf (stderr, "cannot read %s: %s\n", inputPath.c_str(), err.c_str());
        return 1;
    }
    const auto in96 = input.rescaled (kPhrasePpq);

    o.bpm = bpm > 0.0 ? bpm : input.bpm;
    o.lengthTicks = bars > 0 ? bars * (double) kTicksPerBar : 0.0;

    JuceLcg rng (seed);
    const auto play = makePlayPhrase (phrase, groovePtr, qStart, qEnd, qLink);
    MidiClip out;
    out.ppq = kPhrasePpq;
    out.bpm = o.bpm;
    out.notes = renderOffline (play, keyMapPtr, in96.notes, o, &rng);

    if (! writeMidiFile (outPath, out, phrase.name))
    {
        std::fprintf (stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::printf ("%s: %zu notes (%s, %s %s%s)\n", outPath.c_str(), out.notes.size(),
                 o.settings.engine.staticMode ? "Static" : "Free", keyName (key), scaleName (scale),
                 o.settings.engine.scaleLock ? ", Scale Lock" : "");
    return 0;
}
