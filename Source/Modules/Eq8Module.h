#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // A classic graphic EQ: kNumEq8Bands fixed-frequency peaking bands, one octave apart
    // (see kEq8BandFrequencies), each with just a gain knob -- no per-band frequency/Q
    // controls, unlike a parametric EQ. Cascaded IIR peaking filters, one chain per channel.
    class Eq8Module : public RackModule
    {
    public:
        Eq8Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        static constexpr int kMaxEq8Channels = 2;
        // Musical bell width for octave-spaced bands -- narrow enough that adjacent bands don't
        // smear into each other much, wide enough to sound like a graphic EQ rather than a
        // surgical notch.
        static constexpr float kBandQ = 1.0f;

        int slotIndex;
        double sampleRate = 44100.0;

        std::array<std::atomic<float>*, kNumEq8Bands> bandParams {};
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // [channel][band] -- each channel gets its own independent filter chain.
        std::array<std::array<juce::dsp::IIR::Filter<float>, kNumEq8Bands>, kMaxEq8Channels> bandFilters;

        juce::AudioBuffer<float> dryBuffer;
    };
}
