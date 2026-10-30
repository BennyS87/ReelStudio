#include "Panels.h"

namespace Reel
{
//==============================================================================
/** A titled, rounded section that flows its children. */
class Box : public juce::Component
{
public:
    Box (juce::String t, juce::String d = {}) : title (std::move (t)), desc (std::move (d)) {}
    template <typename T> T* add (T* c, int w, int h) { kids.add (c); c->setSize (w, h); addAndMakeVisible (c); return c; }
    int heightFor (int w)
    {
        int x = 12, y = top(), lineH = 0;
        for (auto* c : kids) { if (x + c->getWidth() > w - 10 && x > 12) { x = 12; y += lineH + 6; lineH = 0; } x += c->getWidth() + 6; lineH = juce::jmax (lineH, c->getHeight()); }
        return y + lineH + 12;
    }
    void resized() override
    {
        int x = 12, y = top(), lineH = 0;
        for (auto* c : kids) { if (x + c->getWidth() > getWidth() - 10 && x > 12) { x = 12; y += lineH + 6; lineH = 0; } c->setTopLeftPosition (x, y); x += c->getWidth() + 6; lineH = juce::jmax (lineH, c->getHeight()); }
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Col::panel2); g.fillRoundedRectangle (r, 9.0f);
        g.setColour (Col::line2); g.drawRoundedRectangle (r, 9.0f, 1.0f);
        g.setColour (dimmed ? Col::muted : Col::text); g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (title, 12, 8, getWidth() - 120, 20, juce::Justification::left);
        if (desc.isNotEmpty()) { g.setColour (Col::muted); g.setFont (juce::FontOptions (12.5f)); g.drawFittedText (desc, 12, 30, getWidth() - 24, 32, juce::Justification::topLeft, 2); }
    }
    int top() const { return desc.isNotEmpty() ? 64 : 34; }
    juce::String title, desc; bool dimmed = false;
    juce::OwnedArray<juce::Component> kids;
};

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
            hintText ("Audio tracks play recordings, so their sound is the file itself. Shape it in the Editor tab (speed, pitch, reverse, fades) "
                      "and the Effects tab. To play this sound from a keyboard, right-click a clip and choose Make a playable instrument.", 900, 54);
            break;
    }
}

void SoundPanel::buildSynth (te::AudioTrack& t, te::FourOscPlugin& s)
{
    auto* um = &session.undoManager();
    auto id = t.itemID;
    const auto current = t.state.getProperty (IDs::preset, "Soft keys").toString();
    juce::StringArray names; juce::Array<int> ids; int sel = 0;
    auto builtIns = builtInPresetNames(), users = userPresetNames();
    for (int i = 0; i < builtIns.size(); ++i) { names.add (builtIns[i]); ids.add (i + 1); if (builtIns[i] == current) sel = i + 1; }
    for (int i = 0; i < users.size(); ++i) { names.add ("Yours: " + users[i]); ids.add (1000 + i); if ("user:" + users[i] == current) sel = 1000 + i; }
    heading ("Sound", false);
    auto* presetBox = add (combo (names, ids, sel, [this, id, builtIns, users] (int v)
    {
        if (auto* t = te::findAudioTrackForID (*session.edit, id))
        { session.beginAction(); session.setSynthPreset (*t, v >= 1000 ? "user:" + users[v - 1000] : builtIns[v - 1]); }
    }), 220, 30);
    if (sel == 0) presetBox->setText ("Custom sound", juce::dontSendNotification);
    add (button ("Save as my sound", [this, id]
    {
        auto* w = new juce::AlertWindow ("Save this sound", "It will appear in the Sound list for every synth track.", juce::MessageBoxIconType::NoIcon);
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

    const juce::StringArray waves { "Off", "Sine", "Triangle", "Saw", "Square", "Noise" };
    const juce::Array<int> waveIds { 1, 2, 5, 4, 3, 6 };   // Oscillator::Waves value + 1
    lineBreak();
    for (int i = 0; i < juce::jmin (4, s.oscParams.size()); ++i)
    {
        auto& o = *s.oscParams[i];
        auto* box = add (new Box ("Tone " + juce::String (i + 1)), 330, 100);
        auto* wave = box->add (combo (waves, waveIds, o.waveShapeValue.get() + 1, [this, &s, i] (int v) { session.beginAction(); s.oscParams[i]->waveShapeValue = v - 1; }), 92, 28);
        juce::ignoreUnused (wave);
        box->add (new ParamKnob (o.tune, "Pitch", um), 62, 78);
        box->add (new ParamKnob (o.level, "Level", um), 62, 78);
        box->add (new ParamKnob (o.detune, "Spread", um), 62, 78);
    }
    auto* f = add (new Box ("Filter", "Brightness and how it sweeps when you press a key."), 620, 100);
    f->add (combo ({ "Off", "Low pass", "High pass", "Band pass" }, { 1, 2, 3, 4 }, s.filterTypeValue.get() + 1, [this, &s] (int v) { session.beginAction(); s.filterTypeValue = v - 1; }), 110, 28);
    for (auto [p, n] : { std::pair<te::AutomatableParameter*, const char*> { s.filterFreq.get(), "Brightness" }, { s.filterResonance.get(), "Resonance" }, { s.filterAmount.get(), "Sweep" },
                        { s.filterAttack.get(), "Sweep in" }, { s.filterDecay.get(), "Sweep fall" }, { s.filterSustain.get(), "Sweep hold" }, { s.filterRelease.get(), "Sweep out" } })
        f->add (new ParamKnob (p, n, um), 66, 78);
    auto* a = add (new Box ("Volume shape"), 420, 100);
    for (auto [p, n] : { std::pair<te::AutomatableParameter*, const char*> { s.ampAttack.get(), "Attack" }, { s.ampDecay.get(), "Decay" }, { s.ampSustain.get(), "Sustain" },
                        { s.ampRelease.get(), "Release" }, { s.ampVelocity.get(), "Touch" }, { s.masterLevel.get(), "Level" } })
        a->add (new ParamKnob (p, n, um), 62, 78);
    hintText ("Play with a MIDI keyboard, the keys at the bottom, or your computer: A W S E D F T G Y H U J K (Z and X change octave). Changes apply to the next note.");
}

void SoundPanel::buildSampler (te::AudioTrack& t)
{
    auto id = t.itemID;
    auto* s = session.getSampler (t);
    const bool hasSound = s != nullptr && s->getNumSounds() > 0;
    heading (hasSound ? "Sound: " + t.state.getProperty (IDs::sampleName, "loaded").toString() : juce::String ("No sound loaded"), false);
    add (button ("Load sound file", [this, id]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), session.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, id] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (auto* t = te::findAudioTrackForID (*session.edit, id)) if (f.existsAsFile()) { session.beginAction(); session.loadSamplerSound (*t, f, true); rebuildNow(); }
        });
    }), 150, 30);
    if (! hasSound)
    {
        hintText ("Load any short sound: a voice, a hit, one note from a recording. Reel finds its musical note so it plays in tune on every key. "
                  "Or right-click an audio clip on the timeline and choose Make a playable instrument.", 900, 54);
        return;
    }
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
    auto* box = add (new Box ("Playing", "The original note is the key that plays the sound at its own pitch. Every other key shifts it."), 560, 120);
    box->add (new ValueKnob ("Original note", 12, 108, 1, 60, [s] { return (double) s->getKeyNote (0); },
                             [this, id] (double v) { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setSamplerRoot (*t, (int) v); },
                             [] (double v) { return noteName ((int) v); }), 80, 78);
    box->add (new ValueKnob ("Level", -30, 6, 0.1, 0, [s] { return (double) s->getSoundGainDb (0); },
                             [s] (double v) { s->setSoundGains (0, (float) v, s->getSoundPan (0)); },
                             [] (double v) { return juce::String (v, 1) + " dB"; }), 70, 78);
    box->add (new ValueKnob ("Pan", -1, 1, 0.01, 0, [s] { return (double) s->getSoundPan (0); },
                             [s] (double v) { s->setSoundGains (0, s->getSoundGainDb (0), (float) v); },
                             [] (double v) { return std::abs (v) < 0.01 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100)); }), 62, 78);
    auto* whole = box->add (new juce::ToggleButton ("Play the whole sound on any tap"), 260, 30);
    whole->setToggleState (s->isSoundOpenEnded (0), juce::dontSendNotification);
    whole->onClick = [this, s, whole] { session.beginAction(); s->setSoundOpenEnded (0, whole->getToggleState()); };
    hintText ("Play it on your MIDI keyboard, the keys at the bottom, or A to K on your computer. Record what you play with the red button.");
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
    if (t == nullptr) { hintText ("Select a track to add distortion, echo, reverb or the vocoder."); return; }
    auto id = t->itemID;
    auto* um = &session.undoManager();
    hintText ("Effects shape the whole track. To use one only at certain moments, or change it over time, press Automate and click points in the lane "
              "that opens under the track. A point at zero turns the effect off at that moment.", 900, 40);
    lineBreak();
    for (auto& slot : effectSlots())
    {
        const juce::String type = slot.type;
        auto* p = session.getEffect (*t, type);
        const bool on = p != nullptr && p->isEnabled();
        auto* box = add (new Box (slot.title, slot.blurb), 330, 160);
        box->dimmed = ! on;
        auto* tog = box->add (new juce::ToggleButton (on ? "On" : "Off"), 70, 26);
        tog->setToggleState (on, juce::dontSendNotification);
        tog->onClick = [this, id, type, tog] { if (auto* t = te::findAudioTrackForID (*session.edit, id)) session.setEffectOn (*t, type, tog->getToggleState()); rebuildNow(); };
        if (p == nullptr) { auto* l = box->add (new juce::Label ({}, "Turn it on to adjust it."), 200, 26); styleHint (*l); continue; }
        auto automate = [&] (te::AutomatableParameter* ap)
        {
            const auto key = type + "/" + ap->paramID;
            auto* b = box->add (button (ap->hasAutomationPoints() ? "Automated" : "Automate", [this, id, key]
            {
                if (auto* t = te::findAudioTrackForID (*session.edit, id))
                {
                    session.showAutomation (*t, key);
                    if (session.onMessage) session.onMessage ("Click in the lane under " + t->getName() + " to add points, then drag them.");
                }
            }), 92, 24);
            if (ap->hasAutomationPoints()) b->setColour (juce::TextButton::textColourOffId, Col::acc);
        };
        auto knob = [&] (te::AutomatableParameter* ap, const char* label, bool canAutomate)
        {
            box->add (new ParamKnob (ap, label, um), 66, 78);
            juce::ignoreUnused (canAutomate);
        };
        if (auto* d = dynamic_cast<DistortionPlugin*> (p))
        {
            box->add (combo ({ "Overdrive", "Fuzz", "Bit crush" }, { 1, 2, 3 }, d->typeValue.get() + 1, [this, d] (int v) { session.beginAction(); d->typeValue = v - 1; }), 110, 26);
            knob (d->drive.get(), "Drive", true); knob (d->tone.get(), "Tone", false); knob (d->mix.get(), "Amount", true);
            automate (d->mix.get()); automate (d->drive.get());
        }
        else if (auto* v = dynamic_cast<VocoderPlugin*> (p))
        {
            box->add (combo ({ "Single note", "Chord", "Whisper" }, { 1, 2, 3 }, v->carrierValue.get() + 1, [this, v] (int x) { session.beginAction(); v->carrierValue = x - 1; }), 110, 26);
            juce::StringArray notes; juce::Array<int> ids; for (int n = 36; n <= 72; ++n) { notes.add (noteName (n)); ids.add (n); }
            box->add (combo (notes, ids, v->noteValue.get(), [this, v] (int x) { session.beginAction(); v->noteValue = x; }), 80, 26);
            knob (v->breath.get(), "Breath", false); knob (v->level.get(), "Output", false); knob (v->mix.get(), "Amount", true);
            automate (v->mix.get());
        }
        else if (auto* e = dynamic_cast<te::DelayPlugin*> (p))
        {
            const double divs[] = { 0, 0.25, 0.5, 0.75, 1, 1.5, 2 };
            box->add (combo ({ "Free time", "1/16", "1/8", "Dotted 1/8", "1/4", "Dotted 1/4", "1/2" }, { 1, 2, 3, 4, 5, 6, 7 }, 1,
                             [this, id, divs] (int x) { if (auto* t = te::findAudioTrackForID (*session.edit, id)) { session.beginAction(); session.setEchoTiming (*t, divs[x - 1]); } }), 120, 26);
            box->add (new ValueKnob ("Time", 20, 1500, 1, 375, [e] { return (double) e->lengthMs.get(); }, [e] (double v) { e->lengthMs = (int) v; },
                                     [] (double v) { return juce::String ((int) v) + " ms"; }, 0.5), 66, 78);
            knob (e->feedbackDb.get(), "Repeats", true); knob (e->mixProportion.get(), "Amount", true);
            automate (e->mixProportion.get()); automate (e->feedbackDb.get());
        }
        else if (auto* r = dynamic_cast<te::ReverbPlugin*> (p))
        {
            knob (r->roomSizeParam.get(), "Size", false); knob (r->dampParam.get(), "Darkness", false); knob (r->wetParam.get(), "Amount", true);
            automate (r->wetParam.get());
        }
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
                if (auto* lm = t->getLevelMeterPlugin()) { meter = std::make_unique<LevelMeter> (lm->measurer); addAndMakeVisible (*meter); }
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
