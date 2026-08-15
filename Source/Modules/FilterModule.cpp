#include "FilterModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    namespace
    {
        // Filter Type indices, matching getFilterTypeChoices() order.
        constexpr int typeLowPass = 0;
        constexpr int typeHighPass = 1;
        constexpr int typeBandPass = 2;
        constexpr int typeNotch = 3;
        constexpr int typeCombFeedback = 4;
        constexpr int typeCombFeedforward = 5;
        constexpr int typeAllpassDiffusor = 6;

        bool isBiquadType (int type) { return type <= typeNotch; }
    }

    FilterModule::FilterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          frequencyParam (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::frequency))),
          typeParam      (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::type))),
          resonanceParam (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::resonance))),
          feedbackParam  (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::feedback))),
          mixParam       (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::mix))),
          outputParam    (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::output)))
    {
    }

    void FilterModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        // Supports pitches down to 1 Hz -- comfortably below our 20 Hz parameter floor -- at
        // whatever sample rate the host is running.
        const int maxDelaySamples = juce::jmax (64, (int) std::ceil (spec.sampleRate));

        for (auto& line : delayLine)
        {
            line.setMaximumDelayInSamples (maxDelaySamples);
            line.prepare (monoSpec);
        }

        for (auto& b : biquad)
            b.prepare (monoSpec);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        updateBiquadCoefficients (frequencyParam->load(), resonanceParam->load());
    }

    void FilterModule::reset()
    {
        for (auto& line : delayLine)
            line.reset();

        for (auto& b : biquad)
            b.reset();
    }

    void FilterModule::updateBiquadCoefficients (float frequency, float resonance)
    {
        const int type = (int) typeParam->load();
        if (! isBiquadType (type))
            return;

        const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), frequency);
        const float q = juce::jlimit (0.1f, 20.0f, resonance);

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
        switch (type)
        {
            case typeLowPass:  coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass  (currentSampleRate, freq, q); break;
            case typeHighPass: coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, freq, q); break;
            case typeBandPass: coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, freq, q); break;
            case typeNotch:    coeffs = juce::dsp::IIR::Coefficients<float>::makeNotch    (currentSampleRate, freq, q); break;
            default: return;
        }

        for (auto& b : biquad)
            *b.coefficients = *coeffs;
    }

    float FilterModule::processDelayBasedSample (int channel, float x, int type, float feedback)
    {
        auto& line = delayLine[(size_t) channel];

        if (type == typeCombFeedback)
        {
            // v[n] = x[n] + fb * v[n-D]; output is the delayed tap v[n-D], which already carries
            // the accumulated echo history. Dry/wet blending happens later via the Mix param.
            const float delayed = line.popSample (0);
            line.pushSample (0, x + feedback * delayed);
            return delayed;
        }

        if (type == typeCombFeedforward)
        {
            // Pure FIR comb: the delay line only ever stores raw input, so this is unconditionally
            // stable even at the parameter's extremes.
            const float delayed = line.popSample (0);
            line.pushSample (0, x);
            return x + feedback * delayed;
        }

        // typeAllpassDiffusor -- classic Schroeder allpass:
        //   w[n] = x[n] + g*w[n-D]
        //   y[n] = -g*x[n] + w[n-D]
        const float wDelayed = line.popSample (0);
        const float w = x + feedback * wDelayed;
        line.pushSample (0, w);
        return (-feedback * x) + wDelayed;
    }

    void FilterModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int type = (int) typeParam->load();

        const float mixOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const float freqOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::filterFrequency))
                                + modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::frequency), 3000.0f);
        const float feedbackOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::filterFeedback))
                                    + modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::feedback), 0.9f);
        const float resonanceOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::resonance), 3.0f);

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxFilterChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        if (isBiquadType (type))
        {
            const float resonance = juce::jlimit (0.1f, 20.0f, resonanceParam->load() + resonanceOffset);
            updateBiquadCoefficients (frequencyParam->load() + freqOffset, resonance);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                for (size_t i = 0; i < numSamples; ++i)
                    data[i] = biquad[ch].processSample (data[i]);
            }
        }
        else
        {
            const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), frequencyParam->load() + freqOffset);
            const float delaySamples = juce::jlimit (1.0f, (float) (currentSampleRate - 1.0), (float) (currentSampleRate / freq));
            const float feedback = juce::jlimit (-0.95f, 0.95f, feedbackParam->load() + feedbackOffset);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                delayLine[ch].setDelay (delaySamples);
                auto* data = block.getChannelPointer (ch);

                for (size_t i = 0; i < numSamples; ++i)
                    data[i] = processDelayBasedSample ((int) ch, data[i], type, feedback);
            }
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
