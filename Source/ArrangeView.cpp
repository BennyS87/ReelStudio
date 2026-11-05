#include "ArrangeView.h"

namespace Reel
{
ArrangeView::ArrangeView (Session& s) : session (s)
{
    session.addChangeListener (this);
    session.editWatchers.add (this);
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

ArrangeView::~ArrangeView() { session.editWatchers.remove (this); session.removeChangeListener (this); }

std::vector<ArrangeView::Row> ArrangeView::rows() const
{
    std::vector<Row> r; int y = rulerH - scrollY;
    for (auto* t : session.tracks())
    {
        r.push_back ({ t, y, trackH, false }); y += trackH;
        if ((bool) t->state.getProperty (IDs::autoShown, false)) { r.push_back ({ t, y, autoH, true }); y += autoH; }
    }
    return r;
}

const ArrangeView::Row* ArrangeView::rowAt (int y) const
{
    static thread_local std::vector<Row> cache;
    cache = rows();
    for (auto& r : cache) if (y >= r.y && y < r.y + r.h) return &r;
    return nullptr;
}

te::TimePosition ArrangeView::snap (te::TimePosition t, bool fine) const
{
    const double unit = fine ? juce::jmin (snapBeats > 0 ? snapBeats : 0.25, 0.25) : snapBeats;
    if (unit <= 0 || juce::ModifierKeys::currentModifiers.isAltDown()) return t;
    const double b = session.toBeats (t).inBeats();
    return session.toTime (te::BeatPosition::fromBeats (std::round (b / unit) * unit));
}

juce::Rectangle<int> ArrangeView::clipRect (const Row& r, te::Clip& c) const
{
    const auto pos = c.getPosition();
    const int x1 = timeToX (pos.getStart()), x2 = timeToX (pos.getEnd());
    return { x1, r.y + 3, juce::jmax (6, x2 - x1), r.h - 6 };
}

te::Clip* ArrangeView::clipAt (const Row& r, juce::Point<int> p, int& edge) const
{
    auto& clips = r.track->getClips();
    for (int i = clips.size(); --i >= 0;)
    {
        auto* c = clips.getUnchecked (i);
        auto rc = clipRect (r, *c);
        if (rc.contains (p))
        {
            const int grip = juce::jmin (9, rc.getWidth() / 3);
            edge = p.x < rc.getX() + grip ? -1 : p.x > rc.getRight() - grip ? 1 : 0;
            return c;
        }
    }
    return nullptr;
}

juce::Rectangle<int> ArrangeView::headerButton (const Row& r, int i) const
{
    switch (i)
    {
        case 0:  return { 14, r.y + 52, 26, 24 };
        case 1:  return { 44, r.y + 52, 26, 24 };
        case 2:  return { 74, r.y + 52, 44, 24 };
        case 3:  return { headerW - 36, r.y + 6, 28, 22 };
        default: return { 126, r.y + 58, headerW - 138, 12 };   // volume bar
    }
}

te::SmartThumbnail* ArrangeView::thumbFor (te::WaveAudioClip& w)
{
    te::AudioFile proxy (w.getPlaybackFile());
    auto& th = thumbs[w.itemID];
    if (th == nullptr) th = std::make_unique<te::SmartThumbnail> (session.engine, proxy, *this, &w.edit);
    else th->setNewFile (proxy);
    return th.get();
}

//==============================================================================
void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (Col::lane2);
    auto rs = rows();
    for (auto& r : rs) if (r.y + r.h > rulerH && r.y < getHeight()) { paintLane (g, r); paintHeader (g, r); }

    if (rs.empty())
    {
        g.setColour (Col::muted); g.setFont (juce::FontOptions (16.0f));
        g.drawFittedText ("Drop audio or MIDI files here, or use Add track to start.\nOpen the demo song from Project to hear everything working.",
                          getLocalBounds().withTrimmedTop (rulerH).withTrimmedLeft (headerW).reduced (30), juce::Justification::centred, 3);
    }
    auto& tr = session.transport();
    if (tr.looping)
    {
        auto lr = tr.getLoopRange();
        g.setColour (Col::acc.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (juce::jmax (headerW, timeToX (lr.getStart())), rulerH, timeToX (lr.getEnd()), getHeight()));
    }
    paintRuler (g);
    const int px = timeToX (tr.getPosition());
    if (px >= headerW)
    {
        g.setColour (Col::acc); g.fillRect (px - 1, 0, 2, getHeight());
    }
    g.setColour (Col::line); g.drawVerticalLine (headerW - 1, (float) rulerH, (float) getHeight());
}

void ArrangeView::paintRuler (juce::Graphics& g)
{
    auto area = juce::Rectangle<int> (0, 0, getWidth(), rulerH);
    g.setColour (Col::panel); g.fillRect (area);
    g.setColour (Col::line); g.drawHorizontalLine (rulerH - 1, 0, (float) getWidth());
    g.setColour (Col::muted); g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (session.tracks().size()) + (session.tracks().size() == 1 ? " track" : " tracks"), 12, 0, headerW - 20, rulerH, juce::Justification::centredLeft);

    auto& tr = session.transport();
    auto lr = tr.getLoopRange();
    if (lr.getLength() > 0s)
    {
        g.setColour (tr.looping ? Col::acc.withAlpha (0.45f) : Col::line);
        g.fillRect (juce::Rectangle<int>::leftTopRightBottom (juce::jmax (headerW, timeToX (lr.getStart())), rulerH / 2 + 2, juce::jmax (headerW, timeToX (lr.getEnd())), rulerH - 1));
    }
    g.saveState(); g.reduceClipRegion (headerW, 0, getWidth() - headerW, rulerH);
    const double secsPerBeat = session.toTime (te::BeatPosition::fromBeats (1)).inSeconds();
    const double pxPerBeat = secsPerBeat * pxPerSec;
    int every = 1; while (every * 4 * pxPerBeat < 36) every *= 2;
    const int b0 = juce::jmax (0, (int) std::floor (session.toBeats (xToTime (headerW)).inBeats())), b1 = (int) std::ceil (session.toBeats (xToTime (getWidth())).inBeats());
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    for (int b = b0; b <= b1; ++b)
    {
        const int x = timeToX (session.toTime (te::BeatPosition::fromBeats (b)));
        if (b % 4 == 0)
        {
            const int bar = b / 4;
            g.setColour (Col::muted); g.fillRect (x, bar % every == 0 ? 0 : 12, 1, rulerH);
            if (bar % every == 0) { g.setColour (Col::text); g.drawText (juce::String (bar + 1), x + 4, 2, 40, 14, juce::Justification::left); }
        }
        else if (pxPerBeat >= 9) { g.setColour (Col::line); g.fillRect (x, rulerH - 8, 1, 8); }
    }
    g.restoreState();
}

void ArrangeView::paintHeader (juce::Graphics& g, const Row& r)
{
    auto& t = *r.track;
    const bool sel = t.itemID == session.selectedTrack;
    auto area = juce::Rectangle<int> (0, r.y, headerW, r.h);
    g.setColour (sel ? Col::panel2 : Col::panel); g.fillRect (area);
    g.setColour (t.getColour().withAlpha (r.automation ? 0.45f : 1.0f)); g.fillRect (area.withWidth (5));
    g.setColour (Col::line2); g.drawHorizontalLine (r.y + r.h - 1, 0, (float) headerW);
    if (r.automation)
    {
        auto* p = session.getAutomationParam (t);
        g.setColour (Col::text); g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        juce::String label = "Volume";
        for (auto& a : session.automationTargets (t)) if (a.param == p) label = a.label;
        g.drawText (label + "  \xe2\x96\xbe", 14, r.y + 8, headerW - 22, 18, juce::Justification::left);   // ▾
        g.setColour (Col::muted); g.setFont (juce::FontOptions (12.0f));
        g.drawText (p != nullptr && p->hasAutomationPoints() ? "Drag points, right-click removes" : "Click in the lane to add points", 14, r.y + 30, headerW - 22, 16, juce::Justification::left);
        return;
    }
    if (sel) { g.setColour (Col::acc); g.drawRect (area, 1); }
    g.setColour (Col::text); g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (t.getName(), 14, r.y + 7, headerW - 54, 20, juce::Justification::left, true);
    juce::String sub = kindLabel (getKind (t));
    if (getKind (t) == Kind::synth) sub << ": " << t.state.getProperty (IDs::preset, "Custom").toString().replace ("user:", "");
    if (getKind (t) == Kind::sampler) sub << ": " << t.state.getProperty (IDs::sampleName, "no sound yet").toString();
    int fx = 0; for (auto* p : session.effectChain (t)) if (p->isEnabled()) ++fx;
    if (fx) sub << ", " << fx << (fx == 1 ? " effect" : " effects");
    if (session.getLines (t) != nullptr) sub << ", Lines";
    g.setColour (Col::muted); g.setFont (juce::FontOptions (12.5f));
    g.drawText (sub, 14, r.y + 28, headerW - 22, 16, juce::Justification::left, true);

    auto button = [&] (int i, const juce::String& text, bool on, juce::Colour onColour)
    {
        auto b = headerButton (r, i).toFloat();
        g.setColour (on ? onColour : Col::panel2); g.fillRoundedRectangle (b, 5.0f);
        g.setColour (on ? onColour : Col::line); g.drawRoundedRectangle (b, 5.0f, 1.0f);
        g.setColour (on ? juce::Colour (0xff1a1200) : Col::text); g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawText (text, b, juce::Justification::centred);
    };
    button (0, "M", t.isMuted (false), juce::Colour (0xff8aa3b0));
    button (1, "S", t.isSolo (false), Col::acc);
    button (2, "Auto", (bool) t.state.getProperty (IDs::autoShown, false), Col::acc.withAlpha (0.8f));
    button (3, "...", false, Col::acc);
    if (auto* v = t.getVolumePlugin())
    {
        auto b = headerButton (r, 4).toFloat();
        g.setColour (Col::lane); g.fillRoundedRectangle (b.withSizeKeepingCentre (b.getWidth(), 4), 2);
        const float pos = juce::jlimit (0.0f, 1.0f, v->getSliderPos());
        g.setColour (t.getColour()); g.fillRoundedRectangle (b.withWidth (b.getWidth() * pos).withSizeKeepingCentre (b.getWidth() * pos, 4).withX (b.getX()), 2);
        g.fillEllipse (b.getX() + b.getWidth() * pos - 6, b.getCentreY() - 6, 12, 12);
    }
}

void ArrangeView::paintLane (juce::Graphics& g, const Row& r)
{
    auto lane = juce::Rectangle<int> (headerW, r.y, getWidth() - headerW, r.h);
    g.setColour (r.automation ? Col::lane2 : Col::lane); g.fillRect (lane);
    g.saveState(); g.reduceClipRegion (lane);
    const double pxPerBeat = session.toTime (te::BeatPosition::fromBeats (1)).inSeconds() * pxPerSec;
    const int b0 = juce::jmax (0, (int) std::floor (session.toBeats (xToTime (headerW)).inBeats())), b1 = (int) std::ceil (session.toBeats (xToTime (getWidth())).inBeats());
    for (int b = b0; b <= b1; ++b)
    {
        if (b % 4 != 0 && pxPerBeat < 9) continue;
        g.setColour (b % 4 == 0 ? Col::line : Col::line2);
        g.fillRect (timeToX (session.toTime (te::BeatPosition::fromBeats (b))), r.y, 1, r.h);
    }
    g.setColour (Col::line2); g.drawHorizontalLine (r.y + r.h - 1, (float) headerW, (float) getWidth());
    if (r.automation) paintAutomation (g, r);
    else for (auto* c : r.track->getClips())
    {
        auto rc = clipRect (r, *c);
        if (rc.getRight() >= headerW && rc.getX() <= getWidth()) paintClip (g, *r.track, *c, rc);
    }
    g.restoreState();
}

void ArrangeView::paintClip (juce::Graphics& g, te::AudioTrack& t, te::Clip& c, juce::Rectangle<int> rc)
{
    const auto colour = t.getColour();
    const bool sel = c.itemID == session.selectedClip;
    auto rf = rc.toFloat();
    g.setColour (colour.withAlpha (0.26f).overlaidWith (Col::lane.withAlpha (0.0f))); g.fillRoundedRectangle (rf, 6.0f);
    auto nameStrip = rc.removeFromTop (17);
    g.setColour (colour.withAlpha (0.55f)); g.fillRoundedRectangle (nameStrip.toFloat(), 6.0f);
    g.fillRect (nameStrip.withTrimmedTop (8));
    juce::String name = c.getName();
    juce::StringArray badges;
    auto body = rc.reduced (1, 2);

    if (auto* w = dynamic_cast<te::WaveAudioClip*> (&c))
    {
        if (w->getIsReversed()) badges.add ("Reversed");
        if (std::abs (w->getSpeedRatio() - 1.0) > 1e-3) badges.add (juce::String (w->getSpeedRatio(), 2) + "x speed");
        if (Session::keepsPitch (*w) && std::abs (w->getPitchChange()) > 1e-3) badges.add ((w->getPitchChange() > 0 ? "+" : "") + juce::String (w->getPitchChange(), 0) + " st");
        if (auto* th = thumbFor (*w))
        {
            g.setColour (colour.withAlpha (0.95f));
            const double len = w->getPosition().getLength().inSeconds();
            if (w->usesTimeStretchedProxy() || w->getIsReversed())
            {
                if (! th->isOutOfDate()) th->drawChannels (g, body, { te::TimePosition(), te::TimePosition::fromSeconds (len) }, (float) w->getGain());
            }
            else
            {
                const double off = w->getPosition().getOffset().inSeconds(), sr = w->getSpeedRatio();
                th->drawChannels (g, body, { te::TimePosition::fromSeconds (off * sr), te::TimePosition::fromSeconds ((off + len) * sr) }, (float) w->getGain());
            }
            if (th->isGeneratingProxy())
            {
                g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRect (body);
                g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("Processing " + juce::String (juce::roundToInt (th->getProxyProgress() * 100.0f)) + "%", body.reduced (6, 0), juce::Justification::centredLeft);
            }
        }
    }
    else if (auto* mc = dynamic_cast<te::MidiClip*> (&c))
    {
        auto& notes = mc->getSequence().getNotes();
        int lo = 127, hi = 0; for (auto* n : notes) { lo = juce::jmin (lo, n->getNoteNumber()); hi = juce::jmax (hi, n->getNoteNumber()); }
        if (notes.isEmpty())
        {
            g.setColour (Col::muted); g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Empty: double-click to edit", body.reduced (6, 0), juce::Justification::centredLeft);
        }
        else
        {
            const int span = juce::jmax (12, hi - lo + 1); lo = (lo + hi) / 2 - span / 2;
            const float rh = body.getHeight() / (float) span;
            g.setColour (colour);
            for (auto* n : notes)
            {
                const int x1 = timeToX (n->getEditStartTime (*mc)), x2 = timeToX (n->getEditEndTime (*mc));
                if (x2 < rc.getX() || x1 > rc.getRight()) continue;
                g.setOpacity (0.45f + 0.55f * n->getVelocity() / 127.0f);
                g.fillRect ((float) x1, body.getBottom() - (n->getNoteNumber() - lo + 1) * rh, (float) juce::jmax (2, x2 - x1 - 1), juce::jmax (2.0f, rh - 0.5f));
            }
            g.setOpacity (1.0f);
        }
    }
    else if (auto* sc = dynamic_cast<te::StepClip*> (&c))
    {
        auto pat = sc->getPattern (0);
        const int steps = juce::jmax (1, pat.getNumNotes()), chans = juce::jmin (numDrumVoices, sc->getChannels().size());
        const double stepBeats = pat.getNoteLength().inBeats(), startBeat = session.toBeats (c.getPosition().getStart()).inBeats();
        const double lenBeats = session.toBeats (c.getPosition().getEnd()).inBeats() - startBeat;
        juce::Array<int> used; for (int ch = 0; ch < chans; ++ch) for (int s = 0; s < steps; ++s) if (pat.getNote (ch, s)) { used.add (ch); break; }
        const float rh = body.getHeight() / (float) juce::jmax (1, used.size());
        g.setColour (colour);
        for (int k = 0; k * stepBeats < lenBeats - 1e-6; ++k)
        {
            const int s = k % steps;
            const int x1 = timeToX (session.toTime (te::BeatPosition::fromBeats (startBeat + k * stepBeats))), x2 = timeToX (session.toTime (te::BeatPosition::fromBeats (startBeat + (k + 1) * stepBeats)));
            if (x2 < rc.getX() || x1 > rc.getRight()) continue;
            for (int u = 0; u < used.size(); ++u)
                if (pat.getNote (used[u], s))
                {
                    g.setOpacity (pat.getVelocity (used[u], s) > 110 ? 1.0f : 0.6f);
                    g.fillRect ((float) x1, body.getY() + u * rh + 1, (float) juce::jmax (1, x2 - x1 - 1), juce::jmax (1.5f, juce::jmin (6.0f, rh - 1)));
                }
        }
        g.setOpacity (1.0f);
    }
    g.setColour (sel ? Col::acc : colour.withAlpha (0.7f)); g.drawRoundedRectangle (rf.reduced (sel ? 1.0f : 0.5f), 6.0f, sel ? 2.0f : 1.0f);
    g.setColour (Col::text); g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (name, nameStrip.reduced (7, 0), juce::Justification::centredLeft, true);
    if (! badges.isEmpty())
    {
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (badges.joinIntoString (", "), rc.reduced (6, 3), juce::Justification::bottomRight, true);
    }
}

float ArrangeView::automationY (const Row& r, te::AutomatableParameter& p, float v) const
{
    auto range = p.getValueRange();
    return (float) r.y + 5.0f + (1.0f - range.getLength() > 0 ? 0.0f : 0.0f) + (1.0f - (v - range.getStart()) / juce::jmax (1e-6f, range.getLength())) * (r.h - 10);
}

void ArrangeView::paintAutomation (juce::Graphics& g, const Row& r)
{
    auto* p = session.getAutomationParam (*r.track);
    if (p == nullptr) return;
    auto& curve = p->getCurve();
    const auto colour = r.track->getColour();
    g.setColour (colour);
    if (curve.getNumPoints() == 0)
    {
        const float y = automationY (r, *p, p->getCurrentValue());
        const float dashes[] = { 5.0f, 5.0f };
        g.drawDashedLine ({ (float) headerW, y, (float) getWidth(), y }, dashes, 2, 1.5f);
        return;
    }
    juce::Path path;
    path.startNewSubPath ((float) headerW, automationY (r, *p, curve.getPointValue (0)));
    for (int i = 0; i < curve.getNumPoints(); ++i) path.lineTo ((float) timeToX (curve.getPointTime (i)), automationY (r, *p, curve.getPointValue (i)));
    path.lineTo ((float) getWidth(), automationY (r, *p, curve.getPointValue (curve.getNumPoints() - 1)));
    juce::Path fill (path); fill.lineTo ((float) getWidth(), (float) (r.y + r.h)); fill.lineTo ((float) headerW, (float) (r.y + r.h)); fill.closeSubPath();
    g.setColour (colour.withAlpha (0.15f)); g.fillPath (fill);
    g.setColour (colour); g.strokePath (path, juce::PathStrokeType (2.0f));
    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const float x = (float) timeToX (curve.getPointTime (i)), y = automationY (r, *p, curve.getPointValue (i));
        g.setColour (Col::panel); g.fillEllipse (x - 4.5f, y - 4.5f, 9, 9);
        g.setColour (colour); g.drawEllipse (x - 4.5f, y - 4.5f, 9, 9, 2.0f);
    }
}

//==============================================================================
void ArrangeView::mouseDown (const juce::MouseEvent& e)
{
    dragMoved = false; drag = Drag::none;
    const auto p = e.getPosition();
    auto& tr = session.transport();
    if (p.y < rulerH)
    {
        if (p.x < headerW) return;
        if (p.y > rulerH / 2 || e.mods.isShiftDown()) { drag = Drag::loop; dragLoopAnchor = snap (xToTime (p.x)); }
        else { drag = Drag::seek; session.seek (xToTime (p.x)); }
        return;
    }
    auto* r = rowAt (p.y);
    if (r == nullptr) { session.select (nullptr); return; }
    auto& t = *r->track;
    if (p.x < headerW)
    {
        if (r->automation) { showAutomationMenu (t); return; }
        if (e.mods.isPopupMenu()) { session.select (&t); showTrackMenu (t); return; }
        if (headerButton (*r, 0).contains (p)) { session.beginAction(); t.setMute (! t.isMuted (false)); repaint(); return; }
        if (headerButton (*r, 1).contains (p)) { session.beginAction(); t.setSolo (! t.isSolo (false)); repaint(); return; }
        if (headerButton (*r, 2).contains (p)) { t.state.setProperty (IDs::autoShown, ! (bool) t.state.getProperty (IDs::autoShown, false), nullptr); repaint(); return; }
        if (headerButton (*r, 3).contains (p)) { session.select (&t); showTrackMenu (t); return; }
        if (headerButton (*r, 4).expanded (0, 6).contains (p) && t.getVolumePlugin() != nullptr)
        {
            session.beginAction(); drag = Drag::volume; dragClip = t.itemID; dragVolStart = t.getVolumePlugin()->getSliderPos(); return;
        }
        session.select (&t);
        return;
    }
    if (r->automation)
    {
        auto* param = session.getAutomationParam (t);
        if (param == nullptr) return;
        auto& curve = param->getCurve();
        int hit = -1;
        for (int i = 0; i < curve.getNumPoints(); ++i)
            if (std::abs (timeToX (curve.getPointTime (i)) - p.x) < 8 && std::abs (automationY (*r, *param, curve.getPointValue (i)) - p.y) < 8) hit = i;
        session.beginAction();
        if (e.mods.isPopupMenu() || e.mods.isAltDown()) { if (hit >= 0) curve.removePoint (hit); repaint(); return; }
        auto range = param->getValueRange();
        const float v = juce::jlimit (range.getStart(), range.getEnd(), range.getStart() + (1.0f - (p.y - r->y - 5.0f) / (r->h - 10.0f)) * range.getLength());
        if (hit < 0)
        {
            if (curve.getNumPoints() == 0) curve.addPoint (te::TimePosition(), param->getCurrentValue(), 0.0f);
            hit = curve.addPoint (snap (xToTime (p.x), true), v, 0.0f);
        }
        drag = Drag::autoPoint; dragPoint = hit; dragParam = param; dragClip = t.itemID;
        repaint(); return;
    }
    int edge = 0;
    if (auto* c = clipAt (*r, p, edge))
    {
        session.select (&t, c);
        if (e.mods.isPopupMenu()) { showClipMenu (*c); return; }
        session.beginAction();
        drag = edge < 0 ? Drag::trimLeft : edge > 0 ? Drag::trimRight : Drag::moveClip;
        dragClip = c->itemID; dragClipStart = c->getPosition().getStart(); dragClipLen = c->getPosition().getLength();
        return;
    }
    session.select (&t);
    if (! e.mods.isPopupMenu()) { session.seek (snap (xToTime (p.x))); drag = Drag::seek; }
}

void ArrangeView::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (e.getDistanceFromDragStart() > 3) dragMoved = true;
    auto& tr = session.transport();
    switch (drag)
    {
        case Drag::seek: session.seek (xToTime (p.x)); break;
        case Drag::loop:
        {
            auto t = snap (xToTime (p.x));
            auto a = juce::jmin (t, dragLoopAnchor), b = juce::jmax (t, dragLoopAnchor);
            if (b - a < session.barLength() / 4) b = a + session.barLength() / 4;
            tr.setLoopRange ({ a, b }); tr.looping = true; break;
        }
        case Drag::volume:
            if (auto* t = te::findAudioTrackForID (*session.edit, dragClip))
                t->getVolumePlugin()->setSliderPos (juce::jlimit (0.0f, 1.0f, dragVolStart + e.getDistanceFromDragStartX() / (float) (headerW - 138)));
            break;
        case Drag::autoPoint:
            if (dragParam != nullptr && dragPoint >= 0)
            {
                auto& curve = dragParam->getCurve();
                const Row* r = nullptr; auto rs = rows(); for (auto& x : rs) if (x.automation && x.track->itemID == dragClip) r = &x;
                if (r == nullptr) break;
                auto range = dragParam->getValueRange();
                const float v = juce::jlimit (range.getStart(), range.getEnd(), range.getStart() + (1.0f - (p.y - r->y - 5.0f) / (r->h - 10.0f)) * range.getLength());
                auto t = snap (xToTime (p.x), true);
                if (dragPoint > 0) t = juce::jmax (t, curve.getPointTime (dragPoint - 1));
                if (dragPoint < curve.getNumPoints() - 1) t = juce::jmin (t, curve.getPointTime (dragPoint + 1));
                dragPoint = curve.movePoint (dragPoint, t, v, false);
            }
            break;
        case Drag::moveClip: case Drag::trimLeft: case Drag::trimRight:
        {
            if (! dragMoved) break;
            auto* c = te::findClipForID (*session.edit, dragClip); if (c == nullptr) break;
            const auto delta = xToTime (p.x) - xToTime (e.getMouseDownX());
            const auto minLen = session.barLength() / 16;
            if (drag == Drag::moveClip)
            {
                auto ns = snap (juce::jmax (te::TimePosition(), dragClipStart + delta));
                c->setStart (ns, false, true);
                if (auto* r = rowAt (p.y))
                    if (! r->automation && r->track != c->getTrack())
                    {
                        const auto k = getKind (*r->track);
                        const bool ok = dynamic_cast<te::WaveAudioClip*> (c) ? k == Kind::audio : dynamic_cast<te::StepClip*> (c) ? k == Kind::drums : k != Kind::audio;
                        if (ok) { c->moveTo (*r->track); session.select (r->track, c); }
                    }
            }
            else if (drag == Drag::trimRight)
            {
                auto ne = snap (dragClipStart + dragClipLen + delta);
                if (ne - dragClipStart >= minLen) c->setLength (ne - dragClipStart, true);
            }
            else
            {
                auto ns = snap (juce::jmax (te::TimePosition(), dragClipStart + delta));
                if ((dragClipStart + dragClipLen) - ns >= minLen) c->setStart (ns, true, false);
            }
            break;
        }
        case Drag::none: break;
    }
    repaint();
}

void ArrangeView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::moveClip || drag == Drag::trimLeft || drag == Drag::trimRight)
        if (auto* sc = dynamic_cast<te::StepClip*> (te::findClipForID (*session.edit, dragClip)))
            if (drag == Drag::trimRight)
            {
                const int bars = juce::jmax (1, juce::roundToInt (sc->getPosition().getLength().inSeconds() / session.barLength().inSeconds()));
                session.setStepClipBars (*sc, bars);
            }
    drag = Drag::none; dragParam = nullptr; dragPoint = -1;
    repaint();
}

void ArrangeView::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto cursor = juce::MouseCursor::NormalCursor;
    if (p.y < rulerH && p.x > headerW) cursor = juce::MouseCursor::PointingHandCursor;
    else if (auto* r = rowAt (p.y))
    {
        if (p.x > headerW && ! r->automation) { int edge = 0; if (clipAt (*r, p, edge)) cursor = edge != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor; }
        else if (p.x > headerW) cursor = juce::MouseCursor::CrosshairCursor;
    }
    setMouseCursor (cursor);
}

void ArrangeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    auto* r = rowAt (p.y); if (r == nullptr || r->automation) return;
    auto& t = *r->track;
    if (p.x < headerW)
    {
        if (p.y < r->y + 28)
        {
            auto* w = new juce::AlertWindow ("Rename track", {}, juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", t.getName());
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            auto id = t.itemID;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int res)
            {
                if (res == 1) if (auto* tt = te::findAudioTrackForID (*session.edit, id))
                {
                    auto n = w->getTextEditorContents ("name").trim();
                    if (n.isNotEmpty()) { session.beginAction(); tt->setName (n.substring (0, 40)); }
                }
            }), true);
        }
        return;
    }
    int edge = 0;
    if (clipAt (*r, p, edge) != nullptr) { if (onOpenEditor) onOpenEditor(); return; }
    const auto at = session.snapToBar (xToTime (p.x));
    session.beginAction();
    switch (getKind (t))
    {
        case Kind::drums: if (auto* c = session.addStepClip (t, at, 4)) session.select (&t, c); break;
        case Kind::synth: case Kind::sampler: if (auto* c = session.addMidiClip (t, at, 1)) session.select (&t, c); break;
        case Kind::audio: if (onImportAudioRequest) onImportAudioRequest (&t, at); break;
    }
    if (onOpenEditor && getKind (t) != Kind::audio) onOpenEditor();
}

void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { zoom (std::exp (w.deltaY * 1.5), e.x); return; }
    float dx = w.deltaX, dy = w.deltaY;
    if (e.mods.isShiftDown()) { dx = dy; dy = 0; }
    scrollSecs = juce::jmax (0.0, scrollSecs - dx * 320.0 / pxPerSec);
    int total = 0; for (auto& r : rows()) total += r.h;
    scrollY = juce::jlimit (0, juce::jmax (0, total - (getHeight() - rulerH) + 40), scrollY - juce::roundToInt (dy * 320.0f));
    repaint();
}

void ArrangeView::mouseMagnify (const juce::MouseEvent& e, float s) { zoom (s, e.x); }

void ArrangeView::zoom (double f, int anchorX)
{
    if (anchorX < headerW) anchorX = headerW + (getWidth() - headerW) / 2;
    const double anchor = xToTime (anchorX).inSeconds();
    pxPerSec = juce::jlimit (4.0, 900.0, pxPerSec * f);
    scrollSecs = juce::jmax (0.0, anchor - (anchorX - headerW) / pxPerSec);
    repaint();
}

void ArrangeView::filesDropped (const juce::StringArray& files, int x, int y)
{
    te::AudioTrack* target = nullptr;
    if (auto* r = rowAt (y)) target = r->track;
    if (onFilesDropped) onFilesDropped (files, target, snap (xToTime (x)));
}

void ArrangeView::timerCallback()
{
    auto& tr = session.transport();
    const int px = timeToX (tr.getPosition());
    if (tr.isPlaying() && (px > getWidth() - 30 || px < headerW))
    {
        scrollSecs = juce::jmax (0.0, tr.getPosition().inSeconds() - 40.0 / pxPerSec);
        repaint();
    }
    else if (px != lastPlayheadX)
    {
        repaint (juce::jmin (px, lastPlayheadX) - 2, 0, std::abs (px - lastPlayheadX) + 4, getHeight());
    }
    lastPlayheadX = px;
}

//==============================================================================
void ArrangeView::showClipMenu (te::Clip& c)
{
    juce::PopupMenu m;
    auto* w = dynamic_cast<te::WaveAudioClip*> (&c);
    m.addItem (1, w ? "Speed, pitch and fades" : dynamic_cast<te::StepClip*> (&c) ? "Edit steps" : "Edit notes");
    if (w)
    {
        m.addItem (4, w->getIsReversed() ? "Play forwards again" : "Reverse");
        m.addItem (5, "Make a playable instrument from this sound");
        juce::PopupMenu tm;
        tm.addItem (8, "Melody (one note at a time: voice, bass, lead)");
        tm.addItem (9, "Chords (several notes at once)");
        tm.addItem (10, "Drums (kick, snare, hi-hat)");
        m.addSubMenu ("Turn into MIDI notes", tm);
    }
    m.addSeparator();
    m.addItem (2, "Split at playhead (B)"); m.addItem (3, "Duplicate (Cmd+D)"); m.addItem (6, "Loop this clip");
    m.addSeparator(); m.addItem (7, "Delete clip");
    auto id = c.itemID;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, id] (int r)
    {
        auto* c = te::findClipForID (*session.edit, id); if (c == nullptr) return;
        auto* w = dynamic_cast<te::WaveAudioClip*> (c);
        switch (r)
        {
            case 1: if (onOpenEditor) onOpenEditor(); break;
            case 2: session.splitAtPlayhead(); break;
            case 3: session.duplicateClip (*c); break;
            case 4: if (w) session.setReversed (*w, ! w->getIsReversed()); break;
            case 5: if (w) session.makeSamplerFromClip (*w); break;
            case 6: session.loopClip (*c); break;
            case 7: session.deleteClip (*c); break;
            case 8: case 9: case 10:
                if (w) session.convertToMidi (*w, r == 8 ? TranscribeMode::melody : r == 9 ? TranscribeMode::chords : TranscribeMode::drums);
                break;
            default: break;
        }
        repaint();
    });
}

void ArrangeView::showTrackMenu (te::AudioTrack& t)
{
    juce::PopupMenu m, colours;
    const char* names[] = { "Sage", "Rose", "Periwinkle", "Lilac", "Moss", "Cyan", "Apricot", "Mustard" };
    for (int i = 0; i < 8; ++i) colours.addItem (100 + i, names[i], true, t.getColour() == trackColours[i]);
    m.addItem (1, "Rename (double-click the name)", false);
    m.addItem (2, "Duplicate track");
    m.addItem (3, "Move up", session.tracks().indexOf (&t) > 0);
    m.addItem (4, "Move down", session.tracks().indexOf (&t) < session.tracks().size() - 1);
    m.addSubMenu ("Colour", colours);
    m.addSeparator(); m.addItem (9, "Delete track");
    auto id = t.itemID;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, id] (int r)
    {
        auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr || r == 0) return;
        session.beginAction();
        if (r == 2) session.duplicateTrack (*t);
        else if (r == 3) session.moveTrack (*t, -1);
        else if (r == 4) session.moveTrack (*t, 1);
        else if (r == 9) session.deleteTrack (*t);
        else if (r >= 100) t->setColour (trackColours[r - 100]);
        repaint();
    });
}

void ArrangeView::showAutomationMenu (te::AudioTrack& t)
{
    juce::PopupMenu m;
    auto targets = session.automationTargets (t);
    auto* current = session.getAutomationParam (t);
    m.addSectionHeader ("Change over time");
    for (int i = 0; i < targets.size(); ++i)
        m.addItem (i + 1, targets[i].label + (targets[i].param->hasAutomationPoints() ? "  (on)" : ""), true, targets[i].param == current);
    m.addSeparator();
    m.addItem (1000, "Remove all points for this setting", current != nullptr && current->hasAutomationPoints());
    m.addItem (1001, "Hide this lane");
    if (targets.size() <= 2) m.addSectionHeader ("Turn on effects in the Effects tab to automate them");
    auto id = t.itemID;
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, id, targets] (int r)
    {
        auto* t = te::findAudioTrackForID (*session.edit, id); if (t == nullptr || r == 0) return;
        if (r == 1000) { session.beginAction(); if (auto* p = session.getAutomationParam (*t)) p->getCurve().clear(); }
        else if (r == 1001) t->state.setProperty (IDs::autoShown, false, nullptr);
        else if (r - 1 < targets.size()) session.showAutomation (*t, targets[r - 1].key);
        repaint();
    });
}
}
