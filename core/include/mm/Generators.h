#pragma once

#include "Rng.h"
#include "Scales.h"
#include "Types.h"

#include <string>
#include <vector>

namespace mm::gen
{
    /**
        Phrase generators (ANALYSIS.md 4.3 - behavioural, not bit-exact). Every generator
        has a flat list of integer parameters (so every knob moves in whole steps), per-knob
        locks, Regen (new random draw), Randomize (unlocked knobs only) and Reset.
    */
    enum class Kind { Random303 = 0, ChordBass = 1, Mutate = 2 };
    constexpr int kNumKinds = 3;

    struct Param
    {
        const char* id;
        const char* name;
        int min, max, def;
        const char* unit;      // "%", "steps", "ticks", "st", "" ...
        bool lockedByDefault;  // e.g. Bars, so Randomize keeps the length
        bool isToggle;         // 0 / 1 shown as a button
    };

    const std::vector<Param>& params (Kind);
    const char* kindName (Kind);
    int indexOf (Kind, const std::string& id);
    std::vector<int> defaults (Kind);
    std::vector<bool> defaultLocks (Kind);

    /** Randomize: new values for every unlocked parameter. */
    void randomize (Kind, std::vector<int>& values, const std::vector<bool>& locks, Rng&);

    /** Layout of the phrase being written (lanes shown and rows per lane). */
    struct Layout
    {
        int lanes = 3;
        int rows = 3;
    };

    /** 303 Random: a new monophonic line. */
    Phrase generate303 (const std::vector<int>& v, Layout, Rng&);

    /** 303 mutator: show/hide, pitch, octave, swap, accent and slide flips applied to 'source'. */
    Phrase mutate303 (const Phrase& source, const std::vector<int>& v, Rng&);

    /** Chord / Bass: chord hits and a bass line, with macro (motif) repetition. */
    Phrase generateChordBass (const std::vector<int>& v, Layout, Rng&);

    /** Mutate: random changes to note number, start, length, velocity and pitch (Note_Offset). */
    Phrase mutate (const Phrase& source, const std::vector<int>& v, const ScaleContext&, Rng&);

    /** Joins overlapping notes that share a Number. */
    void joinOverlaps (Phrase&);
}
