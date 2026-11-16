#include "mm/ArpEngine.h"

#include <algorithm>
#include <cstdint>

namespace mm
{
    ArpEngine::ArpEngine (Rng* r) : rng (r) {}

    //==============================================================================
    int ArpEngine::roundHalfEven (double v) noexcept
    {
        // Same result as adding and subtracting 1.5 * 2^52 in double precision.
        const double magic = 6755399441055744.0;
        return (int) ((v + magic) - magic);
    }

    int ArpEngine::pitchOf (int number, int noteOffset, const std::vector<ChordNote>& chord,
                            const std::vector<int>& state, const EngineSettings& s)
    {
        const int row = number % 10;
        const int octave = number / 10 - 5;   // truncating, as in the original
        if (row < 0 || row >= (int) chord.size())
            return -1;

        int p = chord[(size_t) row].note + 12 * octave;
        if (noteOffset != 0 && ! state.empty() && row < (int) state.size())
            p = diatonicShift (chord.front().note + 12 * octave, noteOffset, state[(size_t) row], s.scale);

        if (p < 0 || p > 127)
            return -1;
        return s.scaleLock ? s.scale.quantise (p) : p;
    }

    int ArpEngine::velocityOf (int phraseVel, int hostVel, const EngineSettings& s, Rng* r)
    {
        int pv = phraseVel;
        if (s.randomVel > 0 && r != nullptr)
        {
            const auto a = (std::int8_t) (int) ((float) s.randomVel * r->nextFloat());
            const int sign = r->nextBool() ? 1 : -1;
            pv = std::clamp (pv + sign * (int) a, 0, 127);
        }
        if (s.ignoreHostVelocities)
            return pv;
        return roundHalfEven ((double) pv / 127.0 * (double) hostVel);
    }

    void ArpEngine::removeZeroLength (std::vector<Event>& ev)
    {
        for (size_t i = 0; i < ev.size(); ++i)
        {
            if (ev[i].type != Event::On)
                continue;
            for (size_t j = i + 1; j < ev.size() && ev[j].time == ev[i].time; ++j)
            {
                if (ev[j].type == Event::Off && ev[j].note == ev[i].note)
                {
                    ev[i].type = Event::Dropped;
                    ev[j].type = Event::Dropped;
                    break;
                }
            }
        }
        ev.erase (std::remove_if (ev.begin(), ev.end(), [] (const Event& e) { return e.type == Event::Dropped; }), ev.end());
    }

    //==============================================================================
    void ArpEngine::recomputeState (const EngineSettings& s)
    {
        std::vector<int> pitches;
        pitches.reserve (chord.size());
        for (auto& c : chord)
            pitches.push_back (c.note);
        state = computeScaleState (pitches, s.scale);
    }

    void ArpEngine::applyHost (const Event& e)
    {
        if (e.type == Event::On)
        {
            ChordNote c { e.note, e.vel, e.channel, e.noteOffset };
            auto pos = std::upper_bound (chord.begin(), chord.end(), c, [] (const ChordNote& a, const ChordNote& b)
            {
                return a.note != b.note ? a.note < b.note : a.noteOffset < b.noteOffset;
            });
            chord.insert (pos, c);
        }
        else if (e.type == Event::Off)
        {
            auto it = std::find_if (chord.begin(), chord.end(), [&] (const ChordNote& c) { return c.note == e.note && c.channel == e.channel; });
            if (it == chord.end())
                it = std::find_if (chord.begin(), chord.end(), [&] (const ChordNote& c) { return c.note == e.note; });
            if (it != chord.end())
                chord.erase (it);
        }
    }

    ArpEngine::Active* ArpEngine::findActive (int number, int noteOffset)
    {
        for (auto& a : active)
            if (a.number == number && a.noteOffset == noteOffset)
                return &a;
        return nullptr;
    }

    void ArpEngine::removeActive (int number, int noteOffset)
    {
        active.erase (std::remove_if (active.begin(), active.end(), [&] (const Active& a)
        {
            return a.number == number && a.noteOffset == noteOffset;
        }), active.end());
    }

    void ArpEngine::emitOn (int time, Active& a, const EngineSettings& s, std::vector<Event>& out)
    {
        a.pitch = -1;
        const int row = a.number % 10;
        if (row >= (int) chord.size())
            return;

        const int p = pitchOf (a.number, a.noteOffset, chord, state, s);
        if (p < 0)
            return;

        const auto& tone = chord[(size_t) row];
        const int vel = velocityOf (a.vel, tone.vel, s, rng);
        if (vel <= 0)
            return;   // a velocity-0 note-on is a note-off in MIDI: nothing sounds

        out.push_back (Event::on (time, p, std::min (vel, 127), tone.channel));
        a.pitch = p;
        a.channel = tone.channel;
    }

    void ArpEngine::flushSounding (int time, std::vector<Event>& out)
    {
        for (auto& a : active)
        {
            if (a.pitch >= 0)
                out.push_back (Event::off (time, a.pitch, a.channel));
            a.pitch = -1;
        }
        for (auto& slot : slots)
        {
            if (slot.set && slot.pitch >= 0)
                out.push_back (Event::off (time, slot.pitch, slot.channel));
            slot = {};
        }
    }

    void ArpEngine::silence (int time, std::vector<Event>& out)
    {
        flushSounding (time, out);
    }

    void ArpEngine::reset (int time, std::vector<Event>& out)
    {
        flushSounding (time, out);
        chord.clear();
        state.clear();
        active.clear();
        lastEngine = -1;
    }

    //==============================================================================
    void ArpEngine::processBlock (std::vector<Event> host, std::vector<Event> phrase,
                                  bool transportRunning, bool atPositionZero,
                                  const EngineSettings& s, std::vector<Event>& out)
    {
        const size_t firstOut = out.size();

        // 2. stopped at position 0: every host event happens at the start of the block
        if (! transportRunning && atPositionZero)
            for (auto& e : host)
                e.time = 0;

        // 1. merge (stable: host events first at equal times)
        std::vector<Event> events;
        events.reserve (host.size() + phrase.size());
        for (auto& e : host)   { e.fromPhrase = false; events.push_back (e); }
        for (auto& e : phrase) { e.fromPhrase = true;  events.push_back (e); }
        std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });

        // 3. engine selection
        const int engine = (! s.staticMode && transportRunning) ? 0 : 1;
        if (lastEngine >= 0 && engine != lastEngine)
            flushSounding (0, out);
        lastEngine = engine;

        recomputeState (s);   // the scale may have changed since the last block

        if (engine == 0)
            runFree (events, s, out);
        else
            runStatic (events, s, out);

        // 4. cleanup (only this block's output)
        std::vector<Event> produced (out.begin() + (std::ptrdiff_t) firstOut, out.end());
        removeZeroLength (produced);
        out.resize (firstOut);
        out.insert (out.end(), produced.begin(), produced.end());
    }

    void ArpEngine::runFree (std::vector<Event>& ev, const EngineSettings& s, std::vector<Event>& out)
    {
        size_t i = 0;
        while (i < ev.size())
        {
            const auto& e = ev[i];

            if (e.fromPhrase)
            {
                if (e.type == Event::On)
                {
                    auto* a = findActive (e.note, e.noteOffset);
                    if (a == nullptr)
                    {
                        active.push_back ({});
                        a = &active.back();
                        a->number = e.note;
                        a->noteOffset = e.noteOffset;
                    }
                    a->vel = e.vel;
                    emitOn (e.time, *a, s, out);
                }
                else if (e.type == Event::Off)
                {
                    if (auto* a = findActive (e.note, e.noteOffset))
                        if (a->pitch >= 0)
                            out.push_back (Event::off (e.time, a->pitch, a->channel));
                    removeActive (e.note, e.noteOffset);
                }
                ++i;
                continue;
            }

            // Host group: every host event of this type at this time
            const auto type = e.type;
            const int time = e.time;
            size_t j = i;
            while (j < ev.size() && ! ev[j].fromPhrase && ev[j].type == type && ev[j].time == time)
                ++j;

            for (auto& a : active)
            {
                if (a.pitch >= 0)
                    out.push_back (Event::off (time, a.pitch, a.channel));
                a.pitch = -1;
            }

            for (size_t k = i; k < j; ++k)
                applyHost (ev[k]);
            recomputeState (s);

            for (auto& a : active)
                emitOn (time, a, s, out);

            i = std::max (j, i + 1);
        }
    }

    void ArpEngine::runStatic (std::vector<Event>& ev, const EngineSettings& s, std::vector<Event>& out)
    {
        for (auto& e : ev)
        {
            if (! e.fromPhrase)
            {
                applyHost (e);
                recomputeState (s);
                continue;
            }

            const int slotIndex = e.note - 30;
            const bool slotValid = slotIndex >= 0 && slotIndex < (int) slots.size();

            if (e.type == Event::On)
            {
                auto* a = findActive (e.note, e.noteOffset);
                if (a == nullptr)
                {
                    active.push_back ({});
                    a = &active.back();
                    a->number = e.note;
                    a->noteOffset = e.noteOffset;
                }
                a->vel = e.vel;
                a->pitch = -1;

                const int row = e.note % 10;
                if (! slotValid || row >= (int) chord.size())
                    continue;

                auto& slot = slots[(size_t) slotIndex];
                if (slot.set && slot.pitch >= 0)   // overlapping repeat of the same Number
                    out.push_back (Event::off (e.time, slot.pitch, slot.channel));
                slot = {};
                slot.set = true;

                Active tmp = *a;
                emitOn (e.time, tmp, s, out);
                slot.pitch = tmp.pitch;
                slot.channel = tmp.channel;
            }
            else if (e.type == Event::Off)
            {
                if (slotValid)
                {
                    auto& slot = slots[(size_t) slotIndex];
                    if (slot.set && slot.pitch >= 0)
                        out.push_back (Event::off (e.time, slot.pitch, slot.channel));
                    slot = {};
                }
                removeActive (e.note, e.noteOffset);
            }
        }
    }
}
