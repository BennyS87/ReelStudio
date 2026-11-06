#pragma once
#include "Widgets.h"

namespace Reel
{
enum class DockPlace { bottom = 0, left = 1, right = 2, window = 3 };

/** A section opened in its own window. Closing it puts the section back where it was docked. */
class PanelWindow : public juce::DocumentWindow
{
public:
    PanelWindow (const juce::String& name, juce::Component& content);
    ~PanelWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { if (onClose) juce::MessageManager::callAsync ([f = onClose] { f(); }); }
    bool keyPressed (const juce::KeyPress& k) override { return keys && keys (k); }
    bool keyStateChanged (bool down) override { return keyState && keyState (down); }
    std::function<void()> onClose;
    std::function<bool (const juce::KeyPress&)> keys;
    std::function<bool (bool)> keyState;
};

/** One dock area: tabs plus a menu for moving the current section. */
class DockArea : public juce::Component
{
public:
    explicit DockArea (DockPlace p);
    void resized() override;
    juce::String currentName() const { return tabs.getNumTabs() > 0 ? tabs.getCurrentTabName() : juce::String(); }
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TextButton menuButton { "..." };
    const DockPlace place;
};

/** Lays out the timeline and the sections around it. Every section can sit at the bottom, on either side or in its own
    window; areas are resized by dragging their edges, and any area (or the timeline) can fill the whole window. */
class DockManager : public juce::Component
{
public:
    DockManager();
    ~DockManager() override;

    void setCentre (juce::Component&);
    void addPanel (const juce::String& name, juce::Component& content);
    void movePanel (const juce::String& name, DockPlace);
    void showPanel (const juce::String& name);
    DockPlace placeOf (const juce::String& name) const;
    juce::StringArray panelNames() const;
    PanelWindow* windowFor (const juce::String& name) const;

    /** -1 normal, 0 bottom, 1 left, 2 right fill the window, 3 the timeline does. */
    void setFill (int area);
    int getFill() const { return fill; }
    void resetLayout();

    juce::ValueTree getState() const;
    void restoreState (const juce::ValueTree&);
    juce::Rectangle<int> bottomTabBar() const;   // where the bottom tabs are, for labels drawn over them

    std::function<void()> onLayoutChanged;
    std::function<void (juce::PopupMenu&)> addGlobalItems;   // extra items for each area's menu (full screen etc.)
    std::function<bool (const juce::KeyPress&)> keys;
    std::function<bool (bool)> keyState;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Panel
    {
        juce::String name; juce::Component* content = nullptr;
        DockPlace place = DockPlace::bottom, docked = DockPlace::bottom;
        std::unique_ptr<PanelWindow> window; juce::Rectangle<int> windowBounds;
    };
    Panel* find (const juce::String&) const;
    void rebuildAreas();
    void openWindow (Panel&);
    void showAreaMenu (DockArea&);
    void changed() { if (onLayoutChanged) onLayoutChanged(); }
    int splitterAt (juce::Point<int>) const;

    std::vector<std::unique_ptr<Panel>> panels;
    DockArea bottom { DockPlace::bottom }, left { DockPlace::left }, right { DockPlace::right };
    DockArea* areas[3] { &bottom, &left, &right };
    juce::Component* centre = nullptr;
    int sizes[3] { 340, 380, 420 };
    int fill = -1, dragging = -1, dragStart = 0;
    juce::Rectangle<int> splitters[3];
};
}
