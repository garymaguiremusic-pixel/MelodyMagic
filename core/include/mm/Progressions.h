#pragma once

#include "Scales.h"
#include "Types.h"

#include <string>
#include <vector>

namespace mm
{
    /**
        Chords used to render a phrase without a host (Drag MIDI): diatonic chords built by
        stacking every other note of the Scale Lock scale, one chord per bar.
    */
    const std::vector<std::string>& progressionNames();

    /** Scale degrees (0-based) of a progression. */
    std::vector<int> progressionDegrees (int index);

    /** Chord on a scale degree: 'size' notes, stacked in thirds of the scale above the key root
        (rootNote = the key's root in the wanted octave, e.g. 48 + key). */
    std::vector<int> diatonicChord (const ScaleContext&, int rootNote, int degree, int size);

    /** One chord per bar, repeated for 'bars' bars; ticks at 96 PPQ. */
    std::vector<TimedNote> buildProgression (int index, const ScaleContext&, int rootNote, int bars, int chordSize, int velocity = 100);

    /** A held chord (MIDI notes) repeated for 'bars' bars as one long note per chord tone. */
    std::vector<TimedNote> holdChord (const std::vector<int>& notes, int bars, int velocity = 100);
}
