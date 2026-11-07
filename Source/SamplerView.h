#pragma once
#include "Session.h"
#include "Widgets.h"

namespace Reel
{
inline juce::Colour sliceColour (int i)
{
    static const juce::uint32 c[] = { 0xffe8c547, 0xff5fc7d6, 0xffe0774f, 0xff9b8de8, 0xff7fd37a, 0xffe36fa5, 0xff4f9be0, 0xffd6a35f, 0xff65d6b0, 0xffc96fe0, 0xffb5d65f, 0xffe05f6a };
    return juce::Colour (c[(size_t) (i % 12)]);
}

/** Plays one slice briefly through the selected track, as if its key were pressed. */
inline void auditionNote (Session& s, int note)
{
    s.keyboardNote (note, 0.85f, true);
    juce::Timer::callAfterDelay (350, [&s, note] { s.keyboardNote (note, 0.0f, false); });
}

/** The sound's waveform. In Slice mode: coloured slices you can drag, add (double-click) and remove (right-click);
    click a slice to hear it. Otherwise: drag the start point and the loop region. */
class SampleView : public juce::Component, private juce::Timer
{
public:
    SampleView (ReelSampler& s, Session& ses, std::shared_ptr<int> sel) : smp (&s), session (ses), selected (std::move (sel)) { startTimerHz (25); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Col::lane); g.fillRoundedRectangle (r, 6);
        auto snd = smp->currentSound();
        if (snd == nullptr) return;
        if (snd != cachedFor) buildPeaks (*snd);
        const bool slicing = smp->oneShot.get() == ReelSampler::slice;
        const auto sl = smp->getSlices();
        auto xOf = [&] (double f) { return (float) (f * getWidth()); };
        if (slicing)
            for (int i = 0; i < (int) sl.size(); ++i)
            {
                const float x0 = xOf (sl[(size_t) i]), x1 = xOf (i + 1 < (int) sl.size() ? sl[(size_t) i + 1] : 1.0);
                g.setColour (sliceColour (i).withAlpha (i == *selected ? 0.28f : (i == smp->lastSlicePlayed.load() ? 0.18f : 0.07f)));
                g.fillRect (x0, 0.0f, x1 - x0, (float) getHeight());
            }
        else
        {
            g.setColour (Col::lane2); g.fillRect (0.0f, 0.0f, xOf (smp->startAt->getCurrentValue()), (float) getHeight());
            if (smp->loopMode.get() == 1)
            {
                g.setColour (Col::acc.withAlpha (0.12f));
                g.fillRect (xOf (smp->loopStart->getCurrentValue()), 0.0f, xOf (smp->loopEnd->getCurrentValue()) - xOf (smp->loopStart->getCurrentValue()), (float) getHeight());
            }
        }
        const float mid = getHeight() * 0.5f;
        g.setColour (Col::acc.withAlpha (0.85f));
        for (int x = 0; x < (int) peaks.size(); ++x) g.drawVerticalLine (x, mid - peaks[(size_t) x] * mid * 0.92f, mid + peaks[(size_t) x] * mid * 0.92f);
        if (slicing)
            for (int i = 0; i < (int) sl.size(); ++i)
            {
                const float x = xOf (sl[(size_t) i]);
                g.setColour (sliceColour (i)); g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
                g.fillRect (x - 1.0f, 0.0f, 9.0f, 7.0f);
                g.setColour (Col::text); g.setFont (juce::FontOptions (10.5f));
                if (i + 1 >= (int) sl.size() || xOf (sl[(size_t) i + 1]) - x > 24) g.drawText (noteName (ReelSampler::firstSliceNote + i), (int) x + 3, getHeight() - 15, 40, 13, juce::Justification::left);
            }
        else
            for (auto* p : { smp->startAt, smp->loopStart, smp->loopEnd })
            {
                if (p != smp->startAt && smp->loopMode.get() != 1) continue;
                g.setColour (p == smp->startAt ? Col::text : Col::acc); g.fillRect (xOf (p->getCurrentValue()) - 1.0f, 0.0f, 2.0f, (float) getHeight());
            }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        session.undoManager().beginNewTransaction();
        dragMarker = -1; dragParam = nullptr;
        const double f = juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth());
        if (smp->oneShot.get() != ReelSampler::slice)
        {
            float best = 8.0f;
            for (auto* p : { smp->startAt, smp->loopStart, smp->loopEnd })
                if ((p == smp->startAt || smp->loopMode.get() == 1) && std::abs (p->getCurrentValue() * getWidth() - e.position.x) < best) { best = std::abs (p->getCurrentValue() * getWidth() - e.position.x); dragParam = p; }
            if (dragParam == nullptr) dragParam = smp->startAt;
            mouseDrag (e);
            return;
        }
        auto sl = smp->getSlices();
        for (int i = 1; i < (int) sl.size(); ++i)
            if (std::abs (sl[(size_t) i] * getWidth() - e.position.x) < 6.0)
            {
                if (e.mods.isPopupMenu()) { sl.erase (sl.begin() + i); smp->sliceMethod = 2; smp->setSlices (sl, &session.undoManager()); repaint(); return; }
                dragMarker = i; return;
            }
        int idx = 0; while (idx + 1 < (int) sl.size() && sl[(size_t) idx + 1] <= f) ++idx;
        *selected = idx;
        auditionNote (session, ReelSampler::firstSliceNote + idx);
        if (onSelect) onSelect();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double f = juce::jlimit (0.0, 1.0, (double) e.position.x / getWidth());
        if (dragParam != nullptr) { dragParam->setParameter ((float) f, juce::sendNotification); repaint(); return; }
        if (dragMarker < 1) return;
        auto sl = smp->getSlices();
        if (dragMarker >= (int) sl.size()) return;
        const double lo = sl[(size_t) dragMarker - 1] + 0.002, hi = dragMarker + 1 < (int) sl.size() ? sl[(size_t) dragMarker + 1] - 0.002 : 0.999;
        sl[(size_t) dragMarker] = juce::jlimit (lo, juce::jmax (lo, hi), f);
        smp->sliceMethod = 2;
        smp->setSlices (sl, &session.undoManager());
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (smp->oneShot.get() != ReelSampler::slice) return;
        auto sl = smp->getSlices();
        sl.push_back (juce::jlimit (0.001, 0.999, (double) e.position.x / getWidth()));
        smp->sliceMethod = 2;
        smp->setSlices (sl, &session.undoManager());
        if (onSelect) onSelect();
        repaint();
    }

    std::function<void()> onSelect;

private:
    void buildPeaks (const ReelSampler::Sound& s)
    {
        cachedFor = smp->currentSound();
        const int w = juce::jmax (1, getWidth()), n = s.data.getNumSamples();
        peaks.assign ((size_t) w, 0.0f);
        float top = 1e-6f;
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((int64_t) n * x / w), b = juce::jmax (a + 1, (int) ((int64_t) n * (x + 1) / w));
            float m = 0; for (int c = 0; c < s.data.getNumChannels(); ++c) m = juce::jmax (m, s.data.getMagnitude (c, a, b - a));
            peaks[(size_t) x] = m; top = juce::jmax (top, m);
        }
        for (auto& p : peaks) p /= top;
    }
    void resized() override { cachedFor.reset(); }
    void timerCallback() override { repaint(); }

    juce::ReferenceCountedObjectPtr<ReelSampler> smp;
    Session& session;
    std::shared_ptr<int> selected;
    std::shared_ptr<const ReelSampler::Sound> cachedFor;
    std::vector<float> peaks;
    int dragMarker = -1; te::AutomatableParameter* dragParam = nullptr;
};

/** One coloured pad per slice, labelled with its key. Click to hear it. */
class SlicePads : public juce::Component, private juce::Timer
{
public:
    SlicePads (ReelSampler& s, Session& ses, std::shared_ptr<int> sel) : smp (&s), session (ses), selected (std::move (sel)) { startTimerHz (20); }
    static int heightFor (int count, int width) { const int cols = juce::jmax (1, width / 64); return ((count + cols - 1) / cols) * 46; }
    void paint (juce::Graphics& g) override
    {
        const int n = (int) smp->getSlices().size(), cols = juce::jmax (1, getWidth() / 64);
        for (int i = 0; i < n; ++i)
        {
            auto r = cellFor (i, cols).reduced (3).toFloat();
            const bool lit = i == smp->lastSlicePlayed.load() || i == *selected;
            g.setColour (sliceColour (i).withAlpha (lit ? 1.0f : 0.55f)); g.fillRoundedRectangle (r, 5);
            if (i == *selected) { g.setColour (Col::text); g.drawRoundedRectangle (r, 5, 2); }
            g.setColour (juce::Colours::black.withAlpha (0.75f)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (noteName (ReelSampler::firstSliceNote + i), r.reduced (6, 4).toNearestInt(), juce::Justification::bottomLeft);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int n = (int) smp->getSlices().size(), cols = juce::jmax (1, getWidth() / 64);
        for (int i = 0; i < n; ++i)
            if (cellFor (i, cols).contains (e.getPosition())) { *selected = i; auditionNote (session, ReelSampler::firstSliceNote + i); if (onSelect) onSelect(); repaint(); return; }
    }
    std::function<void()> onSelect;
private:
    juce::Rectangle<int> cellFor (int i, int cols) const { return { (i % cols) * 64, (i / cols) * 46, 64, 46 }; }
    void timerCallback() override { repaint(); }
    juce::ReferenceCountedObjectPtr<ReelSampler> smp;
    Session& session;
    std::shared_ptr<int> selected;
};
}
