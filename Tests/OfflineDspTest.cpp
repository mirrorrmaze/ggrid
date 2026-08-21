#include "Params/ParameterLayout.h"
#include "Params/Identifiers.h"
#include "Modules/WaveshaperModule.h"
#include "Modules/FilterModule.h"
#include "Modules/NonlinearFilterModule.h"
#include "Modules/MackityModule.h"
#include "Modules/ShimmerReverbModule.h"
#include "Modules/DelayModule.h"
#include "Modules/CompressorModule.h"
#include "Modules/LimiterModule.h"
#include "Modules/ConvolutionModule.h"
#include "Modules/MultibandConvolutionModule.h"
#include "Modules/UtilityModule.h"
#include "Modules/RingModModule.h"
#include "Modules/LFOModule.h"
#include "Modules/LfoTableModule.h"
#include "Modules/LossyModule.h"
#include "Modules/SpectralClipperModule.h"
#include "Modules/Eq8Module.h"
#include "Modules/Eq3Module.h"
#include "Modules/ChorusModule.h"
#include "Modules/ThreeOscModule.h"
#include "Modules/WavetableSynthModule.h"
#include "Modules/AdsrModule.h"
#include "Modules/EnvelopeModule.h"
#include "Modules/MultipassModule.h"
#include "Modulation/ModulationMatrix.h"
#include "Rack/RackSlot.h"
#include "Rack/ConnectionGraph.h"
#include "IR/IRLibrary.h"
#include "Wavetable/WavetableLibrary.h"
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
    ModulationMatrix modMatrix;

    juce::dsp::ConvolutionMessageQueue convolutionQueue;
    IRReshapeWorker irReshapeWorker;
    std::atomic<double> testBpm { 120.0 };
    std::array<std::unique_ptr<juce::XmlElement>, kMaxSlots> testPendingModuleExtraState;
    SharedServices sharedServices { convolutionQueue, irReshapeWorker, testBpm, testPendingModuleExtraState };

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

    // --- Mackity: vintage input-stage style drive stays finite and audibly changes the signal ---
    {
        apvts.getRawParameterValue (mackityParamId (0, MackityParam::pad))->store (65.0f);
        apvts.getRawParameterValue (mackityParamId (0, MackityParam::mix))->store (100.0f);
        apvts.getRawParameterValue (mackityParamId (0, MackityParam::output))->store (0.0f);

        auto renderMackity = [&] (float input)
        {
            apvts.getRawParameterValue (mackityParamId (0, MackityParam::input))->store (input);
            MackityModule module (apvts, 0);
            module.prepare (spec);

            auto buffer = makeTestSignal (blockSize, 0.8f, 220.0f, sampleRate);
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            return buffer;
        };

        auto dry = makeTestSignal (blockSize, 0.8f, 220.0f, sampleRate);
        auto buffer = renderMackity (70.0f);
        auto smashed = renderMackity (100.0f);

        double diff = 0.0;
        for (int i = 0; i < blockSize; ++i)
            diff += std::abs ((double) buffer.getSample (0, i) - (double) dry.getSample (0, i));

        expect (isFiniteAndBounded (buffer, 2.0f) && diff > 1.0,
                "Mackity drive stays finite/bounded and changes the input-stage character (absolute diff "
                    + juce::String (diff, 3) + ")");

        auto absoluteDifference = [&] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            double sum = 0.0;
            for (int i = 0; i < blockSize; ++i)
                sum += std::abs ((double) a.getSample (0, i) - (double) b.getSample (0, i));
            return sum;
        };

        const double moderateOverload = absoluteDifference (buffer, dry);
        const double smashedOverload = absoluteDifference (smashed, dry);
        expect (isFiniteAndBounded (smashed, 4.0f) && smashedOverload > moderateOverload * 1.35,
                "Mackity max Input produces much more circuit slam than moderate drive (moderate diff "
                    + juce::String (moderateOverload, 3) + ", max diff " + juce::String (smashedOverload, 3) + ")");
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

    // --- Nonlinear Filter: low-pass mode attenuates high-frequency content while staying driven ---
    {
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::mode))->store (0.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::frequency))->store (500.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::resonance))->store (20.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::drive))->store (18.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::morph))->store (35.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::distortion))->store (0.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::output))->store (0.0f);

        NonlinearFilterModule lowModule (apvts, 0);
        lowModule.prepare (spec);
        auto lowBuffer = makeTestSignal (blockSize, 0.5f, 100.0f, sampleRate);
        juce::dsp::AudioBlock<float> lowBlock (lowBuffer);
        juce::MidiBuffer midi;
        lowModule.process (lowBlock, midi, modMatrix);

        NonlinearFilterModule highModule (apvts, 0);
        highModule.prepare (spec);
        auto highBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        juce::dsp::AudioBlock<float> highBlock (highBuffer);
        highModule.process (highBlock, midi, modMatrix);

        const double lowRms = rms (lowBuffer, 0);
        const double highRms = rms (highBuffer, 0);
        expect (lowRms > highRms * 4.0,
                "Nonlinear Filter Low Pass attenuates 8kHz well below 100Hz while driven (low RMS "
                    + juce::String (lowRms, 4) + ", high RMS " + juce::String (highRms, 4) + ")");
    }

    // --- Nonlinear Filter: every mode/distortion pairing stays stable at extreme drive/resonance/morph ---
    for (int mode = 0; mode < getNonlinearFilterModeChoices().size(); ++mode)
    {
        for (int distortion = 0; distortion < getNonlinearFilterDistortionChoices().size(); ++distortion)
        {
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::mode))->store ((float) mode);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::distortion))->store ((float) distortion);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::frequency))->store (600.0f);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::resonance))->store (100.0f);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::drive))->store (36.0f);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::morph))->store (100.0f);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::mix))->store (100.0f);
            apvts.getRawParameterValue (nonlinearFilterParamId (0, NonlinearFilterParam::output))->store (0.0f);

            NonlinearFilterModule module (apvts, 0);
            module.prepare (spec);

            auto buffer = makeTestSignal (blockSize, 0.95f, 220.0f, sampleRate);
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);

            expect (isFiniteAndBounded (buffer, 4.0f),
                    "Nonlinear Filter mode " + juce::String (mode) + " (" + getNonlinearFilterModeChoices()[mode]
                        + ") with " + getNonlinearFilterDistortionChoices()[distortion]
                        + " stays finite/bounded at max drive/resonance/morph");
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

    // --- Shimmer Reverb: pitch-shifted feedback stays stable and adds octave-like upper energy ---
    {
        auto runShimmer = [&] (float pitchMode)
        {
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::size))->store (80.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::feedback))->store (70.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::diffusion))->store (85.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::shift))->store (12.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::pitchMode))->store (pitchMode);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::color))->store (0.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::modRate))->store (0.2f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::modDepth))->store (12.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::lowCut))->store (80.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::highCut))->store (16000.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::mix))->store (100.0f);
            apvts.getRawParameterValue (shimmerReverbParamId (0, ShimmerReverbParam::output))->store (0.0f);

            ShimmerReverbModule module (apvts, 0);
            module.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize * 160);
            buffer.clear();
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const float sample = 0.1f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sampleRate);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            juce::dsp::AudioBlock<float> fullBlock (buffer);

            for (int chunk = 0; chunk < 160; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }

            return buffer;
        };

        auto bypass = runShimmer (0.0f);
        auto shimmer = runShimmer (1.0f);
        const double bypassHigh = goertzelMagnitude (bypass, 0, 880.0, sampleRate);
        const double shimmerHigh = goertzelMagnitude (shimmer, 0, 880.0, sampleRate);

        expect (isFiniteAndBounded (shimmer, 4.0f) && shimmerHigh > bypassHigh * 1.05,
                "Shimmer Reverb stays finite and pitch-shifted feedback adds upper-octave energy (bypass "
                    + juce::String (bypassHigh, 5) + ", shimmer " + juce::String (shimmerHigh, 5) + ")");
    }

    // --- Compressor: reduces steady-state level of a signal driven above threshold ---
    {
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::threshold))->store (-20.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::ratio))->store (8.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::attack))->store (1.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::release))->store (50.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::knee))->store (0.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::makeup))->store (0.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::mix))->store (100.0f);
        apvts.getRawParameterValue (compressorParamId (0, CompressorParam::detection))->store (0.0f); // Peak

        CompressorModule module (apvts, 0);
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
                "Compressor reduces steady-state level of a signal driven well above threshold (uncompressed RMS ~"
                    + juce::String (expectedUncompressedRms, 4) + ", compressed RMS " + juce::String (outputRms, 4) + ")");
    }

    // --- Compressor: Knee softens the onset -- right AT Threshold, Knee=0 (hard) applies no
    //     reduction yet (the classic sharp corner: static characteristic is 0 for x<=0), while a
    //     wide Knee already applies some reduction there (the smooth ramp-in a soft knee is
    //     known for -- the quadratic knee region starts kneeDb/2 below the threshold) ---
    {
        auto runKnee = [&] (float kneeDb) -> double
        {
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::threshold))->store (-20.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::ratio))->store (4.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::attack))->store (0.5f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::release))->store (50.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::knee))->store (kneeDb);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::makeup))->store (0.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::mix))->store (100.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::detection))->store (0.0f); // Peak

            CompressorModule module (apvts, 0);
            module.prepare (spec);

            const int numChunks = 8;
            const float amplitudeAtThreshold = 0.1f * std::sqrt (2.0f); // Peak level lands right at -20dBFS
            auto fullBuffer = makeTestSignal (blockSize * numChunks, amplitudeAtThreshold, 220.0f, sampleRate);
            juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
            for (int chunk = 0; chunk < numChunks; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }

            juce::AudioBuffer<float> lastChunk (1, blockSize);
            lastChunk.copyFrom (0, 0, fullBuffer, 0, blockSize * (numChunks - 1), blockSize);
            return rms (lastChunk, 0);
        };

        const double hardKneeRms = runKnee (0.0f);
        const double softKneeRms = runKnee (24.0f);

        expect (softKneeRms < hardKneeRms * 0.98,
                "Compressor with a wide Knee (24dB) already reduces gain right at Threshold, unlike Knee=0's sharp "
                "corner (hard-knee RMS " + juce::String (hardKneeRms, 5) + ", soft-knee RMS " + juce::String (softKneeRms, 5) + ")");
    }

    // --- Compressor: Peak vs RMS detection modes react differently to the same signal -- for a
    //     steady sine, Peak reads ~3dB hotter than RMS does (the fixed sqrt(2) peak/RMS ratio),
    //     so a threshold sitting between the two only trips real gain reduction in Peak mode ---
    {
        auto runDetection = [&] (int detectionChoice) -> double
        {
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::threshold))->store (-2.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::ratio))->store (8.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::attack))->store (0.5f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::release))->store (50.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::knee))->store (0.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::makeup))->store (0.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::mix))->store (100.0f);
            apvts.getRawParameterValue (compressorParamId (0, CompressorParam::detection))->store ((float) detectionChoice);

            CompressorModule module (apvts, 0);
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

            juce::AudioBuffer<float> lastChunk (1, blockSize);
            lastChunk.copyFrom (0, 0, fullBuffer, 0, blockSize * (numChunks - 1), blockSize);
            return rms (lastChunk, 0);
        };

        const double peakModeRms = runDetection (0);
        const double rmsModeRms = runDetection (1);

        expect (peakModeRms < rmsModeRms * 0.95,
                "Compressor in Peak detection mode reduces a steady tone more than RMS mode does, since Peak reads "
                "~3dB hotter than RMS for the same sine (Peak-mode RMS " + juce::String (peakModeRms, 5) + ", RMS-mode RMS "
                    + juce::String (rmsModeRms, 5) + ")");
    }

    // --- Limiter: caps output near its Ceiling regardless of how hard Gain drives the input ---
    {
        apvts.getRawParameterValue (limiterParamId (0, LimiterParam::gain))->store (24.0f);
        apvts.getRawParameterValue (limiterParamId (0, LimiterParam::ceiling))->store (-3.0f);
        apvts.getRawParameterValue (limiterParamId (0, LimiterParam::release))->store (50.0f);

        LimiterModule module (apvts, 0);
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

        const float ceilingLinear = juce::Decibels::decibelsToGain (-3.0f);
        const int settleSamples = blockSize * (numChunks - 2);
        const float peak = fullBuffer.getMagnitude (0, settleSamples, fullBuffer.getNumSamples() - settleSamples);

        expect (peak < ceilingLinear * 1.05f,
                "Limiter caps output near its Ceiling (-3dB, linear " + juce::String (ceilingLinear, 4)
                    + ") even when driven hard by Gain (+24dB) -- measured peak " + juce::String (peak, 4));
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
        // own per-slot headroom) -- this check is for NaN/Inf/runaway growth, not for clamping
        // legitimate gain-stacking headroom.
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

    // --- Multipass: splits into Low/Mid/High bands whose OWN output actually reflects the
    // frequency content assigned to that band, not the whole mixed signal duplicated 3x --
    // GGrid's first genuinely multi-output-bus module, this is the core contract being tested ---
    {
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz1))->store (300.0f);
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz2))->store (3000.0f);
        for (int b = 0; b < kNumMultipassBands; ++b)
            apvts.getRawParameterValue (multipassBandParamId (0, b, MultipassBandParam::gain))->store (0.0f);

        MultipassModule module (apvts, 0);
        module.prepare (spec);

        auto lowTone = makeTestSignal (blockSize, 0.5f, 100.0f, sampleRate);
        auto highTone = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        juce::AudioBuffer<float> mixed (2, blockSize);
        mixed.clear();
        for (int ch = 0; ch < 2; ++ch)
        {
            mixed.addFrom (ch, 0, lowTone, 0, 0, blockSize);
            mixed.addFrom (ch, 0, highTone, 0, 0, blockSize);
        }

        juce::dsp::AudioBlock<float> block (mixed);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const auto* lowBandBuffer = module.getOutputBusBuffer (0);
        const auto* highBandBuffer = module.getOutputBusBuffer (2);

        const double lowBandLowMag = goertzelMagnitude (*lowBandBuffer, 0, 100.0, sampleRate);
        const double lowBandHighMag = goertzelMagnitude (*lowBandBuffer, 0, 8000.0, sampleRate);
        const double highBandLowMag = goertzelMagnitude (*highBandBuffer, 0, 100.0, sampleRate);
        const double highBandHighMag = goertzelMagnitude (*highBandBuffer, 0, 8000.0, sampleRate);

        expect (lowBandLowMag > lowBandHighMag * 3.0,
                "Multipass's Low band output bus is dominated by low-frequency content, not the full mixed signal (100Hz mag "
                    + juce::String (lowBandLowMag, 3) + ", 8kHz mag " + juce::String (lowBandHighMag, 3) + ")");
        expect (highBandHighMag > highBandLowMag * 3.0,
                "Multipass's High band output bus is dominated by high-frequency content, not the full mixed signal (8kHz mag "
                    + juce::String (highBandHighMag, 3) + ", 100Hz mag " + juce::String (highBandLowMag, 3) + ")");
    }

    // --- Multipass: a band's Gain knob genuinely scales that band's own output level, for
    // re-levelling Low/Mid/High relative to each other ---
    {
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz1))->store (300.0f);
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz2))->store (3000.0f);
        apvts.getRawParameterValue (multipassBandParamId (0, 0, MultipassBandParam::gain))->store (12.0f); // Low band, +12dB
        apvts.getRawParameterValue (multipassBandParamId (0, 1, MultipassBandParam::gain))->store (0.0f);
        apvts.getRawParameterValue (multipassBandParamId (0, 2, MultipassBandParam::gain))->store (0.0f);

        MultipassModule module (apvts, 0);
        module.prepare (spec);

        auto lowTone = makeTestSignal (blockSize, 0.5f, 100.0f, sampleRate); // well inside the Low band
        juce::dsp::AudioBlock<float> block (lowTone);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const auto* lowBandBuffer = module.getOutputBusBuffer (0);
        const double boostedRms = rms (*lowBandBuffer, 0);
        const double unityRms = 0.5 * std::sqrt (0.5);

        expect (boostedRms > unityRms * 3.0,
                "Multipass Low band's Gain knob at +12dB genuinely boosts that band's own level well above unity "
                "(boosted RMS " + juce::String (boostedRms, 4) + ", unity would be " + juce::String (unityRms, 4) + ")");
    }

    // --- Multipass: exposes exactly 3 output buses and defensively refuses out-of-range ones ---
    {
        MultipassModule module (apvts, 0);
        module.prepare (spec);

        expect (module.getNumOutputBuses() == 3, "Multipass reports exactly 3 output buses (Low/Mid/High)");
        expect (module.getOutputBusBuffer (-1) == nullptr && module.getOutputBusBuffer (3) == nullptr,
                "Multipass::getOutputBusBuffer refuses out-of-range bus indices instead of reading out of bounds");
    }

    // --- Multipass: stays finite/bounded on every band even with split points pushed to the
    // frequency-range edges and nearly on top of each other ---
    {
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz1))->store (20.0f);
        apvts.getRawParameterValue (multipassParamId (0, MultipassParam::splitHz2))->store (20000.0f);
        for (int b = 0; b < kNumMultipassBands; ++b)
            apvts.getRawParameterValue (multipassBandParamId (0, b, MultipassBandParam::gain))->store (0.0f);

        MultipassModule module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.9f, 1000.0f, sampleRate);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        bool allFinite = true;
        for (int b = 0; b < kNumMultipassBands && allFinite; ++b)
            allFinite = isFiniteAndBounded (*module.getOutputBusBuffer (b), 4.0f);

        expect (allFinite, "Multipass stays finite/bounded on every band's output bus at extreme split-point settings");
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
        apvts.getRawParameterValue (slotTypeParamId (2))->store ((float) ModuleType::compressor);
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

        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::threshold))->store (-18.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::ratio))->store (4.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::attack))->store (1.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::release))->store (50.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::knee))->store (6.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::makeup))->store (0.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::mix))->store (100.0f);
        apvts.getRawParameterValue (compressorParamId (2, CompressorParam::detection))->store (0.0f); // Peak

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

    // --- LFO: Ramp Up climbs from strongly negative near phase 0 to strongly positive near phase 1 ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (5.0f); // Ramp Up
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f); // Free
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (1.0f); // 1s period
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        float earlyValue = 0.0f, lateValue = 0.0f;
        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int chunk = 0; chunk < 80; ++chunk) // stays within the first ~0.93s of the 1s period
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            if (chunk == 0) earlyValue = module.getCurrentValue();
            if (chunk == 79) lateValue = module.getCurrentValue();
        }

        expect (earlyValue < -0.7f && lateValue > 0.7f,
                "LFO Ramp Up climbs from strongly negative near phase 0 to strongly positive near phase 1 (early "
                    + juce::String (earlyValue, 3) + ", late " + juce::String (lateValue, 3) + ")");
    }

    // --- LFO: Ramp Down falls from strongly positive near phase 0 to strongly negative near phase 1 ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (6.0f); // Ramp Down
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (1.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        float earlyValue = 0.0f, lateValue = 0.0f;
        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int chunk = 0; chunk < 80; ++chunk)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            if (chunk == 0) earlyValue = module.getCurrentValue();
            if (chunk == 79) lateValue = module.getCurrentValue();
        }

        expect (earlyValue > 0.7f && lateValue < -0.7f,
                "LFO Ramp Down falls from strongly positive near phase 0 to strongly negative near phase 1 (early "
                    + juce::String (earlyValue, 3) + ", late " + juce::String (lateValue, 3) + ")");
    }

    // --- LFO: Custom drawn points evaluate, curve, and round-trip through extra state ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (7.0f); // Custom
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (1.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        const int inserted = module.addCustomPoint ({ 0.125f, -0.5f });
        expect (inserted > 0, "LFO Custom accepts an inserted draw point");
        module.setSegmentCurve (2, -0.75f); // hold the 0.25->0.5 falling segment high for longer

        const float atStart = module.evaluateCustomAt (0.0f);
        const float atPeak = module.evaluateCustomAt (0.25f);
        const float curvedMid = module.evaluateCustomAt (0.375f);

        expect (std::abs (atStart) < 1.0e-4f && atPeak > 0.95f,
                "LFO Custom evaluates drawn endpoint/point values");
        expect (curvedMid > 0.5f,
                "LFO Custom segment curvature bends interpolation away from plain linear (mid "
                    + juce::String (curvedMid, 3) + ")");

        juce::XmlElement state ("LfoState");
        module.writeExtraState (state);

        LFOModule restored (apvts, 0, sharedServices);
        restored.readExtraState (state);

        expect (restored.getNumCustomPoints() == module.getNumCustomPoints()
                    && std::abs (restored.evaluateCustomAt (0.375f) - curvedMid) < 1.0e-4f,
                "LFO Custom drawn points and curve values round-trip through writeExtraState/readExtraState");
    }

    // --- LFO: Editing a preset shape keeps that base shape selected and marks it edited ---
    {
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->store (2.0f); // Square
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateMode))->store (0.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::rateHz))->store (1.0f);
        apvts.getRawParameterValue (lfoParamId (0, LfoParam::depth))->store (100.0f);

        LFOModule module (apvts, 0, sharedServices);
        module.prepare (spec);
        module.seedCustomFromShape (2);
        module.moveCustomPoint (0, { 0.0f, -1.0f });
        module.setCustomEdited (true);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        expect ((int) apvts.getRawParameterValue (lfoParamId (0, LfoParam::shape))->load() == 2
                    && module.isCustomEdited()
                    && module.getCurrentValue() < -0.8f,
                "LFO edited Square keeps Square selected while using the edited curve");

        juce::XmlElement state ("LfoState");
        module.writeExtraState (state);

        LFOModule restored (apvts, 0, sharedServices);
        restored.readExtraState (state);
        expect (restored.isCustomEdited() && restored.evaluateCustomAt (0.01f) < -0.8f,
                "LFO edited-preset marker and points round-trip through patch extra state");
    }

    // --- LFO Table: catalog loads a valid table, using Kilohearts factory files when installed ---
    {
        const auto& catalog = WavetableLibrary::getCatalog();
        expect (! catalog.empty(), "LFO Table wavetable catalog is not empty");

        const auto table = WavetableLibrary::loadTable (0);
        expect (table != nullptr && table->isValid(), "LFO Table loads a valid wavetable");

        if (table != nullptr && table->displayName.startsWith ("Kilohearts/"))
        {
            expect (table->numFrames == 256 && table->frameSize == 2048,
                    "Kilohearts factory LFO Table files are read as 256 frames of 2048 samples");
        }

        if (table != nullptr && table->isValid())
        {
            float rawEnergy = 0.0f;
            float smoothedEnergy = 0.0f;
            for (int i = 0; i < 64; ++i)
            {
                const float phase01 = (float) i / 64.0f;
                rawEnergy += std::abs (table->sample (0.0f, phase01, 0.0f));
                smoothedEnergy += std::abs (table->sample (0.0f, phase01, 1.0f));
            }

            expect (smoothedEnergy <= rawEnergy + 1.0e-4f,
                    "LFO Table Smooth does not increase table amplitude (raw energy "
                        + juce::String (rawEnergy, 3) + ", smoothed energy " + juce::String (smoothedEnergy, 3) + ")");
        }
    }

    // --- LFO Table: free-running source stays finite/bounded and moves over time ---
    {
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::tableIndex))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::frame))->store (1.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::smooth))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::phase))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::rateMode))->store (0.0f); // Free
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::rateHz))->store (2.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::depth))->store (100.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::retrigger))->store (0.0f);

        LfoTableModule module (apvts, 0, sharedServices);
        module.prepare (spec);

        float minVal = 1.0f;
        float maxVal = -1.0f;
        bool bounded = true;
        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int chunk = 0; chunk < 100; ++chunk)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);

            const float value = module.getCurrentModulationValue();
            bounded = bounded && std::isfinite (value) && std::abs (value) <= 1.001f;
            minVal = juce::jmin (minVal, value);
            maxVal = juce::jmax (maxVal, value);
        }

        expect (bounded, "LFO Table modulation output stays finite and within [-1, 1]");
        expect (maxVal - minVal > 0.05f,
                "LFO Table free-running output moves over time (min "
                    + juce::String (minVal, 3) + ", max " + juce::String (maxVal, 3) + ")");
    }

    // --- LFO Table: Depth scales the modulation output ---
    {
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::tableIndex))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::frame))->store (1.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::smooth))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::phase))->store (90.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::rateMode))->store (0.0f);
        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::rateHz))->store (1.0f);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        juce::MidiBuffer midi;

        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::depth))->store (100.0f);
        LfoTableModule fullDepth (apvts, 0, sharedServices);
        fullDepth.prepare (spec);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            fullDepth.process (block, midi, modMatrix);
        }

        apvts.getRawParameterValue (lfoTableParamId (0, LfoTableParam::depth))->store (25.0f);
        LfoTableModule quarterDepth (apvts, 0, sharedServices);
        quarterDepth.prepare (spec);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            quarterDepth.process (block, midi, modMatrix);
        }

        const float fullValue = std::abs (fullDepth.getCurrentModulationValue());
        const float quarterValue = std::abs (quarterDepth.getCurrentModulationValue());
        expect (quarterValue <= fullValue * 0.35f + 1.0e-4f,
                "LFO Table Depth scales modulation output (100% "
                    + juce::String (fullValue, 3) + ", 25% " + juce::String (quarterValue, 3) + ")");
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

        ModulationMatrix cableMatrix;
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

    // --- Modulation cable: two different sources can both target the same destination at once,
    // and their contributions add rather than one overriding the other ---
    {
        apvts.getRawParameterValue (filterParamId (1, FilterParam::type))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::frequency))->store (1000.0f);

        ModulationMatrix stackMatrix;
        const auto destParamId = filterParamId (1, FilterParam::frequency);

        const bool addedFirst = stackMatrix.addModConnection (0, 1, destParamId);
        expect (addedFirst, "a first modulation cable to a destination is accepted");

        const bool addedSecond = stackMatrix.addModConnection (2, 1, destParamId); // a different source slot
        expect (addedSecond, "a second modulation cable from a different source to the SAME destination is "
                              "also accepted -- a destination can take more than one incoming cable");

        const bool rejectedDuplicate = ! stackMatrix.addModConnection (0, 1, destParamId); // same source again
        expect (rejectedDuplicate, "cabling the exact same source to the exact same destination a second "
                                    "time is still rejected as a redundant duplicate");

        stackMatrix.setLfoValue (0, 0.4f);
        stackMatrix.setLfoValue (2, 0.3f);

        const float combined = stackMatrix.getOffsetForParam (destParamId, 1000.0f);
        const float expected = (0.4f + 0.3f) * 1000.0f;
        expect (std::abs (combined - expected) < 1.0e-3f,
                "two modulation cables into the same destination sum their contributions rather than one "
                "overriding the other (combined offset " + juce::String (combined, 2) + ", expected "
                    + juce::String (expected, 2) + ")");
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

    // --- Spectral Clipper: stays finite everywhere, and settles to a bounded level, at extreme
    //     Drive + every Shape ---
    //
    // Unlike the analogous Lossy test above, this deliberately does NOT reprocess the same short
    // buffer through process() repeatedly -- Lossy has no pre-gain knob, so re-feeding its own
    // bounded output back in stays naturally bounded, but Spectral Clipper's Drive would then
    // compound (each of the 20 passes re-amplifying the previous pass's already-clipped output by
    // another +24dB before clipping again), which isn't how a continuous audio stream ever
    // actually drives this module. A single continuous pass over a fresh signal matches real
    // usage and is what the Ceiling-comparison test right below also relies on.
    //
    // The bound is only checked from settleSamples onward, not over the very first hop: any
    // overlap-add STFT reconstruction needs a few hops before enough overlapping windows have
    // accumulated for the steady-state normalisation to be accurate (the same startup ramp Lossy
    // has too), so the first ~windowSize samples can transiently read louder than the settled
    // signal that follows -- finiteness (no NaN/Inf) is still asserted over the entire buffer,
    // startup included.
    for (int shapeIndex = 0; shapeIndex < getSpectralClipperShapeChoices().size(); ++shapeIndex)
    {
        apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::drive))->store (24.0f);
        apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::ceiling))->store (-24.0f);
        apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::shape))->store ((float) shapeIndex);
        apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::mix))->store (100.0f);
        apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::output))->store (0.0f);

        SpectralClipperModule module (apvts, 0);
        module.prepare (spec);

        const int numChunks = 20;
        auto buffer = makeTestSignal (blockSize * numChunks, 0.9f, 1000.0f, sampleRate);
        juce::dsp::AudioBlock<float> fullBlock (buffer);
        for (int chunk = 0; chunk < numChunks; ++chunk)
        {
            auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
            juce::MidiBuffer midi;
            module.process (sub, midi, modMatrix);
        }

        expect (isFinite (buffer),
                "Spectral Clipper shape " + juce::String (shapeIndex) + " never produces NaN/Inf at extreme Drive with a tight Ceiling");

        const int settleSamples = 2048;
        const float settledPeak = juce::jmax (buffer.getMagnitude (0, settleSamples, buffer.getNumSamples() - settleSamples),
                                               buffer.getMagnitude (1, settleSamples, buffer.getNumSamples() - settleSamples));
        expect (settledPeak < 2.0f,
                "Spectral Clipper shape " + juce::String (shapeIndex) + " settles to a bounded level (" + juce::String (settledPeak, 3)
                    + ") well after the STFT's startup ramp, at extreme Drive with a tight Ceiling");
    }

    // --- Spectral Clipper: a tight Ceiling actually caps the reconstructed level below a loose
    //     one, for the same driven input -- confirms the per-bin magnitude clip is doing
    //     something audible, not just a no-op knob ---
    {
        const int numChunks = 40;
        const int totalSamples = blockSize * numChunks;

        auto runOnce = [&] (float ceilingDb) -> juce::AudioBuffer<float>
        {
            apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::drive))->store (24.0f);
            apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::ceiling))->store (ceilingDb);
            apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::shape))->store (0.0f); // Hard
            apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::mix))->store (100.0f);
            apvts.getRawParameterValue (spectralClipperParamId (0, SpectralClipperParam::output))->store (0.0f);

            SpectralClipperModule module (apvts, 0);
            module.prepare (spec);

            auto fullBuffer = makeTestSignal (totalSamples, 0.9f, 1000.0f, sampleRate);
            juce::dsp::AudioBlock<float> fullBlock (fullBuffer);
            for (int chunk = 0; chunk < numChunks; ++chunk)
            {
                auto sub = fullBlock.getSubBlock ((size_t) (chunk * blockSize), (size_t) blockSize);
                juce::MidiBuffer midi;
                module.process (sub, midi, modMatrix);
            }
            return fullBuffer;
        };

        auto looseBuffer = runOnce (6.0f);
        auto tightBuffer = runOnce (-24.0f);

        // Skip the STFT's own startup latency (one window's worth) and measure steady-state peak
        // over the back half of the signal.
        const int settleSamples = 2048;
        const int measureLength = totalSamples - settleSamples;

        const float loosePeak = looseBuffer.getMagnitude (0, settleSamples, measureLength);
        const float tightPeak = tightBuffer.getMagnitude (0, settleSamples, measureLength);

        expect (tightPeak < loosePeak * 0.5f,
                "Spectral Clipper with a tight Ceiling (-24dB) reconstructs a noticeably lower peak than a loose "
                "one (+6dB) for the same driven input (tight " + juce::String (tightPeak, 4) + ", loose "
                    + juce::String (loosePeak, 4) + ")");
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

    // --- EQ 8: a band's Type genuinely changes its filter shape -- High Pass removes
    // low-frequency content instead of just boosting/cutting it like the old gain-only bands ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (b)))->store (0.0f);
            apvts.getRawParameterValue (eq8BandEnabledParamId (0, b))->store (b == 0 ? 1.0f : 0.0f);
        }
        apvts.getRawParameterValue (eq8BandTypeParamId (0, 0))->store (3.0f); // High Pass
        apvts.getRawParameterValue (eq8BandFreqParamId (0, 0))->store (1000.0f);
        apvts.getRawParameterValue (eq8BandQParamId (0, 0))->store (0.707f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::output))->store (0.0f);

        Eq8Module module (apvts, 0);
        module.prepare (spec);

        auto buffer = makeTestSignal (blockSize, 0.5f, 100.0f, sampleRate); // well below the 1kHz HP cutoff
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        module.process (block, midi, modMatrix);

        const double flatRms = 0.5 * std::sqrt (0.5);
        expect (rms (buffer, 0) < flatRms * 0.3,
                "EQ 8 High Pass Type on a band removes a 100Hz tone well below its 1kHz cutoff, unlike the old "
                "gain-only peaking-only bands (output RMS " + juce::String (rms (buffer, 0), 4)
                    + ", flat would be " + juce::String (flatRms, 4) + ")");
    }

    // --- EQ 8: a disabled band contributes nothing, no matter how extreme its Gain is ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8BandEnabledParamId (0, b))->store (b == 0 ? 0.0f : 1.0f);
        for (int b = 1; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (b)))->store (0.0f);
        apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (0)))->store (12.0f); // disabled band, extreme gain
        apvts.getRawParameterValue (eq8BandFreqParamId (0, 0))->store (1000.0f);
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
                "EQ 8 a disabled band's +12dB Gain has no audible effect (input RMS " + juce::String (inputRms, 4)
                    + ", output RMS " + juce::String (outputRms, 4) + ")");
    }

    // --- EQ 8: a Bell band's Frequency knob genuinely moves which frequency gets boosted,
    // unlike the old fixed-frequency-ladder scheme it replaced ---
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            apvts.getRawParameterValue (eq8BandEnabledParamId (0, b))->store (b == 0 ? 1.0f : 0.0f);
        apvts.getRawParameterValue (eq8BandTypeParamId (0, 0))->store (0.0f); // Bell
        apvts.getRawParameterValue (eq8ParamId (0, eq8BandParam (0)))->store (12.0f);
        apvts.getRawParameterValue (eq8BandQParamId (0, 0))->store (2.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::mix))->store (100.0f);
        apvts.getRawParameterValue (eq8ParamId (0, Eq8Param::output))->store (0.0f);

        auto runAtFreq = [&] (float freqHz) -> double
        {
            apvts.getRawParameterValue (eq8BandFreqParamId (0, 0))->store (freqHz);
            Eq8Module module (apvts, 0);
            module.prepare (spec);
            auto buffer = makeTestSignal (blockSize, 0.5f, freqHz, sampleRate);
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
            return rms (buffer, 0);
        };

        const double boostAt500 = runAtFreq (500.0f);
        const double boostAt5000 = runAtFreq (5000.0f);
        const double flatRms = 0.5 * std::sqrt (0.5);

        expect (boostAt500 > flatRms * 1.5 && boostAt5000 > flatRms * 1.5,
                "EQ 8 Bell band boosts whichever frequency its Freq knob is currently set to, tried at both 500Hz "
                "and 5000Hz (" + juce::String (boostAt500, 3) + ", " + juce::String (boostAt5000, 3)
                    + " vs flat " + juce::String (flatRms, 3) + ")");
    }

    // --- EQ 8: Q narrows/widens a Bell band's affected bandwidth -- checked directly via
    // Eq8Module::makeCoefficients' magnitude response, the same static/pure function
    // Eq8CurveEditor's own combined-response curve reads from ---
    {
        auto narrowCoeffs = Eq8Module::makeCoefficients (0, sampleRate, 1000.0f, 8.0f, juce::Decibels::decibelsToGain (12.0f));
        auto wideCoeffs = Eq8Module::makeCoefficients (0, sampleRate, 1000.0f, 0.3f, juce::Decibels::decibelsToGain (12.0f));

        const double narrowMagOffFreq = narrowCoeffs->getMagnitudeForFrequency (2000.0, sampleRate);
        const double wideMagOffFreq = wideCoeffs->getMagnitudeForFrequency (2000.0, sampleRate);

        expect (narrowMagOffFreq < wideMagOffFreq,
                "EQ 8 a narrow-Q Bell band (centred at 1kHz, boosted +12dB) affects a nearby 2kHz frequency less "
                "than a wide-Q band does (narrow magnitude " + juce::String (narrowMagOffFreq, 4)
                    + ", wide magnitude " + juce::String (wideMagOffFreq, 4) + ")");
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

    // --- 3xOsc: a held note produces audible, finite/bounded output, and settles toward silence
    // after note-off once the release tail completes ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (0.0f);
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f); // Sine
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        ThreeOscModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> heldBuffer (2, blockSize);
        heldBuffer.clear();
        {
            juce::dsp::AudioBlock<float> block (heldBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
        }
        // A couple more held blocks so the (near-instant) attack/decay have settled into sustain.
        for (int i = 0; i < 2; ++i)
        {
            heldBuffer.clear();
            juce::dsp::AudioBlock<float> block (heldBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        const double heldRms = rms (heldBuffer, 0);
        expect (isFiniteAndBounded (heldBuffer, 4.0f) && heldRms > 0.05,
                "3xOsc produces audible finite/bounded output while a note is held (RMS " + juce::String (heldRms, 4) + ")");

        // Release, then process enough blocks to run well past the 50ms release tail.
        juce::AudioBuffer<float> releasedBuffer (2, blockSize);
        const int blocksForRelease = (int) std::ceil ((0.05 * sampleRate * 4.0) / blockSize) + 1;
        for (int i = 0; i < blocksForRelease; ++i)
        {
            releasedBuffer.clear();
            juce::dsp::AudioBlock<float> block (releasedBuffer);
            juce::MidiBuffer midi;
            if (i == 0)
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            module.process (block, midi, modMatrix);
        }

        const double releasedRms = rms (releasedBuffer, 0);
        expect (releasedRms < heldRms * 0.05,
                "3xOsc settles toward silence well after note-off's release tail completes (held RMS "
                    + juce::String (heldRms, 4) + ", released RMS " + juce::String (releasedRms, 4) + ")");
    }

    // --- 3xOsc: two simultaneously-held notes both contribute their own frequency content
    // (polyphony actually works, not just "a" voice being retriggered) ---
    {
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f); // Sine
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        auto runNotes = [&] (std::vector<int> notes) -> juce::AudioBuffer<float>
        {
            ThreeOscModule polyModule (apvts, 0);
            polyModule.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize * 4);
            buffer.clear();
            for (int chunk = 0; chunk < 4; ++chunk)
            {
                juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, chunk * blockSize, blockSize);
                juce::dsp::AudioBlock<float> block (sub);
                juce::MidiBuffer midi;
                if (chunk == 0)
                    for (int n : notes)
                        midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0);
                polyModule.process (block, midi, modMatrix);
            }
            return buffer;
        };

        // Note 57 (A3, ~220Hz) and note 69 (A4, ~440Hz) together vs. each alone.
        auto bothNotes = runNotes ({ 57, 69 });
        auto onlyLow = runNotes ({ 57 });
        auto onlyHigh = runNotes ({ 69 });

        const int settle = blockSize; // skip the first block so onset transients don't skew magnitude
        juce::AudioBuffer<float> bothTail (1, bothNotes.getNumSamples() - settle);
        bothTail.copyFrom (0, 0, bothNotes, 0, settle, bothTail.getNumSamples());
        juce::AudioBuffer<float> lowTail (1, onlyLow.getNumSamples() - settle);
        lowTail.copyFrom (0, 0, onlyLow, 0, settle, lowTail.getNumSamples());
        juce::AudioBuffer<float> highTail (1, onlyHigh.getNumSamples() - settle);
        highTail.copyFrom (0, 0, onlyHigh, 0, settle, highTail.getNumSamples());

        const double magLowInBoth = goertzelMagnitude (bothTail, 0, 220.0, sampleRate);
        const double magHighInBoth = goertzelMagnitude (bothTail, 0, 440.0, sampleRate);
        const double magLowAlone = goertzelMagnitude (lowTail, 0, 220.0, sampleRate);
        const double magHighAlone = goertzelMagnitude (highTail, 0, 440.0, sampleRate);

        expect (magLowInBoth > magLowAlone * 0.5 && magHighInBoth > magHighAlone * 0.5,
                "3xOsc plays two simultaneously-held notes as genuine polyphony -- both notes' own "
                "frequency content shows up together near as strongly as each does alone (220Hz: "
                    + juce::String (magLowInBoth, 3) + " vs " + juce::String (magLowAlone, 3)
                    + ", 440Hz: " + juce::String (magHighInBoth, 3) + " vs " + juce::String (magHighAlone, 3) + ")");
    }

    // --- 3xOsc: Coarse tune genuinely shifts pitch (+12 semitones -> one octave up) ---
    {
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f); // Sine
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        auto runWithCoarse = [&] (float coarse) -> juce::AudioBuffer<float>
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, 0, ThreeOscOscParam::coarse))->store (coarse);

            ThreeOscModule tuneModule (apvts, 0);
            tuneModule.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize * 3);
            buffer.clear();
            for (int chunk = 0; chunk < 3; ++chunk)
            {
                juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, chunk * blockSize, blockSize);
                juce::dsp::AudioBlock<float> block (sub);
                juce::MidiBuffer midi;
                if (chunk == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); // middle C, ~261.63Hz
                tuneModule.process (block, midi, modMatrix);
            }
            return buffer;
        };

        auto atZero = runWithCoarse (0.0f);
        auto upOctave = runWithCoarse (12.0f);

        const int settle = blockSize;
        juce::AudioBuffer<float> zeroTail (1, atZero.getNumSamples() - settle);
        zeroTail.copyFrom (0, 0, atZero, 0, settle, zeroTail.getNumSamples());
        juce::AudioBuffer<float> octaveTail (1, upOctave.getNumSamples() - settle);
        octaveTail.copyFrom (0, 0, upOctave, 0, settle, octaveTail.getNumSamples());

        const double baseFreq = 261.63, octaveFreq = 523.25;
        const double magBaseAtZero = goertzelMagnitude (zeroTail, 0, baseFreq, sampleRate);
        const double magOctaveAtZero = goertzelMagnitude (zeroTail, 0, octaveFreq, sampleRate);
        const double magBaseAtOctave = goertzelMagnitude (octaveTail, 0, baseFreq, sampleRate);
        const double magOctaveAtOctave = goertzelMagnitude (octaveTail, 0, octaveFreq, sampleRate);

        expect (magBaseAtZero > magOctaveAtZero * 3.0 && magOctaveAtOctave > magBaseAtOctave * 3.0,
                "3xOsc Coarse +12 semitones shifts a held note's pitch up one octave (base-freq energy "
                "dominates at Coarse=0, octave-freq energy dominates at Coarse=+12: "
                    + juce::String (magBaseAtZero, 3) + "/" + juce::String (magOctaveAtZero, 3) + " vs "
                    + juce::String (magBaseAtOctave, 3) + "/" + juce::String (magOctaveAtOctave, 3) + ")");
    }

    // --- 3xOsc: ADSR actually shapes amplitude over time -- a slow attack starts near-silent and
    // rises, rather than jumping straight to full level ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.5f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        ThreeOscModule envModule (apvts, 0);
        envModule.prepare (spec);

        juce::AudioBuffer<float> earlyBuffer (2, blockSize);
        earlyBuffer.clear();
        {
            juce::dsp::AudioBlock<float> block (earlyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            envModule.process (block, midi, modMatrix);
        }

        // Process forward well past the 0.5s attack ramp.
        juce::AudioBuffer<float> laterBuffer (2, blockSize);
        const int blocksToAdvance = (int) std::ceil ((0.5 * sampleRate * 2.0) / blockSize);
        for (int i = 0; i < blocksToAdvance; ++i)
        {
            laterBuffer.clear();
            juce::dsp::AudioBlock<float> block (laterBuffer);
            juce::MidiBuffer midi;
            envModule.process (block, midi, modMatrix);
        }

        const double earlyRms = rms (earlyBuffer, 0);
        const double laterRms = rms (laterBuffer, 0);
        expect (laterRms > earlyRms * 3.0,
                "3xOsc's Attack knob genuinely ramps amplitude up over time rather than jumping straight "
                "to full level (early-block RMS " + juce::String (earlyRms, 4) + ", post-attack RMS "
                    + juce::String (laterRms, 4) + ")");
    }

    // --- 3xOsc: stays finite/bounded at extreme settings -- max FM depth, all 4 waveforms
    // in play (Saw/Square PolyBLEP-corrected), a very high note (stresses PolyBLEP's dt clamp),
    // and more simultaneously-triggered notes than there are voices (forces voice stealing) ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (24.0f);

        const int waveformForOsc[kNumThreeOscOscillators] = { 2, 3, 1 }; // Saw, Square, Triangle
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store ((float) waveformForOsc[osc]);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (osc == 0 ? 36.0f : -36.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (osc % 2 == 0 ? 100.0f : -100.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (osc % 2 == 0 ? 1.0f : -1.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (100.0f);
        }

        ThreeOscModule stressModule (apvts, 0);
        stressModule.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        for (int block = 0; block < 5; ++block)
        {
            buffer.clear();
            juce::dsp::AudioBlock<float> dspBlock (buffer);
            juce::MidiBuffer midi;
            if (block == 0)
                for (int n = 100; n < 120; ++n) // 20 distinct notes, more than kMaxThreeOscVoices (16)
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 127), 0);
            stressModule.process (dspBlock, midi, modMatrix);
        }

        // Bound set generously above the ~190x peak this legitimately reaches at these settings
        // (16 stolen voices x 3 oscillators x +24dB output x 100% FM overshoot, confirmed stable
        // rather than growing across blocks when checked by hand) -- this check is for NaN/Inf/
        // runaway growth, not for clamping legitimate extreme-stacking headroom (same reasoning
        // as Multiband Convolution's own extreme-stress bound above).
        expect (isFiniteAndBounded (buffer, 300.0f),
                "3xOsc stays finite/bounded at extreme FM depth/output/tuning with more simultaneous "
                "notes than voices (forces voice stealing)");
    }

    // --- 3xOsc Mono/Legato: a second held note retargets the single voice instead of adding a
    // simultaneous second one ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::monoLegato))->store (1.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::glide))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (0.0f); // preceding test leaves this at 100%
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (0.0f); // preceding test leaves this at +24dB
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f); // Sine
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        ThreeOscModule monoModule (apvts, 0);
        monoModule.prepare (spec);

        const int totalChunks = 6;
        juce::AudioBuffer<float> buffer (2, blockSize * totalChunks);
        buffer.clear();
        for (int chunk = 0; chunk < totalChunks; ++chunk)
        {
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, chunk * blockSize, blockSize);
            juce::dsp::AudioBlock<float> block (sub);
            juce::MidiBuffer midi;
            if (chunk == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0); // A3, ~220Hz
            if (chunk == 1)
                midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0); // A4, ~440Hz -- legato retarget, no note-off for 57
            monoModule.process (block, midi, modMatrix);
        }

        const int settle = blockSize * 3; // well past the retarget's instant snap
        juce::AudioBuffer<float> tail (1, buffer.getNumSamples() - settle);
        tail.copyFrom (0, 0, buffer, 0, settle, tail.getNumSamples());

        const double magLow = goertzelMagnitude (tail, 0, 220.0, sampleRate);
        const double magHigh = goertzelMagnitude (tail, 0, 440.0, sampleRate);

        expect (magHigh > magLow * 3.0,
                "3xOsc Mono/Legato retargets its single voice to the newest note rather than adding a second "
                "simultaneous voice -- 440Hz dominates over 220Hz well after the retarget (220Hz "
                    + juce::String (magLow, 3) + ", 440Hz " + juce::String (magHigh, 3) + ")");
    }

    // --- 3xOsc Mono/Legato: retargeting via a legato note-on does not retrigger the amp envelope ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::monoLegato))->store (1.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::glide))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.3f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (0.0f);
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        ThreeOscModule legatoModule (apvts, 0);
        legatoModule.prepare (spec);

        juce::AudioBuffer<float> midBuffer (2, blockSize);
        for (int chunk = 0; chunk < 10; ++chunk)
        {
            juce::AudioBuffer<float> scratch (2, blockSize);
            scratch.clear();
            juce::dsp::AudioBlock<float> block (scratch);
            juce::MidiBuffer midi;
            if (chunk == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            legatoModule.process (block, midi, modMatrix);
            if (chunk == 9)
                midBuffer = scratch; // still ~120ms into a 300ms attack -- partway up, nowhere near silent
        }

        juce::AudioBuffer<float> postBuffer (2, blockSize);
        postBuffer.clear();
        {
            juce::dsp::AudioBlock<float> block (postBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); // legato retarget, note 60 still held
            legatoModule.process (block, midi, modMatrix);
        }

        const double midRms = rms (midBuffer, 0);
        const double postRms = rms (postBuffer, 0);

        expect (postRms > midRms * 0.5,
                "3xOsc Mono/Legato's legato retarget does not retrigger the amp envelope -- amplitude right "
                "after the legato note-on (RMS " + juce::String (postRms, 4) + ") stays close to where the "
                "still-mid-attack envelope already was (RMS " + juce::String (midRms, 4)
                    + "), rather than resetting near zero like a fresh trigger would");
    }

    // --- 3xOsc Mono/Legato: releasing one note of an overlapping run falls back to the other held
    // note rather than cutting to silence ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::monoLegato))->store (1.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::glide))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (0.0f);
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        ThreeOscModule fallbackModule (apvts, 0);
        fallbackModule.prepare (spec);

        const int totalChunks = 10;
        juce::AudioBuffer<float> buffer (2, blockSize * totalChunks);
        buffer.clear();
        for (int chunk = 0; chunk < totalChunks; ++chunk)
        {
            juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, chunk * blockSize, blockSize);
            juce::dsp::AudioBlock<float> block (sub);
            juce::MidiBuffer midi;
            if (chunk == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); // middle C, held throughout
            if (chunk == 2)
                midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0); // legato retarget on top
            if (chunk == 5)
                midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0); // release the newer note -- 60 is still held
            fallbackModule.process (block, midi, modMatrix);
        }

        const int settle = blockSize * 8; // well after the fallback retarget's instant snap
        juce::AudioBuffer<float> tail (1, buffer.getNumSamples() - settle);
        tail.copyFrom (0, 0, buffer, 0, settle, tail.getNumSamples());

        const double tailRms = rms (tail, 0);
        const double magFallback = goertzelMagnitude (tail, 0, 261.63, sampleRate); // middle C

        expect (tailRms > 0.05,
                "3xOsc Mono/Legato keeps sounding after releasing one note of an overlapping run, as long as "
                "another note is still held (tail RMS " + juce::String (tailRms, 4) + ")");
        expect (magFallback > 0.1,
                "3xOsc Mono/Legato falls back to the still-held note's own pitch after the newer note releases "
                "(middle-C magnitude " + juce::String (magFallback, 3) + ")");
    }

    // --- 3xOsc Mono/Legato: Glide ramps pitch smoothly over Glide Time, unlike Glide off which
    // snaps to the new pitch immediately on retarget ---
    {
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::monoLegato))->store (1.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::attack))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::decay))->store (0.001f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::release))->store (0.05f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm1to2))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::fm2to3))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::output))->store (0.0f);
        apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::glideTimeMs))->store (300.0f);
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::waveform))->store (0.0f); // Sine
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::fine))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::pan))->store (0.0f);
            apvts.getRawParameterValue (threeOscOscParamId (0, osc, ThreeOscOscParam::level))->store (osc == 0 ? 100.0f : 0.0f);
        }

        const int retargetChunk = 4;
        const int soonChunk = retargetChunk + 5;   // ~58ms into the 300ms glide -- still transitioning
        const int lateChunk = retargetChunk + 40;  // ~464ms in -- well past the 300ms glide, settled
        const int totalChunks = lateChunk + 2;

        auto runGlideTest = [&] (bool glideOn) -> juce::AudioBuffer<float>
        {
            apvts.getRawParameterValue (threeOscParamId (0, ThreeOscParam::glide))->store (glideOn ? 1.0f : 0.0f);

            ThreeOscModule glideModule (apvts, 0);
            glideModule.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize * totalChunks);
            buffer.clear();
            for (int chunk = 0; chunk < totalChunks; ++chunk)
            {
                juce::AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), 2, chunk * blockSize, blockSize);
                juce::dsp::AudioBlock<float> block (sub);
                juce::MidiBuffer midi;
                if (chunk == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); // middle C, ~261.63Hz
                if (chunk == retargetChunk)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0); // one octave up, ~523.25Hz, legato retarget
                glideModule.process (block, midi, modMatrix);
            }
            return buffer;
        };

        auto extractRange = [&] (const juce::AudioBuffer<float>& buffer, int startChunk, int numChunksToTake) -> juce::AudioBuffer<float>
        {
            juce::AudioBuffer<float> out (1, blockSize * numChunksToTake);
            out.copyFrom (0, 0, buffer, 0, startChunk * blockSize, out.getNumSamples());
            return out;
        };

        auto withGlide = runGlideTest (true);
        auto withoutGlide = runGlideTest (false);

        const double targetFreq = 523.25, baseFreq = 261.63;

        auto soonWithGlide = extractRange (withGlide, soonChunk, 2);
        auto soonWithoutGlide = extractRange (withoutGlide, soonChunk, 2);
        auto lateWithGlide = extractRange (withGlide, lateChunk, 2);

        const double magTargetSoonGlide = goertzelMagnitude (soonWithGlide, 0, targetFreq, sampleRate);
        const double magTargetSoonNoGlide = goertzelMagnitude (soonWithoutGlide, 0, targetFreq, sampleRate);
        const double magTargetLateGlide = goertzelMagnitude (lateWithGlide, 0, targetFreq, sampleRate);
        const double magBaseLateGlide = goertzelMagnitude (lateWithGlide, 0, baseFreq, sampleRate);

        expect (magTargetSoonNoGlide > 0.1 && magTargetSoonNoGlide > magTargetSoonGlide * 2.0,
                "3xOsc Glide off snaps straight to the new note's pitch on legato retarget, while Glide on "
                "is still partway through its ramp at that same point in time (target-freq magnitude, no "
                "glide: " + juce::String (magTargetSoonNoGlide, 3) + ", with glide: " + juce::String (magTargetSoonGlide, 3) + ")");

        expect (magTargetLateGlide > magBaseLateGlide * 3.0,
                "3xOsc Glide eventually reaches the target pitch once Glide Time has fully elapsed (target-freq "
                "magnitude " + juce::String (magTargetLateGlide, 3) + " vs base-freq " + juce::String (magBaseLateGlide, 3) + ")");
    }

    // --- WT Synth: built-in basic waveforms are always available even before Kilohearts tables ---
    {
        const auto names = WavetableLibrary::getCatalogDisplayNames();
        expect (names.size() >= 4
                    && names[0] == "Built-in/Sine"
                    && names[1] == "Built-in/Triangle"
                    && names[2] == "Built-in/Saw"
                    && names[3] == "Built-in/Square",
                "WT Synth wavetable catalog starts with basic built-in Sine/Triangle/Saw/Square tables");
    }

    // --- WT Synth: a held MIDI note produces audible wavetable output ---
    {
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::attack))->store (0.001f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::decay))->store (0.001f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::release))->store (0.05f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::output))->store (0.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::algorithm))->store (0.0f);

        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f); // Built-in/Sine
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::frame))->store (1.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::smooth))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::pan))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::output))->store (0.0f);
        }

        WavetableSynthModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
        module.process (block, midi, modMatrix);

        const double heldRms = rms (buffer, 0);
        expect (heldRms > 0.01 && isFiniteAndBounded (buffer, 2.0f),
                "WT Synth produces audible finite/bounded wavetable output while a note is held (RMS "
                    + juce::String (heldRms, 4) + ")");
    }

    // --- WT Synth: graph-facing output is a single normal audio output ---
    {
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::frame))->store (1.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::smooth))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::pan))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::output))->store (0.0f);
        }

        WavetableSynthModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        module.process (block, midi, modMatrix);

        expect (module.getNumOutputBuses() == 1
                    && module.getOutputBusBuffer (-1) == nullptr
                    && module.getOutputBusBuffer (kNumWavetableSynthOutputs) == nullptr,
                "WT Synth exposes one graph-facing output and refuses out-of-range bus indices");
    }

    // --- WT Synth: Master Pitch and Bend Range move the oscillator pitch ---
    {
        auto configureBasicWt = [&] (float masterPitch, float bendRange, int polyphony)
        {
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::attack))->store (0.001f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::decay))->store (0.001f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::sustain))->store (100.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::release))->store (0.05f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::output))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::masterPitch))->store (masterPitch);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::bendRange))->store (bendRange);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::polyphony))->store ((float) polyphony);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::monoLegato))->store (0.0f);

            for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
            {
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::frame))->store (1.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::smooth))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::pan))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::output))->store (0.0f);
            }
        };

        auto renderNote = [&] (float masterPitch, float bendRange, int bendValue)
        {
            configureBasicWt (masterPitch, bendRange, 8);
            WavetableSynthModule module (apvts, 0);
            module.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            if (bendValue >= 0)
                midi.addEvent (juce::MidiMessage::pitchWheel (1, bendValue), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 1);
            module.process (block, midi, modMatrix);
            return buffer;
        };

        auto octaveUp = renderNote (12.0f, 2.0f, -1);
        const double octaveUp220 = goertzelMagnitude (octaveUp, 0, 220.0, sampleRate);
        const double octaveUp440 = goertzelMagnitude (octaveUp, 0, 440.0, sampleRate);
        expect (octaveUp440 > octaveUp220 * 3.0,
                "WT Synth Master Pitch +12 semitones shifts a held note up one octave (440Hz "
                    + juce::String (octaveUp440, 3) + " vs 220Hz " + juce::String (octaveUp220, 3) + ")");

        auto bentUp = renderNote (0.0f, 12.0f, 16383);
        const double bent220 = goertzelMagnitude (bentUp, 0, 220.0, sampleRate);
        const double bent440 = goertzelMagnitude (bentUp, 0, 440.0, sampleRate);
        expect (bent440 > bent220 * 3.0,
                "WT Synth Bend Range maps full-up pitch wheel to the configured semitone span (440Hz "
                    + juce::String (bent440, 3) + " vs 220Hz " + juce::String (bent220, 3) + ")");
    }

    // --- WT Synth: Polyphony limits the number of simultaneously sounding voices ---
    {
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::polyphony))->store (1.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::masterPitch))->store (0.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::bendRange))->store (2.0f);

        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (0.0f);
        }

        WavetableSynthModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 8);
        module.process (block, midi, modMatrix);

        const double low = goertzelMagnitude (buffer, 0, 220.0, sampleRate);
        const double high = goertzelMagnitude (buffer, 0, 440.0, sampleRate);
        expect (high > low * 3.0,
                "WT Synth Polyphony=1 steals the older voice so the newer note dominates (440Hz "
                    + juce::String (high, 3) + " vs 220Hz " + juce::String (low, 3) + ")");
    }

    // --- WT Synth: Unison/Spread algorithms change the oscillator output while staying bounded ---
    {
        auto configureWtUnison = [&] (float unison, float spread, float algorithm)
        {
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::attack))->store (0.001f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::decay))->store (0.001f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::sustain))->store (100.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::release))->store (0.05f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::output))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::polyphony))->store (8.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::masterPitch))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::unison))->store (unison);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::spread))->store (spread);
            apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::algorithm))->store (algorithm);

            for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
            {
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::frame))->store (1.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::smooth))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::pan))->store (0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
                apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (0.0f);
            }
        };

        auto renderUnison = [&] (float unison, float spread, float algorithm)
        {
            configureWtUnison (unison, spread, algorithm);
            WavetableSynthModule module (apvts, 0);
            module.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
            return buffer;
        };

        auto single = renderUnison (1.0f, 0.0f, 0.0f);
        auto spread = renderUnison (5.0f, 65.0f, 0.0f);

        double diff = 0.0;
        for (int i = 0; i < blockSize; ++i)
            diff += std::abs (single.getSample (0, i) - spread.getSample (0, i));

        expect (diff > 1.0 && isFiniteAndBounded (spread, 8.0f),
                "WT Synth Unison/Spread changes the oscillator output while staying bounded (absolute diff "
                    + juce::String (diff, 3) + ")");
    }

    // --- WT Synth: incoming audio acts as external FM for the carrier ---
    {
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::attack))->store (0.001f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::decay))->store (0.001f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::release))->store (0.05f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::output))->store (0.0f);

        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (gen == 0 ? 1.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::frame))->store (1.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::smooth))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::pan))->store (0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (gen == 0 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (gen == 0 ? 100.0f : 0.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::output))->store (0.0f);
        }

        auto renderWithInputGain = [&] (float inputGain)
        {
            WavetableSynthModule module (apvts, 0);
            module.prepare (spec);

            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            for (int i = 0; i < blockSize; ++i)
            {
                const float mod = inputGain * std::sin ((float) (juce::MathConstants<double>::twoPi * 660.0 * (double) i / sampleRate));
                buffer.setSample (0, i, mod);
                buffer.setSample (1, i, mod);
            }
            juce::dsp::AudioBlock<float> block (buffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
            return buffer;
        };

        auto dryFmBuffer = renderWithInputGain (0.0f);
        auto externalFmBuffer = renderWithInputGain (0.75f);
        double diff = 0.0;
        for (int i = 0; i < blockSize; ++i)
            diff += std::abs (dryFmBuffer.getSample (0, i) - externalFmBuffer.getSample (0, i));

        expect (diff > 1.0,
                "WT Synth external FM input changes the carrier output (absolute diff "
                    + juce::String (diff, 3) + ")");
    }

    // --- WT Synth: external FM and extreme tuning stay finite/bounded under voice stealing ---
    {
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::output))->store (12.0f);
        apvts.getRawParameterValue (wavetableSynthParamId (0, WavetableSynthParam::algorithm))->store (0.0f);
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::enabled))->store (1.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::table))->store ((float) (gen % 4));
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::coarse))->store (gen % 2 == 0 ? 48.0f : -48.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fine))->store (gen % 2 == 0 ? 100.0f : -100.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::level))->store (100.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::fm))->store (100.0f);
            apvts.getRawParameterValue (wavetableSynthGenParamId (0, gen, WavetableSynthGenParam::output))->store ((float) (gen % kNumWavetableSynthOutputs));
        }

        WavetableSynthModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> buffer (2, blockSize);
        buffer.clear();
        juce::dsp::AudioBlock<float> block (buffer);
        juce::MidiBuffer midi;
        for (int n = 92; n < 112; ++n)
            midi.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 110), n - 92);
        module.process (block, midi, modMatrix);

        expect (isFiniteAndBounded (buffer, 500.0f), "WT Synth stays finite/bounded at extreme FM depth/output/tuning with more notes than voices");
    }

    // --- ADSR: sustains while a note is held, releases only once it's released ---
    {
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::attack))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::decay))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::sustain))->store (70.0f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::release))->store (0.05f);

        AdsrModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
        }
        for (int i = 0; i < 3; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        const float sustainValue = module.getCurrentModulationValue();
        expect (std::abs (sustainValue - 0.70f) < 0.05f,
                "ADSR reaches its Sustain level while a note is held (value " + juce::String (sustainValue, 4) + ")");

        for (int i = 0; i < 20; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        expect (std::abs (module.getCurrentModulationValue() - sustainValue) < 0.02f,
                "ADSR stays at Sustain indefinitely while the note is held, not a fixed-length one-shot");

        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            module.process (block, midi, modMatrix);
        }
        const int blocksForRelease = (int) std::ceil ((0.05 * sampleRate * 4.0) / blockSize) + 1;
        for (int i = 0; i < blocksForRelease; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        expect (module.getCurrentModulationValue() < 0.05f,
                "ADSR releases to near-zero well after note-off's release tail completes");
    }

    // --- ADSR: releasing one note out of a held chord doesn't cut the envelope -- only releasing
    // the LAST held note starts the release stage (mono, last-note-wins is too aggressive here) ---
    {
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::attack))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::decay))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::sustain))->store (80.0f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::release))->store (0.05f);

        AdsrModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
        }
        for (int i = 0; i < 3; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        const float heldValue = module.getCurrentModulationValue();

        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            module.process (block, midi, modMatrix);
        }
        for (int i = 0; i < 10; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        expect (std::abs (module.getCurrentModulationValue() - heldValue) < 0.02f,
                "ADSR keeps sustaining after releasing one note out of a held chord, as long as another is still held");

        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
            module.process (block, midi, modMatrix);
        }
        const int blocksForRelease = (int) std::ceil ((0.05 * sampleRate * 4.0) / blockSize) + 1;
        for (int i = 0; i < blocksForRelease; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        expect (module.getCurrentModulationValue() < 0.05f,
                "ADSR only releases once the LAST held note of a chord is released");
    }

    // --- Modulation cable: an ADSR routed to Filter Frequency actually moves the cutoff once it
    // reaches Sustain ---
    {
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::attack))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::decay))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::release))->store (0.05f);

        apvts.getRawParameterValue (filterParamId (1, FilterParam::type))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::resonance))->store (0.707f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::output))->store (0.0f);

        ModulationMatrix cableMatrix;
        const bool added = cableMatrix.addModConnection (0, 1, filterParamId (1, FilterParam::frequency));
        expect (added, "a modulation cable from an ADSR slot to Filter Frequency is accepted");

        AdsrModule adsr (apvts, 0);
        adsr.prepare (spec);
        FilterModule filter (apvts, 1);
        filter.prepare (spec);

        cableMatrix.setLfoValue (0, adsr.getCurrentModulationValue());
        auto closedBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (closedBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            adsr.process (block, midi, modMatrix);
        }
        for (int i = 0; i < 3; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            adsr.process (block, midi, modMatrix);
        }
        cableMatrix.setLfoValue (0, adsr.getCurrentModulationValue());

        auto openBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (openBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        const double closedRms = rms (closedBuffer, 0);
        const double openRms = rms (openBuffer, 0);
        expect (openRms > closedRms * 1.5,
                "an ADSR modulation cable into Filter Frequency opens the cutoff once the envelope reaches "
                "Sustain (closed RMS " + juce::String (closedRms, 4) + ", open RMS " + juce::String (openRms, 4) + ")");
    }

    // --- ADSR: stays finite/bounded [0,1] under rapid alternating note-on/note-off ---
    {
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::attack))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::decay))->store (0.001f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::sustain))->store (100.0f);
        apvts.getRawParameterValue (adsrParamId (0, AdsrParam::release))->store (0.001f);

        AdsrModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int block = 0; block < 30; ++block)
        {
            juce::dsp::AudioBlock<float> b (dummyBuffer);
            juce::MidiBuffer midi;
            if (block % 2 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            else
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            module.process (b, midi, modMatrix);
        }

        const float v = module.getCurrentModulationValue();
        expect (std::isfinite (v) && v >= 0.0f && v <= 1.01f,
                "ADSR stays finite/bounded [0,1] under rapid alternating note-on/note-off (value "
                    + juce::String (v, 4) + ")");
    }

    // --- Envelope: one-shot playback follows the drawn shape, ignores note-off entirely, and
    // holds at the final point's value once finished ---
    {
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::length))->store (0.2f);
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::depth))->store (100.0f);

        EnvelopeModule module (apvts, 0);
        module.prepare (spec);

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 1); // immediate release -- must be ignored
            module.process (block, midi, modMatrix);
        }

        const int blocksToHalfway = juce::jmax (1, (int) std::round ((0.1 * sampleRate) / blockSize));
        for (int i = 0; i < blocksToHalfway; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }

        const float midValue = module.getCurrentModulationValue();
        expect (midValue > 0.25f && midValue < 0.75f,
                "Envelope's one-shot playback follows the drawn shape over time (default linear ramp, "
                "value partway through Length is roughly mid-range: " + juce::String (midValue, 4) + ")");
        expect (module.isPlaying(), "Envelope keeps playing through a note-off -- note-off is ignored entirely");

        const int blocksToFinish = (int) std::round ((0.3 * sampleRate) / blockSize) + 2;
        for (int i = 0; i < blocksToFinish; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            module.process (block, midi, modMatrix);
        }
        expect (! module.isPlaying() && module.getCurrentModulationValue() > 0.95f,
                "Envelope holds at the final point's value once the one-shot finishes, rather than resetting to 0");

        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            module.process (block, midi, modMatrix);
        }
        expect (module.isPlaying() && module.getCurrentModulationValue() < 0.1f,
                "A new note-on retriggers the one-shot from the beginning, even mid-hold-at-end");
    }

    // --- Envelope: breakpoint editing API (add/move/remove), including the pinned-endpoint and
    // minimum-gap rules ---
    {
        EnvelopeModule module (apvts, 0);
        module.prepare (spec);

        expect (module.getNumPoints() == 2, "Envelope starts with exactly 2 points (the pinned endpoints)");

        const int newIndex = module.addPoint ({ 0.5f, 0.2f });
        expect (newIndex == 1 && module.getNumPoints() == 3, "addPoint() inserts a new interior point in sorted order");

        module.movePoint (newIndex, { 0.5f, 0.8f });
        expect (std::abs (module.getPoint (newIndex).y - 0.8f) < 1.0e-4f, "movePoint() updates an interior point's value");

        module.movePoint (0, { 0.3f, 0.5f });
        expect (std::abs (module.getPoint (0).x) < 1.0e-4f, "movePoint() forces the first point's x to stay pinned at 0");

        const int rejected = module.addPoint ({ 0.0f, 0.9f });
        expect (rejected == -1, "addPoint() rejects a point too close to an existing one (here, the pinned start point)");

        module.removePoint (newIndex);
        expect (module.getNumPoints() == 2, "removePoint() removes an interior point");

        module.removePoint (0);
        expect (module.getNumPoints() == 2, "removePoint() refuses to delete the first (pinned) point");
    }

    // --- Envelope: breakpoints round-trip correctly through writeExtraState/readExtraState (the
    // non-APVTS persistence mechanism a variable-length point list needs) ---
    {
        EnvelopeModule sourceModule (apvts, 0);
        sourceModule.prepare (spec);
        sourceModule.addPoint ({ 0.25f, 0.9f });
        sourceModule.addPoint ({ 0.75f, 0.1f });

        juce::XmlElement stateXml ("Test");
        sourceModule.writeExtraState (stateXml);

        EnvelopeModule destModule (apvts, 0);
        destModule.prepare (spec);
        destModule.readExtraState (stateXml);

        bool matches = destModule.getNumPoints() == sourceModule.getNumPoints();
        for (int i = 0; matches && i < sourceModule.getNumPoints(); ++i)
        {
            const auto a = sourceModule.getPoint (i);
            const auto b = destModule.getPoint (i);
            if (std::abs (a.x - b.x) > 1.0e-4f || std::abs (a.y - b.y) > 1.0e-4f)
                matches = false;
        }
        expect (matches, "Envelope's breakpoints round-trip correctly through writeExtraState/readExtraState");
    }

    // --- Modulation cable: an Envelope routed to Filter Frequency actually moves the cutoff once
    // the drawn shape reaches its final value ---
    {
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::length))->store (0.05f);
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::depth))->store (100.0f);

        apvts.getRawParameterValue (filterParamId (1, FilterParam::type))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::frequency))->store (300.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::resonance))->store (0.707f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::feedback))->store (0.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::mix))->store (100.0f);
        apvts.getRawParameterValue (filterParamId (1, FilterParam::output))->store (0.0f);

        ModulationMatrix cableMatrix;
        const bool added = cableMatrix.addModConnection (0, 1, filterParamId (1, FilterParam::frequency));
        expect (added, "a modulation cable from an Envelope slot to Filter Frequency is accepted");

        EnvelopeModule envelope (apvts, 0);
        envelope.prepare (spec);
        FilterModule filter (apvts, 1);
        filter.prepare (spec);

        cableMatrix.setLfoValue (0, envelope.getCurrentModulationValue());
        auto closedBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (closedBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            envelope.process (block, midi, modMatrix);
        }
        const int blocksToFinish = (int) std::round ((0.1 * sampleRate) / blockSize) + 2;
        for (int i = 0; i < blocksToFinish; ++i)
        {
            juce::dsp::AudioBlock<float> block (dummyBuffer);
            juce::MidiBuffer midi;
            envelope.process (block, midi, modMatrix);
        }
        cableMatrix.setLfoValue (0, envelope.getCurrentModulationValue());

        auto openBuffer = makeTestSignal (blockSize, 0.5f, 8000.0f, sampleRate);
        {
            juce::dsp::AudioBlock<float> block (openBuffer);
            juce::MidiBuffer midi;
            filter.process (block, midi, cableMatrix);
        }

        const double closedRms = rms (closedBuffer, 0);
        const double openRms = rms (openBuffer, 0);
        expect (openRms > closedRms * 1.5,
                "an Envelope modulation cable into Filter Frequency opens the cutoff once the drawn shape "
                "reaches its final value (closed RMS " + juce::String (closedRms, 4) + ", open RMS "
                    + juce::String (openRms, 4) + ")");
    }

    // --- Envelope: stays finite/bounded [0,1] with a dense, rapidly-retriggered shape ---
    {
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::length))->store (0.01f);
        apvts.getRawParameterValue (envelopeParamId (0, EnvelopeParam::depth))->store (100.0f);

        EnvelopeModule module (apvts, 0);
        module.prepare (spec);
        for (int i = 0; i < 30; ++i)
            module.addPoint ({ (float) i / 30.0f + 0.005f, (i % 2 == 0) ? 1.0f : 0.0f });

        juce::AudioBuffer<float> dummyBuffer (2, blockSize);
        for (int block = 0; block < 10; ++block)
        {
            juce::dsp::AudioBlock<float> b (dummyBuffer);
            juce::MidiBuffer midi;
            if (block % 3 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            module.process (b, midi, modMatrix);
        }

        const float v = module.getCurrentModulationValue();
        expect (std::isfinite (v) && v >= 0.0f && v <= 1.01f,
                "Envelope stays finite/bounded [0,1] with a dense, rapidly-retriggered shape (value "
                    + juce::String (v, 4) + ")");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "All tests passed." << std::endl;
    else
        std::cout << failures << " test(s) failed." << std::endl;

    return failures == 0 ? 0 : 1;
}
