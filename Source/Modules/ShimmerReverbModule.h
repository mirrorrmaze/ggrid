#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace GGrid
{
    class ShimmerReverbModule : public RackModule
    {
    public:
        ShimmerReverbModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        struct DelayTap
        {
            std::vector<float> buffer;
            int write = 0;
            float last = 0.0f;

            void prepare (int maxSamples);
            void reset();
            float read (float delaySamples) const;
            void push (float sample);
        };

        struct PitchShifter
        {
            std::vector<float> buffer;
            int write = 0;
            float phase = 0.0f;

            void prepare (int maxSamples);
            void reset();
            float process (float input, float semitones, bool reverse);
        };

        struct ToneState
        {
            float lowCut = 0.0f;
            float highCut = 0.0f;
        };

        float processChannel (int ch, float input, float size, float feedback, float diffusion,
                              float shift, int pitchMode, int color, float modDepth,
                              float lowCutHz, float highCutHz);
        float shapeTone (int ch, float sample, float lowCutHz, float highCutHz, int color);

        int slotIndex;
        double sampleRate = 44100.0;
        float lfoPhase = 0.0f;

        std::atomic<float>* sizeParam = nullptr;
        std::atomic<float>* feedbackParam = nullptr;
        std::atomic<float>* diffusionParam = nullptr;
        std::atomic<float>* shiftParam = nullptr;
        std::atomic<float>* pitchModeParam = nullptr;
        std::atomic<float>* colorParam = nullptr;
        std::atomic<float>* modRateParam = nullptr;
        std::atomic<float>* modDepthParam = nullptr;
        std::atomic<float>* lowCutParam = nullptr;
        std::atomic<float>* highCutParam = nullptr;
        std::atomic<float>* mixParam = nullptr;
        std::atomic<float>* outputParam = nullptr;

        static constexpr int kChannels = 2;
        static constexpr int kTaps = 4;
        std::array<std::array<DelayTap, kTaps>, kChannels> taps;
        std::array<std::array<DelayTap, 2>, kChannels> diffusers;
        std::array<PitchShifter, kChannels> pitchUp;
        std::array<PitchShifter, kChannels> pitchDown;
        std::array<ToneState, kChannels> tone;
        juce::AudioBuffer<float> dryBuffer;
    };
}
