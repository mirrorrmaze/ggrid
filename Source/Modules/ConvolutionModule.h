#pragma once

#include "../Rack/RackModule.h"
#include "../Rack/SharedServices.h"
#include "../IR/IRReshapeWorker.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Convolution against a curated factory IR library (ported from MultibandConvolver --
    // D:\Claude Projects\MultibandConvolver\Source\DSP\BandChain.{h,cpp} -- see that file for the
    // original this is adapted from), rather than an arbitrary user-picked file: IR select ->
    // Tone (tilt EQ) -> Fade In/Fade Out/Stretch (reshape the stored IR itself) -> Mix -> Output.
    //
    // IR-affecting changes (irIndex/fadeIn/fadeOut/stretch) are debounced (~200ms) and the actual
    // reshape work -- disk read, resample, fade envelope -- runs on the shared IRReshapeWorker
    // background thread, never inline in process(). This matters: doing that work inline is
    // exactly what caused real CPU spikes/dropouts in MultibandConvolver while dragging Stretch.
    // Tone and Mix are cheap and apply immediately.
    class ConvolutionModule : public RackModule
    {
    public:
        ConvolutionModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        // For the GUI's IR waveform display. Copies into outBuffer and returns true only if the
        // loaded IR has changed since lastSeenGeneration (which the caller owns and passes back
        // in each time), so a polling Timer doesn't do needless work/repaints. outPreFadeOutLength
        // is outBuffer's length before Fade Out's truncation -- see IRProcessor::buildShapedIR's
        // comment for why the display needs this to show a cut instead of a stretch.
        bool copyDisplayBufferIfChanged (juce::AudioBuffer<float>& outBuffer, juce::int64& lastSeenGeneration,
                                          int& outPreFadeOutLength);

    private:
        void applyToneCoefficients (float tone);

        int slotIndex;
        SharedServices& services;

        std::atomic<float>* irIndexParam;
        std::atomic<float>* toneParam;
        std::atomic<float>* fadeInParam;
        std::atomic<float>* fadeOutParam;
        std::atomic<float>* stretchParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        juce::dsp::Convolution convolution;
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> toneLowShelf, toneHighShelf;

        juce::AudioBuffer<float> dryBuffer;
        double sampleRate = 44100.0;

        // Debounced reshape trigger -- see the class comment above.
        static constexpr int reloadDebounceSamplesAt48k = 48000 / 5; // ~200ms
        int pendingReloadCountdown = 0; // 0 = force a load on the very first block
        bool needsInitialLoad = true;
        int lastIrIndexForReload = -1;
        float lastFadeInForReload = -1.0f, lastFadeOutForReload = -1.0f, lastStretchForReload = -1.0f;
        float lastTone = 0.0f;

        // Guarded copy of the currently-loaded (shaped) IR, for the GUI waveform display -- the
        // buffer handed to juce::dsp::Convolution is moved-from and not readable back out, so
        // this is a deliberate second copy, updated only when a new IR is applied (rare, not
        // per-block).
        juce::SpinLock displayLock;
        juce::AudioBuffer<float> displayBuffer;
        int displayPreFadeOutLength = 0;
        juce::int64 displayGeneration = 0;
    };
}
