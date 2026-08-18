#include "Eq8Module.h"

namespace GGrid
{
    Eq8Module::Eq8Module (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          mixParam    (apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, Eq8Param::mix))),
          outputParam (apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, Eq8Param::output)))
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            gainParams[(size_t) b]    = apvtsIn.getRawParameterValue (eq8ParamId (slotIndexIn, eq8BandParam (b)));
            freqParams[(size_t) b]    = apvtsIn.getRawParameterValue (eq8BandFreqParamId (slotIndexIn, b));
            qParams[(size_t) b]       = apvtsIn.getRawParameterValue (eq8BandQParamId (slotIndexIn, b));
            typeParams[(size_t) b]    = apvtsIn.getRawParameterValue (eq8BandTypeParamId (slotIndexIn, b));
            enabledParams[(size_t) b] = apvtsIn.getRawParameterValue (eq8BandEnabledParamId (slotIndexIn, b));
        }
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

    juce::dsp::IIR::Coefficients<float>::Ptr Eq8Module::makeCoefficients (
        int type, double sr, float freq, float q, float gainLinear)
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        freq = juce::jlimit (20.0f, (float) (sr * 0.49), freq);
        q = juce::jmax (0.05f, q);

        switch (type)
        {
            case 1:  return Coeffs::makeLowShelf (sr, freq, q, gainLinear);
            case 2:  return Coeffs::makeHighShelf (sr, freq, q, gainLinear);
            case 3:  return Coeffs::makeHighPass (sr, freq, q);
            case 4:  return Coeffs::makeLowPass (sr, freq, q);
            case 5:  return Coeffs::makeNotch (sr, freq, q);
            case 6:  return Coeffs::makeBandPass (sr, freq, q);
            case 0:
            default: return Coeffs::makePeakFilter (sr, freq, q, gainLinear);
        }
    }

    void Eq8Module::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float mixOffset = modMatrix.getOffsetForParam (eq8ParamId (slotIndex, Eq8Param::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (eq8ParamId (slotIndex, Eq8Param::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxEq8Channels);
        const auto numSamples = block.getNumSamples();

        dryBuffer.setSize ((int) numChannels, (int) numSamples, false, false, true);
        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        // Only enabled bands actually run in the per-sample loop below -- an all-disabled EQ
        // reduces to a plain Mix/Output pass rather than 8 wasted no-op filter calls.
        std::array<int, kNumEq8Bands> activeBands {};
        int numActiveBands = 0;

        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            if (enabledParams[(size_t) b]->load() < 0.5f)
                continue;

            const int type = (int) typeParams[(size_t) b]->load();
            const float freqOffset = modMatrix.getOffsetForParam (eq8BandFreqParamId (slotIndex, b), 2000.0f);
            const float freq = freqParams[(size_t) b]->load() + freqOffset;
            const float q = qParams[(size_t) b]->load();
            const float gainOffset = modMatrix.getOffsetForParam (eq8ParamId (slotIndex, eq8BandParam (b)), 6.0f);
            const float gainDb = juce::jlimit (-24.0f, 24.0f, gainParams[(size_t) b]->load() + gainOffset);
            const float gainLinear = eq8BandTypeHasGain (type) ? juce::Decibels::decibelsToGain (gainDb) : 1.0f;

            auto coeffs = makeCoefficients (type, sampleRate, freq, q, gainLinear);
            for (size_t ch = 0; ch < numChannels; ++ch)
                *bandFilters[ch][(size_t) b].coefficients = *coeffs;

            activeBands[(size_t) numActiveBands++] = b;
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
            {
                float sample = data[i];
                for (int a = 0; a < numActiveBands; ++a)
                    sample = bandFilters[ch][(size_t) activeBands[(size_t) a]].processSample (sample);
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
