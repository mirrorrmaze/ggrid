#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Waveshaper/wavefolder module: drives the signal into one of several shaping curves
    // (hard clip, soft clip, foldback wavefolder, sine fold, rectify/asymmetric), with
    // optional 2x/4x oversampling to tame aliasing from the harder shapes.
    class WaveshaperModule : public RackModule
    {
    public:
        WaveshaperModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        float shapeSample (float x, int shapeIndex, float symmetry, float foldAmount) const;
        void shapeBlockInPlace (juce::dsp::AudioBlock<float>& block, int shapeIndex, float driveGain, float symmetry, float foldAmount);

        std::atomic<float>* driveParam;
        std::atomic<float>* shapeParam;
        std::atomic<float>* symmetryParam;
        std::atomic<float>* foldParam;
        std::atomic<float>* oversampleParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // Fixed at 2 channels: GGrid's buses are mono/stereo FX buses, and JUCE's Oversampling
        // requires its channel count to be fixed at construction. Oversampling is skipped
        // (falls back to non-oversampled shaping) for any block wider than that.
        static constexpr int kMaxOversamplingChannels = 2;
        juce::dsp::Oversampling<float> oversampling2x { kMaxOversamplingChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
        juce::dsp::Oversampling<float> oversampling4x { kMaxOversamplingChannels, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };

        juce::AudioBuffer<float> dryBuffer;
    };
}
