#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // A modulated delay line, switched between two related characters via Mode: Chorus (no
    // feedback path -- lush, doubling) or Flanger (feedback path active -- resonant, "jet plane"
    // comb sweep). One real DSP difference between the modes, not just a cosmetic label -- see
    // process(). Depth swings the delay time as a fraction of the Delay knob itself (not a fixed
    // ms range), so the same knob works proportionally whether Delay is dialed short (flange) or
    // long (chorus) rather than needing separate per-mode depth constants. Each channel's LFO
    // starts at a different phase (see reset()) for stereo width with no extra parameter needed.
    class ChorusModule : public RackModule
    {
    public:
        ChorusModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        static constexpr int kMaxChorusChannels = 2;

        int slotIndex;
        double currentSampleRate = 44100.0;

        std::atomic<float>* modeParam;
        std::atomic<float>* rateParam;
        std::atomic<float>* depthParam;
        std::atomic<float>* delayParam;
        std::atomic<float>* feedbackParam;
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>, kMaxChorusChannels> delayLine;
        std::array<double, kMaxChorusChannels> lfoPhase {};
    };
}
