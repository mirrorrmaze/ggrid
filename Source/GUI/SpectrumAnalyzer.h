#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

namespace GGrid
{
    // Realtime-safe-enough spectrum analyzer feed, ported from MultibandConvolver's own
    // (D:\Claude Projects\MultibandConvolver\Source\GUI\SpectrumAnalyzer.h): pushSamples() is
    // called from the audio thread, performFFTIfReady()/getMagnitudeDb() from the GUI thread on a
    // timer. Follows the same single-flag-plus-scratch-buffer pattern as JUCE's own spectrum
    // analyzer tutorial -- not a true lock-free FIFO, but sufficient for a visual-only,
    // non-safety-critical display; a slow GUI thread just drops a block rather than blocking or
    // tearing.
    class SpectrumAnalyzer
    {
    public:
        static constexpr int fftOrder = 11;
        static constexpr int fftSize = 1 << fftOrder;
        static constexpr int numBins = fftSize / 2;

        SpectrumAnalyzer() : forwardFFT (fftOrder), window (fftSize, juce::dsp::WindowingFunction<float>::hann)
        {
            magnitudesDb.fill (-100.0f);
        }

        // Set from prepare() -- GGrid modules can be prepared at any host sample rate, unlike
        // MultibandConvolver's fixed-plugin-instance original, so this isn't a compile-time
        // constant; the GUI reads it back via getSampleRate() to convert bin index -> Hz.
        void setSampleRate (double sr) { sampleRate = sr; }
        double getSampleRate() const { return sampleRate; }

        // Audio thread. Mixes multi-channel input down to mono before feeding the FFT.
        void pushSamples (const juce::AudioBuffer<float>& buffer)
        {
            const int numCh = buffer.getNumChannels();
            const int numSamples = buffer.getNumSamples();
            if (numCh == 0)
                return;

            for (int i = 0; i < numSamples; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                    sum += buffer.getSample (ch, i);
                pushNextSampleIntoFifo (sum / (float) numCh);
            }
        }

        // GUI thread, call once per repaint/timer tick.
        void performFFTIfReady()
        {
            if (! nextBlockReady.load())
                return;

            std::array<float, fftSize * 2> fftData {};
            std::copy (snapshotBuffer.begin(), snapshotBuffer.end(), fftData.begin());
            nextBlockReady = false;

            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

            constexpr float floorDb = -100.0f;
            for (int i = 0; i < numBins; ++i)
            {
                const float magnitude = fftData[(size_t) i] / (float) fftSize;
                magnitudesDb[(size_t) i] = juce::Decibels::gainToDecibels (magnitude, floorDb);
            }
        }

        // GUI thread. binIndex in [0, numBins). Returns dB, floored at -100.
        float getMagnitudeDb (int binIndex) const
        {
            return magnitudesDb[(size_t) juce::jlimit (0, numBins - 1, binIndex)];
        }

    private:
        void pushNextSampleIntoFifo (float sample)
        {
            if (fifoIndex == fftSize)
            {
                // Snapshot the completed block for the GUI thread to read, independent of
                // whatever we write into fifoBuffer next -- avoids the GUI thread racing a
                // partially-refilled buffer if it hasn't consumed the previous block yet.
                if (! nextBlockReady.load())
                {
                    snapshotBuffer = fifoBuffer;
                    nextBlockReady = true;
                }
                fifoIndex = 0;
            }

            fifoBuffer[(size_t) fifoIndex++] = sample;
        }

        juce::dsp::FFT forwardFFT;
        juce::dsp::WindowingFunction<float> window;

        std::array<float, fftSize> fifoBuffer {};
        std::array<float, fftSize> snapshotBuffer {};
        std::array<float, numBins> magnitudesDb {};
        std::atomic<bool> nextBlockReady { false };
        int fifoIndex = 0;

        double sampleRate = 44100.0;
    };
}
