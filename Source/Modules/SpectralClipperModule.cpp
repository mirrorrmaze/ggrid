#include "SpectralClipperModule.h"
#include "../Params/Identifiers.h"
#include <cmath>
#include <algorithm>

namespace GGrid
{
    // -- ChannelEngine (STFT engine ported from LossyModule::ChannelEngine's windowing/overlap-add
    //    machinery; the per-bin clipping math below is this module's own) --

    void SpectralClipperModule::ChannelEngine::prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        hannWindow.resize ((size_t) windowSize);
        for (int i = 0; i < windowSize; ++i)
            hannWindow[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                                * (float) i / (float) (windowSize - 1)));

        inputRing.assign ((size_t) windowSize, 0.0f);
        fftScratch.assign ((size_t) windowSize * 2, 0.0f);
        accum.assign ((size_t) windowSize, 0.0f);

        // Overlap-add reconstruction gain: sum the squared window across 8 overlapping hops and
        // read the settled middle value -- identical derivation to LossyModule::ChannelEngine.
        {
            std::vector<double> sumSq ((size_t) windowSize * 2, 0.0);
            for (int hopIndex = 0; hopIndex < 8; ++hopIndex)
            {
                const int offset = hopIndex * hopSize;
                for (int i = 0; i < windowSize; ++i)
                {
                    const size_t idx = (size_t) (offset + i);
                    if (idx < sumSq.size())
                        sumSq[idx] += (double) hannWindow[(size_t) i] * hannWindow[(size_t) i];
                }
            }
            const double steadyState = sumSq[(size_t) (windowSize + windowSize / 2)];
            olaNormalisation = steadyState > 0.0 ? (float) (1.0 / steadyState) : 1.0f;
        }

        // Reference scale for "full-scale" magnitude on this window -- see LossyModule's identical
        // field for the full derivation. Ceiling is expressed in dB relative to this scale, so its
        // meaning stays independent of windowSize.
        {
            double windowSum = 0.0;
            for (float w : hannWindow)
                windowSum += w;
            magnitudeScale = (float) windowSum;
        }

        reset();
    }

    void SpectralClipperModule::ChannelEngine::reset()
    {
        std::fill (inputRing.begin(), inputRing.end(), 0.0f);
        std::fill (accum.begin(), accum.end(), 0.0f);
        outputFifo.clear();
        inputWritePos = 0;
        samplesSinceHop = 0;
    }

    float SpectralClipperModule::ChannelEngine::shapeMagnitude (float magnitude, float ceilingLinear, int shape)
    {
        if (ceilingLinear <= 0.0f)
            return 0.0f;

        const float ratio = magnitude / ceilingLinear;
        if (ratio <= 1.0f)
            return magnitude; // below the ceiling -- every shape passes it through unchanged

        switch (shape)
        {
            case 0: // Hard -- flat clamp at the ceiling
                return ceilingLinear;

            case 1: // Soft (tanh) -- smooth, continuous at the ceiling, allows a small amount of
                    // soft-knee overshoot above it before settling rather than a hard corner
                return ceilingLinear * (1.0f + 0.5f * std::tanh (ratio - 1.0f));

            case 2: // Foldback -- closed-form triangle-wave reflection of the overshoot back into
                    // [0, ceiling], same construction as Waveshaper's Foldback shape, applied to
                    // magnitude instead of a bipolar sample
            {
                const float excess = magnitude - ceilingLinear;
                const float period = 2.0f * ceilingLinear;
                float wrapped = std::fmod (excess, period);
                if (wrapped < 0.0f)
                    wrapped += period;
                return wrapped <= ceilingLinear ? (ceilingLinear - wrapped) : (wrapped - ceilingLinear);
            }

            case 3: // Sine Fold -- same sin()-based folding character as Waveshaper's Sine Fold,
                    // applied to the overshoot ratio
                return ceilingLinear * std::abs (std::sin (ratio * juce::MathConstants<float>::halfPi));

            default:
                return ceilingLinear;
        }
    }

    void SpectralClipperModule::ChannelEngine::processHop()
    {
        for (int i = 0; i < windowSize; ++i)
        {
            const int idx = (inputWritePos + i) % windowSize;
            fftScratch[(size_t) i] = inputRing[(size_t) idx] * hannWindow[(size_t) i];
        }
        std::fill (fftScratch.begin() + windowSize, fftScratch.end(), 0.0f);

        fft.performRealOnlyForwardTransform (fftScratch.data());

        const int nyquistBin = windowSize / 2;
        const float ceilingLinear = juce::Decibels::decibelsToGain (ceiling.load()) * magnitudeScale;
        const int shape = shapeIndex.load();
        const float mixNow = mix.load();

        for (int k = 0; k <= nyquistBin; ++k)
        {
            const float re = fftScratch[(size_t) (2 * k)];
            const float im = fftScratch[(size_t) (2 * k + 1)];
            const float mag = std::sqrt (re * re + im * im);

            if (mag <= 1.0e-9f)
                continue; // phase is undefined at zero magnitude -- nothing to clip either way

            const float phase = std::atan2 (im, re);
            const float clippedMag = shapeMagnitude (mag, ceilingLinear, shape);

            const float wetRe = clippedMag * std::cos (phase);
            const float wetIm = clippedMag * std::sin (phase);

            // Blended per-bin in the complex domain, same reasoning as LossyModule's Mix -- Mix at
            // 0 is a true bypass every hop, at 1 it's entirely the clipped spectrum.
            fftScratch[(size_t) (2 * k)]     = re * (1.0f - mixNow) + wetRe * mixNow;
            fftScratch[(size_t) (2 * k + 1)] = im * (1.0f - mixNow) + wetIm * mixNow;
        }

        fft.performRealOnlyInverseTransform (fftScratch.data());

        for (int i = 0; i < windowSize; ++i)
            accum[(size_t) i] += fftScratch[(size_t) i] * hannWindow[(size_t) i] * olaNormalisation;

        for (int i = 0; i < hopSize; ++i)
            outputFifo.push_back (accum[(size_t) i]);

        std::move (accum.begin() + hopSize, accum.end(), accum.begin());
        std::fill (accum.end() - hopSize, accum.end(), 0.0f);
    }

    float SpectralClipperModule::ChannelEngine::processOneSample (float inSample)
    {
        inputRing[(size_t) inputWritePos] = inSample * currentDriveGain;
        inputWritePos = (inputWritePos + 1) % windowSize;
        ++samplesSinceHop;

        if (samplesSinceHop >= hopSize)
        {
            processHop();
            samplesSinceHop = 0;
        }

        float out = 0.0f;
        if (! outputFifo.empty())
        {
            out = outputFifo.front();
            outputFifo.pop_front();
        }
        return out;
    }

    void SpectralClipperModule::ChannelEngine::process (const float* in, float* out, int numSamples)
    {
        currentDriveGain = juce::Decibels::decibelsToGain (drive.load());

        for (int i = 0; i < numSamples; ++i)
            out[i] = processOneSample (in[i]);
    }

    // -- SpectralClipperModule --

    SpectralClipperModule::SpectralClipperModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          driveParam   (apvtsIn.getRawParameterValue (spectralClipperParamId (slotIndexIn, SpectralClipperParam::drive))),
          ceilingParam (apvtsIn.getRawParameterValue (spectralClipperParamId (slotIndexIn, SpectralClipperParam::ceiling))),
          shapeParam   (apvtsIn.getRawParameterValue (spectralClipperParamId (slotIndexIn, SpectralClipperParam::shape))),
          mixParam     (apvtsIn.getRawParameterValue (spectralClipperParamId (slotIndexIn, SpectralClipperParam::mix))),
          outputParam  (apvtsIn.getRawParameterValue (spectralClipperParamId (slotIndexIn, SpectralClipperParam::output)))
    {
    }

    void SpectralClipperModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        for (auto& ch : channels)
            ch.prepare (spec.sampleRate);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void SpectralClipperModule::reset()
    {
        for (auto& ch : channels)
            ch.reset();
    }

    void SpectralClipperModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float driveOffset = modMatrix.getOffsetForParam (spectralClipperParamId (slotIndex, SpectralClipperParam::drive), 24.0f);
        const float driveValue = juce::jlimit (0.0f, 24.0f, driveParam->load() + driveOffset);

        const float ceilingOffset = modMatrix.getOffsetForParam (spectralClipperParamId (slotIndex, SpectralClipperParam::ceiling), 15.0f);
        const float ceilingValue = juce::jlimit (-24.0f, 6.0f, ceilingParam->load() + ceilingOffset);

        const float mixOffset = modMatrix.getOffsetForParam (spectralClipperParamId (slotIndex, SpectralClipperParam::mix), 50.0f);
        const float mixValue = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset);

        const float outputOffset = modMatrix.getOffsetForParam (spectralClipperParamId (slotIndex, SpectralClipperParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const int shapeValue = (int) shapeParam->load();

        for (auto& ch : channels)
        {
            ch.setDrive (driveValue);
            ch.setCeiling (ceilingValue);
            ch.setShape (shapeValue);
            ch.setMix (mixValue / 100.0f);
        }

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxSpectralClipperChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (size_t ch = 0; ch < numChannels; ++ch)
            channels[ch].process (dryBuffer.getReadPointer ((int) ch), block.getChannelPointer (ch), (int) numSamples);

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] *= outputGain;
        }
    }
}
