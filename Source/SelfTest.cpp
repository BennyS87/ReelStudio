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
        // end the render while the tone still plays, so the meter shows the reduction rather than its recovery afterwards
        const te::TimeRange during (te::TimePosition(), te::TimePosition::fromSeconds (2.0));
        cp->setEnabled (false); auto dryDuring = renderStats ("fx-dry-during", during); cp->setEnabled (true);
        auto comp = renderStats ("fx-comp", during);
        check (comp.rms < dryDuring.rms * 0.6 && cp->grDb.load() < -3.0f, "compressor turns loud parts down (gain reduction " + juce::String (cp->grDb.load(), 1) + " dB)");
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

    // ---- audio to MIDI ----
    {
        const double spb = 60.0 / s.getBpm();
        const int tune[] = { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64 };
        auto* mt = s.addTrack (Kind::synth, "Tune");
        s.setSynthPreset (*mt, "Sub sine");
        if (auto* mc = s.addMidiClip (*mt, {}, 3))
            for (int i = 0; i < 10; ++i) mc->getSequence().addNote (tune[i], te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.8), 100, 0, nullptr);
        auto mf = dir.getChildFile ("tune.wav");
        renderTrack (s, *mt, mf, { te::TimePosition(), te::TimePosition::fromSeconds (6.0) });
        auto found = transcribe (mf, TranscribeMode::melody);
        int hits = 0; double worst = 0;
        for (int i = 0; i < 10; ++i)
            for (auto& n : found)
                if (n.pitch == tune[i] && std::abs (n.start - i * spb) < 0.06) { ++hits; worst = juce::jmax (worst, std::abs (n.start - i * spb)); break; }
        check (hits >= 9 && found.size() <= 12, "melody to MIDI: " + juce::String (hits) + " of 10 notes found with the right pitch, timing within "
               + juce::String (juce::roundToInt (worst * 1000)) + " ms (" + juce::String ((int) found.size()) + " notes detected)");

        auto* ct = s.addTrack (Kind::synth, "Chords");
        s.setSynthPreset (*ct, "Organ");
        const int chordNotes[3][3] = { { 60, 64, 67 }, { 65, 69, 72 }, { 67, 71, 74 } };
        if (auto* mc = s.addMidiClip (*ct, {}, 3))
            for (int c = 0; c < 3; ++c) for (int p : chordNotes[c]) mc->getSequence().addNote (p, te::BeatPosition::fromBeats (c * 4), te::BeatDuration::fromBeats (3.8), 100, 0, nullptr);
        auto cf = dir.getChildFile ("chords.wav");
        renderTrack (s, *ct, cf, { te::TimePosition(), te::TimePosition::fromSeconds (6.5) });
        auto cn = transcribe (cf, TranscribeMode::chords);
        int chordsRight = 0, extras = 0;
        for (int c = 0; c < 3; ++c)
        {
            const double t0 = c * 4 * spb + 0.15, t1 = c * 4 * spb + 1.5;
            std::set<int> got; for (auto& n : cn) if (n.start < t1 && n.start + n.length > t0) got.insert (n.pitch);
            bool all = true; for (int p : chordNotes[c]) if (! got.count (p)) all = false;
            if (all) ++chordsRight;
            extras += juce::jmax (0, (int) got.size() - 3);
        }
        check (chordsRight >= 2, "chords to MIDI: " + juce::String (chordsRight) + " of 3 chords found note for note (" + juce::String (extras) + " extra notes)");

        auto* dt2 = s.addTrack (Kind::drums, "Beat check");
        if (auto* sc = s.addStepClip (*dt2, {}, 2))
        {
            auto p = sc->getPattern (0);
            for (int i = 0; i < 16; ++i) for (int ch = 0; ch < numDrumVoices; ++ch) p.setNote (ch, i, false);
            for (int i : { 0, 8 }) p.setNote (0, i, true);
            for (int i : { 4, 12 }) p.setNote (1, i, true);
            for (int i : { 2, 6, 10, 14 }) p.setNote (3, i, true);
        }
        auto df = dir.getChildFile ("beat.wav");
        renderTrack (s, *dt2, df, { te::TimePosition(), s.barLength() * 2 });
        auto dn = transcribe (df, TranscribeMode::drums);
        auto countHits = [&] (int pitch, std::initializer_list<int> steps)
        {
            int ok = 0;
            for (int bar = 0; bar < 2; ++bar) for (int st : steps)
                for (auto& n : dn) if (n.pitch == pitch && std::abs (n.start - (bar * 16 + st) * 0.25 * spb) < 0.04) { ++ok; break; }
            return ok;
        };
        const int k = countHits (36, { 0, 8 }), sn = countHits (38, { 4, 12 }), hh = countHits (42, { 2, 6, 10, 14 });
        check (k >= 3 && sn >= 3 && hh >= 6, "drums to MIDI: kicks " + juce::String (k) + "/4, snares " + juce::String (sn) + "/4, hi-hats " + juce::String (hh) + "/8 found in the right place");

        auto* srcTrack = s.addTrack (Kind::audio, "Tune audio");
        if (auto* wc = s.importAudio (mf, srcTrack, te::TimePosition()))
        {
            const int before = s.tracks().size();
            auto* made = s.convertToMidi (*wc, TranscribeMode::melody);
            int notes = 0;
            if (made) for (auto* c : made->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) notes += mc->getSequence().getNotes().size();
            check (made != nullptr && s.tracks().size() == before + 1 && notes >= 9, "Turn into MIDI notes makes a new synth track with the tune (" + juce::String (notes) + " notes)");
            if (made) s.deleteTrack (*made);
        }
        for (auto* t : { mt, ct, dt2, srcTrack }) s.deleteTrack (*t);
    }

    // ---- Lines sequencer ----
    {
        // the MPE engine on its own: a line holding C4, gliding up to G4, then holding
        LinesPattern pat; pat.lengthBeats = 8;
        LineData line; line.pts = { { 0.0, 60, 0.6f, 0.5f, false }, { 1.0, 60, 0.6f, 0.5f, true }, { 3.0, 67, 0.9f, 0.8f, false }, { 4.0, 67, 0.9f, 0.8f, false } };
        pat.lines.push_back (line);
        LinesEngine eng;
        std::vector<std::pair<double, juce::MidiMessage>> got;
        for (double b = 0; b < 6.0; b += 0.05) eng.process (pat, b, b + 0.05, 0.5, 4, [&] (double beat, const juce::MidiMessage& m) { got.push_back ({ beat, m }); });
        int ons = 0, offs = 0, bendAt2 = -1, bendAt35 = -1, pressures = 0, slides = 0, ch = 0;
        for (auto& [beat, m] : got)
        {
            if (m.isNoteOn()) { ++ons; ch = m.getChannel(); }
            if (m.isNoteOff()) ++offs;
            if (m.isPitchWheel() && beat <= 2.0) bendAt2 = m.getPitchWheelValue();
            if (m.isPitchWheel() && beat <= 3.5) bendAt35 = m.getPitchWheelValue();
            if (m.isChannelPressure()) ++pressures;
            if (m.isController() && m.getControllerNumber() == 74) ++slides;
        }
        const int want2 = 8192 + juce::roundToInt (3.5 / 48.0 * 8192), want35 = 8192 + juce::roundToInt (7.0 / 48.0 * 8192);
        check (ons == 1 && offs == 1 && ch >= 2 && ch <= 16, "Lines plays a line as one MPE note on its own channel (channel " + juce::String (ch) + ")");
        check (std::abs (bendAt2 - want2) < 60 && std::abs (bendAt35 - want35) < 20, "the glide is sent as pitch bend: " + juce::String (bendAt2) + " halfway (want " + juce::String (want2) + "), " + juce::String (bendAt35) + " at the top (want " + juce::String (want35) + ")");
        check (pressures > 3 && slides > 3, "pressure and slide curves are sent as they change (" + juce::String (pressures) + " pressure, " + juce::String (slides) + " slide messages)");

        // the whole device on a synth: hear the glide and check normal clips still play
        auto* lt = s.addTrack (Kind::synth, "Lines test");
        s.setSynthPreset (*lt, "Sub sine");
        auto* lp = s.addLines (*lt);
        check (lp != nullptr && s.getSynth (*lt)->state.getProperty (te::IDs::mpe), "add Lines to a synth track (synth switched to MPE)");
        if (lp != nullptr)
        {
            lp->barsChoice = 1;   // 2 bars
            lp->addLine (line, nullptr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            if (auto* mc = s.addMidiClip (*lt, {}, 2)) mc->getSequence().addNote (84, te::BeatPosition::fromBeats (5), te::BeatDuration::fromBeats (1.5), 100, 0, nullptr);
            auto lf = dir.getChildFile ("lines.wav");
            renderTrack (s, *lt, lf, { te::TimePosition(), te::TimePosition::fromSeconds (3.5) });
            const double spb = 60.0 / s.getBpm();
            auto pStart = detectPitch (lf, 0.05, 0.4 * spb), pTop = detectPitch (lf, 3.1 * spb, 0.8 * spb), pClip = detectPitch (lf, 5.2 * spb, 1.0 * spb);
            check (pStart && std::abs (pStart->freq - 261.6) < 4 && pTop && std::abs (pTop->freq - 392.0) < 5,
                   "the drawn glide plays: " + juce::String (pStart ? pStart->freq : 0.0, 1) + " Hz at the start, " + juce::String (pTop ? pTop->freq : 0.0, 1) + " Hz after gliding up (want 261.6 and 392.0)");
            check (pClip && std::abs (pClip->freq - 1046.5) < 12, "a normal MIDI clip on the same synth still plays through Lines (" + juce::String (pClip ? pClip->freq : 0.0, 1) + " Hz, want 1046.5)");
            const int before = lp->lineTrees().size();
            s.saveProject (dir.getChildFile ("lines.tracktionedit"));
            check (before == 1, "the line is stored in the project");
        }
        s.deleteTrack (*lt);
    }

    // ---- sound to instrument ----
    auto* sam = s.makeSamplerFromClip (*getClip());
    check (sam != nullptr && s.getSampled (*sam) != nullptr && s.getSampled (*sam)->hasSound(), "make a playable instrument from a clip");
    if (sam != nullptr && s.getSampled (*sam) != nullptr)
    {
        check (s.getSampled (*sam)->root.get() == 57, "found the sound's note: A3 (got " + noteName (s.getSampled (*sam)->root.get()) + ")");
        if (auto* mc = s.addMidiClip (*sam, {}, 1)) mc->getSequence().addNote (60, {}, te::BeatDuration::fromBeats (3), 100, 0, nullptr);
        auto f8 = dir.getChildFile ("sampler.wav");
        renderTrack (s, *sam, f8, { te::TimePosition(), te::TimePosition::fromSeconds (1.5) });
        auto p8 = detectPitch (f8, 0.1, 1.0);
        check (p8 && std::abs (p8->freq - 261.63) < 4.0, "playing C4 on that instrument sounds at 261.6 Hz, got " + juce::String (p8 ? p8->freq : 0.0, 1));

        // Lines gliding the recorded sound itself
        for (auto* c : sam->getClips()) c->removeFromParent();
        if (auto* lp = s.addLines (*sam))
        {
            lp->barsChoice = 1;
            LineData g; g.pts = { { 0.0, 60, 0.7f, 0.5f, false }, { 1.0, 60, 0.7f, 0.5f, true }, { 3.0, 67, 0.7f, 0.5f, false }, { 4.0, 67, 0.7f, 0.5f, false } };
            lp->addLine (g, nullptr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto gf = dir.getChildFile ("sampler-lines.wav");
            renderTrack (s, *sam, gf, { te::TimePosition(), te::TimePosition::fromSeconds (2.2) });
            const double spb = 60.0 / s.getBpm();
            auto a = detectPitch (gf, 0.05, 0.4 * spb), b = detectPitch (gf, 3.1 * spb, 0.8 * spb);
            check (s.getSampled (*sam)->mpe.get() && a && std::abs (a->freq - 261.6) < 4 && b && std::abs (b->freq - 392.0) < 5,
                   "Lines glides a recorded sound: " + juce::String (a ? a->freq : 0.0, 1) + " Hz rising to " + juce::String (b ? b->freq : 0.0, 1) + " Hz (want 261.6 and 392.0)");
            s.removeLines (*sam);
        }

        // save it as an instrument and use it on a synth track's notes
        s.deleteInstrument ("Selftest tone");
        check (s.saveInstrument (*sam, "Selftest tone") && s.myInstrumentNames().contains ("Selftest tone")
               && s.instrumentsDir().getChildFile ("Selftest tone").getChildFile ("instrument.xml").existsAsFile(), "save the sound as an instrument (copied into Instruments)");
        auto* st = s.addTrack (Kind::synth, "Uses instrument");
        if (auto* mc = s.addMidiClip (*st, {}, 1)) mc->getSequence().addNote (72, {}, te::BeatDuration::fromBeats (3), 100, 0, nullptr);
        const bool applied = s.applyInstrument (*st, "Selftest tone");
        auto af = dir.getChildFile ("applied.wav");
        renderTrack (s, *st, af, { te::TimePosition(), te::TimePosition::fromSeconds (1.4) });
        auto pa = detectPitch (af, 0.1, 1.0);
        check (applied && getKind (*st) == Kind::sampler && s.getSynth (*st) == nullptr && st->getClips().size() == 1 && pa && std::abs (pa->freq - 523.3) < 7,
               "apply the saved instrument to a synth track: its notes now play the sound (C5 at " + juce::String (pa ? pa->freq : 0.0, 1) + " Hz)");
        check (s.applySynthSound (*st, "Soft keys") && getKind (*st) == Kind::synth && s.getSynth (*st) != nullptr && s.getSampled (*st) == nullptr && st->getClips().size() == 1,
               "switch that track back to a synth sound, keeping its notes");
        auto* it = s.addInstrumentTrack ("Selftest tone");
        check (it != nullptr && s.getSampled (*it) != nullptr && s.getSampled (*it)->root.get() == 57, "Add track, My instrument makes a track with the saved sound and its note");
        s.deleteTrack (*st); if (it) s.deleteTrack (*it);

        // projects from before Reel's own sampler open with the new one
        {
            Session old (engine);
            auto* ot = old.addTrack (Kind::sampler, "Old sampler");
            if (auto* rs = old.getSampled (*ot)) rs->deleteFromParent();
            auto op = old.edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
            if (auto* ts = dynamic_cast<te::SamplerPlugin*> (op.get()))
            {
                ot->pluginList.insertPlugin (op, 0, nullptr);
                ts->addSound (s.getSampled (*sam)->getFile().getFullPathName(), "tone", 0.0, 0.0, 0.0f);
                ts->setSoundParams (0, 57, 0, 127);
            }
            auto of = dir.getChildFile ("old-sampler.tracktionedit");
            old.saveProject (of);
            Session reopened (engine);
            reopened.openProject (of);
            auto* rt = reopened.tracks().getFirst();
            auto* rs = rt ? reopened.getSampled (*rt) : nullptr;
            int oldSounds = -1; if (rt) if (auto* os = reopened.getSampler (*rt)) oldSounds = os->getNumSounds();
            check (rs != nullptr && rs->hasSound() && rs->root.get() == 57 && reopened.getSampler (*rt) == nullptr,
                   "a project made with the old sampler opens with the new one, keeping its sound and note (tracks " + juce::String (reopened.tracks().size())
                   + ", kind " + (rt ? kindToString (getKind (*rt)) : juce::String ("none")) + ", new " + juce::String (rs ? 1 : 0) + ", sound " + juce::String (rs && rs->hasSound() ? 1 : 0)
                   + ", root " + juce::String (rs ? rs->root.get() : -1) + ", old sounds " + juce::String (oldSounds) + ")");
        }
    }

    // ---- arpeggiator ----
    {
        // hold C-E-G for two beats; 1/8 steps at 120 bpm are 0.25 s apart
        auto arpRun = [&] (te::AudioTrack& t, int mode, int oct, const juce::String& file) -> std::vector<double>
        {
            auto* arp = s.addArp (t);
            std::vector<double> got;
            if (arp == nullptr) return got;
            arp->mode = mode; arp->rate = 1; arp->octaves = oct;
            arp->gate->setParameter (0.5f, juce::dontSendNotification);
            for (auto* c : t.getClips()) c->removeFromParent();
            if (auto* mc = s.addMidiClip (t, {}, 1))
                for (int n : { 60, 64, 67 }) mc->getSequence().addNote (n, {}, te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
            auto f = dir.getChildFile (file);
            renderTrack (s, t, f, { te::TimePosition(), te::TimePosition::fromSeconds (1.1) });
            const double stepSec = 0.5 * 60.0 / s.getBpm();
            for (int i = 0; i < 4; ++i) { auto p = detectPitch (f, i * stepSec + 0.015, stepSec * 0.4); got.push_back (p ? p->freq : 0.0); }
            return got;
        };
        auto matches = [] (const std::vector<double>& got, std::initializer_list<double> want)
        {
            int i = 0; for (double w : want) { if (i >= (int) got.size() || std::abs (got[(size_t) i] - w) > w * 0.02) return false; ++i; } return true;
        };
        auto show = [] (const std::vector<double>& got) { juce::StringArray a; for (double g : got) a.add (juce::String (g, 1)); return a.joinIntoString (", "); };

        auto* at = s.addTrack (Kind::synth, "Arp synth");
        s.setSynthPreset (*at, "Sub sine");
        auto up = arpRun (*at, 0, 0, "arp-up.wav");
        check (s.getArp (*at) != nullptr && at->pluginList.indexOf (s.getArp (*at)) < at->pluginList.indexOf (s.getSynth (*at)), "the arpeggiator sits in front of the synth");
        check (matches (up, { 261.6, 329.6, 392.0, 261.6 }), "arpeggiator Up on a synth plays C, E, G, C (" + show (up) + " Hz)");
        auto down = arpRun (*at, 1, 0, "arp-down.wav");
        check (matches (down, { 392.0, 329.6, 261.6, 392.0 }), "Down plays G, E, C, G (" + show (down) + " Hz)");
        auto twoOct = arpRun (*at, 0, 1, "arp-oct.wav");
        check (matches (twoOct, { 261.6, 329.6, 392.0, 523.3 }), "two octaves climbs on to the next C (" + show (twoOct) + " Hz)");
        s.deleteTrack (*at);

        if (auto* it = s.addInstrumentTrack ("Selftest tone"))
        {
            auto sam2 = arpRun (*it, 0, 0, "arp-sampler.wav");
            check (matches (sam2, { 261.6, 329.6, 392.0, 261.6 }), "the arpeggiator plays an instrument made from a recording (" + show (sam2) + " Hz)");
            s.applySynthSound (*it, "Soft keys");
            check (s.getArp (*it) != nullptr && it->pluginList.indexOf (s.getArp (*it)) < it->pluginList.indexOf (s.getSynth (*it)),
                   "switching the instrument keeps the arpeggiator in front of it");
            s.deleteTrack (*it);
        }
    }

    // ---- sound engines ----
    {
        const auto toneFile = s.instrumentsDir().getChildFile ("Selftest tone").getChildFile ("sound.wav");
        auto* et = s.addTrack (Kind::sampler, "Engine test");
        if (auto* rs = s.getSampled (*et)) rs->deleteFromParent();
        auto ep = s.edit->getPluginCache().createNewPlugin (EnginePlugin::xmlTypeName, {});
        et->pluginList.insertPlugin (ep, 0, nullptr);
        auto* eng = dynamic_cast<EnginePlugin*> (ep.get());
        check (eng != nullptr && eng->setLayerSound (0, toneFile, 57, "tone", nullptr) && eng->setLayerSound (1, toneFile, 57, "tone up", nullptr),
               "the engine instrument loads recorded sounds into its layers");
        if (eng != nullptr)
        {
            eng->layerTune[1]->setParameter (12.0f, juce::dontSendNotification);   // layer 2 an octave up, to tell them apart
            eng->drift->setParameter (0.0f, juce::dontSendNotification);
            const double spb = 60.0 / s.getBpm();
            auto play = [&] (std::initializer_list<int> notes, double beats, double seconds, const juce::String& name)
            {
                for (auto* c : et->getClips()) c->removeFromParent();
                if (auto* mc = s.addMidiClip (*et, {}, 2)) for (int nn : notes) mc->getSequence().addNote (nn, {}, te::BeatDuration::fromBeats (beats), 100, 0, nullptr);
                auto f = dir.getChildFile (name + ".wav");
                renderTrack (s, *et, f, { te::TimePosition(), te::TimePosition::fromSeconds (seconds) });
                return f;
            };
            auto energy = [] (const juce::File& f, double hz, double from, double len)   // Goertzel: how much of one frequency is there
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
                if (r == nullptr) return 0.0;
                const int a = (int) (from * r->sampleRate), n = (int) (len * r->sampleRate);
                juce::AudioBuffer<float> b (1, n); r->read (&b, 0, n, a, true, false);
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / r->sampleRate, c = 2.0 * std::cos (w);
                double s1 = 0, s2 = 0;
                for (int i = 0; i < n; ++i) { const double s0 = b.getSample (0, i) + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / n;
            };
            auto onsets = [] (const juce::File& f)   // new notes: sudden rises in loudness, even while the last one is still ringing
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
                if (r == nullptr) return 0;
                const int n = (int) r->lengthInSamples, hop = (int) (r->sampleRate * 0.01);
                juce::AudioBuffer<float> b (1, n); r->read (&b, 0, n, 0, true, false);
                std::vector<float> env; float peak = 0;
                for (int i = 0; i + hop < n; i += hop) { env.push_back (b.getRMSLevel (0, i, hop)); peak = juce::jmax (peak, env.back()); }
                int count = 0, last = -100;
                for (int i = 0; i < (int) env.size(); ++i)
                {
                    const float before = i >= 2 ? env[(size_t) i - 2] : 0.0f;
                    if (env[(size_t) i] > peak * 0.3f && env[(size_t) i] > before * 1.3f && i - last >= 6) { ++count; last = i; }
                }
                return count;
            };

            eng->engine = EnginePlugin::layersEngine;
            eng->blend->setParameter (0.0f, juce::dontSendNotification);
            auto lf = play ({ 60 }, 2, 0.8, "eng-layers");
            auto pl = detectPitch (lf, 0.1, 0.5);
            check (pl && std::abs (pl->freq - 261.6) < 4, "Layers with Blend at zero plays just the first layer, in tune (" + juce::String (pl ? pl->freq : 0.0, 1) + " Hz)");

            eng->engine = EnginePlugin::chordsEngine; eng->chordType = 0; eng->spread->setParameter (0.0f, juce::dontSendNotification);
            auto cf = play ({ 60 }, 2, 0.8, "eng-chords");
            const double eC = energy (cf, 261.6, 0.1, 0.5), eE = energy (cf, 329.6, 0.1, 0.5), eG = energy (cf, 392.0, 0.1, 0.5), eOff = energy (cf, 293.7, 0.1, 0.5);
            check (eC > eOff * 4 && eE > eOff * 4 && eG > eOff * 4, "Chords turns one key into a major chord: C, E and G all sound (" + juce::String (eC / juce::jmax (1e-9, eOff), 0) + "x, "
                   + juce::String (eE / juce::jmax (1e-9, eOff), 0) + "x, " + juce::String (eG / juce::jmax (1e-9, eOff), 0) + "x a note that isn't in it)");

            eng->engine = EnginePlugin::disperseEngine; eng->disperseVoices = 3; eng->spread->setParameter (7.0f, juce::dontSendNotification);
            eng->glide->setParameter (0.4f, juce::dontSendNotification);
            auto df = play ({ 60 }, 4, 1.4, "eng-disperse");
            const double gEarly = energy (df, 392.0, 0.02, 0.1), gLate = energy (df, 392.0, 0.8, 0.4), fLate = energy (df, 174.6, 0.8, 0.4);
            check (gLate > gEarly * 3 && fLate > gEarly, "Disperse fans the voices out from the note to a fifth either side (G4 grows " + juce::String (gLate / juce::jmax (1e-9, gEarly), 1) + "x)");

            eng->engine = EnginePlugin::latticeEngine; eng->latticeRate = 3; eng->density->setParameter (1.0f, juce::dontSendNotification);
            eng->jumps->setParameter (0.0f, juce::dontSendNotification); eng->gate->setParameter (0.4f, juce::dontSendNotification);
            auto tf = play ({ 60 }, 2, 1.1, "eng-lattice");
            const int lat = onsets (tf);
            check (lat >= 7 && lat <= 9, "Lattice turns one held note into sixteenths (" + juce::String (lat) + " hits in 2 beats, want 8)");

            eng->engine = EnginePlugin::threadsEngine; eng->blend->setParameter (0.0f, juce::dontSendNotification);   // lane 1 alone
            eng->laneSteps[0] = 7; eng->laneRate[0] = 3; eng->humanize->setParameter (0.0f, juce::dontSendNotification);
            auto thf = play ({ 60, 64, 67 }, 2, 1.1, "eng-threads");
            const int th = onsets (thf);
            check (th >= 4 && th <= 6, "Threads plays the lane's own pattern: " + juce::String (th) + " notes in 8 steps (want 5, its rests stay silent)");

            eng->engine = EnginePlugin::orbitsEngine; eng->motion = 0; eng->posY->setParameter (0.0f, juce::dontSendNotification);
            eng->posX->setParameter (0.0f, juce::dontSendNotification);
            auto o1 = play ({ 60 }, 2, 0.8, "eng-orbit-left");
            eng->posX->setParameter (1.0f, juce::dontSendNotification);
            auto o2 = play ({ 60 }, 2, 0.8, "eng-orbit-right");
            auto p1 = detectPitch (o1, 0.1, 0.5), p2 = detectPitch (o2, 0.1, 0.5);
            check (p1 && std::abs (p1->freq - 261.6) < 4 && p2 && std::abs (p2->freq - 523.3) < 8,
                   "Orbits blends the corners: left corner plays layer 1 (" + juce::String (p1 ? p1->freq : 0.0, 1) + " Hz), right corner layer 2 (" + juce::String (p2 ? p2->freq : 0.0, 1) + " Hz)");
        }
        s.deleteTrack (*et);

        // a ready-to-play engine track from Add track
        auto* et2 = s.addEngineTrack (EnginePlugin::chordsEngine);
        auto* e2 = et2 != nullptr ? s.getEngine (*et2) : nullptr;
        int loaded = 0; if (e2) for (int i = 0; i < EnginePlugin::numLayers; ++i) if (e2->layerLoaded (i)) ++loaded;
        check (e2 != nullptr && loaded == 4 && getKind (*et2) == Kind::engine && e2->engine.get() == EnginePlugin::chordsEngine,
               "Add track, Engine makes an engine track with four synth sounds ready in its layers (" + juce::String (loaded) + " loaded)");
        if (et2 != nullptr)
        {
            if (auto* mc = s.addMidiClip (*et2, {}, 1)) mc->getSequence().addNote (60, {}, te::BeatDuration::fromBeats (2), 100, 0, nullptr);
            auto ef = dir.getChildFile ("engine-track.wav");
            renderTrack (s, *et2, ef, { te::TimePosition(), te::TimePosition::fromSeconds (1.2) });
            auto st2 = analyse (ef);
            auto* arp = s.addArp (*et2);
            check (st2.rms > 0.01 && arp != nullptr && et2->pluginList.indexOf (arp) < et2->pluginList.indexOf (e2), "the engine track plays (rms " + juce::String (st2.rms, 3) + ") and takes an arpeggiator in front");
            s.deleteTrack (*et2);
        }
    }

    // ---- sampler: slicing, filter; trance gate ----
    {
        auto bandEnergy = [] (const juce::File& f, double lo, double hi, double from, double len)   // share of energy between lo and hi Hz
        {
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            if (r == nullptr) return 0.0;
            constexpr int order = 12, N = 1 << order;
            juce::AudioBuffer<float> b (1, N); b.clear();
            r->read (&b, 0, juce::jmin (N, (int) (len * r->sampleRate)), (juce::int64) (from * r->sampleRate), true, false);
            std::vector<float> d ((size_t) N * 2); for (int i = 0; i < N; ++i) d[(size_t) i] = b.getSample (0, i);
            juce::dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (d.data());
            double in = 0, all = 1e-12;
            for (int k = 1; k < N / 2; ++k) { const double hz = k * r->sampleRate / N, e = (double) d[(size_t) k] * d[(size_t) k]; all += e; if (hz >= lo && hz < hi) in += e; }
            return in / all;
        };
        const auto loop = dir.getChildFile ("beat.wav");
        auto* st = s.addTrack (Kind::sampler, "Slicer");
        const bool loaded = s.loadSamplerSound (*st, loop, false);
        auto* smp = s.getSampled (*st);
        if (loaded && smp != nullptr)
        {
            smp->oneShot = ReelSampler::slice; smp->sliceMethod = 0;
            const int n = s.sliceSampler (*st);
            const auto sl = smp->getSlices();
            const double secs = smp->soundSeconds(), spb = 60.0 / s.getBpm();
            int lined = 0; double worst = 0;
            for (int bar = 0; bar < 2; ++bar) for (int stp : { 0, 2, 4, 6, 8, 10, 12, 14 })
            {
                const double hit = (bar * 16 + stp) * 0.25 * spb;
                for (double x : sl) if (std::abs (x * secs - hit) < 0.02) { ++lined; worst = juce::jmax (worst, std::abs (x * secs - hit)); break; }
            }
            check (n == 16 && lined == 16, "slicing a drum loop by its hits finds " + juce::String (n) + " slices, " + juce::String (lined) + " of 16 on the hits (within " + juce::String (juce::roundToInt (worst * 1000)) + " ms)");

            auto playNote = [&] (int note, const juce::String& name)
            {
                for (auto* c : st->getClips()) c->removeFromParent();
                if (auto* mc = s.addMidiClip (*st, {}, 1)) mc->getSequence().addNote (note, {}, te::BeatDuration::fromBeats (0.2), 110, 0, nullptr);
                auto f = dir.getChildFile (name + ".wav");
                renderTrack (s, *st, f, { te::TimePosition(), te::TimePosition::fromSeconds (0.25) });
                return f;
            };
            auto kick = playNote (60, "slice-kick"), hat = playNote (61, "slice-hat");
            const double kLow = bandEnergy (kick, 20, 200, 0.0, 0.09), hHigh = bandEnergy (hat, 5000, 20000, 0.0, 0.09);
            check (kLow > 0.5 && hHigh > 0.4, "each key plays its own slice: C4 is the kick (" + juce::String (juce::roundToInt (kLow * 100)) + "% low end), C#4 the hi-hat (" + juce::String (juce::roundToInt (hHigh * 100)) + "% highs)");

            smp->filterType = 1; smp->cutoff->setParameter (400.0f, juce::dontSendNotification);
            auto dark = playNote (61, "slice-hat-filtered");
            const double hDark = bandEnergy (dark, 5000, 20000, 0.0, 0.09);
            check (hDark < hHigh * 0.1, "the low-pass filter darkens the hi-hat (highs " + juce::String (juce::roundToInt (hHigh * 100)) + "% to " + juce::String (hDark * 100, 1) + "%)");
            smp->cutoff->setParameter (20000.0f, juce::dontSendNotification);

            for (auto* c : st->getClips()) c->removeFromParent();
            s.transport().setPosition ({});
            auto* mc = s.slicesToMidi (*st);
            auto back = dir.getChildFile ("slices-back.wav");
            renderTrack (s, *st, back, { te::TimePosition(), te::TimePosition::fromSeconds (secs) });
            auto hits = transcribe (back, TranscribeMode::drums);
            check (mc != nullptr && mc->getSequence().getNotes().size() == n && (int) hits.size() >= 14,
                   "Make a MIDI clip of the slices plays the loop back: " + juce::String ((int) hits.size()) + " hits heard");

            smp->sliceMethod = 1; smp->sliceCount = 1;
            check (s.sliceSampler (*st) == 8, "slicing by the beat gives 8 equal slices");
        }
        else check (false, "load the drum loop into a sampler");
        s.deleteTrack (*st);

        // trance gate on a steady tone: open steps loud, closed steps quiet
        auto* gt = s.addTrack (Kind::synth, "Gate test");
        s.setSynthPreset (*gt, "Organ");
        if (auto* mc = s.addMidiClip (*gt, {}, 2)) mc->getSequence().addNote (60, {}, te::BeatDuration::fromBeats (8), 100, 0, nullptr);
        auto* gp = dynamic_cast<TranceGatePlugin*> (s.addEffect (*gt, TranceGatePlugin::xmlTypeName));
        if (gp != nullptr)
        {
            for (int i = 0; i < 16; ++i) gp->setStep (i, i % 2 == 0 ? 1.0f : 0.0f, nullptr);
            auto gf = dir.getChildFile ("gate.wav");
            renderTrack (s, *gt, gf, { te::TimePosition(), te::TimePosition::fromSeconds (2.0) });
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (gf));
            double on = 0, off = 0;
            if (r != nullptr)
            {
                const double stepSec = 0.25 * 60.0 / s.getBpm();
                for (int k = 2; k < 14; ++k)
                {
                    juce::AudioBuffer<float> b (1, (int) (stepSec * 0.5 * r->sampleRate));
                    r->read (&b, 0, b.getNumSamples(), (juce::int64) ((k + 0.25) * stepSec * r->sampleRate), true, false);
                    (k % 2 == 0 ? on : off) += b.getRMSLevel (0, 0, b.getNumSamples());
                }
            }
            check (on > off * 6, "Trance Gate opens on its steps and closes between them (" + juce::String (juce::Decibels::gainToDecibels (on / juce::jmax (1e-9, off)), 1) + " dB louder on the open steps)");
        }
        else check (false, "add a Trance Gate");
        s.deleteTrack (*gt);
    }

    // ---- recordings into Lines ----
    {
        auto* at = s.addTrack (Kind::audio, "Sung line");
        auto* wc = s.importAudio (dir.getChildFile ("lines.wav"), at, te::TimePosition::fromSeconds (s.barLength().inSeconds()));
        te::AudioTrack* made = wc ? s.convertToLines (*wc) : nullptr;
        auto* lp = made ? s.getLines (*made) : nullptr;
        if (lp != nullptr)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto pat = lp->snapshot();
            const LineData* first = pat && ! pat->lines.empty() ? &pat->lines.front() : nullptr;
            for (auto& l : pat->lines) if (first == nullptr || l.end() - l.start() > first->end() - first->start()) first = &l;
            const float lo = first ? first->pitchAt (first->start() + 0.3) : 0, hi = first ? first->pitchAt (first->start() + 3.3) : 0, mid = first ? first->pitchAt (first->start() + 2.0) : 0;
            check (first != nullptr && std::abs (lo - 60) < 0.4f && std::abs (hi - 67) < 0.4f && mid > 61.5f && mid < 65.5f,
                   "Turn into Lines follows the recording's glide: " + juce::String (lo, 2) + " then " + juce::String (mid, 2) + " mid-slide then " + juce::String (hi, 2) + " ("
                   + juce::String (pat ? (int) pat->lines.size() : 0) + " lines)");
            check (lp->playMode.get() == 1 && std::abs (lp->startBeat.get() - s.toBeats (wc->getPosition().getStart()).inBeats()) < 1e-6,
                   "the lines play once, lined up with the recording (from bar " + juce::String (lp->startBeat.get() / 4.0 + 1.0, 1) + ")");
        }
        else check (false, "Turn into Lines made a Lines track");
        if (made) s.deleteTrack (*made);
        s.deleteTrack (*at);
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
        if (auto* t = uiSession->tracks()[1]) { if (auto* l = uiSession->addLines (*t)) { LineData d; d.pts = { { 0, 60 }, { 2, 64 } }; l->addLine (d, nullptr); } uiSession->select (t); }
        pump();
        std::cout << "      Lines editor showing" << std::endl;
        {
            auto& dock = ui->getDock();
            dock.movePanel ("Mixer", DockPlace::right); pump();
            dock.movePanel ("Sound", DockPlace::left); pump();
            dock.movePanel ("Effects", DockPlace::window); pump();
            auto* fx = dock.windowFor ("Effects");
            const bool sideOk = dock.placeOf ("Mixer") == DockPlace::right && dock.placeOf ("Sound") == DockPlace::left;
            const bool winOk = fx != nullptr && fx->isVisible();
            dock.setFill (0); pump();
            const bool fillOk = dock.getFill() == 0;
            dock.setFill (3); pump();
            uiSession->newProject(); pump();      // switching projects with sections spread about and one in a window
            uiSession->loadDemo(); pump();
            const auto saved = dock.getState();
            dock.resetLayout(); pump();
            dock.restoreState (saved); pump(); pump();
            const bool restored = dock.placeOf ("Mixer") == DockPlace::right && dock.windowFor ("Effects") != nullptr;
            if (auto* w = dock.windowFor ("Effects")) w->closeButtonPressed();
            pump(); pump();
            const bool closedBack = dock.windowFor ("Effects") == nullptr && dock.placeOf ("Effects") == DockPlace::bottom;
            check (sideOk && winOk && fillOk && restored && closedBack,
                   "sections dock on either side, open in their own window, fill the window, survive a project switch, restore, and closing a window docks it back");
            dock.resetLayout(); pump();
        }
        if (auto* et = uiSession->addEngineTrack (EnginePlugin::threadsEngine))
        {
            ui->getDock().showPanel ("Sound");
            for (int i = 0; i < 6; ++i)
            {
                uiSession->getEngine (*et)->engine = i;
                uiSession->select (uiSession->tracks().getFirst()); pump();
                uiSession->select (et); pump();
            }
            std::cout << "      engine screens showing" << std::endl;
        }
        if (auto* tone = uiSession->tracks().getFirst())
        {
            if (auto* sp = uiSession->addTrack (Kind::sampler, "Slicer"))
            {
                uiSession->loadSamplerSound (*sp, dir.getChildFile ("beat.wav"), false);
                if (auto* rs = uiSession->getSampled (*sp)) { rs->oneShot = ReelSampler::slice; uiSession->sliceSampler (*sp); }
                uiSession->select (sp); pump();
                uiSession->addEffect (*sp, TranceGatePlugin::xmlTypeName);
                ui->getDock().showPanel ("Effects"); pump();
                ui->getDock().showPanel ("Sound"); pump();
                std::cout << "      slicer and trance gate showing" << std::endl;
            }
            juce::ignoreUnused (tone);
        }
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

//==============================================================================
/** Real-time test with a running audio device (used with a virtual sound card): plays, records audio from the
    input and records notes from the on-screen keyboard. */
int runLiveTest (te::Engine& engine, const juce::File& dir)
{
    dir.createDirectory();
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl; if (! ok) ++fails; };
    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
    auto& adm = engine.getDeviceManager().deviceManager;
    if (adm.getCurrentDeviceTypeObject() == nullptr || adm.getCurrentDeviceTypeObject()->getTypeName() != "JACK") adm.setCurrentAudioDeviceType ("JACK", true);
    auto setup = adm.getAudioDeviceSetup();
    if (auto* type = adm.getCurrentDeviceTypeObject())   // don't rely on whatever device was saved last time
    {
        type->scanForDevices();
        if (! type->getDeviceNames (false).isEmpty()) setup.outputDeviceName = type->getDeviceNames (false)[0];
        if (! type->getDeviceNames (true).isEmpty()) setup.inputDeviceName = type->getDeviceNames (true)[0];
    }
    setup.inputChannels.setRange (0, 2, true); setup.outputChannels.setRange (0, 2, true);
    setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
    const auto err = adm.setAudioDeviceSetup (setup, true);
    pump (1500);
    auto* dev = adm.getCurrentAudioDevice();
    check (dev != nullptr, "audio device running" + (dev ? ": " + dev->getName() + " at " + juce::String (dev->getCurrentSampleRate()) + " Hz" : juce::String (", error: ") + err));
    if (dev == nullptr) return 1;

    Session s (engine);
    s.newProject();
    check (! s.audioInputNames().isEmpty(), "inputs available: " + s.audioInputNames().joinIntoString (", "));
    std::cout << "LIVE READY" << std::endl;
    pump (4000);   // the external tone source connects now

    s.transport().play (false);
    for (int i = 0; i < 60 && s.transport().getPosition().inSeconds() < 0.05; ++i) pump (50);   // wait for it to start moving
    const double p0 = s.transport().getPosition().inSeconds(), w0 = juce::Time::getMillisecondCounterHiRes();
    pump (1500);
    const double speed = (s.transport().getPosition().inSeconds() - p0) / ((juce::Time::getMillisecondCounterHiRes() - w0) / 1000.0);
    s.stop (true);
    check (speed > 0.9 && speed < 1.1, "playback runs in real time (" + juce::String (speed, 3) + " seconds of song per second)");

    auto* at = s.addTrack (Kind::audio, "Mic");
    s.select (at); s.setCountIn (0);
    s.toggleRecord();
    check (s.transport().isRecording(), "recording started on the audio track");
    pump (2500);
    s.toggleRecord();
    pump (2000);
    te::WaveAudioClip* rec = nullptr;
    for (auto* c : at->getClips()) if (auto* w = dynamic_cast<te::WaveAudioClip*> (c)) rec = w;
    check (rec != nullptr, "recorded audio becomes a clip");
    if (rec != nullptr)
    {
        auto f = rec->getSourceFileReference().getFile();
        auto st = analyse (f);
        auto p = detectPitch (f, 0.3, 1.0);
        check (st.secs > 1.5 && st.rms > 0.01, "the recording has sound (" + juce::String (st.secs, 2) + " s, rms " + juce::String (st.rms, 3) + ", saved as " + f.getFileName() + ")");
        check (p && std::abs (p->freq - 440.0) < 5.0, "the recorded test tone is 440 Hz (got " + juce::String (p ? p->freq : 0.0, 1) + ")");
    }

    auto* syn = s.addTrack (Kind::synth, "Keys");
    s.select (syn);
    s.seek ({});
    s.toggleRecord();
    for (int i = 0; i < 60 && s.transport().getPosition().inSeconds() < 0.05; ++i) pump (50);   // wait until recording is really running
    pump (150);
    for (int i = 0; i < 4; ++i) { s.keyboardNote (60 + i * 2, 0.8f, true); pump (250); s.keyboardNote (60 + i * 2, 0.0f, false); pump (150); }
    s.toggleRecord();
    pump (1500);
    int recorded = 0;
    for (auto* c : syn->getClips()) if (auto* mc = dynamic_cast<te::MidiClip*> (c)) recorded += mc->getSequence().getNotes().size();
    check (recorded == 4, "keyboard notes recorded into a MIDI clip (" + juce::String (recorded) + " of 4)");

    auto* lt = s.addTrack (Kind::synth, "Lines");
    s.select (lt);
    if (auto* lp = s.addLines (*lt))
    {
        s.seek ({});
        lp->recording.store (true);
        s.togglePlay();
        for (int i = 0; i < 60 && s.transport().getPosition().inSeconds() < 0.05; ++i) pump (50);
        pump (150);
        for (int i = 0; i < 3; ++i) { s.keyboardNote (64 + i * 3, 0.8f, true); pump (300); s.keyboardNote (64 + i * 3, 0.0f, false); pump (150); }
        lp->recording.store (false);
        pump (400);
        s.stop (true);
        pump (300);
        int lines = 0; for (auto& l : lp->lineTrees()) if (LinesPlugin::lineFromTree (l).pts.size() >= 2) ++lines;
        check (lines == 3, "playing the keyboard into Lines records lines (" + juce::String (lines) + " of 3)");
    }
    // the arpeggiator keeps its own time when the song is stopped: hold one key and watch the track's meter pulse
    s.stop (true);
    pump (300);
    auto* arpT = s.addTrack (Kind::synth, "Arp live");
    s.setSynthPreset (*arpT, "Sub sine");
    s.select (arpT);
    if (auto* arp = s.addArp (*arpT))
    {
        arp->rate = 0;   // quarter notes: 0.5 s apart at 120 bpm
        arp->gate->setParameter (0.2f, juce::dontSendNotification);
        pump (300);
        if (auto* meter = arpT->pluginList.findFirstPluginOfType<te::LevelMeterPlugin>())
        {
            te::LevelMeasurer::Client client;
            meter->measurer.addClient (client);
            s.keyboardNote (60, 0.8f, true);
            int pulses = 0; bool on = false;
            for (int i = 0; i < 44; ++i)
            {
                pump (50);
                float db = -100.0f; for (int c = 0; c < 2; ++c) db = juce::jmax (db, client.getAndClearAudioLevel (c).dB);
                const bool now = db > -30.0f;
                if (now && ! on) ++pulses;
                on = now;
            }
            s.keyboardNote (60, 0.0f, false);
            meter->measurer.removeClient (client);
            check (! s.transport().isPlaying() && pulses >= 3 && pulses <= 6, "holding a key with the song stopped, the arpeggiator pulses in time (" + juce::String (pulses) + " notes in 2.2 s, want 5: one every half second)");
        }
    }

    std::cout << (fails == 0 ? "ALL LIVE TESTS PASSED" : juce::String (fails) + " LIVE TEST(S) FAILED") << std::endl;
    return fails;
}

int runMigrateTest (te::Engine& engine, const juce::File& f)
{
    Session s (engine);
    s.openProject (f);
    auto* t = s.tracks().getFirst();
    auto* rs = t ? s.getSampled (*t) : nullptr;
    std::cout << "after open: sampler " << (rs != nullptr) << " hasSound " << (rs && rs->hasSound()) << " file '" << (rs ? rs->file.get() : juce::String()) << "' root " << (rs ? rs->root.get() : -1) << std::endl;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    std::cout << "after a moment: hasSound " << (rs && rs->hasSound()) << " file '" << (rs ? rs->file.get() : juce::String()) << "'" << std::endl;
    return 0;
}
}
