// Golden cases for the arpeggiator (CLAUDE_CODE_PROMPT.md section 9, ANALYSIS.md section 3).
#include <catch2/catch_test_macros.hpp>

#include "mm/ArpEngine.h"

using namespace mm;

namespace
{
    std::vector<ChordNote> chordOf (std::initializer_list<int> notes, int vel = 100)
    {
        std::vector<ChordNote> c;
        for (int n : notes)
            c.push_back ({ n, vel, 1, 0 });
        return c;
    }

    std::vector<int> stateOf (const std::vector<ChordNote>& c, const ScaleContext& s)
    {
        std::vector<int> p;
        for (auto& n : c) p.push_back (n.note);
        return computeScaleState (p, s);
    }

    Event phraseOn (int time, int number, int vel = 100, int offset = 0)
    {
        auto e = Event::on (time, number, vel);
        e.fromPhrase = true;
        e.noteOffset = offset;
        return e;
    }

    Event phraseOff (int time, int number, int offset = 0)
    {
        auto e = Event::off (time, number);
        e.fromPhrase = true;
        e.noteOffset = offset;
        return e;
    }

    bool hasEvent (const std::vector<Event>& ev, std::uint8_t type, int note, int time = -1)
    {
        for (auto& e : ev)
            if (e.type == type && e.note == note && (time < 0 || e.time == time))
                return true;
        return false;
    }
}

TEST_CASE ("Row and octave lane map to chord tones", "[engine][golden]")
{
    EngineSettings s;
    s.scale = ScaleContext::make (Chromatic, 0);
    const auto chord = chordOf ({ 60, 64, 67 });
    const auto state = stateOf (chord, s.scale);

    CHECK (ArpEngine::pitchOf (50, 0, chord, state, s) == 60);
    CHECK (ArpEngine::pitchOf (51, 0, chord, state, s) == 64);
    CHECK (ArpEngine::pitchOf (52, 0, chord, state, s) == 67);
    CHECK (ArpEngine::pitchOf (62, 0, chord, state, s) == 79);
    CHECK (ArpEngine::pitchOf (40, 0, chord, state, s) == 48);
    CHECK (ArpEngine::pitchOf (53, 0, chord, state, s) == -1);   // row 3 of a 3-note chord: silent
    CHECK (ArpEngine::pitchOf (72, 0, chord, state, s) == 91);
    CHECK (ArpEngine::pitchOf (30, 0, chord, state, s) == 36);
}

TEST_CASE ("Out-of-range pitches are dropped", "[engine]")
{
    EngineSettings s;
    const auto high = chordOf ({ 120 });
    CHECK (ArpEngine::pitchOf (60, 0, high, stateOf (high, s.scale), s) == -1);   // 132
    const auto low = chordOf ({ 5 });
    CHECK (ArpEngine::pitchOf (40, 0, low, stateOf (low, s.scale), s) == -1);     // -7
}

TEST_CASE ("Diatonic Note_Offset: C major, C-E-G, Number 51, +3 -> G", "[engine][golden]")
{
    EngineSettings s;
    s.scale = ScaleContext::make (Major, 0);
    const auto chord = chordOf ({ 60, 64, 67 });
    const auto state = stateOf (chord, s.scale);
    REQUIRE (state == std::vector<int> { 0, 2, 4 });
    CHECK (ArpEngine::pitchOf (51, 3, chord, state, s) == 67);
}

TEST_CASE ("Diatonic Note_Offset -2 on row 0 snaps down", "[engine][golden]")
{
    EngineSettings s;
    s.scale = ScaleContext::make (Major, 0);
    const auto chord = chordOf ({ 60, 64, 67 });
    const auto state = stateOf (chord, s.scale);
    // root 60 - 2 = 58 (A#) is not in C major: snapping down gives A (57)
    CHECK (ArpEngine::pitchOf (50, -2, chord, state, s) == 57);
    // row 2 keeps the chord shape: state[2] = 4 scale steps above A = E
    CHECK (ArpEngine::pitchOf (52, -2, chord, state, s) == 64);
}

TEST_CASE ("Note_Offset with a chromatic scale is a plain transpose", "[engine]")
{
    EngineSettings s;
    s.scale = ScaleContext::make (Chromatic, 0);
    const auto chord = chordOf ({ 60, 64, 67 });
    const auto state = stateOf (chord, s.scale);
    CHECK (ArpEngine::pitchOf (51, 3, chord, state, s) == 67);
    CHECK (ArpEngine::pitchOf (52, -2, chord, state, s) == 65);
}

TEST_CASE ("Velocity scaling and Ignore_Host_Velocities", "[engine][golden]")
{
    EngineSettings s;
    CHECK (ArpEngine::velocityOf (100, 64, s, nullptr) == 50);   // round(50.39)
    s.ignoreHostVelocities = true;
    CHECK (ArpEngine::velocityOf (100, 64, s, nullptr) == 100);
}

TEST_CASE ("Velocity rounding is half-to-even", "[engine]")
{
    CHECK (ArpEngine::roundHalfEven (2.5) == 2);
    CHECK (ArpEngine::roundHalfEven (3.5) == 4);
    CHECK (ArpEngine::roundHalfEven (50.39) == 50);
    // 127/127 * 1 = 1; 127 * 0.5 -> 63.5 -> 64
    EngineSettings s;
    CHECK (ArpEngine::velocityOf (127, 1, s, nullptr) == 1);
}

TEST_CASE ("Free vs Static: a held phrase note across a chord change C -> F", "[engine][golden]")
{
    for (const bool staticMode : { false, true })
    {
        INFO ("static mode = " << staticMode);
        ArpEngine engine;
        EngineSettings s;
        s.staticMode = staticMode;
        s.ignoreHostVelocities = true;

        std::vector<Event> out;
        // block 1: chord C, then the phrase note (Number 50) starts
        engine.processBlock ({ Event::on (0, 60, 100) }, { phraseOn (10, 50) }, true, false, s, out);
        REQUIRE (hasEvent (out, Event::On, 60, 10));

        // block 2: the chord changes to F while the phrase note is held
        out.clear();
        engine.processBlock ({ Event::off (5, 60), Event::on (5, 65, 100) }, {}, true, false, s, out);
        if (! staticMode)
        {
            CHECK (out.size() == 2);
            CHECK (hasEvent (out, Event::Off, 60, 5));
            CHECK (hasEvent (out, Event::On, 65, 5));
        }
        else
        {
            CHECK (out.empty());
        }

        // block 3: the phrase note ends
        out.clear();
        engine.processBlock ({}, { phraseOff (3, 50) }, true, false, s, out);
        REQUIRE (out.size() == 1);
        CHECK (out[0].type == Event::Off);
        CHECK (out[0].note == (staticMode ? 60 : 65));
    }
}

TEST_CASE ("Static engine picks up the new chord on the next phrase note", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;
    s.staticMode = true;
    s.ignoreHostVelocities = true;
    std::vector<Event> out;

    engine.processBlock ({ Event::on (0, 60, 100) }, { phraseOn (1, 50) }, true, false, s, out);
    engine.processBlock ({ Event::off (0, 60), Event::on (0, 65, 100) }, {}, true, false, s, out);
    out.clear();
    engine.processBlock ({}, { phraseOff (0, 50), phraseOn (2, 50) }, true, false, s, out);
    REQUIRE (out.size() == 2);
    CHECK ((out[0].type == Event::Off && out[0].note == 60));
    CHECK ((out[1].type == Event::On && out[1].note == 65));
}

TEST_CASE ("Static engine runs whenever the transport is stopped", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;   // Free selected, but the transport is stopped
    s.ignoreHostVelocities = true;
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 60, 100) }, { phraseOn (4, 50) }, false, false, s, out);
    engine.processBlock ({ Event::off (0, 60), Event::on (0, 62, 100) }, {}, false, false, s, out);
    REQUIRE (out.size() == 1);   // no re-pitching: Static behaviour
    CHECK (out[0].note == 60);
}

TEST_CASE ("Zero-length notes are removed as a pair", "[engine][golden]")
{
    std::vector<Event> ev { Event::on (5, 60, 100), Event::on (5, 64, 100), Event::off (5, 60), Event::off (9, 64) };
    ArpEngine::removeZeroLength (ev);
    REQUIRE (ev.size() == 2);
    CHECK ((ev[0].type == Event::On && ev[0].note == 64));
    CHECK ((ev[1].type == Event::Off && ev[1].note == 64));

    // through the engine: a phrase note that starts and ends at the same sample
    ArpEngine engine;
    EngineSettings s;
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 60, 100) }, { phraseOn (7, 50), phraseOff (7, 50) }, true, false, s, out);
    CHECK (out.empty());
}

TEST_CASE ("Host events are forced to time 0 when stopped at position 0", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;
    s.ignoreHostVelocities = true;
    std::vector<Event> out;
    // the chord arrives at sample 50, the phrase note at 10: forced to 0, the chord is already there
    engine.processBlock ({ Event::on (50, 60, 100) }, { phraseOn (10, 50) }, false, true, s, out);
    REQUIRE (out.size() == 1);
    CHECK (out[0].note == 60);
    CHECK (out[0].time == 10);
}

TEST_CASE ("Chord is sorted and keeps duplicates", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 67, 90), Event::on (0, 60, 80), Event::on (0, 64, 70, 2), Event::on (0, 60, 50, 3) },
                         {}, true, false, s, out);
    const auto& c = engine.getChord();
    REQUIRE (c.size() == 4);
    CHECK (c[0].note == 60);
    CHECK (c[1].note == 60);
    CHECK (c[2].note == 64);
    CHECK (c[3].note == 67);
    CHECK (engine.getScaleState().size() == 3);   // unique pitches only
}

TEST_CASE ("Output channel and velocity follow the chord tone", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 60, 64, 3), Event::on (0, 64, 127, 5) }, { phraseOn (1, 51, 100) }, true, false, s, out);
    REQUIRE (out.size() == 1);
    CHECK (out[0].note == 64);
    CHECK (out[0].channel == 5);
    CHECK (out[0].vel == 100);
}

TEST_CASE ("Scale Lock quantises output to the key and scale", "[engine][scalelock]")
{
    EngineSettings s;
    s.scale = ScaleContext::make (Major, 0);
    s.scaleLock = true;
    const auto chord = chordOf ({ 61, 66, 68 });   // C#, F#, G#: none in C major
    const auto state = stateOf (chord, s.scale);
    CHECK (ArpEngine::pitchOf (50, 0, chord, state, s) == 60);   // C# -> C (ties go down)
    CHECK (ArpEngine::pitchOf (51, 0, chord, state, s) == 65);   // F# -> F
    CHECK (ArpEngine::pitchOf (52, 0, chord, state, s) == 67);   // G# -> G

    s.scale = ScaleContext::make (MinorPentatonic, 9);   // A minor pentatonic: A C D E G
    const auto c2 = chordOf ({ 59, 62, 65 });
    const auto st2 = stateOf (c2, s.scale);
    CHECK (ArpEngine::pitchOf (50, 0, c2, st2, s) == 60);   // B -> C (nearest)
    CHECK (ArpEngine::pitchOf (51, 0, c2, st2, s) == 62);   // D stays
    CHECK (ArpEngine::pitchOf (52, 0, c2, st2, s) == 64);   // F -> E
}

TEST_CASE ("Free engine never leaves notes hanging when the scale changes mid-note", "[engine][scalelock]")
{
    ArpEngine engine;
    EngineSettings s;
    s.ignoreHostVelocities = true;
    s.scaleLock = true;
    s.scale = ScaleContext::make (Major, 0);
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 61, 100) }, { phraseOn (1, 50) }, true, false, s, out);
    REQUIRE (out.back().note == 60);
    s.scale = ScaleContext::make (Major, 2);   // D major: C# is in the scale now
    out.clear();
    engine.processBlock ({}, { phraseOff (3, 50) }, true, false, s, out);
    REQUIRE (out.size() == 1);
    CHECK (out[0].note == 60);   // the note that actually sounded
}

TEST_CASE ("Switching engines mid-note switches the sounding notes off", "[engine]")
{
    ArpEngine engine;
    EngineSettings s;
    s.ignoreHostVelocities = true;
    std::vector<Event> out;
    engine.processBlock ({ Event::on (0, 60, 100) }, { phraseOn (1, 50) }, true, false, s, out);
    out.clear();
    s.staticMode = true;
    engine.processBlock ({}, {}, true, false, s, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].note == 60));
}
