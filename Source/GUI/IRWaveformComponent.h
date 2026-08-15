#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Rack/RackSlot.h"

namespace GGrid
{
    // Polls its slot (via a Timer) for a freshly-shaped IR buffer and draws a simple
    // min/max-per-pixel-column waveform of it. Direct in-memory drawing rather than a
    // juce::AudioThumbnail -- these buffers are small (a few seconds at most) and only change
    // rarely (after the debounced reshape completes), so there's no need for AudioThumbnail's
    // disk-backed caching machinery.
    //
    // Holds the owning RackSlot rather than a ConvolutionModule reference directly, and
    // dynamic_casts to ConvolutionModule fresh on every timer tick -- RackSlot swaps out its
    // module instance whenever the slot's type changes (even switching away from and back to
    // Convolution creates a *new* instance), so holding a module reference from construction
    // time would go stale.
    class IRWaveformComponent : public juce::Component, private juce::Timer
    {
    public:
        explicit IRWaveformComponent (RackSlot& rackSlotIn);
        ~IRWaveformComponent() override;

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;

        RackSlot& rackSlot;
        juce::AudioBuffer<float> displayBuffer;
        juce::int64 lastSeenGeneration = -1;

        // displayBuffer's length before Fade Out's truncation -- paint() draws the waveform
        // proportionally within this as the reference width instead of always stretching
        // displayBuffer to fill the component, so raising Fade Out visibly cuts the tail short
        // (leaving blank space) rather than looking like the whole thing got time-stretched.
        int preFadeOutLength = 0;

        // Length (in samples) of the real declick ramp IRProcessor::buildShapedIR applied at the
        // cut point -- 0 if Fade Out isn't currently truncating anything. paint() converts this
        // to a pixel width via the same fullLength/width scale as everything else, so the drawn
        // taper actually matches the real (small, fixed ~10ms) audio ramp instead of an invented
        // fraction of whatever visible width happens to be left.
        int fadeRampSamples = 0;

        // Set when displayBuffer is still empty AND IRLibrary::resolveIRRoot() can't find the
        // factory library at all -- distinguishes "genuinely still loading" from "never going to
        // load until the IR library is where the plugin expects it," which otherwise both look
        // identical (an empty displayBuffer) and would sit on the same "(loading...)" message
        // forever with no indication anything's actually wrong.
        bool irLibraryMissing = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRWaveformComponent)
    };
}
