#pragma once
#include "Session.h"
#include "Widgets.h"
#include "LinesPlugin.h"

namespace Reel
{
class LinesCanvas : public juce::Component, private juce::Timer
{
    friend class ExpressionLane;
public:
    LinesCanvas (LinesPlugin&, juce::UndoManager&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;

    enum class Tool { draw, select } tool = Tool::draw;
    bool glideMode = true;
    double grid = 0.25, pxPerBeat = 48.0;
    juce::Array<juce::ValueTree> selected;
    std::function<void()> onSelectionChanged;
    void deleteSelected();
    void quantizeSelected();
    juce::Array<juce::ValueTree> targets() const;   // selected lines, or all of them
    struct View { float scrollY = -1.0f; double scrollBeat = 0, pxPerBeat = 48.0; };
    View getView() const { return { scrollY, scrollBeat, pxPerBeat }; }
    void setView (View v) { scrollY = v.scrollY; scrollBeat = v.scrollBeat; pxPerBeat = v.pxPerBeat; }

private:
    static constexpr int keysW = 46;
    float rowH = 12.0f, scrollY = -1.0f; double scrollBeat = 0;
    float yFor (float pitch) const    { return (127.0f - pitch) * rowH - scrollY + rowH * 0.5f; }
    float pitchAt (float y) const     { return 127.0f - (y + scrollY - rowH * 0.5f) / rowH; }
    float xFor (double beat) const    { return keysW + (float) ((beat - scrollBeat) * pxPerBeat); }
    double beatAt (float x) const     { return scrollBeat + (x - keysW) / pxPerBeat; }
    double snapT (double t, bool fine) const { return (grid <= 0 || fine) ? t : std::round (t / grid) * grid; }
    struct Hit { juce::ValueTree line; int point = -1; double t = 0; };
    Hit hitTest (juce::Point<float>) const;
    void timerCallback() override { repaint(); }

    juce::ReferenceCountedObjectPtr<LinesPlugin> plugin;
    juce::UndoManager& um;
    enum class Drag { none, newLine, point, moveLines, box } drag = Drag::none;
    juce::ValueTree dragLine; int dragPoint = -1;
    LineData drawing; double lastDrawT = 0;
    std::vector<std::pair<juce::ValueTree, LineData>> moveOrig;
    juce::Point<float> downPos; juce::Rectangle<float> box;
};

/** Pressure, slide and velocity for the selected lines (or all of them). Drag the stems. */
class ExpressionLane : public juce::Component
{
public:
    ExpressionLane (LinesPlugin& p, LinesCanvas& c, juce::UndoManager& u) : plugin (&p), canvas (c), um (u) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { um.beginNewTransaction(); edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    int mode = 0;   // 0 pressure, 1 slide, 2 velocity
private:
    void edit (const juce::MouseEvent&);
    float xFor (double beat) const;
    juce::ReferenceCountedObjectPtr<LinesPlugin> plugin;
    LinesCanvas& canvas; juce::UndoManager& um;
};

/** The Lines tab. */
class LinesPanel : public juce::Component, private juce::ChangeListener, public EditWatcher
{
public:
    explicit LinesPanel (Session&);
    ~LinesPanel() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (Col::panel); }
    void editAboutToBeDeleted() override { clear(); }
    bool deleteSelected() { if (canvas) { canvas->deleteSelected(); return true; } return false; }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    void clear();
    Session& session;
    te::EditItemID shownTrack; int shownVersion = -1; LinesPlugin* shownLines = nullptr; bool shownInstrument = false;
    LinesCanvas::View view;
    te::EditItemID shownClip;
    te::EditItemID audioClipFor (te::AudioTrack&) const;
    juce::OwnedArray<juce::Component> controls;
    std::unique_ptr<LinesCanvas> canvas;
    std::unique_ptr<ExpressionLane> lane;
    juce::Label hint;
    std::vector<LineData> clipboard;
};
}
