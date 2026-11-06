#include "Dock.h"

namespace Reel
{
using namespace juce;

PanelWindow::PanelWindow (const String& name, Component& content)
    : DocumentWindow ("Reel Studio: " + name, Col::bg, DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);   // gives the green full-screen button on the Mac
    setContentNonOwned (&content, false);
    content.setVisible (true);
    setResizable (true, true);
    setResizeLimits (320, 200, 10000, 10000);
}

//==============================================================================
DockArea::DockArea (DockPlace p) : place (p)
{
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);
    menuButton.setTooltip ("Move this section to the side, into its own window, or let it fill the window");
    menuButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (menuButton);
}

void DockArea::resized()
{
    tabs.setBounds (getLocalBounds());
    menuButton.setBounds (getWidth() - 40, 4, 34, 26);
}

//==============================================================================
DockManager::DockManager()
{
    for (auto* a : areas)
    {
        addChildComponent (*a);
        a->menuButton.onClick = [this, a] { showAreaMenu (*a); };
    }
}

DockManager::~DockManager()
{
    for (auto& p : panels) p->window.reset();
    for (auto* a : areas) a->tabs.clearTabs();
}

void DockManager::setCentre (Component& c) { centre = &c; addAndMakeVisible (c); }

void DockManager::addPanel (const String& name, Component& content)
{
    auto p = std::make_unique<Panel>();
    p->name = name; p->content = &content;
    panels.push_back (std::move (p));
    rebuildAreas();
}

DockManager::Panel* DockManager::find (const String& name) const
{
    for (auto& p : panels) if (p->name == name) return p.get();
    return nullptr;
}

StringArray DockManager::panelNames() const { StringArray s; for (auto& p : panels) s.add (p->name); return s; }
DockPlace DockManager::placeOf (const String& name) const { if (auto* p = find (name)) return p->place; return DockPlace::bottom; }
PanelWindow* DockManager::windowFor (const String& name) const { if (auto* p = find (name)) return p->window.get(); return nullptr; }

void DockManager::rebuildAreas()
{
    for (auto* a : areas)
    {
        const auto current = a->currentName();
        a->tabs.clearTabs();
        for (auto& p : panels)
            if ((int) p->place == (int) a->place)
            {
                if (auto* parent = p->content->getParentComponent(); parent != nullptr && parent != &a->tabs) parent->removeChildComponent (p->content);
                a->tabs.addTab (p->name, Col::panel, p->content, false);
            }
        for (int i = 0; i < a->tabs.getNumTabs(); ++i) if (a->tabs.getTabNames()[i] == current) a->tabs.setCurrentTabIndex (i);
        a->setVisible (a->tabs.getNumTabs() > 0);
    }
    if (fill >= 0 && fill < 3 && areas[fill]->tabs.getNumTabs() == 0) fill = -1;
    resized();
}

void DockManager::openWindow (Panel& p)
{
    if (auto* parent = p.content->getParentComponent()) parent->removeChildComponent (p.content);
    p.window = std::make_unique<PanelWindow> (p.name, *p.content);
    auto bounds = p.windowBounds;
    if (bounds.isEmpty())
    {
        auto screen = getScreenBounds();
        bounds = Rectangle<int> (jmin (1100, screen.getWidth() - 80), jmin (560, screen.getHeight() - 80)).withCentre (screen.getCentre() + Point<int> (40, 40));
    }
    p.window->setBounds (bounds);
    p.window->keys = keys; p.window->keyState = keyState;
    p.window->onClose = [sp = SafePointer<DockManager> (this), name = p.name]
    {
        if (sp != nullptr) if (auto* q = sp->find (name)) sp->movePanel (name, q->docked);
    };
    p.window->setVisible (true);
    p.window->toFront (true);
}

void DockManager::movePanel (const String& name, DockPlace place)
{
    auto* p = find (name);
    if (p == nullptr) return;
    if (p->window != nullptr)
    {
        if (place == DockPlace::window) { p->window->toFront (true); return; }
        p->windowBounds = p->window->getBounds();
        p->window.reset();
    }
    p->place = place;
    if (place != DockPlace::window) p->docked = place;
    rebuildAreas();
    if (place == DockPlace::window) openWindow (*p);
    else showPanel (name);
    changed();
}

void DockManager::showPanel (const String& name)
{
    auto* p = find (name);
    if (p == nullptr) return;
    if (p->window != nullptr) { p->window->toFront (true); return; }
    auto* a = areas[(int) p->place];
    for (int i = 0; i < a->tabs.getNumTabs(); ++i) if (a->tabs.getTabNames()[i] == name) a->tabs.setCurrentTabIndex (i);
    if (fill >= 0 && fill != (int) p->place) setFill (-1);   // make sure it can be seen
}

void DockManager::setFill (int area)
{
    fill = (area >= 0 && area < 3 && areas[area]->tabs.getNumTabs() == 0) ? -1 : area;
    resized();
    changed();
}

void DockManager::resetLayout()
{
    for (auto& p : panels) { p->window.reset(); p->place = p->docked = DockPlace::bottom; p->windowBounds = {}; }
    sizes[0] = 340; sizes[1] = 380; sizes[2] = 420; fill = -1;
    rebuildAreas();
    changed();
}

void DockManager::resized()
{
    for (auto& s : splitters) s = {};
    auto r = getLocalBounds();
    if (fill == 3) { for (auto* a : areas) a->setVisible (false); if (centre) { centre->setVisible (true); centre->setBounds (r); } return; }
    if (fill >= 0)
    {
        if (centre) centre->setVisible (false);
        for (int i = 0; i < 3; ++i) { areas[i]->setVisible (i == fill); if (i == fill) areas[i]->setBounds (r); }
        return;
    }
    if (centre) centre->setVisible (true);
    const int minCentreW = 260, minCentreH = 120;
    if (left.tabs.getNumTabs() > 0)
    {
        sizes[1] = jlimit (240, jmax (240, r.getWidth() - minCentreW - (right.tabs.getNumTabs() > 0 ? sizes[2] : 0)), sizes[1]);
        left.setVisible (true); left.setBounds (r.removeFromLeft (sizes[1])); splitters[1] = r.removeFromLeft (6);
    }
    if (right.tabs.getNumTabs() > 0)
    {
        sizes[2] = jlimit (240, jmax (240, r.getWidth() - minCentreW), sizes[2]);
        right.setVisible (true); right.setBounds (r.removeFromRight (sizes[2])); splitters[2] = r.removeFromRight (6);
    }
    if (bottom.tabs.getNumTabs() > 0)
    {
        sizes[0] = jlimit (120, jmax (120, r.getHeight() - minCentreH), sizes[0]);
        bottom.setVisible (true); bottom.setBounds (r.removeFromBottom (sizes[0])); splitters[0] = r.removeFromBottom (7);
    }
    if (centre) centre->setBounds (r);
}

void DockManager::paint (Graphics& g)
{
    g.fillAll (Col::bg);
    for (int i = 0; i < 3; ++i)
        if (! splitters[i].isEmpty())
        {
            g.setColour (dragging == i ? Col::acc : Col::line2);
            auto c = splitters[i].getCentre().toFloat();
            if (i == 0) g.fillRoundedRectangle (Rectangle<float> (46, 3).withCentre (c), 1.5f);
            else g.fillRoundedRectangle (Rectangle<float> (3, 46).withCentre (c), 1.5f);
        }
}

int DockManager::splitterAt (Point<int> p) const
{
    for (int i = 0; i < 3; ++i) if (splitters[i].expanded (i == 0 ? 0 : 3, i == 0 ? 3 : 0).contains (p)) return i;
    return -1;
}

void DockManager::mouseMove (const MouseEvent& e)
{
    const int s = splitterAt (e.getPosition());
    setMouseCursor (s == 0 ? MouseCursor::UpDownResizeCursor : s > 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void DockManager::mouseDown (const MouseEvent& e) { dragging = splitterAt (e.getPosition()); if (dragging >= 0) dragStart = sizes[dragging]; repaint(); }

void DockManager::mouseDrag (const MouseEvent& e)
{
    if (dragging < 0) return;
    const int d = dragging == 0 ? -e.getDistanceFromDragStartY() : dragging == 1 ? e.getDistanceFromDragStartX() : -e.getDistanceFromDragStartX();
    sizes[dragging] = dragStart + d;
    resized(); repaint();
}

void DockManager::mouseUp (const MouseEvent&) { if (dragging >= 0) { dragging = -1; repaint(); changed(); } }

Rectangle<int> DockManager::bottomTabBar() const
{
    if (! bottom.isVisible()) return {};
    return bottom.getBounds().withHeight (34);
}

void DockManager::showAreaMenu (DockArea& a)
{
    const auto name = a.currentName();
    PopupMenu m;
    m.addSectionHeader (name);
    m.addItem (1, "Dock at the bottom", a.place != DockPlace::bottom);
    m.addItem (2, "Dock on the left", a.place != DockPlace::left);
    m.addItem (3, "Dock on the right", a.place != DockPlace::right);
    m.addItem (4, "Open in a new window");
    m.addSeparator();
    const int me = (int) a.place;
    m.addItem (5, fill == me ? "Back to the normal layout" : "Fill the window");
    if (addGlobalItems) addGlobalItems (m);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (a.menuButton), [this, name, me] (int r)
    {
        if (r >= 1 && r <= 4) { if (fill >= 0) setFill (-1); movePanel (name, (DockPlace) (r - 1)); }
        else if (r == 5) setFill (fill == me ? -1 : me);
    });
}

ValueTree DockManager::getState() const
{
    ValueTree v ("LAYOUT");
    v.setProperty ("bottom", sizes[0], nullptr); v.setProperty ("left", sizes[1], nullptr); v.setProperty ("right", sizes[2], nullptr);
    for (auto& p : panels)
    {
        ValueTree c ("SECTION");
        c.setProperty ("name", p->name, nullptr);
        c.setProperty ("place", (int) p->place, nullptr); c.setProperty ("docked", (int) p->docked, nullptr);
        const auto wb = p->window ? p->window->getBounds() : p->windowBounds;
        if (! wb.isEmpty()) c.setProperty ("window", wb.toString(), nullptr);
        v.addChild (c, -1, nullptr);
    }
    for (auto* a : areas) v.setProperty ("current" + String ((int) a->place), a->currentName(), nullptr);
    return v;
}

void DockManager::restoreState (const ValueTree& v)
{
    if (! v.hasType ("LAYOUT")) return;
    sizes[0] = v.getProperty ("bottom", 340); sizes[1] = v.getProperty ("left", 380); sizes[2] = v.getProperty ("right", 420);
    std::vector<String> toOpen;
    for (auto c : v)
        if (auto* p = find (c.getProperty ("name").toString()))
        {
            p->place = (DockPlace) jlimit (0, 3, (int) c.getProperty ("place"));
            p->docked = (DockPlace) jlimit (0, 2, (int) c.getProperty ("docked"));
            p->windowBounds = Rectangle<int>::fromString (c.getProperty ("window").toString());
            if (p->place == DockPlace::window) toOpen.push_back (p->name);
        }
    rebuildAreas();
    for (auto* a : areas)
    {
        const auto cur = v.getProperty ("current" + String ((int) a->place)).toString();
        for (int i = 0; i < a->tabs.getNumTabs(); ++i) if (a->tabs.getTabNames()[i] == cur) a->tabs.setCurrentTabIndex (i);
    }
    // windows open once the main window is on screen, so they appear in front of it
    for (auto& n : toOpen) MessageManager::callAsync ([sp = SafePointer<DockManager> (this), n] { if (sp) if (auto* p = sp->find (n); p != nullptr && p->window == nullptr) sp->openWindow (*p); });
}
}
