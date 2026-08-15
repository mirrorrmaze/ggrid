#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Ported from MultibandConvolver (D:\Claude Projects\MultibandConvolver\Source\DSP\IRProcessor.h).
// Pure, stateless IR shaping: loads a catalog entry and applies stretch (resample) + fade
// in/out. Deliberately free of any shared state so it's safe to call from a background thread --
// see IRReshapeWorker, which is what actually calls this off the audio thread.
namespace GGrid::IRProcessor
{
    // Returns false (leaving outShapedIR empty) if the catalog entry couldn't be loaded.
    //
    // outPreFadeOutLength is the buffer's length in samples *before* Fade Out's truncation (i.e.
    // after Stretch, at Fade Out = 0%) -- outShapedIR itself may be shorter, since Fade Out
    // actually shortens the buffer rather than just tapering it in place (see the comment at the
    // fade-out step below). Callers that display this waveform need both numbers: without the
    // pre-truncation length, a display that always stretches whatever buffer it has to fill its
    // full width makes shortening via Fade Out look like time-stretching instead of a cut.
    bool buildShapedIR (int irIndex, double sampleRate, float fadeInMs, float fadeOutPercent, float stretch,
                         juce::AudioBuffer<float>& outShapedIR, int& outPreFadeOutLength);
}
