#include "Panels.h"
#include "EngineWidgets.h"
#include "SamplerView.h"
#include "EffectEditors.h"

namespace Reel
{
namespace
{
    juce::ComboBox* combo (juce::StringArray names, juce::Array<int> ids, int current, std::function<void (int)> onChange)
    {
        auto* c = new juce::ComboBox();
        for (int i = 0; i < names.size(); ++i) c->addItem (names[i], ids[i]);
        c->setSelectedId (current, juce::dontSendNotification);
        c->onChange = [c, onChange] { onChange (c->getSelectedId()); };
        return c;
    }
    juce::TextButton* button (const juce::String& text, std::function<void()> fn)
    {
        auto* b = new juce::TextButton (text); b->onClick = std::move (fn); return b;
    }
    int textW (const juce::String& s, float size = 14.0f) { return juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (size)), s) + 28; }
    juce::Label* caption (const juce::String& s, int w)
    {
        auto* l = new juce::Label ({}, s); l->setColour (juce::Label::textColourId, Col::muted); l->setFont (juce::FontOptions (12.0f)); l->setJustificationType (juce::Justification::bottomLeft); return l;
    }
}

//==============================================================================
int RebuildingPanel::requiredHeight (int width)
{
    int x = 12, y = 10, lineH = 0;
    for (int i = 0; i < items.size(); ++i)
    {
        auto* c = items[i];
        if (auto* b = dynamic_cast<Box*> (c)) { c->setSize (c->getWidth(), b->heightFor (c->getWidth())); b->resized(); }
        if ((breaks.count (i) || x + c->getWidth() > width - 10) && x > 12) { x = 12; y += lineH + 10; lineH = 0; }
        x += c->getWidth() + 10; lineH = juce::jmax (lineH, c->getHeight());
    }
    return y + lineH + 14;
}

void RebuildingPanel::layout()
{
    int x = 12, y = 10, lineH = 0;
    for (int i = 0; i < items.size(); ++i)
    {
        auto* c = items[i];
        if (auto* b = dynamic_cast<Box*> (c)) { c->setSize (c->getWidth(), b->heightFor (c->getWidth())); b->resized(); }
        if ((breaks.count (i) || x + c->getWidth() > getWidth() - 10) && x > 12) { x = 12; y += lineH + 10; lineH = 0; }
        c->setTopLeftPosition (x, y);
        x += c->getWidth() + 10; lineH = juce::jmax (lineH, c->getHeight());
    }
}

juce::Label* RebuildingPanel::heading (const juce::String& text, bool newLine)
{
    if (newLine) lineBreak();
    auto* l = add (new juce::Label ({}, text), juce::jmax (120, textW (text, 18.0f)), 28);
    styleHeading (*l, 18.0f);
    return l;
}

juce::Label* RebuildingPanel::hintText (const juce::String& text, int w, int h)
{
    lineBreak();
    auto* l = add (new juce::Label ({}, text), w, h);
    styleHint (*l);
    return l;
}

//==============================================================================
void SoundPanel::build()
{
    auto* t = session.getSelectedTrack();
    if (t == nullptr) { hintText ("Select a track to change its sound."); return; }
    switch (getKind (*t))
    {
        case Kind::synth:   if (auto* s = session.getSynth (*t)) buildSynth (*t, *s); break;
        case Kind::sampler: buildSampler (*t); break;
        case Kind::drums:   buildKit (*t); break;
        case Kind::engine:  buildEngine (*t); break;
        case Kind::audio:
        {
            auto id = t->itemID;
            heading ("Recording", false);
            add (button (session.transport().isRecording() ? "Stop recording" : "Record now (R)", [this] { session.toggleRecord(); }), 150, 30)
                ->setColour (juce::TextButton::buttonColourId, Col::rec.withAlpha (0.35f));
            auto* box = add (new Box ("Input", "Select this track and press Record to capture the input. Use headphones if you turn on Hear input, otherwise the speakers feed back into the microphone."), 760, 100);
            auto inputs = session.audioInputNames();
            if (inputs.isEmpty())
                box->add (captionLabel ("No audio inputs found. Open Project, Audio and MIDI settings to choose a microphone or interface."), 600, 28);
            else
            {
                auto* in = box->add (new juce::ComboBox(), 300, 28);
                for (int i = 0; i < inputs.size(); ++i) in->addItem (inputs[i], i + 1);
                const int cur = inputs.indexOf (session.getRecordInput (*t));
                in->setSelectedId ((cur >= 0 ? cur : 0) + 1, juce::dontSendNotification);
                in->onChange = [this, id, in] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setRecordInput (*t, in->getText()); rebuildNow(); };
                auto* hear = box->add (new juce::ToggleButton ("Hear input"), 120, 28);
                hear->setToggleState (session.isInputMonitoring (*t), juce::dontSendNotification);
                hear->onClick = [this, id, hear] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setInputMonitoring (*t, hear->getToggleState()); };
                box->breakBefore();
                box->add (captionLabel ("Level"), 44, 22);
                if (auto* m = session.inputMeasurer (*t)) box->add (new InputMeter (*m), 420, 22);
                session.routeLiveInput();
            }
            box->breakBefore();
            box->add (captionLabel ("Count-in"), 64, 28);
            auto* ci = box->add (new juce::ComboBox(), 120, 28);
            const char* ciNames[] = { "None", "1 bar", "2 bars", "2 beats", "1 beat" };
            for (int i = 0; i < 5; ++i) ci->addItem (ciNames[i], i + 1);
            ci->setSelectedId (session.getCountIn() + 1, juce::dontSendNotification);
            ci->onChange = [this, ci] { session.setCountIn (ci->getSelectedId() - 1); };
            box->add (captionLabel ("Turn on Click in the toolbar to hear the metronome while you record."), 420, 28);
            hintText ("Recordings are saved as new audio files next to your project and appear as clips you can trim, stretch and reverse. "
                      "To play a recorded sound from a keyboard, right-click its clip and choose Make a playable instrument. "
                      "To turn a recorded melody into notes, choose Turn into MIDI notes.", 900, 54);
            break;
        }
    }
}

void SoundPanel::buildSynth (te::AudioTrack& t, te::FourOscPlugin& s)
{
    auto* um = &session.undoManager();
    auto id = t.itemID;
    te::Plugin::Ptr keep (&s);
    const auto current = t.state.getProperty (IDs::preset, "Soft keys").toString();
    juce::StringArray names; juce::Array<int> ids; int sel = 0;
    auto builtIns = builtInPresetNames(), users = userPresetNames();
    for (int i = 0; i < builtIns.size(); ++i) { names.add (builtIns[i]); ids.add (i + 1); if (builtIns[i] == current) sel = i + 1; }
    for (int i = 0; i < users.size(); ++i) { names.add ("Yours: " + users[i]); ids.add (1000 + i); if ("user:" + users[i] == current) sel = 1000 + i; }
    heading ("Instrument designer", false);
    auto* presetBox = add (combo (names, ids, sel, [this, id, builtIns, users] (int v)
    {
        if (auto* t = te::findAudioTrackForID (*session.edit, id))
        { session.beginAction(); session.setSynthPreset (*t, v >= 1000 ? "user:" + users[v - 1000] : builtIns[v - 1]); }
    }), 220, 30);
    if (sel == 0) presetBox->setText ("Custom sound", juce::dontSendNotification);
    add (button ("Save as my sound", [this, id]
    {
        auto* w = new juce::AlertWindow ("Save this sound", "It will appear in the sound list for every synth track.", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", "My sound");
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int r)
        {
            auto* t = te::findAudioTrackForID (*session.edit, id); if (r != 1 || t == nullptr) return;
            auto n = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
            if (auto* s = session.getSynth (*t)) if (saveUserPreset (*s, n)) { t->state.setProperty (IDs::preset, "user:" + n, nullptr); rebuildNow(); }
        }), true);
    }), 150, 30);
    add (button ("Reset", [this, id, current] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.setSynthPreset (*t, current); rebuildNow(); } }), 80, 30);
    addInstrumentChooser (t, false);
    addArpBox (t);
    hintText ("Start from a sound above, then shape it: four oscillators, a filter with its own envelope, two LFOs and two modulation envelopes you can route to any setting. "
              "Play notes while you turn knobs to hear changes straight away.", 900, 40);

    auto knob = [um] (Box* box, te::AutomatableParameter::Ptr p, const juce::String& label, std::function<juce::String (float)> fmt = nullptr)
    {
        auto* k = box->add (new ParamKnob (p, label, um), 68, 78);
        if (fmt) { k->formatter = std::move (fmt); k->refresh(); }
    };
    auto toggle = [um] (const juce::String& text, juce::CachedValue<bool>& v)
    {
        auto* b = new juce::ToggleButton (text); b->setToggleState (v.get(), juce::dontSendNotification);
        auto* vp = &v; b->onClick = [b, vp, um] { um->beginNewTransaction(); *vp = b->getToggleState(); }; return b;
    };
    auto secs = [] (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000)) + " ms" : juce::String (v, 2) + " s"; };
    auto pctOf100 = [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; };
    auto noteHz = [] (float v) { return Fmt::hz ((float) (440.0 * std::pow (2.0, (v - 69.0) / 12.0))); };

    // ---- oscillators ----
    const juce::StringArray waveNames { "Off", "Sine", "Square", "Saw", "Triangle", "Noise" };   // te::Oscillator::Waves order
    lineBreak();
    for (int i = 0; i < juce::jmin (4, s.oscParams.size()); ++i)
    {
        auto& o = *s.oscParams[i];
        auto* box = add (new Box ("Oscillator " + juce::String (i + 1)), 540, 100);
        box->accent = Col::acc;
        box->add (captionLabel ("Wave"), 40, 28);
        box->add (makeChoiceBox (o.waveShapeValue, waveNames, {}, um), 104, 28);
        box->add (captionLabel ("Unison voices"), 92, 28);
        box->add (makeChoiceBox (o.voicesValue, { "1", "2", "3", "4", "5", "6", "7", "8" }, { 1, 2, 3, 4, 5, 6, 7, 8 }, um), 64, 28);
        box->breakBefore();
        knob (box, o.tune, "Pitch", [] (float v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; });
        knob (box, o.fineTune, "Fine", [] (float v) { return juce::String (juce::roundToInt (v)) + " ct"; });
        knob (box, o.level, "Level", Fmt::db);
        knob (box, o.pulseWidth, "Pulse width", Fmt::pct);
        knob (box, o.detune, "Unison detune", [] (float v) { return juce::String (juce::roundToInt (v * 200)) + "%"; });
        knob (box, o.spread, "Stereo spread", pctOf100);
        knob (box, o.pan, "Pan", [] (float v) { return std::abs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100)); });
    }

    // ---- filter and amp ----
    auto* f = add (new Box ("Filter", "Cuts or boosts parts of the sound. The envelope sweeps the cutoff each time you press a key."), 700, 100);
    f->accent = Col::acc;
    f->add (captionLabel ("Type"), 36, 28);
    f->add (makeChoiceBox (s.filterTypeValue, { "Off", "Low pass", "High pass", "Band pass" }, { 0, 1, 2, 3 }, um), 110, 28);
    f->add (captionLabel ("Slope"), 40, 28);
    f->add (makeChoiceBox (s.filterSlopeValue, { "12 dB/oct", "24 dB/oct" }, { 12, 24 }, um), 104, 28);
    f->add (new EnvelopeDisplay (s.filterAttack, s.filterDecay, s.filterSustain, s.filterRelease, Col::acc), 220, 60);
    f->breakBefore();
    knob (f, s.filterFreq, "Cutoff", noteHz);
    knob (f, s.filterResonance, "Resonance", pctOf100);
    knob (f, s.filterAmount, "Env amount", [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
    knob (f, s.filterKey, "Key track", pctOf100);
    knob (f, s.filterVelocity, "Velocity", pctOf100);
    knob (f, s.filterAttack, "Attack", secs); knob (f, s.filterDecay, "Decay", secs); knob (f, s.filterSustain, "Sustain", pctOf100); knob (f, s.filterRelease, "Release", secs);

    auto* a = add (new Box ("Volume envelope", "How each note fades in, holds and fades out."), 520, 100);
    a->accent = Col::acc;
    a->add (new EnvelopeDisplay (s.ampAttack, s.ampDecay, s.ampSustain, s.ampRelease, Col::ok), 240, 60);
    a->add (toggle ("Analog drift", s.ampAnalogValue), 120, 28);
    a->breakBefore();
    knob (a, s.ampAttack, "Attack", secs); knob (a, s.ampDecay, "Decay", secs); knob (a, s.ampSustain, "Sustain", pctOf100); knob (a, s.ampRelease, "Release", secs);
    knob (a, s.ampVelocity, "Velocity", pctOf100);

    // ---- modulators ----
    const juce::StringArray lfoWaves { "Off", "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random" };
    for (int i = 0; i < juce::jmin (2, s.lfoParams.size()); ++i)
    {
        auto& l = *s.lfoParams[i];
        auto* box = add (new Box ("LFO " + juce::String (i + 1), "A slow wobble. Route it below to make any setting move."), 360, 100);
        box->add (makeChoiceBox (l.waveShapeValue, lfoWaves, { 0, 1, 2, 3, 4, 5, 6 }, um), 104, 28);
        box->add (toggle ("Sync to tempo", l.syncValue), 120, 28);
        auto* beat = box->add (new juce::ComboBox(), 90, 28);
        const float beats[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        const char* beatNames[] = { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" };
        int best = 3; for (int k = 0; k < 7; ++k) { beat->addItem (beatNames[k], k + 1); if (std::abs (beats[k] - l.beatValue.get()) < 1e-3f) best = k; }
        beat->setSelectedId (best + 1, juce::dontSendNotification);
        auto* bv = &l.beatValue;
        beat->onChange = [beat, bv, um] { const float b[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f }; um->beginNewTransaction(); *bv = b[beat->getSelectedId() - 1]; };
        box->breakBefore();
        knob (box, l.rate, "Rate (free)", [] (float v) { return juce::String (v, v < 10 ? 2 : 1) + " Hz"; });
        knob (box, l.depth, "Depth", Fmt::pct);
    }
    for (int i = 0; i < juce::jmin (2, s.modEnvParams.size()); ++i)
    {
        auto& m = *s.modEnvParams[i];
        auto* box = add (new Box ("Mod envelope " + juce::String (i + 1), "A shape that plays on every note. Route it below."), 330, 100);
        knob (box, m.modAttack, "Attack", secs); knob (box, m.modDecay, "Decay", secs); knob (box, m.modSustain, "Sustain", pctOf100); knob (box, m.modRelease, "Release", secs);
    }

    // ---- modulation matrix ----
    using MS = te::FourOscPlugin::ModSource;
    const std::vector<std::pair<MS, juce::String>> sources { { MS::lfo1, "LFO 1" }, { MS::lfo2, "LFO 2" }, { MS::env1, "Mod envelope 1" }, { MS::env2, "Mod envelope 2" },
        { MS::midiVelocity, "Velocity" }, { MS::midiNoteNum, "Note (key)" }, { (MS) (MS::ccBankSelect + 1), "Mod wheel" }, { MS::mpePressure, "Pressure" } };
    auto* mm = add (new Box ("Modulation", "Each route makes a modulator move a setting. Negative amounts move it the other way."), 760, 100);
    mm->accent = Col::acc;
    juce::Array<te::AutomatableParameter*> dests;
    for (auto* p : s.getAutomatableParameters()) dests.add (p);
    int routes = 0;
    for (auto* p : dests)
        for (auto src : s.getModulationSources (p))
        {
            juce::String srcName = s.modulationSourceToName (src);
            for (auto& sp : sources) if (sp.first == src) srcName = sp.second;
            mm->breakBefore();
            auto* l = mm->add (new juce::Label ({}, srcName + "  to  " + p->getParameterName()), 300, 28);
            l->setColour (juce::Label::textColourId, juce::Colours::white);
            auto* sl = mm->add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight), 300, 28);
            sl->setRange (-1.0, 1.0, 0.01); sl->setValue (s.getModulationDepth (src, p), juce::dontSendNotification);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
            sl->setScrollWheelEnabled (false);
            te::AutomatableParameter::Ptr pp (p);
            sl->onDragStart = [um] { um->beginNewTransaction(); };
            sl->onValueChange = [keep, pp, src, sl] { if (auto* syn = dynamic_cast<te::FourOscPlugin*> (keep.get())) syn->setModulationDepth (src, pp, (float) sl->getValue()); };
            mm->add (button ("Remove", [this, keep, pp, src]
            {
                if (auto* syn = dynamic_cast<te::FourOscPlugin*> (keep.get())) { session.beginAction(); syn->clearModulation (src, pp); }
                juce::Component::SafePointer<SoundPanel> sp (this); juce::MessageManager::callAsync ([sp] { if (sp) sp->rebuildNow(); });
            }), 80, 28);
            ++routes;
        }
    if (routes == 0) { mm->breakBefore(); auto* l = mm->add (captionLabel ("No routes yet. Try LFO 1 to Filter Freq for a classic wobble."), 500, 28); juce::ignoreUnused (l); }
    mm->breakBefore();
    juce::StringArray srcNames; for (auto& sp : sources) srcNames.add (sp.second);
    juce::StringArray destNames; for (auto* p : dests) destNames.add (p->getParameterName());
    auto* srcBox = mm->add (new juce::ComboBox(), 170, 28); for (int i = 0; i < srcNames.size(); ++i) srcBox->addItem (srcNames[i], i + 1); srcBox->setSelectedId (1, juce::dontSendNotification);
    mm->add (captionLabel ("to"), 22, 28);
    auto* dstBox = mm->add (new juce::ComboBox(), 220, 28); for (int i = 0; i < destNames.size(); ++i) dstBox->addItem (destNames[i], i + 1);
    dstBox->setSelectedId (juce::jmax (1, destNames.indexOf ("Filter Freq") + 1), juce::dontSendNotification);
    mm->add (button ("Add route", [this, keep, srcBox, dstBox, sources, dests]
    {
        auto* syn = dynamic_cast<te::FourOscPlugin*> (keep.get());
        const int si = srcBox->getSelectedId() - 1, di = dstBox->getSelectedId() - 1;
        if (syn == nullptr || si < 0 || di < 0) return;
        session.beginAction();
        syn->setModulationDepth (sources[(size_t) si].first, dests[di], 0.5f);
        juce::Component::SafePointer<SoundPanel> sp (this); juce::MessageManager::callAsync ([sp] { if (sp) sp->rebuildNow(); });
    }), 100, 28);

    // ---- voice and character ----
    auto* v = add (new Box ("Voice", "Mono plays one note at a time; legato glides between held notes."), 440, 100);
    v->add (makeChoiceBox (s.voiceModeValue, { "Mono", "Legato", "Poly" }, { 0, 1, 2 }, um), 100, 28);
    v->add (captionLabel ("Max notes"), 70, 28);
    v->add (makeChoiceBox (s.voicesValue, { "1", "2", "4", "8", "16", "32" }, { 1, 2, 4, 8, 16, 32 }, um), 70, 28);
    v->breakBefore();
    knob (v, s.legato, "Glide", [] (float x) { return juce::String (juce::roundToInt (x)) + " ms"; });
    knob (v, s.masterLevel, "Level", Fmt::db);
    auto* ch = add (new Box ("Character", "Built-in drive and chorus, before the track's effects."), 620, 100);
    ch->add (toggle ("Drive", s.distortionOnValue), 80, 28);
    knob (ch, s.distortion, "Drive", Fmt::pct);
    ch->add (toggle ("Chorus", s.chorusOnValue), 86, 28);
    knob (ch, s.chorusSpeed, "Speed", [] (float x) { return juce::String (x, 2) + " Hz"; });
    knob (ch, s.chorusDepth, "Depth", [] (float x) { return juce::String (x, 1) + " ms"; });
    knob (ch, s.chorusWidth, "Width", Fmt::pct);
    knob (ch, s.chorusMix, "Mix", Fmt::pct);
    hintText ("Play with a MIDI keyboard, the keys at the bottom, or your computer: A W S E D F T G Y H U J K (Z and X change octave).");
}

/** A name prompt shared by the save buttons. */
static void askForName (const juce::String& title, const juce::String& text, const juce::String& def, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, text, juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", def);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r) { if (r == 1) done (w->getTextEditorContents ("name").trim()); }), true);
}

void SoundPanel::addInstrumentChooser (te::AudioTrack& t, bool isSampler)
{
    auto id = t.itemID;
    auto builtIns = builtInPresetNames(), users = userPresetNames(), mine = session.myInstrumentNames();
    juce::StringArray names; juce::Array<int> ids; int sel = 0;
    const auto current = t.state.getProperty (IDs::sampleName).toString();
    if (isSampler)
    {
        for (int i = 0; i < builtIns.size(); ++i) { names.add ("Synth: " + builtIns[i]); ids.add (i + 1); }
        for (int i = 0; i < users.size(); ++i) { names.add ("Synth: " + users[i]); ids.add (1000 + i); }
    }
    for (int i = 0; i < mine.size(); ++i) { names.add ("My instrument: " + mine[i]); ids.add (2000 + i); if (isSampler && mine[i] == current) sel = 2000 + i; }
    if (names.isEmpty()) return;
    auto* box = add (combo (names, ids, sel, [this, id, builtIns, users, mine] (int v)
    {
        auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr) return;
        session.beginAction();
        if (v >= 2000) session.applyInstrument (*t, mine[v - 2000]);
        else session.applySynthSound (*t, v >= 1000 ? "user:" + users[v - 1000] : builtIns[v - 1]);
        rebuildNow();
    }), 280, 30);
    if (sel == 0) box->setText (isSampler ? "Change instrument..." : "Use one of my instruments...", juce::dontSendNotification);
}

void SoundPanel::buildEngine (te::AudioTrack& t)
{
    auto* eng = session.getEngine (t);
    if (eng == nullptr) return;
    auto id = t.itemID;
    auto* um = &session.undoManager();
    te::Plugin::Ptr keep (eng);
    const int type = eng->engine.get();
    const char* about[] = {
        "Layers: stack the four sounds. Blend brings them in one after another; Drift makes each one breathe at its own pace.",
        "Orbits: the four sounds sit in the corners. Drag the ring to blend them, and let the orbit move through them as you play.",
        "Threads: hold notes and three sequencer lanes play them, each with its own steps, rate and order, on its own layer. Draw each lane's velocities.",
        "Chords: one key plays a whole chord on the blended layers. Spread opens the voicing, Strum rolls it.",
        "Disperse: one key fans out into several voices that glide apart from the note you played.",
        "Lattice: hold one note and it becomes a rhythmic part, retriggered in time, with octave jumps." };
    heading ("Engine", false);
    for (int i = 0; i < 6; ++i)
    {
        auto* b = add (button (EnginePlugin::engineNames()[i], [this, keep, i] { if (auto* e = dynamic_cast<EnginePlugin*> (keep.get())) { session.undoManager().beginNewTransaction(); e->engine = i; session.sendChangeMessage(); rebuildNow(); } }), 92, 30);
        if (i == type) b->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.45f));
    }
    lineBreak();
    hintText (about[type], 1000, 36);
    lineBreak();
    auto* main = add (new Box ("Overall", "Level and how every note fades in and out."), 260, 120);
    for (auto* p : { eng->level, eng->attack, eng->release }) main->add (new ParamKnob (p, {}, um), 72, 78);

    // the engine's own controls
    auto knobs = [&] (Box* b, std::initializer_list<te::AutomatableParameter*> ps) { for (auto* p : ps) b->add (new ParamKnob (p, {}, um), 72, 78); };
    auto choice = [&] (Box* b, const juce::String& label, juce::CachedValue<int>& v, const juce::StringArray& names, int w, std::function<void()> after = nullptr)
    {
        b->add (captionLabel (label), label.length() * 7 + 8, 28);
        b->add (makeChoiceBox (v, names, {}, um, after), w, 28);
    };
    const auto title = EnginePlugin::engineNames()[type];
    switch (type)
    {
        case EnginePlugin::layersEngine: { auto* b = add (new Box (title, "Blend: from the first layer alone to all four."), 260, 120); knobs (b, { eng->blend, eng->drift }); break; }
        case EnginePlugin::orbitsEngine:
        {
            auto* b = add (new Box (title, "The ring is where the orbit centres; the dot is where it is now."), 640, 360);
            b->add (new XYPad (*eng, *um), 300, 300);
            auto* side = new Box ({}, {});
            side->add (captionLabel ("Motion"), 54, 28);
            side->add (makeChoiceBox (eng->motion, EnginePlugin::motionNames(), {}, um), 110, 28);
            side->breakBefore();
            for (auto* p : { eng->speed, eng->radius, eng->drift }) side->add (new ParamKnob (p, {}, um), 72, 78);
            b->add (side, 290, 300);
            break;
        }
        case EnginePlugin::threadsEngine:
        {
            auto* b = add (new Box (title, "Blend adds lanes 2 and 3. Divisi gives each lane its own share of the held notes."), 520, 120);
            knobs (b, { eng->blend, eng->shift, eng->humanize, eng->gate });
            choice (b, "Divisi", eng->divisi, { "Off", "On" }, 70);
            lineBreak();
            for (int l = 0; l < EnginePlugin::numLanes; ++l)
            {
                const auto layerText = eng->layerLoaded (l) ? eng->layerName (l) : eng->layerName (0);
                auto* lb = add (new Box ("Lane " + juce::String (l + 1) + ": " + layerText, {}), 900, 150);
                choice (lb, "Steps", eng->laneSteps[l], [] { juce::StringArray s; for (int i = 1; i <= 16; ++i) s.add (juce::String (i)); return s; }(), 64);
                choice (lb, "Rate", eng->laneRate[l], EnginePlugin::rateNames(), 116);
                choice (lb, "Order", eng->laneOrder[l], EnginePlugin::orderNames(), 110);
                lb->breakBefore();
                lb->add (new LaneEditor (*eng, l, *um), 860, 70);
            }
            break;
        }
        case EnginePlugin::chordsEngine:
        {
            auto* b = add (new Box (title, {}), 640, 120);
            choice (b, "Chord", eng->chordType, EnginePlugin::chordNames(), 110);
            choice (b, "Inversion", eng->inversion, { "Root", "First", "Second", "Third" }, 90);
            knobs (b, { eng->spread, eng->strum, eng->blend });
            break;
        }
        case EnginePlugin::disperseEngine:
        {
            auto* b = add (new Box (title, {}), 520, 120);
            choice (b, "Voices", eng->disperseVoices, { "2", "3", "4", "5", "6", "7", "8" }, 60);
            knobs (b, { eng->spread, eng->glide, eng->blend, eng->drift });
            break;
        }
        default:
        {
            auto* b = add (new Box (title, {}), 560, 120);
            choice (b, "Rate", eng->latticeRate, EnginePlugin::rateNames(), 116);
            knobs (b, { eng->density, eng->jumps, eng->gate, eng->blend });
            break;
        }
    }

    // the four sound layers
    lineBreak();
    heading ("Sounds", false);
    lineBreak();
    auto builtIns = builtInPresetNames(), mine = session.myInstrumentNames();
    for (int i = 0; i < EnginePlugin::numLayers; ++i)
    {
        auto* lb = add (new Box ("Layer " + juce::String (i + 1), {}), 320, 150);
        juce::StringArray names { "Empty" }; juce::Array<int> ids { 1 };
        for (int k = 0; k < builtIns.size(); ++k) { names.add ("Synth: " + builtIns[k]); ids.add (100 + k); }
        for (int k = 0; k < mine.size(); ++k) { names.add ("My instrument: " + mine[k]); ids.add (1000 + k); }
        names.add ("Load a sound file..."); ids.add (5000);
        auto* box = lb->add (combo (names, ids, 0, [this, id, i, builtIns, mine] (int v)
        {
            auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr) return;
            if (v == 5000)
            {
                chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), session.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id, i] (const juce::FileChooser& fc)
                {
                    if (auto* t = te::findAudioTrackForID (*session.edit, id)) if (fc.getResult().existsAsFile()) { session.beginAction(); session.setEngineLayer (*t, i, fc.getResult().getFullPathName()); rebuildNow(); }
                });
                return;
            }
            session.beginAction();
            session.setEngineLayer (*t, i, v == 1 ? juce::String() : v >= 1000 ? "mine:" + mine[v - 1000] : "synth:" + builtIns[v - 100]);
            rebuildNow();
        }), 290, 28);
        box->setText (eng->layerLoaded (i) ? eng->layerName (i) : juce::String ("Empty"), juce::dontSendNotification);
        lb->breakBefore();
        for (auto* p : { eng->layerLevel[i], eng->layerPan[i], eng->layerTune[i] }) lb->add (new ParamKnob (p, {}, um), 72, 78);
    }
    hintText ("Each layer can be a synth sound, one of your instruments (save any recording as an instrument from its clip menu) or a sound file.", 1000, 36);
    addArpBox (t);
}

void SoundPanel::addArpBox (te::AudioTrack& t)
{
    auto id = t.itemID;
    auto* um = &session.undoManager();
    auto* arp = session.getArp (t);
    lineBreak();
    if (arp == nullptr)
    {
        auto* box = add (new Box ("Arpeggiator", "Hold a chord and it plays the notes one after another, in time with the song. Works with clips, your keyboard and Lines."), 900, 76);
        box->add (button ("Add arpeggiator", [this, id] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.addArp (*t); rebuildNow(); } }), 160, 28);
        lineBreak();
        return;
    }
    te::Plugin::Ptr keep (arp);
    auto* box = add (new Box ("Arpeggiator", "Hold a chord and it plays the notes one after another, in time with the song. Hold keeps playing after you let go."), 900, 184);
    box->accent = arp->isEnabled() ? Col::acc : juce::Colour();
    auto* on = box->add (new juce::ToggleButton ("On"), 56, 28);
    on->setToggleState (arp->isEnabled(), juce::dontSendNotification);
    on->onClick = [this, keep, on] { session.beginAction(); keep->setEnabled (on->getToggleState()); session.sendChangeMessage(); rebuildNow(); };
    auto choice = [&] (const juce::String& label, juce::CachedValue<int>& v, const juce::StringArray& names, int w)
    {
        box->add (captionLabel (label), label.length() * 7 + 8, 28);
        box->add (makeChoiceBox (v, names, {}, um), w, 28);
    };
    choice ("Pattern", arp->mode, ArpPlugin::modeNames(), 130);
    choice ("Rate", arp->rate, ArpPlugin::rateNames(), 116);
    choice ("Octaves", arp->octaves, { "1", "2", "3", "4" }, 60);
    choice ("Velocity", arp->velMode, { "As played", "Fixed" }, 110);
    choice ("Hold", arp->hold, { "Off", "On" }, 66);
    box->breakBefore();
    for (auto* p : { arp->gate, arp->swing, arp->level }) box->add (new ParamKnob (p, {}, um), 72, 78);
    box->add (button ("Remove", [this, id] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.removeArp (*t); rebuildNow(); } }), 80, 28);
    lineBreak();
}

void SoundPanel::buildSampler (te::AudioTrack& t)
{
    auto id = t.itemID;
    auto* um = &session.undoManager();
    auto* s = session.getSampled (t);
    const bool hasSound = s != nullptr && s->hasSound();
    heading (hasSound ? "Sound: " + t.state.getProperty (IDs::sampleName, s->soundName.get()).toString() : juce::String ("No sound loaded"), false);
    add (button ("Load sound file", [this, id]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), session.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (auto* t = te::findAudioTrackForID (*session.edit, id)) if (f.existsAsFile()) { session.beginAction(); session.loadSamplerSound (*t, f, true); rebuildNow(); }
        });
    }), 150, 30);
    addInstrumentChooser (t, true);
    if (! hasSound || s == nullptr)
    {
        hintText ("Load any short sound: a voice, a hit, one note from a recording. Reel finds its musical note so it plays in tune on every key. "
                  "Or right-click an audio clip on the timeline and choose Make a playable instrument.", 900, 54);
        return;
    }
    add (button ("Save as instrument", [this, id]
    {
        auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr) return;
        askForName ("Save as an instrument", "It will appear in the Instrument list of every synth and sampler track, and under Add track.",
                    t->state.getProperty (IDs::sampleName, "My instrument").toString(),
                    [this, id] (juce::String n) { if (auto* t = te::findAudioTrackForID (*session.edit, id)) if (session.saveInstrument (*t, n)) rebuildNow(); });
    }), 160, 30)->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.35f));
    add (button ("Find its note", [this, id]
    {
        if (auto* t = te::findAudioTrackForID (*session.edit, id))
        {
            session.beginAction();
            auto r = session.findSamplerNote (*t);
            if (session.onMessage) session.onMessage (r ? "This sound is close to " + noteName (r->note) + (r->cents ? " (" + juce::String (r->cents) + " cents)" : juce::String()) + "."
                                                        : juce::String ("No clear note found, so it plays at its own pitch on C4."));
            rebuildNow();
        }
    }), 120, 30);
    te::Plugin::Ptr keep (s);
    // mode
    lineBreak();
    const int mode = s->oneShot.get();
    for (int m = 0; m < 3; ++m)
    {
        auto* b = add (button (ReelSampler::modeNames()[m], [this, id, keep, m]
        {
            auto* sp = dynamic_cast<ReelSampler*> (keep.get()); auto* t = te::findAudioTrackForID (*session.edit, id);
            if (sp == nullptr || t == nullptr) return;
            session.beginAction();
            sp->oneShot = m;
            if (m == ReelSampler::slice && sp->getSlices().size() <= 1) session.sliceSampler (*t);
            rebuildNow();
        }), 96, 30);
        if (m == mode) b->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.45f));
    }
    hintText (mode == ReelSampler::slice ? "Slice: each slice plays on its own key from C4 up, at its own pitch. Drag the markers, double-click to add one, right-click to remove one, click a slice or pad to hear it."
            : mode == ReelSampler::oneShotMode ? "One Shot: each key plays the whole sound, pitched to that key, even if you let go."
                                               : "Classic: the sound plays across the keyboard while you hold the key. Drag the white line to move the start; turn on Loop to keep long notes going.", 1000, 30);
    lineBreak();
    auto* view = add (new SampleView (*s, session, selectedSlice), 1000, 150);
    if (mode == ReelSampler::slice)
    {
        lineBreak();
        auto* sb = add (new Box ("Slicing", juce::String ((int) s->getSlices().size()) + " slices, on C4 to " + noteName (ReelSampler::firstSliceNote + (int) s->getSlices().size() - 1) + "."), 540, 120);
        for (int m = 0; m < 3; ++m)
        {
            auto* b = sb->add (button (ReelSampler::methodNames()[m], [this, id, keep, m]
            {
                auto* sp = dynamic_cast<ReelSampler*> (keep.get()); auto* t = te::findAudioTrackForID (*session.edit, id);
                if (sp == nullptr || t == nullptr) return;
                session.beginAction(); sp->sliceMethod = m; if (m != 2) session.sliceSampler (*t); rebuildNow();
            }), 86, 28);
            if (m == s->sliceMethod.get()) b->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.45f));
        }
        if (s->sliceMethod.get() == 0) sb->add (new ParamKnob (s->sensitivity, {}, um), 76, 78);
        if (s->sliceMethod.get() == 1) { sb->add (captionLabel ("Slices"), 46, 28); sb->add (makeChoiceBox (s->sliceCount, { "4", "8", "16", "32" }, {}, um), 64, 28); }
        if (s->sliceMethod.get() != 2)
            sb->add (button ("Find slices", [this, id] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.sliceSampler (*t); rebuildNow(); } }), 100, 28);
        auto* mk = add (new Box ("Play the slices", "A MIDI clip that plays every slice in its original place, ready to rearrange."), 420, 120);
        mk->add (button ("Make a MIDI clip of the slices", [this, id]
        {
            if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); if (auto* c = session.slicesToMidi (*t)) session.select (t, c); }
        }), 240, 30)->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.35f));
        lineBreak();
        const int n = (int) s->getSlices().size();
        auto* pads = add (new SlicePads (*s, session, selectedSlice), 1000, SlicePads::heightFor (n, 1000));
        pads->onSelect = [view] { view->repaint(); };
        view->onSelect = [pads] { pads->repaint(); };
    }
    // tabs
    lineBreak();
    const char* tabNames[] = { "Sample", "Pitch", "Filter", "Amp", "Mod" };
    for (int i = 0; i < 5; ++i)
    {
        auto* b = add (button (tabNames[i], [this, i] { samplerTab = i; rebuildNow(); }), 86, 30);
        if (i == samplerTab) b->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.45f));
    }
    lineBreak();
    auto knobsInto = [&] (Box* b, std::initializer_list<te::AutomatableParameter*> ps) { for (auto* p : ps) b->add (new ParamKnob (p, {}, um), 72, 78); };
    auto choiceInto = [&] (Box* b, const juce::String& label, juce::CachedValue<int>& v, const juce::StringArray& names, int w)
    { b->add (captionLabel (label), label.length() * 7 + 8, 28); b->add (makeChoiceBox (v, names, {}, um, [this] { rebuildNow(); }), w, 28); };
    switch (samplerTab)
    {
        case 0:
        {
            auto* b = add (new Box ("Sample", mode == ReelSampler::slice ? "Applies to every slice." : "Start skips the beginning of the sound; Loop keeps long notes going."), 900, 120);
            knobsInto (b, { s->level, s->transpose, s->pan, s->fadeIn, s->fadeOut });
            choiceInto (b, "Reverse", s->reverse, { "Off", "On" }, 70);
            if (mode == ReelSampler::classic)
            {
                b->breakBefore();
                knobsInto (b, { s->startAt });
                choiceInto (b, "Loop", s->loopMode, { "Off", "On" }, 70);
                if (s->loopMode.get() == 1) knobsInto (b, { s->loopStart, s->loopEnd });
            }
            else if (mode == ReelSampler::oneShotMode) knobsInto (b, { s->startAt });
            break;
        }
        case 1:
        {
            auto* b = add (new Box ("Pitch", mode == ReelSampler::slice ? "Slices keep their own pitch: Pitch and Fine tune move them all." : "The original note is the key that plays the sound at its own pitch."), 640, 120);
            if (mode != ReelSampler::slice)
                b->add (new ValueKnob ("Original note", 12, 108, 1, 60, [keep] { auto* p = dynamic_cast<ReelSampler*> (keep.get()); return p ? (double) p->root.get() : 60.0; },
                                       [this, id] (double v) { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setSamplerRoot (*t, (int) v); },
                                       [] (double v) { return noteName ((int) v); }), 80, 78);
            knobsInto (b, { s->transpose, s->fine });
            break;
        }
        case 2:
        {
            auto* b = add (new Box ("Filter", "Low pass darkens, high pass thins, band pass keeps a slice of the middle. Slide from Lines moves the cutoff."), 640, 120);
            choiceInto (b, "Type", s->filterType, ReelSampler::filterNames(), 110);
            knobsInto (b, { s->cutoff, s->resonance });
            break;
        }
        case 3:
        {
            auto* b = add (new Box ("Amp", "How each note fades in, settles and fades out."), 640, 120);
            knobsInto (b, { s->attack, s->decay, s->sustain, s->release });
            break;
        }
        default:
        {
            auto* b = add (new Box ("Mod", "A slow wobble on pitch, filter, volume or pan, plus how pressure and slide from Lines or an MPE keyboard change the sound."), 760, 120);
            choiceInto (b, "LFO to", s->lfoTarget, ReelSampler::lfoNames(), 90);
            knobsInto (b, { s->lfoRate, s->lfoDepth, s->pressureAmt, s->slideAmt });
            break;
        }
    }
    addArpBox (t);
}

void SoundPanel::buildKit (te::AudioTrack& t)
{
    auto id = t.itemID;
    auto* s = session.getSampler (t);
    heading ("Drum kit", false);
    hintText ("Each drum was synthesised for Reel. Tune, shorten and balance them here, or load your own sound for any drum. MIDI keyboards play the kit on the usual drum keys (kick on C1).", 900, 40);
    if (s == nullptr) return;
    lineBreak();
    for (int i = 0; i < juce::jmin (numDrumVoices, s->getNumSounds()); ++i)
    {
        auto* box = add (new Box (drumVoices[i].name), 300, 100);
        box->add (button ("Play", [this, id, i] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.auditionDrum (*t, i); }), 54, 28);
        auto kit = [this, id] { return te::findAudioTrackForID (*session.edit, id); };
        box->add (new ValueKnob ("Tune", -12, 12, 0.5, 0, [this, kit, i] { auto* t = kit(); return t ? (double) session.getKitValue (*t, IDs::kitTune, i, 0) : 0.0; },
                                 [this, kit, i] (double v) { if (auto* t = kit()) session.setKitValue (*t, IDs::kitTune, i, (float) v); },
                                 [] (double v) { return (v > 0 ? "+" : "") + juce::String (v, 1); }), 58, 74);
        box->add (new ValueKnob ("Length", 0.1, 1, 0.01, 1, [this, kit, i] { auto* t = kit(); return t ? (double) session.getKitValue (*t, IDs::kitLen, i, 1) : 1.0; },
                                 [this, kit, i] (double v) { if (auto* t = kit()) session.setKitValue (*t, IDs::kitLen, i, (float) v); },
                                 [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }), 58, 74);
        box->add (new ValueKnob ("Level", 0, 1, 0.01, 0.8, [this, kit, i] { auto* t = kit(); return t ? (double) session.getKitValue (*t, IDs::kitLevel, i, 0.8f) : 0.8; },
                                 [this, kit, i] (double v) { if (auto* t = kit()) session.setKitValue (*t, IDs::kitLevel, i, (float) v); },
                                 [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }), 58, 74);
        box->add (button ("Load", [this, id, i]
        {
            chooser = std::make_unique<juce::FileChooser> ("Choose a sound for this drum", juce::File(), session.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id, i] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult();
                if (auto* t = te::findAudioTrackForID (*session.edit, id)) if (f.existsAsFile()) session.setDrumSound (*t, i, f);
            });
        }), 54, 28);
        box->add (button ("Built-in", [this, id, i] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.resetDrumSound (*t, i); }), 70, 28);
    }
}

//==============================================================================
void EffectsPanel::build()
{
    auto* t = session.getSelectedTrack();
    if (t == nullptr) { hintText ("Select a track to build its effect chain."); return; }
    auto id = t->itemID;
    heading ("Effects on " + t->getName(), false);
    auto* addBox = add (new juce::ComboBox(), 230, 30);
    addBox->setTextWhenNothingSelected ("Add effect...");
    for (int i = 0; i < effectSlots().size(); ++i) addBox->addItem (effectSlots()[i].title, i + 1);
    addBox->onChange = [this, id, addBox]
    {
        const int k = addBox->getSelectedId(); if (k <= 0) return;
        if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.addEffect (*t, effectSlots()[k - 1].type); }
    };
    hintText ("Sound flows through the chain in order. Use the arrows to reorder. Automate opens a lane under the track where you click points "
              "to change any setting over time, for example an effect only in the chorus.", 900, 40);
    auto chain = session.effectChain (*t);
    if (chain.isEmpty()) { hintText ("No effects yet. Choose one from Add effect: EQ, compressor, saturation, distortion, phaser, echo, reverb or vocoder."); return; }
    lineBreak();
    for (int i = 0; i < chain.size(); ++i)
    {
        te::Plugin::Ptr pp (chain[i]);
        juce::String blurb; for (auto& e : effectSlots()) if (pp->getPluginType() == e.type) blurb = e.blurb;
        auto* box = new Box (juce::String (i + 1) + ".  " + effectTitle (*pp), blurb);
        box->dimmed = ! pp->isEnabled();
        box->accent = pp->isEnabled() ? Col::acc : juce::Colour();
        auto* on = box->add (new juce::ToggleButton ("On"), 56, 26);
        on->setToggleState (pp->isEnabled(), juce::dontSendNotification);
        on->onClick = [this, pp, on, box] { session.beginAction(); pp->setEnabled (on->getToggleState()); box->dimmed = ! on->getToggleState(); box->accent = on->getToggleState() ? Col::acc : juce::Colour(); box->repaint(); };
        box->add (button ("Earlier", [this, id, pp] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.moveEffect (*t, *pp, -1); }), 70, 26)->setEnabled (i > 0);
        box->add (button ("Later", [this, id, pp] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.moveEffect (*t, *pp, 1); }), 60, 26)->setEnabled (i < chain.size() - 1);
        box->add (button ("Automate...", [this, id, pp]
        {
            juce::PopupMenu m; auto params = pp->getAutomatableParameters();
            for (int k = 0; k < params.size(); ++k) m.addItem (k + 1, params[k]->getParameterName() + (params[k]->hasAutomationPoints() ? "  (automated)" : ""));
            m.showMenuAsync (juce::PopupMenu::Options(), [this, id, pp, params] (int r)
            {
                auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr || r <= 0) return;
                session.showAutomation (*t, Session::automationKey (*pp, *params[r - 1]));
                if (session.onMessage) session.onMessage ("Click in the lane under " + t->getName() + " to add points for " + params[r - 1]->getParameterName() + ".");
            });
        }), 96, 26);
        box->add (button ("Remove", [this, pp] { session.removeEffect (*pp); }), 76, 26);
        const int w = buildEffectEditor (*box, session, *pp);
        add (box, w, 100);
    }
}

//==============================================================================
namespace
{
    class VolumeFader : public juce::Component, private juce::Timer
    {
    public:
        VolumeFader (te::VolumeAndPanPlugin& v, juce::UndoManager* u) : vol (&v), um (u)
        {
            slider.setSliderStyle (juce::Slider::LinearVertical);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange (0.0, 1.0); slider.setValue (v.getSliderPos(), juce::dontSendNotification);
            slider.setDoubleClickReturnValue (true, te::decibelsToVolumeFaderPosition (0.0f));
            slider.setScrollWheelEnabled (false);
            slider.onDragStart = [this] { if (um) um->beginNewTransaction(); };
            slider.onValueChange = [this] { vol->setSliderPos ((float) slider.getValue()); update(); };
            addAndMakeVisible (slider);
            db.setJustificationType (juce::Justification::centred); db.setFont (juce::FontOptions (13.0f, juce::Font::bold)); addAndMakeVisible (db);
            update(); startTimerHz (10);
        }
        void resized() override { auto r = getLocalBounds(); db.setBounds (r.removeFromBottom (18)); slider.setBounds (r); }
    private:
        void update() { auto d = vol->getVolumeDb(); db.setText (d < -90 ? juce::String ("-inf dB") : juce::String (d, 1) + " dB", juce::dontSendNotification); }
        void timerCallback() override { if (! slider.isMouseButtonDown() && std::abs (slider.getValue() - vol->getSliderPos()) > 1e-4) { slider.setValue (vol->getSliderPos(), juce::dontSendNotification); update(); } }
        te::VolumeAndPanPlugin::Ptr vol; juce::UndoManager* um; juce::Slider slider; juce::Label db;
    };

    class Strip : public juce::Component
    {
    public:
        Strip (Session& s, te::AudioTrack* t) : session (s), id (t ? t->itemID : te::EditItemID())
        {
            auto* vol = t ? t->getVolumePlugin() : s.edit->getMasterVolumePlugin().get();
            colour = t ? t->getColour() : Col::acc;
            name.setText (t ? t->getName() : juce::String ("Main output"), juce::dontSendNotification);
            name.setJustificationType (juce::Justification::centred); name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            addAndMakeVisible (name);
            if (vol != nullptr)
            {
                pan = std::make_unique<ParamKnob> (vol->panParam, "Pan", &s.undoManager()); addAndMakeVisible (*pan);
                fader = std::make_unique<VolumeFader> (*vol, &s.undoManager()); addAndMakeVisible (*fader);
            }
            if (t != nullptr)
            {
                if (auto* lm = t->getLevelMeterPlugin()) { meter = std::make_unique<LevelMeter> (*lm); addAndMakeVisible (*meter); }
                for (auto* b : { &mute, &solo }) { b->setClickingTogglesState (true); addAndMakeVisible (*b); }
                mute.setToggleState (t->isMuted (false), juce::dontSendNotification); solo.setToggleState (t->isSolo (false), juce::dontSendNotification);
                mute.onClick = [this] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); t->setMute (mute.getToggleState()); } };
                solo.onClick = [this] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); t->setSolo (solo.getToggleState()); } };
            }
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool sel = id.isValid() && id == session.selectedTrack;
            g.setColour (Col::panel2); g.fillRoundedRectangle (r, 9.0f);
            g.setColour (sel ? Col::acc : Col::line2); g.drawRoundedRectangle (r, 9.0f, 1.0f);
            g.setColour (colour); g.fillRoundedRectangle (r.removeFromTop (6), 3.0f);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 8); r.removeFromTop (2);
            name.setBounds (r.removeFromTop (20));
            if (pan) pan->setBounds (r.removeFromTop (74).withSizeKeepingCentre (62, 74));
            if (id.isValid()) { auto ms = r.removeFromBottom (26); mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0)); solo.setBounds (ms.reduced (2, 0)); r.removeFromBottom (4); }
            if (meter) meter->setBounds (r.removeFromRight (10).withTrimmedBottom (18));
            if (fader) fader->setBounds (r);
        }
        void mouseDown (const juce::MouseEvent&) override { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.select (t); }
    private:
        Session& session; te::EditItemID id; juce::Colour colour; juce::Label name;
        std::unique_ptr<ParamKnob> pan; std::unique_ptr<VolumeFader> fader; std::unique_ptr<LevelMeter> meter;
        juce::TextButton mute { "M" }, solo { "S" };
    };
}

void MixerPanel::build()
{
    for (auto* t : session.tracks()) add (new Strip (session, t), 96, 300);
    add (new Strip (session, nullptr), 96, 300);
}

void MixerPanel::layout()
{
    int x = 12;
    for (auto* c : items) { c->setBounds (x, 10, 96, juce::jmax (220, getHeight() - 20)); x += 104; }
}
}
