#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <deque>
#include <vector>
#include <array>

namespace GGrid
{
    // A spectral, "codec-style" lo-fi degradation effect, ported near-verbatim from SPANDEX's
    // LossyProcessor (itself modeled after Goodhertz's Lossy). Not a time-domain bitcrusher --
    // a streaming STFT that quantizes the magnitude spectrum and randomizes per-bin phase,
    // refreshing that quantized/jittered frame at a controllable rate: held (low rate) for a
    // smeared, underwater texture, or refreshed almost every hop (high rate) for a garbled,
    // glitchy one. Phase jitter specifically is what gives this a "bad cellphone codec"
    // character a purely time-domain bitcrusher can't produce -- real low-bitrate speech codecs
    // lose phase coherence between frames in exactly this way.
    //
    // Mix is spectral, not a sample-domain dry/wet blend -- unlike every other module here.
    // Both wet and "dry" spectra come from the same window-delayed STFT, so mixing an undelayed
    // dry signal back in would comb-filter against it (see ChannelEngine::process's Mix blend).
    // Output is a plain post-gain trim, applied after the STFT round-trip.
    class LossyModule : public RackModule
    {
    public:
        LossyModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        // One mono STFT engine per channel -- independent, no cross-channel linking, matching
        // how SPANDEX itself runs a separate instance per channel.
        class ChannelEngine
        {
        public:
            void prepare (double sampleRateIn);
            void reset();

            void setBits (float bitsIn) { bits.store (juce::jlimit (1.0f, 16.0f, bitsIn)); }
            void setRefreshHz (float hz) { refreshHz.store (juce::jlimit (1.0f, 200.0f, hz)); }
            void setJitter (float jitter01) { jitter.store (juce::jlimit (0.0f, 1.0f, jitter01)); }
            void setMix (float mix01) { mix.store (juce::jlimit (0.0f, 1.0f, mix01)); }

            // in and out must not alias -- output lags input by the STFT window's latency.
            void process (const float* in, float* out, int numSamples);

        private:
            void processHop();
            float processOneSample (float inSample);

            static constexpr int windowSize = 1024;
            static constexpr int hopSize = windowSize / 4;
            static constexpr int fftOrder = 10; // 2^10 = 1024

            double sampleRate = 44100.0;
            juce::dsp::FFT fft { fftOrder };
            std::vector<float> hannWindow;

            // Overlap-add reconstruction gain (steady-state trick: sum the squared window across
            // 8 overlapping hops and read the settled middle value) and a separate reference
            // scale for what "full-scale" magnitude means for this window, so the Bits control
            // quantizes something perceptually meaningful rather than FFT's unnormalized raw
            // magnitude units.
            float olaNormalisation = 1.0f;
            float magnitudeScale = 1.0f;

            std::vector<float> inputRing;
            int inputWritePos = 0;
            int samplesSinceHop = 0;

            std::vector<float> fftScratch;
            // Only the quantized+jittered "wet" spectrum is held/refreshed at the configured
            // rate -- the dry side of the Mix blend always uses the current hop's actual
            // spectrum, so mix=0 stays a true bypass no matter how slow refreshHz is set.
            std::vector<float> heldWetRe, heldWetIm;
            std::vector<float> accum;
            std::deque<float> outputFifo;

            int hopsSinceRefresh = 0;
            juce::Random random;

            std::atomic<float> bits { 8.0f };
            std::atomic<float> refreshHz { 40.0f };
            std::atomic<float> jitter { 0.3f };
            std::atomic<float> mix { 1.0f };
        };

        static constexpr int kMaxLossyChannels = 2;

        int slotIndex;

        std::atomic<float>* bitsParam;
        std::atomic<float>* rateParam;
        std::atomic<float>* jitterParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        std::array<ChannelEngine, kMaxLossyChannels> channels;

        // Required as the STFT's non-aliasing "in" buffer (ChannelEngine::process forbids
        // in == out), doubling as the usual dry-copy scratch every other module also keeps.
        juce::AudioBuffer<float> dryBuffer;
    };
}
