#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace te = tracktion;
using namespace std::literals;

namespace Reel
{
    // Palette: deep blue-slate desk with a signal-lamp amber accent
    namespace Col
    {
        const juce::Colour bg      { 0xff101a20 }, panel  { 0xff18242b }, panel2 { 0xff1f2e36 },
                           lane    { 0xff152027 }, lane2  { 0xff19262e }, line   { 0xff2a3b45 },
                           line2   { 0xff22323b }, text   { 0xffd6e2e7 }, muted  { 0xff8197a1 },
                           acc     { 0xffffc247 }, rec    { 0xffff5a67 }, ok     { 0xff56c79a },
                           lcd     { 0xff0c151a };
    }

    inline const juce::Colour trackColours[] = { juce::Colour (0xff6fb8a6), juce::Colour (0xffe07a8a), juce::Colour (0xff8ea4e8),
                                                 juce::Colour (0xffc49ae0), juce::Colour (0xff9bc46a), juce::Colour (0xff5fc0d6),
                                                 juce::Colour (0xffe3a15e), juce::Colour (0xffd6c15a) };

    enum class Kind { audio, synth, sampler, drums, engine };

    namespace IDs
    {
        inline const juce::Identifier kind { "reelKind" }, preset { "reelPreset" }, kitTune { "reelKitTune" },
                                      kitLen { "reelKitLen" }, kitLevel { "reelKitLevel" }, autoParam { "reelAutoParam" },
                                      autoShown { "reelAutoShown" }, sampleName { "reelSampleName" }, samplePath { "reelSamplePath" }, recordInput { "reelRecordInput" };
    }

    inline Kind getKind (const te::Track& t)
    {
        auto s = t.state.getProperty (IDs::kind).toString();
        if (s == "synth")   return Kind::synth;
        if (s == "sampler") return Kind::sampler;
        if (s == "drums")   return Kind::drums;
        if (s == "engine")  return Kind::engine;
        return Kind::audio;
    }

    inline juce::String kindToString (Kind k)
    {
        switch (k) { case Kind::synth: return "synth"; case Kind::sampler: return "sampler"; case Kind::drums: return "drums"; case Kind::engine: return "engine"; default: return "audio"; }
    }

    inline juce::String kindLabel (Kind k)
    {
        switch (k) { case Kind::synth: return "Synth"; case Kind::sampler: return "Sampler"; case Kind::drums: return "Drum machine"; case Kind::engine: return "Engine"; default: return "Audio"; }
    }

    inline bool isInstrument (const te::Track* t) { return t != nullptr && getKind (*t) != Kind::audio; }

    // The ten drum voices and the General MIDI notes they answer to
    struct DrumVoice { const char* id; const char* name; int note; };
    inline const DrumVoice drumVoices[] = {
        { "kick", "Kick", 36 }, { "snare", "Snare", 38 }, { "clap", "Clap", 39 }, { "chh", "Closed hat", 42 },
        { "ohh", "Open hat", 46 }, { "ltom", "Low tom", 45 }, { "htom", "High tom", 50 }, { "rim", "Rim", 37 },
        { "cow", "Cowbell", 56 }, { "crash", "Crash", 49 } };
    constexpr int numDrumVoices = 10;

    inline juce::File appDataDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   #if JUCE_MAC
                    .getChildFile ("Application Support")
                   #endif
                    .getChildFile ("Reel Studio");
        d.createDirectory();
        return d;
    }

    inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 4); }

    inline juce::String fmtSeconds (double s)
    {
        s = juce::jmax (0.0, s);
        auto m = (int) (s / 60.0);
        return juce::String (m) + ":" + juce::String (s - m * 60.0, 1).paddedLeft ('0', 4);
    }
}
