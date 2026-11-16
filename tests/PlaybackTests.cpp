#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "mm/ClipPlayer.h"
#include "mm/PhraseProcessor.h"

using namespace mm;
using Catch::Matchers::WithinAbs;

namespace
{
    PlayPhrase onePhraseNote (int number, double start, double end, double length = 384.0, int vel = 100)
    {
        PlayPhrase pp;
        pp.length = length;
        pp.notes.push_back ({ number, 0, vel, start, end });
        return pp;
    }

    TimedNote held (int note, double start, double end, int vel = 100)
    {
        TimedNote t;
        t.note = note;
        t.vel = vel;
        t.start = start;
        t.end = end;
        return t;
    }

    RenderOptions freeOptions()
    {
        RenderOptions o;
        o.settings.engine.ignoreHostVelocities = true;
        o.settings.engine.scale = ScaleContext::make (Chromatic, 0);
        return o;
    }
}

TEST_CASE ("Clip player emits phrase events at exact ticks", "[clip]")
{
    ClipPlayer clip;
    const auto pp = onePhraseNote (51, 10, 34);
    std::vector<Event> out;
    clip.process (pp, 0.0, 1.0, 100, 0, out);   // 1 tick per sample
    REQUIRE (out.size() == 2);
    CHECK ((out[0].type == Event::On && out[0].time == 10 && out[0].note == 51 && out[0].fromPhrase));
    CHECK ((out[1].type == Event::Off && out[1].time == 34));
}

TEST_CASE ("Clip player loops and splits the block at the loop point", "[clip]")
{
    ClipPlayer clip;
    const auto pp = onePhraseNote (50, 0, 24);
    std::vector<Event> out;
    clip.process (pp, 380.0, 1.0, 10, 0, out);   // ticks 380..390 -> wraps at 384
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::On && out[0].time == 4));
}

TEST_CASE ("Notes running to the phrase end stop at the loop point", "[clip]")
{
    ClipPlayer clip;
    const auto pp = onePhraseNote (50, 300, 384);
    std::vector<Event> out;
    clip.process (pp, 290.0, 1.0, 200, 0, out);
    REQUIRE (out.size() == 2);
    CHECK ((out[0].type == Event::On && out[0].time == 10));
    CHECK ((out[1].type == Event::Off && out[1].time == 94));
}

TEST_CASE ("A position jump stops the sounding phrase notes", "[clip]")
{
    ClipPlayer clip;
    const auto pp = onePhraseNote (50, 0, 200);
    std::vector<Event> out;
    clip.process (pp, 0.0, 1.0, 10, 0, out);
    out.clear();
    clip.process (pp, 300.0, 1.0, 10, 0, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].time == 0));
}

TEST_CASE ("Speed scales the phrase position", "[clip]")
{
    // Speed x2: host quarter note 1 = phrase tick 192
    RenderOptions o = freeOptions();
    o.settings.speed = 2.0;
    o.lengthTicks = 384;
    const auto pp = onePhraseNote (50, 0, 48);
    const auto out = renderOffline (pp, nullptr, { held (60, 0, 384) }, o, nullptr);
    REQUIRE (out.size() == 2);   // the phrase plays twice in one bar
    CHECK_THAT (out[0].end, WithinAbs (24.0, 1e-9));
    CHECK_THAT (out[1].start, WithinAbs (192.0, 1e-9));
}

TEST_CASE ("Quantizer moves starts and ends towards the groove grid", "[quantizer]")
{
    Groove g { "test", 96, 96, { 0, 30 } };
    CHECK_THAT (nearestGridTick (20.0, g), WithinAbs (30.0, 1e-9));
    CHECK_THAT (nearestGridTick (10.0, g), WithinAbs (0.0, 1e-9));
    CHECK_THAT (nearestGridTick (90.0, g), WithinAbs (96.0, 1e-9));    // next period
    CHECK_THAT (nearestGridTick (210.0, g), WithinAbs (222.0, 1e-9));  // 192 + 30
    CHECK_THAT (quantizeTick (20.0, 0.5, g), WithinAbs (25.0, 1e-9));
    CHECK_THAT (quantizeTick (20.0, 0.0, g), WithinAbs (20.0, 1e-9));

    // grid rescaled from Global_PPQ
    Groove g192 { "hi-res", 192, 192, { 0, 60 } };
    CHECK_THAT (nearestGridTick (20.0, g192), WithinAbs (30.0, 1e-9));

    Phrase p;
    p.notes = { { 50, 0, 100, 20, 40 } };
    auto pp = makePlayPhrase (p, &g, 1.0, 0.0, false);
    REQUIRE (pp.notes.size() == 1);
    CHECK_THAT (pp.notes[0].start, WithinAbs (30.0, 1e-9));
    CHECK_THAT (pp.notes[0].end, WithinAbs (40.0, 1e-9));

    pp = makePlayPhrase (p, &g, 1.0, 0.0, true);   // linked: the end uses the start strength
    CHECK_THAT (pp.notes[0].end, WithinAbs (31.0, 1e-9));   // 30 would be zero-length: end = start + 1
}

TEST_CASE ("Offline render: chord C-E-G through a phrase", "[render]")
{
    const auto o = freeOptions();
    PlayPhrase pp;
    pp.length = 384;
    pp.notes = { { 50, 0, 100, 0, 48 }, { 51, 0, 90, 96, 144 }, { 62, 0, 80, 192, 240 }, { 53, 0, 80, 288, 300 } };
    const auto out = renderOffline (pp, nullptr, { held (60, 0, 384), held (64, 0, 384), held (67, 0, 384) }, o, nullptr);
    REQUIRE (out.size() == 3);   // Number 53 is silent with a 3-note chord
    CHECK ((out[0].note == 60 && out[0].vel == 100));
    CHECK ((out[1].note == 64 && out[1].vel == 90));
    CHECK ((out[2].note == 79 && out[2].vel == 80));
    CHECK_THAT (out[2].start, WithinAbs (192.0, 1e-9));
    CHECK_THAT (out[2].end, WithinAbs (240.0, 1e-9));
}

TEST_CASE ("Offline render: Free re-pitches, Static waits for the next note", "[render][golden]")
{
    PlayPhrase pp;
    pp.length = 384;
    pp.notes = { { 50, 0, 100, 24, 120 }, { 50, 0, 100, 192, 240 } };
    const std::vector<TimedNote> input { held (60, 0, 60), held (65, 60, 384) };

    auto o = freeOptions();
    auto out = renderOffline (pp, nullptr, input, o, nullptr);
    REQUIRE (out.size() == 3);
    CHECK ((out[0].note == 60 && out[0].start == 24.0 && out[0].end == 60.0));
    CHECK ((out[1].note == 65 && out[1].start == 60.0 && out[1].end == 120.0));
    CHECK ((out[2].note == 65 && out[2].start == 192.0));

    o.settings.engine.staticMode = true;
    out = renderOffline (pp, nullptr, input, o, nullptr);
    REQUIRE (out.size() == 2);
    CHECK ((out[0].note == 60 && out[0].start == 24.0 && out[0].end == 120.0));
    CHECK ((out[1].note == 65 && out[1].start == 192.0));
}

TEST_CASE ("Key map expands input keys into chords", "[render][keymap]")
{
    KeyMap km;
    km.map[0] = { 0, 4, 7 };   // C -> C major triad
    auto o = freeOptions();
    o.settings.keyMapEnabled = true;
    PlayPhrase pp;
    pp.length = 384;
    pp.notes = { { 50, 0, 100, 0, 24 }, { 51, 0, 100, 24, 48 }, { 52, 0, 100, 48, 72 } };
    const auto out = renderOffline (pp, &km, { held (48, 0, 384) }, o, nullptr);
    REQUIRE (out.size() == 3);
    CHECK (out[0].note == 48);
    CHECK (out[1].note == 52);
    CHECK (out[2].note == 55);
}

TEST_CASE ("Channel filters", "[processor]")
{
    PlayPhrase pp = onePhraseNote (50, 0, 24);
    auto o = freeOptions();
    TimedNote ch2 = held (60, 0, 384);
    ch2.channel = 2;

    o.settings.inputChannel = 1;    // only channel 1 gets in
    CHECK (renderOffline (pp, nullptr, { ch2 }, o, nullptr).empty());
    o.settings.inputChannel = 2;
    auto out = renderOffline (pp, nullptr, { ch2 }, o, nullptr);
    REQUIRE (out.size() == 1);
    CHECK (out[0].channel == 2);

    o.settings.outputChannel = 9;
    out = renderOffline (pp, nullptr, { ch2 }, o, nullptr);
    REQUIRE (out.size() == 1);
    CHECK (out[0].channel == 9);

    o.settings.outputChannel = -1;  // Off
    CHECK (renderOffline (pp, nullptr, { ch2 }, o, nullptr).empty());
}

TEST_CASE ("Force internal sync restarts the phrase on the first key", "[processor]")
{
    PhraseProcessor proc;
    CoreSettings s;
    s.forceInternalSync = true;
    s.engine.ignoreHostVelocities = true;
    const auto pp = onePhraseNote (50, 0, 24);

    TransportInfo t;   // host stopped
    t.sampleRate = 48000;
    t.bpm = 120;
    std::vector<Event> out;
    proc.process (t, 512, s, &pp, nullptr, {}, out);
    CHECK (out.empty());   // no chord yet

    out.clear();
    proc.process (t, 512, s, &pp, nullptr, { Event::on (100, 60, 100) }, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::On && out[0].note == 60 && out[0].time == 100));
}

TEST_CASE ("Transport stop switches every arp note off", "[processor]")
{
    PhraseProcessor proc;
    CoreSettings s;
    s.engine.ignoreHostVelocities = true;
    const auto pp = onePhraseNote (50, 0, 300);
    TransportInfo t;
    t.playing = true;
    t.hasPosition = true;
    t.sampleRate = 48000;

    std::vector<Event> out;
    proc.process (t, 256, s, &pp, nullptr, { Event::on (0, 60, 100) }, out);
    REQUIRE (out.size() == 1);

    out.clear();
    t.playing = false;
    t.ppqPosition = 1.0;
    proc.process (t, 256, s, &pp, nullptr, {}, out);
    REQUIRE (out.size() == 1);
    CHECK ((out[0].type == Event::Off && out[0].note == 60));
}

TEST_CASE ("Bypass passes notes through and leaves nothing hanging", "[processor]")
{
    PhraseProcessor proc;
    CoreSettings s;
    s.engine.ignoreHostVelocities = true;
    const auto pp = onePhraseNote (50, 0, 300);
    TransportInfo t;
    t.playing = true;
    t.hasPosition = true;
    t.sampleRate = 48000;

    std::vector<Event> out;
    proc.process (t, 256, s, &pp, nullptr, { Event::on (0, 60, 100) }, out);
    REQUIRE (out.size() == 1);

    s.bypassed = true;
    out.clear();
    t.ppqPosition = 0.01;
    proc.process (t, 256, s, &pp, nullptr, { Event::on (3, 72, 90) }, out);
    REQUIRE (out.size() == 2);
    CHECK ((out[0].type == Event::Off && out[0].note == 60));
    CHECK ((out[1].type == Event::On && out[1].note == 72));

    s.bypassed = false;
    out.clear();
    proc.process (t, 256, s, &pp, nullptr, {}, out);
    REQUIRE (! out.empty());
    CHECK ((out[0].type == Event::Off && out[0].note == 72));
}

TEST_CASE ("Scale Lock in a full render keeps every note in key", "[render][scalelock]")
{
    auto o = freeOptions();
    o.settings.engine.scale = ScaleContext::make (Dorian, 2);   // D dorian = C major notes
    o.settings.engine.scaleLock = true;
    PlayPhrase pp;
    pp.length = 384;
    for (int i = 0; i < 16; ++i)
        pp.notes.push_back ({ 40 + (i % 3) * 10 + (i % 4), 0, 100, i * 24.0, i * 24.0 + 12 });
    const auto out = renderOffline (pp, nullptr, { held (61, 0, 384), held (63, 0, 384), held (66, 0, 384), held (70, 0, 384) }, o, nullptr);
    REQUIRE (out.size() == 16);
    for (auto& n : out)
        CHECK (o.settings.engine.scale.inScale (n.note));
}
