#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    class GranularModule : public RackModule
    {
    public:
        GranularModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        static constexpr int kMaxChannels = 2;
        static constexpr int kMaxGrains = 64;
        static constexpr float kBufferSeconds = 2.0f;

        struct Grain
        {
            bool active = false;
            float readPos = 0.0f;
            float step = 1.0f;
            int age = 0;
            int length = 1;
            float gainL = 1.0f;
            float gainR = 1.0f;
        };

        void spawnGrain (int lengthSamples, float pitchRatio, float position01, float jitter01, float spread01);
        float readDelayBuffer (int channel, float readPos) const;
        static float grainEnvelope (float phase01);

        int slotIndex;
        double currentSampleRate = 44100.0;
        int bufferLength = 1;
        int writePos = 0;
        float spawnAccumulator = 0.0f;

        std::atomic<float>* sizeParam = nullptr;
        std::atomic<float>* densityParam = nullptr;
        std::atomic<float>* positionParam = nullptr;
        std::atomic<float>* jitterParam = nullptr;
        std::atomic<float>* pitchParam = nullptr;
        std::atomic<float>* spreadParam = nullptr;
        std::atomic<float>* feedbackParam = nullptr;
        std::atomic<float>* freezeParam = nullptr;
        std::atomic<float>* mixParam = nullptr;
        std::atomic<float>* outputParam = nullptr;

        juce::AudioBuffer<float> delayBuffer;
        juce::AudioBuffer<float> dryBuffer;
        std::array<Grain, kMaxGrains> grains;
        juce::Random random;
    };
}
