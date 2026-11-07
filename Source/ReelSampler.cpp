#include "ReelSampler.h"
#include "Dsp.h"

namespace Reel
{
using namespace juce;

namespace
{
    struct SamplerVoice : public MPESynthesiserVoice
    {
        explicit SamplerVoice (ReelSampler& o) : owner (o) {}

        void noteStarted() override
        {
            snd = owner.currentSound();
            if (snd == nullptr || snd->data.getNumSamples() < 4) { clearCurrentNote(); return; }
            const double len = snd->data.getNumSamples() - 2;
            mode = owner.oneShot.get();
            if (mode == ReelSampler::slice)
            {
                auto sl = owner.currentSlices();
                const int idx = getCurrentlyPlayingNote().initialNote - ReelSampler::firstSliceNote;
                if (sl == nullptr || idx < 0 || idx >= (int) sl->size()) { clearCurrentNote(); snd.reset(); return; }
                regionStart = (*sl)[(size_t) idx] * len;
                regionEnd = (idx + 1 < (int) sl->size() ? (*sl)[(size_t) idx + 1] : 1.0) * len;
                owner.lastSlicePlayed.store (idx);
            }
            else { regionStart = jlimit (0.0, len - 2, (double) owner.startAt->getCurrentValue() * len); regionEnd = len; }
            backwards = owner.reverse.get() == 1;
            pos = backwards ? regionEnd - 1 : regionStart;
            env.setSampleRate (getSampleRate());
            env.setParameters ({ jmax (0.001f, owner.attack->getCurrentValue()), jmax (0.001f, owner.decay->getCurrentValue()),
                                 owner.sustain->getCurrentValue(), jmax (0.005f, owner.release->getCurrentValue()) });
            env.noteOn();
            vel = getCurrentlyPlayingNote().noteOnVelocity.asUnsignedFloat();
            for (auto* s : { ic1, ic2 }) s[0] = s[1] = 0;
            lfoPhase = 0;
        }

        void noteStopped (bool allowTailOff) override
        {
            if (! allowTailOff) { env.reset(); clearCurrentNote(); snd.reset(); return; }
            if (mode == ReelSampler::classic) env.noteOff();   // One Shot and Slice play to the end
        }

        void notePressureChanged() override {}
        void notePitchbendChanged() override {}
        void noteTimbreChanged() override {}
        void noteKeyStateChanged() override {}

        void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
        {
            if (! isActive() || snd == nullptr) return;
            const auto& n = getCurrentlyPlayingNote();
            const double sr = getSampleRate();
            // pitch: Classic and One Shot follow the key; slices keep their own pitch
            double semis = owner.transpose->getCurrentValue();
            if (mode == ReelSampler::slice) semis += owner.fine->getCurrentValue() / 100.0 + n.totalPitchbendInSemitones;
            else semis += 12.0 * std::log2 (n.getFrequencyInHertz() / MidiMessage::getMidiNoteInHertz (owner.root.get())) - owner.fine->getCurrentValue() / 100.0;
            // modulation
            const float depth = owner.lfoDepth->getCurrentValue();
            const float lfo = (float) std::sin (lfoPhase);
            lfoPhase += MathConstants<double>::twoPi * owner.lfoRate->getCurrentValue() * num / sr;
            const int target = owner.lfoTarget.get();
            if (target == 0) semis += lfo * depth * 2.0;
            const double ratio = std::pow (2.0, semis / 12.0) * snd->rate / sr * (backwards ? -1.0 : 1.0);
            const float pAmt = owner.pressureAmt->getCurrentValue(), sAmt = owner.slideAmt->getCurrentValue();
            float gain = Decibels::decibelsToGain (owner.level->getCurrentValue()) * (0.25f + 0.75f * vel)
                         * jlimit (0.0f, 1.5f, (1.0f - pAmt) + pAmt * n.pressure.asUnsignedFloat() * 1.3f);
            if (target == 2) gain *= 1.0f - depth * 0.5f * (1.0f + lfo);
            float fc = owner.cutoff->getCurrentValue() * std::pow (2.0f, (n.timbre.asUnsignedFloat() - 0.5f) * sAmt * 6.0f);
            if (target == 1) fc *= std::pow (2.0f, lfo * depth * 4.0f);
            fc = jlimit (20.0f, (float) (sr * 0.45), fc);
            const int ftype = owner.filterType.get();
            const bool filtering = ftype != 0 && ! (ftype == 1 && fc > 19000.0f);
            const float g = std::tan (MathConstants<float>::pi * fc / (float) sr), k = 2.0f - 1.9f * owner.resonance->getCurrentValue();
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            float p = (owner.pan->getCurrentValue() + 1.0f) * 0.5f;
            if (target == 3) p = jlimit (0.0f, 1.0f, p + lfo * depth * 0.5f);
            const float panG[2] = { std::cos (p * MathConstants<float>::halfPi) * 1.4142f, std::sin (p * MathConstants<float>::halfPi) * 1.4142f };
            const int chans = snd->data.getNumChannels();
            const bool looping = mode == ReelSampler::classic && owner.loopMode.get() == 1 && ! backwards;
            const double len = snd->data.getNumSamples() - 2;
            const double ls = jlimit (0.0, len - 2, (double) owner.loopStart->getCurrentValue() * len);
            const double le = jlimit (ls + 16.0, len, (double) owner.loopEnd->getCurrentValue() * len);
            const double fin = jmax (1.0, owner.fadeIn->getCurrentValue() * 0.001 * snd->rate), fout = jmax (1.0, owner.fadeOut->getCurrentValue() * 0.001 * snd->rate);
            for (int i = 0; i < num; ++i)
            {
                if (looping && pos >= le) pos = ls + std::fmod (pos - ls, le - ls);
                if ((! backwards && pos >= regionEnd) || (backwards && pos <= regionStart) || pos < 0 || pos >= len)
                { env.reset(); clearCurrentNote(); snd.reset(); return; }
                const float e = env.getNextSample();
                if (! env.isActive()) { clearCurrentNote(); snd.reset(); return; }
                const double fromStart = backwards ? regionEnd - pos : pos - regionStart, toEnd = backwards ? pos - regionStart : regionEnd - pos;
                const float fade = (float) (jmin (1.0, fromStart / fin) * (looping ? 1.0 : jmin (1.0, toEnd / fout)));
                const int kk = (int) pos; const float fr = (float) (pos - kk);
                for (int c = 0; c < jmin (2, out.getNumChannels()); ++c)
                {
                    const float* d = snd->data.getReadPointer (jmin (c, chans - 1));
                    float s = d[kk] + (d[kk + 1] - d[kk]) * fr;
                    if (filtering)
                    {
                        const float v3 = s - ic2[c], v1 = a1 * ic1[c] + a2 * v3, v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                        ic1[c] = 2.0f * v1 - ic1[c]; ic2[c] = 2.0f * v2 - ic2[c];
                        s = ftype == 2 ? s - k * v1 - v2 : ftype == 3 ? v1 : v2;
                    }
                    out.addSample (c, start + i, s * e * fade * gain * panG[c]);
                }
                pos += ratio;
            }
        }

        ReelSampler& owner;
        std::shared_ptr<const ReelSampler::Sound> snd;
        double pos = 0, regionStart = 0, regionEnd = 0, lfoPhase = 0;
        float vel = 1; int mode = 0; bool backwards = false;
        ADSR env; float ic1[2] {}, ic2[2] {};
    };

    String shortTime (float v) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }
    String millis (float v)    { return String (roundToInt (v)) + " ms"; }
}

static const Identifier fileID ("file"), nameID ("soundName"), rootID ("root"), mpeID ("mpe"), slicesID ("slices");

ReelSampler::ReelSampler (te::PluginCreationInfo info) : ReelEffect (info)
{
    auto* um = getUndoManager();
    file.referTo (state, fileID, um); soundName.referTo (state, nameID, um);
    root.referTo (state, rootID, um, 60); mpe.referTo (state, mpeID, um, false);
    level = addP ("level", "Gain", { -30.0f, 6.0f, 0.1f }, 0.0f, Fmt::db);
    transpose = addP ("transpose", "Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, Fmt::st);
    pan = addP ("pan", "Pan", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v) * 100)); });
    fadeIn = addP ("fadeIn", "Fade in", { 0.0f, 500.0f, 1.0f }, 0.0f, millis);
    fadeOut = addP ("fadeOut", "Fade out", { 0.0f, 500.0f, 1.0f }, 3.0f, millis);
    fine = addP ("fine", "Fine tune", { -100.0f, 100.0f, 1.0f }, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " ct"; });
    attack = addP ("attack", "Attack", Fmt::skewed (0.001f, 4.0f, 0.1f), 0.003f, shortTime);
    decay = addP ("decay", "Decay", Fmt::skewed (0.001f, 8.0f, 0.5f), 0.5f, shortTime);
    sustain = addP ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    release = addP ("release", "Release", Fmt::skewed (0.005f, 8.0f, 0.4f), 0.15f, shortTime);
    cutoff = addP ("cutoff", "Cutoff", Fmt::skewed (20.0f, 20000.0f, 1500.0f), 20000.0f, Fmt::hz);
    resonance = addP ("resonance", "Resonance", { 0.0f, 1.0f }, 0.1f, Fmt::pct);
    pressureAmt = addP ("pressureAmt", "Pressure to gain", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    slideAmt = addP ("slideAmt", "Slide to cutoff", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    startAt = addP ("start", "Start", { 0.0f, 0.9f }, 0.0f, Fmt::pct);
    loopStart = addP ("loopStart", "Loop start", { 0.0f, 0.95f }, 0.3f, Fmt::pct);
    loopEnd = addP ("loopEnd", "Loop end", { 0.05f, 1.0f }, 0.9f, Fmt::pct);
    sensitivity = addP ("sensitivity", "Sensitivity", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    lfoRate = addP ("lfoRate", "LFO rate", Fmt::skewed (0.05f, 20.0f, 2.0f), 4.0f, Fmt::hz);
    lfoDepth = addP ("lfoDepth", "LFO amount", { 0.0f, 1.0f }, 0.0f, Fmt::pct);
    addChoice (oneShot, "oneShot", "Mode", modeNames(), 0);
    addChoice (loopMode, "loop", "Loop", { "Off", "On" }, 0);
    addChoice (sliceMethod, "sliceMethod", "Slice by", methodNames(), 0);
    addChoice (sliceCount, "sliceCount", "Slices", { "4", "8", "16", "32" }, 2);
    addChoice (filterType, "filterType", "Filter", filterNames(), 1);
    addChoice (lfoTarget, "lfoTarget", "LFO to", lfoNames(), 0);
    addChoice (reverse, "reverse", "Reverse", { "Off", "On" }, 0);
    for (int i = 0; i < 16; ++i) synth.addVoice (new SamplerVoice (*this));
    synth.setVoiceStealingEnabled (true);
    applyZones();
    state.addListener (&watcher);
    handleAsyncUpdate();
}

ReelSampler::~ReelSampler() { cancelPendingUpdate(); state.removeListener (&watcher); }

void ReelSampler::Watcher::valueTreePropertyChanged (ValueTree& v, const Identifier& id)
{
    if (v == owner.state && (id == fileID || id == mpeID || id == slicesID)) owner.triggerAsyncUpdate();
}

void ReelSampler::applyZones()
{
    if (mpe.get()) { MPEZoneLayout zones; zones.setLowerZone (15); synth.setZoneLayout (zones); }
    else synth.enableLegacyMode (2);
    zonesAreMpe = mpe.get();
}

void ReelSampler::setMpe (bool on) { mpe = on; }

void ReelSampler::handleAsyncUpdate()
{
    if (zonesAreMpe != mpe.get()) applyZones();
    auto sl = std::make_shared<std::vector<double>> (getSlices());
    { const SpinLock::ScopedLockType l (soundLock); slices = sl; }
    const String path = file.get();
    if (path == loadedPath) return;
    loadedPath = path;
    std::shared_ptr<Sound> s;
    if (File::isAbsolutePath (path) && File (path).existsAsFile())
    {
        AudioFormatManager fm; fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (path))); r != nullptr && r->lengthInSamples > 1)
        {
            s = std::make_shared<Sound>();
            const int n = (int) jmin (r->lengthInSamples, (int64) (r->sampleRate * 120));
            s->data.setSize ((int) jlimit (1u, 2u, r->numChannels), n + 2);
            s->data.clear();
            r->read (&s->data, 0, n, 0, true, true);
            s->rate = r->sampleRate;
        }
    }
    const SpinLock::ScopedLockType l (soundLock);
    sound = std::move (s);
}

std::shared_ptr<const ReelSampler::Sound> ReelSampler::currentSound() const
{
    const SpinLock::ScopedTryLockType l (soundLock);
    return l.isLocked() ? sound : nullptr;
}

std::shared_ptr<const std::vector<double>> ReelSampler::currentSlices() const
{
    const SpinLock::ScopedTryLockType l (soundLock);
    return l.isLocked() ? slices : nullptr;
}

bool ReelSampler::hasSound() const { const SpinLock::ScopedLockType l (soundLock); return sound != nullptr; }

double ReelSampler::soundSeconds() const
{
    const SpinLock::ScopedLockType l (soundLock);
    return sound ? sound->data.getNumSamples() / sound->rate : 0.0;
}

std::vector<double> ReelSampler::getSlices() const
{
    std::vector<double> v { 0.0 };
    for (auto& t : StringArray::fromTokens (state.getProperty (slicesID).toString(), " ", {}))
        if (const double x = t.getDoubleValue(); x > 0.0 && x < 1.0) v.push_back (x);
    std::sort (v.begin(), v.end());
    v.erase (std::unique (v.begin(), v.end(), [] (double a, double b) { return b - a < 1e-6; }), v.end());
    if ((int) v.size() > maxSlices) v.resize ((size_t) maxSlices);
    return v;
}

void ReelSampler::setSlices (std::vector<double> v, UndoManager* um)
{
    StringArray s;
    std::sort (v.begin(), v.end());
    for (double x : v) if (x > 1e-6 && x < 1.0 && s.size() < maxSlices - 1) s.add (String (x, 6));
    state.setProperty (slicesID, s.joinIntoString (" "), um);
    handleAsyncUpdate();
}

int ReelSampler::autoSlice (UndoManager* um)
{
    std::shared_ptr<const Sound> snd;
    { const SpinLock::ScopedLockType l (soundLock); snd = sound; }
    if (snd == nullptr) return 0;
    std::vector<double> v;
    const double secs = snd->data.getNumSamples() / snd->rate;
    if (sliceMethod.get() == 0)
        for (double t : findTransients (snd->data, snd->rate, sensitivity->getCurrentValue())) v.push_back (t / secs);
    else if (sliceMethod.get() == 1)
    {
        const int n = std::array<int, 4> { 4, 8, 16, 32 }[(size_t) jlimit (0, 3, sliceCount.get())];
        for (int i = 0; i < n; ++i) v.push_back ((double) i / n);
    }
    else v = getSlices();
    setSlices (v, um);
    return (int) getSlices().size();
}

bool ReelSampler::loadFile (const File& f, UndoManager* um)
{
    if (! f.existsAsFile()) return false;
    state.setProperty (fileID, f.getFullPathName(), um);
    state.setProperty (nameID, f.getFileNameWithoutExtension(), um);
    handleAsyncUpdate();
    return hasSound();
}

void ReelSampler::restorePluginStateFromValueTree (const ValueTree& v)
{
    ReelEffect::restorePluginStateFromValueTree (v);
    for (auto id : { fileID, nameID, rootID, mpeID, slicesID }) if (v.hasProperty (id)) state.setProperty (id, v.getProperty (id), nullptr);
}

void ReelSampler::initialise (const te::PluginInitialisationInfo& info)
{
    sampleRate = info.sampleRate;
    synth.setCurrentPlaybackSampleRate (info.sampleRate);
}

void ReelSampler::reset() { synth.turnOffAllVoices (false); }

void ReelSampler::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    auto& out = *fc.destBuffer;
    for (int c = 0; c < out.getNumChannels(); ++c) out.clear (c, fc.bufferStartSample, fc.bufferNumSamples);
    midiScratch.clear();
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff) synth.turnOffAllVoices (true);
        for (auto& m : *fc.bufferForMidiMessages)
            midiScratch.addEvent (m, fc.bufferStartSample + jlimit (0, jmax (0, fc.bufferNumSamples - 1), roundToInt (m.getTimeStamp() * sampleRate)));
    }
    synth.renderNextBlock (out, midiScratch, fc.bufferStartSample, fc.bufferNumSamples);
}
}
