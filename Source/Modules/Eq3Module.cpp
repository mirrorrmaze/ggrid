#include "Eq3Module.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    Eq3Module::Eq3Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          lowParam    (apvtsIn.getRawParameterValue (eq3ParamId (slotIndexIn, Eq3Param::low))),
          midParam    (apvtsIn.getRawParameterValue (eq3ParamId (slotIndexIn, Eq3Param::mid))),
          highParam   (apvtsIn.getRawParameterValue (eq3ParamId (slotIndexIn, Eq3Param::high))),
          mixParam    (apvtsIn.getRawParameterValue (eq3ParamId (slotIndexIn, Eq3Param::mix))),
          outputParam (apvtsIn.getRawParameterValue (eq3ParamId (slotIndexIn, Eq3Param::output)))
    {
    }

    void Eq3Module::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        for (auto& f : lowFilters)  f.prepare (monoSpec);
        for (auto& f : midFilters)  f.prepare (monoSpec);
        for (auto& f : highFilters) f.prepare (monoSpec);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        reset();
    }

    void Eq3Module::reset()
    {
        for (auto& f : lowFilters)  f.reset();
        for (auto& f : midFilters)  f.reset();
        for (auto& f : highFilters) f.reset();
    }

    void Eq3Module::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float lowOffset = modMatrix.getOffsetForParam (eq3ParamId (slotIndex, Eq3Param::low), 6.0f);
        const float lowDb = juce::jlimit (-12.0f, 12.0f, lowParam->load() + lowOffset);

        const float midOffset = modMatrix.getOffsetForParam (eq3ParamId (slotIndex, Eq3Param::mid), 6.0f);
        const float midDb = juce::jlimit (-12.0f, 12.0f, midParam->load() + midOffset);

        const float highOffset = modMatrix.getOffsetForParam (eq3ParamId (slotIndex, Eq3Param::high), 6.0f);
        const float highDb = juce::jlimit (-12.0f, 12.0f, highParam->load() + highOffset);

        const float mixOffset = modMatrix.getOffsetForParam (eq3ParamId (slotIndex, Eq3Param::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (eq3ParamId (slotIndex, Eq3Param::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxEq3Channels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        auto lowCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
            sampleRate, kLowShelfFreq, 0.707f, juce::Decibels::decibelsToGain (lowDb));
        auto midCoeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
            sampleRate, kMidFreq, kMidQ, juce::Decibels::decibelsToGain (midDb));
        auto highCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRate, kHighShelfFreq, 0.707f, juce::Decibels::decibelsToGain (highDb));

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            *lowFilters[ch].coefficients = *lowCoeffs;
            *midFilters[ch].coefficients = *midCoeffs;
            *highFilters[ch].coefficients = *highCoeffs;
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
            {
                float sample = data[i];
                sample = lowFilters[ch].processSample (sample);
                sample = midFilters[ch].processSample (sample);
                sample = highFilters[ch].processSample (sample);
                data[i] = sample;
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
