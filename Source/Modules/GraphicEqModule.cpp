#include "GraphicEqModule.h"

namespace GGrid
{
    GraphicEqModule::GraphicEqModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          mixParam    (apvtsIn.getRawParameterValue (graphicEqParamId (slotIndexIn, GraphicEqParam::mix))),
          outputParam (apvtsIn.getRawParameterValue (graphicEqParamId (slotIndexIn, GraphicEqParam::output)))
    {
        for (int b = 0; b < kNumGraphicEqBands; ++b)
            bandParams[(size_t) b] = apvtsIn.getRawParameterValue (graphicEqParamId (slotIndexIn, graphicEqBandParam (b)));
    }

    void GraphicEqModule::prepare (const juce::dsp::ProcessSpec& spec)
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

    void GraphicEqModule::reset()
    {
        for (auto& channelBands : bandFilters)
            for (auto& band : channelBands)
                band.reset();
    }

    void GraphicEqModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        std::array<float, kNumGraphicEqBands> bandGainsDb {};
        for (int b = 0; b < kNumGraphicEqBands; ++b)
        {
            const float offset = modMatrix.getOffsetForParam (
                graphicEqParamId (slotIndex, graphicEqBandParam (b)), 6.0f);
            bandGainsDb[(size_t) b] = juce::jlimit (-12.0f, 12.0f, bandParams[(size_t) b]->load() + offset);
        }

        const float mixOffset = modMatrix.getOffsetForParam (graphicEqParamId (slotIndex, GraphicEqParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (graphicEqParamId (slotIndex, GraphicEqParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxEqChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (int b = 0; b < kNumGraphicEqBands; ++b)
        {
            const float gainLinear = juce::Decibels::decibelsToGain (bandGainsDb[(size_t) b]);
            auto coeffs = juce::dsp::IIR::Coefficients<float>::makePeakFilter (
                sampleRate, kGraphicEqBandFrequencies[(size_t) b], kBandQ, gainLinear);

            for (size_t ch = 0; ch < numChannels; ++ch)
                *bandFilters[ch][(size_t) b].coefficients = *coeffs;
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
            {
                float sample = data[i];
                for (int b = 0; b < kNumGraphicEqBands; ++b)
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
