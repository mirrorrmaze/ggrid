#include "Params/ParameterLayout.h"
#include "Params/Identifiers.h"
#include "Modules/WaveshaperModule.h"
#include "Modules/FilterModule.h"
#include "Modules/DelayModule.h"
#include "Modules/DynamicsModule.h"
#include "Modules/ConvolutionModule.h"
#include "Modulation/ModulationMatrix.h"
#include "Rack/RackSlot.h"
#include "Rack/ConnectionGraph.h"
#include "IR/IRLibrary.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <cmath>

using namespace GGrid;

namespace
{
    // Minimal do-nothing AudioProcessor, just so AudioProcessorValueTreeState has something to
    // attach to -- this harness never goes through a real host or plugin wrapper.
    struct DummyProcessor : public juce::AudioProcessor
    {
        DummyProcessor()
            : juce::AudioProcessor (BusesProperties()
                                       .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
        {
        }

        const juce::String getName() const override { return "OfflineDspTestHost"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
    };

    int failures = 0;

    void expect (bool condition, const juce::String& description)
    {
        if (! condition)
        {
            std::cout << "FAIL: " << description << std::endl;
            ++failures;
        }
        else
        {
            std::cout << "pass: " << description << std::endl;
        }
    }

    juce::AudioBuffer<float> makeTestSignal (int numSamples, float amplitude, float freqHz, double sampleRate)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                data[i] = amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * (double) freqHz * (double) i / sampleRate);
        }
        return buffer;
    }

    bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    return false;
        }
        return true;
    }

    bool isFiniteAndBounded (const juce::AudioBuffer<float>& buffer, float bound)
    {
        if (! isFinite (buffer))
            return false;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (std::abs (data[i]) > bound)
                    return false;
        }
        return true;
    }

    double rms (const juce::AudioBuffer<float>& buffer, int channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        double sumSq = 0.0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            sumSq += (double) data[i] * (double) data[i];
        return std::sqrt (sumSq / buffer.getNumSamples());
    }
}

int main()
{
    DummyProcessor dummy;
    juce::AudioProcessorValueTreeState apvts (dummy, nullptr, "PARAMETERS", createParameterLayout());

    // All routes default to Source=None/Destination=None, so this contributes zero offset
    // everywhere until a test explicitly sets up a route -- safe to pass into every module
    // below without affecting their existing assertions.
    ModulationMatrix modMatrix (apvts);

    juce::dsp::ConvolutionMessageQueue convolutionQueue;
    IRReshapeWorker irReshapeWorker;
    std::atomic<double> testBpm { 120.0 };
    SharedServices sharedServices { convolutionQueue, irReshapeWorker, testBpm };

    const double sampleRate = 44100.0;
    const int blockSize = 512;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) blockSize;
    spec.numChannels = 2;

    // --- Every shape type stays finite and bounded, even at max drive + max oversampling ---
    for (int shapeIndex = 0; shapeIndex < getWaveshaperShapeChoices().size(); ++shapeIndex)
    {
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::drive))->store (40.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::shape))->store ((float) shapeIndex);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::symmetry))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::foldAmount))->store (1.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::oversample))->store (2.0f); // 4x
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::output))->store (0.0f);

        WaveshaperModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 1.5f),
                "shape " + juce::String (shapeIndex) + " (" + getWaveshaperShapeChoices()[shapeIndex]
                    + ") stays finite/bounded at max drive");
    }

    // --- Foldback wavefolder actually folds back into range rather than flat-clipping ---
    {
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::drive))->store (30.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::shape))->store (3.0f); // Foldback Wavefolder
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::symmetry))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::foldAmount))->store (1.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::oversample))->store (0.0f); // Off, check raw math
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::output))->store (0.0f);

        WaveshaperModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 1.0f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        // A hard clipper driven this hard would spend most of its time pinned at +-1.0. A
        // wavefolder should not -- it folds back down, so plenty of samples should land
        // meaningfully below the ceiling.
        int samplesBelowCeiling = 0;
        const auto* data = buffer.getReadPointer (0);
        for (int i = 0; i < blockSize; ++i)
            if (std::abs (data[i]) < 0.9f)
                ++samplesBelowCeiling;

        expect (samplesBelowCeiling > blockSize / 4,
                "foldback wavefolder folds back into range instead of flat-clipping ("
                    + juce::String (samplesBelowCeiling) + "/" + juce::String (blockSize) + " samples below ceiling)");
    }

    // --- Chain order changes the result ---
    {
        RackSlot slotA (apvts, 0, sharedServices);
        RackSlot slotB (apvts, 1, sharedServices);

        apvts.getRawParameterValue (slotTypeParamId (0))->store (1.0f); // Waveshaper
        apvts.getRawParameterValue (slotTypeParamId (1))->store (1.0f); // Waveshaper
        apvts.getRawParameterValue (slotBypassParamId (0))->store (0.0f);
        apvts.getRawParameterValue (slotBypassParamId (1))->store (0.0f);

        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::shape))->store (0.0f); // Hard Clip
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::drive))->store (24.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::oversample))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::output))->store (-6.0f);

        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::shape))->store (1.0f); // Soft Clip tanh
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::drive))->store (12.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::oversample))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::output))->store (0.0f);

        slotA.prepare (spec);
        slotB.prepare (spec);

        auto bufferAB = makeTestSignal (blockSize, 0.8f, 220.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (bufferAB);
            juce::MidiBuffer midi;
            slotA.process (block, midi, modMatrix);
            slotB.process (block, midi, modMatrix);
        }

        slotA.reset();
        slotB.reset();

        auto bufferBA = makeTestSignal (blockSize, 0.8f, 220.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (bufferBA);
            juce::MidiBuffer midi;
            slotB.process (block, midi, modMatrix);
            slotA.process (block, midi, modMatrix);
        }

        bool differs = false;
        for (int i = 0; i < blockSize && ! differs; ++i)
            if (std::abs (bufferAB.getSample (0, i) - bufferBA.getSample (0, i)) > 1.0e-6f)
                differs = true;

        expect (differs, "processing slot A before B gives a different result than B before A");
    }

    // --- Filter: Low Pass attenuates content above cutoff ---
    {
        apvts.getRawParameterValue (filterParamId (0, FilterParam::type))->store (0.0f); // Low Pass
        apvts.getRawParameterValue (filterParamId (0, FilterParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::resonance))->store (0.707f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::output))->store (0.0f);

        FilterModule module (apvts, 0);
        module.prepare (spec);

        auto lowToneBuffer = makeTestSignal (blockSize, 0.5f, 100.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (lowToneBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        module.reset();

        auto highToneBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (highToneBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        const double lowRms = rms (lowToneBuffer, 0);
        const double highRms = rms (highToneBuffer, 0);

        expect (highRms < lowRms * 0.3,
                "Low Pass filter attenuates an 8kHz tone well below a 100Hz tone through a 300Hz cutoff (low RMS "
                    + juce::String (lowRms, 4) + ", high RMS " + juce::String (highRms, 4) + ")");
    }

    // --- Filter: every algorithm stays stable at extreme resonance/feedback ---
    for (int filterType = 0; filterType < getFilterTypeChoices().size(); ++filterType)
    {
        apvts.getRawParameterValue (filterParamId (0, FilterParam::type))->store ((float) filterType);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::frequency))->store (500.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::resonance))->store (18.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::feedback))->store (0.95f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::output))->store (0.0f);

        FilterModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        if (filterType <= 3)
        {
            // Biquad SVF modes: a sharp resonant peak can legitimately amplify a tone driven
            // right at the resonant frequency well past unity -- that's the filter doing its
            // job, not a bug -- so only check for runaway (NaN/Inf), not a tight bound.
            expect (isFinite (buffer),
                    "filter type " + juce::String (filterType) + " (" + getFilterTypeChoices()[filterType]
                        + ") stays finite (no NaN/Inf) at high resonance");
        }
        else
        {
            // Delay-line based modes: |feedback| < 1 guarantees a stable, bounded result.
            expect (isFiniteAndBounded (buffer, 3.0f),
                    "filter type " + juce::String (filterType) + " (" + getFilterTypeChoices()[filterType]
                        + ") stays finite/bounded at feedback 0.95");
        }
    }

    // --- Delay: echo lands at the expected sample offset ---
    {
        apvts.getRawParameterValue (delayParamId (0, DelayParam::time))->store (10.0f); // 10ms
        apvts.getRawParameterValue (delayParamId (0, DelayParam::feedback))->store (0.0f); // single echo only
        apvts.getRawParameterValue (delayParamId (0, DelayParam::saturation))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::mix))->store (100.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::output))->store (0.0f);

        DelayModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f); // impulse at sample 0

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const int expectedDelaySamples = (int) std::round (10.0 * 0.001 * sampleRate);
        int peakIndex = -1;
        float peakValue = 0.0f;
        const auto* data = buffer.getReadPointer (0);
        for (int i = 0; i < blockSize; ++i)
        {
            if (std::abs (data[i]) > peakValue)
            {
                peakValue = std::abs (data[i]);
                peakIndex = i;
            }
        }

        expect (peakIndex >= 0 && std::abs (peakIndex - expectedDelaySamples) <= 2,
                "delay echo lands at the expected sample offset for a 10ms delay time (expected ~"
                    + juce::String (expectedDelaySamples) + ", got " + juce::String (peakIndex) + ")");
    }

    // --- Delay: feedback saturation keeps a hot feedback loop from spiraling ---
    {
        apvts.getRawParameterValue (delayParamId (0, DelayParam::time))->store (5.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::feedback))->store (0.98f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::saturation))->store (1.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::mix))->store (100.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::output))->store (0.0f);

        DelayModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 1.5f),
                "delay with feedback 0.98 and full saturation stays finite/bounded");
    }

    // --- Delay: tempo sync computes the expected note-division time from host BPM ---
    {
        apvts.getRawParameterValue (delayParamId (0, DelayParam::sync))->store (1.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::division))->store (2.0f); // "1/4"
        apvts.getRawParameterValue (delayParamId (0, DelayParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::saturation))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::pingPong))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::mix))->store (100.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::output))->store (0.0f);

        sharedServices.hostBpm.store (960.0); // quarter note = 62.5ms

        DelayModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        const int numChunks = 8; // 8*512 = 4096 samples (~93ms @ 44.1kHz), plenty for a 62.5ms echo
        juce::AudioBuffer<float> fullBuffer (2, blockSize * numChunks);
        fullBuffer.clear();
        fullBuffer.setSample (0, 0, 1.0f); // impulse at sample 0

        juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        const int expectedDelaySamples = (int) std::round (62.5 * 0.001 * sampleRate);
        int peakIndex = -1;
        float peakValue = 0.0f;
        const auto* data = fullBuffer.getReadPointer (0);
        for (int i = 0; i < fullBuffer.getNumSamples(); ++i)
        {
            if (std::abs (data[i]) > peakValue)
            {
                peakValue = std::abs (data[i]);
                peakIndex = i;
            }
        }

        expect (peakIndex >= 0 && std::abs (peakIndex - expectedDelaySamples) <= (int) (0.02 * sampleRate),
                "tempo-synced delay (1/4 @ 960 BPM) lands near the expected sample offset (expected ~"
                    + juce::String (expectedDelaySamples) + ", got " + juce::String (peakIndex) + ")");
    }

    // --- Delay: ping-pong cross-feeds channels; without it, one channel's signal stays put ---
    {
        apvts.getRawParameterValue (delayParamId (0, DelayParam::sync))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::time))->store (5.0f); // short, so several echoes fit in the test buffer
        apvts.getRawParameterValue (delayParamId (0, DelayParam::feedback))->store (0.7f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::saturation))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::mix))->store (100.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::output))->store (0.0f);

        auto runImpulseTest = [&] (bool pingPong) -> double
        {
            apvts.getRawParameterValue (delayParamId (0, DelayParam::pingPong))->store (pingPong ? 1.0f : 0.0f);

            DelayModule module (apvts, 0, sharedServices);
            module.prepare (spec);

            const int numChunks = 8;
            juce::AudioBuffer<float> buffer (2, blockSize * numChunks);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f); // impulse on left channel only

            juce::dsp::AudioBlock<float> fullBlock (buffer);
            for (int chunk = 0; chunk < numChunks; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }

            return rms (buffer, 1); // right channel energy
        };

        const double rightRmsWithoutPingPong = runImpulseTest (false);
        const double rightRmsWithPingPong = runImpulseTest (true);

        expect (rightRmsWithoutPingPong < 1.0e-6, "without ping-pong, an impulse on the left channel never appears on the right");
        expect (rightRmsWithPingPong > 1.0e-4, "with ping-pong, an impulse on the left channel bounces into the right channel");
    }

    // --- Delay: Low Cut/Hi Cut in the feedback path progressively attenuates later repeats ---
    {
        apvts.getRawParameterValue (delayParamId (0, DelayParam::sync))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::time))->store (5.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::feedback))->store (0.85f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::saturation))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::pingPong))->store (0.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::mix))->store (100.0f);
        apvts.getRawParameterValue (delayParamId (0, DelayParam::output))->store (0.0f);

        auto runImpulseTailRms = [&] (float lowCutHz, float hiCutHz) -> double
        {
            apvts.getRawParameterValue (delayParamId (0, DelayParam::lowCut))->store (lowCutHz);
            apvts.getRawParameterValue (delayParamId (0, DelayParam::hiCut))->store (hiCutHz);

            DelayModule module (apvts, 0, sharedServices);
            module.prepare (spec);

            const int numChunks = 40; // 40*512 = 20480 samples (~464ms @ 44.1kHz), many 5ms-spaced repeats
            juce::AudioBuffer<float> buffer (2, blockSize * numChunks);
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);

            juce::dsp::AudioBlock<float> fullBlock (buffer);
            for (int chunk = 0; chunk < numChunks; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }

            // Tail = well past the first several echoes, where filtering has had many passes to compound.
            juce::AudioBuffer<float> tail (1, blockSize * 10);
            tail.copyFrom (0, 0, buffer, 0, blockSize * 30, blockSize * 10);
            return rms (tail, 0);
        };

        const double openRms = runImpulseTailRms (20.0f, 20000.0f);
        const double narrowRms = runImpulseTailRms (300.0f, 2000.0f);

        expect (narrowRms < openRms * 0.5,
                "Low Cut/Hi Cut in the feedback path progressively attenuates repeats more than wide-open filters (open tail RMS "
                    + juce::String (openRms, 9) + ", narrow tail RMS " + juce::String (narrowRms, 9) + ")");
    }

    // --- Dynamics: compressor reduces steady-state level of a signal driven above threshold ---
    {
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::threshold))->store (-20.0f);
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::ratio))->store (8.0f);
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::attack))->store (1.0f);
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::release))->store (50.0f);
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::makeup))->store (0.0f);
        apvts.getRawParameterValue (dynamicsParamId (0, DynamicsParam::mix))->store (100.0f);

        DynamicsModule module (apvts, 0);
        module.prepare (spec);

        const int numChunks = 8;
        auto fullBuffer = makeTestSignal (blockSize * numChunks, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> fullBlock (fullBuffer);

        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        // Measure RMS over just the final chunk, after the envelope has settled.
        juce::AudioBuffer<float> lastChunk (1, blockSize);
        lastChunk.copyFrom (0, 0, fullBuffer, 0, blockSize * (numChunks - 1), blockSize);

        const double outputRms = rms (lastChunk, 0);
        const double expectedUncompressedRms = 0.9 * std::sqrt (0.5);

        expect (outputRms < expectedUncompressedRms * 0.85,
                "compressor reduces steady-state level of a signal driven well above threshold (uncompressed RMS ~"
                    + juce::String (expectedUncompressedRms, 4) + ", compressed RMS " + juce::String (outputRms, 4) + ")");
    }

    // --- IRLibrary: factory catalog resolves and loads correctly ---
    {
        expect ((int) IRLibrary::getCatalog().size() >= IRLibrary::factoryCatalogSize,
                "IR factory catalog has at least " + juce::String (IRLibrary::factoryCatalogSize) + " entries");

        auto root = IRLibrary::resolveIRRoot();
        expect (root.isDirectory(), "IR root resolves to a real directory (" + root.getFullPathName() + ")");

        juce::AudioBuffer<float> irBuffer;
        const bool loaded = IRLibrary::loadEntry (0, sampleRate, irBuffer);
        expect (loaded && irBuffer.getNumSamples() > 0, "IRLibrary::loadEntry loads factory entry 0 successfully");
    }

    // --- Convolution: picking a catalog IR (background reshape) actually changes the output ---
    {
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::irIndex))->store (0.0f); // first factory IR
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::tone))->store (0.0f);
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::fadeIn))->store (0.0f);
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::fadeOut))->store (0.0f);
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::stretch))->store (1.0f);
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::mix))->store (100.0f);
        apvts.getRawParameterValue (convolutionParamId (0, ConvolutionParam::output))->store (0.0f);

        ConvolutionModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        // The debounced reshape request (~200ms) fires from inside process(), and the actual
        // disk read/resample happens on the shared background worker -- then loadImpulseResponse
        // itself finishes its FFT precompute on JUCE's own background thread. Keep processing
        // fresh blocks, with a short real-time wait between attempts, until it audibly kicks in
        // (or we give up after ~2s, which would indicate a real bug).
        juce::AudioBuffer<float> lastBuffer;
        bool differs = false;
        for (int attempt = 0; attempt < 200 && ! differs; ++attempt)
        {
            juce::Thread::sleep (10);

            auto dryReference = makeTestSignal (blockSize, 0.5f, 220.0f, sampleRate);
            lastBuffer = makeTestSignal (blockSize, 0.5f, 220.0f, sampleRate);
            juce::dsp::AudioBlock<float> block (lastBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);

            for (int i = 0; i < blockSize && ! differs; ++i)
                if (std::abs (dryReference.getSample (0, i) - lastBuffer.getSample (0, i)) > 1.0e-6f)
                    differs = true;
        }

        expect (isFiniteAndBounded (lastBuffer, 4.0f), "convolution with a catalog IR loaded stays finite/bounded");
        expect (differs, "selecting a catalog IR actually changes the output (debounce + background reshape completes within ~2s)");
    }

    // --- Mod matrix: a note-pitch -> filter frequency route actually shifts the cutoff ---
    {
        // Route 0: Note Pitch -> Slot 0 Filter Frequency, full positive depth.
        apvts.getRawParameterValue (modRouteSourceParamId (0))->store ((float) ModSource::notePitch);
        apvts.getRawParameterValue (modRouteDestinationParamId (0))->store (
            (float) (modDestinationIndex (0, ModDestinationParam::filterFrequency) + 1));
        apvts.getRawParameterValue (modRouteDepthParamId (0))->store (1.0f);

        apvts.getRawParameterValue (filterParamId (0, FilterParam::type))->store (0.0f); // Low Pass
        apvts.getRawParameterValue (filterParamId (0, FilterParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::resonance))->store (0.707f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::output))->store (0.0f);

        ModulationMatrix routedMatrix (apvts);

        FilterModule moduleLowNote (apvts, 0);
        moduleLowNote.prepare (spec);
        {
            juce::MidiBuffer noteMidi;
            noteMidi.addEvent (juce::MidiMessage::noteOn (1, 0, (juce::uint8) 100), 0);
            routedMatrix.processMidi (noteMidi);
        }
        auto lowNoteBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (lowNoteBuffer);
            juce::MidiBuffer midi;
            moduleLowNote.process (block, midi, routedMatrix);
        }

        FilterModule moduleHighNote (apvts, 0);
        moduleHighNote.prepare (spec);
        {
            juce::MidiBuffer noteMidi;
            noteMidi.addEvent (juce::MidiMessage::noteOn (1, 127, (juce::uint8) 100), 0);
            routedMatrix.processMidi (noteMidi);
        }
        auto highNoteBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (highNoteBuffer);
            juce::MidiBuffer midi;
            moduleHighNote.process (block, midi, routedMatrix);
        }

        const double lowNoteRms = rms (lowNoteBuffer, 0);
        const double highNoteRms = rms (highNoteBuffer, 0);

        expect (highNoteRms > lowNoteRms * 1.5,
                "mod matrix note-pitch route raises the filter cutoff for a higher note, passing more 8kHz energy through (low-note RMS "
                    + juce::String (lowNoteRms, 4) + ", high-note RMS " + juce::String (highNoteRms, 4) + ")");
    }

    // --- ConnectionGraph: a diamond (0 -> 1, 0 -> 2, 1 -> 3, 2 -> 3) topologically orders roots/sinks correctly ---
    {
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 0, 1 };
        conns[1] = { 0, 2 };
        conns[2] = { 1, 3 };
        conns[3] = { 2, 3 };

        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[2] = active[3] = true;

        const auto graph = buildProcessingOrder (conns, 4, active);

        expect (graph.orderCount == 4, "diamond graph orders all 4 active nodes");
        expect (graph.isRoot[0] && ! graph.isRoot[1] && ! graph.isRoot[2] && ! graph.isRoot[3],
                "diamond graph: only slot 0 (no predecessors) is a root");
        expect (! graph.isSink[0] && ! graph.isSink[1] && ! graph.isSink[2] && graph.isSink[3],
                "diamond graph: only slot 3 (no successors) is a sink");

        auto positionOf = [&] (int slot) -> int
        {
            for (int i = 0; i < graph.orderCount; ++i)
                if (graph.order[(size_t) i] == slot)
                    return i;
            return -1;
        };

        expect (positionOf (0) < positionOf (1) && positionOf (0) < positionOf (2),
                "diamond graph: root 0 is ordered before both of its successors");
        expect (positionOf (1) < positionOf (3) && positionOf (2) < positionOf (3),
                "diamond graph: sink 3 is ordered after both of its predecessors");
    }

    // --- ConnectionGraph: a cycle (defensive fallback) still includes every active node exactly once ---
    {
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 0, 1 };
        conns[1] = { 1, 0 };

        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = true;

        const auto graph = buildProcessingOrder (conns, 2, active);

        expect (graph.orderCount == 2, "a 2-node cycle still produces an order containing both nodes (defensive fallback), not a hang or a drop");
    }

    // --- End-to-end graph: fan-out from one root into two parallel branches, summed back at the sinks ---
    {
        RackSlot slotRoot (apvts, 0, sharedServices);
        RackSlot slotBranchA (apvts, 1, sharedServices);
        RackSlot slotBranchB (apvts, 2, sharedServices);

        apvts.getRawParameterValue (slotTypeParamId (0))->store (1.0f); // Waveshaper
        apvts.getRawParameterValue (slotTypeParamId (1))->store (1.0f); // Waveshaper
        apvts.getRawParameterValue (slotTypeParamId (2))->store (4.0f); // Dynamics
        apvts.getRawParameterValue (slotBypassParamId (0))->store (0.0f);
        apvts.getRawParameterValue (slotBypassParamId (1))->store (0.0f);
        apvts.getRawParameterValue (slotBypassParamId (2))->store (0.0f);

        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::shape))->store (0.0f); // Hard Clip
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::drive))->store (18.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::oversample))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (0, WaveshaperParam::output))->store (-6.0f);

        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::shape))->store (1.0f); // Soft Clip tanh
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::drive))->store (9.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::oversample))->store (0.0f);
        apvts.getRawParameterValue (waveshaperParamId (1, WaveshaperParam::output))->store (0.0f);

        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::threshold))->store (-18.0f);
        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::ratio))->store (4.0f);
        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::attack))->store (1.0f);
        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::release))->store (50.0f);
        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::makeup))->store (0.0f);
        apvts.getRawParameterValue (dynamicsParamId (2, DynamicsParam::mix))->store (100.0f);

        slotRoot.prepare (spec);
        slotBranchA.prepare (spec);
        slotBranchB.prepare (spec);

        auto dry = makeTestSignal (blockSize, 0.7f, 220.0f, sampleRate);

        // Hand-computed expected result: root's output feeds BOTH branches independently (fan-out,
        // not a shared running buffer), and the branches' outputs are summed (merge at the sinks) --
        // not chained into each other.
        auto stageRoot = dry;
        {
            juce::dsp::AudioBlock<float> block (stageRoot);
            juce::MidiBuffer midi;
            slotRoot.process (block, midi, modMatrix);
        }

        auto expectedA = stageRoot;
        {
            juce::dsp::AudioBlock<float> block (expectedA);
            juce::MidiBuffer midi;
            slotBranchA.process (block, midi, modMatrix);
        }

        auto expectedB = stageRoot;
        {
            juce::dsp::AudioBlock<float> block (expectedB);
            juce::MidiBuffer midi;
            slotBranchB.process (block, midi, modMatrix);
        }

        juce::AudioBuffer<float> expectedSum (2, blockSize);
        expectedSum.makeCopyOf (expectedA);
        for (int ch = 0; ch < 2; ++ch)
            expectedSum.addFrom (ch, 0, expectedB, ch, 0, blockSize);

        // Now reset and re-run the same 3 slots through the actual graph algorithm/pull-style
        // summation, exactly as GGridAudioProcessor::processBlock does, to confirm it produces
        // the same result as the hand-computed expectation above.
        slotRoot.reset();
        slotBranchA.reset();
        slotBranchB.reset();

        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 0, 1 };
        conns[1] = { 0, 2 };
        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[2] = true;
        const auto graph = buildProcessingOrder (conns, 2, active);

        std::array<juce::AudioBuffer<float>, kMaxSlots> nodeBuffers;
        for (int i = 0; i < kMaxSlots; ++i)
            if (active[(size_t) i])
                nodeBuffers[(size_t) i].setSize (2, blockSize);

        RackSlot* rackSlotsByIndex[3] = { &slotRoot, &slotBranchA, &slotBranchB };

        for (int idx = 0; idx < graph.orderCount; ++idx)
        {
            const int i = graph.order[(size_t) idx];
            if (graph.isRoot[(size_t) i])
            {
                for (int ch = 0; ch < 2; ++ch)
                    nodeBuffers[(size_t) i].copyFrom (ch, 0, dry, ch, 0, blockSize);
            }
            else
            {
                for (int c = 0; c < 2; ++c)
                {
                    if (conns[(size_t) c].to != i) continue;
                    for (int ch = 0; ch < 2; ++ch)
                        nodeBuffers[(size_t) i].addFrom (ch, 0, nodeBuffers[(size_t) conns[(size_t) c].from], ch, 0, blockSize);
                }
            }

            juce::dsp::AudioBlock<float> nodeBlock (nodeBuffers[(size_t) i]);
            juce::MidiBuffer midi;
            rackSlotsByIndex[i]->process (nodeBlock, midi, modMatrix);
        }

        juce::AudioBuffer<float> actualSum (2, blockSize);
        actualSum.clear();
        for (int i = 0; i < kMaxSlots; ++i)
            if (graph.isSink[(size_t) i])
                for (int ch = 0; ch < 2; ++ch)
                    actualSum.addFrom (ch, 0, nodeBuffers[(size_t) i], ch, 0, blockSize);

        bool matches = true;
        for (int ch = 0; ch < 2 && matches; ++ch)
            for (int i = 0; i < blockSize && matches; ++i)
                if (std::abs (expectedSum.getSample (ch, i) - actualSum.getSample (ch, i)) > 1.0e-6f)
                    matches = false;

        expect (matches, "fan-out from one root into two parallel branches, summed back at the sinks, matches hand-computed expectation");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "All tests passed." << std::endl;
    else
        std::cout << failures << " test(s) failed." << std::endl;

    return failures == 0 ? 0 : 1;
}
