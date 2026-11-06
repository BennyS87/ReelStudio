#include "MainComponent.h"

namespace Reel
{
static const char* pianoKeys = "awsedftgyhujkolp;";

MainComponent::MainComponent (Session& s)
    : session (s), arrange (s), editorPanel (s), soundPanel (s), effectsPanel (s), mixerPanel (s), linesPanel (s)
{
    setWantsKeyboardFocus (true);
    session.addChangeListener (this);
    session.onMessage = [this] (const juce::String& m) { showMessage (m); };
    mixerWrap.trackCount = [this] { return session.tracks().size(); };

    for (auto* b : { &bStart, &bPlay, &bStop, &bRec, &bLoop, &bClick, &bZoomOut, &bZoomIn, &bAdd, &bUndo, &bRedo, &bProject, &bHelp, &bView })
    { addAndMakeVisible (*b); b->setWantsKeyboardFocus (false); }
    bAdd.setColour (juce::TextButton::buttonColourId, Col::acc);
    bAdd.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff241800));
    bStart.onClick = [this] { session.seek (session.transport().looping ? session.transport().getLoopRange().getStart() : te::TimePosition()); };
    bPlay.onClick  = [this] { session.togglePlay(); };
    bStop.onClick  = [this] { session.stop (true); };
    bRec.onClick   = [this] { session.toggleRecord(); };
    bLoop.onClick  = [this]
    {
        auto& tr = session.transport();
        if (! tr.looping && tr.getLoopRange().getLength() < session.barLength())
        {
            if (auto* c = session.getSelectedClip()) tr.setLoopRange (c->getEditTimeRange());
            else tr.setLoopRange ({ te::TimePosition(), session.barLength() * 4 });
        }
        tr.looping = ! tr.looping;
        if (tr.looping) showMessage ("Looping. Drag in the lower half of the ruler to change the range.");
        arrange.repaint();
    };
    bClick.onClick = [this] { session.edit->clickTrackEnabled = ! session.edit->clickTrackEnabled.get(); };
    bZoomOut.onClick = [this] { arrange.zoom (1 / 1.4); };
    bZoomIn.onClick  = [this] { arrange.zoom (1.4); };
    bAdd.onClick     = [this] { showAddMenu(); };
    bUndo.onClick    = [this] { session.undoManager().undo(); };
    bRedo.onClick    = [this] { session.undoManager().redo(); };
    bProject.onClick = [this] { showProjectMenu(); };
    bView.onClick = [this] { showViewMenu(); };
    bView.setTooltip ("Move sections to the side or into their own windows, fill the window, full screen");
    bHelp.onClick    = [this] { showHelp(); };

    counter.setFont (juce::FontOptions (26.0f, juce::Font::bold)); counter.setColour (juce::Label::textColourId, Col::acc);
    counter.setColour (juce::Label::backgroundColourId, Col::lcd); counter.setJustificationType (juce::Justification::centred);
    timeLabel.setFont (juce::FontOptions (14.0f)); timeLabel.setColour (juce::Label::textColourId, Col::acc.withAlpha (0.7f));
    timeLabel.setColour (juce::Label::backgroundColourId, Col::lcd);
    for (auto* l : { &counter, &timeLabel, &tempoCaption, &snapCaption, &midiLabel }) addAndMakeVisible (*l);
    for (auto* l : { &tempoCaption, &snapCaption, &midiLabel }) { l->setColour (juce::Label::textColourId, Col::muted); l->setFont (juce::FontOptions (13.0f)); }

    tempo.setSliderStyle (juce::Slider::IncDecButtons);
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 26);
    tempo.setRange (30, 300, 1); tempo.setValue (session.getBpm(), juce::dontSendNotification);
    tempo.onValueChange = [this] { if (std::abs (tempo.getValue() - session.getBpm()) > 0.01) session.setBpm (tempo.getValue()); };
    addAndMakeVisible (tempo);

    for (auto [name, id] : { std::pair<const char*, int> { "Off", 1 }, { "1/16", 2 }, { "1/8", 3 }, { "Beat", 4 }, { "Half bar", 5 }, { "Bar", 6 } }) snapBox.addItem (name, id);
    snapBox.setSelectedId (4, juce::dontSendNotification);
    snapBox.onChange = [this] { const double v[] = { 0, 0.25, 0.5, 1, 2, 4 }; arrange.snapBeats = v[snapBox.getSelectedId() - 1]; };
    snapBox.setWantsKeyboardFocus (false);
    addAndMakeVisible (snapBox);

    arrange.onFilesDropped = [this] (const juce::StringArray& f, te::AudioTrack* t, te::TimePosition at) { importFiles (f, t, at); };
    arrange.onImportAudioRequest = [this] (te::AudioTrack* t, te::TimePosition at) { browseForAudio (t, at); };
    arrange.onOpenEditor = [this] { openEditorTab(); };

    dock.setCentre (arrange);
    dock.addPanel ("Editor", editorPanel);
    dock.addPanel ("Sound", soundWrap);
    dock.addPanel ("Effects", effectsWrap);
    dock.addPanel ("Mixer", mixerWrap);
    dock.addPanel ("Lines", linesPanel);
    dock.keys = [this] (const juce::KeyPress& k) { return keyPressed (k); };
    dock.keyState = [this] (bool down) { return keyStateChanged (down); };
    dock.onLayoutChanged = [this] { resized(); saveLayout(); };
    dock.addGlobalItems = [this] (juce::PopupMenu& m)
    {
        auto* w = findParentComponentOfClass<juce::DocumentWindow>();
        m.addItem ("Timeline fills the window", true, dock.getFill() == 3, [this] { dock.setFill (dock.getFill() == 3 ? -1 : 3); });
        m.addItem ("Full screen", true, w != nullptr && w->isFullScreen(), [this] { toggleFullScreen(); });
    };
    addAndMakeVisible (dock);
    loadLayout();

    keyboard.setLowestVisibleKey (36);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setKeyWidth (22.0f);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Col::acc);
    keyboardState.addListener (this);
    addAndMakeVisible (keyboard);

    midiLabel.setJustificationType (juce::Justification::centredRight);
    midiLabel.toFront (false);
    toast.setColour (juce::Label::backgroundColourId, Col::text);
    toast.setColour (juce::Label::textColourId, Col::bg);
    toast.setJustificationType (juce::Justification::centred);
    toast.setFont (juce::FontOptions (14.0f));
    addChildComponent (toast);

    setSize (1360, 860);
    startTimerHz (20);
    updateTransportButtons();
}

MainComponent::~MainComponent()
{
    keyboardState.removeListener (this);
    session.removeChangeListener (this);
    session.onMessage = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);
    g.setColour (Col::panel); g.fillRect (0, 0, getWidth(), 52);
    g.setColour (Col::line); g.drawHorizontalLine (52, 0, (float) getWidth());
    g.setColour (Col::text); g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("Reel", 14, 0, 60, 52, juce::Justification::centredLeft);
    g.setColour (Col::acc); g.fillEllipse (66, 31, 6, 6);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (52).reduced (8, 9);
    bar.removeFromLeft (76);
    for (auto* b : { &bStart, &bPlay, &bStop, &bRec, &bLoop, &bClick }) { b->setBounds (bar.removeFromLeft (b == &bRec ? 64 : b == &bStart ? 34 : 50)); bar.removeFromLeft (4); }
    bar.removeFromLeft (8);
    auto lcd = bar.removeFromLeft (150); counter.setBounds (lcd.removeFromLeft (86)); timeLabel.setBounds (lcd);
    bar.removeFromLeft (10);
    tempoCaption.setBounds (bar.removeFromLeft (44)); tempo.setBounds (bar.removeFromLeft (92)); bar.removeFromLeft (6);
    snapCaption.setBounds (bar.removeFromLeft (36)); snapBox.setBounds (bar.removeFromLeft (86)); bar.removeFromLeft (6);
    bZoomOut.setBounds (bar.removeFromLeft (34)); bar.removeFromLeft (4); bZoomIn.setBounds (bar.removeFromLeft (34));
    bHelp.setBounds (bar.removeFromRight (30)); bar.removeFromRight (5);
    bProject.setBounds (bar.removeFromRight (72)); bar.removeFromRight (5);
    bView.setBounds (bar.removeFromRight (56)); bar.removeFromRight (5);
    bRedo.setBounds (bar.removeFromRight (52)); bar.removeFromRight (4); bUndo.setBounds (bar.removeFromRight (52)); bar.removeFromRight (8);
    bAdd.setBounds (bar.removeFromRight (96)); bar.removeFromRight (10);

    keyboard.setVisible (keyboardShown);
    if (keyboardShown) keyboard.setBounds (r.removeFromBottom (64));
    dock.setBounds (r);
    // the keyboard status sits over the right of the bottom tabs, clear of the section menu button
    const auto tb = dock.bottomTabBar();
    midiLabel.setVisible (! tb.isEmpty() && tb.getWidth() > 900);
    if (! tb.isEmpty()) midiLabel.setBounds (dock.getX() + tb.getRight() - 520, dock.getY() + tb.getY() + 4, 470, 26);
    midiLabel.toFront (false);
    const auto ab = arrange.isVisible() ? arrange.getBounds() + dock.getPosition() : dock.getBounds();
    toast.setBounds (juce::Rectangle<int> (juce::jmin (720, getWidth() - 40), 38).withCentre ({ ab.getCentreX(), ab.getBottom() - 34 }));
    toast.toFront (false);
}

void MainComponent::mouseDown (const juce::MouseEvent&) { grabKeyboardFocus(); }
void MainComponent::mouseDrag (const juce::MouseEvent&) {}

void MainComponent::toggleFullScreen()
{
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setFullScreen (! w->isFullScreen());
}

void MainComponent::saveLayout()
{
    auto v = dock.getState();
    v.setProperty ("keyboard", keyboardShown, nullptr);
    if (auto xml = v.createXml()) xml->writeTo (appDataDir().getChildFile ("Layout.xml"));
}

void MainComponent::loadLayout()
{
    if (auto xml = juce::parseXML (appDataDir().getChildFile ("Layout.xml")))
    {
        auto v = juce::ValueTree::fromXml (*xml);
        keyboardShown = v.getProperty ("keyboard", true);
        dock.restoreState (v);
    }
}

void MainComponent::showViewMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Sections");
    for (auto& name : dock.panelNames())
    {
        juce::PopupMenu sm;
        const auto place = dock.placeOf (name);
        const char* labels[] = { "At the bottom", "On the left", "On the right", "In its own window" };
        for (int i = 0; i < 4; ++i) sm.addItem (labels[i], true, (int) place == i, [this, name, i] { if (dock.getFill() >= 0) dock.setFill (-1); dock.movePanel (name, (DockPlace) i); });
        if (auto* w = dock.windowFor (name))
        {
            sm.addSeparator();
            sm.addItem ("Full screen", true, w->isFullScreen(), [w] { w->setFullScreen (! w->isFullScreen()); });
        }
        m.addSubMenu (name, sm);
    }
    m.addSeparator();
    m.addItem ("Timeline fills the window", true, dock.getFill() == 3, [this] { dock.setFill (dock.getFill() == 3 ? -1 : 3); });
    if (dock.getFill() >= 0) m.addItem ("Back to the normal layout (Esc)", [this] { dock.setFill (-1); });
    m.addItem ("Show the on-screen keyboard", true, keyboardShown, [this] { keyboardShown = ! keyboardShown; resized(); saveLayout(); });
    auto* w = findParentComponentOfClass<juce::DocumentWindow>();
    m.addItem ("Full screen (Cmd+Ctrl+F)", true, w != nullptr && w->isFullScreen(), [this] { toggleFullScreen(); });
    m.addSeparator();
    m.addItem ("Reset layout", [this] { keyboardShown = true; dock.resetLayout(); resized(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bView));
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (std::abs (tempo.getValue() - session.getBpm()) > 0.01) tempo.setValue (session.getBpm(), juce::dontSendNotification);
    soundWrap.resized(); effectsWrap.resized(); mixerWrap.resized();
    if (auto* w = getTopLevelComponent()) if (auto* dw = dynamic_cast<juce::DocumentWindow*> (w)) dw->setName ("Reel Studio: " + session.getProjectName());
}

void MainComponent::updateTransportButtons()
{
    auto& tr = session.transport();
    bPlay.setButtonText (tr.isPlaying() ? "Pause" : "Play");
    bPlay.setToggleState (tr.isPlaying(), juce::dontSendNotification);
    bRec.setColour (juce::TextButton::buttonColourId, tr.isRecording() ? Col::rec : Col::panel2);
    bLoop.setColour (juce::TextButton::buttonColourId, tr.looping ? Col::acc.withAlpha (0.35f) : Col::panel2);
    bClick.setColour (juce::TextButton::buttonColourId, session.edit->clickTrackEnabled.get() ? Col::acc.withAlpha (0.35f) : Col::panel2);
    bUndo.setEnabled (session.undoManager().canUndo());
    bRedo.setEnabled (session.undoManager().canRedo());
}

void MainComponent::timerCallback()
{
    auto& tr = session.transport();
    const auto pos = tr.getPosition();
    const double beats = session.toBeats (pos).inBeats();
    counter.setText (juce::String ((int) (beats / 4) + 1) + "." + juce::String ((int) std::fmod (beats, 4.0) + 1), juce::dontSendNotification);
    timeLabel.setText (fmtSeconds (pos.inSeconds()), juce::dontSendNotification);
    auto midi = session.midiInputNames();
    midiLabel.setText (midi.isEmpty() ? juce::String ("No MIDI keyboard connected. Computer keys A to K play notes.") : "MIDI: " + midi.joinIntoString (", "), juce::dontSendNotification);
    updateTransportButtons();
    if (toast.isVisible() && juce::Time::getMillisecondCounter() > toastUntil) toast.setVisible (false);
}

void MainComponent::showMessage (const juce::String& m)
{
    toast.setText (m, juce::dontSendNotification);
    toast.setVisible (true); toast.toFront (false);
    toastUntil = juce::Time::getMillisecondCounter() + 4500;
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();
    if (mods.isCommandDown() && mods.isCtrlDown() && (code == 'F' || code == 'f')) { toggleFullScreen(); return true; }
    if (code == juce::KeyPress::escapeKey && dock.getFill() >= 0) { dock.setFill (-1); return true; }
    if (mods.isCommandDown())
    {
        if (code == 'Z' || code == 'z') { if (mods.isShiftDown()) session.undoManager().redo(); else session.undoManager().undo(); return true; }
        if (code == 'D' || code == 'd') { if (auto* c = session.getSelectedClip()) session.duplicateClip (*c); return true; }
        if (code == 'S' || code == 's') { showProjectMenu(); return true; }
        return false;
    }
    if (code == juce::KeyPress::spaceKey) { session.togglePlay(); return true; }
    if (code == juce::KeyPress::homeKey || code == juce::KeyPress::returnKey) { bStart.onClick(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (editorPanel.isShowing() && editorPanel.deleteSelectedNotes()) return true;
        if (linesPanel.isShowing() && linesPanel.deleteSelected()) return true;
        if (auto* c = session.getSelectedClip()) session.deleteClip (*c);
        return true;
    }
    const auto ch = (char) juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (ch == 'r') { session.toggleRecord(); return true; }
    if (ch == 'b') { if (! session.splitAtPlayhead()) showMessage ("Put the playhead over a clip on the selected track, then press B to split."); return true; }
    if (ch == 'z') { octave = juce::jmax (0, octave - 1); showMessage ("Computer keys: A plays " + noteName ((octave + 1) * 12)); return true; }
    if (ch == 'x') { octave = juce::jmin (8, octave + 1); showMessage ("Computer keys: A plays " + noteName ((octave + 1) * 12)); return true; }
    if (juce::String (pianoKeys).containsChar (ch)) return true;   // handled in keyStateChanged
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    bool used = false;
    const juce::String keys (pianoKeys);
    for (int i = 0; i < keys.length(); ++i)
    {
        const int code = keys[i];
        const bool down = juce::KeyPress::isKeyCurrentlyDown (code) && ! juce::ModifierKeys::currentModifiers.isCommandDown();
        auto it = heldComputerKeys.find (code);
        if (down && it == heldComputerKeys.end())
        {
            const int note = (octave + 1) * 12 + i;
            heldComputerKeys[code] = note; keyboardState.noteOn (1, note, 0.8f); used = true;
        }
        else if (! down && it != heldComputerKeys.end())
        {
            keyboardState.noteOff (1, it->second, 0.0f); heldComputerKeys.erase (it); used = true;
        }
    }
    return used;
}

//==============================================================================
void MainComponent::importFiles (const juce::StringArray& files, te::AudioTrack* target, te::TimePosition at)
{
    bool first = true;
    for (auto& path : files)
    {
        juce::File f (path);
        const auto ext = f.getFileExtension().toLowerCase();
        session.beginAction();
        if (ext == ".mid" || ext == ".midi")
        {
            const int n = session.importMidi (f);
            if (n > 0) showMessage ("Imported " + juce::String (n) + (n == 1 ? " part" : " parts") + ". Change any part's sound in the Sound tab.");
        }
        else if (ext == ".tracktionedit") { if (session.openProject (f)) showMessage ("Opened " + f.getFileNameWithoutExtension() + "."); }
        else if (session.importAudio (f, first ? target : nullptr, first && target != nullptr ? std::optional<te::TimePosition> (at) : std::nullopt) != nullptr)
            showMessage ("Imported " + f.getFileName() + ". The original file is never changed.");
        first = false;
    }
}

void MainComponent::browseForAudio (te::AudioTrack* target, std::optional<te::TimePosition> at)
{
    auto id = target ? target->itemID : te::EditItemID();
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File(), session.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, id, at] (const juce::FileChooser& fc)
    {
        bool first = true;
        for (auto& f : fc.getResults())
        {
            session.beginAction();
            auto* t = first ? te::findAudioTrackForID (*session.edit, id) : nullptr;
            session.importAudio (f, t, first ? at : std::nullopt);
            first = false;
        }
    });
}

void MainComponent::showAddMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Audio from a file...");
    m.addItem (2, "Synth (built-in sounds)");
    m.addItem (3, "Sampler (play any sound on keys)");
    m.addItem (4, "Drum machine with step sequencer");
    m.addItem (6, "Engine (Layers, Orbits, Threads, Chords, Disperse, Lattice)");
    auto mine = session.myInstrumentNames();
    if (! mine.isEmpty())
    {
        juce::PopupMenu im;
        for (int i = 0; i < mine.size(); ++i) im.addItem (100 + i, mine[i]);
        m.addSubMenu ("My instrument", im);
    }
    m.addSeparator();
    m.addItem (5, "Import a MIDI file...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bAdd), [this] (int r)
    {
        if (r == 1) { browseForAudio (nullptr, std::nullopt); return; }
        if (r >= 100)
        {
            auto mine = session.myInstrumentNames();
            if (r - 100 < mine.size()) { session.beginAction(); if (auto* t = session.addInstrumentTrack (mine[r - 100])) { session.select (t, session.addMidiClip (*t, session.snapToBar (session.transport().getPosition()), 2)); dock.showPanel ("Sound"); } }
            return;
        }
        if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import a MIDI file", juce::File(), "*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            { if (fc.getResult().existsAsFile()) importFiles ({ fc.getResult().getFullPathName() }, nullptr, {}); });
            return;
        }
        if (r == 6)
        {
            session.beginAction();
            if (auto* t = session.addEngineTrack (0))
            {
                bool any = false; for (auto* x : session.tracks()) if (! x->getClips().isEmpty()) any = true;
                session.select (t, session.addMidiClip (*t, any ? session.snapToBar (session.transport().getPosition()) : te::TimePosition(), 2));
                dock.showPanel ("Sound");
            }
            return;
        }
        if (r < 2) return;
        session.beginAction();
        const Kind k = r == 2 ? Kind::synth : r == 3 ? Kind::sampler : Kind::drums;
        auto* t = session.addTrack (k);
        bool hasContent = false; for (auto* x : session.tracks()) if (! x->getClips().isEmpty()) hasContent = true;
        const auto at = hasContent ? session.snapToBar (session.transport().getPosition()) : te::TimePosition();
        te::Clip* c = nullptr;
        if (k == Kind::drums)
        {
            if (auto* sc = session.addStepClip (*t, at, 4))
            {
                auto p = sc->getPattern (0);
                for (int i = 0; i < 16; ++i) { if (i % 8 == 0 || i == 10) p.setNote (0, i, true); if (i % 8 == 4) p.setNote (1, i, true); if (i % 2 == 0) p.setNote (3, i, true); }
                c = sc;
            }
        }
        else if (k == Kind::synth) c = session.addMidiClip (*t, at, 2);
        session.select (t, c);
        dock.showPanel (k == Kind::sampler ? "Sound" : "Editor");
    });
}

void MainComponent::showProjectMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (session.getProjectName());
    m.addItem (1, "Save project");
    m.addItem (2, "Save project as...");
    m.addItem (3, "Open project...");
    m.addItem (4, "Export mixdown (WAV)...");
    m.addSeparator();
    m.addItem (5, "Open the demo song");
    m.addItem (6, "Start a new empty project");
    m.addSeparator();
    m.addItem (7, "Audio and MIDI settings...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (bProject), [this] (int r)
    {
        auto saveAs = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Save project", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("My song.tracktionedit"), "*.tracktionedit");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
            {
                auto f = fc.getResult(); if (f == juce::File()) return;
                showMessage (session.saveProject (f) ? "Saved " + f.getFileName() + ". Audio stays in its original files." : juce::String ("Could not save there."));
            });
        };
        switch (r)
        {
            case 1: if (session.projectFile == juce::File()) saveAs(); else showMessage (session.saveProject (session.projectFile) ? "Saved." : "Could not save."); break;
            case 2: saveAs(); break;
            case 3:
                chooser = std::make_unique<juce::FileChooser> ("Open project", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.tracktionedit");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                { if (fc.getResult().existsAsFile()) showMessage (session.openProject (fc.getResult()) ? "Opened." : "That file could not be opened."); });
                break;
            case 4:
                chooser = std::make_unique<juce::FileChooser> ("Export mixdown", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (session.getProjectName() + ".wav"), "*.wav");
                chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                {
                    auto f = fc.getResult().withFileExtension ("wav"); if (fc.getResult() == juce::File()) return;
                    showMessage ("Mixing down...");
                    showMessage (session.exportMix (f) ? "Exported " + f.getFileName() + "." : juce::String ("Export failed."));
                });
                break;
            case 5: session.loadDemo(); showMessage ("Demo loaded. Press Space to play."); break;
            case 6: session.newProject(); showMessage ("New empty project."); break;
            case 7:
            {
                juce::DialogWindow::LaunchOptions o;
                o.dialogTitle = "Audio and MIDI settings";
                o.dialogBackgroundColour = Col::panel;
                auto* sel = new juce::AudioDeviceSelectorComponent (session.engine.getDeviceManager().deviceManager, 0, 2, 1, 2, true, false, true, false);
                sel->setSize (520, 520);
                o.content.setOwned (sel);
                o.launchAsync();
                break;
            }
            default: break;
        }
    });
}

void MainComponent::showHelp()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "How to use Reel Studio",
        "Your files are never changed. Every edit is an instruction in the project, and Cmd+Z undoes it.\n\n"
        "Add tracks with Add track, or drop audio and MIDI files on the timeline. Double-click an empty spot on a track to make a clip.\n\n"
        "Speed and pitch: select an audio clip, then use the Editor tab. They stay independent unless you untick Keep pitch.\n\n"
        "Cut and reverse: put the playhead on a clip and press B to split. Drag clip edges to trim. Right-click a clip to reverse it.\n\n"
        "Effects at certain points: turn an effect on in the Effects tab, press Automate, then click points in the lane under the track.\n\n"
        "Sound to instrument: right-click an audio clip and choose Make a playable instrument. Reel finds its note so it plays in tune.\n\n"
        "Drums: add a drum machine, click steps in the Editor tab, shape the drums in the Sound tab.\n\n"
        "Record: select an audio track and press Record (or R) to record your microphone, or select a synth, sampler or drum track to record your MIDI keyboard. "
        "Choose the input and count-in in the Sound tab of an audio track.\n\n"
        "Audio to MIDI: right-click an audio clip, choose Turn into MIDI notes, then Melody, Chords or Drums.\n\n"
        "Layout: each section (Editor, Sound, Effects, Mixer, Lines) can sit at the bottom, on either side or in its own window: use the ... button "
        "on its tabs or the View button. Drag the edges between areas to resize them. Fill the window gives one area (or the timeline) the whole window; "
        "Esc brings the others back. Cmd+Ctrl+F toggles full screen. Reel remembers your layout.\n\n"
        "Engine: Add track, Engine gives an instrument with four sound layers and six ways to play them: Layers, Orbits, Threads, Chords, "
        "Disperse and Lattice. Choose each layer's sound and the engine in the Sound tab.\n\n"
        "Arpeggiator: on a synth or sampler track, open the Sound tab and press Add arpeggiator. Hold a chord and it plays the notes in turn, "
        "in time with the song (or on its own clock when stopped). Choose the pattern, rate, octaves, gate, swing, velocity and hold.\n\n"
        "Instruments: right-click an audio clip and choose Save this sound as an instrument, or press Save as instrument on a sampler track. "
        "Use it on any synth or sampler track from the instrument list in the Sound tab, or from Add track, My instrument.\n\n"
        "Lines on recordings: select an audio track and open the Lines tab to turn the recording into lines, or to play lines with its sound.\n\n"
        "Lines: open the Lines tab on a synth, sampler or drum track and add Lines. Draw notes as lines that glide or step between pitches, "
        "each with its own pressure and slide, played as MPE and looped with the song.\n\n"
        "Keys: Space play, Home back to start, B split, Cmd+D duplicate, Delete remove, Cmd+Z undo, A to K play notes, Z and X change octave. Pinch or Cmd-scroll zooms.");
}
}
