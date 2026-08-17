#include "Params/ParameterLayout.h"
#include "Params/Identifiers.h"
#include "Modules/WaveshaperModule.h"
#include "Modules/FilterModule.h"
#include "Modules/DelayModule.h"
#include "Modules/DynamicsModule.h"
#include "Modules/ConvolutionModule.h"
#include "Modules/MultibandConvolutionModule.h"
#include "Modules/UtilityModule.h"
#include "Modules/RingModModule.h"
#include "Modules/LFOModule.h"
#include "Modules/LossyModule.h"
#include "Modules/Eq8Module.h"
#include "Modules/Eq3Module.h"
#include "Modules/ChorusModule.h"
#include "Modulation/ModulationMatrix.h"
#include "Rack/RackSlot.h"
#include "Rack/ConnectionGraph.h"
#include "IR/IRLibrary.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <cmath>
#include <vector>

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

    // Single-bin Goertzel magnitude -- how much energy a buffer has at one specific frequency,
    // without needing a full FFT. Used to confirm Frequency Shift actually moves energy from one
    // frequency to another, not just "stays finite."
    double goertzelMagnitude (const juce::AudioBuffer<float>& buffer, int channel, double freqHz, double sampleRate)
    {
        const auto* data = buffer.getReadPointer (channel);
        const int n = buffer.getNumSamples();
        const double k = std::round ((double) n * freqHz / sampleRate);
        const double omega = 2.0 * juce::MathConstants<double>::pi * k / (double) n;
        const double coeff = 2.0 * std::cos (omega);

        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double s0 = (double) data[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (double) n;
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

    // --- Filter: Ladder Low Pass attenuates content above cutoff (saturating character, still fundamentally low-pass) ---
    {
        apvts.getRawParameterValue (filterParamId (0, FilterParam::type))->store (7.0f); // Ladder Low Pass
        apvts.getRawParameterValue (filterParamId (0, FilterParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::resonance))->store (0.5f);
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

        expect (highRms < lowRms * 0.5,
                "Ladder Low Pass attenuates an 8kHz tone below a 100Hz tone through a 300Hz cutoff (low RMS "
                    + juce::String (lowRms, 4) + ", high RMS " + juce::String (highRms, 4) + ")");
    }

    // --- Filter: Formant passes more energy near a vowel's formant frequency than far from any ---
    {
        apvts.getRawParameterValue (filterParamId (0, FilterParam::type))->store (9.0f); // Formant
        apvts.getRawParameterValue (filterParamId (0, FilterParam::frequency))->store (20.0f); // vowel "A" (F1=800Hz)
        apvts.getRawParameterValue (filterParamId (0, FilterParam::resonance))->store (8.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (0, FilterParam::output))->store (0.0f);

        FilterModule module (apvts, 0);
        module.prepare (spec);

        auto onFormantBuffer = makeTestSignal (blockSize, 0.5f, 800.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (onFormantBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        module.reset();

        auto offFormantBuffer = makeTestSignal (blockSize, 0.5f, 5000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (offFormantBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        const double onRms = rms (onFormantBuffer, 0);
        const double offRms = rms (offFormantBuffer, 0);

        expect (onRms > offRms * 1.5,
                "Formant filter (vowel A) passes more 800Hz (F1) energy through than a 5kHz tone far from any formant (on RMS "
                    + juce::String (onRms, 4) + ", off RMS " + juce::String (offRms, 4) + ")");
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

        // Biquad SVF modes (0-3) and Formant (9, itself three resonant biquad peaks) can
        // legitimately amplify a tone driven right at a resonant frequency well past unity --
        // that's the filter doing its job, not a bug -- so only check for runaway (NaN/Inf), not
        // a tight bound. Delay-line modes (4-6, |feedback| < 1 guarantees boundedness) and the
        // Ladder modes (7-8, bounded by their tanh() saturation regardless of resonance) get the
        // tighter check.
        const bool canLegitimatelyExceedUnity = filterType <= 3 || getFilterTypeChoices()[filterType] == "Formant";

        if (canLegitimatelyExceedUnity)
        {
            expect (isFinite (buffer),
                    "filter type " + juce::String (filterType) + " (" + getFilterTypeChoices()[filterType]
                        + ") stays finite (no NaN/Inf) at high resonance");
        }
        else
        {
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

    // --- Multiband Convolution: with every band's Mix at 0%, the LR4 crossover split + sum
    // recombines to a unity-gain ALLPASS -- flat magnitude spectrum, but phase-shifted, NOT a
    // sample-exact copy of the input. A time-domain sample diff against a sine tone is the wrong
    // check for that (phase shift alone makes it fail despite a perfectly flat crossover), so this
    // feeds an impulse and checks the FFT MAGNITUDE of the recombined output stays flat (~0dB)
    // across the spectrum -- mirrors MultibandConvolver's own "Phase 2 null test" for this exact
    // property (see CrossoverSplitter.h's class comment) ---
    {
        for (int band = 0; band < kNumConvolutionBands; ++band)
        {
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::mix))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::output))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::tone))->store (0.0f);
        }
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz1))->store (300.0f);
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz2))->store (3000.0f);

        MultibandConvolutionModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        // Let the split-point smoothing (50ms ramp) settle on silence before injecting the impulse.
        for (int i = 0; i < 20; ++i)
        {
            juce::AudioBuffer<float> silence (2, blockSize);
            silence.clear();
            juce::dsp::AudioBlock<float> silenceBlock (silence);
            juce::MidiBuffer midi;
            module.process (silenceBlock, midi, modMatrix);
        }

        // A unit impulse's true spectrum is exactly 1.0 (0dB) at every bin by definition (DFT of a
        // delta function), so this needs no reference/normalization -- the recombined signal's own
        // FFT magnitude should likewise sit at ~0dB everywhere if the crossover really is flat.
        constexpr int fftOrder = 14;
        constexpr int captureLen = 1 << fftOrder; // 16384 samples, ~370ms at 44.1kHz -- ample decay time
        std::vector<float> captured (captureLen, 0.0f);
        int written = 0;
        bool impulseSent = false;

        while (written < captureLen)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            block.clear();
            if (! impulseSent)
            {
                block.setSample (0, 0, 1.0f);
                block.setSample (1, 0, 1.0f);
                impulseSent = true;
            }

            juce::dsp::AudioBlock<float> dspBlock (block);
            juce::MidiBuffer midi;
            module.process (dspBlock, midi, modMatrix);

            for (int i = 0; i < blockSize && written < captureLen; ++i, ++written)
                captured[(size_t) written] = block.getSample (0, i);
        }

        juce::dsp::FFT fft (fftOrder);
        std::vector<float> fftData ((size_t) captureLen * 2, 0.0f);
        std::copy (captured.begin(), captured.end(), fftData.begin());
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const int numBins = captureLen / 2;
        double maxDevDb = 0.0;
        double worstFreq = 0.0;

        for (int bin = 1; bin < numBins; ++bin)
        {
            const double freq = bin * sampleRate / captureLen;
            if (freq < 30.0 || freq > 20000.0)
                continue;

            const double mag = fftData[(size_t) bin];
            const double db = std::abs (juce::Decibels::gainToDecibels (mag, -100.0));
            if (db > maxDevDb)
            {
                maxDevDb = db;
                worstFreq = freq;
            }
        }

        expect (maxDevDb < 1.5, "Multiband Convolution with every band's Mix at 0% recombines to a flat unity-gain-allpass spectrum (max deviation "
                                     + juce::String (maxDevDb, 3) + " dB at ~" + juce::String (worstFreq, 0) + " Hz)");
    }

    // --- Multiband Convolution: stays finite/bounded at extreme split points/tone/stretch/fade.
    // Uses one continuous buffer split into sub-blocks (like the compressor test above) rather than
    // reprocessing the same short buffer in place -- reusing an already-boosted block as the next
    // block's "new" input would compound the +24dB output gain across iterations and blow up for
    // reasons that have nothing to do with the module's real per-block behavior. ---
    {
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz1))->store (25.0f);
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz2))->store (18000.0f);

        for (int band = 0; band < kNumConvolutionBands; ++band)
        {
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::irIndex))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::tone))->store (band % 2 == 0 ? 1.0f : -1.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::fadeIn))->store (500.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::fadeOut))->store (100.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::stretch))->store (4.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::mix))->store (100.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::output))->store (24.0f);
        }

        MultibandConvolutionModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        const int numChunks = 5; // several blocks so the debounced reshape has a chance to fire mid-stream
        auto fullBuffer = makeTestSignal (blockSize * numChunks, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> fullBlock (fullBuffer);

        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        // Bound set generously above the ~28x peak this legitimately reaches at these settings (3
        // bands simultaneously at +24dB output/100% mix/no dry blend, matching ConvolutionModule's
        // own per-slot headroom -- the master safety limiter downstream is what protects the user's
        // ears/gear at that point, not this module) -- this check is for NaN/Inf/runaway growth,
        // not for clamping legitimate gain-stacking headroom.
        expect (isFiniteAndBounded (fullBuffer, 60.0f), "Multiband Convolution stays finite/bounded at extreme split points/tone/stretch/fade/output");
    }

    // --- Multiband Convolution: each band's IR reshape uses its own identity with the shared
    // background worker (the IRReshapeWorker generalization this module depends on) -- picking a
    // catalog IR on one band, with only that band audible, eventually changes the output ---
    {
        for (int band = 0; band < kNumConvolutionBands; ++band)
        {
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::irIndex))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::tone))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::fadeIn))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::fadeOut))->store (0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::stretch))->store (1.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::mix))->store (band == 1 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (multibandConvolutionBandParamId (0, band, MultibandConvolutionBandParam::output))->store (0.0f);
        }
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz1))->store (20.0f);
        apvts.getRawParameterValue (multibandConvolutionParamId (0, MultibandConvolutionParam::splitHz2))->store (20000.0f);

        MultibandConvolutionModule module (apvts, 0, sharedServices);
        module.prepare (spec);

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

        expect (isFiniteAndBounded (lastBuffer, 4.0f), "Multiband Convolution with a catalog IR loaded on one band stays finite/bounded");
        expect (differs, "Multiband Convolution's per-band IR reshape (its own IRReshapeWorker identity) actually completes and changes the output");
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

    // --- ConnectionGraph: slot 10 (root role) -> 0 -> 1 -> 2 -> slot 11 (sink role) topologically orders correctly ---
    {
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 10, 0 };
        conns[1] = { 0, 1 };
        conns[2] = { 1, 2 };
        conns[3] = { 2, 11 };

        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[2] = active[10] = active[11] = true;
        std::array<bool, kMaxSlots> isRootRole {};
        std::array<bool, kMaxSlots> isSinkRole {};
        isRootRole[10] = true;
        isSinkRole[11] = true;

        const auto graph = buildProcessingOrder (conns, 4, active, isRootRole, isSinkRole);

        expect (graph.orderCount == 5, "a linear root->0->1->2->sink chain orders all 5 active nodes");
        expect (graph.isRoot[10] && ! graph.isRoot[0] && ! graph.isRoot[1] && ! graph.isRoot[2] && ! graph.isRoot[11],
                "only the slot marked root role is ever a root, regardless of which regular slot has zero incoming edges");
        expect (graph.isSink[11] && ! graph.isSink[0] && ! graph.isSink[1] && ! graph.isSink[2] && ! graph.isSink[10],
                "only the slot marked sink role is ever a sink, regardless of which regular slot has zero outgoing edges");

        auto positionOf = [&] (int node) -> int
        {
            for (int i = 0; i < graph.orderCount; ++i)
                if (graph.order[(size_t) i] == node)
                    return i;
            return -1;
        };

        expect (positionOf (10) < positionOf (0) && positionOf (0) < positionOf (1)
                    && positionOf (1) < positionOf (2) && positionOf (2) < positionOf (11),
                "chain orders strictly root(10), 0, 1, 2, sink(11)");
    }

    // --- ConnectionGraph: a regular module with no path from any root is excluded from the order entirely ---
    {
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 10, 0 };
        conns[1] = { 0, 11 };
        // Slot 1 is active (a real module sits there) but nothing connects it to the root or
        // sink -- a floating, unpatched node, which should simply never run.

        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[10] = active[11] = true;
        std::array<bool, kMaxSlots> isRootRole {};
        std::array<bool, kMaxSlots> isSinkRole {};
        isRootRole[10] = true;
        isSinkRole[11] = true;

        const auto graph = buildProcessingOrder (conns, 2, active, isRootRole, isSinkRole);

        bool slot1InOrder = false;
        for (int i = 0; i < graph.orderCount; ++i)
            if (graph.order[(size_t) i] == 1)
                slot1InOrder = true;

        expect (! slot1InOrder, "a module with no path from any root is excluded from the processing order -- an unpatched node does nothing");
        expect (graph.orderCount == 3, "only the root, 0, and the sink actually run");
    }

    // --- ConnectionGraph: a cycle reachable from a root (defensive fallback) still includes every active node exactly once, doesn't hang ---
    {
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 10, 0 };
        conns[1] = { 0, 1 };
        conns[2] = { 1, 0 }; // artificial cycle -- connect-time validation would normally reject this

        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[10] = true;
        std::array<bool, kMaxSlots> isRootRole {};
        std::array<bool, kMaxSlots> isSinkRole {};
        isRootRole[10] = true;

        const auto graph = buildProcessingOrder (conns, 3, active, isRootRole, isSinkRole);

        expect (graph.orderCount == 3, "a cycle reachable from a root still produces an order containing the root and both cyclic nodes (defensive fallback), not a hang or a drop");
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

        // Slot 3 (root role) -> 0, 0 fans out to both branches, both branches feed slot 4 (sink
        // role) -- mirrors GGridAudioProcessor::processBlock's actual root/sink-anchored model
        // exactly (see its isRoot/isSink handling).
        std::array<Connection, kMaxConnections> conns {};
        conns[0] = { 3, 0 };
        conns[1] = { 0, 1 };
        conns[2] = { 0, 2 };
        conns[3] = { 1, 4 };
        conns[4] = { 2, 4 };
        std::array<bool, kMaxSlots> active {};
        active[0] = active[1] = active[2] = active[3] = active[4] = true;
        std::array<bool, kMaxSlots> isRootRole {};
        std::array<bool, kMaxSlots> isSinkRole {};
        isRootRole[3] = true;
        isSinkRole[4] = true;
        const auto graph = buildProcessingOrder (conns, 5, active, isRootRole, isSinkRole);

        std::array<juce::AudioBuffer<float>, kMaxSlots> nodeBuffers;
        for (int i = 0; i < kMaxSlots; ++i)
            if (active[(size_t) i])
            {
                // setSize alone doesn't zero freshly-grown memory (clearExtraSpace defaults to
                // false) -- without an explicit clear, addFrom below accumulates onto whatever
                // garbage happened to be in that heap memory, exactly the bug this test would
                // otherwise fail to catch. Mirrors GGridAudioProcessor::processBlock's own
                // setSize+clear pair exactly, see PluginProcessor.cpp.
                nodeBuffers[(size_t) i].setSize (2, blockSize);
                nodeBuffers[(size_t) i].clear();
            }

        RackSlot* rackSlotsByIndex[3] = { &slotRoot, &slotBranchA, &slotBranchB };

        for (int idx = 0; idx < graph.orderCount; ++idx)
        {
            const int i = graph.order[(size_t) idx];

            if (graph.isRoot[(size_t) i])
            {
                for (int ch = 0; ch < 2; ++ch)
                    nodeBuffers[(size_t) i].copyFrom (ch, 0, dry, ch, 0, blockSize);
                continue;
            }

            for (int c = 0; c < 5; ++c)
            {
                if (conns[(size_t) c].to != i) continue;
                for (int ch = 0; ch < 2; ++ch)
                    nodeBuffers[(size_t) i].addFrom (ch, 0, nodeBuffers[(size_t) conns[(size_t) c].from], ch, 0, blockSize);
            }

            if (graph.isSink[(size_t) i])
                continue;

            juce::dsp::AudioBlock<float> nodeBlock (nodeBuffers[(size_t) i]);
            juce::MidiBuffer midi;
            rackSlotsByIndex[i]->process (nodeBlock, midi, modMatrix);
        }

        juce::AudioBuffer<float> actualSum (2, blockSize);
        actualSum.clear();
        for (int ch = 0; ch < 2; ++ch)
            actualSum.addFrom (ch, 0, nodeBuffers[4], ch, 0, blockSize);

        bool matches = true;
        for (int ch = 0; ch < 2 && matches; ++ch)
            for (int i = 0; i < blockSize && matches; ++i)
                if (std::abs (expectedSum.getSample (ch, i) - actualSum.getSample (ch, i)) > 1.0e-6f)
                    matches = false;

        expect (matches, "fan-out from one root into two parallel branches, summed back at the sinks, matches hand-computed expectation");
    }

    // --- Utility: Mono collapses L/R to identical values ---
    {
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::gain))->store (0.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::pan))->store (0.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::width))->store (100.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::mono))->store (1.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::phaseInvertL))->store (0.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::phaseInvertR))->store (0.0f);

        UtilityModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, 0.5f);
            buffer.setSample (1, i, -0.3f);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        bool identical = true;
        for (int i = 0; i < blockSize; ++i)
            if (std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)) > 1.0e-6f)
                identical = false;

        expect (identical, "Utility Mono collapses L/R to identical values");
    }

    // --- Utility: Phase Invert L flips only the left channel ---
    {
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::mono))->store (0.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::phaseInvertL))->store (1.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::phaseInvertR))->store (0.0f);

        UtilityModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, 0.4f);
            buffer.setSample (1, i, 0.4f);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (std::abs (buffer.getSample (0, 0) - (-0.4f)) < 1.0e-4f && std::abs (buffer.getSample (1, 0) - 0.4f) < 1.0e-4f,
                "Utility Phase Invert L flips only the left channel's polarity (L " + juce::String (buffer.getSample (0, 0), 4)
                    + ", R " + juce::String (buffer.getSample (1, 0), 4) + ")");
    }

    // --- Utility: Gain applies the expected linear factor ---
    {
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::phaseInvertL))->store (0.0f);
        apvts.getRawParameterValue (utilityParamId (0, UtilityParam::gain))->store (6.0f);

        UtilityModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, 0.1f);
            buffer.setSample (1, i, 0.1f);
        }

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const float expectedGain = juce::Decibels::decibelsToGain (6.0f);
        expect (std::abs (buffer.getSample (0, 0) - 0.1f * expectedGain) < 1.0e-4f,
                "Utility Gain applies the expected +6dB linear factor");
    }

    // --- Ring Mod / Freq Shift: both modes stay finite/bounded ---
    for (int mode = 0; mode < 2; ++mode)
    {
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::mode))->store ((float) mode);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::fine))->store (0.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::mix))->store (100.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::output))->store (0.0f);

        RingModModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 1.5f),
                juce::String (mode == 0 ? "Ring Mod" : "Freq Shift") + " mode stays finite/bounded");
    }

    // --- Freq Shift: actually moves a tone's energy to a different frequency, not just distorts it ---
    {
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::mode))->store (1.0f); // Freq Shift
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::frequency))->store (200.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::fine))->store (0.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::mix))->store (100.0f);
        apvts.getRawParameterValue (ringModParamId (0, RingModParam::output))->store (0.0f);

        RingModModule module (apvts, 0);
        module.prepare (spec);

        const double srcFreq = 440.0;
        const double shiftHz = 200.0;
        const int numChunks = 16; // 16*512 = 8192 samples, plenty of resolution for a Goertzel bin
        auto fullBuffer = makeTestSignal (blockSize * numChunks, 0.8f, (float) srcFreq, sampleRate);
        juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        // Analyze just the back half, past the allpass filters' initial settling transient.
        const int tailStart = blockSize * numChunks / 2;
        juce::AudioBuffer<float> tail (1, blockSize * numChunks - tailStart);
        tail.copyFrom (0, 0, fullBuffer, 0, tailStart, tail.getNumSamples());

        const double magAtShifted = goertzelMagnitude (tail, 0, srcFreq + shiftHz, sampleRate);
        const double magAtOriginal = goertzelMagnitude (tail, 0, srcFreq, sampleRate);

        expect (magAtShifted > magAtOriginal * 3.0,
                "Freq Shift moves a 440Hz tone's energy toward 640Hz rather than leaving it at 440Hz (640Hz magnitude "
                    + juce::String (magAtShifted, 4) + ", 440Hz magnitude " + juce::String (magAtOriginal, 4) + ")");
    }

    // --- LFO: Sine sweeps through its full bipolar range over one period ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (0.0f); // Sine
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f); // Free
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (2.0f); // 0.5s period
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        float minVal = 1.0f, maxVal = -1.0f;
        const int numChunks = 100; // ~1.16s @ blockSize/sampleRate -- comfortably over one 0.5s period
        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            minVal = juce::jmin (minVal, module.getCurrentValue());
            maxVal = juce::jmax (maxVal, module.getCurrentValue());
        }

        expect (maxVal > 0.9f && minVal < -0.9f,
                "LFO (Sine, 2Hz free-running) sweeps through its full bipolar range over ~1 second (min "
                    + juce::String (minVal, 3) + ", max " + juce::String (maxVal, 3) + ")");
    }

    // --- LFO: Sample & Hold changes value as phase wraps, and stays within [-1, 1] ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (4.0f); // Sample & Hold
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (5.0f); // wraps several times in the test window
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        std::vector<float> seenValues;
        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int chunk = 0; chunk < 40; ++chunk)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            seenValues.push_back (module.getCurrentValue());
        }

        bool changed = false, bounded = true;
        for (float v : seenValues)
        {
            if (std::abs (v - seenValues[0]) > 1.0e-4f) changed = true;
            if (std::abs (v) > 1.001f) bounded = false;
        }

        expect (changed, "LFO Sample & Hold value changes at least once as phase wraps");
        expect (bounded, "LFO Sample & Hold stays within [-1, 1]");
    }

    // --- Modulation cable: an LFO routed to Filter Frequency actually moves the cutoff ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (2.0f); // Square -- deterministic +1/-1, no phase-timing ambiguity
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (1.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        apvts.getRawParameterValue (filterParamId (1, FilterParam::type))->store (0.0f); // Low Pass
        apvts.getRawParameterValue (filterParamId (1, FilterParam::frequency))->store (1000.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::resonance))->store (0.707f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::output))->store (0.0f);

        ModulationMatrix cableMatrix (apvts);
        const bool added = cableMatrix.addModConnection (0, 1, filterParamId (1, FilterParam::frequency));
        expect (added, "a modulation cable from an LFO slot to Filter Frequency is accepted");

        LFOModule lfo (apvts, 0, sharedServices);
        lfo.prepare (spec);
        FilterModule filter (apvts, 1);
        filter.prepare (spec);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            lfo.process (block, midi, cableMatrix);
        }
        cableMatrix.setLfoValue (0, lfo.getCurrentValue());
        expect (std::abs (lfo.getCurrentValue() - 1.0f) < 1.0e-4f, "square LFO reads +1 during the first half of its cycle");

        auto highLfoBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (highLfoBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        // Advance half a cycle (0.5s @ 1Hz) so the square wave flips to its -1 half.
        {
            const int numChunks = (int) std::round (0.5 * sampleRate / blockSize);
            for (int c = 0; c < numChunks; ++c)
            {
                juce::dsp::AudioBlock<float> block (dummyBuffer);
                juce::MidiBuffer midi;
                lfo.process (block, midi, cableMatrix);
            }
        }
        cableMatrix.setLfoValue (0, lfo.getCurrentValue());
        expect (lfo.getCurrentValue() < -0.9f, "square LFO reads close to -1 after half a cycle");

        filter.reset();
        auto lowLfoBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (lowLfoBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        const double rmsAtPositiveLfo = rms (highLfoBuffer, 0);
        const double rmsAtNegativeLfo = rms (lowLfoBuffer, 0);

        expect (rmsAtPositiveLfo > rmsAtNegativeLfo * 1.5,
                "an LFO modulation cable into Filter Frequency passes more 8kHz energy through when the LFO is at +1 than at -1 (positive-LFO RMS "
                    + juce::String (rmsAtPositiveLfo, 4) + ", negative-LFO RMS " + juce::String (rmsAtNegativeLfo, 4) + ")");
    }

    // --- Lossy: stays finite/bounded at maximum degradation ---
    {
        apvts.getRawParameterValue (lossyParamId (0, LossyParam::bits))->store (1.0f);
        apvts.getRawParameterValue (lossyParamId (0, LossyParam::rate))->store (200.0f);
        apvts.getRawParameterValue (lossyParamId (0, LossyParam::jitter))->store (1.0f);
        apvts.getRawParameterValue (lossyParamId (0, LossyParam::mix))->store (100.0f);
        apvts.getRawParameterValue (lossyParamId (0, LossyParam::output))->store (0.0f);

        LossyModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 1000.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        for (int i = 0; i < 20; ++i)
            module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 2.0f), "Lossy stays finite/bounded at extreme Bits/Rate/Jitter/Mix");
    }

    // --- Lossy: Jitter=0 is deterministic run-to-run; Jitter=1 is not (random per-bin phase) ---
    {
        const int numChunks = 40;
        const int totalSamples = blockSize * numChunks;

        auto runOnce = [&] (float jitterValue) -> juce::AudioBuffer<float>
        {
            apvts.getRawParameterValue (lossyParamId (0, LossyParam::bits))->store (4.0f);
            apvts.getRawParameterValue (lossyParamId (0, LossyParam::rate))->store (200.0f);
            apvts.getRawParameterValue (lossyParamId (0, LossyParam::jitter))->store (jitterValue);
            apvts.getRawParameterValue (lossyParamId (0, LossyParam::mix))->store (100.0f);
            apvts.getRawParameterValue (lossyParamId (0, LossyParam::output))->store (0.0f);

            LossyModule module (apvts, 0);
            module.prepare (spec);

            auto fullBuffer = makeTestSignal (totalSamples, 0.5f, 1000.0f, sampleRate);
            juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
            for (int chunk = 0; chunk < numChunks; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }
            return fullBuffer;
        };

        auto buffersMatch = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < a.getNumChannels(); ++ch)
            {
                const auto* dataA = a.getReadPointer (ch);
                const auto* dataB = b.getReadPointer (ch);
                for (int i = 0; i < a.getNumSamples(); ++i)
                    if (std::abs (dataA[i] - dataB[i]) > 1.0e-6f)
                        return false;
            }
            return true;
        };

        expect (buffersMatch (runOnce (0.0f), runOnce (0.0f)),
                "Lossy with Jitter=0 is fully deterministic across independent runs on the same input");

        expect (! buffersMatch (runOnce (1.0f), runOnce (1.0f)),
                "Lossy with Jitter=1 produces different output across independent runs on the same input (random per-bin phase)");
    }

    // --- EQ 8: stays finite/bounded with all bands at extreme alternating gains ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (b)))->store (b % 2 == 0 ? 12.0f : -12.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::output))->store (0.0f);

        Eq8Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 3200.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 4.0f), "EQ 8 stays finite/bounded with all bands alternating +/-12dB");
    }

    // --- EQ 8: flat (all bands 0dB) leaves a tone's level essentially unchanged ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (b)))->store (0.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::output))->store (0.0f);

        Eq8Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.5f, 1000.0f, sampleRate);
        const double inputRms = rms (buffer, 0);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const double outputRms = rms (buffer, 0);
        expect (std::abs (outputRms - inputRms) < inputRms * 0.05,
                "EQ 8 with all bands flat (0dB) leaves a 1kHz tone's level essentially unchanged (input RMS "
                    + juce::String (inputRms, 4) + ", output RMS " + juce::String (outputRms, 4) + ")");
    }

    // --- EQ 8: boosting the band matching the input frequency raises its level ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (b)))->store (0.0f);
        apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (5)))->store (12.0f); // 3.2kHz band
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::output))->store (0.0f);

        Eq8Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.5f, kEq8BandFrequencies[5], sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const double flatRms = 0.5 * std::sqrt (0.5);
        expect (rms (buffer, 0) > flatRms * 1.5,
                "EQ 8 boosting the 3.2kHz band by +12dB raises a matching 3.2kHz tone's RMS well above its flat level (RMS "
                    + juce::String (rms (buffer, 0), 4) + ", flat would be " + juce::String (flatRms, 4) + ")");
    }

    // --- EQ 3: stays finite/bounded at extreme Low/Mid/High gains ---
    {
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::low))->store (12.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mid))->store (-12.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::high))->store (12.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::output))->store (0.0f);

        Eq3Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 1000.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 4.0f), "EQ 3 stays finite/bounded at extreme Low/Mid/High gains");
    }

    // --- EQ 3: flat (Low/Mid/High all 0dB) leaves a tone's level essentially unchanged ---
    {
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::low))->store (0.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mid))->store (0.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::high))->store (0.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::output))->store (0.0f);

        Eq3Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.5f, 1000.0f, sampleRate);
        const double inputRms = rms (buffer, 0);

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const double outputRms = rms (buffer, 0);
        expect (std::abs (outputRms - inputRms) < inputRms * 0.05,
                "EQ 3 with Low/Mid/High flat (0dB) leaves a 1kHz tone's level essentially unchanged (input RMS "
                    + juce::String (inputRms, 4) + ", output RMS " + juce::String (outputRms, 4) + ")");
    }

    // --- EQ 3: boosting Low raises a low-frequency tone's level, boosting High raises a
    //     high-frequency tone's level (confirms the shelves are actually wired to the right
    //     bands, not swapped) ---
    {
        auto measureRms = [&] (const juce::String& band, float freqHz) -> double
        {
            apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::low))->store (0.0f);
            apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mid))->store (0.0f);
            apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::high))->store (0.0f);
            apvts.getRawParameterValue (eq3ParamId (0, band))->store (12.0f);
            apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::mix))->store (100.0f);
            apvts.getRawParameterValue (eq3ParamId (0, Eq3Param::output))->store (0.0f);

            Eq3Module module (apvts, 0);
            module.prepare (spec);

            auto buffer = makeTestSignal (blockSize, 0.5f, freqHz, sampleRate);
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);

            return rms (buffer, 0);
        };

        const double flatRms = 0.5 * std::sqrt (0.5);
        const double lowBoostRms = measureRms (Eq3Param::low, 60.0f);
        const double highBoostRms = measureRms (Eq3Param::high, 10000.0f);

        expect (lowBoostRms > flatRms * 1.3,
                "EQ 3 boosting Low by +12dB raises a 60Hz tone's RMS above its flat level (RMS "
                    + juce::String (lowBoostRms, 4) + ", flat would be " + juce::String (flatRms, 4) + ")");
        expect (highBoostRms > flatRms * 1.3,
                "EQ 3 boosting High by +12dB raises a 10kHz tone's RMS above its flat level (RMS "
                    + juce::String (highBoostRms, 4) + ", flat would be " + juce::String (flatRms, 4) + ")");
    }

    // --- Chorus/Flanger: stays finite/bounded at extreme Flanger settings across a continuous signal ---
    {
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mode))->store (1.0f); // Flanger
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::rate))->store (10.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::depth))->store (100.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::delay))->store (1.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::feedback))->store (0.95f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mix))->store (100.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::output))->store (0.0f);

        ChorusModule module (apvts, 0);
        module.prepare (spec);

        // A continuous signal, not the same short buffer reprocessed repeatedly -- reprocessing
        // a static buffer would feed a feedback-heavy effect's own already-wet output back in as
        // "new" input each pass, compounding in a way no real streaming use ever would.
        const int numChunks = 40;
        auto fullBuffer = makeTestSignal (blockSize * numChunks, 0.9f, 220.0f, sampleRate);
        juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        expect (isFiniteAndBounded (fullBuffer, 4.0f),
                "Chorus/Flanger stays finite/bounded at extreme Rate/Depth/Feedback in Flanger mode across a continuous signal");
    }

    // --- Chorus/Flanger: Mix=0 leaves the signal sample-exactly unchanged (no latency) ---
    {
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mode))->store (0.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::rate))->store (0.8f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::depth))->store (50.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::delay))->store (15.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mix))->store (0.0f);
        apvts.getRawParameterValue (chorusParamId (0, ChorusParam::output))->store (0.0f);

        ChorusModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.5f, 220.0f, sampleRate);
        auto reference = buffer;

        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        bool matchesExactly = true;
        for (int ch = 0; ch < buffer.getNumChannels() && matchesExactly; ++ch)
        {
            const auto* out = buffer.getReadPointer (ch);
            const auto* in = reference.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (std::abs (out[i] - in[i]) > 1.0e-5f) { matchesExactly = false; break; }
        }

        expect (matchesExactly, "Chorus/Flanger with Mix=0 leaves the signal sample-exactly unchanged (no latency introduced)");
    }

    // --- Chorus/Flanger: Mode gates the feedback path (Chorus forces it off, Flanger doesn't) ---
    {
        auto runWithMode = [&] (float modeValue) -> juce::AudioBuffer<float>
        {
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mode))->store (modeValue);
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::rate))->store (0.5f);
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::depth))->store (0.0f); // isolate feedback's effect, no LFO sweep
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::delay))->store (5.0f);
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::feedback))->store (0.9f);
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::mix))->store (100.0f);
            apvts.getRawParameterValue (chorusParamId (0, ChorusParam::output))->store (0.0f);

            ChorusModule module (apvts, 0);
            module.prepare (spec);

            auto buffer = makeTestSignal (blockSize, 0.5f, 220.0f, sampleRate);
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            for (int i = 0; i < 5; ++i)
                module.process (block, midi, modMatrix);

            return buffer;
        };

        auto chorusOutput = runWithMode (0.0f);
        auto flangerOutput = runWithMode (1.0f);

        bool identical = true;
        for (int ch = 0; ch < chorusOutput.getNumChannels() && identical; ++ch)
        {
            const auto* a = chorusOutput.getReadPointer (ch);
            const auto* b = flangerOutput.getReadPointer (ch);
            for (int i = 0; i < chorusOutput.getNumSamples(); ++i)
                if (std::abs (a[i] - b[i]) > 1.0e-5f) { identical = false; break; }
        }

        expect (! identical, "Chorus and Flanger modes produce different output at the same Feedback setting "
                              "(Flanger's feedback path is actually active, Chorus's is gated off)");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "All tests passed." << std::endl;
    else
        std::cout << failures << " test(s) failed." << std::endl;

    return failures == 0 ? 0 : 1;
}
