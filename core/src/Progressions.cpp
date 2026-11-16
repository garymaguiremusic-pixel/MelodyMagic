#include "mm/Progressions.h"

#include <algorithm>

namespace mm
{
    const std::vector<std::string>& progressionNames()
    {
        static const std::vector<std::string> names
        {
            "I", "I - V - vi - IV", "vi - IV - I - V", "I - vi - IV - V", "ii - V - I - I",
            "I - IV - V - IV", "i - VII - VI - VII", "i - iv - v - i", "i - VI - III - VII"
        };
        return names;
    }

    std::vector<int> progressionDegrees (int index)
    {
        switch (index)
        {
            case 1:  return { 0, 4, 5, 3 };
            case 2:  return { 5, 3, 0, 4 };
            case 3:  return { 0, 5, 3, 4 };
            case 4:  return { 1, 4, 0, 0 };
            case 5:  return { 0, 3, 4, 3 };
            case 6:  return { 0, 6, 5, 6 };
            case 7:  return { 0, 3, 4, 0 };
            case 8:  return { 0, 5, 2, 6 };
            case 0:
            default: return { 0 };
        }
    }

    std::vector<int> diatonicChord (const ScaleContext& scale, int rootNote, int degree, int size)
    {
        const int n = std::max (1, scale.notesPerOctave);
        // degrees past the octave fold back down, so progressions stay around the root
        const int root = scale.stepsAbove (rootNote, degree % n);
        std::vector<int> notes;
        for (int k = 0; k < std::max (1, size); ++k)
        {
            const int p = scale.stepsAbove (root, 2 * k);
            if (p >= 0 && p <= 127)
                notes.push_back (p);
        }
        return notes;
    }

    std::vector<TimedNote> buildProgression (int index, const ScaleContext& scale, int rootNote, int bars, int chordSize, int velocity)
    {
        const auto degrees = progressionDegrees (index);
        std::vector<TimedNote> out;
        for (int bar = 0; bar < std::max (1, bars); ++bar)
        {
            const int degree = degrees[(size_t) bar % degrees.size()];
            for (int p : diatonicChord (scale, rootNote, degree, chordSize))
            {
                TimedNote t;
                t.note = p;
                t.vel = velocity;
                t.start = bar * (double) kTicksPerBar;
                t.end = (bar + 1) * (double) kTicksPerBar;
                out.push_back (t);
            }
        }
        return out;
    }

    std::vector<TimedNote> holdChord (const std::vector<int>& notes, int bars, int velocity)
    {
        std::vector<TimedNote> out;
        for (int p : notes)
        {
            TimedNote t;
            t.note = std::clamp (p, 0, 127);
            t.vel = velocity;
            t.start = 0.0;
            t.end = std::max (1, bars) * (double) kTicksPerBar;
            out.push_back (t);
        }
        return out;
    }
}
