#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    // A feed-forward downward compressor modeled on Ableton Live's Compressor: Threshold/Ratio/
    // Attack/Release, a soft Knee, a Peak/RMS detection switch, Makeup gain, and a wet/dry Mix
    // for parallel ("New York") compression. Deliberately NOT built on juce::dsp::Compressor --
    // that class hard-codes its knee and detector, and Knee/Detection are exactly the two things
    // that need to be configurable here, so this hand-rolls the classic decoupled/soft-knee
    // topology instead (Giannoulis/Massberg/Reiss, "Digital Dynamic Range Compressor Design"):
    // a static gain-computer curve (quadratic-interpolated through the knee region) evaluated
    // every sample from the detected level, then smoothed toward that target with separate
    // attack/release one-pole ballistics -- attack while the target is MORE negative (louder,
    // gain reducing further) than the current smoothed value, release while it's less. No
    // external sidechain input -- GGrid's graph has no secondary-input concept yet, so detection
    // is always this module's own signal.
    class CompressorModule : public RackModule
    {
    public:
        CompressorModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        // Static (unsmoothed) gain-reduction curve in dB, <= 0, for a given detected input level
        // in dB -- the standard soft-knee quadratic-interpolation formula, degenerating to a
        // sharp corner at Knee = 0.
        static float staticCharacteristic (float inputDb, float thresholdDb, float ratio, float kneeDb);

        static constexpr int kMaxCompressorChannels = 2;

        int slotIndex;

        std::atomic<float>* thresholdParam;
        std::atomic<float>* ratioParam;
        std::atomic<float>* attackParam;
        std::atomic<float>* releaseParam;
        std::atomic<float>* kneeParam;
        std::atomic<float>* makeupParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* detectionParam; // 0 = Peak, 1 = RMS

        double sampleRate = 44100.0;

        struct ChannelState
        {
            // One-pole-smoothed x^2 for the RMS detector (~5ms time constant) -- Peak mode reads
            // the instantaneous rectified sample directly and ignores this.
            float rmsSquared = 0.0f;
            // Currently-applied gain reduction in dB (<= 0), after attack/release ballistics --
            // this, not the raw static-characteristic output, is what actually gets applied,
            // giving the compressor its time-based behaviour rather than snapping instantly.
            float gainReductionDb = 0.0f;
        };
        std::array<ChannelState, kMaxCompressorChannels> channels;

        juce::AudioBuffer<float> dryBuffer;
    };
}
