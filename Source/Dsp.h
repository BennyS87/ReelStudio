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
