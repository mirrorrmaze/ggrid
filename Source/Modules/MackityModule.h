#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    class MackityModule : public RackModule
    {
    public:
        MackityModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        struct Biquad
        {
            double a0 = 1.0, a1 = 0.0, a2 = 0.0, b1 = 0.0, b2 = 0.0;
            std::array<double, 2> x1 {}, x2 {}, y1 {}, y2 {};
        };

        void updateBiquad (Biquad& b, double cutoffHz, double q);
        double processBiquad (Biquad& b, int channel, double input);
        double processChannel (int channel, double input, double inTrim, double smash, double outPad);

        int slotIndex;
        double sampleRate = 44100.0;

        std::atomic<float>* inputParam = nullptr;
        std::atomic<float>* padParam = nullptr;
        std::atomic<float>* mixParam = nullptr;
        std::atomic<float>* outputParam = nullptr;

        Biquad biquadA, biquadB;
        std::array<double, 2> iirA {}, iirB {};
        juce::AudioBuffer<float> dryBuffer;
    };
}
