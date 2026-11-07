#pragma once
#include "ReelPlugins.h"

namespace Reel
{
/** Chops the sound into a rhythm: a ring of steps in time with the song, each opening the sound with its own level,
    shaped by an envelope (attack, decay, sustain, release), with swing and a dry/wet mix. */
class TranceGatePlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelGate", "Trance Gate")
    explicit TranceGatePlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo& i) override { sampleRate = i.sampleRate; }
    void deinitialise() override {}
    void reset() override { env = 0; stage = 3; lastStep = -1; }
    void applyToBuffer (const te::PluginRenderContext&) override;
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    static constexpr int maxSteps = 32;
    static juce::StringArray rateNames() { return { "1/4", "1/8", "1/8 triplet", "1/16", "1/16 triplet", "1/32" }; }
    static double rateBeats (int c) { return std::array<double, 6> { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 }[(size_t) juce::jlimit (0, 5, c)]; }
    int numSteps() const { return steps.get() + 1; }
    float stepLevel (int i) const { return levels[(size_t) juce::jlimit (0, maxSteps - 1, i)].load(); }
    void setStep (int i, float level, juce::UndoManager*);

    juce::CachedValue<int> steps, rate;
    te::AutomatableParameter *attack = nullptr, *decay = nullptr, *sustain = nullptr, *release = nullptr, *swing = nullptr, *mix = nullptr;
    std::atomic<int> currentStep { -1 };

private:
    void loadPattern();
    std::array<std::atomic<float>, maxSteps> levels;
    double freeBeat = 0; float env = 0; int stage = 3, lastStep = -1; float peak = 1;   // stage 0 attack, 1 decay, 2 hold, 3 release
};
}
