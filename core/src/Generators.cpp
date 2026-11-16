#include "mm/Generators.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace mm::gen
{
    namespace
    {
        constexpr int kStep = kPhrasePpq / 4;   // 1/16

        const std::vector<Param> p303 =
        {
            { "bars",       "Bars",         1,   8,   1, "bars", true,  false },
            { "density",    "Density",      0, 100,  60, "%",    false, false },
            { "height",     "Height",       1,  10,   3, "rows", false, false },
            { "expression", "Express",      0, 100,  40, "%",    false, false },
            { "octave",     "Oct Mut",      0, 100,  15, "%",    false, false },
            { "slide",      "Slide",        0, 100,  15, "%",    false, false },
            { "pitch",      "Pitch Chg",    0, 100,  50, "%",    false, false },
            { "shuffle",    "Shuffle",      0, 100,   0, "%",    false, false },
            { "gate",       "Gate",         5, 100,  50, "%",    false, false },
            { "accent",     "Accent",       0, 100,  25, "%",    false, false },
            { "accentVel",  "Acc Vel",      1, 127, 120, "",     false, false },
            { "regularVel", "Reg Vel",      1, 127,  90, "",     false, false },
            { "rotate",     "Rotate",     -16,  16,   0, "steps", false, false },
            // mutator
            { "mShow",      "Show/Hide",    0, 100,  10, "%",    false, false },
            { "mPitch",     "Pitch Oct",    0, 100,  10, "%",    false, false },
            { "mOctave",    "Octave",       0, 100,   5, "%",    false, false },
            { "mSwap",      "Swap",         0, 100,   5, "%",    false, false },
            { "mAccent",    "Acc Flip",     0, 100,  10, "%",    false, false },
            { "mSlide",     "Slide Flip",   0, 100,   5, "%",    false, false },
        };

        const std::vector<Param> pChordBass =
        {
            { "bars",        "Bars",          1,   8,   2, "bars",  true,  false },
            { "density",     "Density",       0, 100,  50, "%",     false, false },
            { "movement",    "Movement",      0, 100,  30, "%",     false, false },
            { "length",      "Length",        1,  16,   2, "steps", false, false },
            { "velVar",      "Vel Var",       0, 100,  20, "%",     false, false },
            { "chordAlter",  "Ch Alter",      0, 100,  10, "%",     false, false },
            { "balance",     "Balance",       0, 100,  50, "%",     false, false },
            { "juice",       "Juice",         0, 100,  30, "%",     false, false },
            { "lenVar",      "Len Var",       0, 100,  20, "%",     false, false },
            { "jitter",      "Jitter",        0, 100,   0, "%",     false, false },
            { "bassAlter",   "Bs Alter",      0, 100,  10, "%",     false, false },
            { "bassAtten",   "Bs Atten",      0, 100,  20, "%",     false, false },
            { "macro",       "Macro",         0,   3,   1, "",      false, false },   // 1, 2, 4, 8 bars
            { "chordSize",   "Size",          1,   5,   3, "notes", false, false },
            { "reserveBass", "Bass Row",      0,   1,   1, "",      true,  true  },
        };

        const std::vector<Param> pMutate =
        {
            { "numProb",   "Num %",      0, 100, 20, "%",     false, false },
            { "numAmt",    "Num Amt",    1,   5,  1, "rows",  false, false },
            { "startProb", "Start %",    0, 100, 10, "%",     false, false },
            { "startAmt",  "Start Amt",  1,  48,  6, "ticks", false, false },
            { "lenProb",   "Len %",      0, 100, 15, "%",     false, false },
            { "lenAmt",    "Len Amt",    1,  96, 12, "ticks", false, false },
            { "velProb",   "Vel %",      0, 100, 25, "%",     false, false },
            { "velAmt",    "Vel Amt",    1, 127, 20, "",      false, false },
            { "pitchProb", "Pitch %",    0, 100, 10, "%",     false, false },
            { "pitchAmt",  "Pitch Amt",  1,  12,  2, "st",    false, false },
            { "together",  "Grouped",    0,   1,  1, "",      true,  true  },
            { "join",      "Join",       0,   1,  1, "",      true,  true  },
        };

        struct V
        {
            const std::vector<int>& v;
            Kind kind;
            int operator() (const char* id) const
            {
                const int i = indexOf (kind, id);
                const auto& ps = params (kind);
                if (i < 0) return 0;
                const auto& p = ps[(size_t) i];
                return i < (int) v.size() ? std::clamp (v[(size_t) i], p.min, p.max) : p.def;
            }
            double pct (const char* id) const   { return (*this) (id) / 100.0; }
        };

        int laneMin (int lanes) { return lanes >= 5 ? 3 : (lanes >= 3 ? 4 : 5); }
        int laneMax (int lanes) { return lanes >= 5 ? 7 : (lanes >= 3 ? 6 : 5); }
        int clampLane (int lane, int lanes) { return std::clamp (lane, laneMin (lanes), laneMax (lanes)); }

        Phrase blank (const char* name, Layout l, int bars)
        {
            Phrase p;
            p.name = name;
            p.snap = "1/16";
            p.setLayout (l.lanes, l.rows);
            p.autoSize = false;
            p.totalSize = std::max (1, bars) * kTicksPerBar;
            return p;
        }

        // 303 accent / slide flags travel in the velocity and in a side table while mutating
        struct Step303 { bool on = false; int row = 0, lane = 5; bool accent = false, slide = false; };

        Phrase render303 (const std::vector<Step303>& steps, Layout l, int bars, const V& v, double gate, double shuffle)
        {
            Phrase p = blank ("303 Random", l, bars);
            const int n = (int) steps.size();
            for (int i = 0; i < n; ++i)
            {
                const auto& s = steps[(size_t) i];
                if (! s.on)
                    continue;

                const int swing = (i % 2 == 1) ? (int) std::lround (shuffle * kStep / 3.0) : 0;
                PhraseNote note;
                note.number = s.lane * 10 + std::clamp (s.row, 0, l.rows - 1);
                note.velocity = s.accent ? v ("accentVel") : v ("regularVel");
                note.start = i * kStep + swing;

                int nextOn = -1;
                for (int j = i + 1; j < n; ++j)
                    if (steps[(size_t) j].on) { nextOn = j; break; }

                if (s.slide && nextOn >= 0)
                {
                    const int nextSwing = (nextOn % 2 == 1) ? (int) std::lround (shuffle * kStep / 3.0) : 0;
                    note.end = nextOn * kStep + nextSwing + 6;   // overlap = legato into the next note
                }
                else
                {
                    note.end = note.start + std::max (2, (int) std::lround (kStep * gate));
                }
                note.end = std::min (note.end, p.totalSize);
                if (note.end > note.start)
                    p.notes.push_back (note);
            }
            p.sortNotes();
            return p;
        }

        std::vector<Step303> stepsFrom (const Phrase& src, int numSteps, int accentThreshold)
        {
            std::vector<Step303> steps ((size_t) numSteps);
            for (auto& n : src.notes)
            {
                const int i = std::clamp ((int) std::lround ((double) n.start / kStep), 0, numSteps - 1);
                auto& s = steps[(size_t) i];
                s.on = true;
                s.row = n.row();
                s.lane = n.lane();
                s.accent = n.velocity >= accentThreshold;
                s.slide = n.end > (i + 1) * kStep + 2;
            }
            return steps;
        }
    }

    //==============================================================================
    const std::vector<Param>& params (Kind k)
    {
        switch (k)
        {
            case Kind::Random303: return p303;
            case Kind::ChordBass: return pChordBass;
            case Kind::Mutate:
            default:              return pMutate;
        }
    }

    const char* kindName (Kind k)
    {
        switch (k)
        {
            case Kind::Random303: return "303 Random";
            case Kind::ChordBass: return "Chord / Bass";
            case Kind::Mutate:
            default:              return "Mutate";
        }
    }

    int indexOf (Kind k, const std::string& id)
    {
        const auto& ps = params (k);
        for (size_t i = 0; i < ps.size(); ++i)
            if (id == ps[i].id)
                return (int) i;
        return -1;
    }

    std::vector<int> defaults (Kind k)
    {
        std::vector<int> v;
        for (auto& p : params (k))
            v.push_back (p.def);
        return v;
    }

    std::vector<bool> defaultLocks (Kind k)
    {
        std::vector<bool> v;
        for (auto& p : params (k))
            v.push_back (p.lockedByDefault);
        return v;
    }

    void randomize (Kind k, std::vector<int>& values, const std::vector<bool>& locks, Rng& rng)
    {
        const auto& ps = params (k);
        values.resize (ps.size());
        for (size_t i = 0; i < ps.size(); ++i)
        {
            const bool locked = i < locks.size() ? locks[i] : ps[i].lockedByDefault;
            if (! locked)
                values[i] = rng.nextRange (ps[i].min, ps[i].max);
        }
    }

    //==============================================================================
    Phrase generate303 (const std::vector<int>& values, Layout l, Rng& rng)
    {
        const V v { values, Kind::Random303 };
        l.rows = std::clamp (l.rows, 1, kMaxRows);
        const int bars = v ("bars");
        const int numSteps = bars * 16;
        const int height = std::min (v ("height"), l.rows);

        std::vector<Step303> steps ((size_t) numSteps);
        int row = 0, lane = kCentreLane;
        for (auto& s : steps)
        {
            s.on = rng.chance (v.pct ("density"));
            if (! s.on)
                continue;

            if (rng.chance (v.pct ("pitch")))
            {
                // expression: how far notes move away from the middle line (row 0)
                row = (height > 1 && rng.chance (v.pct ("expression"))) ? rng.nextRange (1, height - 1) : 0;
                lane = kCentreLane;
                if (l.lanes > 1 && rng.chance (v.pct ("octave")))
                    lane = clampLane (kCentreLane + (rng.nextBool() ? 1 : -1), l.lanes);
            }
            s.row = row;
            s.lane = lane;
            s.accent = rng.chance (v.pct ("accent"));
            s.slide = rng.chance (v.pct ("slide"));
        }

        // rotate
        const int rot = v ("rotate");
        if (rot != 0 && numSteps > 0)
        {
            const int r = floorMod (rot, numSteps);
            std::rotate (steps.begin(), steps.begin() + (numSteps - r) % numSteps, steps.end());
        }

        return render303 (steps, l, bars, v, v.pct ("gate"), v.pct ("shuffle"));
    }

    Phrase mutate303 (const Phrase& src, const std::vector<int>& values, Rng& rng)
    {
        const V v { values, Kind::Random303 };
        Layout l { src.lanesShown(), src.rowsPerLane() };
        const int bars = std::max (1, src.lengthTicks() / kTicksPerBar);
        const int numSteps = bars * 16;
        const int accentThreshold = (v ("accentVel") + v ("regularVel") + 1) / 2;
        auto steps = stepsFrom (src, numSteps, accentThreshold);
        const int height = std::min (v ("height"), l.rows);

        for (auto& s : steps)
        {
            if (rng.chance (v.pct ("mShow")))
            {
                s.on = ! s.on;
                if (s.on)
                {
                    s.row = height > 1 ? rng.nextRange (0, height - 1) : 0;
                    s.lane = kCentreLane;
                }
            }
            if (! s.on)
                continue;
            if (rng.chance (v.pct ("mPitch")))   s.row = l.rows > 1 ? rng.nextRange (0, std::max (0, height - 1)) : 0;
            if (rng.chance (v.pct ("mOctave")) && l.lanes > 1)
                s.lane = clampLane (s.lane + (rng.nextBool() ? 1 : -1), l.lanes);
            if (rng.chance (v.pct ("mAccent")))  s.accent = ! s.accent;
            if (rng.chance (v.pct ("mSlide")))   s.slide = ! s.slide;
        }
        for (int i = 0; i + 1 < numSteps; ++i)
            if (steps[(size_t) i].on && rng.chance (v.pct ("mSwap")))
            {
                int j = i + 1;
                while (j < numSteps && ! steps[(size_t) j].on) ++j;
                if (j < numSteps)
                {
                    std::swap (steps[(size_t) i].row, steps[(size_t) j].row);
                    std::swap (steps[(size_t) i].lane, steps[(size_t) j].lane);
                }
            }

        auto p = render303 (steps, l, bars, v, v.pct ("gate"), v.pct ("shuffle"));
        p.name = src.name;
        return p;
    }

    //==============================================================================
    Phrase generateChordBass (const std::vector<int>& values, Layout l, Rng& rng)
    {
        const V v { values, Kind::ChordBass };
        l.rows = std::clamp (l.rows, 1, kMaxRows);
        const int bars = v ("bars");
        const int macroBars = std::min (bars, 1 << v ("macro"));
        const int motifSteps = macroBars * 16;
        const bool reserveBass = v ("reserveBass") != 0;

        const int bassLane = l.lanes > 1 ? kCentreLane - 1 : kCentreLane;
        const int firstChordRow = (reserveBass && l.rows > 1) ? 1 : 0;
        const int availableRows = std::max (1, l.rows - firstChordRow);
        const int chordSize = std::min (v ("chordSize"), availableRows);

        Phrase motif = blank ("Chord Bass", l, macroBars);
        std::vector<int> voicing;
        auto newVoicing = [&]
        {
            std::vector<int> rows;
            for (int r = firstChordRow; r < l.rows; ++r)
                rows.push_back (r);
            // keep the lowest chord row most of the time, pick the rest
            voicing.clear();
            voicing.push_back (rows.front());
            for (int k = 1; k < (int) rows.size(); ++k)
                std::swap (rows[(size_t) k], rows[(size_t) rng.nextRange (k, (int) rows.size() - 1)]);
            for (int k = 1; k < chordSize && k < (int) rows.size(); ++k)
                voicing.push_back (rows[(size_t) k]);
        };
        newVoicing();

        auto lengthFor = [&]
        {
            int len = v ("length") * kStep;
            if (rng.chance (v.pct ("lenVar")))
                len = rng.nextBool() ? len * 2 : std::max (kStep, len / 2);
            if (v ("jitter") > 0)
            {
                const int j = (int) std::lround (v.pct ("jitter") * kStep * 0.5);
                len += rng.nextRange (-j, j);
            }
            return std::max (3, len);
        };
        auto velocity = [&] (int base)
        {
            const int spread = (int) std::lround (v.pct ("velVar") * 40.0);
            return std::clamp (base + rng.nextRange (-spread, spread), 1, 127);
        };

        for (int step = 0; step < motifSteps; ++step)
        {
            const bool strong = step % 4 == 0;
            if (! rng.chance (v.pct ("density") * (strong ? 1.0 : 0.6)))
                continue;

            const int start = step * kStep;
            const int len = lengthFor();
            const bool bass = rng.chance (v.pct ("balance"));

            if (bass)
            {
                int row = 0;
                if (rng.chance (v.pct ("bassAlter")) && l.rows > 1)
                    row = rng.nextRange (1, std::min (l.rows - 1, 2));
                const int baseVel = (int) std::lround (100.0 * (1.0 - v.pct ("bassAtten") * 0.6));
                motif.notes.push_back ({ bassLane * 10 + row, 0, velocity (baseVel), start, start + len });

                // juiciness: octave pops and ghost notes after the bass hit
                if (rng.chance (v.pct ("juice")) && step + 2 < motifSteps)
                {
                    const int popLane = l.lanes > 1 ? kCentreLane : bassLane;
                    const int popStart = start + 2 * kStep;
                    motif.notes.push_back ({ popLane * 10 + row, 0, velocity (baseVel - 15), popStart, popStart + kStep / 2 });
                }
            }
            else
            {
                if (rng.chance (v.pct ("movement")))
                    newVoicing();
                for (int r : voicing)
                {
                    int lane = kCentreLane;
                    if (rng.chance (v.pct ("chordAlter")) && l.lanes > 1)
                        lane = clampLane (kCentreLane + (rng.nextBool() ? 1 : -1), l.lanes);
                    motif.notes.push_back ({ lane * 10 + r, 0, velocity (96), start, start + len });
                }
            }
        }

        // keep the motif inside its own bars, then repeat it across the phrase
        const int motifLength = macroBars * kTicksPerBar;
        for (auto& n : motif.notes)
            n.end = std::min (n.end, motifLength);
        joinOverlaps (motif);

        Phrase p = blank ("Chord Bass", l, bars);
        for (int rep = 0; rep * macroBars < bars; ++rep)
            for (auto n : motif.notes)
            {
                n.start += rep * macroBars * kTicksPerBar;
                n.end = std::min (n.end + rep * macroBars * kTicksPerBar, p.totalSize);
                if (n.start < p.totalSize && n.end > n.start)
                    p.notes.push_back (n);
            }
        p.sortNotes();
        return p;
    }

    //==============================================================================
    void joinOverlaps (Phrase& p)
    {
        p.sortNotes();
        std::vector<PhraseNote> out;
        for (auto& n : p.notes)
        {
            bool merged = false;
            for (auto& o : out)
                if (o.number == n.number && o.noteOffset == n.noteOffset && n.start < o.end && n.end > o.start)
                {
                    o.start = std::min (o.start, n.start);
                    o.end = std::max (o.end, n.end);
                    o.velocity = std::max (o.velocity, n.velocity);
                    merged = true;
                    break;
                }
            if (! merged)
                out.push_back (n);
        }
        p.notes = std::move (out);
    }

    Phrase mutate (const Phrase& src, const std::vector<int>& values, const ScaleContext& scale, Rng& rng)
    {
        const V v { values, Kind::Mutate };
        Phrase p = src;
        const int length = p.lengthTicks();
        const int rows = p.rowsPerLane();
        const int lanes = p.lanesShown();
        const bool together = v ("together") != 0;

        // notes sharing a start move together (one draw per start time)
        std::map<int, std::pair<int, int>> groupDelta;   // start -> (start delta, length delta)

        for (auto& n : p.notes)
        {
            if (rng.chance (v.pct ("numProb")))
            {
                const int amt = v ("numAmt");
                int row = n.row() + rng.nextRange (-amt, amt);
                int lane = n.lane();
                while (row < 0)     { row += rows; --lane; }
                while (row >= rows) { row -= rows; ++lane; }
                lane = clampLane (lane, lanes);
                n.number = lane * 10 + row;
            }

            int ds = 0, dl = 0;
            const bool haveGroup = together && groupDelta.count (n.start) != 0;
            if (haveGroup)
            {
                std::tie (ds, dl) = groupDelta[n.start];
            }
            else
            {
                if (rng.chance (v.pct ("startProb"))) ds = rng.nextRange (-v ("startAmt"), v ("startAmt"));
                if (rng.chance (v.pct ("lenProb")))   dl = rng.nextRange (-v ("lenAmt"), v ("lenAmt"));
                if (together)
                    groupDelta[n.start] = { ds, dl };
            }
            const int len = std::max (3, n.end - n.start + dl);
            n.start = std::clamp (n.start + ds, 0, length - 1);
            n.end = std::min (length, n.start + len);

            if (rng.chance (v.pct ("velProb")))
                n.velocity = std::clamp (n.velocity + rng.nextRange (-v ("velAmt"), v ("velAmt")), 1, 127);

            if (rng.chance (v.pct ("pitchProb")))
            {
                // scale aware: the shift is a whole number of scale steps, turned into semitones
                const int steps = rng.nextRange (1, std::max (1, (v ("pitchAmt") * scale.notesPerOctave + 11) / 12));
                const int dir = rng.nextBool() ? 1 : -1;
                const int root = 60 + scale.lowestPc;
                const int semis = scale.stepsAbove (root, dir * steps) - root;
                n.noteOffset = std::clamp (n.noteOffset + semis, -24, 24);
            }
        }

        if (v ("join") != 0)
            joinOverlaps (p);
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [] (const PhraseNote& n) { return n.end <= n.start; }), p.notes.end());
        p.sortNotes();
        return p;
    }
}
