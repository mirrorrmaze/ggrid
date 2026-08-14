#include "DynamicsModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    DynamicsModule::DynamicsModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : thresholdParam (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::threshold))),
          ratioParam     (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::ratio))),
          attackParam    (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::attack))),
          releaseParam   (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::release))),
          makeupParam    (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::makeup))),
          mixParam       (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::mix)))
    {
    }

    void DynamicsModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        compressor.prepare (spec);
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void DynamicsModule::reset()
    {
        compressor.reset();
    }

    void DynamicsModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix&)
    {
        compressor.setThreshold (thresholdParam->load());
        compressor.setRatio (ratioParam->load());
        compressor.setAttack (attackParam->load());
        compressor.setRelease (releaseParam->load());

        const float makeupGain = juce::Decibels::decibelsToGain (makeupParam->load());
        const float mix = mixParam->load() / 100.0f;

        const auto numChannels = block.getNumChannels();
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        juce::dsp::ProcessContextReplacing<float> context (block);
        compressor.process (context);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = data[i] * makeupGain * mix + dry[i] * (1.0f - mix);
        }
    }
}
