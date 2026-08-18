#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // True for the 3 band types whose Gain knob does anything (Bell/Low Shelf/High Shelf) --
    // High Pass/Low Pass/Notch/Band Pass have no meaningful gain, matching how Eq8CurveEditor's
    // drag gesture ignores vertical movement for those types (see that file's own comment). Free
    // function (not a Eq8Module member) since the GUI needs the exact same answer independent of
    // any live module instance -- see Eq8CurveEditor's magnitude-response computation, which reads
    // APVTS params directly rather than reaching into Eq8Module's internal state.
    inline bool eq8BandTypeHasGain (int type) { return type == 0 || type == 1 || type == 2; }

    // A draggable graphic EQ, 8 bands each independently Frequency/Q/Type/Gain/Enabled -- see
    // Identifiers.h's Eq8Param comment for the parameter scheme and Eq8CurveEditor for the GUI
    // that drives it. Cascaded IIR filters, one chain per channel, coefficients rebuilt every
    // block from live params (matches every other filter-ish module here, e.g. FilterModule).
    class Eq8Module : public RackModule
    {
    public:
        Eq8Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        // Public/static/pure (no instance state) so Eq8CurveEditor can compute the exact same
        // per-band coefficients the DSP would, purely from live APVTS param values, for its
        // combined magnitude-response curve -- no cross-thread reach into a live Eq8Module needed.
        static juce::dsp::IIR::Coefficients<float>::Ptr makeCoefficients (
            int type, double sampleRate, float freq, float q, float gainLinear);

    private:
        static constexpr int kMaxEq8Channels = 2;

        int slotIndex;
        double sampleRate = 44100.0;

        std::array<std::atomic<float>*, kNumEq8Bands> gainParams {};
        std::array<std::atomic<float>*, kNumEq8Bands> freqParams {};
        std::array<std::atomic<float>*, kNumEq8Bands> qParams {};
        std::array<std::atomic<float>*, kNumEq8Bands> typeParams {};
        std::array<std::atomic<float>*, kNumEq8Bands> enabledParams {};
        std::atomic<float>* mixParam;
        std::atomic<float>* outputParam;

        // [channel][band] -- each channel gets its own independent filter chain.
        std::array<std::array<juce::dsp::IIR::Filter<float>, kNumEq8Bands>, kMaxEq8Channels> bandFilters;

        juce::AudioBuffer<float> dryBuffer;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Eq8Module)
    };
}
