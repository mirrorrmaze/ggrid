#include "MackityModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    MackityModule::MackityModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          inputParam  (apvtsIn.getRawParameterValue (mackityParamId (slotIndexIn, MackityParam::input))),
          padParam    (apvtsIn.getRawParameterValue (mackityParamId (slotIndexIn, MackityParam::pad))),
          mixParam    (apvtsIn.getRawParameterValue (mackityParamId (slotIndexIn, MackityParam::mix))),
          outputParam (apvtsIn.getRawParameterValue (mackityParamId (slotIndexIn, MackityParam::output)))
    {
    }

    void MackityModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        updateBiquad (biquadA, 19160.0, 0.431684981684982);
        updateBiquad (biquadB, 19160.0, 1.1582298);
        reset();
    }

    void MackityModule::reset()
    {
        biquadA.x1.fill (0.0); biquadA.x2.fill (0.0); biquadA.y1.fill (0.0); biquadA.y2.fill (0.0);
        biquadB.x1.fill (0.0); biquadB.x2.fill (0.0); biquadB.y1.fill (0.0); biquadB.y2.fill (0.0);
        iirA.fill (0.0);
        iirB.fill (0.0);
    }

    void MackityModule::updateBiquad (Biquad& b, double cutoffHz, double q)
    {
        const double clippedCutoff = juce::jlimit (20.0, sampleRate * 0.45, cutoffHz);
        const double k = std::tan (juce::MathConstants<double>::pi * clippedCutoff / sampleRate);
        const double norm = 1.0 / (1.0 + k / q + k * k);

        b.a0 = k * k * norm;
        b.a1 = 2.0 * b.a0;
        b.a2 = b.a0;
        b.b1 = 2.0 * (k * k - 1.0) * norm;
        b.b2 = (1.0 - k / q + k * k) * norm;
    }

    double MackityModule::processBiquad (Biquad& b, int channel, double input)
    {
        const double output = b.a0 * input + b.a1 * b.x1[(size_t) channel] + b.a2 * b.x2[(size_t) channel]
                            - b.b1 * b.y1[(size_t) channel] - b.b2 * b.y2[(size_t) channel];

        b.x2[(size_t) channel] = b.x1[(size_t) channel];
        b.x1[(size_t) channel] = input;
        b.y2[(size_t) channel] = b.y1[(size_t) channel];
        b.y1[(size_t) channel] = output;
        return output;
    }

    double MackityModule::processChannel (int channel, double input, double inTrim, double outPad)
    {
        const double overallScale = sampleRate / 44100.0;
        const double iirAmountA = 0.001860867 / overallScale;
        const double iirAmountB = 0.000287496 / overallScale;

        auto& hpA = iirA[(size_t) channel];
        hpA = hpA * (1.0 - iirAmountA) + input * iirAmountA;
        input -= hpA;
        input *= inTrim;

        input = processBiquad (biquadA, channel, input);

        input = juce::jlimit (-1.0, 1.0, input);
        input -= std::pow (input, 5.0) * 0.1768;

        input = processBiquad (biquadB, channel, input);

        auto& hpB = iirB[(size_t) channel];
        hpB = hpB * (1.0 - iirAmountB) + input * iirAmountB;
        input -= hpB;

        return input * outPad;
    }

    void MackityModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        updateBiquad (biquadA, 19160.0, 0.431684981684982);
        updateBiquad (biquadB, 19160.0, 1.1582298);

        const float inputOffset = modMatrix.getOffsetForParam (mackityParamId (slotIndex, MackityParam::input), 50.0f);
        const float padOffset = modMatrix.getOffsetForParam (mackityParamId (slotIndex, MackityParam::pad), 50.0f);
        const float mixOffset = modMatrix.getOffsetForParam (mackityParamId (slotIndex, MackityParam::mix), 50.0f);
        const float outputOffset = modMatrix.getOffsetForParam (mackityParamId (slotIndex, MackityParam::output), 12.0f);

        const double input01 = juce::jlimit (0.0f, 100.0f, inputParam->load() + inputOffset) / 100.0;
        double inTrim = input01 * 10.0;
        inTrim *= inTrim;
        const double outPad = juce::jlimit (0.0f, 100.0f, padParam->load() + padOffset) / 100.0;
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) 2);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);

            for (size_t i = 0; i < numSamples; ++i)
            {
                const double wet = processChannel ((int) ch, (double) data[i], inTrim, outPad);
                data[i] = (float) (wet * outputGain * mix + dry[i] * (1.0f - mix));
            }
        }
    }
}
