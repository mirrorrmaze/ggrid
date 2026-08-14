#pragma once

#include "../Rack/RackModule.h"
#include "../Rack/SharedServices.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Feedback delay: per-channel delay line, feedback with an optional tanh saturator in the
    // feedback path (so pushing feedback hard adds grit and self-limits instead of just
    // spiraling into a runaway buildup), and Low Cut/Hi Cut filters ALSO in the feedback path --
    // so each successive repeat gets a little darker/thinner, like a real tape/analog delay,
    // rather than every echo sounding identically full-band. Time and Feedback are both
    // modulatable via the MIDI mod matrix. Sync locks Time to the host tempo via a note division
    // instead of the free-running ms value; Ping-Pong cross-feeds L/R so echoes alternate
    // channels instead of each channel just feeding back into itself.
    class DelayModule : public RackModule
    {
    public:
        DelayModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        void updateFilterCoefficients (float lowCutHz, float hiCutHz);

        int slotIndex;
        SharedServices& services;

        std::atomic<float>* timeParam;
        std::atomic<float>* syncParam;
        std::atomic<float>* divisionParam;
        std::atomic<float>* feedbackParam;
        std::atomic<float>* saturationParam;
        std::atomic<float>* lowCutParam;
        std::atomic<float>* hiCutParam;
        std::atomic<float>* pingPongParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // Fixed at 2 channels -- same convention as WaveshaperModule/FilterModule.
        static constexpr int kMaxDelayChannels = 2;

        // Lagrange3rd (cubic) rather than the default Linear interpolation -- meaningfully less
        // high-frequency smearing on the fractional-sample reads that the smoothed delay-time
        // ramp constantly produces, at a small, entirely affordable CPU cost for one delay line.
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine[kMaxDelayChannels];

        // Feedback-path tone shaping -- see the class comment above for why this lives in the
        // feedback loop rather than being applied once to the dry-tap wet output.
        juce::dsp::IIR::Filter<float> lowCutFilter[kMaxDelayChannels], hiCutFilter[kMaxDelayChannels];
        float lastLowCutHz = -1.0f, lastHiCutHz = -1.0f;

        // Smoothed per-sample rather than jumped per-block: setDelay() moving the delay line's
        // read position abruptly (e.g. right after a knob turn or a mod-matrix-driven Time
        // change) reads a discontinuous chunk of the buffer and clicks. Ramping the target over
        // ~15ms makes rate changes glide instead.
        juce::SmoothedValue<float> smoothedDelaySamples;

        double currentSampleRate = 44100.0;
        juce::AudioBuffer<float> dryBuffer;
    };
}
