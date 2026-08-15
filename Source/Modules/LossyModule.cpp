#include "LossyModule.h"
#include "../Params/Identifiers.h"
#include <cmath>
#include <algorithm>

namespace GGrid
{
    // -- ChannelEngine (ported from SPANDEX's LossyProcessor) --

    void LossyModule::ChannelEngine::prepare (double sampleRateIn)
    {
        sampleRate = sampleRateIn > 0.0 ? sampleRateIn : 44100.0;

        hannWindow.resize ((size_t) windowSize);
        for (int i = 0; i < windowSize; ++i)
            hannWindow[(size_t) i] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                                * (float) i / (float) (windowSize - 1)));

        inputRing.assign ((size_t) windowSize, 0.0f);
        fftScratch.assign ((size_t) windowSize * 2, 0.0f);
        heldWetRe.assign ((size_t) (windowSize / 2 + 1), 0.0f);
        heldWetIm.assign ((size_t) (windowSize / 2 + 1), 0.0f);
        accum.assign ((size_t) windowSize, 0.0f);

        // Overlap-add reconstruction gain: sum the squared window across 8 overlapping hops and
        // read the settled middle value.
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

        // Reference scale for "full-scale" magnitude on this window -- the sum of the window's
        // own coefficients, i.e. what a full-scale DC input would read as at bin 0. Quantizing
        // magnitude relative to this (rather than FFT's unnormalized raw units, which scale with
        // window size) keeps the Bits control's meaning independent of windowSize.
        {
            double windowSum = 0.0;
            for (float w : hannWindow)
                windowSum += w;
            magnitudeScale = (float) windowSum;
        }

        reset();
    }

    void LossyModule::ChannelEngine::reset()
    {
        std::fill (inputRing.begin(), inputRing.end(), 0.0f);
        std::fill (heldWetRe.begin(), heldWetRe.end(), 0.0f);
        std::fill (heldWetIm.begin(), heldWetIm.end(), 0.0f);
        std::fill (accum.begin(), accum.end(), 0.0f);
        outputFifo.clear();
        inputWritePos = 0;
        samplesSinceHop = 0;
        hopsSinceRefresh = 0;
    }

    void LossyModule::ChannelEngine::processHop()
    {
        for (int i = 0; i < windowSize; ++i)
        {
            const int idx = (inputWritePos + i) % windowSize;
            fftScratch[(size_t) i] = inputRing[(size_t) idx] * hannWindow[(size_t) i];
        }
        std::fill (fftScratch.begin() + windowSize, fftScratch.end(), 0.0f);

        fft.performRealOnlyForwardTransform (fftScratch.data());

        // Refresh rate is expressed in Hz (times/second); convert to "how many hops between
        // refreshes" using this window's actual hop rate, which depends on the real sample rate.
        const double hopRateHz = sampleRate / (double) hopSize;
        const int holdHops = juce::jmax (1, (int) std::round (hopRateHz / (double) refreshHz.load()));

        ++hopsSinceRefresh;
        const bool doRefresh = hopsSinceRefresh >= holdHops;
        if (doRefresh)
            hopsSinceRefresh = 0;

        const int nyquistBin = windowSize / 2;

        if (doRefresh)
        {
            const float levels = std::pow (2.0f, bits.load());
            const float jitterNow = jitter.load();

            for (int k = 0; k <= nyquistBin; ++k)
            {
                const float re = fftScratch[(size_t) (2 * k)];
                const float im = fftScratch[(size_t) (2 * k + 1)];
                const float mag = std::sqrt (re * re + im * im);
                const float phase = std::atan2 (im, re);

                // Magnitude quantization: the "bit-starved" digital-crunch half of the codec-
                // artifact character.
                const float normMag = mag / magnitudeScale;
                const float quantizedMag = (std::round (normMag * levels) / levels) * magnitudeScale;

                // Phase jitter: the ingredient that actually gives this a garbled, "lost sync"
                // cellphone-codec character rather than just sounding gritty or muffled -- real
                // low-bitrate speech codecs lose phase coherence between frames this way.
                const float jitteredPhase = phase + jitterNow
                    * (random.nextFloat() * 2.0f - 1.0f) * juce::MathConstants<float>::pi;

                heldWetRe[(size_t) k] = quantizedMag * std::cos (jitteredPhase);
                heldWetIm[(size_t) k] = quantizedMag * std::sin (jitteredPhase);
            }
        }
        // else: not a refresh hop -- heldWetRe/heldWetIm keep whatever they were set to at the
        // last refresh, which is the actual "hold" that produces spectral smear at low refresh
        // rates. The dry side of the blend below is unaffected -- it always reads the current
        // hop's spectrum.

        // Blended per-bin in the complex domain (not by lerping magnitude and phase angle
        // separately, which would misbehave across the phase wrap) -- Mix at 0 is a true bypass
        // every hop regardless of refresh rate, at 1 it's entirely the held quantized+jittered
        // spectrum.
        const float mixNow = mix.load();
        for (int k = 0; k <= nyquistBin; ++k)
        {
            const float re = fftScratch[(size_t) (2 * k)];
            const float im = fftScratch[(size_t) (2 * k + 1)];
            fftScratch[(size_t) (2 * k)] = re * (1.0f - mixNow) + heldWetRe[(size_t) k] * mixNow;
            fftScratch[(size_t) (2 * k + 1)] = im * (1.0f - mixNow) + heldWetIm[(size_t) k] * mixNow;
        }

        fft.performRealOnlyInverseTransform (fftScratch.data());

        for (int i = 0; i < windowSize; ++i)
            accum[(size_t) i] += fftScratch[(size_t) i] * hannWindow[(size_t) i] * olaNormalisation;

        for (int i = 0; i < hopSize; ++i)
            outputFifo.push_back (accum[(size_t) i]);

        std::move (accum.begin() + hopSize, accum.end(), accum.begin());
        std::fill (accum.end() - hopSize, accum.end(), 0.0f);
    }

    float LossyModule::ChannelEngine::processOneSample (float inSample)
    {
        inputRing[(size_t) inputWritePos] = inSample;
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

    void LossyModule::ChannelEngine::process (const float* in, float* out, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            out[i] = processOneSample (in[i]);
    }

    // -- LossyModule --

    LossyModule::LossyModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          bitsParam   (apvtsIn.getRawParameterValue (lossyParamId (slotIndexIn, LossyParam::bits))),
          rateParam   (apvtsIn.getRawParameterValue (lossyParamId (slotIndexIn, LossyParam::rate))),
          jitterParam (apvtsIn.getRawParameterValue (lossyParamId (slotIndexIn, LossyParam::jitter))),
          mixParam    (apvtsIn.getRawParameterValue (lossyParamId (slotIndexIn, LossyParam::mix))),
          outputParam (apvtsIn.getRawParameterValue (lossyParamId (slotIndexIn, LossyParam::output)))
    {
    }

    void LossyModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        for (auto& ch : channels)
            ch.prepare (spec.sampleRate);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
    }

    void LossyModule::reset()
    {
        for (auto& ch : channels)
            ch.reset();
    }

    void LossyModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float bitsOffset = modMatrix.getOffsetForParam (lossyParamId (slotIndex, LossyParam::bits), 4.0f);
        const float bitsValue = juce::jlimit (1.0f, 16.0f, bitsParam->load() + bitsOffset);

        const float rateOffset = modMatrix.getOffsetForParam (lossyParamId (slotIndex, LossyParam::rate), 40.0f);
        const float rateValue = juce::jlimit (1.0f, 200.0f, rateParam->load() + rateOffset);

        const float jitterOffset = modMatrix.getOffsetForParam (lossyParamId (slotIndex, LossyParam::jitter), 0.5f);
        const float jitterValue = juce::jlimit (0.0f, 1.0f, jitterParam->load() + jitterOffset);

        const float mixOffset = modMatrix.getOffsetForParam (lossyParamId (slotIndex, LossyParam::mix), 50.0f);
        const float mixValue = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset);

        const float outputOffset = modMatrix.getOffsetForParam (lossyParamId (slotIndex, LossyParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        for (auto& ch : channels)
        {
            ch.setBits (bitsValue);
            ch.setRefreshHz (rateValue);
            ch.setJitter (jitterValue);
            ch.setMix (mixValue / 100.0f);
        }

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxLossyChannels);
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
