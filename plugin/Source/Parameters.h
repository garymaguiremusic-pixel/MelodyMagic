#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "mm/Progressions.h"
#include "mm/Scales.h"

/** Parameter IDs use the original plugin's serialised property names, so saved states
    can be compared with it directly. Every knob parameter is an integer: knobs move in
    whole steps (drag and mouse wheel). */
namespace mmp
{
    inline constexpr int kParamVersion = 1;

    namespace ParamIDs
    {
        inline constexpr const char* staticMode     = "Static_Mode";
        inline constexpr const char* ignoreHostVel  = "Ignore_Host_Velocities";
        inline constexpr const char* internalSync   = "Force_Internal_Sync";
        inline constexpr const char* speed          = "Speed";
        inline constexpr const char* bypassed       = "Bypassed";
        inline constexpr const char* randomVel      = "Random_Vel";
        inline constexpr const char* quantizeStart  = "Quantize_Start";
        inline constexpr const char* quantizeEnd    = "Quantize_End";
        inline constexpr const char* quantizeLink   = "Quantize_Link";
        inline constexpr const char* inputChannel   = "Midi_Input_Channel_Filter";
        inline constexpr const char* outputChannel  = "Midi_Output_Channel_Filter";
        inline constexpr const char* scaleKey       = "Scale_Key";
        inline constexpr const char* scaleIndex     = "Scale_Index";
        inline constexpr const char* scaleLock      = "Scale_Lock";
        inline constexpr const char* keyMapEnabled  = "Key_Map_Enabled";
    }

    inline juce::StringArray getModeNames()    { return { "Free", "Static" }; }
    inline juce::StringArray getSpeedNames()   { return { "x1/4", "x1/3", "x1/2", "x2/3", "x3/4", "x1", "x3/2", "x2", "x3", "x4" }; }
    inline double speedValue (int index)
    {
        static const double v[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        return v[juce::jlimit (0, 9, index)];
    }
    inline constexpr int kDefaultSpeed = 5;

    /** "All", "Off", "1" .. "16"  ->  0, -1, 1..16 */
    inline juce::StringArray getChannelNames()
    {
        juce::StringArray s { "All", "Off" };
        for (int i = 1; i <= 16; ++i)
            s.add (juce::String (i));
        return s;
    }
    inline int channelValue (int index)   { return index == 0 ? 0 : (index == 1 ? -1 : index - 1); }

    inline juce::StringArray getKeyNames()
    {
        juce::StringArray s;
        for (int k = 0; k < 12; ++k)
            s.add (mm::keyName (k));
        return s;
    }

    inline juce::StringArray getScaleNames()
    {
        juce::StringArray s;
        for (int i = 0; i < mm::kNumScales; ++i)
            s.add (mm::scaleName (i));
        return s;
    }

    // Drag MIDI export -------------------------------------------------------------------
    inline juce::StringArray getExportChordNames()
    {
        juce::StringArray s { "Last Played Chord", "Chord Clip" };
        for (auto& n : mm::progressionNames())
            s.add (juce::String (n));
        return s;
    }
    inline constexpr int kExportLastChord = 0, kExportClip = 1, kExportFirstProgression = 2;

    inline juce::StringArray getExportBarNames()  { return { "Phrase", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" }; }
    inline int exportBars (int index, int phraseBars)
    {
        static const int bars[] = { 0, 1, 2, 4, 8, 16 };
        const int b = bars[juce::jlimit (0, 5, index)];
        return b == 0 ? juce::jmax (1, phraseBars) : b;
    }

    inline juce::String noteName (int note) { return juce::String (mm::noteName (note)); }

    //==============================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        auto id = [] (const char* s) { return ParameterID { s, kParamVersion }; };

        auto choice = [&] (const char* pid, const char* name, const StringArray& items, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (id (pid), name, items, def));
        };
        auto toggle = [&] (const char* pid, const char* name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (id (pid), name, def));
        };
        auto integer = [&] (const char* pid, const char* name, int lo, int hi, int def, const char* suffix)
        {
            layout.add (std::make_unique<AudioParameterInt> (id (pid), name, lo, hi, def,
                AudioParameterIntAttributes().withStringFromValueFunction ([suffix] (int v, int) { return String (v) + suffix; })));
        };

        choice  (ParamIDs::staticMode,    "Arp Mode", getModeNames(), 0);
        toggle  (ParamIDs::ignoreHostVel, "Ignore Host Velocities", false);
        toggle  (ParamIDs::internalSync,  "Force Internal Sync", false);
        choice  (ParamIDs::speed,         "Speed", getSpeedNames(), kDefaultSpeed);
        toggle  (ParamIDs::bypassed,      "Bypass", false);
        integer (ParamIDs::randomVel,     "Random Velocity", 0, 127, 0, "");
        integer (ParamIDs::quantizeStart, "Quantize Start", 0, 100, 0, " %");
        integer (ParamIDs::quantizeEnd,   "Quantize End", 0, 100, 0, " %");
        toggle  (ParamIDs::quantizeLink,  "Quantize Link", false);
        choice  (ParamIDs::inputChannel,  "MIDI Input Channel", getChannelNames(), 0);
        choice  (ParamIDs::outputChannel, "MIDI Output Channel", getChannelNames(), 0);
        choice  (ParamIDs::scaleKey,      "Scale Key", getKeyNames(), 0);
        choice  (ParamIDs::scaleIndex,    "Scale", getScaleNames(), 0);
        toggle  (ParamIDs::scaleLock,     "Scale Lock", false);
        toggle  (ParamIDs::keyMapEnabled, "Key Map", false);
        return layout;
    }
}
