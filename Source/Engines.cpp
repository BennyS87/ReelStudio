#include "Engines.h"

namespace Reel
{
using namespace juce;

static Identifier fileId (int i) { return Identifier ("file" + String (i)); }
static Identifier rootId (int i) { return Identifier ("root" + String (i)); }
static Identifier nameId (int i) { return Identifier ("name" + String (i)); }
static Identifier laneId (int i) { return Identifier ("lane" + String (i)); }
static String secs (float v) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; }

EnginePlugin::EnginePlugin (te::PluginCreationInfo info) : ReelEffect (info)
{
    addChoice (engine, "engine", "Engine", engineNames(), 0);
    addChoice (chordType, "chordType", "Chord", chordNames(), 0);
    addChoice (inversion, "inversion", "Inversion", { "Root", "First", "Second", "Third" }, 0);
    addChoice (motion, "motion", "Motion", motionNames(), 1);
    addChoice (divisi, "divisi", "Divisi", { "Off", "On" }, 0);
    addChoice (latticeRate, "latticeRate", "Rate", rateNames(), 3);
    addChoice (disperseVoices, "voices", "Voices", { "2", "3", "4", "5", "6", "7", "8" }, 3);
    StringArray stepNames; for (int i = 1; i <= maxSteps; ++i) stepNames.add (String (i));
    const int defSteps[] = { 7, 4, 11 }, defRate[] = { 3, 3, 1 }, defOrder[] = { 0, 2, 3 };
    for (int i = 0; i < numLanes; ++i)
    {
        addChoice (laneSteps[i], "steps" + String (i), "Steps", stepNames, defSteps[i]);
        addChoice (laneRate[i], "rate" + String (i), "Rate", rateNames(), defRate[i]);
        addChoice (laneOrder[i], "order" + String (i), "Order", orderNames(), defOrder[i]);
    }
    level = addP ("level", "Level", { -30.0f, 6.0f, 0.1f }, -3.0f, Fmt::db);
    attack = addP ("attack", "Attack", Fmt::skewed (0.001f, 4.0f, 0.2f), 0.01f, secs);
    release = addP ("release", "Release", Fmt::skewed (0.01f, 8.0f, 0.6f), 0.4f, secs);
    blend = addP ("blend", "Blend", { 0.0f, 1.0f }, 0.35f, Fmt::pct);
    drift = addP ("drift", "Drift", { 0.0f, 1.0f }, 0.3f, Fmt::pct);
    posX = addP ("x", "X", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    posY = addP ("y", "Y", { 0.0f, 1.0f }, 0.5f, Fmt::pct);
    speed = addP ("speed", "Orbit time", Fmt::skewed (0.25f, 16.0f, 2.0f), 2.0f, [] (float v) { return String (v, 2) + " bars"; });
    radius = addP ("radius", "Radius", { 0.0f, 0.5f }, 0.35f, Fmt::pct);
    spread = addP ("spread", "Spread", { 0.0f, 24.0f, 0.1f }, 7.0f, Fmt::st);
    strum = addP ("strum", "Strum", { 0.0f, 200.0f, 1.0f }, 0.0f, [] (float v) { return String (roundToInt (v)) + " ms"; });
    glide = addP ("glide", "Glide", Fmt::skewed (0.01f, 8.0f, 1.0f), 1.5f, secs);
    density = addP ("density", "Density", { 0.0f, 1.0f }, 0.75f, Fmt::pct);
    jumps = addP ("jumps", "Octave jumps", { 0.0f, 1.0f }, 0.2f, Fmt::pct);
    gate = addP ("gate", "Gate", { 0.05f, 1.0f }, 0.6f, Fmt::pct);
    humanize = addP ("humanize", "Humanize", { 0.0f, 1.0f }, 0.15f, Fmt::pct);
    shift = addP ("shift", "Shift", { -0.5f, 0.5f }, 0.0f, Fmt::pct);
    for (int i = 0; i < numLayers; ++i)
    {
        layerLevel[i] = addP ("layerLevel" + String (i), "Level", { -30.0f, 6.0f, 0.1f }, 0.0f, Fmt::db);
        layerPan[i] = addP ("layerPan" + String (i), "Pan", { -1.0f, 1.0f }, (i % 2 == 0 ? -0.25f : 0.25f),
                            [] (float v) { return std::abs (v) < 0.01f ? String ("C") : (v < 0 ? "L" : "R") + String (roundToInt (std::abs (v) * 100)); });
        layerTune[i] = addP ("layerTune" + String (i), "Tune", { -24.0f, 24.0f, 1.0f }, 0.0f, Fmt::st);
    }
    state.addListener (&watcher);
    handleAsyncUpdate();
}

EnginePlugin::~EnginePlugin() { cancelPendingUpdate(); state.removeListener (&watcher); }

void EnginePlugin::initialise (const te::PluginInitialisationInfo& i) { sampleRate = i.sampleRate; }
void EnginePlugin::reset() { for (auto& v : voices) v.active = false; held.clear(); }

//==============================================================================
void EnginePlugin::handleAsyncUpdate()
{
    for (int i = 0; i < numLayers; ++i)
    {
        const String path = state.getProperty (fileId (i)).toString();
        const int root = state.getProperty (rootId (i), 60);
        std::shared_ptr<const Sound> current;
        { const SpinLock::ScopedLockType l (soundLock); current = sounds[(size_t) i]; }
        if (path == loadedPaths[(size_t) i] && (current == nullptr || current->root == root)) continue;
        std::shared_ptr<Sound> s;
        if (path == loadedPaths[(size_t) i] && current != nullptr) { s = std::make_shared<Sound> (*current); s->root = root; }
        else if (File::isAbsolutePath (path) && File (path).existsAsFile())
        {
            AudioFormatManager fm; fm.registerBasicFormats();
            if (std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (path))); r != nullptr && r->lengthInSamples > 1)
            {
                s = std::make_shared<Sound>();
                const int n = (int) jmin (r->lengthInSamples, (int64) (r->sampleRate * 60));
                s->data.setSize ((int) jlimit (1u, 2u, r->numChannels), n + 1); s->data.clear();
                r->read (&s->data, 0, n, 0, true, true);
                s->rate = r->sampleRate; s->root = root;
            }
        }
        loadedPaths[(size_t) i] = path;
        const SpinLock::ScopedLockType l (soundLock);
        sounds[(size_t) i] = std::move (s);
    }
}

bool EnginePlugin::setLayerSound (int i, const File& f, int root, const String& name, UndoManager* um)
{
    if (i < 0 || i >= numLayers || ! f.existsAsFile()) return false;
    state.setProperty (fileId (i), f.getFullPathName(), um);
    state.setProperty (rootId (i), root, um);
    state.setProperty (nameId (i), name.isEmpty() ? f.getFileNameWithoutExtension() : name, um);
    handleAsyncUpdate();
    return layerLoaded (i);
}

void EnginePlugin::clearLayer (int i, UndoManager* um)
{
    for (auto id : { fileId (i), rootId (i), nameId (i) }) state.removeProperty (id, um);
    handleAsyncUpdate();
}

File EnginePlugin::layerFile (int i) const   { return File (state.getProperty (fileId (i)).toString()); }
String EnginePlugin::layerName (int i) const { return state.getProperty (nameId (i)).toString(); }
int EnginePlugin::layerRoot (int i) const    { return state.getProperty (rootId (i), 60); }
bool EnginePlugin::layerLoaded (int i) const { const SpinLock::ScopedLockType l (const_cast<SpinLock&> (soundLock)); return sounds[(size_t) i] != nullptr; }

std::array<float, EnginePlugin::maxSteps> EnginePlugin::laneVelocities (int lane) const
{
    std::array<float, maxSteps> v;
    auto parts = StringArray::fromTokens (state.getProperty (laneId (lane)).toString(), " ", {});
    // a pattern to start from: strong first step, lighter off-beats, a few rests
    const float def[numLanes][maxSteps] = { { 1, 0, .6f, 0, .8f, 0, .6f, .4f, 1, 0, .6f, 0, .8f, 0, .6f, .4f },
                                            { .9f, .5f, 0, .7f, .5f, .9f, .5f, 0, .7f, .5f, .9f, .5f, 0, .7f, .5f, .9f },
                                            { .7f, 0, 0, .5f, 0, .6f, 0, 0, .7f, 0, .4f, 0, .6f, 0, 0, .5f } };
    for (int s = 0; s < maxSteps; ++s) v[(size_t) s] = s < parts.size() ? jlimit (0.0f, 1.0f, parts[s].getFloatValue()) : def[jlimit (0, numLanes - 1, lane)][s];
    return v;
}

void EnginePlugin::setLaneVelocity (int lane, int step, float value, UndoManager* um)
{
    auto v = laneVelocities (lane);
    v[(size_t) jlimit (0, maxSteps - 1, step)] = jlimit (0.0f, 1.0f, value);
    StringArray s; for (auto x : v) s.add (String (x, 2));
    state.setProperty (laneId (lane), s.joinIntoString (" "), um);
}

//==============================================================================
float EnginePlugin::layerWeight (int layer) const
{
    // Blend brings the layers in one after another: 0 = first layer alone, 1 = all four
    return jlimit (0.0f, 1.0f, 1.0f + blend->getCurrentValue() * (numLayers - 1) - (float) layer);
}

void EnginePlugin::startVoice (int layer, float pitch, float vel, int key, float gainMul, int delaySamples, int gateSamples, float fromPitch, float glideSeconds)
{
    std::shared_ptr<const Sound> snd;
    { const SpinLock::ScopedTryLockType l (soundLock); if (l.isLocked()) snd = sounds[(size_t) layer]; }
    if (snd == nullptr || gainMul <= 0.0001f) return;
    Voice* v = nullptr;
    for (auto& x : voices) if (! x.active) { v = &x; break; }
    if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.stage == 2 && x.env < v->env) v = &x; }   // steal the quietest fading voice
    *v = {};
    v->active = true; v->layer = layer; v->key = key; v->snd = snd; v->vel = vel; v->gainMul = gainMul;
    v->target = pitch + layerTune[layer]->getCurrentValue();
    v->pitch = fromPitch >= 0 ? fromPitch + layerTune[layer]->getCurrentValue() : v->target;
    v->glideStep = glideSeconds > 0 ? (v->target - v->pitch) / (float) (glideSeconds * sampleRate) : 0.0f;
    v->delay = delaySamples; v->gateLeft = gateSamples;
}

void EnginePlugin::releaseKey (int key)
{
    for (auto& v : voices) if (v.active && v.key == key && v.gateLeft < 0) v.stage = 2;
}

void EnginePlugin::onNoteOn (int note, float vel)
{
    const int type = engine.get();
    if (type == threadsEngine || type == latticeEngine) return;   // the sequencers play held notes on their own clock
    if (type == layersEngine || type == orbitsEngine)
    {
        for (int i = 0; i < numLayers; ++i) startVoice (i, (float) note, vel, note, type == orbitsEngine ? 1.0f : layerWeight (i));
        return;
    }
    if (type == chordsEngine)
    {
        static const std::vector<int> shapes[] = { { 0, 4, 7 }, { 0, 3, 7 }, { 0, 2, 7 }, { 0, 5, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 4, 7, 10, 14 }, { 0, 7, 12 }, { 0, 1, 2, 5 } };
        auto iv = shapes[jlimit (0, 8, chordType.get())];
        for (int k = 0; k < inversion.get() && k < (int) iv.size(); ++k) { iv[(size_t) k] += 12; }
        std::sort (iv.begin(), iv.end());
        const float spreadOct = spread->getCurrentValue() / 12.0f;   // spread opens the voicing upwards
        const int strumSamples = roundToInt (strum->getCurrentValue() * 0.001 * sampleRate);
        for (int k = 0; k < (int) iv.size(); ++k)
        {
            const float lift = iv.size() > 1 ? 12.0f * std::round (spreadOct * (float) k / (float) (iv.size() - 1)) : 0.0f;
            for (int i = 0; i < numLayers; ++i) startVoice (i, (float) (note + iv[(size_t) k]) + lift, vel, note, layerWeight (i), k * strumSamples);
        }
        return;
    }
    if (type == disperseEngine)
    {
        const int n = disperseVoices.get() + 2;
        const float sp = spread->getCurrentValue();
        for (int k = 0; k < n; ++k)
        {
            const float offset = n == 1 ? 0.0f : -sp + 2.0f * sp * (float) k / (float) (n - 1);
            const int layer = k % numLayers;
            startVoice (layerWeight (layer) > 0 ? layer : 0, (float) note + offset, vel * 0.8f, note, 1.0f / std::sqrt ((float) n) * 1.6f, 0, -1, (float) note, glide->getCurrentValue());
        }
    }
}

void EnginePlugin::sequencerStep (double, int lane)
{
    if (held.empty()) return;
    const double spb = 60.0 / currentBpm();
    if (lane < 0)   // Lattice: retrigger the held note
    {
        const auto& h = held.back();
        if (rng.nextFloat() > density->getCurrentValue()) { ++latticeCount; return; }
        float pitch = (float) h.note;
        if (rng.nextFloat() < jumps->getCurrentValue()) pitch += rng.nextBool() ? 12.0f : -12.0f;
        const int len = roundToInt (rateBeats (latticeRate.get()) * spb * gate->getCurrentValue() * sampleRate);
        const float accent = latticeCount % 4 == 0 ? 1.0f : 0.75f;
        for (int i = 0; i < numLayers; ++i) startVoice (i, pitch, h.vel * accent, -2, layerWeight (i), 0, len);
        ++latticeCount;
        return;
    }
    // Threads: each lane plays its own layer with its own steps, rate and order
    if (lane > 0 && blend->getCurrentValue() * numLanes < (float) lane) { ++laneCount[lane]; return; }
    const int steps = laneSteps[lane].get() + 1;
    const int idx = laneCount[lane] % steps;
    shownStep[lane].store (idx);
    const float velStep = laneVelocities (lane)[(size_t) idx];
    const int count = laneCount[lane]++;
    if (velStep <= 0.0f) return;
    std::vector<Held> notes = held;
    std::sort (notes.begin(), notes.end(), [&] (auto& a, auto& b) { return laneOrder[lane].get() == 4 ? a.order < b.order : a.note < b.note; });
    if (divisi.get() == 1 && notes.size() >= 2)
    {
        std::vector<Held> mine; for (size_t k = 0; k < notes.size(); ++k) if ((int) (k % numLanes) == lane) mine.push_back (notes[k]);
        if (! mine.empty()) notes = mine;
    }
    const int n = (int) notes.size();
    int pick = 0;
    switch (laneOrder[lane].get())
    {
        case 1: pick = n - 1 - count % n; break;
        case 2: { const int p = jmax (1, 2 * n - 2); const int i = count % p; pick = n == 1 ? 0 : (i < n ? i : p - i); break; }
        case 3: pick = rng.nextInt (n); break;
        default: pick = count % n; break;
    }
    const auto& h = notes[(size_t) pick];
    const double stepSec = rateBeats (laneRate[lane].get()) * spb;
    const int len = roundToInt (stepSec * gate->getCurrentValue() * sampleRate);
    const float hum = humanize->getCurrentValue();
    const int late = roundToInt (rng.nextFloat() * hum * 0.02 * sampleRate);
    const float v = jlimit (0.05f, 1.0f, h.vel * velStep * (1.0f - hum * 0.3f * rng.nextFloat()));
    const int layer = layerLoaded (lane) ? lane : 0;
    startVoice (layer, (float) h.note, v, -3, 1.0f, late, len);
}

//==============================================================================
void EnginePlugin::render (AudioBuffer<float>& out, int start, int num)
{
    const float master = Decibels::decibelsToGain (level->getCurrentValue());
    const float att = jmax (0.001f, attack->getCurrentValue()), rel = jmax (0.01f, release->getCurrentValue());
    const float attStep = 1.0f / (float) (att * sampleRate), relMul = std::exp (-4.6f / (float) (rel * sampleRate));
    const int type = engine.get();
    const float dr = drift->getCurrentValue();
    float layerGain[numLayers], panG[numLayers][2];
    for (int i = 0; i < numLayers; ++i)
    {
        // drift: each layer breathes at its own slow rate
        const float wob = dr > 0 ? 1.0f - dr * 0.45f * (1.0f + (float) std::sin (driftPhase * (0.7 + 0.37 * i) + i * 1.7)) : 1.0f;
        layerGain[i] = Decibels::decibelsToGain (layerLevel[i]->getCurrentValue()) * wob * (type == orbitsEngine ? orbitW[i] : 1.0f);
        const float p = (layerPan[i]->getCurrentValue() + 1.0f) * 0.5f;
        panG[i][0] = std::cos (p * MathConstants<float>::halfPi) * 1.4142f; panG[i][1] = std::sin (p * MathConstants<float>::halfPi) * 1.4142f;
    }
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const int len = v.snd->data.getNumSamples(), chans = v.snd->data.getNumChannels();
        const float g = master * layerGain[v.layer] * v.gainMul * (0.3f + 0.7f * v.vel);
        for (int i = 0; i < num; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            if (v.gateLeft >= 0 && --v.gateLeft < 0) v.stage = 2;
            if (v.stage == 0) { v.env += attStep; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 1; } }
            else if (v.stage == 2) { v.env *= relMul; if (v.env < 0.0005f) { v.active = false; v.snd.reset(); break; } }
            if (v.glideStep != 0.0f) { v.pitch += v.glideStep; if ((v.glideStep > 0 && v.pitch >= v.target) || (v.glideStep < 0 && v.pitch <= v.target)) { v.pitch = v.target; v.glideStep = 0; } }
            const double ratio = std::pow (2.0, (v.pitch - v.snd->root) / 12.0) * v.snd->rate / sampleRate;
            const int k = (int) v.pos;
            if (k >= len - 1) { v.active = false; v.snd.reset(); break; }
            const float fr = (float) (v.pos - k);
            for (int c = 0; c < jmin (2, out.getNumChannels()); ++c)
            {
                const float* d = v.snd->data.getReadPointer (jmin (c, chans - 1));
                out.addSample (c, start + i, (d[k] + (d[k + 1] - d[k]) * fr) * v.env * g * panG[v.layer][c]);
            }
            v.pos += ratio;
        }
    }
}

void EnginePlugin::applyToBuffer (const te::PluginRenderContext& fc)
{
    if (fc.destBuffer == nullptr) return;
    auto& out = *fc.destBuffer;
    for (int c = 0; c < out.getNumChannels(); ++c) out.clear (c, fc.bufferStartSample, fc.bufferNumSamples);
    const double spb = 60.0 / currentBpm(), bps = 1.0 / spb;
    double b0;
    if (fc.isPlaying) { b0 = fc.editTime.getStart().inSeconds() * bps; if (! wasPlaying) origin = 0; }
    else b0 = freeBeat;
    wasPlaying = fc.isPlaying;
    const int n = fc.bufferNumSamples;
    freeBeat = b0 + n / sampleRate * bps;

    struct Ev { int at; MidiMessage m; };
    std::vector<Ev> evs;
    if (fc.bufferForMidiMessages != nullptr)
    {
        if (fc.bufferForMidiMessages->isAllNotesOff) { for (auto& v : voices) if (v.active) v.stage = 2; held.clear(); }
        for (auto& m : *fc.bufferForMidiMessages)
            if (m.isNoteOnOrOff()) evs.push_back ({ jlimit (0, jmax (0, n - 1), roundToInt (m.getTimeStamp() * sampleRate)), m });
        std::stable_sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return a.at < b.at; });
    }
    const int type = engine.get();
    size_t ei = 0;
    constexpr int chunk = 32;
    for (int s = 0; s < n; s += chunk)
    {
        const int len = jmin (chunk, n - s);
        while (ei < evs.size() && evs[ei].at < s + len)
        {
            const auto& m = evs[ei++].m;
            const int note = m.getNoteNumber();
            if (m.isNoteOn())
            {
                const bool wasEmpty = held.empty();
                held.push_back ({ note, m.getFloatVelocity(), orderCounter++ });
                if (wasEmpty)
                {
                    if (! fc.isPlaying) origin = b0 + s / sampleRate * bps;
                    for (int l = 0; l < numLanes; ++l) { laneCount[l] = 0; laneLast[l] = -1e9; }
                    latticeCount = 0; latticeLast = -1e9;
                }
                onNoteOn (note, m.getFloatVelocity());
            }
            else
            {
                held.erase (std::remove_if (held.begin(), held.end(), [note] (auto& h) { return h.note == note; }), held.end());
                releaseKey (note);
            }
        }
        // sequencer steps that fall in this slice
        const double beatHere = b0 + (s + len) / sampleRate * bps - origin;
        if (! held.empty() && (type == threadsEngine || type == latticeEngine))
        {
            if (type == latticeEngine)
            {
                const double step = rateBeats (latticeRate.get());
                const double idx = std::floor (beatHere / step - 1e-9);
                if (idx != latticeLast) { latticeLast = idx; sequencerStep (beatHere, -1); }
            }
            else
                for (int l = 0; l < numLanes; ++l)
                {
                    const double step = rateBeats (laneRate[l].get());
                    const double idx = std::floor (beatHere / step - shift->getCurrentValue() - 1e-9);
                    if (idx != laneLast[l]) { laneLast[l] = idx; sequencerStep (beatHere, l); }
                }
        }
        if (held.empty()) for (auto& st : shownStep) st.store (-1);
        // orbit: move the point and blend the four corners
        if (type == orbitsEngine)
        {
            const double bars = jmax (0.25, (double) speed->getCurrentValue());
            orbitPhase += len / sampleRate * bps / (4.0 * bars) * MathConstants<double>::twoPi;
            float x = posX->getCurrentValue(), y = posY->getCurrentValue();
            const float r = radius->getCurrentValue();
            switch (motion.get())
            {
                case 1: x += r * (float) std::cos (orbitPhase); y += r * (float) std::sin (orbitPhase); break;
                case 2: x += r * (float) std::sin (orbitPhase); y += r * 0.6f * (float) std::sin (2.0 * orbitPhase); break;
                case 3: { const float rr = r * (float) (0.5 + 0.5 * std::cos (orbitPhase * 0.25)); x += rr * (float) std::cos (orbitPhase); y += rr * (float) std::sin (orbitPhase); break; }
                default: break;
            }
            x = jlimit (0.0f, 1.0f, x); y = jlimit (0.0f, 1.0f, y);
            shownX.store (x); shownY.store (y);
            const float w[] = { (1 - x) * (1 - y), x * (1 - y), (1 - x) * y, x * y };   // corners: top left, top right, bottom left, bottom right
            for (int i = 0; i < numLayers; ++i) orbitW[i] = std::sqrt (w[i]);
        }
        driftPhase += len / sampleRate * MathConstants<double>::twoPi * 0.12;
        render (out, fc.bufferStartSample + s, len);
    }
}
}
