#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // A simple 3-band tone EQ -- EQ 8's lighter sibling. Low Shelf, Mid Bell, High Shelf, each
    // just a gain knob, no per-band frequency/Q controls (matching EQ 8's own "gain only"
    // convention, just with 3 fixed bands at fixed types instead of 8 identical peaking ones).
    class Eq3Module : public RackModule
    {
    public:
        Eq3Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        static constexpr int kMaxEq3Channels = 2;

        // Classic bass/mid/treble tone-control spread -- Low and High are shelves (broad,
        // extremes-focused), Mid is a wider bell (Q well below EQ 8's bands, since one knob here
        // has to cover the whole middle of the spectrum rather than a narrow octave slice).
        static constexpr float kLowShelfFreq = 150.0f;
        static constexpr float kMidFreq = 1000.0f;
        static constexpr float kMidQ = 0.7f;
        static constexpr float kHighShelfFreq = 4000.0f;

        int slotIndex;
        double sampleRate = 44100.0;

        std::atomic<float>* lowParam;
        std::atomic<float>* midParam;
        std::atomic<float>* highParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        std::array<juce::dsp::IIR::Filter<float>, kMaxEq3Channels> lowFilters, midFilters, highFilters;

        juce::AudioBuffer<float> dryBuffer;
    };
}
