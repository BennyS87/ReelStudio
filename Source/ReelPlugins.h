#pragma once
#include "Common.h"

namespace Reel
{
// Simple RBJ biquad (also used by the drum kit generator)
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

/** Double-precision biquad used by the EQ. */
struct BiquadD
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double magnitude (double hz, double sr) const;
};
struct BiquadState { double z1 = 0, z2 = 0; inline double run (const BiquadD& c, double x) noexcept { const double y = c.b0 * x + z1; z1 = c.b1 * x - c.a1 * y + z2; z2 = c.b2 * x - c.a2 * y; return y; } };

namespace Fmt
{
    juce::String pct (float v);
    juce::String db (float v);
    juce::String hz (float v);
    juce::String ms (float v);
    juce::String sec (float v);
    juce::String ratio (float v);
    juce::String st (float v);
    juce::NormalisableRange<float> skewed (float lo, float hi, float centre);
}

//==============================================================================
/** Base for Reel's effects: every knob is an automatable, undoable, saved parameter. */
class ReelEffect : public te::Plugin
{
public:
    explicit ReelEffect (te::PluginCreationInfo info) : te::Plugin (info) {}
    ~ReelEffect() override;
    juce::String getShortName (int) override            { return getName().substring (0, 5); }
    juce::String getSelectableDescription() override    { return getName(); }
    bool needsConstantBufferSize() override             { return false; }
    void restorePluginStateFromValueTree (const juce::ValueTree&) override;
    /** Non-automatable choice settings (type, mode, sync...). */
    struct Choice { juce::String id, label; juce::StringArray options; juce::CachedValue<int>* value; };
    std::vector<Choice> choices;
    double sampleRate = 44100.0;

protected:
    te::AutomatableParameter* addP (const juce::String& id, const juce::String& name, juce::NormalisableRange<float> range, float def, std::function<juce::String (float)> fmt);
    void addChoice (juce::CachedValue<int>& v, const juce::String& id, const juce::String& label, juce::StringArray options, int def);
    static float mixDry (float mix) { return juce::jmin (1.0f, 2.0f * (1.0f - mix)); }
    static float mixWet (float mix) { return juce::jmin (1.0f, 2.0f * mix); }
    double currentBpm() const;
private:
    struct P { juce::CachedValue<float> value; te::AutomatableParameter::Ptr param; };
    std::vector<std::unique_ptr<P>> params;
};

#define REEL_EFFECT_NAMES(Type, Title) \
    static const char* getPluginName() { return Title; } \
    static inline const char* xmlTypeName = Type; \
    juce::String getName() const override { return Title; } \
    juce::String getPluginType() override { return xmlTypeName; }

//==============================================================================
class EQPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelEQ", "EQ")
    static constexpr int numBands = 8;
    enum Type { lowCut, lowShelf, bell, notch, highShelf, highCut };
    struct Band { te::AutomatableParameter *freq = nullptr, *gain = nullptr, *q = nullptr; juce::CachedValue<int> type, on, slope; };
    explicit EQPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    double responseDb (double hz) const;                  // whole curve at current settings
    double bandResponseDb (int band, double hz) const;
    static BiquadD design (int type, double sr, double f, double gainDb, double q, bool firstStage);
    int stagesFor (int band) const;
    Band bands[numBands];
    te::AutomatableParameter* output = nullptr;
    // analyser: the last samples of the output, read by the editor
    static constexpr int scopeSize = 4096;
    float scope[scopeSize] {};
    std::atomic<int> scopeWrite { 0 };
private:
    BiquadState state[2][numBands][4];
    BiquadD coefs[numBands][4];
};

//==============================================================================
class CompressorPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelComp", "Compressor")
    explicit CompressorPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override { env = 0; rms = 0; hpZ[0] = hpZ[1] = 0; hpX[0] = hpX[1] = 0; }
    void applyToBuffer (const te::PluginRenderContext&) override;
    float curveDb (float inDb) const;                     // static transfer curve
    float autoMakeupDb() const;
    te::AutomatableParameter *threshold, *ratio, *knee, *attack, *release, *makeup, *mix, *scHp;
    juce::CachedValue<int> detect, autoGain;
    std::atomic<float> grDb { 0 }, levelDb { -100 };
private:
    float env = 0, rms = 0, hpZ[2] {}, hpX[2] {};
};

//==============================================================================
class SaturationPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelSat", "Saturation")
    explicit SaturationPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    static float shape (int type, float x);
    float curve (float x) const;                          // transfer curve at current settings, for display
    te::AutomatableParameter *drive, *bias, *colour, *output, *mix;
    juce::CachedValue<int> type;
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry;
    float tilt[2] {}, dc[2] {}, dcX[2] {}, tape[2] {};
};

//==============================================================================
/** Distortion with oversampling and pre/post filters. */
class DistortionPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelDistortion", "Distortion")
    explicit DistortionPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    static float shape (int type, float x);
    float curve (float x) const;
    te::AutomatableParameter *drive, *tone, *mix, *bias, *lowCut, *output;
    juce::CachedValue<int> typeValue;
private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::AudioBuffer<float> dry;
    float hpZ[2] {}, hpX[2] {}, lp[2] {}, dc[2] {}, dcX[2] {};
};

//==============================================================================
class PhaserPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelPhaser", "Phaser / Flanger / Chorus")
    explicit PhaserPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    te::AutomatableParameter *rate, *depth, *centre, *feedback, *spread, *mix;
    juce::CachedValue<int> mode, stages, sync;
private:
    double phase = 0;
    float ap[2][12] {}, last[2] {};
    std::vector<float> line[2];
    int writePos = 0;
};

//==============================================================================
class EchoPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelEcho", "Echo")
    explicit EchoPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    double getTailLength() const override { return 6.0; }
    static double syncBeats (int index);
    te::AutomatableParameter *timeL, *timeR, *feedback, *lowCut, *highCut, *wowRate, *wowDepth, *dirt, *duck, *width, *mix;
    juce::CachedValue<int> mode, syncL, syncR, link;
private:
    std::vector<float> line[2];
    int writePos = 0;
    double wowPhase = 0, smoothL = 0, smoothR = 0;
    float hp[2] {}, hpX[2] {}, lp[2] {}, duckEnv = 0;
};

//==============================================================================
class ReverbPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelReverb", "Reverb")
    explicit ReverbPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void reset() override;
    void applyToBuffer (const te::PluginRenderContext&) override;
    double getTailLength() const override { return juce::jmin (20.0, (double) decay->getCurrentValue() * 1.5); }
    te::AutomatableParameter *preDelay, *size, *decay, *damping, *diffusion, *modulation, *early, *lowCut, *highCut, *width, *duck, *mix;
    juce::CachedValue<int> freeze;
private:
    static constexpr int numLines = 8, lineSize = 32768, preSize = 65536;
    std::vector<float> lines[numLines], pre;
    std::vector<float> diff[4];
    int linePos = 0, prePos = 0, diffPos[4] {}, diffLen[4] {};
    float damp[numLines] {}, lc[2] {}, lcX[2] {}, hc[2] {}, duckEnv = 0;
    double modPhase = 0;
};

//==============================================================================
/** Channel vocoder: the track's audio shapes a built-in synth carrier. */
class VocoderPlugin : public ReelEffect
{
public:
    REEL_EFFECT_NAMES ("reelVocoder", "Vocoder")
    static constexpr int maxBands = 32;
    explicit VocoderPlugin (te::PluginCreationInfo);
    void initialise (const te::PluginInitialisationInfo&) override;
    void deinitialise() override {}
    void applyToBuffer (const te::PluginRenderContext&) override;
    te::AutomatableParameter *mix, *breath, *level, *sharpness, *attack, *release, *formant, *sibilance;
    juce::CachedValue<int> noteValue, carrierValue, bandsValue;
private:
    void design (int bands, float q, float formantSt);
    static double polyBlepSaw (double t, double dt);
    double phase[4] {};
    Biquad mod[maxBands], car[maxBands], sib;
    float env[maxBands] {}, envGain[maxBands] {};
    int designedBands = 0; float designedQ = 0, designedFormant = 999;
    juce::Random rng;
};
}
