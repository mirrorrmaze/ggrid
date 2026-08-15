#include "IRProcessor.h"
#include "IRLibrary.h"

namespace GGrid::IRProcessor
{
    bool buildShapedIR (int irIndex, double sampleRate, float fadeInMs, float fadeOutPercent, float stretch,
                         juce::AudioBuffer<float>& outShapedIR, int& outPreFadeOutLength)
    {
        outPreFadeOutLength = 0;
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
        constexpr double maxGrowthSeconds = 8.0;
        const int growthCeilingSamples = juce::jmax (srcLength, (int) (maxGrowthSeconds * sampleRate));
        const int stretchedLength = juce::jmin (growthCeilingSamples, juce::jmax (1, (int) std::round (srcLength * stretch)));
        outShapedIR.setSize (srcChannels, stretchedLength, false, false, true);

        const double ratio = (double) srcLength / (double) stretchedLength;
        for (int ch = 0; ch < srcChannels; ++ch)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.reset();
            interpolator.process (ratio, rawIR.getReadPointer (ch), outShapedIR.getWritePointer (ch), stretchedLength);
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
        }

        return true;
    }
}
