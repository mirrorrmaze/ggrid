#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    class NonlinearFilterModule : public RackModule
    {
    public:
        NonlinearFilterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        static constexpr int kMaxChannels = 2;

        float shapeSample (float x, float morph, int distortion) const;

        int slotIndex;
        double sampleRate = 44100.0;

        std::atomic<float>* frequencyParam = nullptr;
        std::atomic<float>* resonanceParam = nullptr;
        std::atomic<float>* driveParam = nullptr;
        std::atomic<float>* morphParam = nullptr;
        std::atomic<float>* modeParam = nullptr;
        std::atomic<float>* distortionParam = nullptr;
        std::atomic<float>* mixParam = nullptr;
        std::atomic<float>* outputParam = nullptr;

        std::array<float, kMaxChannels> lowState {};
        std::array<float, kMaxChannels> bandState {};
        juce::AudioBuffer<float> dryBuffer;
    };
}
