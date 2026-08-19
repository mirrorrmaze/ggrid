#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // One "Filter" rack module type covering four different DSP approaches, picked by the
    // Filter Type choice:
    //   - Low Pass / High Pass / Band Pass / Notch: standard biquads (juce::dsp::IIR), using
    //     Frequency + Resonance.
    //   - Comb (Feedback) / Comb (Feedforward) / Allpass (Diffusor): a per-channel delay line
    //     with Frequency mapped to pitch (delay = sampleRate / frequency) and Feedback
    //     controlling character. The Allpass Diffusor is a Schroeder allpass -- the classic
    //     reverb-diffusion building block -- standing in for a dedicated reverb algorithm until
    //     the convolution module lands.
    //   - Ladder Low Pass / Ladder High Pass: a 4-stage nonlinear-feedback ladder (Frequency +
    //     Resonance), the classic saturating/self-oscillating analog-ladder-filter character
    //     (Moog-style) that a clean biquad can't produce -- Resonance drives how hard the
    //     feedback path pushes into tanh() saturation rather than a clean Q. High Pass is
    //     derived from the same ladder core via input-minus-lowpass (a standard technique for
    //     getting a complementary tap out of one ladder state) rather than a second independent
    //     ladder, so both share identical resonant/saturating character.
    //   - Formant: three parallel resonant biquad peaks at vowel formant frequencies, blended by
    //     Frequency repurposed as a vowel-sweep position (A->E->I->O->U) rather than a literal
    //     cutoff; Resonance controls each peak's Q/sharpness.
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
        float processLadderSample (int channel, float x, bool highPass, float cutoffCoefficient, float resonanceAmount);
        void updateFormantCoefficients (float vowelPosition, float resonance);

        int slotIndex;

        std::atomic<float>* frequencyParam;
        std::atomic<float>* typeParam;
        std::atomic<float>* resonanceParam;
        std::atomic<float>* driveParam;
        std::atomic<float>* feedbackParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // Fixed at 2 channels -- same tradeoff as WaveshaperModule's oversampling: GGrid's
        // buses are mono/stereo, and this keeps per-channel DSP state simple to own directly
        // rather than through a dynamically-sized container.
        static constexpr int kMaxFilterChannels = 2;

        juce::dsp::IIR::Filter<float> biquad[kMaxFilterChannels];
        juce::dsp::DelayLine<float> delayLine[kMaxFilterChannels];

        // 4-stage ladder state (one running lowpass output per stage, per channel) -- see
        // processLadderSample.
        std::array<std::array<float, 4>, kMaxFilterChannels> ladderStage {};

        // 3 parallel resonant peaks per channel for the Formant type (first three vowel
        // formants), recomputed once per block from the vowel-sweep position -- see
        // updateFormantCoefficients.
        static constexpr int kNumFormants = 3;
        juce::dsp::IIR::Filter<float> formantBiquad[kMaxFilterChannels][kNumFormants];

        double currentSampleRate = 44100.0;
        juce::AudioBuffer<float> dryBuffer;
    };
}
