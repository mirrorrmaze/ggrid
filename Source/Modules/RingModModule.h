#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    // Ring Modulator (plain multiply by a sine carrier) and true Frequency Shifter (single-
    // sideband modulation via a Hilbert transform) in one module, switched by Mode -- they share
    // a carrier oscillator and Frequency/Fine/Mix/Output controls, and differ only in how the
    // carrier combines with the signal.
    //
    // Frequency Shift moves every partial by the same Hz amount rather than the same ratio (what
    // a pitch shifter does) -- harmonic content becomes inharmonic, which is the distinctive
    // "shifter" sound (Bode/Moog frequency shifters, etc.), not achievable with simple ring mod
    // alone. Implemented via the classic 2-branch cascaded-allpass Hilbert transformer (4
    // first-order allpass stages per branch, tuned so the two branches stay ~90 degrees apart in
    // phase across most of the audible band) rather than an FFT/convolution approach, so it's
    // zero-latency and cheap per-sample.
    class RingModModule : public RackModule
    {
    public:
        RingModModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        // One first-order allpass stage: y[n] = a*(x[n] - y[n-1]) + x[n-1].
        struct AllpassStage
        {
            float a = 0.0f;
            float x1 = 0.0f, y1 = 0.0f;

            void setCoefficient (float coeff) { a = coeff; }
            void reset() { x1 = 0.0f; y1 = 0.0f; }

            float processSample (float x)
            {
                const float y = a * (x - y1) + x1;
                x1 = x;
                y1 = y;
                return y;
            }
        };

        // 4-stage cascade per branch; per-channel since each channel's audio is independent.
        struct HilbertChannel
        {
            std::array<AllpassStage, 4> branchA, branchB; // ~90 degrees apart in phase, not "signal vs. its Hilbert transform" individually

            void reset()
            {
                for (auto& s : branchA) s.reset();
                for (auto& s : branchB) s.reset();
            }

            void process (float in, float& outReal, float& outImag)
            {
                float a = in;
                for (auto& s : branchA) a = s.processSample (a);
                outReal = a;

                float b = in;
                for (auto& s : branchB) b = s.processSample (b);
                outImag = b;
            }
        };

        static constexpr int kMaxChannels = 2;

        int slotIndex;

        std::atomic<float>* modeParam;
        std::atomic<float>* frequencyParam;
        std::atomic<float>* fineParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        double sampleRate = 44100.0;
        double carrierPhase = 0.0; // 0..1, shared across channels

        std::array<HilbertChannel, kMaxChannels> hilbert;
        juce::AudioBuffer<float> dryBuffer;
    };
}
