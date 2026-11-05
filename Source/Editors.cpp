#include "Editors.h"

namespace Reel
{
static bool isBlack (int n) { const int k = ((n % 12) + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }

//==============================================================================
PianoRoll::PianoRoll (Session& s, te::MidiClip& c) : session (s), clipID (c.itemID) { startTimerHz (25); }

te::MidiClip* PianoRoll::clip() const { return dynamic_cast<te::MidiClip*> (te::findClipForID (*session.edit, clipID)); }

bool PianoRoll::isValid (te::MidiNote* n) const
{
    auto* c = clip(); return c != nullptr && n != nullptr && c->getSequence().getNotes().contains (n);
}

juce::Array<te::MidiNote*> PianoRoll::targets() const
{
    juce::Array<te::MidiNote*> out;
    for (auto* n : selected) if (isValid (n)) out.add (n);
    if (out.isEmpty()) if (auto* c = clip()) out.addArray (c->getSequence().getNotes());
    return out;
}

te::MidiNote* PianoRoll::noteAt (juce::Point<int> p) const
{
    auto* c = clip(); if (c == nullptr) return nullptr;
    const int pitch = noteAtY (p.y); const double b = beatAtX (p.x);
    auto& notes = c->getSequence().getNotes();
    for (int i = notes.size(); --i >= 0;)
    {
        auto* n = notes.getUnchecked (i);
        if (n->getNoteNumber() == pitch && b >= n->getStartBeat().inBeats() && b <= n->getEndBeat().inBeats()) return n;
    }
    return nullptr;
}

void PianoRoll::audition (int note, int vel)
{
    session.keyboardNote (note, vel / 127.0f, true);
    juce::Timer::callAfterDelay (170, [this, note, sp = juce::Component::SafePointer<PianoRoll> (this)] { if (sp != nullptr) session.keyboardNote (note, 0, false); });
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto* c = clip(); if (c == nullptr) return;
    const auto colour = c->getTrack() ? c->getTrack()->getColour() : Col::acc;
    if (scrollY < 0)
    {
        double avg = 0; auto& ns = c->getSequence().getNotes();
        for (auto* n : ns) avg += n->getNoteNumber();
        avg = ns.isEmpty() ? (getKind (*c->getTrack()) == Kind::drums ? 42 : 60) : avg / ns.size();
        scrollY = juce::jmax (0.0f, (float) ((127 - avg) * rowH - (getHeight() - rulerH) / 2.0));
        scrollBeat = c->getOffsetInBeats().inBeats();
    }
    scrollY = juce::jlimit (0.0f, juce::jmax (0.0f, 128 * rowH - (getHeight() - rulerH)), scrollY);
    const double off = c->getOffsetInBeats().inBeats(), len = c->getLengthInBeats().inBeats();
    const int top = noteAtY (rulerH), bottom = noteAtY (getHeight());
    for (int n = bottom; n <= top; ++n)
    {
        g.setColour (isBlack (n) ? Col::lane2 : Col::lane); g.fillRect (keysW, yForNote (n), getWidth(), (int) rowH);
        if (n % 12 == 0) { g.setColour (Col::line); g.fillRect (keysW, yForNote (n) + (int) rowH - 1, getWidth(), 1); }
    }
    const double b0 = std::floor (beatAtX (keysW)), b1 = beatAtX (getWidth());
    const double sub = grid > 0 && grid < 1 && grid * pxPerBeat >= 10 ? grid : 1.0;
    for (double b = juce::jmax (0.0, b0); b <= b1; b += sub)
    {
        const bool bar = std::fmod (b - off + 1e-6, 4.0) < 2e-6, beat = std::fmod (b + 1e-6, 1.0) < 2e-6;
        g.setColour (bar ? Col::line : beat ? Col::line2 : Col::line2.withAlpha (0.5f));
        g.fillRect (xForBeat (b), rulerH, 1, getHeight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (juce::Rectangle<int>::leftTopRightBottom (keysW, rulerH, juce::jmax (keysW, xForBeat (off)), getHeight()));
    g.fillRect (juce::Rectangle<int>::leftTopRightBottom (juce::jmax (keysW, xForBeat (off + len)), rulerH, getWidth(), getHeight()));
    g.setColour (Col::acc); g.fillRect (xForBeat (off + len) - 1, rulerH, 2, getHeight());

    for (auto* n : c->getSequence().getNotes())
    {
        const int x = xForBeat (n->getStartBeat().inBeats()), y = yForNote (n->getNoteNumber());
        const int w = juce::jmax (3, juce::roundToInt (n->getLengthBeats().inBeats() * pxPerBeat) - 1);
        if (x + w < keysW || x > getWidth() || y + rowH < rulerH || y > getHeight()) continue;
        const bool inside = n->getStartBeat().inBeats() < off + len && n->getEndBeat().inBeats() > off;
        g.setColour (colour.withAlpha ((inside ? 1.0f : 0.3f) * (0.45f + 0.55f * n->getVelocity() / 127.0f)));
        g.fillRoundedRectangle ((float) x, (float) y + 1, (float) w, rowH - 2, 2.0f);
        if (selected.contains (n)) { g.setColour (Col::acc); g.drawRoundedRectangle ((float) x + 1, (float) y + 2, (float) w - 2, rowH - 4, 2.0f, 2.0f); }
        if (rowH >= 12 && w > 28) { g.setColour (juce::Colour (0xcc10181c)); g.setFont (juce::FontOptions (10.0f)); g.drawText (noteName (n->getNoteNumber()), x + 3, y, w - 4, (int) rowH, juce::Justification::centredLeft); }
    }
    if (! box.isEmpty()) { g.setColour (Col::acc); g.drawRect (box, 1); }
    auto& tr = session.transport();
    const double pb = session.toBeats (tr.getPosition()).inBeats() - session.toBeats (c->getPosition().getStart()).inBeats() + off;
    if (pb >= off && pb <= off + len) { g.setColour (Col::acc); g.fillRect (xForBeat (pb) - 1, rulerH, 2, getHeight()); }

    const bool drums = getKind (*c->getTrack()) == Kind::drums;
    g.setColour (Col::panel); g.fillRect (0, rulerH, keysW, getHeight());
    for (int n = bottom; n <= top; ++n)
    {
        const int y = yForNote (n);
        g.setColour (juce::Colour (0xfff4f7f8)); g.fillRect (0, y, keysW, (int) rowH - 1);
        if (isBlack (n)) { g.setColour (juce::Colour (0xff1c262c)); g.fillRect (0, y, (int) (keysW * 0.62f), (int) rowH); }
        juce::String label;
        if (drums) { for (auto& v : drumVoices) if (v.note == n) label = v.name; }
        else if (n % 12 == 0) label = noteName (n);
        if (label.isNotEmpty() && rowH >= 9) { g.setColour (isBlack (n) ? juce::Colour (0xffd0dbe0) : juce::Colour (0xff45555d)); g.setFont (juce::FontOptions (9.5f)); g.drawText (label, 3, y, keysW - 4, (int) rowH, juce::Justification::centredLeft); }
    }
    g.setColour (Col::panel); g.fillRect (0, 0, getWidth(), rulerH);
    g.setColour (Col::line); g.fillRect (0, rulerH - 1, getWidth(), 1);
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    for (double b = std::ceil ((b0 - off) / 4.0) * 4.0 + off; b <= b1; b += 4.0)
    {
        const int x = xForBeat (b); if (x < keysW) continue;
        g.setColour (Col::muted); g.fillRect (x, 0, 1, rulerH);
        g.setColour (Col::text); g.drawText ("Bar " + juce::String (juce::roundToInt ((b - off) / 4.0) + 1), x + 4, 2, 60, rulerH - 4, juce::Justification::left);
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    auto* c = clip(); if (c == nullptr) return;
    const auto p = e.getPosition();
    created = false; mode = Mode::none;
    if (p.y < rulerH) { session.seek (c->getPosition().getStart() + (session.toTime (te::BeatPosition::fromBeats (juce::jmax (0.0, beatAtX (p.x) - c->getOffsetInBeats().inBeats()))) - session.toTime (te::BeatPosition()))); return; }
    if (p.x < keysW) { const int n = noteAtY (p.y); session.keyboardNote (n, 0.8f, true); lastAuditioned = n; return; }
    auto* hit = noteAt (p);
    auto& um = session.undoManager();
    if (e.mods.isPopupMenu()) { if (hit) { session.beginAction(); selected.removeFirstMatchingValue (hit); c->getSequence().removeNote (*hit, &um); repaint(); } return; }
    if (hit == nullptr && e.mods.isShiftDown()) { mode = Mode::box; box = { p.x, p.y, 0, 0 }; return; }
    session.beginAction();
    if (hit != nullptr)
    {
        if (e.mods.isShiftDown()) { if (selected.contains (hit)) selected.removeFirstMatchingValue (hit); else selected.add (hit); }
        else if (! selected.contains (hit)) { selected.clearQuick(); selected.add (hit); }
        const int edgeX = xForBeat (hit->getEndBeat().inBeats());
        mode = edgeX - p.x < juce::jmin (8, juce::roundToInt (hit->getLengthBeats().inBeats() * pxPerBeat / 3)) ? Mode::resize : Mode::move;
        anchor = hit;
        if (mode == Mode::move) audition (hit->getNoteNumber(), hit->getVelocity());
    }
    else
    {
        selected.clearQuick();
        const double b = beatAtX (p.x), start = juce::jmax (0.0, grid > 0 ? std::floor (b / grid) * grid : b);
        anchor = c->getSequence().addNote (noteAtY (p.y), te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (lastLen), 100, 0, &um);
        selected.add (anchor); mode = Mode::resize; created = true; audition (anchor->getNoteNumber());
    }
    orig.clear();
    for (auto* n : selected) if (isValid (n)) orig.push_back ({ n, n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), n->getNoteNumber() });
    downBeat = beatAtX (p.x); downNote = noteAtY (p.y); lastAuditioned = anchor ? anchor->getNoteNumber() : -1;
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    auto* c = clip(); if (c == nullptr) return;
    const auto p = e.getPosition();
    if (mode == Mode::box) { box = juce::Rectangle<int> (e.getMouseDownPosition(), p); repaint(); return; }
    if (mode == Mode::none) return;
    auto& um = session.undoManager();
    const double q = e.mods.isAltDown() ? 0.0 : grid, db = beatAtX (p.x) - downBeat, dq = q > 0 ? std::round (db / q) * q : db;
    if (mode == Mode::move)
    {
        const int dn = noteAtY (p.y) - downNote;
        for (auto& o : orig) if (isValid (o.n))
        {
            o.n->setStartAndLength (te::BeatPosition::fromBeats (juce::jmax (0.0, o.start + dq)), te::BeatDuration::fromBeats (o.len), &um);
            o.n->setNoteNumber (juce::jlimit (0, 127, o.pitch + dn), &um);
        }
        if (isValid (anchor) && anchor->getNoteNumber() != lastAuditioned) { lastAuditioned = anchor->getNoteNumber(); audition (lastAuditioned, anchor->getVelocity()); }
    }
    else
    {
        const double minLen = q > 0 ? q : 1.0 / 32.0;
        for (auto& o : orig) if (isValid (o.n))
        {
            double l = created && q > 0 ? std::ceil ((beatAtX (p.x) - o.start) / q - 1e-6) * q : o.len + dq;
            o.n->setStartAndLength (te::BeatPosition::fromBeats (o.start), te::BeatDuration::fromBeats (juce::jmax (minLen, l)), &um);
        }
    }
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (lastAuditioned >= 0 && mode == Mode::none) session.keyboardNote (lastAuditioned, 0, false);
    if (mode == Mode::box)
    {
        if (auto* c = clip())
            for (auto* n : c->getSequence().getNotes())
            {
                auto r = juce::Rectangle<int>::leftTopRightBottom (xForBeat (n->getStartBeat().inBeats()), yForNote (n->getNoteNumber()), xForBeat (n->getEndBeat().inBeats()), yForNote (n->getNoteNumber()) + (int) rowH);
                if (r.intersects (box)) selected.addIfNotAlreadyThere (n);
            }
        box = {};
    }
    if ((mode == Mode::resize || created) && isValid (anchor)) lastLen = anchor->getLengthBeats().inBeats();
    mode = Mode::none; repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    auto p = e.getPosition();
    auto cursor = p.x < keysW ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor;
    if (auto* n = noteAt (p)) cursor = xForBeat (n->getEndBeat().inBeats()) - p.x < 8 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
    setMouseCursor (cursor);
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto* n = noteAt (e.getPosition()))
        if (auto* c = clip()) { session.beginAction(); selected.removeFirstMatchingValue (n); c->getSequence().removeNote (*n, &session.undoManager()); repaint(); }
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { mouseMagnify (e, (float) std::exp (w.deltaY * 1.5)); return; }
    float dx = w.deltaX, dy = w.deltaY; if (e.mods.isShiftDown()) { dx = dy; dy = 0; }
    scrollBeat = juce::jmax (0.0, scrollBeat - dx * 300.0 / pxPerBeat);
    scrollY -= dy * 300.0f;
    repaint();
}

void PianoRoll::mouseMagnify (const juce::MouseEvent& e, float s)
{
    const double b = beatAtX (e.x);
    pxPerBeat = juce::jlimit (12.0, 400.0, pxPerBeat * s);
    scrollBeat = juce::jmax (0.0, b - (e.x - keysW) / pxPerBeat);
    repaint();
}

bool PianoRoll::deleteSelected()
{
    auto* c = clip(); if (c == nullptr) return false;
    juce::Array<te::MidiNote*> del; for (auto* n : selected) if (isValid (n)) del.add (n);
    if (del.isEmpty()) return false;
    session.beginAction();
    for (auto* n : del) c->getSequence().removeNote (*n, &session.undoManager());
    selected.clear(); repaint(); return true;
}

void PianoRoll::transpose (int semis)
{
    session.beginAction();
    for (auto* n : targets()) n->setNoteNumber (juce::jlimit (0, 127, n->getNoteNumber() + semis), &session.undoManager());
    repaint();
}

void PianoRoll::quantize()
{
    const double q = grid > 0 ? grid : 0.25;
    session.beginAction();
    for (auto* n : targets())
        n->setStartAndLength (te::BeatPosition::fromBeats (std::round (n->getStartBeat().inBeats() / q) * q), n->getLengthBeats(), &session.undoManager());
    repaint();
}

void PianoRoll::setVelocity (int v)
{
    for (auto* n : targets()) n->setVelocity (juce::jlimit (1, 127, v), &session.undoManager());
    repaint();
}

//==============================================================================
StepGrid::StepGrid (Session& s, te::StepClip& c) : session (s), clipID (c.itemID) { startTimerHz (20); }

juce::Rectangle<int> StepGrid::cellRect (int ch, int step) const
{
    return { nameW + 8 + step * (cell + gap) + (step / 4) * 6, 4 + ch * (cell + gap), cell, cell };
}

void StepGrid::paint (juce::Graphics& g)
{
    auto* c = clip(); if (c == nullptr) return;
    auto pat = c->getPattern (0);
    const auto colour = c->getTrack() ? c->getTrack()->getColour() : Col::acc;
    const int steps = pat.getNumNotes(), chans = juce::jmin (numDrumVoices, c->getChannels().size());
    int playing = -1;
    auto& tr = session.transport();
    if (tr.isPlaying() && c->getEditTimeRange().contains (tr.getPosition()))
    {
        const double b = session.toBeats (tr.getPosition()).inBeats() - session.toBeats (c->getPosition().getStart()).inBeats();
        playing = (int) std::floor (b / pat.getNoteLength().inBeats()) % juce::jmax (1, steps);
    }
    for (int ch = 0; ch < chans; ++ch)
    {
        auto nr = juce::Rectangle<int> (0, 4 + ch * (cell + gap), nameW, cell).toFloat();
        const float fs = juce::jmin (12.5f, cell * 0.55f);
        g.setColour (Col::panel2); g.fillRoundedRectangle (nr, 6.0f);
        g.setColour (Col::line); g.drawRoundedRectangle (nr, 6.0f, 1.0f);
        g.setColour (Col::text); g.setFont (juce::FontOptions (fs, juce::Font::bold));
        g.drawText (c->getChannels()[ch]->name.get(), nr.reduced (8, 0), juce::Justification::centredLeft, true);
        for (int s = 0; s < steps; ++s)
        {
            auto r = cellRect (ch, s).toFloat();
            const bool on = pat.getNote (ch, s), accent = on && pat.getVelocity (ch, s) > 110;
            g.setColour (on ? (accent ? colour : colour.withAlpha (0.6f)) : Col::lane); g.fillRoundedRectangle (r, 5.0f);
            g.setColour (accent ? Col::text.withAlpha (0.6f) : on ? colour : Col::line); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, accent ? 2.0f : 1.0f);
            if (s == playing) { g.setColour (Col::acc); g.drawRoundedRectangle (r.reduced (1.0f), 5.0f, 2.0f); }
        }
    }
}

void StepGrid::setCell (int ch, int step, int level)
{
    auto* c = clip(); if (c == nullptr) return;
    auto pat = c->getPattern (0);
    pat.setNote (ch, step, level > 0);
    if (level > 0) pat.setVelocity (ch, step, level == 2 ? 127 : 96);
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    auto* c = clip(); if (c == nullptr) return;
    auto pat = c->getPattern (0);
    const auto p = e.getPosition();
    for (int ch = 0; ch < juce::jmin (numDrumVoices, c->getChannels().size()); ++ch)
    {
        if (juce::Rectangle<int> (0, 4 + ch * (cell + gap), nameW, cell).contains (p))
        {
            if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack())) session.auditionDrum (*t, ch);
            paintLevel = -1; return;
        }
        for (int s = 0; s < pat.getNumNotes(); ++s)
            if (cellRect (ch, s).contains (p))
            {
                session.beginAction();
                const int cur = pat.getNote (ch, s) ? (pat.getVelocity (ch, s) > 110 ? 2 : 1) : 0;
                paintLevel = (cur + 1) % 3;
                setCell (ch, s, paintLevel);
                if (paintLevel > 0) if (auto* t = dynamic_cast<te::AudioTrack*> (c->getTrack())) session.auditionDrum (*t, ch);
                return;
            }
    }
    paintLevel = -1;
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (paintLevel < 0) return;
    auto* c = clip(); if (c == nullptr) return;
    auto pat = c->getPattern (0);
    for (int ch = 0; ch < juce::jmin (numDrumVoices, c->getChannels().size()); ++ch)
        for (int s = 0; s < pat.getNumNotes(); ++s)
            if (cellRect (ch, s).contains (e.getPosition()))
            {
                const int cur = pat.getNote (ch, s) ? (pat.getVelocity (ch, s) > 110 ? 2 : 1) : 0;
                if (cur != paintLevel) setCell (ch, s, paintLevel);
            }
}

//==============================================================================
namespace
{
    template <typename T> T* own (juce::OwnedArray<juce::Component>& o, T* c) { o.add (c); return c; }

    juce::TextButton* makeButton (juce::OwnedArray<juce::Component>& owner, const juce::String& text, std::function<void()> fn)
    {
        auto* b = own (owner, new juce::TextButton (text)); b->onClick = std::move (fn); b->setSize (juce::jmax (60, (int) juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (14.0f)), text) + 26), 30); return b;
    }
    juce::Slider* makeSlider (juce::OwnedArray<juce::Component>& owner, double min, double max, double step, double value, const juce::String& suffix)
    {
        auto* s = own (owner, new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
        s->setRange (min, max, step); s->setValue (value, juce::dontSendNotification); s->setTextValueSuffix (suffix);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22); s->setSize (230, 30); s->setScrollWheelEnabled (false); return s;
    }
    juce::Label* makeLabel (juce::OwnedArray<juce::Component>& owner, const juce::String& text, int w, bool muted = true)
    {
        auto* l = own (owner, new juce::Label ({}, text));
        l->setColour (juce::Label::textColourId, muted ? Col::muted : Col::text); l->setFont (juce::FontOptions (13.0f)); l->setSize (w, 30); return l;
    }
}

EditorPanel::EditorPanel (Session& s) : session (s)
{
    session.addChangeListener (this);
    session.editWatchers.add (this);
    styleHint (hint); addAndMakeVisible (hint);
    rebuild();
}

EditorPanel::~EditorPanel() { session.editWatchers.remove (this); session.removeChangeListener (this); }

void EditorPanel::paint (juce::Graphics& g) { g.fillAll (Col::panel); }

void EditorPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (session.selectedClip != shownClip || session.structureVersion != shownVersion) rebuild();
    else { if (roll) roll->repaint(); if (steps) steps->repaint(); }
}

void EditorPanel::rebuild()
{
    shownClip = session.selectedClip; shownVersion = session.structureVersion;
    controls.clear(); roll.reset(); steps.reset();
    auto* clip = session.getSelectedClip();
    hint.setText ({}, juce::dontSendNotification);
    auto& C = controls;
    auto id = clip ? clip->itemID : te::EditItemID();
    auto get = [this, id] { return te::findClipForID (*session.edit, id); };

    if (clip == nullptr)
        hint.setText ("Select a clip to edit it here. To make one, double-click an empty spot on a synth, sampler or drum track, "
                      "or drop audio and MIDI files on the timeline. Audio clips show speed, pitch, reverse and fades; "
                      "note clips open a piano roll; drum clips open the step sequencer.", juce::dontSendNotification);
    else if (auto* w = dynamic_cast<te::WaveAudioClip*> (clip))
    {
        auto file = w->getSourceFileReference().getFile();
        makeLabel (C, "From " + file.getFileName() + ". The file itself is never changed.", 440);
        makeLabel (C, "Speed", 50, false);
        auto* speed = makeSlider (C, 0.25, 4.0, 0.01, w->getSpeedRatio(), "x"); speed->setSkewFactorFromMidPoint (1.0);
        makeLabel (C, "Pitch", 46, false);
        auto* pitch = makeSlider (C, -24, 24, 1, w->getPitchChange(), " st");
        auto* keep = own (C, new juce::ToggleButton ("Keep pitch when changing speed")); keep->setToggleState (Session::keepsPitch (*w), juce::dontSendNotification); keep->setSize (250, 30);
        auto apply = [this, get, speed, pitch, keep]
        {
            if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) { session.beginAction(); session.setSpeedPitch (*w, speed->getValue(), (float) pitch->getValue(), keep->getToggleState()); }
            pitch->setEnabled (keep->getToggleState());
        };
        pitch->setEnabled (keep->getToggleState());
        speed->onDragEnd = apply; pitch->onDragEnd = apply; keep->onClick = apply;
        speed->onValueChange = [speed, apply] { if (! speed->isMouseButtonDown()) apply(); };
        pitch->onValueChange = [pitch, apply] { if (! pitch->isMouseButtonDown()) apply(); };
        auto* rev = own (C, new juce::ToggleButton ("Reverse")); rev->setToggleState (w->getIsReversed(), juce::dontSendNotification); rev->setSize (100, 30);
        rev->onClick = [this, get, rev] { if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) session.setReversed (*w, rev->getToggleState()); };
        makeLabel (C, "Level", 46, false);
        auto* gain = makeSlider (C, -40, 12, 0.1, w->getGainDB(), " dB");
        gain->onDragStart = [this] { session.beginAction(); };
        gain->onValueChange = [get, gain] { if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) w->setGainDB ((float) gain->getValue()); };
        makeLabel (C, "Fade in", 60, false);
        auto* fi = makeSlider (C, 0, 10, 0.01, w->getFadeIn().inSeconds(), " s"); fi->setSkewFactorFromMidPoint (1.0);
        fi->onDragStart = [this] { session.beginAction(); };
        fi->onValueChange = [get, fi] { if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) w->setFadeIn (te::TimeDuration::fromSeconds (fi->getValue())); };
        makeLabel (C, "Fade out", 64, false);
        auto* fo = makeSlider (C, 0, 10, 0.01, w->getFadeOut().inSeconds(), " s"); fo->setSkewFactorFromMidPoint (1.0);
        fo->onDragStart = [this] { session.beginAction(); };
        fo->onValueChange = [get, fo] { if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) w->setFadeOut (te::TimeDuration::fromSeconds (fo->getValue())); };
        makeButton (C, "Split at playhead", [this] { session.splitAtPlayhead(); });
        makeButton (C, "Duplicate", [this, get] { if (auto* c = get()) session.duplicateClip (*c); });
        makeButton (C, "Loop this clip", [this, get] { if (auto* c = get()) session.loopClip (*c); });
        auto* mk = makeButton (C, "Make a playable instrument", [this, get] { if (auto* w = dynamic_cast<te::WaveAudioClip*> (get())) session.makeSamplerFromClip (*w); });
        makeButton (C, "Turn into MIDI notes...", [this, get]
        {
            juce::PopupMenu m;
            m.addItem (1, "Melody (one note at a time: voice, bass, lead)");
            m.addItem (2, "Chords (several notes at once)");
            m.addItem (3, "Drums (kick, snare, hi-hat)");
            m.showMenuAsync (juce::PopupMenu::Options(), [this, get] (int r)
            {
                if (auto* w = dynamic_cast<te::WaveAudioClip*> (get()); w != nullptr && r > 0)
                    session.convertToMidi (*w, r == 1 ? TranscribeMode::melody : r == 2 ? TranscribeMode::chords : TranscribeMode::drums);
            });
        });
        mk->setColour (juce::TextButton::buttonColourId, Col::acc.withAlpha (0.25f));
        makeButton (C, "Delete clip", [this, get] { if (auto* c = get()) session.deleteClip (*c); });
        hint.setText ("Drag a clip's edges on the timeline to trim it. Trimming hides audio; it never deletes it. "
                      "Speed and pitch changes are rendered in the background; the clip shows its progress.", juce::dontSendNotification);
    }
    else if (auto* mc = dynamic_cast<te::MidiClip*> (clip))
    {
        roll = std::make_unique<PianoRoll> (session, *mc);
        addAndMakeVisible (*roll);
        auto* r = roll.get();
        makeLabel (C, "Grid", 36, false);
        auto* gridBox = own (C, new juce::ComboBox()); gridBox->setSize (90, 28);
        const double grids[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 0.0 }; const char* gn[] = { "1/32", "1/16", "1/8", "1/4", "1/2", "Free" };
        for (int i = 0; i < 6; ++i) gridBox->addItem (gn[i], i + 1);
        gridBox->setSelectedId (2, juce::dontSendNotification);
        gridBox->onChange = [r, gridBox, grids] { r->grid = grids[gridBox->getSelectedId() - 1]; r->repaint(); };
        makeLabel (C, "Length", 50, false);
        auto* lenBox = own (C, new juce::ComboBox()); lenBox->setSize (100, 28);
        const int curBars = juce::jmax (1, juce::roundToInt (mc->getLengthInBeats().inBeats() / 4.0));
        for (int b : { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 }) lenBox->addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
        if (lenBox->indexOfItemId (curBars) < 0) lenBox->addItem (juce::String (curBars) + " bars", curBars);
        lenBox->setSelectedId (curBars, juce::dontSendNotification);
        lenBox->onChange = [this, get, lenBox]
        {
            if (auto* c = get()) { session.beginAction(); auto s = c->getPosition().getStart(); c->setLength (session.toTime (session.toBeats (s) + te::BeatDuration::fromBeats (4.0 * lenBox->getSelectedId())) - s, true); }
        };
        makeLabel (C, "Transpose", 70, false);
        for (auto [label, d] : { std::pair<const char*, int> { "-12", -12 }, { "-1", -1 }, { "+1", 1 }, { "+12", 12 } })
            makeButton (C, label, [r, d = d] { r->transpose (d); })->setSize (46, 30);
        makeButton (C, "Quantize", [r] { r->quantize(); });
        makeLabel (C, "Velocity", 60, false);
        auto* vel = makeSlider (C, 1, 127, 1, 100, ""); vel->setSize (180, 30);
        vel->onDragStart = [this] { session.beginAction(); };
        vel->onValueChange = [r, vel] { r->setVelocity ((int) vel->getValue()); };
        makeButton (C, "Zoom -", [r] { r->pxPerBeat = juce::jmax (12.0, r->pxPerBeat / 1.3); r->repaint(); });
        makeButton (C, "Zoom +", [r] { r->pxPerBeat = juce::jmin (400.0, r->pxPerBeat * 1.3); r->repaint(); });
        hint.setText ("Click to add a note, drag to move, drag its right edge to lengthen. Double-click or right-click deletes; Shift-drag selects several. "
                      "Scroll to move around, pinch or Cmd-scroll to zoom. Change the sound in the Sound tab.", juce::dontSendNotification);
    }
    else if (auto* sc = dynamic_cast<te::StepClip*> (clip))
    {
        steps = std::make_unique<StepGrid> (session, *sc);
        addAndMakeVisible (*steps);
        auto pat = sc->getPattern (0);
        makeLabel (C, "Steps", 44, false);
        auto* stepBox = own (C, new juce::ComboBox()); stepBox->setSize (130, 28);
        stepBox->addItem ("16 (1 bar)", 16); stepBox->addItem ("32 (2 bars)", 32);
        stepBox->setSelectedId (pat.getNumNotes() == 32 ? 32 : 16, juce::dontSendNotification);
        stepBox->onChange = [this, get, stepBox]
        {
            if (auto* c = dynamic_cast<te::StepClip*> (get()))
            {
                session.beginAction(); c->getPattern (0).setNumNotes (stepBox->getSelectedId());
                session.setStepClipBars (*c, juce::jmax (1, juce::roundToInt (c->getPosition().getLength().inSeconds() / session.barLength().inSeconds())));
            }
        };
        makeLabel (C, "Clip length", 76, false);
        auto* lenBox = own (C, new juce::ComboBox()); lenBox->setSize (100, 28);
        const int curBars = juce::jmax (1, juce::roundToInt (sc->getPosition().getLength().inSeconds() / session.barLength().inSeconds()));
        for (int b : { 1, 2, 4, 8, 12, 16, 24, 32 }) lenBox->addItem (juce::String (b) + (b == 1 ? " bar" : " bars"), b);
        if (lenBox->indexOfItemId (curBars) < 0) lenBox->addItem (juce::String (curBars) + " bars", curBars);
        lenBox->setSelectedId (curBars, juce::dontSendNotification);
        lenBox->onChange = [this, get, lenBox] { if (auto* c = dynamic_cast<te::StepClip*> (get())) { session.beginAction(); session.setStepClipBars (*c, lenBox->getSelectedId()); } };
        makeButton (C, "Clear", [this, get] { if (auto* c = dynamic_cast<te::StepClip*> (get())) { session.beginAction(); c->getPattern (0).clear(); } });
        makeButton (C, "Surprise me", [this, get] { if (auto* c = dynamic_cast<te::StepClip*> (get())) { session.beginAction(); c->getPattern (0).randomiseSteps(); } });
        makeButton (C, "Duplicate clip", [this, get] { if (auto* c = get()) session.duplicateClip (*c); });
        hint.setText ("Click a step to turn it on, again for an accent, once more to clear. Drag across steps to paint. "
                      "Click a drum's name to hear it. Shape each drum, or load your own sounds, in the Sound tab.", juce::dontSendNotification);
    }
    for (auto* c : controls) addAndMakeVisible (c);
    resized();
}

void EditorPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    // flow the controls left to right, wrapping
    int x = r.getX(), y = r.getY(), lineH = 0;
    for (auto* c : controls)
    {
        if (x + c->getWidth() > r.getRight() && x > r.getX()) { x = r.getX(); y += lineH + 6; lineH = 0; }
        c->setTopLeftPosition (x, y); x += c->getWidth() + 8; lineH = juce::jmax (lineH, c->getHeight());
    }
    if (! controls.isEmpty()) r.setTop (y + lineH + 8);
    auto hintArea = r.removeFromBottom (roll || steps ? 36 : juce::jmax (40, r.getHeight()));
    hint.setBounds (hintArea);
    if (roll) roll->setBounds (r);
    if (steps) steps->setBounds (r);
}
}
