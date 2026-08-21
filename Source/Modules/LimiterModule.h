#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace GGrid
{
    // A brickwall peak limiter: Gain (input trim, drives harder into the ceiling), Ceiling
    // (the hard output cap), and Release -- the same three core knobs Ableton Live's Limiter
    // exposes. Wraps juce::dsp::Limiter (fixed fast attack with a small built-in lookahead, no
    // separate Attack knob to expose) rather than reusing CompressorModule's hand-rolled
    // soft-knee/Peak-RMS engine -- a limiter's whole job is a hard, always-brickwall ceiling, not
    // a musically-configurable knee/ratio, so the two modules deliberately don't share DSP.
    class LimiterModule : public RackModule
    {
    public:
        LimiterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        int slotIndex;

        std::atomic<float>* gainParam;
        std::atomic<float>* ceilingParam;
        std::atomic<float>* releaseParam;

        juce::dsp::Limiter<float> limiter;
    };
}
