#include "CompressorModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    float CompressorModule::staticCharacteristic (float inputDb, float thresholdDb, float ratio, float kneeDb)
    {
        const float x = inputDb - thresholdDb;
        const float slope = (1.0f / ratio) - 1.0f; // negative for any ratio > 1

        if (kneeDb <= 0.0f)
            return x > 0.0f ? slope * x : 0.0f;

        if (2.0f * x < -kneeDb)
            return 0.0f;

        if (2.0f * std::abs (x) <= kneeDb)
        {
            const float t = x + kneeDb * 0.5f;
            return slope * (t * t) / (2.0f * kneeDb);
        }

        return slope * x;
    }

    CompressorModule::CompressorModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          thresholdParam  (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::threshold))),
          ratioParam      (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::ratio))),
          attackParam     (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::attack))),
          releaseParam    (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::release))),
          kneeParam       (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::knee))),
          makeupParam     (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::makeup))),
          mixParam        (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::mix))),
          detectionParam  (apvtsIn.getRawParameterValue (compressorParamId (slotIndexIn, CompressorParam::detection)))
    {
    }

    void CompressorModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        reset();
    }

    void CompressorModule::reset()
    {
        for (auto& ch : channels)
        {
            ch.rmsSquared = 0.0f;
            ch.gainReductionDb = 0.0f;
        }
    }

    void CompressorModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float threshold = juce::jlimit (-60.0f, 0.0f, thresholdParam->load()
            + modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::threshold), 12.0f));
        const float ratio = juce::jlimit (1.0f, 20.0f, ratioParam->load()
            + modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::ratio), 4.0f));
        const float attackMs = juce::jlimit (0.1f, 200.0f, attackParam->load()
            + modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::attack), 50.0f));
        const float releaseMs = juce::jlimit (5.0f, 1000.0f, releaseParam->load()
            + modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::release), 200.0f));
        const float kneeDb = juce::jlimit (0.0f, 24.0f, kneeParam->load()
            + modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::knee), 12.0f));

        const float makeupOffset = modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::makeup), 12.0f);
        const float makeupGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, makeupParam->load() + makeupOffset));

        const float mixOffset = modMatrix.getOffsetForParam (compressorParamId (slotIndex, CompressorParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const bool rmsMode = (int) detectionParam->load() == 1;

        // One-pole ballistics coefficients -- exp(-1 / (timeConstantSamples)) is the standard
        // per-sample smoothing coefficient for a given time-in-ms at this sample rate.
        const float attackCoeff = std::exp (-1.0f / (float) (sampleRate * (double) attackMs * 0.001));
        const float releaseCoeff = std::exp (-1.0f / (float) (sampleRate * (double) releaseMs * 0.001));
        // Fixed ~5ms time constant for the RMS detector's own smoothing -- fast enough to track
        // program material, slow enough to genuinely average rather than just re-deriving Peak.
        const float rmsCoeff = std::exp (-1.0f / (float) (sampleRate * 0.005));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxCompressorChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto& state = channels[ch];
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);

            for (size_t i = 0; i < numSamples; ++i)
            {
                const float in = dry[i];

                float detectedLevel;
                if (rmsMode)
                {
                    state.rmsSquared = rmsCoeff * state.rmsSquared + (1.0f - rmsCoeff) * (in * in);
                    detectedLevel = std::sqrt (juce::jmax (state.rmsSquared, 1.0e-12f));
                }
                else
                {
                    detectedLevel = std::abs (in);
                }

                const float levelDb = juce::Decibels::gainToDecibels (detectedLevel, -100.0f);
                const float targetGrDb = staticCharacteristic (levelDb, threshold, ratio, kneeDb);

                // Decoupled ballistics: attack while gain reduction is deepening (target more
                // negative than where we currently are), release while it's recovering.
                const float coeff = (targetGrDb < state.gainReductionDb) ? attackCoeff : releaseCoeff;
                state.gainReductionDb = targetGrDb + coeff * (state.gainReductionDb - targetGrDb);

                const float grGain = juce::Decibels::decibelsToGain (state.gainReductionDb);
                data[i] = (in * grGain * makeupGain) * mix + in * (1.0f - mix);
            }
        }
    }
}
