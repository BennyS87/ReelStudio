#pragma once
#include "Widgets.h"
#include "Engines.h"

namespace Reel
{
/** Orbits: drag the point to blend the four layers at the corners; the moving dot shows where the orbit is now. */
class XYPad : public juce::Component, private juce::Timer
{
public:
    XYPad (EnginePlugin& e, juce::UndoManager& u) : eng (&e), um (u) { startTimerHz (30); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2);
        g.setColour (Col::lane); g.fillRoundedRectangle (r, 8);
        g.setColour (Col::line2);
        for (int i = 1; i < 4; ++i) { g.drawVerticalLine ((int) (r.getX() + r.getWidth() * i / 4), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * i / 4), r.getX(), r.getRight()); }
        g.setFont (juce::FontOptions (12.0f));
        const juce::Justification just[] = { juce::Justification::topLeft, juce::Justification::topRight, juce::Justification::bottomLeft, juce::Justification::bottomRight };
        for (int i = 0; i < EnginePlugin::numLayers; ++i)
        {
            g.setColour (eng->layerLoaded (i) ? Col::text : Col::muted);
            g.drawText (juce::String (i + 1) + ". " + (eng->layerLoaded (i) ? eng->layerName (i) : juce::String ("empty")), r.reduced (8).toNearestInt(), just[i]);
        }
        auto toPt = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight()); };
        // the path the orbit follows
        const float cx = eng->posX->getCurrentValue(), cy = eng->posY->getCurrentValue(), rad = eng->radius->getCurrentValue();
        const int m = eng->motion.get();
        if (m > 0)
        {
            juce::Path p;
            for (int i = 0; i <= 200; ++i)
            {
                const double ph = i / 200.0 * juce::MathConstants<double>::twoPi * (m == 3 ? 4 : 1);
                float x = cx, y = cy;
                if (m == 1) { x += rad * (float) std::cos (ph); y += rad * (float) std::sin (ph); }
                if (m == 2) { x += rad * (float) std::sin (ph); y += rad * 0.6f * (float) std::sin (2 * ph); }
                if (m == 3) { const float rr = rad * (float) (0.5 + 0.5 * std::cos (ph * 0.25)); x += rr * (float) std::cos (ph); y += rr * (float) std::sin (ph); }
                auto pt = toPt (juce::jlimit (0.0f, 1.0f, x), juce::jlimit (0.0f, 1.0f, y));
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (Col::acc.withAlpha (0.35f)); g.strokePath (p, juce::PathStrokeType (1.5f));
        }
        auto c = toPt (cx, cy);
        g.setColour (Col::acc); g.drawEllipse (juce::Rectangle<float> (16, 16).withCentre (c), 2.0f);
        auto live = toPt (eng->shownX.load(), eng->shownY.load());
        g.setColour (Col::text); g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre (live));
    }
    void mouseDown (const juce::MouseEvent& e) override { um.beginNewTransaction(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat().reduced (2);
        eng->posX->setParameter (juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth()), juce::sendNotification);
        eng->posY->setParameter (juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight()), juce::sendNotification);
        if (eng->motion.get() == 0) { eng->shownX = eng->posX->getCurrentValue(); eng->shownY = eng->posY->getCurrentValue(); }
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    juce::ReferenceCountedObjectPtr<EnginePlugin> eng;
    juce::UndoManager& um;
};

/** Threads: one bar per step; drag up and down to set its velocity, drag to the bottom for a rest. */
class LaneEditor : public juce::Component, private juce::Timer
{
public:
    LaneEditor (EnginePlugin& e, int l, juce::UndoManager& u) : eng (&e), lane (l), um (u) { startTimerHz (20); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Col::lane); g.fillRoundedRectangle (r, 6);
        const int steps = eng->laneSteps[lane].get() + 1;
        const auto v = eng->laneVelocities (lane);
        const int now = eng->shownStep[lane].load();
        const float w = r.getWidth() / (float) EnginePlugin::maxSteps;
        for (int s = 0; s < EnginePlugin::maxSteps; ++s)
        {
            auto cell = juce::Rectangle<float> (r.getX() + s * w, r.getY(), w, r.getHeight()).reduced (2, 4);
            if (s >= steps) { g.setColour (Col::line2.withAlpha (0.3f)); g.fillRoundedRectangle (cell.withTop (cell.getBottom() - 3), 1.5f); continue; }
            g.setColour (Col::line2); g.fillRoundedRectangle (cell, 3);
            const float val = v[(size_t) s];
            g.setColour (s == now ? Col::text : Col::acc.withAlpha (0.4f + 0.6f * val));
            g.fillRoundedRectangle (cell.withTop (cell.getBottom() - cell.getHeight() * val), 3);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override { um.beginNewTransaction(); mouseDrag (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = getLocalBounds().toFloat();
        const int s = (int) ((e.position.x - r.getX()) / (r.getWidth() / (float) EnginePlugin::maxSteps));
        if (s < 0 || s >= eng->laneSteps[lane].get() + 1) return;
        float val = 1.0f - (e.position.y - r.getY() - 4) / (r.getHeight() - 8);
        if (e.mods.isPopupMenu() || val < 0.08f) val = 0.0f;
        eng->setLaneVelocity (lane, s, juce::jlimit (0.0f, 1.0f, val), &um);
        repaint();
    }
private:
    void timerCallback() override { repaint(); }
    juce::ReferenceCountedObjectPtr<EnginePlugin> eng;
    int lane; juce::UndoManager& um;
};
}
