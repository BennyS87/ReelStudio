#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** Plays held notes one after another in time with the song. Sits in front of a track's instrument (synth or a sampled
    instrument), so it works on clips, keyboards and Lines alike. Each note keeps the channel it came in on, which keeps
    a line's own glide and pressure when Lines feeds it. */
class ArpPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelArp", "Arpeggiator")
    explicit ArpPlugin (te::PluginCreationInfo);

    bool takesMidiInput() override                    { return true; }
    bool takesAudioInput() override                   { return true; }
    bool producesAudioWhenNoAudioInput() override     { return false; }
    void initialise (const te::PluginInitialisationInfo& i) override { sampleRate = i.sampleRate; }
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;

    static juce::StringArray modeNames()  { return { "Up", "Down", "Up and down", "Down and up", "As played", "Random", "Chords" }; }
    static juce::StringArray rateNames()  { return { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" }; }
    static double rateBeats (int choice)  { return std::array<double, 6> { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }[(size_t) juce::jlimit (0, 5, choice)]; }

    juce::CachedValue<int> mode, rate, octaves, velMode, hold;
    te::AutomatableParameter* gate = nullptr, *swing = nullptr, *level = nullptr;

private:
    struct Held { int note, channel; float velocity; int order; };
    struct Off { double beat; int note, channel; };
    std::vector<Held> held, latched;
    std::vector<Off> offs;
    int stepCount = 0, orderCounter = 0;
    double origin = 0, freeBeat = 0;
    bool wasPlaying = false;
    juce::Random rng;
    te::MPESourceID sourceID;
};
}
