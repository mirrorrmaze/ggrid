#include "MultipassModule.h"

namespace GGrid
{
    MultipassModule::MultipassModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          splitHz1Param (apvtsIn.getRawParameterValue (multipassParamId (slotIndexIn, MultipassParam::splitHz1))),
          splitHz2Param (apvtsIn.getRawParameterValue (multipassParamId (slotIndexIn, MultipassParam::splitHz2)))
    {
        for (int b = 0; b < kNumMultipassBands; ++b)
            mixParams[(size_t) b] = apvtsIn.getRawParameterValue (multipassBandParamId (slotIndexIn, b, MultipassBandParam::mix));

        for (auto& stage : splitStages)
        {
            stage.lowFilter.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            stage.highFilter.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        }
    }

    void MultipassModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        for (auto& stage : splitStages)
        {
            stage.lowFilter.prepare (spec);
            stage.highFilter.prepare (spec);
            stage.smoothedHz.reset (spec.sampleRate, 0.05);
        }
        splitStages[0].smoothedHz.setCurrentAndTargetValue (juce::jlimit (20.0f, (float) (spec.sampleRate * 0.49), splitHz1Param->load()));
        splitStages[1].smoothedHz.setCurrentAndTargetValue (juce::jlimit (20.0f, (float) (spec.sampleRate * 0.49), splitHz2Param->load()));

        remaining.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        lowScratch.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        for (auto& bandBuffer : bandOutputBuffers)
            bandBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void MultipassModule::reset()
    {
        for (auto& stage : splitStages)
        {
            stage.lowFilter.reset();
            stage.highFilter.reset();
        }
    }

    void MultipassModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        const float nyquistGuard = (float) (sampleRate * 0.49);

        // Captured once, before splitting -- every band's own Mix knob blends its isolated
        // crossover-filtered content against this same original signal (see the class comment).
        dryBuffer.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), numSamples);

        remaining.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            remaining.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), numSamples);

        // Split point 2 must always sit meaningfully above split point 1 -- same defensive clamp
        // as MultibandConvolutionModule::process(), matching CrossoverSplitBar's own drag-time
        // ordering so dragging can never produce an order the DSP would silently correct behind
        // the GUI's back.
        const float hz1 = juce::jlimit (20.0f, nyquistGuard, splitHz1Param->load());
        const float hz2 = juce::jlimit (hz1 * 1.05f, nyquistGuard, splitHz2Param->load());
        const std::array<float, kNumMultipassBands - 1> targetSplitHz { hz1, hz2 };

        for (int s = 0; s < kNumMultipassBands - 1; ++s)
        {
            auto& stage = splitStages[(size_t) s];
            stage.smoothedHz.setTargetValue (targetSplitHz[(size_t) s]);
            stage.smoothedHz.skip (juce::jmax (0, numSamples - 1));
            const float hz = juce::jlimit (20.0f, nyquistGuard, stage.smoothedHz.getNextValue());
            stage.lowFilter.setCutoffFrequency (hz);
            stage.highFilter.setCutoffFrequency (hz);

            auto& band = bandOutputBuffers[(size_t) s];
            band.setSize (numChannels, numSamples, false, false, true);
            for (int ch = 0; ch < numChannels; ++ch)
                band.copyFrom (ch, 0, remaining, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> bandBlock (band);
            stage.lowFilter.process (juce::dsp::ProcessContextReplacing<float> (bandBlock));

            lowScratch.setSize (numChannels, numSamples, false, false, true);
            for (int ch = 0; ch < numChannels; ++ch)
                lowScratch.copyFrom (ch, 0, remaining, ch, 0, numSamples);

            juce::dsp::AudioBlock<float> highBlock (lowScratch);
            stage.highFilter.process (juce::dsp::ProcessContextReplacing<float> (highBlock));

            std::swap (remaining, lowScratch);
        }

        auto& lastBand = bandOutputBuffers[(size_t) (kNumMultipassBands - 1)];
        lastBand.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            lastBand.copyFrom (ch, 0, remaining, ch, 0, numSamples);

        for (int b = 0; b < kNumMultipassBands; ++b)
        {
            const float mixOffset = modMatrix.getOffsetForParam (
                multipassBandParamId (slotIndex, b, MultipassBandParam::mix), 50.0f);
            const float mix = juce::jlimit (0.0f, 100.0f, mixParams[(size_t) b]->load() + mixOffset) / 100.0f;

            auto& bandBuf = bandOutputBuffers[(size_t) b];
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = bandBuf.getWritePointer (ch);
                const auto* dry = dryBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    data[i] = data[i] * mix + dry[i] * (1.0f - mix);
            }
        }

        // Deliberately does not touch `block` itself -- Multipass has no single "main" output,
        // only the 3 named bands above (see getOutputBusBuffer); its shared buffer just keeps
        // whatever it already held (the accumulated pre-split input), which is harmless since
        // nothing legitimate ever reads it (the 4th, unpinned output nub is hidden in the GUI).
    }
}
