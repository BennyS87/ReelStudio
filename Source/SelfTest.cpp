#include "Session.h"
#include <iostream>

namespace Reel
{
namespace
{
    struct Stats { double rms = 0, peak = 0, secs = 0, firstRms = 0, lastRms = 0; bool finite = true; };

    Stats analyse (const juce::File& f)
    {
        Stats s; juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) return s;
        const int n = (int) r->lengthInSamples; s.secs = n / r->sampleRate;
        juce::AudioBuffer<float> b ((int) r->numChannels, n); r->read (&b, 0, n, 0, true, true);
        double sum = 0; const int edge = (int) juce::jmin ((double) n / 4, r->sampleRate * 0.4); double a = 0, z = 0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v)) s.finite = false;
                sum += v * v; s.peak = juce::jmax (s.peak, (double) std::abs (v));
                if (i < edge) a += v * v; if (i >= n - edge) z += v * v;
            }
        s.rms = std::sqrt (sum / juce::jmax (1, n * b.getNumChannels()));
        s.firstRms = std::sqrt (a / juce::jmax (1, edge * b.getNumChannels())); s.lastRms = std::sqrt (z / juce::jmax (1, edge * b.getNumChannels()));
        return s;
    }

    void writeTone (const juce::File& f)
    {
        const double sr = 44100; const int n = (int) (sr * 3.0);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr; double v = 0;
            for (int k = 1; k < 8; ++k) v += 0.5 / k * std::sin (juce::MathConstants<double>::twoPi * 220.0 * k * t);
            v *= juce::jmin (1.0, t * 20.0) * std::exp (-t * 0.3) * 0.5;
            b.setSample (0, i, (float) v); b.setSample (1, i, (float) (v * 0.9));
        }
        writeWav (f, b, sr);
    }

    void waitForProxies (Session& s, double maxSecs = 90)
    {
        const auto end = juce::Time::getMillisecondCounterHiRes() + maxSecs * 1000.0;
        for (;;)
        {
            bool busy = false;
            for (auto* t : s.tracks())
                for (auto* c : t->getClips())
                    if (auto* w = dynamic_cast<te::WaveAudioClip*> (c))
                    {
                        te::AudioFile pf (w->getPlaybackFile());
                        if (! pf.isValid() || s.engine.getAudioFileManager().proxyGenerator.isProxyBeingGenerated (pf)) busy = true;
                    }
            if (! busy || juce::Time::getMillisecondCounterHiRes() > end) return;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        }
    }

    bool renderTrack (Session& s, te::AudioTrack& t, const juce::File& out, te::TimeRange range)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);   // let samplers finish loading their sounds
        waitForProxies (s);
        juce::BigInteger bits; bits.setBit (te::getAllTracks (*s.edit).indexOf (&t));
        out.deleteFile();
        return te::Renderer::renderToFile ("Test", out, *s.edit, range, bits, true, false, {}, false);
    }
}

int runSelfTest (te::Engine& engine, const juce::File& dir)
{
    dir.createDirectory();
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl; if (! ok) ++fails; };

    Session s (engine);
    s.onMessage = [] (const juce::String& m) { std::cout << "      (message) " << m << std::endl; };
    auto tone = dir.getChildFile ("tone.wav");
    writeTone (tone);
    juce::MemoryBlock toneBytes; tone.loadFileAsData (toneBytes);

    // ---- audio import, speed, pitch, tape, reverse ----
    auto* clip = s.importAudio (tone);
    check (clip != nullptr, "import audio file");
    if (clip == nullptr) return 1;
    auto* audioTrack = dynamic_cast<te::AudioTrack*> (clip->getTrack());
    auto clipID = clip->itemID;
    auto getClip = [&] { return dynamic_cast<te::WaveAudioClip*> (te::findClipForID (*s.edit, clipID)); };

    s.setSpeedPitch (*getClip(), 1.5, 3.0f, true);
    check (std::abs (getClip()->getPosition().getLength().inSeconds() - 2.0) < 0.01, "speed 1.5x shortens the clip to 2.0 s (got " + juce::String (getClip()->getPosition().getLength().inSeconds(), 3) + ")");
    auto f1 = dir.getChildFile ("stretch.wav");
    check (renderTrack (s, *audioTrack, f1, getClip()->getEditTimeRange()), "render time-stretched clip");
    auto p1 = detectPitch (f1, 0.3, 1.2);
    check (p1 && std::abs (p1->freq - 261.63) < 4.0, "speed 1.5x with +3 semitones keeps pitch independent: expected 261.6 Hz, got " + juce::String (p1 ? p1->freq : 0.0, 1));

    s.setSpeedPitch (*getClip(), 1.5, 0.0f, false);
    auto f2 = dir.getChildFile ("tape.wav");
    renderTrack (s, *audioTrack, f2, getClip()->getEditTimeRange());
    auto p2 = detectPitch (f2, 0.3, 1.2);
    check (p2 && std::abs (p2->freq - 330.0) < 5.0, "tape mode: 1.5x speed raises pitch to 330 Hz, got " + juce::String (p2 ? p2->freq : 0.0, 1));

    s.setSpeedPitch (*getClip(), 1.0, 0.0f, true);
    s.setReversed (*getClip(), true);
    auto f3 = dir.getChildFile ("reverse.wav");
    renderTrack (s, *audioTrack, f3, getClip()->getEditTimeRange());
    auto st3 = analyse (f3);
    check (st3.rms > 0.01 && st3.firstRms < st3.lastRms, "reverse: sound now swells towards the end (start " + juce::String (st3.firstRms, 3) + ", end " + juce::String (st3.lastRms, 3) + ")");
    s.setReversed (*getClip(), false);

    // ---- split and duplicate ----
    s.select (audioTrack, getClip());
    s.seek (te::TimePosition::fromSeconds (1.0));
    check (s.splitAtPlayhead() && audioTrack->getClips().size() == 2, "split a clip at the playhead");
    s.undoManager().undo();
    check (audioTrack->getClips().size() == 1, "undo the split");

    // ---- synth with a built-in sound ----
    auto* syn = s.addTrack (Kind::synth);
    s.setSynthPreset (*syn, "Bright lead");
    if (auto* mc = s.addMidiClip (*syn, {}, 2))
        for (int i = 0; i < 8; ++i) mc->getSequence().addNote (60 + (i % 4) * 3, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.8), 100, 0, nullptr);
    auto f4 = dir.getChildFile ("synth.wav");
    renderTrack (s, *syn, f4, { te::TimePosition(), te::TimePosition::fromSeconds (4) });
    auto st4 = analyse (f4);
    check (st4.rms > 0.01 && st4.finite, "synth plays notes (rms " + juce::String (st4.rms, 3) + ")");
    auto* syn2 = s.addTrack (Kind::synth);
    for (auto& name : builtInPresetNames()) s.setSynthPreset (*syn2, name);
    check (s.getSynth (*syn2) != nullptr, "all " + juce::String (builtInPresetNames().size()) + " built-in sounds load");
    s.deleteTrack (*syn2);

    // ---- drum machine ----
    auto* dr = s.addTrack (Kind::drums);
    auto* sc = s.addStepClip (*dr, {}, 2);
    check (sc != nullptr && sc->getChannels().size() == numDrumVoices, "drum machine has 10 drums");
    if (sc != nullptr) { auto p = sc->getPattern (0); for (int i = 0; i < 16; i += 4) { p.setNote (0, i, true); p.setNote (1, i + 2, true); p.setNote (3, i + 1, true); } }
    check (sc != nullptr && std::abs (sc->getPosition().getLength().inSeconds() - s.barLength().inSeconds() * 2) < 0.01, "step clip spans 2 bars");
    auto f5 = dir.getChildFile ("drums.wav");
    renderTrack (s, *dr, f5, { te::TimePosition(), s.barLength() * 2 });
    auto st5 = analyse (f5);
    check (st5.rms > 0.01 && st5.finite, "drum pattern plays (rms " + juce::String (st5.rms, 3) + ")");
    s.setKitValue (*dr, IDs::kitTune, 0, 5.0f);
    check (s.getSampler (*dr)->getKeyNote (0) == 31, "tuning the kick moves its key note");

    // ---- effects and automation ----
    for (auto& e : effectSlots()) s.setEffectOn (*audioTrack, e.type, true);
    int on = 0; for (auto& e : effectSlots()) if (s.isEffectOn (*audioTrack, e.type)) ++on;
    check (on == 4, "distortion, vocoder, echo and reverb all switch on");
    auto order = juce::StringArray(); for (auto* p : audioTrack->pluginList.getPlugins()) order.add (p->getPluginType());
    std::cout << "      plugin order: " << order.joinIntoString (" > ") << std::endl;
    if (auto* d = dynamic_cast<te::DelayPlugin*> (s.getEffect (*audioTrack, te::DelayPlugin::xmlTypeName)))
    {
        auto& curve = d->mixProportion->getCurve();
        curve.addPoint (te::TimePosition(), 0.0f, 0.0f); curve.addPoint (te::TimePosition::fromSeconds (1), 0.8f, 0.0f); curve.addPoint (te::TimePosition::fromSeconds (2), 0.0f, 0.0f);
        check (d->mixProportion->hasAutomationPoints(), "echo amount automated at three points");
    }
    check (s.automationTargets (*audioTrack).size() >= 8, "automation lane offers volume, pan and effect settings (" + juce::String (s.automationTargets (*audioTrack).size()) + ")");
    auto f6 = dir.getChildFile ("effects.wav");
    renderTrack (s, *audioTrack, f6, { te::TimePosition(), te::TimePosition::fromSeconds (5) });
    auto st6 = analyse (f6);
    check (st6.rms > 0.01 && st6.finite, "all four effects render cleanly (rms " + juce::String (st6.rms, 3) + ", peak " + juce::String (st6.peak, 2) + ")");
    for (auto& e : effectSlots()) s.setEffectOn (*audioTrack, e.type, juce::String (e.type) == juce::String (VocoderPlugin::xmlTypeName));
    auto f7 = dir.getChildFile ("vocoder.wav");
    renderTrack (s, *audioTrack, f7, getClip()->getEditTimeRange());
    auto st7 = analyse (f7);
    check (st7.rms > 0.01 && st7.finite, "vocoder speaks the audio through its synth (rms " + juce::String (st7.rms, 3) + ")");
    s.setEffectOn (*audioTrack, VocoderPlugin::xmlTypeName, false);

    // ---- sound to instrument ----
    auto* sam = s.makeSamplerFromClip (*getClip());
    check (sam != nullptr && s.getSampler (*sam) != nullptr && s.getSampler (*sam)->getNumSounds() == 1, "make a playable instrument from a clip");
    if (sam != nullptr)
    {
        check (s.getSampler (*sam)->getKeyNote (0) == 57, "found the sound's note: A3 (got " + noteName (s.getSampler (*sam)->getKeyNote (0)) + ")");
        if (auto* mc = s.addMidiClip (*sam, {}, 1)) mc->getSequence().addNote (60, {}, te::BeatDuration::fromBeats (3), 100, 0, nullptr);
        auto f8 = dir.getChildFile ("sampler.wav");
        renderTrack (s, *sam, f8, { te::TimePosition(), te::TimePosition::fromSeconds (1.5) });
        auto p8 = detectPitch (f8, 0.1, 1.0);
        check (p8 && std::abs (p8->freq - 261.63) < 4.0, "playing C4 on that instrument sounds at 261.6 Hz, got " + juce::String (p8 ? p8->freq : 0.0, 1));
    }

    // ---- MIDI file import ----
    {
        juce::MidiFile mf; mf.setTicksPerQuarterNote (480);
        juce::MidiMessageSequence a, b;
        a.addEvent (juce::MidiMessage::tempoMetaEvent (600000));
        a.addEvent (juce::MidiMessage::programChange (1, 33));
        for (int i = 0; i < 4; ++i) { a.addEvent (juce::MidiMessage::noteOn (1, 40 + i, (juce::uint8) 100), i * 480.0); a.addEvent (juce::MidiMessage::noteOff (1, 40 + i), i * 480.0 + 400); }
        for (int i = 0; i < 8; ++i) { b.addEvent (juce::MidiMessage::noteOn (10, i % 2 ? 38 : 36, (juce::uint8) 110), i * 240.0); b.addEvent (juce::MidiMessage::noteOff (10, i % 2 ? 38 : 36), i * 240.0 + 120); }
        a.updateMatchedPairs(); b.updateMatchedPairs();
        mf.addTrack (a); mf.addTrack (b);
        auto mid = dir.getChildFile ("test.mid"); mid.deleteFile();
        { juce::FileOutputStream os (mid); mf.writeTo (os); }
        const int before = s.tracks().size();
        const int parts = s.importMidi (mid);
        check (parts == 2 && s.tracks().size() == before + 2, "import a MIDI file with a bass part and a drum part");
        auto* bassT = s.tracks()[before];
        check (bassT != nullptr && getKind (*bassT) == Kind::synth && bassT->state.getProperty (IDs::preset).toString() == "Round bass", "MIDI program 33 picks the bass sound");
        check (s.tracks()[before + 1] != nullptr && getKind (*s.tracks()[before + 1]) == Kind::drums, "MIDI channel 10 becomes a drum track");
    }

    // ---- save and reopen ----
    auto proj = dir.getChildFile ("test.tracktionedit");
    const int tracksBefore = s.tracks().size();
    check (s.saveProject (proj), "save project");
    check (s.openProject (proj) && s.tracks().size() == tracksBefore, "reopen project with all " + juce::String (tracksBefore) + " tracks");
    bool stillStretched = false;
    for (auto* t : s.tracks()) for (auto* c : t->getClips()) if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) if (Session::keepsPitch (*w)) stillStretched = true;
    check (stillStretched, "clip settings survive saving");

    // ---- demo song and mixdown ----
    s.loadDemo();
    check (s.tracks().size() == 4, "demo song has 4 tracks");
    auto mix = dir.getChildFile ("demo-mixdown.wav");
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    check (s.exportMix (mix), "export mixdown");
    auto sm = analyse (mix);
    check (sm.rms > 0.02 && sm.finite && sm.secs > 25, "demo mixdown has sound (" + juce::String (sm.secs, 1) + " s, rms " + juce::String (sm.rms, 3) + ", peak " + juce::String (sm.peak, 2) + ")");

    juce::MemoryBlock after; tone.loadFileAsData (after);
    check (after == toneBytes && after.getSize() > 0, "the original audio file is byte-for-byte unchanged");
    std::cout << (fails == 0 ? "ALL TESTS PASSED" : juce::String (fails) + " TEST(S) FAILED") << std::endl;
    return fails;
}
}
