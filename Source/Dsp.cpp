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

//==============================================================================
// Audio to MIDI
namespace Reel
{
namespace
{
    bool loadMono (const juce::File& f, std::vector<float>& out, double& sr)
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) return false;
        sr = r->sampleRate;
        const int n = (int) juce::jmin (r->lengthInSamples, (juce::int64) (sr * 600));
        juce::AudioBuffer<float> b ((int) r->numChannels, n); r->read (&b, 0, n, 0, true, true);
        out.assign ((size_t) n, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < n; ++i) out[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
        return true;
    }

    struct PitchFrames { std::vector<float> med, rms; float maxRms = 0, gate = 0; int hop = 256; };

    PitchFrames pitchFrames (const std::vector<float>& x, double sr, float sens)
    {
        constexpr int W = 2048, half = W / 2, order = 12, N = 1 << order;   // 4096-point FFT for fast YIN
        const int hop = juce::jmax (64, (int) (sr * 0.0058));
        const int frames = (int) ((x.size() > (size_t) W ? x.size() - W : 0) / (size_t) hop);
        const int tauMin = juce::jmax (2, (int) (sr / 1500.0)), tauMax = juce::jmin (half - 2, (int) (sr / 45.0));
        juce::dsp::FFT fft (order);
        std::vector<float> A ((size_t) N * 2), B ((size_t) N * 2), cs ((size_t) W + 1);
        std::vector<float> midi ((size_t) frames, -1.0f), rms ((size_t) frames, 0.0f);
        float maxRms = 1e-9f;
        for (int fr = 0; fr < frames; ++fr)
        {
            const float* s = x.data() + (size_t) fr * (size_t) hop;
            double e = 0; cs[0] = 0;
            for (int j = 0; j < W; ++j) { cs[(size_t) j + 1] = cs[(size_t) j] + s[j] * s[j]; if (j < half) e += s[j] * s[j]; }
            rms[(size_t) fr] = (float) std::sqrt (e / half); maxRms = juce::jmax (maxRms, rms[(size_t) fr]);
            if (e < 1e-10) continue;
            std::fill (A.begin(), A.end(), 0.0f); std::fill (B.begin(), B.end(), 0.0f);
            for (int j = 0; j < half; ++j) A[(size_t) j] = s[j];
            for (int j = 0; j < W; ++j) B[(size_t) j] = s[j];
            fft.performRealOnlyForwardTransform (A.data()); fft.performRealOnlyForwardTransform (B.data());
            for (int k = 0; k <= N / 2; ++k)
            {
                const float ar = A[(size_t) 2 * k], ai = A[(size_t) 2 * k + 1], br = B[(size_t) 2 * k], bi = B[(size_t) 2 * k + 1];
                A[(size_t) 2 * k] = ar * br + ai * bi; A[(size_t) 2 * k + 1] = ar * bi - ai * br;   // conj(A) * B
            }
            fft.performRealOnlyInverseTransform (A.data());   // A[tau] = sum a[j] b[j + tau], up to a scale
            const double scale = std::abs (A[0]) > 1e-20 ? e / A[0] : 1.0;   // zero lag must equal the energy
            double run = 0; float bestV = 1.0f; int best = -1;
            std::vector<float> cm ((size_t) tauMax + 2, 1.0f);
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                const double d = e + (cs[(size_t) (tau + half)] - cs[(size_t) tau]) - 2.0 * A[(size_t) tau] * scale;
                run += d; cm[(size_t) tau] = run > 0 ? (float) (d * tau / run) : 1.0f;
            }
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (cm[(size_t) tau] < 0.15f) { while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; bestV = cm[(size_t) tau]; break; }
            if (best < 0) for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < bestV) { bestV = cm[(size_t) tau]; best = tau; }
            if (best <= 0 || bestV > 0.3f) continue;
            const double x0 = cm[(size_t) best - 1], x1 = cm[(size_t) best], x2 = cm[(size_t) best + 1], den = x0 + x2 - 2 * x1;
            const double t = best + (std::abs (den) > 1e-12 ? (x0 - x2) / (2 * den) : 0.0);
            midi[(size_t) fr] = (float) (69.0 + 12.0 * std::log2 (sr / t / 440.0));
        }
        const float gate = maxRms * (0.01f + (1.0f - sens) * 0.08f);
        // smooth pitch with a 5-frame median over voiced frames
        std::vector<float> med = midi;
        for (int i = 2; i + 2 < frames; ++i)
        {
            std::vector<float> w; for (int k = -2; k <= 2; ++k) if (midi[(size_t) (i + k)] > 0) w.push_back (midi[(size_t) (i + k)]);
            if (midi[(size_t) i] > 0 && w.size() >= 3) { std::sort (w.begin(), w.end()); med[(size_t) i] = w[w.size() / 2]; }
        }
        PitchFrames pf; pf.med = std::move (med); pf.rms = std::move (rms); pf.maxRms = maxRms; pf.gate = gate; pf.hop = hop;
        return pf;
    }

    std::vector<TranscribedNote> melody (const std::vector<float>& x, double sr, float sens)
    {
        constexpr int W = 2048;
        auto pf = pitchFrames (x, sr, sens);
        auto& med = pf.med; auto& rms = pf.rms;
        const float maxRms = pf.maxRms, gate = pf.gate;
        const int hop = pf.hop, frames = (int) med.size();
        std::vector<TranscribedNote> notes;
        const double frameSec = hop / sr, offset = (W * 0.25) / sr;
        const int minFrames = juce::jmax (3, (int) (0.045 / frameSec));
        int start = -1, quiet = 0, away = 0; std::vector<float> pitches; float peak = 0;
        auto finish = [&] (int endFrame)
        {
            if (start >= 0 && endFrame - start >= minFrames && ! pitches.empty())
            {
                std::sort (pitches.begin(), pitches.end());
                TranscribedNote n; n.pitch = juce::roundToInt (pitches[pitches.size() / 2]);
                n.start = start * frameSec + offset; n.length = (endFrame - start) * frameSec;
                n.velocity = juce::jlimit (30, 127, juce::roundToInt (40 + 87 * std::sqrt (peak / maxRms)));
                notes.push_back (n);
            }
            start = -1; pitches.clear(); peak = 0; away = 0;
        };
        for (int i = 0; i < frames; ++i)
        {
            const bool voiced = med[(size_t) i] > 0 && rms[(size_t) i] > gate;
            const bool onset = i >= 3 && rms[(size_t) i] > gate * 2 && rms[(size_t) i] > rms[(size_t) (i - 3)] * 1.8f;
            if (! voiced) { if (start >= 0 && ++quiet >= 3) finish (i - quiet + 1); continue; }
            quiet = 0;
            if (start < 0) { start = i; pitches.push_back (med[(size_t) i]); peak = rms[(size_t) i]; continue; }
            std::vector<float> sorted = pitches; std::sort (sorted.begin(), sorted.end());
            const float centre = sorted[sorted.size() / 2];
            if (std::abs (med[(size_t) i] - centre) > 0.6f) { if (++away >= 3) { finish (i - 2); start = i - 2; for (int k = i - 2; k <= i; ++k) if (med[(size_t) k] > 0) pitches.push_back (med[(size_t) k]); peak = rms[(size_t) i]; } continue; }
            away = 0;
            if (onset && i - start > minFrames) { finish (i); start = i; }
            pitches.push_back (med[(size_t) i]); peak = juce::jmax (peak, rms[(size_t) i]);
        }
        finish (frames);
        return notes;
    }

    std::vector<TranscribedNote> chords (const std::vector<float>& x, double sr, float sens)
    {
        constexpr int order = 13, N = 1 << order;
        const int hop = 1024;
        const int frames = (int) ((x.size() > (size_t) N ? x.size() - N : 0) / (size_t) hop) + 1;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2), win ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (N - 1));
        const int lo = 36, hi = 96;
        std::vector<std::vector<float>> active ((size_t) frames, std::vector<float> (128, 0.0f));
        float globalMax = 1e-9f;
        std::vector<float> mag ((size_t) N / 2 + 1);
        for (int fr = 0; fr < frames; ++fr)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int j = 0; j < N; ++j) { const size_t idx = (size_t) fr * (size_t) hop + (size_t) j; buf[(size_t) j] = idx < x.size() ? x[idx] * win[(size_t) j] : 0.0f; }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            float frameMax = 0;
            for (int k = 0; k <= N / 2; ++k) { mag[(size_t) k] = buf[(size_t) k]; frameMax = juce::jmax (frameMax, mag[(size_t) k]); }
            globalMax = juce::jmax (globalMax, frameMax);
            auto peakAt = [&] (double hz)
            {
                const double b = hz * N / sr; const int a = (int) std::floor (b * 0.985), z = (int) std::ceil (b * 1.015) + 1;
                float m = 0; for (int k = juce::jmax (1, a); k <= juce::jmin (N / 2, z); ++k) m = juce::jmax (m, mag[(size_t) k]); return m;
            };
            auto removeNote = [&] (int m)
            {
                for (int h = 1; h <= 10; ++h)
                {
                    const double b = juce::MidiMessage::getMidiNoteInHertz (m) * h * N / sr; const int a = (int) std::floor (b * 0.98), z = (int) std::ceil (b * 1.02) + 1;
                    for (int k = juce::jmax (1, a); k <= juce::jmin (N / 2, z); ++k) mag[(size_t) k] *= 0.15f;
                }
            };
            float firstS = 0;
            for (int pick = 0; pick < 6; ++pick)
            {
                int bestM = -1; float bestS = 0;
                for (int m = lo; m <= hi; ++m)
                {
                    const double f = juce::MidiMessage::getMidiNoteInHertz (m);
                    if (f * 1.02 > sr / 2) break;
                    const float f0 = peakAt (f);
                    if (f0 < frameMax * 0.04f) continue;            // the fundamental must actually be there
                    float s = 0, w = 1.0f;
                    for (int h = 1; h <= 8 && f * h < sr / 2; ++h) { s += w * peakAt (f * h); w *= 0.8f; }
                    if (s > bestS) { bestS = s; bestM = m; }
                }
                if (bestM < 0) break;
                if (pick == 0) firstS = bestS;
                else if (bestS < firstS * (0.32f - sens * 0.12f)) break;
                active[(size_t) fr][(size_t) bestM] = bestS;
                removeNote (bestM);
            }
        }
        // track notes over time: on after 2 frames, off after 2 frames of absence
        std::vector<TranscribedNote> notes;
        const double frameSec = hop / sr, offset = (N * 0.5) / sr - frameSec;
        const float floor = globalMax * (0.02f + (1.0f - sens) * 0.05f);
        for (int m = lo; m <= hi; ++m)
        {
            int start = -1, gap = 0; float peak = 0;
            for (int fr = 0; fr <= frames; ++fr)
            {
                const bool on = fr < frames && active[(size_t) fr][(size_t) m] > floor;
                if (on) { if (start < 0) start = fr; gap = 0; peak = juce::jmax (peak, active[(size_t) fr][(size_t) m]); continue; }
                if (start >= 0 && ++gap >= 2)
                {
                    const int end = fr - gap + 1;
                    if ((end - start) * frameSec >= 0.08)
                    {
                        TranscribedNote n; n.pitch = m; n.start = juce::jmax (0.0, start * frameSec + offset); n.length = (end - start) * frameSec;
                        n.velocity = juce::jlimit (30, 127, juce::roundToInt (50 + 77 * std::sqrt (peak / (globalMax * 4))));
                        notes.push_back (n);
                    }
                    start = -1; gap = 0; peak = 0;
                }
            }
        }
        std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.start < b.start; });
        return notes;
    }

    std::vector<TranscribedNote> drums (const std::vector<float>& x, double sr, float sens)
    {
        constexpr int order = 10, N = 1 << order;
        const int hop = 256;
        const int frames = (int) ((x.size() > (size_t) N ? x.size() - N : 0) / (size_t) hop);
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);
        std::vector<float> flux ((size_t) frames, 0.0f), lowF ((size_t) frames), midF ((size_t) frames), highF ((size_t) frames);
        std::vector<float> lowE ((size_t) frames), midE ((size_t) frames), highE ((size_t) frames);   // band energy, for telling drums apart
        auto bin = [&] (double hz) { return juce::jlimit (1, N / 2, (int) std::ceil (hz * N / sr)); };
        const int b1 = bin (170), b2 = bin (2000), b3 = bin (5000), b4 = bin (16000);   // low band includes a kick's upper sweep
        for (int fr = 0; fr < frames; ++fr)
        {
            for (int j = 0; j < N; ++j) buf[(size_t) j] = x[(size_t) fr * (size_t) hop + (size_t) j] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * j / (N - 1)));
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            float l = 0, m = 0, h = 0, el = 0, em = 0, eh = 0;
            for (int k = 1; k <= N / 2; ++k)
            {
                const float mg = buf[(size_t) k], d = juce::jmax (0.0f, mg - prev[(size_t) k]); prev[(size_t) k] = mg;
                if (k < b1) { l += d; el += mg * mg; } else if (k < b2) { m += d; em += mg * mg; } else if (k >= b3 && k < b4) { h += d; eh += mg * mg; }
            }
            lowE[(size_t) fr] = el; midE[(size_t) fr] = em; highE[(size_t) fr] = eh;
            lowF[(size_t) fr] = l; midF[(size_t) fr] = m; highF[(size_t) fr] = h; flux[(size_t) fr] = l + m + h;
        }
        float maxFlux = 1e-9f; for (auto f : flux) maxFlux = juce::jmax (maxFlux, f);
        std::vector<TranscribedNote> notes;
        const double frameSec = hop / sr;
        const int minGap = juce::jmax (2, (int) (0.05 / frameSec));
        int last = -minGap;
        auto fl = [&] (int i) { return i >= 0 && i < frames ? flux[(size_t) i] : 0.0f; };
        for (int i = 0; i + 1 < frames; ++i)
        {
            float localMean = 0; int cnt = 0;
            for (int k = juce::jmax (0, i - 20); k <= juce::jmin (frames - 1, i + 20); ++k) { localMean += flux[(size_t) k]; ++cnt; }
            localMean /= (float) cnt;
            const bool peak = fl (i) >= fl (i - 1) && fl (i) > fl (i + 1) && fl (i) >= fl (i - 2);
            if (! peak || flux[(size_t) i] < localMean * 1.5f + maxFlux * (0.02f + (1.0f - sens) * 0.08f) || i - last < minGap) continue;
            last = i;
            // classify by where the energy sits over the next ~40 ms, not just the first instant (a kick's click is bright)
            float l = 0, m = 0, h = 0;
            const int span = juce::jmax (4, (int) (0.04 / frameSec));
            for (int k = i; k < juce::jmin (frames, i + span); ++k) { l += lowE[(size_t) k]; m += midE[(size_t) k]; h += highE[(size_t) k]; }
            const float tot = l + m + h + 1e-12f;
            TranscribedNote n; n.start = i * frameSec + (N * 0.25) / sr; n.length = 0.08;
            n.velocity = juce::jlimit (30, 127, juce::roundToInt (40 + 87 * std::sqrt (flux[(size_t) i] / maxFlux)));
            if (l / tot > 0.5f) n.pitch = 36;                       // kick: mostly low end
            else if (m / tot < 0.12f && h / tot > 0.6f) n.pitch = 42;   // hi-hat: bright with almost no body
            else n.pitch = 38;                                      // snare: body plus noise
            notes.push_back (n);
        }
        return notes;
    }
}

std::vector<ContourLine> transcribeContour (const juce::File& f, float sens)
{
    std::vector<float> x; double sr = 44100;
    if (! loadMono (f, x, sr)) return {};
    auto pf = pitchFrames (x, sr, juce::jlimit (0.0f, 1.0f, sens));
    const int frames = (int) pf.med.size();
    const double frameSec = pf.hop / sr, offset = (2048 * 0.25) / sr;
    std::vector<ContourLine> out;
    int start = -1, quiet = 0;
    auto finish = [&] (int end)
    {
        if (start >= 0 && (end - start) * frameSec >= 0.06)
        {
            // simplify the pitch curve: keep only the points needed to stay within a fifth of a semitone
            std::vector<int> keep { start, end - 1 };
            std::function<void (int, int)> rdp = [&] (int a, int b)
            {
                float worst = 0; int at = -1;
                for (int i = a + 1; i < b; ++i)
                {
                    const float lin = pf.med[(size_t) a] + (pf.med[(size_t) b] - pf.med[(size_t) a]) * (float) (i - a) / (float) (b - a);
                    const float d = std::abs (pf.med[(size_t) i] - lin);
                    if (d > worst) { worst = d; at = i; }
                }
                if (at >= 0 && worst > 0.2f) { keep.push_back (at); rdp (a, at); rdp (at, b); }
            };
            rdp (start, end - 1);
            std::sort (keep.begin(), keep.end());
            ContourLine line; float peak = 0;
            for (int i = start; i < end; ++i) peak = juce::jmax (peak, pf.rms[(size_t) i]);
            line.velocity = juce::jlimit (30, 127, juce::roundToInt (40 + 87 * std::sqrt (peak / pf.maxRms)));
            for (int i : keep)
            {
                float level = 0; int n = 0;   // loudness around the point becomes pressure
                for (int k = juce::jmax (start, i - 3); k <= juce::jmin (end - 1, i + 3); ++k) { level += pf.rms[(size_t) k]; ++n; }
                line.points.push_back ({ i * frameSec + offset, pf.med[(size_t) i], juce::jlimit (0.05f, 1.0f, std::sqrt (level / n / peak)) });
            }
            if (line.points.size() == 1) { auto p = line.points.front(); p.time += frameSec; line.points.push_back (p); }
            out.push_back (std::move (line));
        }
        start = -1;
    };
    for (int i = 0; i < frames; ++i)
    {
        const bool voiced = pf.med[(size_t) i] > 0 && pf.rms[(size_t) i] > pf.gate;
        const bool attack = i >= 3 && pf.rms[(size_t) i] > pf.gate * 2 && pf.rms[(size_t) i] > pf.rms[(size_t) (i - 3)] * 1.8f;
        if (! voiced) { if (start >= 0 && ++quiet >= 4) finish (i - quiet + 1); continue; }
        quiet = 0;
        if (start >= 0 && attack && (i - start) * frameSec > 0.08) finish (i);
        if (start >= 0 && std::abs (pf.med[(size_t) i] - pf.med[(size_t) juce::jmax (start, i - 1)]) > 4.0f) finish (i);   // a leap, not a slide
        if (start < 0) start = i;
    }
    finish (frames);
    return out;
}

std::vector<TranscribedNote> transcribe (const juce::File& f, TranscribeMode mode, float sensitivity)
{
    std::vector<float> x; double sr = 44100;
    if (! loadMono (f, x, sr)) return {};
    sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity);
    switch (mode)
    {
        case TranscribeMode::chords: return chords (x, sr, sensitivity);
        case TranscribeMode::drums:  return drums (x, sr, sensitivity);
        default:                     return melody (x, sr, sensitivity);
    }
}
}
