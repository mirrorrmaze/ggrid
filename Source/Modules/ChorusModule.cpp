#include "ChorusModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    ChorusModule::ChorusModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          modeParam     (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::mode))),
          rateParam     (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::rate))),
          depthParam    (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::depth))),
          delayParam    (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::delay))),
          feedbackParam (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::feedback))),
          mixParam      (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::mix))),
          outputParam   (apvtsIn.getRawParameterValue (chorusParamId (slotIndexIn, ChorusParam::output)))
    {
    }

    void ChorusModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        // Delay swings up to ~1.9x the Delay knob's own max (30ms) -- 100ms of headroom covers
        // that with plenty to spare.
        const int maxDelaySamples = juce::jmax (64, (int) std::ceil (spec.sampleRate * 0.1));

        for (auto& line : delayLine)
        {
            line.setMaximumDelayInSamples (maxDelaySamples);
            line.prepare (monoSpec);
        }

        reset();
    }

    void ChorusModule::reset()
    {
        for (auto& line : delayLine)
            line.reset();

        // A quarter-cycle phase offset between channels gives natural stereo width without a
        // dedicated parameter -- both channels still read the same Rate/Depth/Delay.
        for (size_t ch = 0; ch < kMaxChorusChannels; ++ch)
            lfoPhase[ch] = (double) ch * 0.25;
    }

    void ChorusModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int mode = (int) modeParam->load(); // 0 = Chorus, 1 = Flanger

        const float rateOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::rate), 2.0f);
        const float rateHz = juce::jlimit (0.02f, 10.0f, rateParam->load() + rateOffset);

        const float depthOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::depth), 50.0f);
        const float depth01 = juce::jlimit (0.0f, 100.0f, depthParam->load() + depthOffset) / 100.0f;

        const float delayOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::delay), 10.0f);
        const float centerDelayMs = juce::jlimit (0.5f, 30.0f, delayParam->load() + delayOffset);

        const float feedbackOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::feedback), 0.5f);
        const float feedbackRaw = juce::jlimit (-0.95f, 0.95f, feedbackParam->load() + feedbackOffset);
        // Chorus stays feedback-free by design -- real chorus effects essentially never use
        // feedback (it creeps toward a metallic, flangey resonance); Flanger is where the
        // resonant comb character actually belongs.
        const float appliedFeedback = (mode == 1) ? feedbackRaw : 0.0f;

        const float mixOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (chorusParamId (slotIndex, ChorusParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxChorusChannels);
        const auto numSamples = block.getNumSamples();

        const double phaseInc = (double) rateHz / currentSampleRate;
        // Swing scales with the center delay itself (not a fixed ms range) so Depth behaves
        // proportionally whether Delay is dialed down near flange territory or up near chorus
        // territory, without needing separate per-mode constants.
        const float swingMs = depth01 * centerDelayMs * 0.9f;
        const float maxDelaySamplesF = (float) (currentSampleRate * 0.1 - 1.0);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            double phase = lfoPhase[ch];

            for (size_t i = 0; i < numSamples; ++i)
            {
                const float lfoValue = (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase);
                const float modulatedDelayMs = centerDelayMs + swingMs * lfoValue;
                const float delaySamples = juce::jlimit (1.0f, maxDelaySamplesF,
                    (float) (modulatedDelayMs * 0.001 * currentSampleRate));

                delayLine[ch].setDelay (delaySamples);
                const float delayed = delayLine[ch].popSample (0);

                // Soft-clip the feedback contribution itself (not the dry+wet sum below) --
                // unlike DelayModule's fixed-per-block delay time, Flanger's delay length is
                // modulated every sample, and that combined with feedback approaching unity can
                // make the Lagrange interpolator's output grow without bound over many cycles.
                // tanh caps what the feedback path can inject per sample to (-1, 1) regardless
                // of how large `delayed` gets, which rules that out structurally rather than
                // just capping the Feedback knob's range lower.
                const float feedbackSample = appliedFeedback != 0.0f ? std::tanh (delayed * appliedFeedback) : 0.0f;
                delayLine[ch].pushSample (0, data[i] + feedbackSample);

                data[i] = data[i] * (1.0f - mix) + delayed * mix;

                phase += phaseInc;
                phase -= std::floor (phase);
            }

            lfoPhase[ch] = phase;
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] *= outputGain;
        }
    }
}
