#include "ConvolutionModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    ConvolutionModule::ConvolutionModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : slotIndex (slotIndexIn), services (servicesIn),
          irIndexParam (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::irIndex))),
          toneParam    (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::tone))),
          fadeInParam  (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::fadeIn))),
          fadeOutParam (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::fadeOut))),
          stretchParam (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::stretch))),
          mixParam     (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::mix))),
          outputParam  (apvtsIn.getRawParameterValue (convolutionParamId (slotIndexIn, ConvolutionParam::output))),
          convolution (servicesIn.convolutionQueue)
    {
    }

    void ConvolutionModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        convolution.prepare (spec);
        toneLowShelf.prepare (spec);
        toneHighShelf.prepare (spec);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        applyToneCoefficients (toneParam->load());
        lastTone = toneParam->load();

        needsInitialLoad = true;
        pendingReloadCountdown = 0; // force a load on the very first block
    }

    void ConvolutionModule::reset()
    {
        convolution.reset();
        toneLowShelf.reset();
        toneHighShelf.reset();
    }

    void ConvolutionModule::applyToneCoefficients (float tone)
    {
        // Simple tilt EQ: tone in [-1, 1] maps to a low-shelf cut / high-shelf boost pair (or
        // the reverse), +/-6dB at the extremes. Runtime-only, no IR reload needed.
        const float gainDb = tone * 6.0f;
        const float lowGain = juce::Decibels::decibelsToGain (-gainDb);
        const float highGain = juce::Decibels::decibelsToGain (gainDb);

        *toneLowShelf.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf (sampleRate, 300.0, 0.707f, lowGain);
        *toneHighShelf.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf (sampleRate, 3000.0, 0.707f, highGain);
    }

    bool ConvolutionModule::copyDisplayBufferIfChanged (juce::AudioBuffer<float>& outBuffer, juce::int64& lastSeenGeneration)
    {
        const juce::SpinLock::ScopedLockType lock (displayLock);
        if (displayGeneration == lastSeenGeneration)
            return false;

        outBuffer = displayBuffer;
        lastSeenGeneration = displayGeneration;
        return true;
    }

    void ConvolutionModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix&)
    {
        // Pick up a finished reshape if the background worker has one ready. This is just a
        // spin-locked pointer scan across a handful of slots, not the actual shaping work.
        {
            juce::AudioBuffer<float> readyIR;
            double readySr = sampleRate;
            if (services.irReshapeWorker.tryTakeResult (*this, readyIR, readySr))
            {
                {
                    const juce::SpinLock::ScopedLockType lock (displayLock);
                    displayBuffer = readyIR; // copy for GUI display before the move below empties it
                    ++displayGeneration;
                }

                // Must happen on the audio thread (JUCE's docs for Convolution say load calls
                // "must be synchronised with process() calls"). The call itself is wait-free --
                // JUCE defers the actual engine-building work to its own background thread via
                // the shared ConvolutionMessageQueue -- so this doesn't reintroduce the
                // CPU-spike problem the debounce/background-reshape exists to avoid.
                convolution.loadImpulseResponse (std::move (readyIR), readySr,
                                                  juce::dsp::Convolution::Stereo::yes,
                                                  juce::dsp::Convolution::Trim::no,
                                                  juce::dsp::Convolution::Normalise::yes);
            }
        }

        // Tone applies immediately (cheap runtime filter coefficients).
        const float tone = toneParam->load();
        if (! juce::approximatelyEqual (tone, lastTone))
        {
            applyToneCoefficients (tone);
            lastTone = tone;
        }

        // Debounced reshape trigger for IR-affecting params -- coalesces rapid knob movement
        // into one request ~200ms after the last change, rather than resampling a multi-second
        // buffer on every callback while the user drags Stretch.
        const int irIndex = (int) irIndexParam->load();
        const float fadeIn = fadeInParam->load();
        const float fadeOut = fadeOutParam->load();
        const float stretch = stretchParam->load();

        const bool irRelevantChanged =
            irIndex != lastIrIndexForReload ||
            ! juce::approximatelyEqual (fadeIn, lastFadeInForReload) ||
            ! juce::approximatelyEqual (fadeOut, lastFadeOutForReload) ||
            ! juce::approximatelyEqual (stretch, lastStretchForReload);

        if (irRelevantChanged || needsInitialLoad)
        {
            lastIrIndexForReload = irIndex;
            lastFadeInForReload = fadeIn;
            lastFadeOutForReload = fadeOut;
            lastStretchForReload = stretch;
            pendingReloadCountdown = needsInitialLoad ? 0 : reloadDebounceSamplesAt48k;
            needsInitialLoad = false;
        }

        const auto numSamples = block.getNumSamples();

        if (pendingReloadCountdown >= 0)
        {
            pendingReloadCountdown -= (int) numSamples;
            if (pendingReloadCountdown <= 0)
            {
                pendingReloadCountdown = -1;

                IRReshapeWorker::Job job;
                job.irIndex = irIndex;
                job.sampleRate = sampleRate;
                job.fadeInMs = fadeIn;
                job.fadeOutPercent = fadeOut;
                job.stretch = stretch;
                services.irReshapeWorker.requestReshape (*this, job);
            }
        }

        const float mix = mixParam->load() / 100.0f;
        const float outputGain = juce::Decibels::decibelsToGain (outputParam->load());
        const auto numChannels = block.getNumChannels();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        juce::dsp::ProcessContextReplacing<float> context (block);
        convolution.process (context);
        toneLowShelf.process (context);
        toneHighShelf.process (context);

        // Self-heal against non-finite samples before they reach the output -- an IIR filter
        // (the tone shelves) that ever ingests a NaN/Inf sample stays corrupted on every future
        // block, since y[n] depends on y[n-1], and nothing short of a state reset clears that.
        bool nonFinite = false;
        for (size_t ch = 0; ch < numChannels && ! nonFinite; ++ch)
        {
            const auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                if (! std::isfinite (data[i])) { nonFinite = true; break; }
        }

        if (nonFinite)
        {
            block.clear();
            reset();
            return;
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
