#pragma once
#include "Common.h"

namespace Reel
{
// Simple RBJ biquad used by the effects
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void bandpass (double sr, double f, double q)
    {
        auto w = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, sr * 0.45, f) / sr, al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
        b0 = (float) (al / a0); b1 = 0; b2 = (float) (-al / a0); a1 = (float) (-2.0 * std::cos (w) / a0); a2 = (float) ((1.0 - al) / a0);
    }
    inline float process (float x) noexcept { auto y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

//==============================================================================
/** Distortion with three characters. Drive and Amount are automatable. */
class DistortionPlugin : public te::Plugin
{
public:
    static const char* getPluginName() { return NEEDS_TRANS ("Distortion"); }
    static inline const char* xmlTypeName = "reelDistortion";

    DistortionPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        auto um = getUndoManager();
        driveValue.referTo (state, "drive", um, 0.45f);
        toneValue.referTo (state, "tone", um, 0.7f);
        mixValue.referTo (state, "mix", um, 0.7f);
        typeValue.referTo (state, "character", um, 0);
        drive = addParam ("drive", TRANS ("Drive"), { 0.0f, 1.0f });
        tone  = addParam ("tone", TRANS ("Tone"), { 0.0f, 1.0f });
        mix   = addParam ("mix", TRANS ("Amount"), { 0.0f, 1.0f });
        for (auto* p : { drive.get(), tone.get(), mix.get() })
            p->valueToStringFunction = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        drive->attachToCurrentValue (driveValue);
        tone->attachToCurrentValue (toneValue);
        mix->attachToCurrentValue (mixValue);
    }

    ~DistortionPlugin() override
    {
        notifyListenersOfDeletion();
        drive->detachFromCurrentValue(); tone->detachFromCurrentValue(); mix->detachFromCurrentValue();
    }

    juce::String getName() const override              { return TRANS ("Distortion"); }
    juce::String getPluginType() override              { return xmlTypeName; }
    juce::String getShortName (int) override           { return "Dist"; }
    juce::String getSelectableDescription() override   { return getName(); }
    bool needsConstantBufferSize() override            { return false; }

    void initialise (const te::PluginInitialisationInfo& info) override { sampleRate = info.sampleRate; lp[0] = lp[1] = 0; }
    void deinitialise() override {}
    void reset() override { lp[0] = lp[1] = 0; }

    void applyToBuffer (const te::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr || ! isEnabled()) return;
        const float d = drive->getCurrentValue(), m = mix->getCurrentValue();
        const float g = 1.0f + std::pow (d, 1.6f) * 59.0f;
        const float cutoff = 400.0f * std::pow (2.0f, tone->getCurrentValue() * 5.5f);
        const float k = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sampleRate);
        const int type = typeValue.get();
        const int nch = juce::jmin (2, fc.destBuffer->getNumChannels());

        for (int c = 0; c < nch; ++c)
        {
            auto* x = fc.destBuffer->getWritePointer (c, fc.bufferStartSample);
            float z = lp[c];
            for (int i = 0; i < fc.bufferNumSamples; ++i)
            {
                const float in = x[i], v = in * g;
                float y;
                if (type == 1)      y = v >= 0 ? 1.0f - std::exp (-v) : -(1.0f - std::exp (v * 1.6f)) * 0.8f;   // fuzz
                else if (type == 2) y = std::round (std::tanh (v * 0.5f) * 8.0f) / 8.0f;                         // bit crush
                else                y = std::tanh (v);                                                          // overdrive
                z += k * (y * 0.55f - z);
                x[i] = in * (1.0f - m) + z * m;
            }
            lp[c] = z;
        }
    }

    void restorePluginStateFromValueTree (const juce::ValueTree& v) override
    {
        te::copyPropertiesToCachedValues (v, driveValue, toneValue, mixValue, typeValue);
        for (auto p : getAutomatableParameters()) p->updateFromAttachedValue();
    }

    juce::CachedValue<float> driveValue, toneValue, mixValue;
    juce::CachedValue<int> typeValue;
    te::AutomatableParameter::Ptr drive, tone, mix;

private:
    double sampleRate = 44100.0;
    float lp[2] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DistortionPlugin)
};

//==============================================================================
/** Channel vocoder: the track's audio shapes a built-in synth carrier (note, chord or whisper). */
class VocoderPlugin : public te::Plugin
{
public:
    static const char* getPluginName() { return NEEDS_TRANS ("Vocoder"); }
    static inline const char* xmlTypeName = "reelVocoder";
    static constexpr int numBands = 18;

    VocoderPlugin (te::PluginCreationInfo info) : te::Plugin (info)
    {
        auto um = getUndoManager();
        mixValue.referTo (state, "mix", um, 1.0f);
        breathValue.referTo (state, "breath", um, 0.15f);
        levelValue.referTo (state, "level", um, 0.7f);
        noteValue.referTo (state, "note", um, 48);
        carrierValue.referTo (state, "carrier", um, 1);
        mix    = addParam ("mix", TRANS ("Amount"), { 0.0f, 1.0f });
        breath = addParam ("breath", TRANS ("Breath"), { 0.0f, 1.0f });
        level  = addParam ("level", TRANS ("Output"), { 0.0f, 1.5f });
        for (auto* p : { mix.get(), breath.get(), level.get() })
            p->valueToStringFunction = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        mix->attachToCurrentValue (mixValue); breath->attachToCurrentValue (breathValue); level->attachToCurrentValue (levelValue);
    }

    ~VocoderPlugin() override
    {
        notifyListenersOfDeletion();
        mix->detachFromCurrentValue(); breath->detachFromCurrentValue(); level->detachFromCurrentValue();
    }

    juce::String getName() const override              { return TRANS ("Vocoder"); }
    juce::String getPluginType() override              { return xmlTypeName; }
    juce::String getShortName (int) override           { return "Voc"; }
    juce::String getSelectableDescription() override   { return getName(); }
    bool needsConstantBufferSize() override            { return false; }

    void initialise (const te::PluginInitialisationInfo& info) override
    {
        sr = info.sampleRate;
        for (int b = 0; b < numBands; ++b)
        {
            const double f = 110.0 * std::pow (7200.0 / 110.0, b / (double) (numBands - 1));
            mod[b].bandpass (sr, f, 6.0); car[b].bandpass (sr, f, 6.0); mod[b].reset(); car[b].reset();
            env[b] = 0; envGain[b] = (float) (3.2 * std::pow (f / 250.0, 0.35));
        }
        envCoef = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 28.0 / sr));
    }
    void deinitialise() override {}

    void applyToBuffer (const te::PluginRenderContext& fc) override
    {
        if (fc.destBuffer == nullptr || ! isEnabled()) return;
        auto& buf = *fc.destBuffer;
        const int nch = juce::jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
        if (nch == 0) return;
        const float m = mix->getCurrentValue(), noiseAmt = breath->getCurrentValue() * 0.6f, out = level->getCurrentValue() * 5.5f;
        const int type = carrierValue.get();
        const double base = juce::MidiMessage::getMidiNoteInHertz (juce::jlimit (12, 108, noteValue.get()));
        const double ratios[4] = { 1.0, 1.4983, 2.0, 0.5 };
        const float gains[4] = { type == 1 ? 0.5f : 0.8f, type == 1 ? 0.35f : 0.0f, type == 1 ? 0.3f : 0.0f, type == 1 ? 0.3f : 0.0f };

        for (int i = 0; i < n; ++i)
        {
            float x = 0;
            for (int c = 0; c < nch; ++c) x += buf.getSample (c, s0 + i);
            x /= (float) nch;

            float carrier = 0;
            if (type != 2)
                for (int o = 0; o < 4; ++o)
                {
                    if (gains[o] == 0) continue;
                    const double inc = base * ratios[o] / sr;
                    phase[o] += inc; if (phase[o] >= 1.0) phase[o] -= 1.0;
                    carrier += gains[o] * (float) polyBlepSaw (phase[o], inc);
                }
            carrier += (rng.nextFloat() * 2.0f - 1.0f) * (type == 2 ? 0.7f : noiseAmt);

            float y = 0;
            for (int b = 0; b < numBands; ++b)
            {
                const float mb = mod[b].process (x);
                env[b] += envCoef * (std::abs (mb) - env[b]);
                y += car[b].process (carrier) * env[b] * envGain[b];
            }
            y *= out;
            for (int c = 0; c < nch; ++c)
            {
                const float dry = buf.getSample (c, s0 + i);
                buf.setSample (c, s0 + i, dry * (1.0f - m) + y * m);
            }
        }
    }

    void restorePluginStateFromValueTree (const juce::ValueTree& v) override
    {
        te::copyPropertiesToCachedValues (v, mixValue, breathValue, levelValue, noteValue, carrierValue);
        for (auto p : getAutomatableParameters()) p->updateFromAttachedValue();
    }

    juce::CachedValue<float> mixValue, breathValue, levelValue;
    juce::CachedValue<int> noteValue, carrierValue;   // carrier: 0 single note, 1 chord, 2 whisper
    te::AutomatableParameter::Ptr mix, breath, level;

private:
    static double polyBlepSaw (double t, double dt)
    {
        double v = 2.0 * t - 1.0;
        if (t < dt)            { auto x = t / dt; v -= x + x - x * x - 1.0; }
        else if (t > 1.0 - dt) { auto x = (t - 1.0) / dt; v -= x * x + x + x + 1.0; }
        return v;
    }
    double sr = 44100.0, phase[4] {};
    Biquad mod[numBands], car[numBands];
    float env[numBands] {}, envGain[numBands] {}, envCoef = 0.004f;
    juce::Random rng;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocoderPlugin)
};
}
