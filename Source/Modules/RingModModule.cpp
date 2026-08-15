#include "RingModModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    namespace
    {
        // Classic 2-branch cascaded-allpass Hilbert transformer coefficients (4 first-order
        // stages per branch) -- widely published (Bernie Hutchins/Electronotes derivation),
        // tuned for good ~90 degree phase separation between the two branches across roughly
        // 20Hz-20kHz at a ~44.1kHz sample rate. Not re-derived per actual sample rate (that's a
        // much more involved filter-design problem) -- phase accuracy degrades gracefully away
        // from 44.1/48kHz rather than breaking outright, which is an acceptable tradeoff for a
        // creative effect rather than a measurement-grade shifter.
        constexpr std::array<float, 4> kBranchACoeffs { 0.6923877308f, 0.9360654322f, 0.9882295226f, 0.9987488453f };
        constexpr std::array<float, 4> kBranchBCoeffs { 0.4021921162f, 0.8561710882f, 0.9722909545f, 0.9952584198f };
    }

    RingModModule::RingModModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          modeParam      (apvtsIn.getRawParameterValue (ringModParamId (slotIndexIn, RingModParam::mode))),
          frequencyParam (apvtsIn.getRawParameterValue (ringModParamId (slotIndexIn, RingModParam::frequency))),
          fineParam      (apvtsIn.getRawParameterValue (ringModParamId (slotIndexIn, RingModParam::fine))),
          mixParam       (apvtsIn.getRawParameterValue (ringModParamId (slotIndexIn, RingModParam::mix))),
          outputParam    (apvtsIn.getRawParameterValue (ringModParamId (slotIndexIn, RingModParam::output)))
    {
    }

    void RingModModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        carrierPhase = 0.0;

        for (auto& channel : hilbert)
        {
            for (int s = 0; s < 4; ++s)
            {
                channel.branchA[(size_t) s].setCoefficient (kBranchACoeffs[(size_t) s]);
                channel.branchB[(size_t) s].setCoefficient (kBranchBCoeffs[(size_t) s]);
            }
            channel.reset();
        }

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void RingModModule::reset()
    {
        carrierPhase = 0.0;
        for (auto& channel : hilbert)
            channel.reset();
    }

    void RingModModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const auto numChannels = block.getNumChannels();
        const auto numSamples = block.getNumSamples();

        const int mode = (int) modeParam->load(); // 0 = Ring Mod, 1 = Freq Shift

        const float freqOffset = modMatrix.getOffsetForParam (ringModParamId (slotIndex, RingModParam::frequency), 500.0f);
        const float fineOffset = modMatrix.getOffsetForParam (ringModParamId (slotIndex, RingModParam::fine), 10.0f);
        const float carrierHz = juce::jlimit (-2000.0f, 2000.0f, frequencyParam->load() + freqOffset)
                               + juce::jlimit (-20.0f, 20.0f, fineParam->load() + fineOffset);

        const float mixOffset = modMatrix.getOffsetForParam (ringModParamId (slotIndex, RingModParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (ringModParamId (slotIndex, RingModParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        const double phaseInc = (double) carrierHz / sampleRate;
        double phase = carrierPhase;

        for (size_t i = 0; i < numSamples; ++i)
        {
            const float carrierSin = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
            const float carrierCos = (float) std::cos (2.0 * juce::MathConstants<double>::pi * phase);

            for (size_t ch = 0; ch < numChannels && ch < (size_t) kMaxChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                const float dry = data[i];

                if (mode == 0)
                {
                    data[i] = dry * carrierSin;
                }
                else
                {
                    float real = 0.0f, imag = 0.0f;
                    hilbert[ch].process (dry, real, imag);
                    // Single-sideband shift -- the sign baked into phaseInc (from carrierHz,
                    // which can be negative) picks the shift direction automatically since
                    // cos is even and sin is odd, no separate up/down branch needed.
                    data[i] = real * carrierCos - imag * carrierSin;
                }
            }

            phase += phaseInc;
            phase -= std::floor (phase);
        }

        carrierPhase = phase;

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = (data[i] * mix + dry[i] * (1.0f - mix)) * outputGain;
        }
    }
}
