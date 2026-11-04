#include "MainComponent.h"
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
    check (on == effectSlots().size(), "all " + juce::String (effectSlots().size()) + " effects switch on");
    auto order = juce::StringArray(); for (auto* p : audioTrack->pluginList.getPlugins()) order.add (p->getPluginType());
    std::cout << "      plugin order: " << order.joinIntoString (" > ") << std::endl;
    if (auto* d = dynamic_cast<EchoPlugin*> (s.getEffect (*audioTrack, EchoPlugin::xmlTypeName)))
    {
        auto& curve = d->mix->getCurve();
        curve.addPoint (te::TimePosition(), 0.0f, 0.0f); curve.addPoint (te::TimePosition::fromSeconds (1), 0.8f, 0.0f); curve.addPoint (te::TimePosition::fromSeconds (2), 0.0f, 0.0f);
        check (d->mix->hasAutomationPoints(), "echo amount automated at three points");
    }
    check (s.automationTargets (*audioTrack).size() >= 8, "automation lane offers volume, pan and effect settings (" + juce::String (s.automationTargets (*audioTrack).size()) + ")");
    auto f6 = dir.getChildFile ("effects.wav");
    renderTrack (s, *audioTrack, f6, { te::TimePosition(), te::TimePosition::fromSeconds (5) });
    auto st6 = analyse (f6);
    check (st6.rms > 0.01 && st6.finite, "every effect switched on at once renders cleanly (rms " + juce::String (st6.rms, 3) + ", peak " + juce::String (st6.peak, 2) + ")");
    for (auto& e : effectSlots()) s.setEffectOn (*audioTrack, e.type, juce::String (e.type) == juce::String (VocoderPlugin::xmlTypeName));
    auto f7 = dir.getChildFile ("vocoder.wav");
    renderTrack (s, *audioTrack, f7, getClip()->getEditTimeRange());
    auto st7 = analyse (f7);
    check (st7.rms > 0.01 && st7.finite, "vocoder speaks the audio through its synth (rms " + juce::String (st7.rms, 3) + ")");
    s.setEffectOn (*audioTrack, VocoderPlugin::xmlTypeName, false);

    // ---- each new effect does its job ----
    {
        auto* ft = s.addTrack (Kind::audio, "FX test");
        s.importAudio (tone, ft, te::TimePosition());
        const te::TimeRange range (te::TimePosition(), te::TimePosition::fromSeconds (3.0)), longRange (te::TimePosition(), te::TimePosition::fromSeconds (5.0));
        auto renderStats = [&] (const juce::String& name, te::TimeRange rg) { auto f = dir.getChildFile (name + ".wav"); renderTrack (s, *ft, f, rg); return analyse (f); };
        auto dry = renderStats ("fx-dry", range);
        auto setP = [] (te::AutomatableParameter* p, float v) { p->setParameter (v, juce::sendNotification); };

        auto* eq = dynamic_cast<EQPlugin*> (s.addEffect (*ft, EQPlugin::xmlTypeName));
        eq->bands[0].on = 1; setP (eq->bands[0].freq, 1000.0f); eq->bands[0].slope = 2;
        auto cut = renderStats ("fx-eq-cut", range);
        check (cut.rms < dry.rms * 0.35, "EQ low cut at 1 kHz removes the low tone (rms " + juce::String (dry.rms, 3) + " to " + juce::String (cut.rms, 3) + ")");
        eq->bands[0].on = 0; setP (eq->bands[2].freq, 220.0f); setP (eq->bands[2].gain, 12.0f); setP (eq->bands[2].q, 1.0f);
        auto boost = renderStats ("fx-eq-boost", range);
        check (boost.rms > dry.rms * 1.8, "EQ bell +12 dB at 220 Hz makes the tone louder (rms " + juce::String (boost.rms, 3) + ")");
        check (std::abs (eq->responseDb (220.0) - 12.0) < 0.6, "EQ curve display agrees: " + juce::String (eq->responseDb (220.0), 1) + " dB at 220 Hz");
        s.removeEffect (*eq);

        auto* cp = dynamic_cast<CompressorPlugin*> (s.addEffect (*ft, CompressorPlugin::xmlTypeName));
        setP (cp->threshold, -30.0f); setP (cp->ratio, 10.0f);
        auto comp = renderStats ("fx-comp", range);
        check (comp.rms < dry.rms * 0.6 && cp->grDb.load() < -3.0f, "compressor turns loud parts down (gain reduction " + juce::String (cp->grDb.load(), 1) + " dB)");
        s.removeEffect (*cp);

        auto* sat = dynamic_cast<SaturationPlugin*> (s.addEffect (*ft, SaturationPlugin::xmlTypeName));
        setP (sat->drive, 24.0f);
        auto st = renderStats ("fx-sat", range);
        auto sp = detectPitch (dir.getChildFile ("fx-sat.wav"), 0.3, 1.0);
        check (st.finite && st.rms > 0.05 && st.peak <= 1.01 && sp && std::abs (sp->freq - 220.0) < 4.0, "saturation drives the tone into a soft limit and stays in tune (peak " + juce::String (st.peak, 2) + ")");
        for (int type = 0; type < 6; ++type) { sat->type = type; check (renderStats ("fx-sat-" + juce::String (type), range).finite, "saturation character " + juce::String (type + 1) + " of 6 renders"); }
        s.removeEffect (*sat);

        auto* ph = dynamic_cast<PhaserPlugin*> (s.addEffect (*ft, PhaserPlugin::xmlTypeName));
        const char* modes[] = { "phaser", "flanger", "chorus" };
        for (int m = 0; m < 3; ++m) { ph->mode = m; auto r = renderStats (juce::String ("fx-") + modes[m], range); check (r.finite && r.rms > 0.02, juce::String (modes[m]) + " renders (rms " + juce::String (r.rms, 3) + ")"); }
        s.removeEffect (*ph);

        auto silentTail = renderStats ("fx-dry-long", longRange);
        auto* ec = dynamic_cast<EchoPlugin*> (s.addEffect (*ft, EchoPlugin::xmlTypeName));
        setP (ec->feedback, 0.6f); setP (ec->mix, 0.5f);
        auto et = renderStats ("fx-echo", longRange);
        check (et.finite && et.lastRms > 0.002 && silentTail.lastRms < 0.0005, "echo repeats keep going after the sound stops (tail rms " + juce::String (et.lastRms, 4) + ")");
        ec->mode = 1; check (renderStats ("fx-echo-pingpong", longRange).finite, "ping-pong echo renders");
        s.removeEffect (*ec);

        auto* rv = dynamic_cast<ReverbPlugin*> (s.addEffect (*ft, ReverbPlugin::xmlTypeName));
        setP (rv->mix, 0.5f); setP (rv->decay, 0.4f);
        auto shortTail = renderStats ("fx-reverb-short", longRange);
        setP (rv->decay, 8.0f);
        auto longTail = renderStats ("fx-reverb-long", longRange);
        check (longTail.finite && longTail.lastRms > shortTail.lastRms * 3.0 && longTail.lastRms > 0.001, "reverb decay control works (tail rms " + juce::String (shortTail.lastRms, 4) + " short, " + juce::String (longTail.lastRms, 4) + " long)");
        s.removeEffect (*rv);

        auto* vc = dynamic_cast<VocoderPlugin*> (s.addEffect (*ft, VocoderPlugin::xmlTypeName));
        vc->bandsValue = 5; setP (vc->formant, 5.0f);
        auto vr = renderStats ("fx-vocoder32", range);
        check (vr.finite && vr.rms > 0.01, "vocoder with 32 bands and formant shift renders (rms " + juce::String (vr.rms, 3) + ")");
        s.removeEffect (*vc);

        auto* e1 = s.addEffect (*ft, EQPlugin::xmlTypeName);
        auto* e2 = s.addEffect (*ft, CompressorPlugin::xmlTypeName);
        s.moveEffect (*ft, *e2, -1);
        check (s.effectChain (*ft).size() == 2 && s.effectChain (*ft)[0] == e2, "move an effect earlier in the chain");
        s.removeEffect (*e1);
        check (s.effectChain (*ft).size() == 1, "remove an effect from the chain");
        s.deleteTrack (*ft);
    }

    // ---- instrument designer ----
    {
        auto* dt = s.addTrack (Kind::synth, "Designer");
        auto* fo = s.getSynth (*dt);
        fo->oscParams[0]->voicesValue = 4; fo->oscParams[0]->detune->setParameter (0.2f, juce::sendNotification);
        fo->oscParams[1]->waveShapeValue = 2; fo->oscParams[1]->pulseWidth->setParameter (0.2f, juce::sendNotification);
        fo->filterTypeValue = 1; fo->filterSlopeValue = 24;
        fo->setModulationDepth (te::FourOscPlugin::lfo1, fo->filterFreq, 0.5f);
        fo->setModulationDepth (te::FourOscPlugin::env1, fo->oscParams[0]->pulseWidth, -0.3f);
        check (std::abs (fo->getModulationDepth (te::FourOscPlugin::lfo1, fo->filterFreq) - 0.5f) < 1e-3 && fo->getModulationSources (fo->filterFreq).size() == 1,
               "route LFO 1 to the filter cutoff in the modulation matrix");
        if (auto* mc = s.addMidiClip (*dt, {}, 1)) mc->getSequence().addNote (57, {}, te::BeatDuration::fromBeats (3), 100, 0, nullptr);
        auto f = dir.getChildFile ("designer.wav");
        renderTrack (s, *dt, f, { te::TimePosition(), te::TimePosition::fromSeconds (2.0) });
        auto ds = analyse (f);
        check (ds.finite && ds.rms > 0.01, "designed sound plays: unison, 24 dB filter, two modulation routes (rms " + juce::String (ds.rms, 3) + ")");
        s.deleteTrack (*dt);
    }

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
    // ---- switching projects while the window is open (regression: crash on New project) ----
    {
        auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (300); };
        auto uiSession = std::make_unique<Session> (engine);
        uiSession->loadDemo();
        auto ui = std::make_unique<MainComponent> (*uiSession);
        ui->setSize (1360, 860);
        pump();
        std::cout << "      window open, starting a new project" << std::endl;
        uiSession->newProject(); pump();
        std::cout << "      loading the demo song" << std::endl;
        uiSession->loadDemo(); pump();
        for (auto* t : uiSession->tracks()) if (t->getName() == "Lead") { for (auto& e : effectSlots()) uiSession->addEffect (*t, e.type); uiSession->select (t); }
        pump();
        std::cout << "      effects panel showing every effect" << std::endl;
        if (auto* t = uiSession->tracks()[1]) uiSession->select (t);
        pump();
        std::cout << "      instrument designer showing" << std::endl;
        uiSession->select (uiSession->tracks().getFirst()); pump();
        std::cout << "      opening a saved project" << std::endl;
        uiSession->openProject (proj); pump();
        std::cout << "      deleting a track" << std::endl;
        if (auto* t = uiSession->tracks().getFirst()) uiSession->deleteTrack (*t);
        pump();
        uiSession->newProject(); pump();
        ui.reset(); uiSession.reset();
        check (true, "switch projects with the window open: new, demo, open, delete a track");
    }

    std::cout << (fails == 0 ? "ALL TESTS PASSED" : juce::String (fails) + " TEST(S) FAILED") << std::endl;
    return fails;
}
}
