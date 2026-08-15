#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Ported from MultibandConvolver (D:\Claude Projects\MultibandConvolver\Source\DSP\IRProcessor.h).
// Pure, stateless IR shaping: loads a catalog entry and applies stretch (resample) + fade
// in/out. Deliberately free of any shared state so it's safe to call from a background thread --
// see IRReshapeWorker, which is what actually calls this off the audio thread.
namespace GGrid::IRProcessor
{
    // Absolute cap on how long Stretch can ever grow a shaped IR to, regardless of the 0.25x-4x
    // multiplier or the source's own length -- see buildShapedIR's Stretch comment. Exposed here
    // (not just a local constant inside buildShapedIR) so callers that need to pre-size a buffer
    // to the worst case an IR can ever reach -- e.g. ConvolutionModule's GUI display copy,
    // avoiding a reallocation on the audio thread every time a new IR is applied -- share the
    // exact same number rather than duplicating it and hoping it stays in sync.
    constexpr double kMaxIRGrowthSeconds = 8.0;

    // Returns false (leaving outShapedIR empty) if the catalog entry couldn't be loaded.
    //
    // outPreFadeOutLength is the buffer's length in samples *before* Fade Out's truncation (i.e.
    // after Stretch, at Fade Out = 0%) -- outShapedIR itself may be shorter, since Fade Out
    // actually shortens the buffer rather than just tapering it in place (see the comment at the
    // fade-out step below). Callers that display this waveform need both numbers: without the
    // pre-truncation length, a display that always stretches whatever buffer it has to fill its
    // full width makes shortening via Fade Out look like time-stretching instead of a cut.
    //
    // outFadeRampSamples is the length (in samples, at `sampleRate`) of the short declick ramp
    // actually applied at the new cut point -- 0 if Fade Out didn't cause any truncation this
    // call. A caller drawing that ramp on the waveform display needs the real length rather than
    // inventing its own (e.g. a fraction of however much visible width happens to be left):
    // that both draws a taper when there isn't one (Fade Out = 0%, nothing was truncated) and
    // exaggerates it into eating a growing fraction of the shrinking visible region as Fade Out
    // increases, when the real ramp is a small fixed ~10ms regardless.
    bool buildShapedIR (int irIndex, double sampleRate, float fadeInMs, float fadeOutPercent, float stretch,
                         juce::AudioBuffer<float>& outShapedIR, int& outPreFadeOutLength, int& outFadeRampSamples);
}
