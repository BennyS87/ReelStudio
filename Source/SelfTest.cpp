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
    pump (400);
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
        pump (300);
        for (int i = 0; i < 3; ++i) { s.keyboardNote (64 + i * 3, 0.8f, true); pump (300); s.keyboardNote (64 + i * 3, 0.0f, false); pump (150); }
        lp->recording.store (false);
        pump (400);
        s.stop (true);
        pump (300);
        int lines = 0; for (auto& l : lp->lineTrees()) if (LinesPlugin::lineFromTree (l).pts.size() >= 2) ++lines;
        check (lines == 3, "playing the keyboard into Lines records lines (" + juce::String (lines) + " of 3)");
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
