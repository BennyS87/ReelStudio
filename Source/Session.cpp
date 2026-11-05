#include "Session.h"

namespace Reel
{
//==============================================================================
// Built-in synth sounds (all fully editable in the Sound panel)
namespace
{
    using W = te::Oscillator::Waves;
    float hz (float f) { return 69.0f + 12.0f * std::log2 (f / 440.0f); }

    void clearSynth (te::FourOscPlugin& p)
    {
        for (int i = 0; i < p.oscParams.size(); ++i)
        {
            auto& o = *p.oscParams[i];
            o.waveShapeValue = i == 0 ? (int) W::sine : (int) W::none;
            o.tuneValue = 0; o.fineTuneValue = 0; o.levelValue = 0; o.voicesValue = 1; o.detuneValue = 0; o.spreadValue = 0; o.panValue = 0;
        }
        p.distortionOnValue = false; p.reverbOnValue = false; p.delayOnValue = false; p.chorusOnValue = false;
        p.filterTypeValue = 0; p.filterAmountValue = 0; p.filterResonanceValue = 10; p.filterFreqValue = hz (8000);
        p.voiceModeValue = 2; p.masterLevelValue = -6.0f; p.ampVelocityValue = 70.0f;
    }
    void osc (te::FourOscPlugin& p, int i, W w, float tune = 0, float levelDb = 0, int voices = 1, float detune = 0, float fine = 0)
    {
        auto& o = *p.oscParams[i];
        o.waveShapeValue = (int) w; o.tuneValue = tune; o.levelValue = levelDb; o.voicesValue = voices;
        o.detuneValue = detune; o.fineTuneValue = fine; o.spreadValue = voices > 1 ? 60.0f : 0.0f;
    }
    void amp (te::FourOscPlugin& p, float a, float d, float s, float r) { p.ampAttackValue = a; p.ampDecayValue = d; p.ampSustainValue = s; p.ampReleaseValue = r; }
    void filt (te::FourOscPlugin& p, int type, float freqHz, float res, float amount, float a = 0.01f, float d = 0.3f, float s = 30, float r = 0.3f)
    {
        p.filterTypeValue = type; p.filterFreqValue = hz (freqHz); p.filterResonanceValue = res; p.filterAmountValue = amount;
        p.filterAttackValue = a; p.filterDecayValue = d; p.filterSustainValue = s; p.filterReleaseValue = r;
    }

    const std::vector<std::pair<juce::String, std::function<void (te::FourOscPlugin&)>>>& presets()
    {
        static const std::vector<std::pair<juce::String, std::function<void (te::FourOscPlugin&)>>> list {
            { "Soft keys",      [] (auto& p) { osc (p, 0, W::triangle); osc (p, 1, W::sine, 12, -10); filt (p, 1, 3000, 10, 0.3f, 0.004f, 0.7f, 20, 0.4f); amp (p, 0.004f, 1.3f, 12, 0.45f); } },
            { "Electric piano", [] (auto& p) { osc (p, 0, W::sine); osc (p, 1, W::sine, 12, -8, 1, 0, 4); osc (p, 2, W::triangle, 24, -18); filt (p, 1, 5000, 5, 0.1f); amp (p, 0.003f, 1.6f, 18, 0.5f); } },
            { "Round bass",     [] (auto& p) { osc (p, 0, W::saw); osc (p, 1, W::square, -12, -4); filt (p, 1, 420, 25, 0.3f, 0.003f, 0.22f, 20, 0.12f); amp (p, 0.003f, 0.4f, 75, 0.12f); } },
            { "Acid bass",      [] (auto& p) { osc (p, 0, W::saw); filt (p, 1, 300, 78, 0.6f, 0.003f, 0.18f, 0, 0.1f); amp (p, 0.002f, 0.3f, 60, 0.08f); } },
            { "Bright lead",    [] (auto& p) { osc (p, 0, W::saw, 0, 0, 3, 0.15f); osc (p, 1, W::saw, 12, -9); filt (p, 1, 6000, 20, 0.2f); amp (p, 0.01f, 0.2f, 80, 0.25f); } },
            { "Warm pad",       [] (auto& p) { osc (p, 0, W::saw, 0, 0, 4, 0.2f); osc (p, 1, W::saw, 12, -6, 2, 0.1f); filt (p, 1, 1500, 10, 0.15f, 0.8f, 1.0f, 60, 1.5f); amp (p, 0.9f, 1.0f, 80, 1.8f); } },
            { "Strings",        [] (auto& p) { osc (p, 0, W::saw, 0, 0, 3, 0.12f); osc (p, 1, W::saw, 12, -8); filt (p, 1, 2800, 5, 0.1f); amp (p, 0.35f, 0.5f, 85, 0.9f); } },
            { "Pluck",          [] (auto& p) { osc (p, 0, W::square); osc (p, 1, W::saw, 12, -8); filt (p, 1, 700, 30, 0.55f, 0.002f, 0.15f, 0, 0.2f); amp (p, 0.002f, 0.35f, 0, 0.25f); } },
            { "Organ",          [] (auto& p) { osc (p, 0, W::sine); osc (p, 1, W::sine, 12, -4); osc (p, 2, W::sine, 19, -10); osc (p, 3, W::sine, 24, -12); amp (p, 0.005f, 0.1f, 100, 0.08f); } },
            { "Bell",           [] (auto& p) { osc (p, 0, W::sine); osc (p, 1, W::sine, 24, -6, 1, 0, 7); osc (p, 2, W::sine, 31, -14, 1, 0, -14); amp (p, 0.002f, 2.5f, 0, 1.5f); } },
            { "Brass",          [] (auto& p) { osc (p, 0, W::saw, 0, 0, 2, 0.06f); osc (p, 1, W::saw, -12, -6); filt (p, 1, 900, 15, 0.45f, 0.06f, 0.3f, 50, 0.2f); amp (p, 0.04f, 0.2f, 80, 0.2f); } },
            { "Sub sine",       [] (auto& p) { osc (p, 0, W::sine); osc (p, 1, W::triangle, 12, -14); amp (p, 0.004f, 0.3f, 90, 0.15f); } },
        };
        return list;
    }

    juce::File presetDir() { auto d = appDataDir().getChildFile ("Sounds"); d.createDirectory(); return d; }
    juce::File samplesDir() { auto d = appDataDir().getChildFile ("Samples"); d.createDirectory(); return d; }
}

juce::StringArray builtInPresetNames() { juce::StringArray s; for (auto& p : presets()) s.add (p.first); return s; }

juce::StringArray userPresetNames()
{
    juce::StringArray s;
    for (auto& f : presetDir().findChildFiles (juce::File::findFiles, false, "*.xml")) s.add (f.getFileNameWithoutExtension());
    s.sort (true);
    return s;
}

void applyPreset (te::FourOscPlugin& p, const juce::String& name)
{
    if (name.startsWith ("user:"))
    {
        if (auto xml = juce::parseXML (presetDir().getChildFile (name.substring (5) + ".xml")))
        {
            auto v = juce::ValueTree::fromXml (*xml);
            for (int i = 0; i < v.getNumProperties(); ++i)
                if (v.getPropertyName (i) != te::IDs::id && v.getPropertyName (i) != te::IDs::type)
                    p.state.setProperty (v.getPropertyName (i), v.getProperty (v.getPropertyName (i)), p.getUndoManager());
        }
    }
    else
    {
        clearSynth (p);
        for (auto& pr : presets()) if (pr.first == name) { pr.second (p); break; }
    }
    p.restorePluginStateFromValueTree (p.state);
    for (auto ap : p.getAutomatableParameters()) ap->updateFromAttachedValue();
}

bool saveUserPreset (te::FourOscPlugin& p, const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim());
    if (safe.isEmpty()) return false;
    auto v = p.state.createCopy();
    v.removeAllChildren (nullptr);
    if (auto xml = v.createXml()) return xml->writeTo (presetDir().getChildFile (safe + ".xml"));
    return false;
}

const juce::Array<EffectInfo>& effectSlots()
{
    static const juce::Array<EffectInfo> slots = []
    {
        juce::Array<EffectInfo> a;
        a.add (EffectInfo { EQPlugin::xmlTypeName,         "EQ",         "Eight bands of tone shaping with a live spectrum. Drag the points.", 1 });
        a.add (EffectInfo { CompressorPlugin::xmlTypeName, "Compressor", "Evens out levels and adds punch, with parallel mix and sidechain low cut.", 2 });
        a.add (EffectInfo { SaturationPlugin::xmlTypeName, "Saturation", "Warmth and harmonics, from gentle tape to hard clipping.", 3 });
        a.add (EffectInfo { DistortionPlugin::xmlTypeName, "Distortion", "Grit, from overdrive to fuzz, rectify and foldback.", 4 });
        a.add (EffectInfo { PhaserPlugin::xmlTypeName,     "Phaser / Flanger / Chorus", "Sweeping phase shifts, jet flanges or a widening chorus.", 5 });
        a.add (EffectInfo { EchoPlugin::xmlTypeName,       "Echo",       "Tempo-synced or free repeats, ping-pong, wobble, dirt and ducking.", 6 });
        a.add (EffectInfo { ReverbPlugin::xmlTypeName,     "Reverb",     "Rooms to huge halls, with pre-delay, movement, ducking and freeze.", 7 });
        a.add (EffectInfo { VocoderPlugin::xmlTypeName,    "Vocoder",    "Makes the track speak through a synth. Best on vocals or speech.", 8 });
        return a;
    }();
    return slots;
}

juce::String effectTitle (te::Plugin& p)
{
    for (auto& s : effectSlots()) if (p.getPluginType() == s.type) return s.title;
    return p.getName();
}

//==============================================================================
Session::Session (te::Engine& e) : engine (e)
{
    autosaveFile = appDataDir().getChildFile ("Autosave.tracktionedit");
    if (keyboardDevice() == nullptr)
        engine.getDeviceManager().createVirtualMidiDevice ("Reel keyboard");
    prepareAudioInputs();
    newProject();
    startTimer (15000);
}

Session::~Session()
{
    stopTimer();
    saveAutosave();
    detachEdit();
}

void Session::detachEdit()
{
    if (edit != nullptr)
    {
        editWatchers.call ([] (EditWatcher& w) { w.editAboutToBeDeleted(); });   // UI lets go of plugins first
        edit->getTransport().stop (false, true);
        edit->state.removeListener (this);
        edit.reset();
    }
}

void Session::attachEdit()
{
    migrateSamplers();
    edit->state.addListener (this);
    edit->getTransport().ensureContextAllocated();
    selectedTrack = selectedClip = lastInstrumentTrack = {};
    if (auto* t = tracks().getFirst()) select (t);
    routeLiveInput();
    ++structureVersion; dirty = false;
    sendChangeMessage();
}

void Session::newProject()
{
    detachEdit();
    auto f = appDataDir().getChildFile ("Working.tracktionedit");
    f.deleteFile();
    edit = te::createEmptyEdit (engine, f);
    for (auto* t : te::getAudioTracks (*edit)) edit->deleteTrack (t);   // start with no tracks
    edit->getMasterVolumePlugin()->setVolumeDb (-3.0f);
    projectFile = juce::File();
    attachEdit();
}

bool Session::openProject (const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    auto loaded = te::loadEditFromFile (engine, f);
    if (loaded == nullptr) return false;
    detachEdit();
    edit = std::move (loaded);
    projectFile = f == autosaveFile ? juce::File() : f;
    attachEdit();
    return true;
}

bool Session::saveProject (const juce::File& f)
{
    auto target = f.withFileExtension ("tracktionedit");
    if (! te::EditFileOperations (*edit).writeToFile (target, false)) return false;
    projectFile = target;
    appDataDir().getChildFile ("LastProject.txt").replaceWithText (target.getFullPathName());
    return true;
}

void Session::saveAutosave()
{
    if (edit == nullptr) return;
    te::EditFileOperations (*edit).writeToFile (autosaveFile, false);
    appDataDir().getChildFile ("LastProject.txt").replaceWithText (projectFile.getFullPathName());
}

void Session::timerCallback() { if (dirty) { dirty = false; saveAutosave(); } }

juce::String Session::getProjectName() const { return projectFile == juce::File() ? juce::String ("Untitled song") : projectFile.getFileNameWithoutExtension(); }

//==============================================================================
te::AudioTrack* Session::addTrack (Kind k, const juce::String& name)
{
    auto existing = tracks();
    edit->ensureNumberOfAudioTracks (existing.size() + 1);
    auto* t = tracks().getLast();
    if (t == nullptr) return nullptr;
    t->state.setProperty (IDs::kind, kindToString (k), nullptr);

    juce::Array<juce::Colour> used; for (auto* x : existing) used.add (x->getColour());
    auto colour = trackColours[existing.size() % 8];
    for (auto c : trackColours) if (! used.contains (c)) { colour = c; break; }
    t->setColour (colour);

    int sameKind = 0; for (auto* x : existing) if (getKind (*x) == k) ++sameKind;
    t->setName (name.isNotEmpty() ? name : kindLabel (k) + (sameKind > 0 ? " " + juce::String (sameKind + 1) : juce::String()));

    if (k == Kind::synth)
    {
        if (auto p = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
        {
            t->pluginList.insertPlugin (p, 0, nullptr);
            setSynthPreset (*t, "Soft keys");
        }
    }
    else if (k == Kind::sampler)
    {
        if (auto p = edit->getPluginCache().createNewPlugin (ReelSampler::xmlTypeName, {}))
            t->pluginList.insertPlugin (p, 0, nullptr);
    }
    else if (k == Kind::drums)
        createDrumKit (*t);

    return t;
}

void Session::createDrumKit (te::AudioTrack& t)
{
    auto p = edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
    auto* sampler = dynamic_cast<te::SamplerPlugin*> (p.get());
    if (sampler == nullptr) return;
    t.pluginList.insertPlugin (p, 0, nullptr);
    auto files = ensureDrumKitFiles();
    for (int i = 0; i < numDrumVoices; ++i)
    {
        sampler->addSound (files[i].getFullPathName(), drumVoices[i].name, 0.0, 0.0, 0.0f);
        sampler->setSoundParams (i, drumVoices[i].note, drumVoices[i].note, drumVoices[i].note);
        sampler->setSoundOpenEnded (i, true);
    }
    applyKit (t);
}

te::AudioTrack* Session::getSelectedTrack() const { return te::findAudioTrackForID (*edit, selectedTrack); }
te::Clip* Session::getSelectedClip() const { return selectedClip.isValid() ? te::findClipForID (*edit, selectedClip) : nullptr; }

void Session::select (te::AudioTrack* t, te::Clip* c)
{
    const bool trackChanged = (t ? t->itemID : te::EditItemID()) != selectedTrack;
    selectedTrack = t ? t->itemID : te::EditItemID();
    selectedClip = c ? c->itemID : te::EditItemID();
    if (isInstrument (t)) lastInstrumentTrack = t->itemID;
    if (trackChanged) routeLiveInput();
    sendChangeMessage();
}

void Session::deleteTrack (te::AudioTrack& t)
{
    if (t.itemID == selectedTrack) { selectedTrack = {}; selectedClip = {}; }
    edit->deleteTrack (&t);
    routeLiveInput();
}

void Session::duplicateTrack (te::AudioTrack& t)
{
    auto copy = t.state.createCopy();
    std::function<void (juce::ValueTree)> renew = [&] (juce::ValueTree v)
    {
        if (v.hasProperty (te::IDs::id)) edit->createNewItemID().writeID (v, nullptr);
        for (auto child : v) renew (child);
    };
    renew (copy);
    copy.setProperty (te::IDs::name, t.getName() + " copy", nullptr);
    auto parent = t.state.getParent();
    parent.addChild (copy, parent.indexOf (t.state) + 1, &undoManager());
}

void Session::moveTrack (te::AudioTrack& t, int delta)
{
    auto parent = t.state.getParent();
    auto all = tracks(); const int i = all.indexOf (&t), j = i + delta;
    if (j < 0 || j >= all.size()) return;
    parent.moveChild (parent.indexOf (t.state), parent.indexOf (all[j]->state), &undoManager());
}

//==============================================================================
te::TimeDuration Session::barLength() const { return toTime (te::BeatPosition::fromBeats (4.0)) - toTime (te::BeatPosition()); }

te::TimePosition Session::snapToBar (te::TimePosition t) const
{
    return toTime (te::BeatPosition::fromBeats (std::floor (toBeats (t).inBeats() / 4.0) * 4.0));
}

te::AudioTrack* Session::trackForNewClip (Kind k)
{
    if (auto* t = getSelectedTrack()) if (getKind (*t) == k && t->getClips().isEmpty()) return t;
    return addTrack (k);
}

te::WaveAudioClip* Session::importAudio (const juce::File& f, te::AudioTrack* target, std::optional<te::TimePosition> at)
{
    te::AudioFile af (engine, f);
    if (! af.isValid()) { if (onMessage) onMessage (f.getFileName() + " could not be read. Try WAV, AIFF, MP3, M4A or FLAC."); return nullptr; }
    if (target == nullptr || getKind (*target) != Kind::audio) target = addTrack (Kind::audio, f.getFileNameWithoutExtension().substring (0, 40));
    const auto start = at.value_or (target->getClips().isEmpty() && tracks().size() <= 1 ? te::TimePosition() : snapToBar (transport().getPosition()));
    auto clip = target->insertWaveClip (f.getFileNameWithoutExtension(), f, { { start, start + te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
    if (clip == nullptr) return nullptr;
    clip->getSourceFileReference().setToDirectFileReference (f, false);   // absolute path, file stays where it is
    clip->setAutoTempo (false);
    clip->setAutoPitch (false);
    clip->setFadeIn (te::TimeDuration::fromSeconds (0.005));
    clip->setFadeOut (te::TimeDuration::fromSeconds (0.01));
    select (target, clip.get());
    return clip.get();
}

int Session::importMidi (const juce::File& f)
{
    juce::MidiFile mf;
    { juce::FileInputStream in (f); if (! in.openedOk() || ! mf.readFrom (in)) { if (onMessage) onMessage ("That file is not a standard MIDI file."); return 0; } }
    const int ppq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 480;
    if (mf.getTimeFormat() <= 0) mf.convertTimestampTicksToSeconds();

    bool hasContent = false; for (auto* t : tracks()) if (! t->getClips().isEmpty()) hasContent = true;
    juce::MidiMessageSequence tempos; mf.findAllTempoEvents (tempos);
    if (! hasContent && tempos.getNumEvents() > 0)
        setBpm (juce::jlimit (30.0, 300.0, std::round (60.0 / tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote())));
    const auto start = hasContent ? snapToBar (transport().getPosition()) : te::TimePosition();
    const double startBeat = toBeats (start).inBeats();
    int made = 0; te::AudioTrack* first = nullptr; te::Clip* firstClip = nullptr;

    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (ti));
        seq.updateMatchedPairs();
        juce::String trackName;
        for (auto* ev : seq) if (ev->message.isTrackNameEvent()) { trackName = ev->message.getTextFromTextMetaEvent().trim(); break; }
        for (int ch = 1; ch <= 16; ++ch)
        {
            struct N { int p; double t, d; int v; };
            std::vector<N> notes; int program = -1;
            for (auto* ev : seq)
            {
                auto& m = ev->message;
                if (m.getChannel() != ch) continue;
                if (m.isProgramChange() && program < 0) program = m.getProgramChangeNumber();
                if (m.isNoteOn())
                {
                    const double t0 = m.getTimeStamp() / ppq, t1 = ev->noteOffObject ? ev->noteOffObject->message.getTimeStamp() / ppq : t0 + 0.25;
                    notes.push_back ({ m.getNoteNumber(), t0, juce::jmax (1.0 / 64.0, t1 - t0), m.getVelocity() });
                }
            }
            if (notes.empty()) continue;
            const bool drums = ch == 10;
            static const char* families[] = { "Soft keys", "Bell", "Organ", "Pluck", "Round bass", "Strings", "Strings", "Brass", "Brass", "Bright lead", "Bright lead", "Warm pad", "Warm pad", "Pluck", "Bell", "Pluck" };
            const juce::String preset = program >= 0 ? families[juce::jlimit (0, 15, program / 8)] : "Soft keys";
            auto* t = addTrack (drums ? Kind::drums : Kind::synth, (trackName.isNotEmpty() ? trackName : (drums ? juce::String ("Drums") : preset)).substring (0, 40));
            if (! drums) setSynthPreset (*t, preset);
            double end = 0; for (auto& n : notes) end = juce::jmax (end, n.t + n.d);
            const int bars = juce::jmax (1, (int) std::ceil (end / 4.0 - 1e-6));
            if (auto* c = addMidiClip (*t, start, bars))
            {
                c->setName (trackName.isNotEmpty() ? trackName : f.getFileNameWithoutExtension());
                auto& list = c->getSequence();
                for (auto& n : notes) list.addNote (n.p, te::BeatPosition::fromBeats (n.t), te::BeatDuration::fromBeats (n.d), n.v, 0, nullptr);
                if (first == nullptr) { first = t; firstClip = c; }
            }
            ++made;
        }
    }
    juce::ignoreUnused (startBeat);
    if (first != nullptr) select (first, firstClip);
    return made;
}

te::MidiClip* Session::addMidiClip (te::AudioTrack& t, te::TimePosition start, int bars)
{
    const auto end = toTime (toBeats (start) + te::BeatDuration::fromBeats (4.0 * bars));
    auto c = t.insertMIDIClip ({ start, end }, nullptr);
    if (c == nullptr) return nullptr;
    c->setName ("Notes");
    return c.get();
}

te::StepClip* Session::addStepClip (te::AudioTrack& t, te::TimePosition start, int bars)
{
    const auto end = toTime (toBeats (start) + te::BeatDuration::fromBeats (4.0));
    auto* sc = dynamic_cast<te::StepClip*> (t.insertNewClip (te::TrackItem::Type::step, "Beat", { start, end }, nullptr));
    if (sc == nullptr) return nullptr;
    while (sc->getChannels().size() < numDrumVoices) sc->insertNewChannel (-1);
    for (int i = 0; i < numDrumVoices; ++i)
    {
        auto* ch = sc->getChannels()[i];
        ch->noteNumber = drumVoices[i].note;
        ch->name = drumVoices[i].name;
    }
    sc->createDefaultPatternIfEmpty();
    setStepClipBars (*sc, bars);
    return sc;
}

void Session::setStepClipBars (te::StepClip& sc, int bars)
{
    bars = juce::jlimit (1, 64, bars);
    const int pattern = 0;
    sc.removeAllVariations();
    const int stepsPerPattern = juce::jmax (1, sc.getPattern (pattern).getNumNotes());
    const int barsPerPattern = juce::jmax (1, stepsPerPattern / 16);
    for (int i = 0; i < (bars + barsPerPattern - 1) / barsPerPattern; ++i) sc.insertVariation (pattern, i);
    sc.resizeClipForPatternInstances();
}

bool Session::splitAtPlayhead()
{
    const auto pos = transport().getPosition();
    te::Clip* clip = getSelectedClip();
    if (clip == nullptr || ! clip->getEditTimeRange().contains (pos))
    {
        clip = nullptr;
        if (auto* t = getSelectedTrack())
            for (auto* c : t->getClips()) if (c->getEditTimeRange().contains (pos) && c->getPosition().getStart() < pos) { clip = c; break; }
    }
    if (clip == nullptr) return false;
    auto* ct = clip->getClipTrack(); if (ct == nullptr) return false;
    beginAction();
    if (auto* right = ct->splitClip (*clip, pos)) { select (getSelectedTrack(), right); return true; }
    return false;
}

te::Clip* Session::duplicateClip (te::Clip& c)
{
    auto* ct = c.getClipTrack(); if (ct == nullptr) return nullptr;
    beginAction();
    auto copy = te::duplicateClip (c);
    if (copy == nullptr) return nullptr;
    copy->setStart (c.getPosition().getEnd(), false, true);
    select (getSelectedTrack(), copy.get());
    return copy.get();
}

void Session::deleteClip (te::Clip& c)
{
    beginAction();
    if (c.itemID == selectedClip) selectedClip = {};
    c.removeFromParent();
}

void Session::setReversed (te::WaveAudioClip& c, bool r) { beginAction(); c.setIsReversed (r); }

void Session::setSpeedPitch (te::WaveAudioClip& c, double speed, float semitones, bool keepPitch)
{
    speed = juce::jlimit (0.25, 4.0, speed);
    const auto oldRatio = c.getSpeedRatio();
    const auto oldLen = c.getPosition().getLength();
    c.setAutoTempo (false);
    c.setAutoPitch (false);
    c.setTimeStretchMode (keepPitch ? te::TimeStretcher::soundtouchBetter : te::TimeStretcher::disabled);
    c.setSpeedRatio (speed);
    c.setPitchChange (keepPitch ? semitones : 0.0f);
    c.setLength (oldLen * (oldRatio / speed), true);
}

void Session::loopClip (te::Clip& c)
{
    transport().setLoopRange (c.getEditTimeRange());
    transport().looping = true;
    seek (c.getPosition().getStart());
}

te::AudioTrack* Session::makeSamplerFromClip (te::WaveAudioClip& c)
{
    auto* src = dynamic_cast<te::AudioTrack*> (c.getTrack());
    if (src == nullptr) return nullptr;
    auto file = renderClip (c, samplesDir().getNonexistentChildFile (juce::File::createLegalFileName (c.getName().isEmpty() ? "Sound" : c.getName()), ".wav", false));
    if (file == juce::File()) { if (onMessage) onMessage ("Could not prepare that sound."); return nullptr; }
    beginAction();
    auto* t = addTrack (Kind::sampler, (c.getName().isEmpty() ? juce::String ("Sound") : c.getName()).substring (0, 30) + " keys");
    loadSamplerSound (*t, file, true);
    moveTrack (*t, tracks().indexOf (src) + 1 - tracks().indexOf (t));
    select (t);
    return t;
}

//==============================================================================
te::FourOscPlugin* Session::getSynth (te::AudioTrack& t) const { return t.pluginList.findFirstPluginOfType<te::FourOscPlugin>(); }
te::SamplerPlugin* Session::getSampler (te::AudioTrack& t) const { return t.pluginList.findFirstPluginOfType<te::SamplerPlugin>(); }
ReelSampler* Session::getSampled (te::AudioTrack& t) const { return t.pluginList.findFirstPluginOfType<ReelSampler>(); }

te::Plugin* Session::instrumentOf (te::AudioTrack& t) const
{
    if (auto* s = getSynth (t)) return s;
    if (auto* s = getSampled (t)) return s;
    if (getKind (t) == Kind::sampler) return getSampler (t);
    return nullptr;
}

/** Projects from before Reel's own sampler used Tracktion's: swap those for the new one, keeping the sound and its settings. */
void Session::migrateSamplers()
{
    for (auto* t : te::getAudioTracks (*edit))
    {
        if (getKind (*t) != Kind::sampler || getSampled (*t) != nullptr) continue;
        auto* old = getSampler (*t);
        if (old == nullptr) continue;
        const int idx = t->pluginList.indexOf (old);
        auto p = edit->getPluginCache().createNewPlugin (ReelSampler::xmlTypeName, {});
        auto* ns = dynamic_cast<ReelSampler*> (p.get());
        if (ns == nullptr) continue;
        if (old->getNumSounds() > 0)
        {
            // the sound's path may be stored relative to the project, so try each way of finding it
            juce::File src;
            const auto stored = t->state.getProperty (IDs::samplePath).toString();
            if (juce::File::isAbsolutePath (stored)) src = juce::File (stored);
            if (! src.existsAsFile()) src = old->getSoundFile (0).getFile();
            if (! src.existsAsFile())
            {
                // straight from the saved project: right after loading, the old sampler hasn't built its sound list yet
                const auto media = old->state.getChildWithName (te::IDs::SOUND).getProperty (te::IDs::source).toString();
                src = juce::File::isAbsolutePath (media) ? juce::File (media)
                                                         : (edit->editFileRetriever ? edit->editFileRetriever().getParentDirectory().getChildFile (media) : juce::File());
            }
            ns->loadFile (src, nullptr);
            ns->root = old->getKeyNote (0);
            ns->level->setParameter (old->getSoundGainDb (0), juce::dontSendNotification);
            ns->pan->setParameter (old->getSoundPan (0), juce::dontSendNotification);
            ns->oneShot = old->isSoundOpenEnded (0) ? 1 : 0;
        }
        old->deleteFromParent();
        t->pluginList.insertPlugin (p, juce::jmax (0, idx), nullptr);
    }
}

juce::File Session::instrumentsDir() { auto d = appDataDir().getChildFile ("Instruments"); d.createDirectory(); return d; }

juce::StringArray Session::myInstrumentNames()
{
    juce::StringArray names;
    for (auto& d : instrumentsDir().findChildFiles (juce::File::findDirectories, false))
        if (d.getChildFile ("instrument.xml").existsAsFile()) names.add (d.getFileName());
    names.sortNatural();
    return names;
}

bool Session::saveInstrument (te::AudioTrack& t, const juce::String& rawName)
{
    auto* s = getSampled (t);
    if (s == nullptr || ! s->hasSound()) { if (onMessage) onMessage ("This track has no sound to save yet."); return false; }
    auto name = juce::File::createLegalFileName (rawName.trim()); if (name.isEmpty()) name = "My instrument";
    auto dir = instrumentsDir().getChildFile (name);
    dir.createDirectory();
    auto src = s->getFile(), dst = dir.getChildFile ("sound" + src.getFileExtension());
    if (src != dst) { dst.deleteFile(); if (! src.copyFileTo (dst)) { if (onMessage) onMessage ("Could not copy the sound into your instruments."); return false; } }
    auto v = s->state.createCopy();
    v.setProperty ("file", dst.getFileName(), nullptr);
    v.setProperty ("soundName", name, nullptr);
    v.removeProperty ("mpe", nullptr);
    if (auto xml = v.createXml(); xml == nullptr || ! xml->writeTo (dir.getChildFile ("instrument.xml"))) return false;
    t.state.setProperty (IDs::sampleName, name, &undoManager());
    if (onMessage) onMessage ("Saved " + name + ". Choose it from the Instrument list on any synth or sampler track, or from Add track.");
    return true;
}

bool Session::saveClipAsInstrument (te::WaveAudioClip& c, const juce::String& name)
{
    auto file = renderClip (c, appDataDir().getChildFile ("Analysis").getChildFile ("instrument.wav"));
    if (file == juce::File()) return false;
    // build it on a temporary track so the note detection and settings match a normal sampler track
    auto* t = addTrack (Kind::sampler, "Instrument");
    const bool ok = loadSamplerSound (*t, file, true) && saveInstrument (*t, name);
    deleteTrack (*t);
    return ok;
}

bool Session::applyInstrument (te::AudioTrack& t, const juce::String& name)
{
    const auto k = getKind (t);
    if (k != Kind::synth && k != Kind::sampler) return false;
    auto dir = instrumentsDir().getChildFile (name);
    auto xml = juce::parseXML (dir.getChildFile ("instrument.xml"));
    if (xml == nullptr) { if (onMessage) onMessage ("That instrument could not be found."); return false; }
    auto v = juce::ValueTree::fromXml (*xml);
    const auto soundFile = dir.getChildFile (v.getProperty ("file").toString());
    if (! soundFile.existsAsFile()) { if (onMessage) onMessage ("The sound for " + name + " is missing."); return false; }
    int idx = getLines (t) != nullptr ? 1 : 0;
    if (auto* old = instrumentOf (t)) { idx = t.pluginList.indexOf (old); old->deleteFromParent(); }
    auto p = edit->getPluginCache().createNewPlugin (ReelSampler::xmlTypeName, {});
    auto* ns = dynamic_cast<ReelSampler*> (p.get());
    if (ns == nullptr) return false;
    t.pluginList.insertPlugin (p, juce::jmax (0, idx), nullptr);
    ns->restorePluginStateFromValueTree (v);
    ns->loadFile (soundFile, nullptr);
    ns->soundName = name;
    ns->setMpe (getLines (t) != nullptr);
    t.state.setProperty (IDs::kind, kindToString (Kind::sampler), &undoManager());
    t.state.setProperty (IDs::sampleName, name, &undoManager());
    t.state.setProperty (IDs::samplePath, soundFile.getFullPathName(), &undoManager());
    ++structureVersion; sendChangeMessage();
    return true;
}

bool Session::applySynthSound (te::AudioTrack& t, const juce::String& preset)
{
    if (getSynth (t) != nullptr) { setSynthPreset (t, preset); return true; }
    if (getKind (t) != Kind::sampler) return false;
    int idx = getLines (t) != nullptr ? 1 : 0;
    if (auto* old = instrumentOf (t)) { idx = t.pluginList.indexOf (old); old->deleteFromParent(); }
    auto p = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
    if (p == nullptr) return false;
    t.pluginList.insertPlugin (p, juce::jmax (0, idx), nullptr);
    t.state.setProperty (IDs::kind, kindToString (Kind::synth), &undoManager());
    setSynthPreset (t, preset);
    if (getLines (t) != nullptr) if (auto* s = getSynth (t)) s->state.setProperty (te::IDs::mpe, true, &undoManager());
    ++structureVersion; sendChangeMessage();
    return true;
}

te::AudioTrack* Session::addInstrumentTrack (const juce::String& name)
{
    auto* t = addTrack (Kind::sampler, name);
    if (! applyInstrument (*t, name)) { deleteTrack (*t); return nullptr; }
    select (t);
    return t;
}

bool Session::deleteInstrument (const juce::String& name)
{
    auto d = instrumentsDir().getChildFile (name);
    return d.isDirectory() && d.moveToTrash();
}

te::AudioTrack* Session::convertToLines (te::WaveAudioClip& c)
{
    auto file = renderClip (c, appDataDir().getChildFile ("Analysis").getChildFile ("clip.wav"));
    if (file == juce::File()) { if (onMessage) onMessage ("Could not read that clip."); return nullptr; }
    auto contour = transcribeContour (file);
    if (contour.empty()) { if (onMessage) onMessage ("No clear pitch was found. Lines works best on one voice or instrument at a time."); return nullptr; }
    beginAction();
    auto* src = dynamic_cast<te::AudioTrack*> (c.getTrack());
    const auto clipName = c.getName().isEmpty() ? juce::String ("Audio") : c.getName();
    auto* t = addTrack (Kind::synth, (clipName + " lines").substring (0, 40));
    std::vector<float> ps; for (auto& l : contour) for (auto& p : l.points) ps.push_back (p.pitch);
    std::sort (ps.begin(), ps.end());
    setSynthPreset (*t, ps[ps.size() / 2] < 48 ? "Round bass" : "Soft keys");
    auto* lp = addLines (*t);
    if (lp == nullptr) return t;
    const auto start = c.getPosition().getStart();
    const double b0 = toBeats (start).inBeats();
    const double lenBeats = toBeats (c.getPosition().getEnd()).inBeats() - b0;
    int choice = 6; for (int i = 0; i < 7; ++i) if (LinesPlugin::barsFor (i) * 4.0 >= lenBeats - 1e-6) { choice = i; break; }
    lp->barsChoice = choice;
    lp->playMode = 1;                 // play once, lined up with the recording
    lp->startBeat = b0;
    const double L = lp->lengthBeats();
    int made = 0;
    for (auto& cl : contour)
    {
        LineData d; d.velocity = cl.velocity / 127.0f;
        for (auto& p : cl.points)
        {
            const double tb = toBeats (start + te::TimeDuration::fromSeconds (p.time)).inBeats() - b0;
            if (tb >= L) break;
            d.pts.push_back ({ tb, p.pitch, p.level, 0.5f, true });
        }
        if (d.pts.size() < 2) continue;
        lp->addLine (d, &undoManager());
        ++made;
    }
    if (src != nullptr) moveTrack (*t, tracks().indexOf (src) + 1 - tracks().indexOf (t));
    select (t);
    if (onMessage) onMessage ("Turned " + clipName + " into " + juce::String (made) + (made == 1 ? " line" : " lines") + " that follow its slides and wobbles. Open the Lines tab to reshape them"
                              + (lenBeats > L ? juce::String (" (only the first 64 bars fit).") : juce::String (".")));
    return t;
}

void Session::setSynthPreset (te::AudioTrack& t, const juce::String& name)
{
    if (auto* s = getSynth (t)) { applyPreset (*s, name); t.state.setProperty (IDs::preset, name, &undoManager()); }
}

bool Session::loadSamplerSound (te::AudioTrack& t, const juce::File& f, bool findNote)
{
    auto* s = getSampled (t);
    if (s == nullptr || ! s->loadFile (f, &undoManager())) { if (onMessage) onMessage (f.getFileName() + " could not be read."); return false; }
    s->root = 60;
    s->oneShot = s->soundSeconds() < 1.2 ? 1 : 0;
    t.state.setProperty (IDs::sampleName, f.getFileNameWithoutExtension(), &undoManager());
    t.state.setProperty (IDs::samplePath, f.getFullPathName(), &undoManager());
    if (findNote)
    {
        if (auto r = findSamplerNote (t))
        {
            if (onMessage) onMessage ("This sound is close to " + noteName (r->note) + ". It now plays in tune on every key.");
        }
        else if (onMessage) onMessage ("No clear note found (normal for drums and noises), so it plays at its own pitch on C4.");
    }
    return true;
}

std::optional<PitchResult> Session::findSamplerNote (te::AudioTrack& t)
{
    auto* s = getSampled (t);
    if (s == nullptr || ! s->hasSound()) return {};
    auto file = s->getFile();
    auto r = detectPitch (file);
    if (r && r->confidence > 0.5) { setSamplerRoot (t, juce::jlimit (12, 108, r->note)); return r; }
    setSamplerRoot (t, 60);
    return {};
}

void Session::setSamplerRoot (te::AudioTrack& t, int note)
{
    if (auto* s = getSampled (t)) s->root = juce::jlimit (0, 127, note);
}

float Session::getKitValue (te::AudioTrack& t, const juce::Identifier& base, int v, float def) const
{
    return (float) t.state.getProperty (juce::Identifier (base.toString() + juce::String (v)), def);
}

void Session::setKitValue (te::AudioTrack& t, const juce::Identifier& base, int v, float value)
{
    t.state.setProperty (juce::Identifier (base.toString() + juce::String (v)), value, &undoManager());
    applyKit (t);
}

void Session::applyKit (te::AudioTrack& t)
{
    auto* s = getSampler (t); if (s == nullptr) return;
    for (int i = 0; i < juce::jmin (numDrumVoices, s->getNumSounds()); ++i)
    {
        const int note = drumVoices[i].note;
        const int tune = juce::roundToInt (getKitValue (t, IDs::kitTune, i, 0.0f));
        s->setSoundParams (i, note - tune, note, note);
        const float level = getKitValue (t, IDs::kitLevel, i, 0.8f);
        s->setSoundGains (i, level <= 0.001f ? -100.0f : juce::Decibels::gainToDecibels (level / 0.8f), 0.0f);
        const float len = getKitValue (t, IDs::kitLen, i, 1.0f);
        te::AudioFile af (engine, juce::File (s->getSoundMedia (i)));
        const double full = af.isValid() ? af.getLength() : 0.0;
        s->setSoundExcerpt (i, 0.0, len >= 0.999f ? full : juce::jmax (0.01, full * len));
    }
}

void Session::setDrumSound (te::AudioTrack& t, int v, const juce::File& f)
{
    if (auto* s = getSampler (t)) if (v < s->getNumSounds()) { beginAction(); s->setSoundMedia (v, f.getFullPathName()); applyKit (t); }
}

void Session::resetDrumSound (te::AudioTrack& t, int v)
{
    auto files = ensureDrumKitFiles();
    if (auto* s = getSampler (t)) if (v < s->getNumSounds()) { beginAction(); s->setSoundMedia (v, files[v].getFullPathName()); applyKit (t); }
}

void Session::auditionDrum (te::AudioTrack& t, int voice)
{
    select (&t, getSelectedClip());
    keyboardNote (drumVoices[voice].note, 0.85f, true);
    juce::Timer::callAfterDelay (150, [this, n = drumVoices[voice].note] { keyboardNote (n, 0.0f, false); });
}

//==============================================================================
static bool isEffectPlugin (te::Plugin* p)
{
    return p != nullptr && ! p->isSynth() && dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr
        && dynamic_cast<te::SamplerPlugin*> (p) == nullptr && dynamic_cast<te::FourOscPlugin*> (p) == nullptr && dynamic_cast<LinesPlugin*> (p) == nullptr && dynamic_cast<ReelSampler*> (p) == nullptr;
}

LinesPlugin* Session::getLines (te::AudioTrack& t) const { return t.pluginList.findFirstPluginOfType<LinesPlugin>(); }

LinesPlugin* Session::addLines (te::AudioTrack& t)
{
    if (auto* l = getLines (t)) return l;
    auto p = edit->getPluginCache().createNewPlugin (LinesPlugin::xmlTypeName, {});
    if (p == nullptr) return nullptr;
    t.pluginList.insertPlugin (p, 0, nullptr);   // before the instrument, so it feeds it
    if (auto* s = getSynth (t)) s->state.setProperty (te::IDs::mpe, true, &undoManager());   // each line bends on its own
    if (auto* s = getSampled (t)) s->setMpe (true);
    return dynamic_cast<LinesPlugin*> (p.get());
}

void Session::removeLines (te::AudioTrack& t)
{
    if (auto* l = getLines (t))
    {
        l->deleteFromParent();
        if (auto* s = getSynth (t)) s->state.setProperty (te::IDs::mpe, false, &undoManager());
        if (auto* s = getSampled (t)) s->setMpe (false);
    }
}

juce::Array<te::Plugin*> Session::effectChain (te::AudioTrack& t) const
{
    juce::Array<te::Plugin*> out;
    for (auto* p : t.pluginList.getPlugins()) if (isEffectPlugin (p)) out.add (p);
    return out;
}

te::Plugin* Session::getEffect (te::AudioTrack& t, const juce::String& type) const
{
    for (auto* p : effectChain (t)) if (p->getPluginType() == type) return p;
    return nullptr;
}

te::Plugin* Session::addEffect (te::AudioTrack& t, const juce::String& type)
{
    auto plugins = t.pluginList.getPlugins();
    int index = plugins.size();
    for (int i = 0; i < plugins.size(); ++i)
        if (dynamic_cast<te::VolumeAndPanPlugin*> (plugins[i].get()) || dynamic_cast<te::LevelMeterPlugin*> (plugins[i].get())) { index = i; break; }
    auto p = edit->getPluginCache().createNewPlugin (type, {});
    if (p == nullptr) return nullptr;
    t.pluginList.insertPlugin (p, index, nullptr);
    p->restorePluginStateFromValueTree (p->state);
    p->setEnabled (true);
    return p.get();
}

te::Plugin* Session::ensureEffect (te::AudioTrack& t, const juce::String& type)
{
    if (auto* p = getEffect (t, type)) return p;
    return addEffect (t, type);
}

void Session::removeEffect (te::Plugin& p)
{
    beginAction();
    p.deleteFromParent();
}

void Session::moveEffect (te::AudioTrack& t, te::Plugin& p, int delta)
{
    auto chain = effectChain (t);
    const int i = chain.indexOf (&p), j = i + delta;
    if (i < 0 || j < 0 || j >= chain.size()) return;
    beginAction();
    auto parent = p.state.getParent();
    parent.moveChild (parent.indexOf (p.state), parent.indexOf (chain[j]->state), &undoManager());
}

bool Session::isEffectOn (te::AudioTrack& t, const juce::String& type) const
{
    auto* p = getEffect (t, type);
    return p != nullptr && p->isEnabled();
}

void Session::setEffectOn (te::AudioTrack& t, const juce::String& type, bool on)
{
    beginAction();
    if (auto* p = on ? ensureEffect (t, type) : getEffect (t, type)) p->setEnabled (on);
}

juce::Array<Session::AutoTarget> Session::automationTargets (te::AudioTrack& t) const
{
    juce::Array<AutoTarget> out;
    if (auto* v = t.getVolumePlugin())
    {
        out.add ({ "volume/volume", "Volume", v->volParam.get() });
        out.add ({ "volume/pan", "Pan", v->panParam.get() });
    }
    juce::StringArray seen;
    for (auto* p : effectChain (t))
    {
        auto title = effectTitle (*p);
        int n = 2; while (seen.contains (title)) title = effectTitle (*p) + " " + juce::String (n++);
        seen.add (title);
        for (auto* ap : p->getAutomatableParameters())
            out.add ({ automationKey (*p, *ap), title + ": " + ap->getParameterName(), ap });
    }
    if (auto* s = getSynth (t))
        for (auto* ap : s->getAutomatableParameters())
            out.add ({ automationKey (*s, *ap), "Synth: " + ap->getParameterName(), ap });
    return out;
}

te::AutomatableParameter* Session::getAutomationParam (te::AudioTrack& t) const
{
    auto key = t.state.getProperty (IDs::autoParam, "volume/volume").toString();
    for (auto& a : automationTargets (t)) if (a.key == key) return a.param;
    return t.getVolumePlugin() ? t.getVolumePlugin()->volParam.get() : nullptr;
}

void Session::showAutomation (te::AudioTrack& t, const juce::String& key)
{
    t.state.setProperty (IDs::autoParam, key, nullptr);
    t.state.setProperty (IDs::autoShown, true, nullptr);
}

//==============================================================================
void Session::togglePlay()
{
    auto& tr = transport();
    if (tr.isPlaying()) tr.stop (false, false);
    else tr.play (false);
}

void Session::stop (bool rewind)
{
    auto& tr = transport();
    tr.stop (false, false);
    if (rewind) tr.setPosition (tr.looping ? tr.getLoopRange().getStart() : te::TimePosition());
}

void Session::disarmAll()
{
    for (auto* inst : edit->getAllInputDevices()) for (auto* tt : tracks()) inst->setRecordingEnabled (tt->itemID, false);
}

void Session::toggleRecord()
{
    auto& tr = transport();
    if (tr.isRecording()) { tr.stop (false, false); disarmAll(); return; }
    auto* t = getSelectedTrack();
    if (t == nullptr)
    {
        if (onMessage) onMessage ("Select a track first. An audio track records your microphone; a synth, sampler or drum track records your MIDI keyboard.");
        return;
    }
    if (getKind (*t) == Kind::audio)
    {
        auto* inst = routeAudioInput (*t);
        if (inst == nullptr) { if (onMessage) onMessage ("No microphone or audio input was found. Check Project, Audio and MIDI settings."); return; }
        disarmAll();
        inst->setRecordingEnabled (t->itemID, true);
        tr.record (false);
        if (onMessage) onMessage ("Recording " + inst->getInputDevice().getName() + " onto " + t->getName() + ". Press Space or Record to stop.");
        return;
    }
    routeLiveInput();
    disarmAll();
    for (auto* inst : edit->getAllInputDevices())
    {
        const auto type = inst->getInputDevice().getDeviceType();
        if (type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice)
            inst->setRecordingEnabled (t->itemID, true);
    }
    tr.record (false);
    if (onMessage) onMessage ("Recording onto " + t->getName() + ". Play your keyboard, then press Space to stop.");
}

void Session::prepareAudioInputs()
{
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i))
        {
            w->setEnabled (true);
            if (w->getMonitorMode() != te::InputDevice::MonitorMode::on) w->setMonitorMode (te::InputDevice::MonitorMode::off);   // no feedback through laptop speakers by default
        }
}

juce::StringArray Session::audioInputNames() const
{
    juce::StringArray s;
    auto& dm = engine.getDeviceManager();
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i) if (auto* w = dm.getWaveInDevice (i)) if (w->isEnabled()) s.add (w->getName());
    return s;
}

juce::String Session::getRecordInput (te::AudioTrack& t) const { return t.state.getProperty (IDs::recordInput).toString(); }
void Session::setRecordInput (te::AudioTrack& t, const juce::String& n) { t.state.setProperty (IDs::recordInput, n, nullptr); routeAudioInput (t); }

te::WaveInputDevice* Session::waveDeviceFor (te::AudioTrack& t) const
{
    auto& dm = engine.getDeviceManager();
    const auto wanted = getRecordInput (t);
    te::WaveInputDevice* first = nullptr;
    for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
        if (auto* w = dm.getWaveInDevice (i); w != nullptr && w->isEnabled())
        {
            if (first == nullptr) first = w;
            if (w->getName() == wanted) return w;
        }
    return first;
}

te::InputDeviceInstance* Session::routeAudioInput (te::AudioTrack& t)
{
    prepareAudioInputs();
    edit->getTransport().ensureContextAllocated();
    auto* dev = waveDeviceFor (t);
    if (dev == nullptr) return nullptr;
    for (auto* inst : edit->getAllInputDevices())
        if (&inst->getInputDevice() == dev)
        {
            [[maybe_unused]] auto r = inst->setTarget (t.itemID, true, nullptr, 0);
            edit->restartPlayback();
            return inst;
        }
    return nullptr;
}

bool Session::isInputMonitoring (te::AudioTrack& t) const
{
    auto* d = waveDeviceFor (t); return d != nullptr && d->getMonitorMode() == te::InputDevice::MonitorMode::on;
}

void Session::setInputMonitoring (te::AudioTrack& t, bool on)
{
    if (auto* d = waveDeviceFor (t)) { d->setMonitorMode (on ? te::InputDevice::MonitorMode::on : te::InputDevice::MonitorMode::off); routeAudioInput (t); }
}

te::LevelMeasurer* Session::inputMeasurer (te::AudioTrack& t) const { if (auto* d = waveDeviceFor (t)) return &d->levelMeasurer; return nullptr; }
int Session::getCountIn() const { return (int) edit->getCountInMode(); }
void Session::setCountIn (int v) { edit->setCountInMode ((te::Edit::CountIn) juce::jlimit (0, 4, v)); }

juce::File Session::renderClip (te::WaveAudioClip& c, const juce::File& file)
{
    auto* src = dynamic_cast<te::AudioTrack*> (c.getTrack());
    if (src == nullptr) return {};
    juce::BigInteger trackBits; trackBits.setBit (te::getAllTracks (*edit).indexOf (src));
    file.deleteFile();
    const bool ok = te::Renderer::renderToFile ("Render clip", file, *edit, c.getEditTimeRange(), trackBits, false, false, { &c }, false);
    return ok && file.existsAsFile() ? file : juce::File();
}

te::AudioTrack* Session::convertToMidi (te::WaveAudioClip& c, TranscribeMode mode, float sensitivity)
{
    auto file = renderClip (c, appDataDir().getChildFile ("Analysis").getChildFile ("clip.wav"));
    if (file == juce::File()) { if (onMessage) onMessage ("Could not read that clip."); return nullptr; }
    auto notes = transcribe (file, mode, sensitivity);
    if (notes.empty())
    {
        if (onMessage) onMessage (mode == TranscribeMode::drums ? "No drum hits were found in that clip." : "No clear notes were found. Try another mode, or a cleaner recording.");
        return nullptr;
    }
    beginAction();
    auto* src = dynamic_cast<te::AudioTrack*> (c.getTrack());
    const auto clipName = c.getName().isEmpty() ? juce::String ("Audio") : c.getName();
    auto* t = addTrack (mode == TranscribeMode::drums ? Kind::drums : Kind::synth, (clipName + " notes").substring (0, 40));
    if (mode != TranscribeMode::drums)
    {
        std::vector<int> ps; for (auto& n : notes) ps.push_back (n.pitch);
        std::sort (ps.begin(), ps.end());
        setSynthPreset (*t, mode == TranscribeMode::chords ? "Electric piano" : ps[ps.size() / 2] < 48 ? "Round bass" : "Soft keys");
    }
    const auto start = c.getPosition().getStart();
    double endSec = c.getPosition().getLength().inSeconds();
    for (auto& n : notes) endSec = juce::jmax (endSec, n.start + n.length);
    auto mc = t->insertMIDIClip ({ start, start + te::TimeDuration::fromSeconds (endSec) }, nullptr);
    if (mc == nullptr) return t;
    mc->setName (clipName + " notes");
    const double b0 = toBeats (start).inBeats();
    for (auto& n : notes)
    {
        const double sb = toBeats (start + te::TimeDuration::fromSeconds (n.start)).inBeats() - b0;
        const double eb = toBeats (start + te::TimeDuration::fromSeconds (n.start + n.length)).inBeats() - b0;
        mc->getSequence().addNote (n.pitch, te::BeatPosition::fromBeats (sb), te::BeatDuration::fromBeats (juce::jmax (1.0 / 32.0, eb - sb)), n.velocity, 0, &undoManager());
    }
    if (src != nullptr) moveTrack (*t, tracks().indexOf (src) + 1 - tracks().indexOf (t));
    select (t, mc.get());
    if (onMessage) onMessage ("Found " + juce::String ((int) notes.size()) + (notes.size() == 1 ? " note" : " notes") + ". They're on the new track " + t->getName() + "; change their sound in the Sound tab.");
    return t;
}

void Session::seek (te::TimePosition t) { transport().setPosition (juce::jmax (te::TimePosition(), t)); }

double Session::getBpm() const { return edit->tempoSequence.getTempo (0)->getBpm(); }

void Session::setBpm (double bpm)
{
    beginAction();
    edit->tempoSequence.getTempo (0)->setBpm (juce::jlimit (30.0, 300.0, bpm));
}

//==============================================================================
std::shared_ptr<te::VirtualMidiInputDevice> Session::keyboardDevice() const
{
    for (auto& d : engine.getDeviceManager().getMidiInDevices())
        if (d->getName() == "Reel keyboard")
            if (auto v = std::dynamic_pointer_cast<te::VirtualMidiInputDevice> (d)) return v;
    return {};
}

void Session::routeLiveInput()
{
    if (edit == nullptr) return;
    auto* target = getSelectedTrack();
    if (! isInstrument (target)) target = te::findAudioTrackForID (*edit, lastInstrumentTrack);
    for (auto& m : engine.getDeviceManager().getMidiInDevices())
    {
        m->setEnabled (true);
        m->setMonitorMode (te::InputDevice::MonitorMode::on);
    }
    edit->getTransport().ensureContextAllocated();
    if (target == nullptr) return;
    for (auto* inst : edit->getAllInputDevices())
    {
        const auto type = inst->getInputDevice().getDeviceType();
        if (type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice)
            [[maybe_unused]] auto res = inst->setTarget (target->itemID, true, nullptr, 0);
    }
    edit->restartPlayback();
}

void Session::keyboardNote (int note, float velocity, bool isOn)
{
    if (auto d = keyboardDevice())
    {
        auto msg = isOn ? juce::MidiMessage::noteOn (1, note, juce::jlimit (0.01f, 1.0f, velocity)) : juce::MidiMessage::noteOff (1, note);
        msg.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
        d->handleIncomingMidiMessage (nullptr, msg);
    }
}

juce::StringArray Session::midiInputNames() const
{
    juce::StringArray s;
    for (auto& d : engine.getDeviceManager().getMidiInDevices())
        if (d->getDeviceType() == te::InputDevice::physicalMidiDevice) s.add (d->getName());
    return s;
}

bool Session::exportMix (const juce::File& f)
{
    transport().stop (false, false);
    return te::Renderer::renderToFile (*edit, f, false);
}

//==============================================================================
void Session::loadDemo()
{
    newProject();
    setBpm (96);
    auto* drums = addTrack (Kind::drums, "Drums");
    if (auto* sc = addStepClip (*drums, {}, 8))
    {
        auto pat = sc->getPattern (0);
        for (int i = 0; i < 16; ++i)
        {
            if (i % 8 == 0 || i == 10) pat.setNote (0, i, true);
            if (i % 8 == 4) pat.setNote (1, i, true);
            if (i % 2 == 0) { pat.setNote (3, i, true); pat.setVelocity (3, i, i % 4 == 0 ? 110 : 70); }
        }
        pat.setNote (4, 14, true);
        pat.setVelocity (0, 0, 127);
    }
    const int roots[] = { 33, 29, 36, 31 };
    const int chords[4][3] = { { 57, 60, 64 }, { 57, 60, 65 }, { 55, 60, 64 }, { 55, 59, 62 } };
    auto* bass = addTrack (Kind::synth, "Bass"); setSynthPreset (*bass, "Round bass");
    if (auto* c = addMidiClip (*bass, {}, 8))
    {
        c->setName ("Bass line");
        for (int i = 0; i < 4; ++i) for (int bar = 0; bar < 2; ++bar)
        {
            const double o = i * 8 + bar * 4; const int r = roots[i];
            for (auto [p, t, d, v] : { std::tuple<int, double, double, int> { r, 0, 0.75, 115 }, { r, 1.5, 0.4, 90 }, { r + 12, 2.5, 0.4, 95 }, { r, 3, 0.8, 100 } })
                c->getSequence().addNote (p, te::BeatPosition::fromBeats (o + t), te::BeatDuration::fromBeats (d), v, 0, nullptr);
        }
    }
    auto* keys = addTrack (Kind::synth, "Keys"); setSynthPreset (*keys, "Electric piano");
    if (auto* c = addMidiClip (*keys, {}, 8))
    {
        c->setName ("Chords");
        for (int i = 0; i < 4; ++i) for (int p : chords[i])
            for (auto [t, d, v] : { std::tuple<double, double, int> { 0, 3.8, 90 }, { 4, 1.5, 70 }, { 6, 1.8, 75 } })
                c->getSequence().addNote (p, te::BeatPosition::fromBeats (i * 8 + t), te::BeatDuration::fromBeats (d), v, 0, nullptr);
    }
    setEffectOn (*keys, ReverbPlugin::xmlTypeName, true);
    if (auto* v = keys->getVolumePlugin()) v->setVolumeDb (-4.0f);
    auto* lead = addTrack (Kind::synth, "Lead"); setSynthPreset (*lead, "Pluck");
    const double mel[][3] = { { 76, 0, 1 }, { 74, 1, 0.5 }, { 72, 1.5, 0.5 }, { 69, 2, 2 }, { 72, 4, 1 }, { 74, 5, 1 }, { 76, 6, 1.5 }, { 79, 7.5, 0.5 }, { 77, 8, 2 }, { 76, 10, 1 }, { 74, 11, 1 }, { 72, 12, 3 }, { 74, 15, 1 }, { 71, 16, 2 }, { 72, 18, 1 }, { 74, 19, 1 }, { 76, 20, 3 }, { 74, 23, 0.5 }, { 72, 23.5, 0.5 }, { 74, 24, 4 } };
    if (auto* c = addMidiClip (*lead, toTime (te::BeatPosition::fromBeats (16)), 8))
    {
        c->setName ("Melody");
        for (auto& m : mel) c->getSequence().addNote ((int) m[0], te::BeatPosition::fromBeats (m[1]), te::BeatDuration::fromBeats (m[2] * 0.9), 100, 0, nullptr);
    }
    setEffectOn (*lead, EchoPlugin::xmlTypeName, true);
    setEffectOn (*lead, ReverbPlugin::xmlTypeName, true);
    if (auto* d = dynamic_cast<EchoPlugin*> (getEffect (*lead, EchoPlugin::xmlTypeName)))
    {
        auto& curve = d->mix->getCurve();
        for (auto [b, v] : { std::pair<double, float> { 16, 0.1f }, { 30, 0.1f }, { 32, 0.6f }, { 40, 0.6f }, { 44, 0.15f } })
            curve.addPoint (toTime (te::BeatPosition::fromBeats (b)), v, 0.0f);
        showAutomation (*lead, automationKey (*d, *d->mix));
    }
    if (auto* r = dynamic_cast<ReverbPlugin*> (getEffect (*keys, ReverbPlugin::xmlTypeName))) r->mix->setParameter (0.35f, juce::sendNotification);
    if (auto* v = lead->getVolumePlugin()) v->setVolumeDb (-3.0f);
    for (auto [t, db] : { std::pair<te::AudioTrack*, float> { drums, -3.0f }, { bass, -4.0f }, { keys, -7.0f }, { lead, -6.0f } })
        if (auto* v = t->getVolumePlugin()) v->setVolumeDb (db);
    undoManager().clearUndoHistory();
    select (drums, drums->getClips().getFirst());
    seek ({});
}
}
