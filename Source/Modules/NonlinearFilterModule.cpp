#include "NonlinearFilterModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    NonlinearFilterModule::NonlinearFilterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          frequencyParam (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::frequency))),
          resonanceParam (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::resonance))),
          driveParam     (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::drive))),
          morphParam     (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::morph))),
          modeParam      (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::mode))),
          distortionParam (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::distortion))),
          mixParam       (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::mix))),
          outputParam    (apvtsIn.getRawParameterValue (nonlinearFilterParamId (slotIndexIn, NonlinearFilterParam::output)))
    {
    }

    void NonlinearFilterModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        reset();
    }

    void NonlinearFilterModule::reset()
    {
        lowState.fill (0.0f);
        bandState.fill (0.0f);
    }

    float NonlinearFilterModule::shapeSample (float x, float morph, int distortion) const
    {
        const float soft = std::tanh (x);
        const float amount = juce::jlimit (0.0f, 1.0f, morph);

        switch (distortion)
        {
            case 1: // Hard Clip
            {
                const float clipped = juce::jlimit (-1.0f, 1.0f, x * (1.0f + amount));
                return soft + (clipped - soft) * (0.35f + amount * 0.65f);
            }

            case 2: // Sine Fold
                return std::sin (x * juce::MathConstants<float>::halfPi * (1.0f + amount * 2.5f));

            case 3: // Foldback
            {
                const float threshold = juce::jmap (amount, 1.35f, 0.45f);
                const float period = threshold * 4.0f;
                float folded = std::fmod (x + threshold, period);
                if (folded < 0.0f)
                    folded += period;
                folded = std::abs (folded - threshold * 2.0f) - threshold;
                return juce::jlimit (-1.25f, 1.25f, folded / juce::jmax (0.001f, threshold));
            }

            case 4: // Asymmetric
            {
                const float bias = amount * 1.4f;
                const float shaped = std::tanh (x * (1.0f + amount) + bias) - std::tanh (bias);
                return juce::jlimit (-1.25f, 1.25f, shaped * (1.0f + amount * 0.5f));
            }

            case 5: // Warm
            {
                const float rounded = std::tanh (x * (0.85f + amount * 0.75f));
                return rounded - 0.08f * amount * rounded * rounded * rounded;
            }

            case 6: // Saturated
            {
                const float pushed = std::tanh (x * (1.8f + amount * 3.2f));
                const float density = pushed - pushed * pushed * pushed * 0.08f;
                return juce::jlimit (-1.2f, 1.2f, density);
            }

            case 7: // Bias
            {
                const float bias = juce::jmap (amount, 0.12f, 1.1f);
                const float positive = std::tanh ((x + bias) * (1.0f + amount));
                const float centre = std::tanh (bias * (1.0f + amount));
                return juce::jlimit (-1.25f, 1.25f, (positive - centre) * (1.0f + amount * 0.35f));
            }

            case 8: // Clipped
            {
                const float ceiling = juce::jmap (amount, 1.0f, 0.55f);
                const float clipped = juce::jlimit (-ceiling, ceiling, x);
                return clipped / ceiling;
            }

            case 0: // Soft Clip
            default:
                return std::tanh (x * (1.0f + amount * 1.8f));
        }
    }

    void NonlinearFilterModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float freqOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::frequency), 3000.0f);
        const float resonanceOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::resonance), 50.0f);
        const float driveOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::drive), 18.0f);
        const float morphOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::morph), 50.0f);
        const float mixOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mix), 50.0f);
        const float outputOffset = modMatrix.getOffsetForParam (
            nonlinearFilterParamId (slotIndex, NonlinearFilterParam::output), 12.0f);

        const float cutoff = juce::jlimit (20.0f, (float) sampleRate * 0.45f, frequencyParam->load() + freqOffset);
        const float resonance = juce::jlimit (0.0f, 100.0f, resonanceParam->load() + resonanceOffset) / 100.0f;
        const float driveDb = juce::jlimit (0.0f, 36.0f, driveParam->load() + driveOffset);
        const float morph = juce::jlimit (0.0f, 100.0f, morphParam->load() + morphOffset) / 100.0f;
        const int mode = juce::jlimit (0, getNonlinearFilterModeChoices().size() - 1, (int) modeParam->load());
        const int distortion = juce::jlimit (0, getNonlinearFilterDistortionChoices().size() - 1, (int) distortionParam->load());
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const float f = 2.0f * std::sin (juce::MathConstants<float>::pi * cutoff / (float) sampleRate);
        const float damp = juce::jmap (resonance, 0.0f, 1.0f, 1.85f, 0.08f);
        const float driveGain = juce::Decibels::decibelsToGain (driveDb);
        const float makeup = 1.0f / juce::jmax (1.0f, std::sqrt (driveGain));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            float low = lowState[ch];
            float band = bandState[ch];

            for (size_t i = 0; i < numSamples; ++i)
            {
                const float input = shapeSample ((data[i] - band * resonance * 1.4f) * driveGain, morph, distortion) * makeup;
                low += f * band;
                const float high = input - low - damp * band;
                band += f * shapeSample (high, morph, distortion);
                const float notch = low + high;

                float wet = low;
                switch (mode)
                {
                    case 1: wet = high; break;
                    case 2: wet = band; break;
                    case 3: wet = notch; break;
                    default: break;
                }

                data[i] = wet;
            }

            lowState[ch] = juce::jlimit (-4.0f, 4.0f, low);
            bandState[ch] = juce::jlimit (-4.0f, 4.0f, band);
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = data[i] * outputGain * mix + dry[i] * (1.0f - mix);
        }
    }
}
