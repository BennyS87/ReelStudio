#include "ReelPlugins.h"

namespace Reel
{
using namespace juce;
static constexpr double twoPi = MathConstants<double>::twoPi;

double BiquadD::magnitude (double hz, double sr) const
{
    const std::complex<double> z1 = std::polar (1.0, -twoPi * hz / sr), z2 = z1 * z1;
    return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
}

namespace Fmt
{
    String pct (float v)   { return String (roundToInt (v * 100.0f)) + "%"; }
    String db (float v)    { return v <= -99.0f ? String ("-inf dB") : (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; }
    String hz (float v)    { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; }
    String ms (float v)    { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (v, v < 10.0f ? 1 : 0) + " ms"; }
    String sec (float v)   { return String (v, v < 10.0f ? 2 : 1) + " s"; }
    String ratio (float v) { return v >= 19.9f ? String ("inf:1") : String (v, 1) + ":1"; }
    String st (float v)    { return (v > 0 ? "+" : "") + String (v, 1) + " st"; }
    NormalisableRange<float> skewed (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; }
}

//==============================================================================
ReelEffect::~ReelEffect()
{
    notifyListenersOfDeletion();
    for (auto& p : params) p->param->detachFromCurrentValue();
}

te::AutomatableParameter* ReelEffect::addP (const String& id, const String& name, NormalisableRange<float> range, float def, std::function<String (float)> fmt)
{
    auto p = std::make_unique<P>();
    p->value.referTo (state, id, getUndoManager(), def);
    p->param = addParam (id, name, range);
    if (fmt) p->param->valueToStringFunction = std::move (fmt);
    p->param->attachToCurrentValue (p->value);
    auto* raw = p->param.get();
    params.push_back (std::move (p));
    return raw;
}

void ReelEffect::addChoice (CachedValue<int>& v, const String& id, const String& label, StringArray options, int def)
{
    v.referTo (state, id, getUndoManager(), def);
    choices.push_back ({ id, label, std::move (options), &v });
}

void ReelEffect::restorePluginStateFromValueTree (const ValueTree& v)
{
    for (auto& p : params) te::copyPropertiesToCachedValues (v, p->value);
    for (auto& c : choices) te::copyPropertiesToCachedValues (v, *c.value);
    for (auto ap : getAutomatableParameters()) ap->updateFromAttachedValue();
}

double ReelEffect::currentBpm() const
{
    if (auto* t = edit.tempoSequence.getTempo (0)) return jmax (20.0, t->getBpm());
    return 120.0;
}

static float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
static float onePoleCoef (double hz, double sr) { return (float) (1.0 - std::exp (-twoPi * jlimit (1.0, sr * 0.49, hz) / sr)); }
static float hpCoef (double hz, double sr) { return (float) std::exp (-twoPi * jlimit (1.0, sr * 0.49, hz) / sr); }

//==============================================================================
EQPlugin::EQPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    const int defType[] = { lowCut, lowShelf, bell, bell, bell, bell, highShelf, highCut };
    const float defFreq[] = { 40, 110, 300, 800, 2000, 4500, 9000, 18000 };
    const bool defOn[] = { false, true, true, true, true, true, true, false };
    for (int b = 0; b < numBands; ++b)
    {
        auto n = String (b + 1);
        auto& band = bands[b];
        band.freq = addP ("f" + n, "Band " + n + " frequency", Fmt::skewed (20, 20000, 1000), defFreq[b], Fmt::hz);
        band.gain = addP ("g" + n, "Band " + n + " gain", { -24.0f, 24.0f }, 0.0f, Fmt::db);
        band.q    = addP ("q" + n, "Band " + n + " width (Q)", Fmt::skewed (0.1f, 18.0f, 1.0f), 0.71f, [] (float v) { return String (v, 2); });
        addChoice (band.type, "t" + n, "Band " + n + " shape", { "Low cut", "Low shelf", "Bell", "Notch", "High shelf", "High cut" }, defType[b]);
        addChoice (band.on, "on" + n, "Band " + n + " on", { "Off", "On" }, defOn[b] ? 1 : 0);
        addChoice (band.slope, "s" + n, "Band " + n + " slope", { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1);
    }
    output = addP ("out", "Output", { -24.0f, 24.0f }, 0.0f, Fmt::db);
}

void EQPlugin::initialise (const te::PluginInitialisationInfo& info) { sampleRate = info.sampleRate; reset(); }
void EQPlugin::reset() { for (auto& c : state) for (auto& b : c) for (auto& s : b) s = {}; }

int EQPlugin::stagesFor (int b) const
{
    if (bands[b].on.get() == 0) return 0;
    const int t = bands[b].type.get();
    if (t == lowCut || t == highCut) { const int s = bands[b].slope.get(); return s == 0 ? 1 : s == 1 ? 2 : 4; }
    return 1;
}

BiquadD EQPlugin::design (int type, double sr, double f, double gainDb, double q, bool firstStage)
{
    f = jlimit (10.0, sr * 0.49, f);
    if (! firstStage) q = 0.7071;
    const double w = twoPi * f / sr, cs = std::cos (w), sn = std::sin (w), Q = jmax (0.05, q), al = sn / (2.0 * Q), A = std::pow (10.0, gainDb / 40.0), sA = 2.0 * std::sqrt (A) * al;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type)
    {
        case lowCut:    b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case highCut:   b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case notch:     b0 = 1; b1 = -2 * cs; b2 = 1; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; break;
        case lowShelf:  b0 = A * ((A + 1) - (A - 1) * cs + sA); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - sA);
                        a0 = (A + 1) + (A - 1) * cs + sA; a1 = -2 * ((A - 1) + (A + 1) * cs); a2 = (A + 1) + (A - 1) * cs - sA; break;
        case highShelf: b0 = A * ((A + 1) + (A - 1) * cs + sA); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sA);
                        a0 = (A + 1) - (A - 1) * cs + sA; a1 = 2 * ((A - 1) - (A + 1) * cs); a2 = (A + 1) - (A - 1) * cs - sA; break;
        default:        b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; break;   // bell
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

double EQPlugin::bandResponseDb (int b, double hz) const
{
    const int st = stagesFor (b); double db = 0;
    for (int s = 0; s < st; ++s)
        db += 20.0 * std::log10 (jmax (1e-9, design (bands[b].type.get(), sampleRate, bands[b].freq->getCurrentValue(), bands[b].gain->getCurrentValue(),
                                                     bands[b].q->getCurrentValue(), s == 0).magnitude (hz, sampleRate)));
    return db;
}

double EQPlugin::responseDb (double hz) const
{
    double db = output->getCurrentValue();
    for (int b = 0; b < numBands; ++b) db += bandResponseDb (b, hz);
    return db;
}

void EQPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    int st[numBands];
    for (int b = 0; b < numBands; ++b)
    {
        st[b] = stagesFor (b);
        for (int s = 0; s < st[b]; ++s)
            coefs[b][s] = design (bands[b].type.get(), sampleRate, bands[b].freq->getCurrentValue(), bands[b].gain->getCurrentValue(), bands[b].q->getCurrentValue(), s == 0);
    }
    const double g = dbToGain (output->getCurrentValue());
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples;
    for (int c = 0; c < nch; ++c)
    {
        auto* x = buf.getWritePointer (c, fc.bufferStartSample);
        for (int i = 0; i < n; ++i)
        {
            double v = x[i];
            for (int b = 0; b < numBands; ++b) for (int s = 0; s < st[b]; ++s) v = state[c][b][s].run (coefs[b][s], v);
            x[i] = (float) (v * g);
        }
    }
    int w = scopeWrite.load();
    for (int i = 0; i < n; ++i)
    {
        float m = 0; for (int c = 0; c < nch; ++c) m += buf.getSample (c, fc.bufferStartSample + i);
        scope[w] = m / (float) jmax (1, nch); w = (w + 1) % scopeSize;
    }
    scopeWrite.store (w);
}

//==============================================================================
CompressorPlugin::CompressorPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    threshold = addP ("threshold", "Threshold", { -60.0f, 0.0f }, -18.0f, Fmt::db);
    ratio     = addP ("ratio", "Ratio", Fmt::skewed (1.0f, 20.0f, 4.0f), 4.0f, Fmt::ratio);
    knee      = addP ("knee", "Knee", { 0.0f, 24.0f }, 6.0f, Fmt::db);
    attack    = addP ("attack", "Attack", Fmt::skewed (0.1f, 100.0f, 10.0f), 10.0f, Fmt::ms);
    release   = addP ("release", "Release", Fmt::skewed (10.0f, 2000.0f, 150.0f), 150.0f, Fmt::ms);
    makeup    = addP ("makeup", "Makeup", { -12.0f, 24.0f }, 0.0f, Fmt::db);
    mix       = addP ("mix", "Mix", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    scHp      = addP ("schp", "Sidechain low cut", Fmt::skewed (20.0f, 500.0f, 100.0f), 20.0f, Fmt::hz);
    addChoice (detect, "detect", "Detection", { "Peak", "RMS" }, 0);
    addChoice (autoGain, "auto", "Auto makeup", { "Off", "On" }, 0);
}

void CompressorPlugin::initialise (const te::PluginInitialisationInfo& info) { sampleRate = info.sampleRate; reset(); }

float CompressorPlugin::curveDb (float x) const
{
    const float T = threshold->getCurrentValue(), R = jmax (1.0f, ratio->getCurrentValue()), W = knee->getCurrentValue();
    if (W > 0.01f && 2.0f * std::abs (x - T) <= W) { const float d = x - T + W / 2.0f; return x + (1.0f / R - 1.0f) * d * d / (2.0f * W); }
    return x > T ? T + (x - T) / R : x;
}

float CompressorPlugin::autoMakeupDb() const { return -0.5f * curveDb (0.0f); }

void CompressorPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
    if (nch == 0) return;
    const float atk = (float) std::exp (-1.0 / (jmax (0.05f, attack->getCurrentValue()) * 0.001 * sampleRate));
    const float rel = (float) std::exp (-1.0 / (jmax (1.0f, release->getCurrentValue()) * 0.001 * sampleRate));
    const float hpA = hpCoef (scHp->getCurrentValue(), sampleRate), rmsC = (float) std::exp (-1.0 / (0.01 * sampleRate));
    const float mk = makeup->getCurrentValue() + (autoGain.get() ? autoMakeupDb() : 0.0f), m = mix->getCurrentValue();
    const bool useRms = detect.get() == 1;
    float lastIn = -100.0f;
    for (int i = 0; i < n; ++i)
    {
        float d = 0;
        for (int c = 0; c < nch; ++c)
        {
            const float x = buf.getSample (c, s0 + i);
            const float h = hpA * (hpZ[c] + x - hpX[c]); hpX[c] = x; hpZ[c] = h;
            d = jmax (d, std::abs (h));
        }
        float lvl = d;
        if (useRms) { rms = rmsC * rms + (1.0f - rmsC) * d * d; lvl = std::sqrt (rms) * 1.414f; }
        const float inDb = 20.0f * std::log10 (jmax (lvl, 1e-6f));
        const float target = curveDb (inDb) - inDb;
        env = target < env ? atk * env + (1.0f - atk) * target : rel * env + (1.0f - rel) * target;
        const float g = dbToGain (env + mk);
        for (int c = 0; c < nch; ++c) { const float x = buf.getSample (c, s0 + i); buf.setSample (c, s0 + i, x * (1.0f - m) + x * g * m); }
        lastIn = inDb;
    }
    grDb.store (env); levelDb.store (lastIn);
}

//==============================================================================
float SaturationPlugin::shape (int type, float x)
{
    switch (type)
    {
        case 1:  return (float) (std::atan (x * 1.2f) * (2.0 / MathConstants<double>::pi));            // tape
        case 2:  return x >= 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x * 1.4f)) / 1.4f;            // tube (asymmetric)
        case 3:  return jlimit (-1.0f, 1.0f, x);                                                           // hard clip
        case 4:  return std::sin (x);                                                                       // wave fold
        case 5:  { const float y = std::tanh (x); return std::round (y * 12.0f) / 12.0f; }                // digital
        default: return std::tanh (x);                                                                      // analog
    }
}

SaturationPlugin::SaturationPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    drive  = addP ("drive", "Drive", { 0.0f, 36.0f }, 9.0f, Fmt::db);
    bias   = addP ("bias", "Bias (even harmonics)", { -1.0f, 1.0f }, 0.0f, [] (float v) { return String (roundToInt (v * 100)); });
    colour = addP ("colour", "Colour", { -1.0f, 1.0f }, 0.0f, [] (float v) { return v < -0.02f ? "Dark " + String (roundToInt (-v * 100)) : v > 0.02f ? "Bright " + String (roundToInt (v * 100)) : String ("Neutral"); });
    output = addP ("output", "Output", { -24.0f, 12.0f }, -3.0f, Fmt::db);
    mix    = addP ("mix", "Mix", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    addChoice (type, "type", "Character", { "Analog", "Tape", "Tube", "Hard clip", "Wave fold", "Digital" }, 0);
}

void SaturationPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    os = std::make_unique<dsp::Oversampling<float>> (2, 1, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) jmax (64, info.blockSizeSamples));
    dry.setSize (2, jmax (64, info.blockSizeSamples));
    reset();
}

void SaturationPlugin::reset() { if (os) os->reset(); for (int c = 0; c < 2; ++c) tilt[c] = dc[c] = dcX[c] = tape[c] = 0; }

float SaturationPlugin::curve (float x) const
{
    const float g = dbToGain (drive->getCurrentValue()), b = bias->getCurrentValue() * 0.5f;
    return shape (type.get(), x * g + b) - shape (type.get(), b);
}

void SaturationPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || os == nullptr) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels());
    if (nch == 0) return;
    const float g = dbToGain (drive->getCurrentValue()), b = bias->getCurrentValue() * 0.5f, col = colour->getCurrentValue(),
                out = dbToGain (output->getCurrentValue()), m = mix->getCurrentValue();
    const float kT = onePoleCoef (900.0, sampleRate), kTape = onePoleCoef (11000.0, sampleRate), dcA = hpCoef (8.0, sampleRate);
    const int t = type.get(); const float offset = shape (t, b);
    const int maxBlock = dry.getNumSamples();
    for (int done = 0; done < fc.bufferNumSamples;)
    {
        const int n = jmin (maxBlock, fc.bufferNumSamples - done), s0 = fc.bufferStartSample + done;
        for (int c = 0; c < nch; ++c)
        {
            dry.copyFrom (c, 0, buf, c, s0, n);
            auto* x = buf.getWritePointer (c, s0);
            for (int i = 0; i < n; ++i) { tilt[c] += kT * (x[i] - tilt[c]); const float hp = x[i] - tilt[c]; x[i] = tilt[c] * (1.0f - col * 0.6f) + hp * (1.0f + col * 0.6f); }
        }
        dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) nch, (size_t) s0, (size_t) n);
        auto up = os->processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* y = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) y[i] = shape (t, y[i] * g + b) - offset;
        }
        os->processSamplesDown (block);
        for (int c = 0; c < nch; ++c)
        {
            auto* x = buf.getWritePointer (c, s0); auto* d = dry.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                float v = x[i];
                if (t == 1) { tape[c] += kTape * (v - tape[c]); v = tape[c]; }
                const float h = dcA * (dc[c] + v - dcX[c]); dcX[c] = v; dc[c] = h;
                x[i] = d[i] * (1.0f - m) + h * out * m;
            }
        }
        done += n;
    }
}

//==============================================================================
float DistortionPlugin::shape (int type, float x)
{
    switch (type)
    {
        case 1:  return x >= 0 ? 1.0f - std::exp (-x) : -(1.0f - std::exp (x * 1.6f)) * 0.8f;   // fuzz
        case 2:  return std::round (std::tanh (x * 0.5f) * 8.0f) / 8.0f;                          // bit crush
        case 3:  return std::tanh (std::abs (x)) * 1.6f - 0.6f;                                   // rectify (octave-up grit)
        case 4:  return std::sin (x * 0.8f);                                                       // foldback
        default: return std::tanh (x);                                                             // overdrive
    }
}

DistortionPlugin::DistortionPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    drive  = addP ("drive", "Drive", { 0.0f, 1.0f }, 0.45f, Fmt::pct);
    tone   = addP ("tone", "Tone", { 0.0f, 1.0f }, 0.7f, Fmt::pct);
    mix    = addP ("mix", "Amount", { 0.0f, 1.0f }, 0.7f, Fmt::pct);
    bias   = addP ("bias", "Bias", { -1.0f, 1.0f }, 0.0f, [] (float v) { return String (roundToInt (v * 100)); });
    lowCut = addP ("lowcut", "Low cut before", Fmt::skewed (20.0f, 1000.0f, 150.0f), 20.0f, Fmt::hz);
    output = addP ("output", "Output", { -24.0f, 12.0f }, -3.0f, Fmt::db);
    addChoice (typeValue, "character", "Character", { "Overdrive", "Fuzz", "Bit crush", "Rectify", "Foldback" }, 0);
}

void DistortionPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    os = std::make_unique<dsp::Oversampling<float>> (2, 1, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    os->initProcessing ((size_t) jmax (64, info.blockSizeSamples));
    dry.setSize (2, jmax (64, info.blockSizeSamples));
    reset();
}

void DistortionPlugin::reset() { if (os) os->reset(); for (int c = 0; c < 2; ++c) hpZ[c] = hpX[c] = lp[c] = dc[c] = dcX[c] = 0; }

float DistortionPlugin::curve (float x) const
{
    const float g = 1.0f + std::pow (drive->getCurrentValue(), 1.6f) * 59.0f, b = bias->getCurrentValue() * 0.5f;
    return shape (typeValue.get(), x * g + b) - shape (typeValue.get(), b);
}

void DistortionPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || os == nullptr) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels());
    if (nch == 0) return;
    const float g = 1.0f + std::pow (drive->getCurrentValue(), 1.6f) * 59.0f, b = bias->getCurrentValue() * 0.5f, m = mix->getCurrentValue();
    const float hpA = hpCoef (lowCut->getCurrentValue(), sampleRate), k = onePoleCoef (400.0 * std::pow (2.0, tone->getCurrentValue() * 5.5), sampleRate);
    const float out = dbToGain (output->getCurrentValue()) * 0.7f, dcA = hpCoef (8.0, sampleRate);
    const int t = typeValue.get(); const float offset = shape (t, b);
    const int maxBlock = dry.getNumSamples();
    for (int done = 0; done < fc.bufferNumSamples;)
    {
        const int n = jmin (maxBlock, fc.bufferNumSamples - done), s0 = fc.bufferStartSample + done;
        for (int c = 0; c < nch; ++c)
        {
            dry.copyFrom (c, 0, buf, c, s0, n);
            auto* x = buf.getWritePointer (c, s0);
            for (int i = 0; i < n; ++i) { const float h = hpA * (hpZ[c] + x[i] - hpX[c]); hpX[c] = x[i]; hpZ[c] = h; x[i] = h; }
        }
        dsp::AudioBlock<float> block (buf.getArrayOfWritePointers(), (size_t) nch, (size_t) s0, (size_t) n);
        auto up = os->processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* y = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i) y[i] = shape (t, y[i] * g + b) - offset;
        }
        os->processSamplesDown (block);
        for (int c = 0; c < nch; ++c)
        {
            auto* x = buf.getWritePointer (c, s0); auto* d = dry.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                lp[c] += k * (x[i] - lp[c]);
                const float h = dcA * (dc[c] + lp[c] - dcX[c]); dcX[c] = lp[c]; dc[c] = h;
                x[i] = d[i] * (1.0f - m) + h * out * m;
            }
        }
        done += n;
    }
}

//==============================================================================
PhaserPlugin::PhaserPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    rate     = addP ("rate", "Rate", Fmt::skewed (0.02f, 10.0f, 0.6f), 0.4f, [] (float v) { return String (v, 2) + " Hz"; });
    depth    = addP ("depth", "Depth", { 0.0f, 1.0f }, 0.7f, Fmt::pct);
    centre   = addP ("centre", "Centre", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    feedback = addP ("feedback", "Feedback", { -0.95f, 0.95f }, 0.5f, [] (float v) { return String (roundToInt (v * 100)) + "%"; });
    spread   = addP ("spread", "Stereo spread", { 0.0f, 180.0f }, 90.0f, [] (float v) { return String (roundToInt (v)) + " deg"; });
    mix      = addP ("mix", "Mix", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    addChoice (mode, "mode", "Type", { "Phaser", "Flanger", "Chorus" }, 0);
    addChoice (stages, "stages", "Stages", { "2", "4", "6", "8", "12" }, 2);
    addChoice (sync, "sync", "Rate", { "Free (Hz)", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }, 0);
}

void PhaserPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    for (auto& l : line) l.assign ((size_t) (sampleRate * 0.12) + 8, 0.0f);
    reset();
}

void PhaserPlugin::reset() { for (auto& c : ap) for (auto& v : c) v = 0; last[0] = last[1] = 0; for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f); writePos = 0; }

void PhaserPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || line[0].empty()) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
    const int md = mode.get(), numStages = std::array<int, 5> { 2, 4, 6, 8, 12 }[(size_t) jlimit (0, 4, stages.get())];
    const double beats[] = { 0, 1, 2, 4, 8, 16 };
    const double hz = sync.get() > 0 ? currentBpm() / 60.0 / beats[jlimit (1, 5, sync.get())] : rate->getCurrentValue();
    const float dp = depth->getCurrentValue(), ce = centre->getCurrentValue(), fb = feedback->getCurrentValue(), m = mix->getCurrentValue();
    const double off = spread->getCurrentValue() / 360.0, inc = hz / sampleRate;
    const int size = (int) line[0].size();
    auto readAt = [&] (int c, double d)
    {
        double p = writePos - jlimit (1.0, (double) size - 3, d); while (p < 0) p += size;
        const int i0 = (int) p; const float fr = (float) (p - i0);
        return line[c][(size_t) i0] + fr * (line[c][(size_t) ((i0 + 1) % size)] - line[c][(size_t) i0]);
    };
    for (int i = 0; i < n; ++i)
    {
        phase += inc; if (phase >= 1.0) phase -= 1.0;
        for (int c = 0; c < nch; ++c)
        {
            const float lfo = (float) std::sin (twoPi * (phase + c * off));
            const float x = buf.getSample (c, s0 + i);
            float wet = 0;
            if (md == 0)
            {
                const double f = jlimit (20.0, sampleRate * 0.45, 200.0 * std::pow (2.0, ce * 5.0) * std::pow (2.0, lfo * dp * 2.5));
                const double tn = std::tan (MathConstants<double>::pi * f / sampleRate);
                const float a = (float) ((tn - 1.0) / (tn + 1.0));
                float v = x + fb * last[c];
                for (int s = 0; s < numStages; ++s) { const float y = a * v + ap[c][s]; ap[c][s] = v - a * y; v = y; }
                last[c] = v; wet = v;
            }
            else if (md == 1)
            {
                const double baseMs = 0.4 + ce * 8.0, d = baseMs * (1.0 + dp * 0.95 * lfo) * 0.001 * sampleRate;
                wet = readAt (c, d);
                line[c][(size_t) writePos] = x + fb * wet;
            }
            else
            {
                const double baseMs = 8.0 + ce * 22.0;
                const float lfo2 = (float) std::sin (twoPi * (phase + c * off + 0.33));
                const float t1 = readAt (c, (baseMs + dp * 6.0 * lfo) * 0.001 * sampleRate), t2 = readAt (c, (baseMs * 1.3 + dp * 6.0 * lfo2) * 0.001 * sampleRate);
                wet = (t1 + t2) * 0.5f;
                line[c][(size_t) writePos] = x + fb * 0.3f * wet;
            }
            buf.setSample (c, s0 + i, (x * mixDry (m) + wet * mixWet (m)) * 0.75f);
        }
        if (md != 0) writePos = (writePos + 1) % size;
    }
}

//==============================================================================
double EchoPlugin::syncBeats (int i)
{
    const double b[] = { 0, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
    return b[jlimit (0, 7, i)];
}

EchoPlugin::EchoPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    timeL    = addP ("timel", "Left time", Fmt::skewed (1.0f, 2000.0f, 300.0f), 375.0f, Fmt::ms);
    timeR    = addP ("timer", "Right time", Fmt::skewed (1.0f, 2000.0f, 300.0f), 500.0f, Fmt::ms);
    feedback = addP ("feedback", "Repeats", { 0.0f, 1.0f }, 0.4f, Fmt::pct);
    lowCut   = addP ("lowcut", "Low cut", Fmt::skewed (20.0f, 2000.0f, 200.0f), 120.0f, Fmt::hz);
    highCut  = addP ("highcut", "High cut", Fmt::skewed (500.0f, 20000.0f, 4000.0f), 8000.0f, Fmt::hz);
    wowRate  = addP ("wowrate", "Wobble rate", Fmt::skewed (0.05f, 8.0f, 1.0f), 0.6f, [] (float v) { return String (v, 2) + " Hz"; });
    wowDepth = addP ("wowdepth", "Wobble depth", { 0.0f, 1.0f }, 0.1f, Fmt::pct);
    dirt     = addP ("dirt", "Dirt", { 0.0f, 1.0f }, 0.1f, Fmt::pct);
    duck     = addP ("duck", "Ducking", { 0.0f, 1.0f }, 0.0f, Fmt::pct);
    width    = addP ("width", "Width", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    mix      = addP ("mix", "Amount", { 0.0f, 1.0f }, 0.35f, Fmt::pct);
    const StringArray sync { "Free (ms)", "1/16", "1/8", "Dotted 1/8", "1/4", "Dotted 1/4", "1/2", "1 bar" };
    addChoice (mode, "mode", "Mode", { "Stereo", "Ping-pong" }, 0);
    addChoice (syncL, "syncl", "Left timing", sync, 3);
    addChoice (syncR, "syncr", "Right timing", sync, 4);
    addChoice (link, "link", "Link", { "Separate", "Linked" }, 0);
}

void EchoPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    for (auto& l : line) l.assign ((size_t) (sampleRate * 4.5), 0.0f);
    reset();
}

void EchoPlugin::reset()
{
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    writePos = 0; smoothL = smoothR = 0; duckEnv = 0;
    for (int c = 0; c < 2; ++c) hp[c] = hpX[c] = lp[c] = 0;
}

void EchoPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || line[0].empty()) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
    if (nch == 0) return;
    const int size = (int) line[0].size();
    const double spb = 60.0 / currentBpm();
    auto timeFor = [&] (int sync, float ms) { return jlimit (2.0, size - 8.0, (sync > 0 ? syncBeats (sync) * spb : ms * 0.001) * sampleRate); };
    const double tL = timeFor (syncL.get(), timeL->getCurrentValue());
    const double tR = link.get() ? tL : timeFor (syncR.get(), timeR->getCurrentValue());
    if (smoothL <= 0) { smoothL = tL; smoothR = tR; }
    const float fb = jlimit (0.0f, 0.99f, feedback->getCurrentValue()), hpA = hpCoef (lowCut->getCurrentValue(), sampleRate),
                lpK = onePoleCoef (highCut->getCurrentValue(), sampleRate), dt = dirt->getCurrentValue(), dk = duck->getCurrentValue(),
                w = width->getCurrentValue(), m = mix->getCurrentValue();
    const double wowInc = wowRate->getCurrentValue() / sampleRate, wowAmt = wowDepth->getCurrentValue() * 0.003 * sampleRate;
    const bool ping = mode.get() == 1;
    auto readAt = [&] (int c, double d)
    {
        double p = writePos - d; while (p < 0) p += size;
        const int i0 = (int) p; const float fr = (float) (p - i0);
        return line[c][(size_t) i0] + fr * (line[c][(size_t) ((i0 + 1) % size)] - line[c][(size_t) i0]);
    };
    for (int i = 0; i < n; ++i)
    {
        smoothL += (tL - smoothL) * 0.0005; smoothR += (tR - smoothR) * 0.0005;
        wowPhase += wowInc; if (wowPhase >= 1.0) wowPhase -= 1.0;
        const double wm = std::sin (twoPi * wowPhase) * wowAmt;
        const float xL = buf.getSample (0, s0 + i), xR = nch > 1 ? buf.getSample (1, s0 + i) : xL;
        float dl = readAt (0, smoothL + wm), dr = readAt (1, smoothR - wm);
        float f[2] = { dl, dr };
        for (int c = 0; c < 2; ++c)
        {
            const float h = hpA * (hp[c] + f[c] - hpX[c]); hpX[c] = f[c]; hp[c] = h;
            lp[c] += lpK * (h - lp[c]);
            float v = lp[c];
            if (dt > 0.001f) v = (1.0f - dt) * v + dt * std::tanh (v * 2.0f) * 0.5f;
            f[c] = v;
        }
        if (ping) { line[0][(size_t) writePos] = (xL + xR) * 0.5f + fb * f[1]; line[1][(size_t) writePos] = fb * f[0]; }
        else      { line[0][(size_t) writePos] = xL + fb * f[0]; line[1][(size_t) writePos] = xR + fb * f[1]; }
        writePos = (writePos + 1) % size;
        const float lvl = jmax (std::abs (xL), std::abs (xR));
        duckEnv += (lvl > duckEnv ? 0.01f : 0.0002f) * (lvl - duckEnv);
        const float wg = 1.0f - dk * jmin (1.0f, duckEnv * 4.0f);
        const float mid = (dl + dr) * 0.5f, side = (dl - dr) * 0.5f * w;
        const float wl = (mid + side) * wg * mixWet (m), wr = (mid - side) * wg * mixWet (m);
        buf.setSample (0, s0 + i, xL * mixDry (m) + wl);
        if (nch > 1) buf.setSample (1, s0 + i, xR * mixDry (m) + wr);
    }
}

//==============================================================================
ReverbPlugin::ReverbPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    preDelay   = addP ("predelay", "Pre-delay", { 0.0f, 250.0f }, 20.0f, Fmt::ms);
    size       = addP ("size", "Size", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    decay      = addP ("decay", "Decay", Fmt::skewed (0.2f, 20.0f, 2.5f), 2.2f, Fmt::sec);
    damping    = addP ("damping", "High decay", Fmt::skewed (1000.0f, 20000.0f, 6000.0f), 7000.0f, Fmt::hz);
    diffusion  = addP ("diffusion", "Diffusion", { 0.0f, 1.0f }, 0.7f, Fmt::pct);
    modulation = addP ("modulation", "Movement", { 0.0f, 1.0f }, 0.3f, Fmt::pct);
    early      = addP ("early", "Early reflections", { 0.0f, 1.0f }, 0.3f, Fmt::pct);
    lowCut     = addP ("lowcut", "Low cut", Fmt::skewed (20.0f, 1000.0f, 150.0f), 100.0f, Fmt::hz);
    highCut    = addP ("highcut", "High cut", Fmt::skewed (1000.0f, 20000.0f, 6000.0f), 12000.0f, Fmt::hz);
    width      = addP ("width", "Width", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    duck       = addP ("duck", "Ducking", { 0.0f, 1.0f }, 0.0f, Fmt::pct);
    mix        = addP ("mix", "Amount", { 0.0f, 1.0f }, 0.3f, Fmt::pct);
    addChoice (freeze, "freeze", "Freeze", { "Off", "On" }, 0);
}

void ReverbPlugin::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    for (auto& l : lines) l.assign (lineSize, 0.0f);
    pre.assign (preSize, 0.0f);
    const int dl[4] = { 142, 107, 379, 277 };
    for (int k = 0; k < 4; ++k) { diffLen[k] = jmax (8, (int) (dl[k] * sampleRate / 44100.0)); diff[k].assign ((size_t) diffLen[k] + 1, 0.0f); }
    reset();
}

void ReverbPlugin::reset()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    std::fill (pre.begin(), pre.end(), 0.0f);
    for (auto& d : diff) std::fill (d.begin(), d.end(), 0.0f);
    for (auto& d : damp) d = 0;
    linePos = prePos = 0; for (auto& p : diffPos) p = 0;
    for (int c = 0; c < 2; ++c) lc[c] = lcX[c] = hc[c] = 0;
    duckEnv = 0;
}

void ReverbPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr || pre.empty()) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
    if (nch == 0) return;
    static const double base[numLines] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    const double sc = (0.4 + size->getCurrentValue() * 1.4) * sampleRate / 44100.0;
    const bool frz = freeze.get() == 1;
    double len[numLines]; float g[numLines];
    const double T = decay->getCurrentValue();
    for (int k = 0; k < numLines; ++k) { len[k] = jmin ((double) lineSize - 64, base[k] * sc); g[k] = frz ? 1.0f : (float) std::pow (10.0, -3.0 * len[k] / (T * sampleRate)); }
    const float kd = frz ? 1.0f : onePoleCoef (damping->getCurrentValue(), sampleRate);
    const float gd = 0.75f * diffusion->getCurrentValue(), er = early->getCurrentValue(), w = width->getCurrentValue(), dk = duck->getCurrentValue(), m = mix->getCurrentValue();
    const float lcA = hpCoef (lowCut->getCurrentValue(), sampleRate), hcK = onePoleCoef (highCut->getCurrentValue(), sampleRate);
    const int pd = jlimit (1, preSize / 2, (int) (preDelay->getCurrentValue() * 0.001 * sampleRate));
    const double modDepth = modulation->getCurrentValue() * 10.0 * sampleRate / 44100.0, modInc = 0.7 / sampleRate;
    static const float tapMsL[5] = { 7.1f, 13.3f, 19.7f, 29.3f, 37.9f }, tapMsR[5] = { 8.3f, 11.9f, 22.1f, 31.7f, 41.3f }, tapG[5] = { 0.8f, 0.6f, 0.5f, 0.4f, 0.3f };
    const float erScale = 0.5f + size->getCurrentValue();
    auto readLine = [&] (int k, double d)
    {
        double p = linePos - d; while (p < 0) p += lineSize;
        const int i0 = (int) p; const float fr = (float) (p - i0);
        const auto& L = lines[k];
        return L[(size_t) i0] + fr * (L[(size_t) ((i0 + 1) & (lineSize - 1))] - L[(size_t) i0]);
    };
    for (int i = 0; i < n; ++i)
    {
        const float xL = buf.getSample (0, s0 + i), xR = nch > 1 ? buf.getSample (1, s0 + i) : xL;
        const float in = frz ? 0.0f : (xL + xR) * 0.5f;
        pre[(size_t) prePos] = in;
        auto preAt = [&] (int d) { int p = prePos - d; while (p < 0) p += preSize; return pre[(size_t) p]; };
        float v = preAt (pd);
        float erL = 0, erR = 0;
        if (er > 0.001f)
            for (int t = 0; t < 5; ++t)
            {
                erL += tapG[t] * preAt (jmin (preSize - 1, pd + (int) (tapMsL[t] * erScale * 0.001 * sampleRate)));
                erR += tapG[t] * preAt (jmin (preSize - 1, pd + (int) (tapMsR[t] * erScale * 0.001 * sampleRate)));
            }
        prePos = (prePos + 1) % preSize;
        for (int k = 0; k < 4; ++k)
        {
            auto& d = diff[k]; const float del = d[(size_t) diffPos[k]];
            const float y = -gd * v + del; d[(size_t) diffPos[k]] = v + gd * y; v = y;
            diffPos[k] = (diffPos[k] + 1) % diffLen[k];
        }
        modPhase += modInc; if (modPhase >= 1.0) modPhase -= 1.0;
        float y[numLines];
        for (int k = 0; k < numLines; ++k)
        {
            const double md = k < 4 ? modDepth * (1.0 + std::sin (twoPi * (modPhase * (1.0 + k * 0.37) + k * 0.25))) : 0.0;
            float s = readLine (k, len[k] + md);
            damp[k] += kd * (s - damp[k]);
            y[k] = damp[k];
        }
        // Hadamard mixing (orthogonal, energy preserving)
        float h[numLines]; std::copy (y, y + numLines, h);
        for (int step = 1; step < numLines; step <<= 1)
            for (int a = 0; a < numLines; a += step << 1)
                for (int b = a; b < a + step; ++b) { const float p = h[b], q = h[b + step]; h[b] = p + q; h[b + step] = p - q; }
        const float norm = 0.35355339f;
        static const float sign[numLines] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        for (int k = 0; k < numLines; ++k) lines[k][(size_t) linePos] = v * sign[k] * 0.35f + g[k] * h[k] * norm;
        linePos = (linePos + 1) & (lineSize - 1);
        float wl = (y[0] + y[2] + y[4] + y[6]) * 0.4f + erL * er * 0.5f;
        float wr = (y[1] + y[3] + y[5] + y[7]) * 0.4f + erR * er * 0.5f;
        float wet[2] = { wl, wr };
        for (int c = 0; c < 2; ++c)
        {
            const float hpv = lcA * (lc[c] + wet[c] - lcX[c]); lcX[c] = wet[c]; lc[c] = hpv;
            hc[c] += hcK * (hpv - hc[c]); wet[c] = hc[c];
        }
        const float mid = (wet[0] + wet[1]) * 0.5f, side = (wet[0] - wet[1]) * 0.5f * w;
        const float lvl = jmax (std::abs (xL), std::abs (xR));
        duckEnv += (lvl > duckEnv ? 0.01f : 0.0002f) * (lvl - duckEnv);
        const float wg = (1.0f - dk * jmin (1.0f, duckEnv * 4.0f)) * mixWet (m);
        buf.setSample (0, s0 + i, xL * mixDry (m) + (mid + side) * wg);
        if (nch > 1) buf.setSample (1, s0 + i, xR * mixDry (m) + (mid - side) * wg);
    }
}

//==============================================================================
VocoderPlugin::VocoderPlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    mix       = addP ("mix", "Amount", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    breath    = addP ("breath", "Breath", { 0.0f, 1.0f }, 0.15f, Fmt::pct);
    level     = addP ("level", "Output", { 0.0f, 1.5f }, 0.7f, Fmt::pct);
    sharpness = addP ("sharpness", "Sharpness", Fmt::skewed (2.0f, 20.0f, 6.0f), 6.0f, [] (float v) { return String (v, 1); });
    attack    = addP ("attack", "Attack", Fmt::skewed (0.5f, 50.0f, 5.0f), 5.0f, Fmt::ms);
    release   = addP ("release", "Release", Fmt::skewed (5.0f, 500.0f, 60.0f), 60.0f, Fmt::ms);
    formant   = addP ("formant", "Formant shift", { -12.0f, 12.0f }, 0.0f, Fmt::st);
    sibilance = addP ("sibilance", "Sibilance", { 0.0f, 1.0f }, 0.3f, Fmt::pct);
    addChoice (carrierValue, "carrier", "Voice", { "Single note", "Chord", "Whisper" }, 1);
    addChoice (noteValue, "note", "Note", {}, 48);
    addChoice (bandsValue, "bands", "Bands", { "8", "12", "16", "20", "24", "32" }, 3);
}

void VocoderPlugin::initialise (const te::PluginInitialisationInfo& info) { sampleRate = info.sampleRate; designedBands = 0; }

void VocoderPlugin::design (int nb, float q, float form)
{
    const double shift = std::pow (2.0, form / 12.0);
    for (int b = 0; b < nb; ++b)
    {
        const double f = 110.0 * std::pow (7200.0 / 110.0, b / (double) (nb - 1));
        mod[b].bandpass (sampleRate, f, q); car[b].bandpass (sampleRate, f * shift, q);
        envGain[b] = (float) (3.2 * std::pow (f / 250.0, 0.35) * std::sqrt (18.0 / nb) * std::sqrt (q / 6.0));
    }
    sib.bandpass (sampleRate, 8000.0, 0.8);
    designedBands = nb; designedQ = q; designedFormant = form;
}

double VocoderPlugin::polyBlepSaw (double t, double dt)
{
    double v = 2.0 * t - 1.0;
    if (t < dt)            { auto x = t / dt; v -= x + x - x * x - 1.0; }
    else if (t > 1.0 - dt) { auto x = (t - 1.0) / dt; v -= x * x + x + x + 1.0; }
    return v;
}

void VocoderPlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    auto& buf = *fc.destBuffer;
    const int nch = jmin (2, buf.getNumChannels()), n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
    if (nch == 0) return;
    const int nb = std::array<int, 6> { 8, 12, 16, 20, 24, 32 }[(size_t) jlimit (0, 5, bandsValue.get())];
    const float q = sharpness->getCurrentValue(), form = formant->getCurrentValue();
    if (nb != designedBands || std::abs (q - designedQ) > 0.01f || std::abs (form - designedFormant) > 0.01f) design (nb, q, form);
    const float m = mix->getCurrentValue(), noiseAmt = breath->getCurrentValue() * 0.6f, out = level->getCurrentValue() * 5.5f, sb = sibilance->getCurrentValue();
    const float aA = (float) (1.0 - std::exp (-1.0 / (attack->getCurrentValue() * 0.001 * sampleRate)));
    const float aR = (float) (1.0 - std::exp (-1.0 / (release->getCurrentValue() * 0.001 * sampleRate)));
    const int type = carrierValue.get();
    const double baseHz = MidiMessage::getMidiNoteInHertz (jlimit (12, 108, noteValue.get()));
    const double ratios[4] = { 1.0, 1.4983, 2.0, 0.5 };
    const float gains[4] = { type == 1 ? 0.5f : 0.8f, type == 1 ? 0.35f : 0.0f, type == 1 ? 0.3f : 0.0f, type == 1 ? 0.3f : 0.0f };
    for (int i = 0; i < n; ++i)
    {
        float x = 0; for (int c = 0; c < nch; ++c) x += buf.getSample (c, s0 + i); x /= (float) nch;
        float carrier = 0;
        if (type != 2)
            for (int o = 0; o < 4; ++o)
            {
                if (gains[o] == 0) continue;
                const double inc = baseHz * ratios[o] / sampleRate;
                phase[o] += inc; if (phase[o] >= 1.0) phase[o] -= 1.0;
                carrier += gains[o] * (float) polyBlepSaw (phase[o], inc);
            }
        carrier += (rng.nextFloat() * 2.0f - 1.0f) * (type == 2 ? 0.7f : noiseAmt);
        float y = 0;
        for (int b = 0; b < nb; ++b)
        {
            const float mb = std::abs (mod[b].process (x));
            env[b] += (mb > env[b] ? aA : aR) * (mb - env[b]);
            y += car[b].process (carrier) * env[b] * envGain[b];
        }
        y = y * out + sib.process (x) * sb * 2.0f;
        for (int c = 0; c < nch; ++c) { const float dry = buf.getSample (c, s0 + i); buf.setSample (c, s0 + i, dry * (1.0f - m) + y * m); }
    }
}
}
