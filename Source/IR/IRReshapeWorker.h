#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "IRProcessor.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    // Ported from MultibandConvolver's IRReshapeWorker (D:\Claude Projects\MultibandConvolver\
    // Source\DSP\IRReshapeWorker.h), adapted for a generic identity key instead of BandChain*.
    //
    // Shared background thread that does the actual IR reshaping (disk read + resample + fade
    // envelope) for every IR-driven module/voice in the rack, so the audio thread never blocks on
    // it. The audio thread only ever calls requestReshape(), which just stashes a small POD job
    // in a per-caller slot and returns -- the real work happens here instead.
    //
    // One fixed slot per caller, matched by an arbitrary `const void*` identity key -- any object
    // with a stable address for its lifetime works (a whole ConvolutionModule, or one band's
    // sub-voice inside MultibandConvolutionModule), since the pointer is only ever compared for
    // identity here, never dereferenced. A new request for a key that already has one pending
    // simply overwrites it, so rapid changes (e.g. dragging Stretch) never build an unbounded
    // backlog.
    class IRReshapeWorker : private juce::Thread
    {
    public:
        IRReshapeWorker() : juce::Thread ("IR Reshape")
        {
            startThread (juce::Thread::Priority::background);
        }

        ~IRReshapeWorker() override
        {
            shutdown();
        }

        void shutdown()
        {
            stopThread (3000);
        }

        struct Job
        {
            int irIndex = 0;
            double sampleRate = 44100.0;
            float fadeInMs = 0.0f;
            float fadeOutPercent = 0.0f;
            float stretch = 1.0f;
        };

        // Audio thread. Cheap: acquires a spin lock just long enough to copy a few POD fields.
        void requestReshape (const void* identity, const Job& job)
        {
            {
                const juce::SpinLock::ScopedLockType lock (slotLock);
                Slot* freeSlot = nullptr;
                for (auto& slot : slots)
                {
                    if (slot.identity == identity) { slot.job = job; slot.pending = true; notify(); return; }
                    if (freeSlot == nullptr && slot.identity == nullptr)
                        freeSlot = &slot;
                }
                if (freeSlot != nullptr)
                {
                    freeSlot->identity = identity;
                    freeSlot->job = job;
                    freeSlot->pending = true;
                }
            }
            notify();
        }

        // Audio thread only. If the background thread has finished shaping an IR for `identity`,
        // moves it into outBuffer/outSampleRate and returns true (consuming it).
        // outPreFadeOutLength is outBuffer's length before Fade Out's truncation, and
        // outFadeRampSamples is the length of the declick ramp actually applied at the cut point
        // (0 if none) -- see IRProcessor::buildShapedIR's comment for why callers displaying the
        // waveform need both.
        bool tryTakeResult (const void* identity, juce::AudioBuffer<float>& outBuffer, double& outSampleRate,
                             int& outPreFadeOutLength, int& outFadeRampSamples)
        {
            const juce::SpinLock::ScopedLockType lock (slotLock);
            for (auto& slot : slots)
            {
                if (slot.identity == identity && slot.resultReady)
                {
                    outBuffer = std::move (slot.result);
                    outSampleRate = slot.resultSampleRate;
                    outPreFadeOutLength = slot.resultPreFadeOutLength;
                    outFadeRampSamples = slot.resultFadeRampSamples;
                    slot.resultReady = false;
                    return true;
                }
            }
            return false;
        }

    private:
        struct Slot
        {
            const void* identity = nullptr;
            Job job;
            bool pending = false;

            juce::AudioBuffer<float> result;
            double resultSampleRate = 44100.0;
            int resultPreFadeOutLength = 0;
            int resultFadeRampSamples = 0;
            bool resultReady = false;
        };
        // Sized beyond kMaxSlots -- a single MultibandConvolutionModule needs one slot per band
        // (kNumConvolutionBands), not just one for the whole module, so worst-case demand is
        // higher than "one per rack slot" now.
        std::array<Slot, (size_t) (kMaxSlots * 4)> slots;
        juce::SpinLock slotLock;

        void run() override;
    };
}
