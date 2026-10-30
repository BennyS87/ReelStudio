#pragma once
#include "Session.h"

namespace Reel
{
class ArrangeView : public juce::Component,
                    public juce::FileDragAndDropTarget,
                    public EditWatcher,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit ArrangeView (Session&);
    ~ArrangeView() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int x, int y) override;

    void zoom (double factor, int anchorX = -1);
    void editAboutToBeDeleted() override { thumbs.clear(); drag = Drag::none; dragParam = nullptr; dragPoint = -1; }
    double snapBeats = 1.0;                       // 0 = off
    std::function<void (const juce::StringArray&, te::AudioTrack*, te::TimePosition)> onFilesDropped;
    std::function<void (te::AudioTrack*, te::TimePosition)> onImportAudioRequest;
    std::function<void()> onOpenEditor;

    static constexpr int headerW = 214, rulerH = 30, trackH = 86, autoH = 64;

private:
    struct Row { te::AudioTrack* track; int y, h; bool automation; };
    std::vector<Row> rows() const;
    const Row* rowAt (int y) const;
    int timeToX (te::TimePosition t) const   { return headerW + juce::roundToInt ((t.inSeconds() - scrollSecs) * pxPerSec); }
    te::TimePosition xToTime (int x) const    { return te::TimePosition::fromSeconds (juce::jmax (0.0, scrollSecs + (x - headerW) / pxPerSec)); }
    te::TimePosition snap (te::TimePosition t, bool fine = false) const;
    juce::Rectangle<int> clipRect (const Row&, te::Clip&) const;
    te::Clip* clipAt (const Row&, juce::Point<int>, int& edge) const;   // edge: -1 left, 1 right, 0 body
    void paintRuler (juce::Graphics&);
    void paintHeader (juce::Graphics&, const Row&);
    void paintLane (juce::Graphics&, const Row&);
    void paintClip (juce::Graphics&, te::AudioTrack&, te::Clip&, juce::Rectangle<int>);
    void paintAutomation (juce::Graphics&, const Row&);
    void showClipMenu (te::Clip&);
    void showTrackMenu (te::AudioTrack&);
    void showAutomationMenu (te::AudioTrack&);
    te::SmartThumbnail* thumbFor (te::WaveAudioClip&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void timerCallback() override;
    juce::Rectangle<int> headerButton (const Row&, int index) const;
    float automationY (const Row&, te::AutomatableParameter&, float value) const;

    Session& session;
    double pxPerSec = 56.0, scrollSecs = 0.0;
    int scrollY = 0;

    enum class Drag { none, seek, loop, moveClip, trimLeft, trimRight, autoPoint, volume };
    Drag drag = Drag::none;
    te::EditItemID dragClip;
    te::TimePosition dragClipStart, dragLoopAnchor;
    te::TimeDuration dragClipLen;
    int dragPoint = -1;
    te::AutomatableParameter* dragParam = nullptr;
    float dragVolStart = 0;
    bool dragMoved = false;
    std::map<te::EditItemID, std::unique_ptr<te::SmartThumbnail>> thumbs;
    int lastPlayheadX = -1;
};
}
