#include "LimiterModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    LimiterModule::LimiterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          gainParam    (apvtsIn.getRawParameterValue (limiterParamId (slotIndexIn, LimiterParam::gain))),
          ceilingParam (apvtsIn.getRawParameterValue (limiterParamId (slotIndexIn, LimiterParam::ceiling))),
          releaseParam (apvtsIn.getRawParameterValue (limiterParamId (slotIndexIn, LimiterParam::release)))
    {
    }

    void LimiterModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        limiter.prepare (spec);
    }

    void LimiterModule::reset()
    {
        limiter.reset();
    }

    void LimiterModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float gainOffset = modMatrix.getOffsetForParam (limiterParamId (slotIndex, LimiterParam::gain), 24.0f);
        const float gainLinear = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, gainParam->load() + gainOffset));

        const float ceilingOffset = modMatrix.getOffsetForParam (limiterParamId (slotIndex, LimiterParam::ceiling), 12.0f);
        const float ceiling = juce::jlimit (-12.0f, 0.0f, ceilingParam->load() + ceilingOffset);

        const float releaseOffset = modMatrix.getOffsetForParam (limiterParamId (slotIndex, LimiterParam::release), 200.0f);
        const float release = juce::jlimit (1.0f, 1000.0f, releaseParam->load() + releaseOffset);

        const auto numChannels = block.getNumChannels();
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] *= gainLinear;
        }

        limiter.setThreshold (ceiling);
        limiter.setRelease (release);
        juce::dsp::ProcessContextReplacing<float> context (block);
        limiter.process (context);

        // juce::dsp::Limiter's Threshold only drives a gentle internal pre-compression stage
        // (roughly 10:1) -- its own hard safety clip underneath is fixed at 0dBFS regardless of
        // Threshold, so on its own it does NOT guarantee output stays at or below Ceiling for a
        // hard-driven signal (verified empirically: even undriven input can end up barely
        // touched, and enough Gain just pins output at exactly 1.0, not Ceiling). A final
        // explicit hard clamp here is what actually makes Ceiling a real ceiling -- the
        // dsp::Limiter stage above still does its job of keeping most material well clear of
        // this clamp so it's rarely more than a safety net in practice, not the main character.
        const float ceilingLinear = juce::Decibels::decibelsToGain (ceiling);
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = juce::jlimit (-ceilingLinear, ceilingLinear, data[i]);
        }
    }
}
