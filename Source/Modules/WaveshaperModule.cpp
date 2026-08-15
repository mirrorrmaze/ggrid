#include "WaveshaperModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    WaveshaperModule::WaveshaperModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          driveParam      (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::drive))),
          shapeParam      (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::shape))),
          symmetryParam   (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::symmetry))),
          foldParam       (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::foldAmount))),
          oversampleParam (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::oversample))),
          mixParam        (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::mix))),
          outputParam     (apvtsIn.getRawParameterValue (waveshaperParamId (slotIndexIn, WaveshaperParam::output)))
    {
    }

    void WaveshaperModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        oversampling2x.initProcessing (spec.maximumBlockSize);
        oversampling4x.initProcessing (spec.maximumBlockSize);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void WaveshaperModule::reset()
    {
        oversampling2x.reset();
        oversampling4x.reset();
    }

    float WaveshaperModule::shapeSample (float x, int shapeIndex, float symmetry, float foldAmount) const
    {
        // Bias in before shaping, partially compensate after -- the classic way to introduce
        // even-harmonic (asymmetric) character without just adding a static DC offset to the output.
        x += symmetry * 0.5f;

        float y;
        switch (shapeIndex)
        {
            case 0: // Hard Clip
                y = juce::jlimit (-1.0f, 1.0f, x);
                break;

            case 1: // Soft Clip (tanh)
                y = std::tanh (x);
                break;

            case 2: // Soft Clip (cubic)
            {
                const float xc = juce::jlimit (-1.5f, 1.5f, x);
                y = juce::jlimit (-1.0f, 1.0f, (xc - (xc * xc * xc) / 3.0f) * 1.5f);
                break;
            }

            case 3: // Foldback Wavefolder
            {
                // Closed-form triangle-wave fold: bounded to [-1, 1] for any input magnitude,
                // no iteration needed. (An iterative "reflect off the ceiling" approach doesn't
                // actually shrink large-magnitude values fast enough and can overshoot the bound.)
                const float drive = 1.0f + foldAmount * 7.0f;
                const float z = x * drive * 0.25f;
                const float frac = z - std::floor (z + 0.5f); // sawtooth in [-0.5, 0.5)
                y = 4.0f * std::abs (frac) - 1.0f;             // triangle wave in [-1, 1)
                break;
            }

            case 4: // Sine Fold
            {
                const float drive = 1.0f + foldAmount * 8.0f;
                y = std::sin (x * drive * juce::MathConstants<float>::halfPi);
                break;
            }

            case 5: // Rectify/Asymmetric
                y = juce::jlimit (-1.0f, 1.0f, std::abs (x) * 2.0f - 1.0f);
                break;

            default:
                y = x;
                break;
        }

        return y - symmetry * 0.25f;
    }

    void WaveshaperModule::shapeBlockInPlace (juce::dsp::AudioBlock<float>& block, int shapeIndex, float driveGain, float symmetry, float foldAmount)
    {
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < block.getNumSamples(); ++i)
                data[i] = shapeSample (data[i] * driveGain, shapeIndex, symmetry, foldAmount);
        }
    }

    void WaveshaperModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int shapeIndex        = (int) shapeParam->load();
        const int oversampleChoice  = (int) oversampleParam->load(); // 0 = Off, 1 = 2x, 2 = 4x

        const float driveOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::waveshaperDrive));
        const float driveDb = juce::jlimit (0.0f, 40.0f, driveParam->load() + driveOffset);
        const float driveGain       = juce::Decibels::decibelsToGain (driveDb);
        const float symmetry        = symmetryParam->load();
        const float foldAmount      = foldParam->load();
        const float mix             = mixParam->load() / 100.0f;
        const float outputGain      = juce::Decibels::decibelsToGain (outputParam->load());

        const auto numChannels = block.getNumChannels();
        const auto numSamples  = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        if (oversampleChoice != 0 && numChannels <= (size_t) kMaxOversamplingChannels)
        {
            auto& os = (oversampleChoice == 1 ? oversampling2x : oversampling4x);
            auto upBlock = os.processSamplesUp (block);
            shapeBlockInPlace (upBlock, shapeIndex, driveGain, symmetry, foldAmount);
            os.processSamplesDown (block);
        }
        else
        {
            shapeBlockInPlace (block, shapeIndex, driveGain, symmetry, foldAmount);
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
