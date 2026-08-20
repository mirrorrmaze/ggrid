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

    double MackityModule::processChannel (int channel, double input, double inTrim, double smash, double outPad)
    {
        const double overallScale = sampleRate / 44100.0;
        const double iirAmountA = 0.001860867 / overallScale;
        const double iirAmountB = 0.000287496 / overallScale;

        auto& hpA = iirA[(size_t) channel];
        hpA = hpA * (1.0 - iirAmountA) + input * iirAmountA;
        input -= hpA;
        input *= inTrim;

        input = processBiquad (biquadA, channel, input);

        smash = juce::jlimit (0.0, 1.0, smash);
        const double limited = juce::jlimit (-12.0, 12.0, input);
        const double bridged = std::sin (juce::jlimit (-juce::MathConstants<double>::halfPi,
                                                       juce::MathConstants<double>::halfPi,
                                                       limited * (0.65 + smash * 1.85)));
        const double chewed = std::tanh (limited * (1.2 + smash * 5.5));
        const double folded = std::sin (limited * (0.35 + smash * 2.4)) * (0.25 + smash * 0.75);
        const double asymmetry = std::tanh ((limited + limited * limited * 0.18 * smash) * (0.8 + smash * 2.2));
        const double shrapnel = std::sin (limited * (4.0 + smash * 18.0))
                              * std::sin (limited * (0.5 + smash * 0.7))
                              * smash;
        input = bridged * (0.48 - smash * 0.12)
              + chewed * (0.28 + smash * 0.22)
              + folded * (smash * 0.22)
              + asymmetry * (0.24 + smash * 0.08)
              + shrapnel * (0.05 + smash * 0.55);

        input -= std::pow (juce::jlimit (-1.6, 1.6, input), 5.0) * (0.12 + smash * 0.18);
        input = std::tanh (input * (1.0 + smash * 1.55)) * (1.0 + smash * 0.95);

        input = processBiquad (biquadB, channel, input);

        const double edge = juce::jlimit (-1.0, 1.0, limited * (0.32 + smash * 0.9));
        const double hardEdge = edge - std::pow (edge, 3.0) * 0.18 + std::pow (edge, 5.0) * 0.08;
        input += hardEdge * smash * 0.85;
        input = juce::jlimit (-3.2, 3.2, input);

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
        const double inTrim = std::pow (1.0 + input01 * 15.0, 2.0);
        const double smash = std::pow (input01, 3.0);
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
                const double wet = processChannel ((int) ch, (double) data[i], inTrim, smash, outPad);
                data[i] = (float) (wet * outputGain * mix + dry[i] * (1.0f - mix));
            }
        }
    }
}
