#include "Panels.h"
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
        if (auto* b = dynamic_cast<Box*> (c)) c->setSize (c->getWidth(), b->heightFor (c->getWidth()));
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
        if (auto* b = dynamic_cast<Box*> (c)) c->setSize (c->getWidth(), b->heightFor (c->getWidth()));
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
    addArpBox (t);
    te::Plugin::Ptr keep (s);
    auto* play = add (new Box ("Playing", "The original note is the key that plays the sound at its own pitch; every other key shifts it, and Lines can glide it."), 640, 120);
    play->add (new ValueKnob ("Original note", 12, 108, 1, 60, [keep] { auto* p = dynamic_cast<ReelSampler*> (keep.get()); return p ? (double) p->root.get() : 60.0; },
                              [this, id] (double v) { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setSamplerRoot (*t, (int) v); },
                              [] (double v) { return noteName ((int) v); }), 80, 78);
    for (auto* p : { s->fine, s->level, s->pan }) play->add (new ParamKnob (p, {}, um), 68, 78);
    play->add (captionLabel ("Play"), 34, 28);
    play->add (makeChoiceBox (s->oneShot, { "While held", "Whole sound" }, {}, um), 120, 28);
    auto* env = add (new Box ("Envelope", "How each note fades in, settles and fades out."), 330, 120);
    for (auto* p : { s->attack, s->decay, s->sustain, s->release }) env->add (new ParamKnob (p, {}, um), 68, 78);
    auto* tone = add (new Box ("Tone and expression", "Pressure and slide come from Lines or an MPE keyboard."), 260, 120);
    for (auto* p : { s->cutoff, s->pressureAmt, s->slideAmt }) tone->add (new ParamKnob (p, {}, um), 72, 78);
    auto* lp = add (new Box ("Start and loop", "Skip the start of the sound, or loop part of it so long notes keep going."), 420, 120);
    lp->add (new ParamKnob (s->startAt, {}, um), 64, 78);
    lp->add (captionLabel ("Loop"), 36, 28);
    lp->add (makeChoiceBox (s->loopMode, { "Off", "On" }, {}, um), 76, 28);
    for (auto* p : { s->loopStart, s->loopEnd }) lp->add (new ParamKnob (p, {}, um), 64, 78);
    hintText ("Play it on your MIDI keyboard, the keys at the bottom, or A to K on your computer. Add Lines in the Lines tab to draw glides with this sound.");
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
