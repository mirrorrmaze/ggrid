#include "IRProcessor.h"
#include "IRLibrary.h"
#include <cmath>

namespace GGrid::IRProcessor
{
    bool buildShapedIR (int irIndex, double sampleRate, float fadeInMs, float fadeOutPercent, float stretch,
                         juce::AudioBuffer<float>& outShapedIR, int& outPreFadeOutLength, int& outFadeRampSamples)
    {
        outPreFadeOutLength = 0;
        outFadeRampSamples = 0;
        juce::AudioBuffer<float> rawIR;
        if (! IRLibrary::loadEntry (irIndex, sampleRate, rawIR))
            return false;

        const int srcChannels = rawIR.getNumChannels();
        const int srcLength = rawIR.getNumSamples();
        if (srcChannels <= 0 || srcLength <= 0)
            return false;

        // Stretch: resample the IR buffer length by `stretch` (0.25x - 4x). This shifts the
        // perceived density/pitch of the tail rather than doing a true time-stretch -- reverb
        // tails are diffuse/unpitched content where that artifact is least perceptible.
        //
        // Growth is capped in absolute duration (not just the 0.25x-4x multiplier) because
        // juce::dsp::Convolution's per-block cost scales with IR length. The cap floor is the
        // source's own natural length, NOT a flat ceiling -- anchoring the ceiling at
        // max(natural length, cap) means Stretch can only ever grow something, never shrink it
        // below what picking that IR at 1x already gives you.
        const int growthCeilingSamples = juce::jmax (srcLength, (int) (kMaxIRGrowthSeconds * sampleRate));
        const int stretchedLength = juce::jmin (growthCeilingSamples, juce::jmax (1, (int) std::round (srcLength * stretch)));
        outShapedIR.setSize (srcChannels, stretchedLength, false, false, true);

        // The plain 4-argument LagrangeInterpolator::process() requires the input to hold at
        // least (speedRatio * numOutputSamplesToProduce) samples -- here that's exactly
        // srcLength, by construction of `ratio`, with zero margin. An earlier version of this
        // function fed it rawIR directly (with a small fixed padding buffer as a safety margin),
        // which still wasn't always enough: the interpolator's own internal lookahead plus
        // floating-point drift in its accumulated sub-sample position (over potentially tens of
        // thousands of iterations at high Stretch ratios) could read past even a padded margin,
        // into whatever heap memory happened to sit beyond it -- garbage (not necessarily NaN/
        // Inf, so not something a finite-value check alone catches), showing up as visible noise
        // near the tail of the stretched waveform and getting convolved into the actual IR audio
        // too. Using the 6-argument overload instead tells the interpolator explicitly that only
        // `srcLength` real samples exist and to feed zeroes once it runs past that
        // (wrapAround = 0) -- this is JUCE's own documented mechanism for exactly this case, so
        // it can never read out of bounds no matter how the sub-sample position drifts, rather
        // than us guessing how much margin is enough.
        const double ratio = (double) srcLength / (double) stretchedLength;
        for (int ch = 0; ch < srcChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process (ratio, rawIR.getReadPointer (ch), outShapedIR.getWritePointer (ch), stretchedLength,
                                   srcLength, 0);
        }

        // Fade in: linear ramp from 0 over fadeInMs at the head of the IR.
        const int fadeInSamples = juce::jlimit (0, stretchedLength, (int) (fadeInMs * 0.001 * sampleRate));
        for (int ch = 0; ch < srcChannels; ++ch)
        {
            auto* data = outShapedIR.getWritePointer (ch);
            for (int i = 0; i < fadeInSamples; ++i)
                data[i] *= (float) i / (float) juce::jmax (1, fadeInSamples);
        }

        outPreFadeOutLength = stretchedLength;

        // Fade out: a decay/length control, like Kilohearts Convolver's -- actually shortens the
        // IR's audible tail rather than tapering a fraction of the existing one in place. 0%
        // keeps the full natural tail; 100% gates it down to a small fraction of its original
        // length. A short fade-to-zero right at the new cut point avoids a click from truncation.
        const float minKeepFraction = 0.02f; // never truncate to literally nothing
        const float keepFraction = 1.0f - (fadeOutPercent * 0.01f) * (1.0f - minKeepFraction);
        const int keptLength = juce::jlimit (64, stretchedLength, (int) std::round (stretchedLength * keepFraction));

        if (keptLength < stretchedLength)
        {
            const int clickFadeSamples = juce::jmin (keptLength, (int) (0.01 * sampleRate)); // ~10ms
            const int fadeStart = keptLength - clickFadeSamples;
            for (int ch = 0; ch < srcChannels; ++ch)
            {
                auto* data = outShapedIR.getWritePointer (ch);
                for (int i = 0; i < clickFadeSamples; ++i)
                {
                    const float g = 1.0f - ((float) i / (float) clickFadeSamples);
                    data[fadeStart + i] *= g;
                }
            }

            outShapedIR.setSize (srcChannels, keptLength, true, false, true); // true = keep existing content
            outFadeRampSamples = clickFadeSamples;
        }

        // Guard against the interpolator's accumulated sub-sample position drifting far enough
        // (over tens of thousands of iterations at extreme Stretch ratios) to read past even the
        // padded source buffer, into real out-of-bounds heap memory -- unlike the padding zone
        // itself (explicitly zeroed), that memory can be anything, including NaN/Inf bit
        // patterns. A NaN/Inf sample baked into the IR itself isn't something the convolution
        // engine can self-heal from: it stays loaded and re-corrupts every block until a
        // *different* IR gets loaded, which reads as the effect going completely silent and only
        // recovering when you switch IRs (not on its own). Scrubbing here means whatever reaches
        // juce::dsp::Convolution::loadImpulseResponse is always finite, regardless of any
        // resampling edge case.
        for (int ch = 0; ch < outShapedIR.getNumChannels(); ++ch)
        {
            auto* data = outShapedIR.getWritePointer (ch);
            for (int i = 0; i < outShapedIR.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    data[i] = 0.0f;
        }

        return true;
    }
}
