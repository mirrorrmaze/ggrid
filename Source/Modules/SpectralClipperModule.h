#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <deque>
#include <vector>
#include <array>

namespace GGrid
{
    // Clips in the frequency domain instead of the time domain -- a streaming STFT (same
    // window/hop/overlap-add machinery as LossyModule) transforms each hop to its per-bin
    // magnitude/phase, Ceiling sets a magnitude cap relative to full scale, Shape decides how
    // magnitude beyond that cap gets brought back down (phase is always left untouched), and an
    // inverse transform reconstructs the signal. Because the nonlinearity acts on each frequency
    // bin independently rather than on the combined time-domain waveform, it doesn't generate the
    // same broadband harmonic series ordinary sample-domain clipping (Waveshaper's Hard/Soft
    // Clip) does -- this is the "spectral clipping" mastering engineers mean when they say it
    // sounds cleaner than a straight brickwall for a given amount of gain reduction, since the
    // correction is spread across frequency rather than concentrated at the waveform's peaks.
    //
    // Mix is spectral, not a sample-domain dry/wet blend -- unlike every module except Lossy.
    // Both wet and "dry" spectra come from the same window-delayed STFT, so mixing an undelayed
    // dry signal back in would comb-filter against it (see ChannelEngine::process's Mix blend,
    // identical reasoning to LossyModule's own).
    class SpectralClipperModule : public RackModule
    {
    public:
        SpectralClipperModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        // One mono STFT engine per channel -- independent, no cross-channel linking, matching
        // LossyModule::ChannelEngine.
        class ChannelEngine
        {
        public:
            void prepare (double sampleRateIn);
            void reset();

            void setDrive (float driveDb) { drive.store (driveDb); }
            void setCeiling (float ceilingDb) { ceiling.store (ceilingDb); }
            void setShape (int shapeIndexIn) { shapeIndex.store (shapeIndexIn); }
            void setMix (float mix01) { mix.store (juce::jlimit (0.0f, 1.0f, mix01)); }

            // in and out must not alias -- output lags input by the STFT window's latency.
            void process (const float* in, float* out, int numSamples);

        private:
            void processHop();
            float processOneSample (float inSample);

            // Brings a magnitude that's above ceilingLinear back down according to the selected
            // shape -- Hard clamps flat at the ceiling; the others fold the overshoot back into
            // range with progressively softer/more textured curves, mirroring the same shape
            // vocabulary Waveshaper/Nonlinear Filter already use elsewhere, just applied to a
            // per-bin magnitude instead of a raw sample.
            static float shapeMagnitude (float magnitude, float ceilingLinear, int shape);

            static constexpr int windowSize = 1024;
            static constexpr int hopSize = windowSize / 4;
            static constexpr int fftOrder = 10; // 2^10 = 1024

            double sampleRate = 44100.0;
            juce::dsp::FFT fft { fftOrder };
            std::vector<float> hannWindow;

            // Overlap-add reconstruction gain and a reference scale for what "full-scale"
            // magnitude means for this window -- see LossyModule::ChannelEngine's identical
            // fields for the full derivation.
            float olaNormalisation = 1.0f;
            float magnitudeScale = 1.0f;

            std::vector<float> inputRing;
            int inputWritePos = 0;
            int samplesSinceHop = 0;

            // Cached once per process() call (block-rate, not per-sample) from the drive atomic --
            // matches how every other module here updates its resolved parameters once per block
            // rather than re-reading atomics in a per-sample hot loop.
            float currentDriveGain = 1.0f;

            std::vector<float> fftScratch;
            std::vector<float> accum;
            std::deque<float> outputFifo;

            std::atomic<float> drive { 0.0f };
            std::atomic<float> ceiling { 0.0f };
            std::atomic<int> shapeIndex { 0 };
            std::atomic<float> mix { 1.0f };
        };

        static constexpr int kMaxSpectralClipperChannels = 2;

        int slotIndex;

        std::atomic<float>* driveParam;
        std::atomic<float>* ceilingParam;
        std::atomic<float>* shapeParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        std::array<ChannelEngine, kMaxSpectralClipperChannels> channels;

        // Required as the STFT's non-aliasing "in" buffer (ChannelEngine::process forbids
        // in == out), doubling as the usual dry-copy scratch every other module also keeps.
        juce::AudioBuffer<float> dryBuffer;
    };
}
