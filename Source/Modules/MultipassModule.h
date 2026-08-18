#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include "../GUI/SpectrumAnalyzer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // A 3-band (Low/Mid/High) LR4 crossover splitter -- see Identifiers.h's Multipass* comment
    // for how this differs from Multiband Convolution (no per-band effect, no recombination).
    // The split-stage DSP itself (2 cascaded matched lowpass/highpass LR4 stages) is identical to
    // MultibandConvolutionModule's own -- see that class's comment for where it was originally
    // ported from -- just without a BandVoice/convolution engine per band, and without ever
    // summing the bands back together: each band's post-split, post-Gain content is handed out
    // through its own output bus instead (see getNumOutputBuses/getOutputBusBuffer), GGrid's
    // first module to genuinely need more than one.
    class MultipassModule : public RackModule
    {
    public:
        MultipassModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        int getNumOutputBuses() const override { return kNumMultipassBands; }
        const juce::AudioBuffer<float>* getOutputBusBuffer (int busIndex) const override
        {
            if (busIndex < 0 || busIndex >= kNumMultipassBands) return nullptr;
            return &bandOutputBuffers[(size_t) busIndex];
        }

        // For CrossoverSplitBar's live spectrum display -- see that class's own comment. Fed the
        // raw pre-split incoming signal (see process()), same "analyze what's arriving, before
        // any processing" tap point as MultibandConvolver's own inputAnalyzer.
        SpectrumAnalyzer& getAnalyzer() { return analyzer; }

    private:
        // Mirrors MultibandConvolutionModule::SplitStage exactly (see that class for the full
        // LR4/smoothed-cutoff reasoning) -- duplicated rather than shared since the two modules'
        // surrounding process() logic diverges enough (no BandVoice, no recombination) that a
        // shared base would need to abstract over very little actual code.
        struct SplitStage
        {
            juce::dsp::LinkwitzRileyFilter<float> lowFilter, highFilter;
            juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedHz;
        };
        std::array<SplitStage, kNumMultipassBands - 1> splitStages;

        int slotIndex;

        std::atomic<float>* splitHz1Param;
        std::atomic<float>* splitHz2Param;
        std::array<std::atomic<float>*, kNumMultipassBands> gainParams {};

        // Post-Gain, per-band content -- what getOutputBusBuffer hands out. Sized/cleared each
        // block in process(), same lifecycle as GGridAudioProcessor's own nodeBuffers.
        std::array<juce::AudioBuffer<float>, kNumMultipassBands> bandOutputBuffers;
        juce::AudioBuffer<float> remaining, lowScratch;

        SpectrumAnalyzer analyzer;

        double sampleRate = 44100.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultipassModule)
    };
}
