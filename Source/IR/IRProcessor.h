#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Ported from MultibandConvolver (D:\Claude Projects\MultibandConvolver\Source\DSP\IRProcessor.h).
// Pure, stateless IR shaping: loads a catalog entry and applies stretch (resample) + fade
// in/out. Deliberately free of any shared state so it's safe to call from a background thread --
// see IRReshapeWorker, which is what actually calls this off the audio thread.
namespace GGrid::IRProcessor
{
    // Returns false (leaving outShapedIR empty) if the catalog entry couldn't be loaded.
    bool buildShapedIR (int irIndex, double sampleRate, float fadeInMs, float fadeOutPercent, float stretch,
                         juce::AudioBuffer<float>& outShapedIR);
}
