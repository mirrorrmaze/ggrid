#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Standard downward compressor wrapping juce::dsp::Compressor (threshold/ratio/attack/
    // release), plus a Makeup gain trim and a wet/dry Mix for parallel ("New York") compression.
    class DynamicsModule : public RackModule
    {
    public:
        DynamicsModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        std::atomic<float>* thresholdParam;
        std::atomic<float>* ratioParam;
        std::atomic<float>* attackParam;
        std::atomic<float>* releaseParam;
        std::atomic<float>* makeupParam;
        std::atomic<float>* mixParam;

        juce::dsp::Compressor<float> compressor;
        juce::AudioBuffer<float> dryBuffer;
    };
}
