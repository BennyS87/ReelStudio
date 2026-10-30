#pragma once
#include "Session.h"
#include "Widgets.h"

namespace Reel
{
//==============================================================================
class PianoRoll : public juce::Component, private juce::Timer
{
public:
    PianoRoll (Session&, te::MidiClip&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;

    te::MidiClip* clip() const;
    juce::Array<te::MidiNote*> targets() const;          // selected notes, or all when none selected
    bool deleteSelected();
    void transpose (int semis);
    void quantize();
    void setVelocity (int v);
    double grid = 0.25, pxPerBeat = 64.0;

private:
    static constexpr int keysW = 54, rulerH = 20;
    float rowH = 14.0f, scrollY = -1.0f; double scrollBeat = 0, lastLen = 0.5;
    int  yForNote (int n) const    { return rulerH + juce::roundToInt ((127 - n) * rowH - scrollY); }
    int  noteAtY (int y) const     { return juce::jlimit (0, 127, 127 - (int) std::floor ((y - rulerH + scrollY) / rowH)); }
    int  xForBeat (double b) const { return keysW + juce::roundToInt ((b - scrollBeat) * pxPerBeat); }
    double beatAtX (int x) const   { return scrollBeat + (x - keysW) / pxPerBeat; }
    te::MidiNote* noteAt (juce::Point<int>) const;
    bool isValid (te::MidiNote*) const;
    void audition (int note, int vel = 100);
    void timerCallback() override { if (session.transport().isPlaying()) repaint(); }

    Session& session;
    te::EditItemID clipID;
    juce::Array<te::MidiNote*> selected;
    enum class Mode { none, move, resize, box } mode = Mode::none;
    struct Orig { te::MidiNote* n; double start, len; int pitch; };
    std::vector<Orig> orig;
    te::MidiNote* anchor = nullptr;
    double downBeat = 0; int downNote = 0, lastAuditioned = -1;
    bool created = false;
    juce::Rectangle<int> box;
};

//==============================================================================
class StepGrid : public juce::Component, private juce::Timer
{
public:
    StepGrid (Session&, te::StepClip&);
    void paint (juce::Graphics&) override;
    void resized() override { cell = juce::jlimit (14, 30, (getHeight() - 8) / numDrumVoices - gap); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    te::StepClip* clip() const { return dynamic_cast<te::StepClip*> (te::findClipForID (*session.edit, clipID)); }
private:
    static constexpr int nameW = 104, gap = 3;
    int cell = 30;
    juce::Rectangle<int> cellRect (int ch, int step) const;
    void timerCallback() override { repaint(); }
    void setCell (int ch, int step, int level);
    Session& session; te::EditItemID clipID;
    int paintLevel = -1;
};

//==============================================================================
/** Bottom-panel "Editor" tab: shows the right editor for the selected clip. */
class EditorPanel : public juce::Component, private juce::ChangeListener, public EditWatcher
{
public:
    explicit EditorPanel (Session&);
    ~EditorPanel() override;
    void editAboutToBeDeleted() override { controls.clear(); roll.reset(); steps.reset(); shownVersion = -1; shownClip = {}; }
    void resized() override;
    void paint (juce::Graphics&) override;
    bool deleteSelectedNotes() { return roll != nullptr && roll->deleteSelected(); }
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuild();
    Session& session;
    te::EditItemID shownClip; int shownVersion = -1;
    juce::OwnedArray<juce::Component> controls;
    std::unique_ptr<PianoRoll> roll;
    std::unique_ptr<StepGrid> steps;
    juce::Label hint;
    juce::Rectangle<int> controlsArea;
};
}
