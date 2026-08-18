#pragma once

#include "../Rack/RackModule.h"
#include "../Rack/SharedServices.h"
#include "../IR/IRReshapeWorker.h"
#include "../GUI/SpectrumAnalyzer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>

namespace GGrid
{
    // A fixed 3-band (Low/Mid/High) multiband convolution -- ported from MultibandConvolver
    // (D:\Claude Projects\MultibandConvolver\Source\DSP\CrossoverSplitter.{h,cpp}/BandChain.
    // {h,cpp}), scoped down from that project's up-to-8-band, 12-macro-per-band desktop UI to a
    // fixed 3 bands with a leaner per-band knob set so it fits GGrid's compact node-panel format
    // (see the class comment on ConvolutionModule for the per-band macros this mirrors).
    //
    // Topology: an LR4 (Linkwitz-Riley, 24 dB/oct) crossover -- 2 cascaded split stages, each a
    // matched lowpass/highpass pair -- splits the signal into 3 contiguous bands at 2 draggable
    // split points (dragged on-canvas via CrossoverSplitBar, not a knob). Each band then runs an
    // independent copy of ConvolutionModule's own engine (IR select -> Tone tilt EQ -> Fade In/
    // Fade Out/Stretch IR reshape -> Mix -> Output), and the 3 bands sum back together at the
    // output. IR-affecting changes are debounced per band and the actual reshape work runs on the
    // shared IRReshapeWorker background thread, identical to ConvolutionModule -- see that
    // class's comment for why this matters (inline reshaping is what caused real CPU spikes in
    // the original MultibandConvolver).
    class MultibandConvolutionModule : public RackModule
    {
    public:
        MultibandConvolutionModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        // For CrossoverSplitBar's live spectrum display -- see that class's own comment.
        SpectrumAnalyzer& getAnalyzer() { return analyzer; }

    private:
        // One independent convolution engine per band -- mirrors ConvolutionModule's own member
        // set closely (see that class), minus the waveform-display buffer (no per-band waveform
        // in this module, see MultibandConvolutionControlsPanel).
        struct BandVoice
        {
            explicit BandVoice (juce::dsp::ConvolutionMessageQueue& queue) : convolution (queue) {}

            std::atomic<float>* irIndexParam = nullptr;
            std::atomic<float>* toneParam = nullptr;
            std::atomic<float>* fadeInParam = nullptr;
            std::atomic<float>* fadeOutParam = nullptr;
            std::atomic<float>* stretchParam = nullptr;
            std::atomic<float>* mixParam = nullptr;
            std::atomic<float>* outputParam = nullptr;

            juce::dsp::Convolution convolution;
            juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowShelf, toneHighShelf;

            double sampleRate = 44100.0;
            int pendingReloadCountdown = 0; // 0 = force a load on the very first block
            bool needsInitialLoad = true;
            int lastIrIndexForReload = -1;
            float lastFadeInForReload = -1.0f, lastFadeOutForReload = -1.0f, lastStretchForReload = -1.0f;
            float lastTone = 0.0f;
        };

        void applyToneCoefficients (BandVoice& voice, float tone);
        void processBand (BandVoice& voice, int bandIndex, juce::AudioBuffer<float>& buffer,
                           const ModulationMatrix& modMatrix, int numSamples);

        int slotIndex;
        SharedServices& services;

        std::atomic<float>* splitHz1Param;
        std::atomic<float>* splitHz2Param;

        std::array<std::unique_ptr<BandVoice>, kNumConvolutionBands> bands;

        // LR4 crossover -- kNumConvolutionBands - 1 (2) split stages, each producing one band's
        // lowpass output plus a highpass "remaining" signal fed into the next stage. See
        // MultibandConvolver's CrossoverSplitter for the original, generalized-band-count version
        // this is a fixed-3-band unrolling of.
        struct SplitStage
        {
            juce::dsp::LinkwitzRileyFilter<float> lowFilter, highFilter;
            juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedHz;
        };
        std::array<SplitStage, kNumConvolutionBands - 1> splitStages;

        std::array<juce::AudioBuffer<float>, kNumConvolutionBands> bandBuffers;
        juce::AudioBuffer<float> remaining, lowScratch;

        SpectrumAnalyzer analyzer;

        double sampleRate = 44100.0;

        static constexpr int reloadDebounceSamplesAt48k = 48000 / 5; // ~200ms, see ConvolutionModule
    };
}
