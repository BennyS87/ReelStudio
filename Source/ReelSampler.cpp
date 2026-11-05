#include "ReelSampler.h"

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
            if (snd == nullptr || snd->data.getNumSamples() < 2) { clearCurrentNote(); return; }
            const int len = snd->data.getNumSamples();
            pos = jlimit (0.0, len - 2.0, (double) owner.startAt->getCurrentValue() * len);
            env.setSampleRate (getSampleRate());
            env.setParameters ({ jmax (0.001f, owner.attack->getCurrentValue()), jmax (0.001f, owner.decay->getCurrentValue()),
                                 owner.sustain->getCurrentValue(), jmax (0.005f, owner.release->getCurrentValue()) });
            env.noteOn();
            vel = getCurrentlyPlayingNote().noteOnVelocity.asUnsignedFloat();
            lp1[0] = lp1[1] = lp2[0] = lp2[1] = 0;
            oneShot = owner.oneShot.get() == 1;
        }

        void noteStopped (bool allowTailOff) override
        {
            if (! allowTailOff) { env.reset(); clearCurrentNote(); snd.reset(); return; }
            if (! oneShot) env.noteOff();
        }

        void notePressureChanged() override {}
        void notePitchbendChanged() override {}
        void noteTimbreChanged() override {}
        void noteKeyStateChanged() override {}

        void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
        {
            if (! isActive() || snd == nullptr) return;
            const auto& n = getCurrentlyPlayingNote();
            const double rootHz = MidiMessage::getMidiNoteInHertz (owner.root.get()) * std::pow (2.0, owner.fine->getCurrentValue() / 1200.0);
            const double ratio = n.getFrequencyInHertz() / rootHz * snd->rate / getSampleRate();
            const float pAmt = owner.pressureAmt->getCurrentValue(), sAmt = owner.slideAmt->getCurrentValue();
            const float pressureGain = jlimit (0.0f, 1.5f, (1.0f - pAmt) + pAmt * n.pressure.asUnsignedFloat() * 1.3f);
            const float gain = Decibels::decibelsToGain (owner.level->getCurrentValue()) * (0.25f + 0.75f * vel) * pressureGain;
            const float fc = jlimit (40.0f, 20000.0f, owner.cutoff->getCurrentValue() * std::pow (2.0f, (n.timbre.asUnsignedFloat() - 0.5f) * sAmt * 6.0f));
            const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * fc / (float) getSampleRate());
            const bool filtered = fc < 19500.0f;
            const float p = (owner.pan->getCurrentValue() + 1.0f) * 0.5f;
            const float panG[2] = { std::cos (p * MathConstants<float>::halfPi) * 1.4142f, std::sin (p * MathConstants<float>::halfPi) * 1.4142f };
            const int len = snd->data.getNumSamples(), chans = snd->data.getNumChannels();
            const bool looping = owner.loopMode.get() == 1 && ! oneShot;
            const double ls = jlimit (0.0, len - 2.0, (double) owner.loopStart->getCurrentValue() * len);
            const double le = jlimit (ls + 16.0, len - 1.0, (double) owner.loopEnd->getCurrentValue() * len);
            for (int i = 0; i < num; ++i)
            {
                if (looping && pos >= le) pos = ls + std::fmod (pos - ls, le - ls);
                if (pos >= len - 1) { if (oneShot || ! looping) { env.reset(); clearCurrentNote(); snd.reset(); return; } }
                const float e = env.getNextSample();
                if (! env.isActive()) { clearCurrentNote(); snd.reset(); return; }
                const int k = (int) pos; const float fr = (float) (pos - k);
                for (int c = 0; c < jmin (2, out.getNumChannels()); ++c)
                {
                    const float* d = snd->data.getReadPointer (jmin (c, chans - 1));
                    float s = d[k] + (d[k + 1] - d[k]) * fr;
                    if (filtered) { lp1[c] += a * (s - lp1[c]); lp2[c] += a * (lp1[c] - lp2[c]); s = lp2[c]; }
                    out.addSample (c, start + i, s * e * gain * panG[c]);
                }
                pos += ratio;
            }
        }

        ReelSampler& owner;
        std::shared_ptr<const ReelSampler::Sound> snd;
        double pos = 0; float vel = 1; bool oneShot = false;
        ADSR env; float lp1[2] {}, lp2[2] {};
    };
}

static String shortTime (float v) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }

static const Identifier fileID ("file"), nameID ("soundName"), rootID ("root"), mpeID ("mpe");

ReelSampler::ReelSampler (te::PluginCreationInfo info) : ReelEffect (info)
{
    auto* um = getUndoManager();
    file.referTo (state, fileID, um); soundName.referTo (state, nameID, um);
    root.referTo (state, rootID, um, 60); mpe.referTo (state, mpeID, um, false);
    level = addP ("level", "Level", { -30.0f, 6.0f, 0.1f }, 0.0f, Fmt::db);
    pan = addP ("pan", "Pan", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v) * 100)); });
    fine = addP ("fine", "Fine tune", { -100.0f, 100.0f, 1.0f }, 0.0f, [] (float v) { return (v > 0 ? "+" : "") + String (roundToInt (v)) + " ct"; });
    attack = addP ("attack", "Attack", Fmt::skewed (0.001f, 4.0f, 0.1f), 0.003f, shortTime);
    decay = addP ("decay", "Decay", Fmt::skewed (0.001f, 8.0f, 0.5f), 0.5f, shortTime);
    sustain = addP ("sustain", "Sustain", { 0.0f, 1.0f }, 1.0f, Fmt::pct);
    release = addP ("release", "Release", Fmt::skewed (0.005f, 8.0f, 0.4f), 0.15f, shortTime);
    cutoff = addP ("cutoff", "Brightness", Fmt::skewed (200.0f, 20000.0f, 3000.0f), 20000.0f, Fmt::hz);
    pressureAmt = addP ("pressureAmt", "Pressure to level", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    slideAmt = addP ("slideAmt", "Slide to brightness", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    startAt = addP ("start", "Start", { 0.0f, 0.9f }, 0.0f, Fmt::pct);
    loopStart = addP ("loopStart", "Loop start", { 0.0f, 0.95f }, 0.3f, Fmt::pct);
    loopEnd = addP ("loopEnd", "Loop end", { 0.05f, 1.0f }, 0.9f, Fmt::pct);
    addChoice (oneShot, "oneShot", "Play", { "While held", "Whole sound" }, 0);
    addChoice (loopMode, "loop", "Loop", { "Off", "On" }, 0);
    for (int i = 0; i < 16; ++i) synth.addVoice (new SamplerVoice (*this));
    synth.setVoiceStealingEnabled (true);
    applyZones();
    state.addListener (&watcher);
    handleAsyncUpdate();
}

ReelSampler::~ReelSampler()
{
    cancelPendingUpdate();
    state.removeListener (&watcher);
}

void ReelSampler::Watcher::valueTreePropertyChanged (ValueTree& v, const Identifier& id)
{
    if (v == owner.state && (id == fileID || id == mpeID)) owner.triggerAsyncUpdate();
}

void ReelSampler::applyZones()
{
    if (mpe.get()) { MPEZoneLayout zones; zones.setLowerZone (15); synth.setZoneLayout (zones); }   // members bend +-48 semitones
    else synth.enableLegacyMode (2);
    zonesAreMpe = mpe.get();
}

void ReelSampler::setMpe (bool on) { mpe = on; }

void ReelSampler::handleAsyncUpdate()
{
    if (zonesAreMpe != mpe.get()) applyZones();
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
            s->data.setSize ((int) jlimit (1u, 2u, r->numChannels), n + 1);
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

bool ReelSampler::hasSound() const { const SpinLock::ScopedLockType l (soundLock); return sound != nullptr; }

double ReelSampler::soundSeconds() const
{
    const SpinLock::ScopedLockType l (soundLock);
    return sound ? sound->data.getNumSamples() / sound->rate : 0.0;
}

bool ReelSampler::loadFile (const File& f, UndoManager* um)
{
    if (! f.existsAsFile()) return false;
    state.setProperty (fileID, f.getFullPathName(), um);
    state.setProperty (nameID, f.getFileNameWithoutExtension(), um);
    handleAsyncUpdate();   // load now so callers can use it straight away
    return hasSound();
}

void ReelSampler::restorePluginStateFromValueTree (const ValueTree& v)
{
    ReelEffect::restorePluginStateFromValueTree (v);
    for (auto id : { fileID, nameID, rootID, mpeID }) if (v.hasProperty (id)) state.setProperty (id, v.getProperty (id), nullptr);
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
