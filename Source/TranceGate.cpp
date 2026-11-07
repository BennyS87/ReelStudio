#include "TranceGate.h"

namespace Reel
{
using namespace juce;
static const Identifier patternID ("pattern");
static String shortTime (float v) { return v < 1.0f ? String (v * 1000.0f, v < 0.01f ? 1 : 0) + " ms" : String (v, 2) + " s"; }

TranceGatePlugin::TranceGatePlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    StringArray stepNames; for (int i = 1; i <= maxSteps; ++i) stepNames.add (String (i));
    addChoice (steps, "steps", "Steps", stepNames, 15);
    addChoice (rate, "rate", "Rate", rateNames(), 3);
    attack = addP ("attack", "Attack", Fmt::skewed (0.0005f, 0.2f, 0.01f), 0.003f, shortTime);
    decay = addP ("decay", "Decay", Fmt::skewed (0.001f, 2.0f, 0.15f), 0.16f, shortTime);
    sustain = addP ("sustain", "Sustain", { 0.0f, 1.0f }, 0.6f, Fmt::pct);
    release = addP ("release", "Release", Fmt::skewed (0.001f, 1.0f, 0.06f), 0.04f, shortTime);
    swing = addP ("swing", "Swing", { 0.0f, 1.0f }, 0.0f, Fmt::pct);
    mix = addP ("mix", "Mix", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    loadPattern();
}

void TranceGatePlugin::loadPattern()
{
    auto parts = StringArray::fromTokens (state.getProperty (patternID).toString(), " ", {});
    // a classic gate to start from: on, off, on, on, off, on, off, on...
    const float def[16] = { 1, 0, 1, .7f, 0, 1, 0, .8f, 1, 0, 1, .7f, 0, 1, .6f, 0 };
    for (int i = 0; i < maxSteps; ++i) levels[(size_t) i].store (i < parts.size() ? jlimit (0.0f, 1.0f, parts[i].getFloatValue()) : def[i % 16]);
}

void TranceGatePlugin::setStep (int i, float level, UndoManager* um)
{
    levels[(size_t) jlimit (0, maxSteps - 1, i)].store (jlimit (0.0f, 1.0f, level));
    StringArray s; for (auto& l : levels) s.add (String (l.load(), 2));
    state.setProperty (patternID, s.joinIntoString (" "), um);
}

void TranceGatePlugin::restorePluginStateFromValueTree (const ValueTree& v)
{
    ReelEffect::restorePluginStateFromValueTree (v);
    if (v.hasProperty (patternID)) state.setProperty (patternID, v.getProperty (patternID), nullptr);
    loadPattern();
}

void TranceGatePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    auto& buf = *fc.destBuffer;
    const int n = fc.bufferNumSamples, chans = buf.getNumChannels();
    const double bps = currentBpm() / 60.0, sr = sampleRate;
    const double b0 = fc.isPlaying ? fc.editTime.getStart().inSeconds() * bps : freeBeat;   // its own clock while stopped
    freeBeat = b0 + n / sr * bps;
    const double stepLen = rateBeats (rate.get()), swingDelay = stepLen * 0.33 * swing->getCurrentValue();
    const int count = numSteps();
    const float aRate = 1.0f / (float) (jmax (0.0005f, attack->getCurrentValue()) * sr);
    const float dCoef = std::exp (-4.6f / (float) (jmax (0.001f, decay->getCurrentValue()) * sr));
    const float rCoef = std::exp (-4.6f / (float) (jmax (0.001f, release->getCurrentValue()) * sr));
    const float sus = sustain->getCurrentValue(), wet = mix->getCurrentValue();
    for (int i = 0; i < n; ++i)
    {
        const double beat = b0 + i / sr * bps;
        double idxD = std::floor (beat / stepLen);
        if ((((long long) idxD) & 1) != 0 && beat - idxD * stepLen < swingDelay) idxD -= 1;   // swung steps start a little late
        const int idx = (int) ((((long long) idxD) % count + count) % count);
        if ((long long) idxD != lastStep)
        {
            lastStep = (int) ((long long) idxD);
            currentStep.store (idx);
            const float lv = levels[(size_t) idx].load();
            if (lv > 0.0f) { stage = 0; peak = lv; } else stage = 3;
        }
        // the step ends: let go so the next one is heard as a new hit
        const double nextStart = (idxD + 1) * stepLen + ((((long long) idxD + 1) & 1) != 0 ? swingDelay : 0.0);
        if (stage < 3 && beat >= nextStart - 0.02 * stepLen) stage = 3;
        switch (stage)
        {
            case 0: env += aRate * peak; if (env >= peak) { env = peak; stage = 1; } break;
            case 1: env = peak * sus + (env - peak * sus) * dCoef; if (env - peak * sus < 0.001f) stage = 2; break;
            case 2: env = peak * sus; break;
            default: env *= rCoef; break;
        }
        const float g = 1.0f - wet + wet * env;
        for (int c = 0; c < chans; ++c) buf.setSample (c, fc.bufferStartSample + i, buf.getSample (c, fc.bufferStartSample + i) * g);
    }
}
}
