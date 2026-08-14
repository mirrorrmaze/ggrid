#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include "IRProcessor.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    class ConvolutionModule; // see ConvolutionModule.h -- only needs pointer identity here

    // Ported from MultibandConvolver's IRReshapeWorker (D:\Claude Projects\MultibandConvolver\
    // Source\DSP\IRReshapeWorker.h), adapted for ConvolutionModule* identity instead of
    // BandChain* and kMaxSlots instead of Params::maxBands.
    //
    // Shared background thread that does the actual IR reshaping (disk read + resample + fade
    // envelope) for every Convolution module in the rack, so the audio thread never blocks on
    // it. The audio thread only ever calls requestReshape(), which just stashes a small POD job
    // in a per-module slot and returns -- the real work happens here instead.
    //
    // One fixed slot per module (matched by ConvolutionModule pointer identity): a new request
    // for a module that already has one pending simply overwrites it, so rapid changes (e.g.
    // dragging Stretch) never build an unbounded backlog.
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
        void requestReshape (ConvolutionModule& targetModule, const Job& job)
        {
            {
                const juce::SpinLock::ScopedLockType lock (slotLock);
                Slot* freeSlot = nullptr;
                for (auto& slot : slots)
                {
                    if (slot.module == &targetModule) { slot.job = job; slot.pending = true; notify(); return; }
                    if (freeSlot == nullptr && slot.module == nullptr)
                        freeSlot = &slot;
                }
                if (freeSlot != nullptr)
                {
                    freeSlot->module = &targetModule;
                    freeSlot->job = job;
                    freeSlot->pending = true;
                }
            }
            notify();
        }

        // Audio thread only. If the background thread has finished shaping an IR for
        // `targetModule`, moves it into outBuffer/outSampleRate and returns true (consuming it).
        bool tryTakeResult (ConvolutionModule& targetModule, juce::AudioBuffer<float>& outBuffer, double& outSampleRate)
        {
            const juce::SpinLock::ScopedLockType lock (slotLock);
            for (auto& slot : slots)
            {
                if (slot.module == &targetModule && slot.resultReady)
                {
                    outBuffer = std::move (slot.result);
                    outSampleRate = slot.resultSampleRate;
                    slot.resultReady = false;
                    return true;
                }
            }
            return false;
        }

    private:
        struct Slot
        {
            ConvolutionModule* module = nullptr;
            Job job;
            bool pending = false;

            juce::AudioBuffer<float> result;
            double resultSampleRate = 44100.0;
            bool resultReady = false;
        };
        std::array<Slot, (size_t) kMaxSlots> slots;
        juce::SpinLock slotLock;

        void run() override;
    };
}
