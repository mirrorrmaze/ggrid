#include "IRReshapeWorker.h"

namespace GGrid
{
    void IRReshapeWorker::run()
    {
        while (! threadShouldExit())
        {
            bool didWork = false;

            for (auto& slot : slots)
            {
                const void* identity = nullptr;
                Job job;

                {
                    const juce::SpinLock::ScopedLockType lock (slotLock);
                    if (slot.pending)
                    {
                        identity = slot.identity;
                        job = slot.job;
                        slot.pending = false;
                    }
                }

                if (identity == nullptr)
                    continue;

                didWork = true;

                // The shaping itself (disk read, resample, fade envelope) is the expensive part
                // and stays off the audio thread. The result just sits in the slot until the
                // owning module/voice's process() picks it up and hands it to
                // juce::dsp::Convolution itself -- that call has to happen on the audio thread,
                // so we don't touch anything through `identity` here beyond identity matching.
                juce::AudioBuffer<float> shaped;
                int preFadeOutLength = 0;
                int fadeRampSamples = 0;
                if (IRProcessor::buildShapedIR (job.irIndex, job.sampleRate, job.fadeInMs, job.fadeOutPercent, job.stretch, shaped, preFadeOutLength, fadeRampSamples))
                {
                    const juce::SpinLock::ScopedLockType lock (slotLock);
                    slot.result = std::move (shaped);
                    slot.resultSampleRate = job.sampleRate;
                    slot.resultPreFadeOutLength = preFadeOutLength;
                    slot.resultFadeRampSamples = fadeRampSamples;
                    slot.resultReady = true;
                }

                if (threadShouldExit())
                    return;
            }

            if (! didWork)
                wait (30);
        }
    }
}
