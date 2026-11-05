#pragma once
#include "ArrangeView.h"
#include "Editors.h"
#include "Panels.h"
#include "LinesEditor.h"

namespace Reel
{
/** Puts a panel in a scrollable viewport and keeps its size right as it rebuilds. */
class ScrollWrap : public juce::Component
{
public:
    ScrollWrap (RebuildingPanel& p, bool horizontalOnly = false) : panel (p), horizontal (horizontalOnly)
    {
        view.setViewedComponent (&panel, false);
        view.setScrollBarsShown (! horizontal, horizontal);
        addAndMakeVisible (view);
        panel.onContentChanged = [this] { resized(); };
    }
    void resized() override
    {
        view.setBounds (getLocalBounds());
        const int w = view.getMaximumVisibleWidth(), h = view.getMaximumVisibleHeight();
        if (horizontal) panel.setSize (juce::jmax (w, 12 + 104 * (session_tracks() + 1)), h);
        else panel.setSize (w, juce::jmax (h, panel.requiredHeight (w)));
    }
    std::function<int()> trackCount;
private:
    int session_tracks() const { return trackCount ? trackCount() : 4; }
    RebuildingPanel& panel; bool horizontal; juce::Viewport view;
};

class MainComponent : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer,
                      private juce::MidiKeyboardState::Listener
{
public:
    explicit MainComponent (Session&);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void importFiles (const juce::StringArray&, te::AudioTrack* target, te::TimePosition at);
    void showMessage (const juce::String&);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override  { session.keyboardNote (note, vel, true); }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override     { session.keyboardNote (note, 0, false); }
    void showAddMenu();
    void showProjectMenu();
    void showHelp();
    void browseForAudio (te::AudioTrack* target, std::optional<te::TimePosition> at);
    void openEditorTab() { tabs.setCurrentTabIndex (0); }
    void updateTransportButtons();

    Session& session;
    juce::TextButton bStart { "|<" }, bPlay { "Play" }, bStop { "Stop" }, bRec { "Record" }, bLoop { "Loop" }, bClick { "Click" },
                     bZoomOut { "-" }, bZoomIn { "+" }, bAdd { "Add track" }, bUndo { "Undo" }, bRedo { "Redo" }, bProject { "Project" }, bHelp { "?" };
    juce::Label counter, timeLabel, tempoCaption { {}, "Tempo" }, snapCaption { {}, "Snap" }, midiLabel, toast;
    juce::Slider tempo;
    juce::ComboBox snapBox;
    ArrangeView arrange;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    EditorPanel editorPanel;
    SoundPanel soundPanel; EffectsPanel effectsPanel; MixerPanel mixerPanel; LinesPanel linesPanel;
    ScrollWrap soundWrap { soundPanel }, effectsWrap { effectsPanel }, mixerWrap { mixerPanel, true };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    std::unique_ptr<juce::FileChooser> chooser;
    int dockHeight = 340, dragStartDock = 0, octave = 4;
    std::map<int, int> heldComputerKeys;   // key code -> note
    juce::uint32 toastUntil = 0;
    juce::Rectangle<int> divider;
};
}
