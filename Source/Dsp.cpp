#include "Dsp.h"
#include "ReelPlugins.h"

namespace Reel
{
namespace
{
    constexpr double SR = 44100.0;
    struct Synth
    {
        std::vector<float> d; juce::Random rng { 1234 };
        explicit Synth (double secs) : d ((size_t) (secs * SR), 0.0f) {}
        float noise() { return rng.nextFloat() * 2.0f - 1.0f; }
        static double expEnv (double t, double len) { return std::exp (-t * 6.9 / juce::jmax (1e-4, len)); }
    };
    struct OnePoleHP { float a, x1 = 0, y1 = 0; OnePoleHP (double f) : a ((float) std::exp (-juce::MathConstants<double>::twoPi * f / SR)) {} float p (float x) { auto y = a * (y1 + x - x1); x1 = x; y1 = y; return y; } };

    std::vector<float> makeVoice (const juce::String& id)
    {
        const double tau = juce::MathConstants<double>::twoPi;
        if (id == "kick")
        {
            Synth s (0.55); double ph = 0;
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR, f = t < 0.07 ? 155.0 * std::pow (52.0 / 155.0, t / 0.07) : 52.0 * std::pow (42.0 / 52.0, juce::jmin (1.0, (t - 0.07) / 0.38));
                ph += tau * f / SR;
                s.d[i] = (float) (std::sin (ph) * Synth::expEnv (t, 0.5) + s.noise() * 0.35 * Synth::expEnv (t, 0.012));
            }
            return s.d;
        }
        if (id == "snare")
        {
            Synth s (0.3); OnePoleHP hp (1300); double ph = 0;
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR, f = 190.0 * std::pow (150.0 / 190.0, juce::jmin (1.0, t / 0.08));
                ph += f / SR; const double tri = 4.0 * std::abs (ph - std::floor (ph + 0.5)) - 1.0;
                s.d[i] = (float) (tri * 0.55 * Synth::expEnv (t, 0.12) + hp.p (s.noise()) * 0.9 * Synth::expEnv (t, 0.24));
            }
            return s.d;
        }
        if (id == "clap")
        {
            Synth s (0.4); Biquad bp; bp.bandpass (SR, 1150, 1.3);
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR; double e;
                if (t < 0.033) { const double lt = std::fmod (t, 0.011); e = 0.9 * std::exp (-lt / 0.004); }
                else e = 0.8 * Synth::expEnv (t - 0.033, 0.32);
                s.d[i] = bp.process (s.noise()) * (float) e * 2.5f;
            }
            return s.d;
        }
        if (id == "chh" || id == "ohh" || id == "crash")
        {
            const double len = id == "chh" ? 0.055 : id == "ohh" ? 0.42 : 1.6;
            Synth s (len + 0.1); OnePoleHP hp1 (id == "crash" ? 4500 : 7000), hp2 (id == "crash" ? 4500 : 7000);
            const double ratios[] = { 2, 3, 4.16, 5.43, 6.79, 8.21 }; double ph[6] {};
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR; double m = 0;
                for (int k = 0; k < 6; ++k) { ph[k] += 40.0 * ratios[k] * (id == "crash" ? 1.3 : 1.0) / SR; m += (std::fmod (ph[k], 1.0) < 0.5 ? 1.0 : -1.0); }
                const float v = (float) (m * 0.18 + s.noise() * (id == "crash" ? 0.6 : 0.35));
                s.d[i] = hp2.p (hp1.p (v)) * (float) Synth::expEnv (t, len);
            }
            return s.d;
        }
        if (id == "ltom" || id == "htom")
        {
            const double f0 = id == "ltom" ? 105.0 : 165.0; Synth s (0.45); double ph = 0;
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR, f = f0 * 1.7 * std::pow (1.0 / 1.7, juce::jmin (1.0, t / 0.12));
                ph += tau * f / SR; s.d[i] = (float) (std::sin (ph) * 0.9 * Synth::expEnv (t, 0.38) + s.noise() * 0.15 * Synth::expEnv (t, 0.03));
            }
            return s.d;
        }
        if (id == "rim")
        {
            Synth s (0.08); Biquad bp; bp.bandpass (SR, 1700, 2.0); double p1 = 0, p2 = 0;
            for (size_t i = 0; i < s.d.size(); ++i)
            {
                const double t = i / SR; p1 += 500 / SR; p2 += 1700 / SR;
                const double v = (std::fmod (p1, 1.0) < 0.5 ? 1 : -1) + (4.0 * std::abs (p2 - std::floor (p2 + 0.5)) - 1.0);
                s.d[i] = bp.process ((float) v) * 2.0f * (float) Synth::expEnv (t, 0.05);
            }
            return s.d;
        }
        // cowbell
        Synth s (0.4); Biquad bp; bp.bandpass (SR, 800, 1.6); double p1 = 0, p2 = 0;
        for (size_t i = 0; i < s.d.size(); ++i)
        {
            const double t = i / SR; p1 += 587 / SR; p2 += 845 / SR;
            const double v = (std::fmod (p1, 1.0) < 0.5 ? 1 : -1) + (std::fmod (p2, 1.0) < 0.5 ? 1 : -1);
            const double e = t < 0.03 ? 0.5 * std::pow (0.36, t / 0.03) : 0.18 * Synth::expEnv (t - 0.03, 0.32);
            s.d[i] = bp.process ((float) v) * (float) e * 2.0f;
        }
        return s.d;
    }
}

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, (unsigned int) buffer.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

juce::Array<juce::File> ensureDrumKitFiles()
{
    juce::Array<juce::File> files;
    auto dir = appDataDir().getChildFile ("Drum kit");
    dir.createDirectory();
    for (auto& v : drumVoices)
    {
        auto f = dir.getChildFile (juce::String (v.id) + ".wav");
        if (! f.existsAsFile())
        {
            auto d = makeVoice (v.id);
            float peak = 1e-6f; for (auto x : d) peak = juce::jmax (peak, std::abs (x));
            juce::AudioBuffer<float> b (1, (int) d.size());
            for (int i = 0; i < (int) d.size(); ++i) b.setSample (0, i, d[(size_t) i] * 0.9f / peak);
            writeWav (f, b, SR);
        }
        files.add (f);
    }
    return files;
}

std::optional<PitchResult> detectPitch (const juce::File& file, double startSec, double lengthSec)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return {};
    const double sr = r->sampleRate;
    const auto start = (juce::int64) (startSec * sr);
    const auto len = (int) juce::jmin ((juce::int64) ((lengthSec > 0 ? lengthSec : 3.0) * sr), r->lengthInSamples - start, (juce::int64) (sr * 4));
    if (len < 4096) return {};
    juce::AudioBuffer<float> b ((int) r->numChannels, len);
    r->read (&b, 0, len, start, true, true);
    std::vector<float> x ((size_t) len, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < len; ++i) x[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    float peak = 0; for (auto v : x) peak = juce::jmax (peak, std::abs (v));
    if (peak < 1e-4f) return {};
    size_t on = 0; while (on < x.size() && std::abs (x[on]) < peak * 0.2f) ++on;
    on += (size_t) (0.03 * sr);
    const int W = 2048, H = W / 2, tauMin = juce::jmax (2, (int) (sr / 2000)), tauMax = juce::jmin (H - 1, (int) (sr / 45));
    std::vector<double> freqs; double bestQ = 0;
    for (int w = 0; w < 6; ++w)
    {
        const size_t s = on + (size_t) (w * H); if (s + (size_t) W > x.size()) break;
        std::vector<double> d ((size_t) tauMax + 1, 0.0), cm ((size_t) tauMax + 1, 1.0);
        for (int tau = 1; tau <= tauMax; ++tau) { double sum = 0; for (int j = 0; j < H; ++j) { auto q = (double) x[s + (size_t) j] - x[s + (size_t) (j + tau)]; sum += q * q; } d[(size_t) tau] = sum; }
        double run = 0; for (int tau = 1; tau <= tauMax; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1.0; }
        int t = -1;
        for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < 0.15) { while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; t = tau; break; }
        if (t < 0) { double m = 1; for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < m) { m = cm[(size_t) tau]; t = tau; } if (m > 0.35) t = -1; }
        if (t > 0)
        {
            const double x0 = cm[(size_t) t - 1], x1 = cm[(size_t) t], x2 = cm[(size_t) t + 1], den = x0 + x2 - 2 * x1;
            const double tt = t + (std::abs (den) > 1e-12 ? (x0 - x2) / (2 * den) : 0.0);
            freqs.push_back (sr / tt); bestQ = juce::jmax (bestQ, 1.0 - x1);
        }
    }
    if (freqs.empty()) return {};
    std::sort (freqs.begin(), freqs.end());
    PitchResult p; p.freq = freqs[freqs.size() / 2];
    const double midi = 69.0 + 12.0 * std::log2 (p.freq / 440.0);
    p.note = juce::roundToInt (midi); p.cents = juce::roundToInt ((midi - p.note) * 100.0); p.confidence = bestQ;
    return p;
}
}
