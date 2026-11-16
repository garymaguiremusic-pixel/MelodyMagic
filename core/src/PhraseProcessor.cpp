#include "mm/PhraseProcessor.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace mm
{
    //==============================================================================
    void OutputTracker::record (const Event& e) noexcept
    {
        if (e.note < 0 || e.note > 127 || e.channel < 1 || e.channel > 16)
            return;
        auto& c = counts[(size_t) e.channel - 1][(size_t) e.note];
        if (e.type == Event::On && c < 255)  ++c;
        if (e.type == Event::Off && c > 0)   --c;
    }

    void OutputTracker::flush (int time, std::vector<Event>& out)
    {
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
            {
                auto& c = counts[(size_t) ch][(size_t) n];
                for (; c > 0; --c)
                    out.push_back (Event::off (time, n, ch + 1));
            }
    }

    bool OutputTracker::anySounding() const noexcept
    {
        for (auto& ch : counts)
            for (auto c : ch)
                if (c > 0)
                    return true;
        return false;
    }

    //==============================================================================
    PhraseProcessor::PhraseProcessor (Rng* rng) : engine (rng) {}

    void PhraseProcessor::panic (int time, std::vector<Event>& out)
    {
        std::vector<Event> discard;
        clip.stopAll (time, discard);
        engine.silence (time, discard);
        tracker.flush (time, out);
    }

    void PhraseProcessor::process (const TransportInfo& t, int numSamples, const CoreSettings& s,
                                   const PlayPhrase* phrase, const KeyMap* keyMap,
                                   const std::vector<Event>& hostIn, std::vector<Event>& out)
    {
        const size_t firstOut = out.size();

        // Bypass: pass host notes straight through; switch off whatever the arp left sounding.
        if (s.bypassed)
        {
            if (! wasBypassed)
            {
                // The tracker knows what was really sent (after the output channel filter),
                // so it sends the note-offs; the engine and clip only forget their notes.
                std::vector<Event> discard;
                clip.stopAll (0, discard);
                engine.reset (0, discard);
                tracker.flush (0, out);
                expansions.clear();
                heldKeys = 0;
                wasBypassed = true;
            }
            for (auto& e : hostIn)
            {
                out.push_back (e);
                tracker.record (e);
            }
            phrasePosition = -1.0;
            wasPlaying = t.playing;
            return;
        }
        if (wasBypassed)
        {
            tracker.flush (0, out);   // end the notes that were passed through
            wasBypassed = false;
            clip.resetPosition();
        }

        // Output channel switched: end the notes sent on the old setting.
        if (s.outputChannel != lastOutputChannel)
        {
            std::vector<Event> discard;
            engine.silence (0, discard);
            tracker.flush (0, out);
            lastOutputChannel = s.outputChannel;
        }

        // 1. input channel filter + key map
        std::vector<Event> host;
        std::vector<int> restartTimes;   // internal sync: first key after all keys were released
        for (auto& e : hostIn)
        {
            if (s.inputChannel < 0 || (s.inputChannel > 0 && e.channel != s.inputChannel))
                continue;

            if (e.type == Event::On)
            {
                if (heldKeys == 0)
                    restartTimes.push_back (e.time);
                ++heldKeys;
            }
            else if (e.type == Event::Off)
            {
                heldKeys = std::max (0, heldKeys - 1);
            }

            const int id = e.channel * 128 + e.note;
            if (e.type == Event::On && s.keyMapEnabled && keyMap != nullptr && ! keyMap->isEmpty())
            {
                auto notes = keyMap->expand (e.note);
                for (int n : notes)
                {
                    Event x = e;
                    x.note = n;
                    host.push_back (x);
                }
                expansions[id] = std::move (notes);
            }
            else if (e.type == Event::Off && expansions.count (id) != 0)
            {
                for (int n : expansions[id])
                {
                    Event x = e;
                    x.note = n;
                    host.push_back (x);
                }
                expansions.erase (id);
            }
            else
            {
                host.push_back (e);
            }
        }

        // 2. phrase events from the clip player
        std::vector<Event> phraseEvents;
        if (phrase != lastPhrase)
        {
            clip.stopAll (0, phraseEvents);
            clip.resetPosition();
            lastPhrase = phrase;
        }

        const double bpm = t.bpm > 1.0 ? t.bpm : 120.0;
        const double tps = bpm / 60.0 * kPhrasePpq * std::max (0.01, s.speed) / std::max (1.0, t.sampleRate);

        if (s.forceInternalSync)
        {
            if (! wasInternal)
            {
                clip.stopAll (0, phraseEvents);
                clip.resetPosition();
                internalTick = 0.0;
            }

            int pos = 0;
            for (int r : restartTimes)
            {
                r = std::clamp (r, 0, numSamples);
                if (phrase != nullptr && r > pos)
                    clip.process (*phrase, internalTick, tps, r - pos, pos, phraseEvents);
                internalTick += (r - pos) * tps;
                clip.stopAll (r, phraseEvents);
                clip.resetPosition();
                internalTick = 0.0;
                pos = r;
            }
            if (phrase != nullptr && numSamples > pos)
                clip.process (*phrase, internalTick, tps, numSamples - pos, pos, phraseEvents);
            internalTick += (numSamples - pos) * tps;
            phrasePosition = phrase != nullptr && phrase->length > 0 ? std::fmod (internalTick, phrase->length) : -1.0;
        }
        else
        {
            if (wasInternal)
            {
                clip.stopAll (0, phraseEvents);
                clip.resetPosition();
            }

            if (t.playing && t.hasPosition && phrase != nullptr)
            {
                const double startTick = t.ppqPosition * kPhrasePpq * s.speed;
                clip.process (*phrase, startTick, tps, numSamples, 0, phraseEvents);
                phrasePosition = phrase->length > 0 ? std::fmod (std::fmod (startTick, phrase->length) + phrase->length, phrase->length) : -1.0;
            }
            else
            {
                if (wasPlaying || clip.isSounding())
                    clip.stopAll (0, phraseEvents);
                clip.resetPosition();
                phrasePosition = -1.0;
            }
        }
        wasInternal = s.forceInternalSync;
        wasPlaying = t.playing;

        // 3. arpeggiator
        const bool atZero = t.hasPosition && std::abs (t.ppqPosition) < 1.0e-9;
        std::vector<Event> arpOut;
        engine.processBlock (std::move (host), std::move (phraseEvents), t.playing, atZero, s.engine, arpOut);

        // 4. output channel filter
        for (auto& e : arpOut)
        {
            if (s.outputChannel < 0)
                continue;
            if (s.outputChannel > 0)
                e.channel = s.outputChannel;
            out.push_back (e);
        }

        std::stable_sort (out.begin() + (std::ptrdiff_t) firstOut, out.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });
        for (size_t i = firstOut; i < out.size(); ++i)
            tracker.record (out[i]);
    }

    //==============================================================================
    std::vector<TimedNote> renderOffline (const PlayPhrase& phrase, const KeyMap* keyMap, const std::vector<TimedNote>& input,
                                          const RenderOptions& o, Rng* rng)
    {
        const double samplesPerTick = o.sampleRate * 60.0 / (o.bpm * kPhrasePpq);

        double length = o.lengthTicks;
        if (length <= 0.0)
        {
            double inEnd = 0.0;
            for (auto& n : input)
                inEnd = std::max (inEnd, n.end);
            length = std::max (inEnd, phrase.length);
            length = std::ceil (length / kTicksPerBar - 1.0e-9) * kTicksPerBar;
        }
        const auto totalSamples = (long long) std::llround (length * samplesPerTick);

        // host events at absolute sample times: offs before ons at the same time
        struct Abs { long long time; int order; Event e; };
        std::vector<Abs> hostAbs;
        for (auto& n : input)
        {
            const auto on = (long long) std::llround (n.start * samplesPerTick);
            const auto off = std::min (totalSamples, (long long) std::llround (n.end * samplesPerTick));
            if (on >= totalSamples || off <= on)
                continue;
            hostAbs.push_back ({ on, 1, Event::on (0, n.note, n.vel, n.channel) });
            if (off < totalSamples)
                hostAbs.push_back ({ off, 0, Event::off (0, n.note, n.channel) });
        }
        std::stable_sort (hostAbs.begin(), hostAbs.end(), [] (const Abs& a, const Abs& b)
        {
            return a.time != b.time ? a.time < b.time : a.order < b.order;
        });

        PhraseProcessor proc (rng);
        std::vector<std::pair<long long, Event>> produced;
        std::vector<Event> hostBlock, outBlock;
        size_t next = 0;

        for (long long blockStart = 0; blockStart < totalSamples; blockStart += o.blockSize)
        {
            const int n = (int) std::min<long long> (o.blockSize, totalSamples - blockStart);
            hostBlock.clear();
            while (next < hostAbs.size() && hostAbs[next].time < blockStart + n)
            {
                Event e = hostAbs[next].e;
                e.time = (int) (hostAbs[next].time - blockStart);
                hostBlock.push_back (e);
                ++next;
            }

            TransportInfo t;
            t.playing = true;
            t.hasPosition = true;
            t.bpm = o.bpm;
            t.sampleRate = o.sampleRate;
            t.ppqPosition = (double) blockStart / (samplesPerTick * kPhrasePpq);

            outBlock.clear();
            proc.process (t, n, o.settings, &phrase, keyMap, hostBlock, outBlock);
            for (auto& e : outBlock)
                produced.emplace_back (blockStart + e.time, e);
        }

        std::vector<Event> tail;
        proc.panic (0, tail);
        for (auto& e : tail)
            produced.emplace_back (totalSamples, e);

        // pair ons and offs per channel/note (first in, first out)
        std::vector<TimedNote> result;
        std::map<int, std::deque<size_t>> open;
        for (auto& [time, e] : produced)
        {
            const int id = e.channel * 128 + e.note;
            const double tick = (double) time / samplesPerTick;
            if (e.type == Event::On)
            {
                TimedNote tn;
                tn.note = e.note;
                tn.vel = e.vel;
                tn.channel = e.channel;
                tn.start = tick;
                tn.end = -1.0;
                open[id].push_back (result.size());
                result.push_back (tn);
            }
            else if (e.type == Event::Off)
            {
                auto& q = open[id];
                if (! q.empty())
                {
                    result[q.front()].end = tick;
                    q.pop_front();
                }
            }
        }
        for (auto& n : result)
            if (n.end < 0.0)
                n.end = length;

        result.erase (std::remove_if (result.begin(), result.end(), [] (const TimedNote& n) { return n.end <= n.start; }), result.end());
        std::stable_sort (result.begin(), result.end(), [] (const TimedNote& a, const TimedNote& b)
        {
            return a.start != b.start ? a.start < b.start : a.note < b.note;
        });
        return result;
    }
}
