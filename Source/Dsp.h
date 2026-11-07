#pragma once
#include "Common.h"

namespace Reel
{
    struct PitchResult { double freq = 0; int note = 60; int cents = 0; double confidence = 0; };

    /** Writes the built-in drum kit (synthesised once) to the app data folder and returns the files in drumVoices order. */
    juce::Array<juce::File> ensureDrumKitFiles();

    /** Finds the musical note of a sound (YIN). Returns nothing for noises and drums. */
    std::optional<PitchResult> detectPitch (const juce::File&, double startSec = 0, double lengthSec = 0);

    bool writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
}

namespace Reel
{
    /** A note found in a recording. Times in seconds from the start of the file. */
    struct TranscribedNote { int pitch = 60; double start = 0, length = 0.1; int velocity = 100; };
    enum class TranscribeMode { melody, chords, drums };

    /** Finds the notes in an audio file.
        melody: one note at a time (voice, bass, lead). Accurate pitch and timing.
        chords: several notes at once (piano, pads, guitar chords). Best on clean recordings.
        drums:  hits, sorted into kick, snare and hi-hat.
        sensitivity 0..1: higher finds quieter notes. */
    std::vector<TranscribedNote> transcribe (const juce::File&, TranscribeMode, float sensitivity = 0.5f);

    /** The pitch curve of a recording as lines: each sung or played phrase keeps its slides and vibrato.
        Point times are seconds from the start of the file; level (0..1) is the loudness there. */
    struct ContourPoint { double time; float pitch, level; };
    struct ContourLine { std::vector<ContourPoint> points; int velocity = 100; };
    std::vector<ContourLine> transcribeContour (const juce::File&, float sensitivity = 0.5f);
}

namespace Reel
{
    /** Where the hits are in a sound, in seconds (always starts with 0). sensitivity 0..1: higher finds softer hits. */
    std::vector<double> findTransients (const juce::AudioBuffer<float>&, double sampleRate, float sensitivity);
}
