#include "DynamicsModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    DynamicsModule::DynamicsModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          thresholdParam (apvtsIn.getRawParameterValue (dynamicsParamId (slotIndexIn, DynamicsParam::threshold))),
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

    void DynamicsModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float threshold = juce::jlimit (-60.0f, 0.0f, thresholdParam->load()
            + modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::threshold), 12.0f));
        const float ratio = juce::jlimit (1.0f, 20.0f, ratioParam->load()
            + modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::ratio), 4.0f));
        const float attack = juce::jlimit (0.1f, 200.0f, attackParam->load()
            + modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::attack), 50.0f));
        const float release = juce::jlimit (5.0f, 1000.0f, releaseParam->load()
            + modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::release), 200.0f));

        compressor.setThreshold (threshold);
        compressor.setRatio (ratio);
        compressor.setAttack (attack);
        compressor.setRelease (release);

        const float makeupOffset = modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::makeup), 12.0f);
        const float makeupGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, makeupParam->load() + makeupOffset));

        const float mixOffset = modMatrix.getOffsetForParam (dynamicsParamId (slotIndex, DynamicsParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

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
