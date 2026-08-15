#include "Eq8Module.h"

namespace GGrid
{
    Eq8Module::Eq8Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          mixParam    (apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, Eq8Param::mix))),
          outputParam (apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, Eq8Param::output)))
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            bandParams[(size_t) b] = apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, eq8BandParam (b)));
    }

    void Eq8Module::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        for (auto& channelBands : bandFilters)
            for (auto& band : channelBands)
                band.prepare (monoSpec);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        reset();
    }

    void Eq8Module::reset()
    {
        for (auto& channelBands : bandFilters)
            for (auto& band : channelBands)
                band.reset();
    }

    void Eq8Module::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        std::array<float, kNumEq8Bands> bandGainsDb {};
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            const float offset = modMatrix.getOffsetForParam (
                eq8ParamId (slotIndex, eq8BandParam (b)), 6.0f);
            bandGainsDb[(size_t) b] = juce::jlimit (-12.0f, 12.0f, bandParams[(size_t) b]->load() + offset);
        }

        const float mixOffset = modMatrix.getOffsetForParam (eq8ParamId (slotIndex, Eq8Param::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (eq8ParamId (slotIndex, Eq8Param::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxEq8Channels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            const float gainLinear = juce::Decibels::decibelsToGain (bandGainsDb[(size_t) b]);
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sampleRate, kEq8BandFrequencies[(size_t) b], kBandQ, gainLinear);

            for (size_t ch = 0; ch < numChannels; ++ch)
                *bandFilters[ch][(size_t) b].coefficients = *coeffs;
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
            {
                float sample = data[i];
                for (int b = 0; b < kNumEq8Bands; ++b)
                    sample = bandFilters[ch][(size_t) b].processSample (sample);
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
