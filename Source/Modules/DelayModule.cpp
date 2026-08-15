#include "DelayModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    DelayModule::DelayModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : slotIndex (slotIndexIn), services (servicesIn),
          timeParam       (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::time))),
          syncParam       (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::sync))),
          divisionParam   (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::division))),
          feedbackParam   (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::feedback))),
          saturationParam (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::saturation))),
          lowCutParam     (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::lowCut))),
          hiCutParam      (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::hiCut))),
          pingPongParam   (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::pingPong))),
          mixParam        (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::mix))),
          outputParam     (apvtsIn.getRawParameterValue (delayParamId (slotIndexIn, DelayParam::output)))
    {
    }

    void DelayModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        // 2 seconds max at whatever sample rate the host is running, covering the parameter's
        // full 1-2000ms range with no truncation.
        const int maxDelaySamples = juce::jmax (64, (int) std::ceil (spec.sampleRate * 2.0));

        for (auto& line : delayLine)
        {
            line.setMaximumDelayInSamples (maxDelaySamples);
            line.prepare (monoSpec);
        }

        for (auto& f : lowCutFilter) f.prepare (monoSpec);
        for (auto& f : hiCutFilter)  f.prepare (monoSpec);
        lastLowCutHz = -1.0f;
        lastHiCutHz = -1.0f;
        updateFilterCoefficients (lowCutParam->load(), hiCutParam->load());

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        const float initialDelaySamples = juce::jlimit (1.0f, (float) (currentSampleRate * 2.0 - 1.0),
                                                          (float) (timeParam->load() * 0.001 * currentSampleRate));
        smoothedDelaySamples.reset (spec.sampleRate, 0.015); // ~15ms ramp
        smoothedDelaySamples.setCurrentAndTargetValue (initialDelaySamples);
    }

    void DelayModule::reset()
    {
        for (auto& line : delayLine)
            line.reset();

        for (auto& f : lowCutFilter) f.reset();
        for (auto& f : hiCutFilter)  f.reset();
    }

    void DelayModule::updateFilterCoefficients (float lowCutHz, float hiCutHz)
    {
        if (juce::approximatelyEqual (lowCutHz, lastLowCutHz) && juce::approximatelyEqual (hiCutHz, lastHiCutHz))
            return;

        lastLowCutHz = lowCutHz;
        lastHiCutHz = hiCutHz;

        const float lowCut = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), lowCutHz);
        const float hiCut = juce::jlimit (lowCut + 1.0f, (float) (currentSampleRate * 0.45), hiCutHz);

        auto lowCutCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, lowCut);
        auto hiCutCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass (currentSampleRate, hiCut);

        for (auto& f : lowCutFilter) *f.coefficients = *lowCutCoeffs;
        for (auto& f : hiCutFilter)  *f.coefficients = *hiCutCoeffs;
    }

    void DelayModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float timeOffsetMs = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::delayTime))
                                  + modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::time), 500.0f);
        const float feedbackOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::delayFeedback))
                                    + modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::feedback), 0.9f);
        const float saturationOffset = modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::saturation), 0.5f);
        const float lowCutOffset = modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::lowCut), 300.0f);
        const float hiCutOffset = modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::hiCut), 3000.0f);
        const float mixOffset = modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::mix), 50.0f);
        const float outputOffset = modMatrix.getOffsetForParam (delayParamId (slotIndex, DelayParam::output), 12.0f);

        const bool synced = syncParam->load() >= 0.5f;
        float timeMs;
        if (synced)
        {
            const double quarterNoteMs = 60000.0 / juce::jmax (1.0, services.hostBpm.load());
            timeMs = (float) (quarterNoteMs * getDelayDivisionMultiplier ((int) divisionParam->load()));
        }
        else
        {
            timeMs = timeParam->load();
        }
        timeMs += timeOffsetMs;

        const float delaySamples = juce::jlimit (1.0f, (float) (currentSampleRate * 2.0 - 1.0),
                                                  (float) (timeMs * 0.001 * currentSampleRate));
        const float feedback = juce::jlimit (-0.98f, 0.98f, feedbackParam->load() + feedbackOffset);
        const float saturation = juce::jlimit (0.0f, 1.0f, saturationParam->load() + saturationOffset);
        const bool pingPong = pingPongParam->load() >= 0.5f;

        updateFilterCoefficients (juce::jmax (20.0f, lowCutParam->load() + lowCutOffset),
                                   juce::jmax (20.0f, hiCutParam->load() + hiCutOffset));
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxDelayChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        const float driveAmt = 1.0f + saturation * 4.0f;

        smoothedDelaySamples.setTargetValue (delaySamples);

        // Sample-outer / channel-inner so the ramp advances exactly once per sample index and
        // both channels see the identical smoothed delay time at that instant.
        for (size_t i = 0; i < numSamples; ++i)
        {
            const float currentDelaySamples = smoothedDelaySamples.getNextValue();

            // Pop both channels' delayed values before pushing either -- ping-pong mode cross-
            // feeds one channel's echo into the other's next write, so both reads must happen
            // against last block's state, not a value one of this loop's own writes just changed.
            float delayedValues[kMaxDelayChannels] = {};
            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                delayLine[ch].setDelay (currentDelaySamples);
                delayedValues[ch] = delayLine[ch].popSample (0);
            }

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                const size_t feedbackSourceChannel = (pingPong && numChannels == 2) ? (1 - ch) : ch;

                // Low Cut/Hi Cut shape the signal *going back into the delay line*, not the wet
                // tap -- so each successive repeat is filtered a little more than the last,
                // instead of every echo (including the first) sounding identically filtered.
                float feedbackSample = hiCutFilter[ch].processSample (
                                            lowCutFilter[ch].processSample (delayedValues[feedbackSourceChannel]))
                                        * feedback;
                if (saturation > 0.0f)
                    feedbackSample = std::tanh (feedbackSample * driveAmt) / driveAmt;

                auto* data = block.getChannelPointer (ch);
                delayLine[ch].pushSample (0, data[i] + feedbackSample);
                data[i] = delayedValues[ch]; // wet-only echo signal; blended with dry below
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
