#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // One "Filter" rack module type covering two different DSP approaches, picked by the
    // Filter Type choice:
    //   - Low Pass / High Pass / Band Pass / Notch: standard biquads (juce::dsp::IIR), using
    //     Frequency + Resonance.
    //   - Comb (Feedback) / Comb (Feedforward) / Allpass (Diffusor): a per-channel delay line
    //     with Frequency mapped to pitch (delay = sampleRate / frequency) and Feedback
    //     controlling character. The Allpass Diffusor is a Schroeder allpass -- the classic
    //     reverb-diffusion building block -- standing in for a dedicated reverb algorithm until
    //     the convolution module lands.
    class FilterModule : public RackModule
    {
    public:
        FilterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        void updateBiquadCoefficients (float frequency, float resonance);
        float processDelayBasedSample (int channel, float x, int type, float feedback);

        int slotIndex;

        std::atomic<float>* frequencyParam;
        std::atomic<float>* typeParam;
        std::atomic<float>* resonanceParam;
        std::atomic<float>* feedbackParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // Fixed at 2 channels -- same tradeoff as WaveshaperModule's oversampling: GGrid's
        // buses are mono/stereo, and this keeps per-channel DSP state simple to own directly
        // rather than through a dynamically-sized container.
        static constexpr int kMaxFilterChannels = 2;

        juce::dsp::IIR::Filter<float> biquad[kMaxFilterChannels];
        juce::dsp::DelayLine<float> delayLine[kMaxFilterChannels];

        double currentSampleRate = 44100.0;
        juce::AudioBuffer<float> dryBuffer;
    };
}
