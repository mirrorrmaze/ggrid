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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IRWaveformComponent)
    };
}
