#include "mm/Pattern.h"

#include <algorithm>
#include <cmath>

namespace mm
{
    void Pattern::sort()
    {
        std::stable_sort (notes.begin(), notes.end(), [] (const PatternNote& a, const PatternNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.offset < b.offset;
        });
    }

    const char* arpOrderName (ArpOrder o)
    {
        switch (o)
        {
            case ArpOrder::Up:     return "Up";
            case ArpOrder::Down:   return "Down";
            case ArpOrder::UpDown: return "Up & Down";
            case ArpOrder::DownUp:
            default:               return "Down & Up";
        }
    }

    std::vector<int> arpSequence (int n, ArpOrder order)
    {
        std::vector<int> seq;
        if (n <= 0)
            return seq;

        std::vector<int> up, down;
        for (int i = 0; i < n; ++i)    up.push_back (i);
        for (int i = n - 1; i >= 0; --i) down.push_back (i);

        switch (order)
        {
            case ArpOrder::Up:   return up;
            case ArpOrder::Down: return down;
            case ArpOrder::UpDown:
                seq = up;
                for (int i = n - 2; i >= 1; --i) seq.push_back (i);
                return seq;
            case ArpOrder::DownUp:
            default:
                seq = down;
                for (int i = 1; i <= n - 2; ++i) seq.push_back (i);
                return seq;
        }
    }

    //==============================================================================
    int stepOffset (int offset, int direction, const ScaleContext* scale, int key)
    {
        const int dir = direction >= 0 ? 1 : -1;
        int o = offset + dir;
        if (scale != nullptr)
            while (std::abs (o) <= kMaxOffset && ! scale->inScale (key + o))
                o += dir;
        return std::clamp (o, -kMaxOffset, kMaxOffset);
    }

    int legalBars (int bars)
    {
        if (bars <= 1) return 1;
        if (bars <= 2) return 2;
        if (bars <= 4) return 4;
        return 8;
    }

    //==============================================================================
    void shuffleNotes (Pattern& p, const std::vector<int>& pool, Rng& rng)
    {
        if (pool.empty() || p.notes.empty())
            return;
        p.sort();

        std::vector<int> bag;
        while (bag.size() < p.notes.size())
            bag.insert (bag.end(), pool.begin(), pool.end());
        bag.resize (p.notes.size());

        std::vector<int> before;
        for (auto& n : p.notes)
            before.push_back (n.offset);

        // Fisher-Yates; try a few times to get a different order than before
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            for (int i = (int) bag.size() - 1; i > 0; --i)
                std::swap (bag[(size_t) i], bag[(size_t) rng.nextInt (i + 1)]);
            if (bag != before)
                break;
        }
        for (size_t i = 0; i < p.notes.size(); ++i)
            p.notes[i].offset = bag[i];
    }

    //==============================================================================
    Pattern patternFromMidi (const MidiClip& clip, int key, const std::string& name, std::vector<int>* fileNotes)
    {
        const auto c = clip.rescaled (kPhrasePpq);
        Pattern p;
        p.name = name;
        if (c.notes.empty())
            return p;

        // start at the bar of the first note
        double first = c.notes.front().start;
        int lowest = 127;
        for (auto& n : c.notes)
        {
            first = std::min (first, n.start);
            lowest = std::min (lowest, n.note);
        }
        const double origin = std::floor (first / kTicksPerBar) * kTicksPerBar;
        const int keyRoot = std::clamp (key, 0, 11);
        const int reference = lowest - floorMod (lowest - keyRoot, 12);   // key root at or below the lowest note

        double end = 0.0;
        for (auto& n : c.notes)
        {
            const double s = n.start - origin, e = n.end - origin;
            if (s >= kMaxBars * kTicksPerBar)
                continue;
            PatternNote pn;
            pn.start = (int) std::lround (s);
            pn.end = std::max (pn.start + 1, (int) std::lround (std::min (e, (double) kMaxBars * kTicksPerBar)));
            pn.offset = std::clamp (n.note - reference, -kMaxOffset, kMaxOffset);
            pn.velocity = std::clamp (n.vel, 1, 127);
            p.notes.push_back (pn);
            end = std::max (end, (double) pn.end);
        }
        p.bars = legalBars ((int) std::ceil (end / kTicksPerBar - 1.0e-9));
        for (auto& n : p.notes)
            n.end = std::min (n.end, p.lengthTicks());
        p.notes.erase (std::remove_if (p.notes.begin(), p.notes.end(), [&p] (const PatternNote& n)
        {
            return n.start >= p.lengthTicks() || n.end <= n.start;
        }), p.notes.end());
        p.sort();

        if (fileNotes != nullptr)
        {
            fileNotes->clear();
            for (auto& n : p.notes)
                fileNotes->push_back (n.offset);
        }
        return p;
    }

    Pattern patternFromPhrase (const Phrase& ph)
    {
        Pattern p;
        p.name = ph.name;
        const int length = ph.lengthTicks();
        p.bars = legalBars ((length + kTicksPerBar - 1) / kTicksPerBar);
        for (auto& n : ph.notes)
        {
            if (n.start >= p.lengthTicks())
                continue;
            PatternNote pn;
            pn.start = n.start;
            pn.end = std::min (std::max (n.end, n.start + 1), p.lengthTicks());
            pn.offset = std::clamp ((n.lane() - kCentreLane) * 12, -kMaxOffset, kMaxOffset);
            pn.velocity = std::clamp (n.velocity, 1, 127);
            p.notes.push_back (pn);
        }
        p.sort();
        return p;
    }
}
