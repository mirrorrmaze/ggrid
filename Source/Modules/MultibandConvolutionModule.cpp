#include "MultibandConvolutionModule.h"
#include "../Params/Identifiers.h"
#include "../IR/IRProcessor.h"
#include <cmath>

namespace GGrid
{
    MultibandConvolutionModule::MultibandConvolutionModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : slotIndex (slotIndexIn), services (servicesIn),
          splitHz1Param (apvtsIn.getRawParameterValue (multibandConvolutionParamId (slotIndexIn, MultibandConvolutionParam::splitHz1))),
          splitHz2Param (apvtsIn.getRawParameterValue (multibandConvolutionParamId (slotIndexIn, MultibandConvolutionParam::splitHz2)))
    {
        for (int b = 0; b < kNumConvolutionBands; ++b)
        {
            auto voice = std::make_unique<BandVoice> (servicesIn.convolutionQueue);
            voice->irIndexParam = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::irIndex));
            voice->toneParam    = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::tone));
            voice->fadeInParam  = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::fadeIn));
            voice->fadeOutParam = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::fadeOut));
            voice->stretchParam = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::stretch));
            voice->mixParam     = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::mix));
            voice->outputParam  = apvtsIn.getRawParameterValue (multibandConvolutionBandParamId (slotIndexIn, b, MultibandConvolutionBandParam::output));
            bands[(size_t) b] = std::move (voice);
        }

        for (auto& stage : splitStages)
        {
            stage.lowFilter.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
            stage.highFilter.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        }
    }

    void MultibandConvolutionModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        analyzer.setSampleRate (spec.sampleRate);

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

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
        for (auto& bandBuffer : bandBuffers)
            bandBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        for (auto& voice : bands)
        {
            voice->sampleRate = sampleRate;
            voice->convolution.prepare (spec);
            voice->toneLowShelf.prepare (spec);
            voice->toneHighShelf.prepare (spec);

            applyToneCoefficients (*voice, voice->toneParam->load());
            voice->lastTone = voice->toneParam->load();

            voice->needsInitialLoad = true;
            voice->pendingReloadCountdown = 0; // force a load on the very first block
        }
    }

    void MultibandConvolutionModule::reset()
    {
        for (auto& stage : splitStages)
        {
            stage.lowFilter.reset();
            stage.highFilter.reset();
        }

        for (auto& voice : bands)
        {
            voice->convolution.reset();
            voice->toneLowShelf.reset();
            voice->toneHighShelf.reset();
        }
    }

    void MultibandConvolutionModule::applyToneCoefficients (BandVoice& voice, float tone)
    {
        // Same tilt EQ as ConvolutionModule -- see that class's comment.
        const float gainDb = tone * 6.0f;
        const float lowGain = juce::Decibels::decibelsToGain (-gainDb);
        const float highGain = juce::Decibels::decibelsToGain (gainDb);

        *voice.toneLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (voice.sampleRate, 300.0, 0.707f, lowGain);
        *voice.toneHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (voice.sampleRate, 3000.0, 0.707f, highGain);
    }

    void MultibandConvolutionModule::processBand (BandVoice& voice, int bandIndex, juce::AudioBuffer<float>& buffer,
                                                    const ModulationMatrix& modMatrix, int numSamples)
    {
        const auto paramId = [&] (const juce::String& name)
        {
            return multibandConvolutionBandParamId (slotIndex, bandIndex, name);
        };

        // Pick up a finished reshape if the background worker has one ready -- identical pattern
        // to ConvolutionModule::process(), keyed by this band voice's own stable address rather
        // than the whole module's, since each band reshapes independently.
        {
            juce::AudioBuffer<float> readyIR;
            double readySr = voice.sampleRate;
            int readyPreFadeOutLength = 0;
            int readyFadeRampSamples = 0;
            if (services.irReshapeWorker.tryTakeResult (&voice, readyIR, readySr, readyPreFadeOutLength, readyFadeRampSamples))
                voice.convolution.loadImpulseResponse (std::move (readyIR), readySr,
                                                        juce::dsp::Convolution::Stereo::yes,
                                                        juce::dsp::Convolution::Trim::no,
                                                        juce::dsp::Convolution::Normalise::yes);
        }

        const float tone = juce::jlimit (-1.0f, 1.0f, voice.toneParam->load()
            + modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::tone), 0.5f));
        if (! juce::approximatelyEqual (tone, voice.lastTone))
        {
            applyToneCoefficients (voice, tone);
            voice.lastTone = tone;
        }

        const int irIndex = (int) voice.irIndexParam->load();
        const float fadeIn = juce::jlimit (0.0f, 500.0f, voice.fadeInParam->load()
            + modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::fadeIn), 100.0f));
        const float fadeOut = juce::jlimit (0.0f, 100.0f, voice.fadeOutParam->load()
            + modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::fadeOut), 25.0f));
        const float stretch = juce::jlimit (0.25f, 4.0f, voice.stretchParam->load()
            + modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::stretch), 1.0f));

        const bool irRelevantChanged =
            irIndex != voice.lastIrIndexForReload ||
            ! juce::approximatelyEqual (fadeIn, voice.lastFadeInForReload) ||
            ! juce::approximatelyEqual (fadeOut, voice.lastFadeOutForReload) ||
            ! juce::approximatelyEqual (stretch, voice.lastStretchForReload);

        if (irRelevantChanged || voice.needsInitialLoad)
        {
            voice.lastIrIndexForReload = irIndex;
            voice.lastFadeInForReload = fadeIn;
            voice.lastFadeOutForReload = fadeOut;
            voice.lastStretchForReload = stretch;
            voice.pendingReloadCountdown = voice.needsInitialLoad ? 0 : reloadDebounceSamplesAt48k;
            voice.needsInitialLoad = false;
        }

        if (voice.pendingReloadCountdown >= 0)
        {
            voice.pendingReloadCountdown -= numSamples;
            if (voice.pendingReloadCountdown <= 0)
            {
                voice.pendingReloadCountdown = -1;

                IRReshapeWorker::Job job;
                job.irIndex = irIndex;
                job.sampleRate = voice.sampleRate;
                job.fadeInMs = fadeIn;
                job.fadeOutPercent = fadeOut;
                job.stretch = stretch;
                services.irReshapeWorker.requestReshape (&voice, job);
            }
        }

        const float mixOffset = modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, voice.mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (paramId (MultibandConvolutionBandParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, voice.outputParam->load() + outputOffset));

        const int numChannels = buffer.getNumChannels();

        // dryBuffer here is this band's post-split, pre-convolution content -- its own Mix knob
        // blends against what this band actually received, not the original pre-split signal.
        juce::AudioBuffer<float> dryBuffer (numChannels, numSamples);
        for (int ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> context (block);
        voice.convolution.process (context);
        voice.toneLowShelf.process (context);
        voice.toneHighShelf.process (context);

        bool nonFinite = false;
        for (int ch = 0; ch < numChannels && ! nonFinite; ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (data[i])) { nonFinite = true; break; }
        }

        if (nonFinite)
        {
            buffer.clear();
            voice.convolution.reset();
            voice.toneLowShelf.reset();
            voice.toneHighShelf.reset();
            return;
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto* dry = dryBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = data[i] * outputGain * mix + dry[i] * (1.0f - mix);
        }
    }

    void MultibandConvolutionModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int numChannels = (int) block.getNumChannels();
        const int numSamples = (int) block.getNumSamples();
        const float nyquistGuard = (float) (sampleRate * 0.49);

        // Feeds CrossoverSplitBar's live spectrum display -- analyzes the raw pre-split signal
        // as it arrives, same tap point as MultibandConvolver's own inputAnalyzer.
        {
            float* channelPtrs[8];
            const int analyzerChannels = juce::jmin (numChannels, 8);
            for (int ch = 0; ch < analyzerChannels; ++ch)
                channelPtrs[ch] = block.getChannelPointer ((size_t) ch);
            juce::AudioBuffer<float> analyzerView (channelPtrs, analyzerChannels, numSamples);
            analyzer.pushSamples (analyzerView);
        }

        remaining.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            remaining.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), numSamples);

        // Split point 2 must always sit meaningfully above split point 1, regardless of what the
        // two parameters happen to hold right now (host automation could in principle set them
        // out of order) -- clamped here rather than trusting CrossoverSplitBar's own drag-time
        // ordering to be the only thing enforcing it.
        const float hz1 = juce::jlimit (20.0f, nyquistGuard, splitHz1Param->load());
        const float hz2 = juce::jlimit (hz1 * 1.05f, nyquistGuard, splitHz2Param->load());
        const std::array<float, kNumConvolutionBands - 1> targetSplitHz { hz1, hz2 };

        for (int s = 0; s < kNumConvolutionBands - 1; ++s)
        {
            auto& stage = splitStages[(size_t) s];
            stage.smoothedHz.setTargetValue (targetSplitHz[(size_t) s]);
            stage.smoothedHz.skip (juce::jmax (0, numSamples - 1));
            const float hz = juce::jlimit (20.0f, nyquistGuard, stage.smoothedHz.getNextValue());
            stage.lowFilter.setCutoffFrequency (hz);
            stage.highFilter.setCutoffFrequency (hz);

            auto& band = bandBuffers[(size_t) s];
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

        auto& lastBand = bandBuffers[(size_t) (kNumConvolutionBands - 1)];
        lastBand.setSize (numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            lastBand.copyFrom (ch, 0, remaining, ch, 0, numSamples);

        for (int b = 0; b < kNumConvolutionBands; ++b)
            processBand (*bands[(size_t) b], b, bandBuffers[(size_t) b], modMatrix, numSamples);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < numSamples; ++i)
            {
                float sum = 0.0f;
                for (int b = 0; b < kNumConvolutionBands; ++b)
                    sum += bandBuffers[(size_t) b].getSample (ch, i);
                out[i] = sum;
            }
        }
    }
}
