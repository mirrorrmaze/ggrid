#include "ParameterLayout.h"
#include "Identifiers.h"
#include "../IR/IRLibrary.h"

namespace GGrid
{
    static juce::NormalisableRange<float> skewedRange (float start, float end, float centre)
    {
        juce::NormalisableRange<float> range (start, end);
        range.setSkewForCentre (centre);
        return range;
    }

    static void addWaveshaperParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::drive), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Drive",
            NormalisableRange<float> (0.0f, 40.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::shape), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Shape",
            getWaveshaperShapeChoices(), 1));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::symmetry), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Symmetry",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::foldAmount), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Fold Amount",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::oversample), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Oversampling",
            StringArray { "Off", "2x", "4x" }, 1));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { waveshaperParamId (slotIndex, WaveshaperParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Waveshaper Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addFilterParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { filterParamId (slotIndex, FilterParam::frequency), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Frequency",
            skewedRange (20.0f, 8000.0f, 1000.0f), 1000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { filterParamId (slotIndex, FilterParam::type), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Type",
            getFilterTypeChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { filterParamId (slotIndex, FilterParam::resonance), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Resonance",
            skewedRange (0.1f, 20.0f, 1.0f), 0.707f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { filterParamId (slotIndex, FilterParam::feedback), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Feedback",
            NormalisableRange<float> (-0.95f, 0.95f, 0.001f), 0.5f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { filterParamId (slotIndex, FilterParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { filterParamId (slotIndex, FilterParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addDelayParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::time), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Time",
            skewedRange (1.0f, 2000.0f, 300.0f), 300.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { delayParamId (slotIndex, DelayParam::sync), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Sync",
            false));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { delayParamId (slotIndex, DelayParam::division), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Division",
            getDelayDivisionChoices(), 2)); // default "1/4"

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::feedback), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Feedback",
            NormalisableRange<float> (-0.98f, 0.98f, 0.001f), 0.4f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::saturation), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Feedback Saturation",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::lowCut), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Low Cut",
            skewedRange (20.0f, 2000.0f, 100.0f), 20.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::hiCut), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Hi Cut",
            skewedRange (1000.0f, 20000.0f, 8000.0f), 20000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { delayParamId (slotIndex, DelayParam::pingPong), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Ping-Pong",
            false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { delayParamId (slotIndex, DelayParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Delay Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addDynamicsParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::threshold), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -18.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::ratio), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Ratio",
            skewedRange (1.0f, 20.0f, 4.0f), 4.0f, AudioParameterFloatAttributes().withLabel (":1")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::attack), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Attack",
            skewedRange (0.1f, 200.0f, 10.0f), 10.0f, AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Release",
            skewedRange (5.0f, 1000.0f, 100.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::makeup), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Makeup",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { dynamicsParamId (slotIndex, DynamicsParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Dynamics Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    static void addConvolutionParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::irIndex), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution IR",
            IRLibrary::getCatalogDisplayNames(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::tone), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Tone",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::fadeIn), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Fade In",
            NormalisableRange<float> (0.0f, 500.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::fadeOut), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Fade Out",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::stretch), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Stretch",
            skewedRange (0.25f, 4.0f, 1.0f), 1.0f,
            AudioParameterFloatAttributes().withLabel ("x")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { convolutionParamId (slotIndex, ConvolutionParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Convolution Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addUtilityParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::gain), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Gain",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::pan), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Pan",
            NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::width), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Width",
            NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::mono), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Mono",
            false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::phaseInvertL), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Phase Invert L",
            false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { utilityParamId (slotIndex, UtilityParam::phaseInvertR), 1 },
            "Slot " + String (slotIndex + 1) + " Utility Phase Invert R",
            false));
    }

    static void addRingModParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { ringModParamId (slotIndex, RingModParam::mode), 1 },
            "Slot " + String (slotIndex + 1) + " Ring Mod Mode",
            getRingModModeChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ringModParamId (slotIndex, RingModParam::frequency), 1 },
            "Slot " + String (slotIndex + 1) + " Ring Mod Frequency",
            NormalisableRange<float> (-2000.0f, 2000.0f, 0.1f), 200.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ringModParamId (slotIndex, RingModParam::fine), 1 },
            "Slot " + String (slotIndex + 1) + " Ring Mod Fine",
            NormalisableRange<float> (-20.0f, 20.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ringModParamId (slotIndex, RingModParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Ring Mod Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ringModParamId (slotIndex, RingModParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Ring Mod Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addLfoParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { lfoParamId (slotIndex, LfoParam::shape), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Shape",
            getLfoShapeChoices(), 0));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { lfoParamId (slotIndex, LfoParam::rateMode), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Sync",
            false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoParamId (slotIndex, LfoParam::rateHz), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Rate",
            skewedRange (0.02f, 20.0f, 1.0f), 1.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { lfoParamId (slotIndex, LfoParam::division), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Division",
            getDelayDivisionChoices(), 2)); // default "1/4"

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoParamId (slotIndex, LfoParam::depth), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Depth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    static void addLossyParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lossyParamId (slotIndex, LossyParam::bits), 1 },
            "Slot " + String (slotIndex + 1) + " Lossy Bits",
            NormalisableRange<float> (1.0f, 16.0f, 0.01f), 8.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lossyParamId (slotIndex, LossyParam::rate), 1 },
            "Slot " + String (slotIndex + 1) + " Lossy Rate",
            skewedRange (1.0f, 200.0f, 40.0f), 40.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lossyParamId (slotIndex, LossyParam::jitter), 1 },
            "Slot " + String (slotIndex + 1) + " Lossy Jitter",
            NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.3f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lossyParamId (slotIndex, LossyParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Lossy Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lossyParamId (slotIndex, LossyParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Lossy Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addEq8Params (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        auto bandLabels = getEq8BandLabels();
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { eq8ParamId (slotIndex, eq8BandParam (b)), 1 },
                "Slot " + String (slotIndex + 1) + " EQ 8 " + bandLabels[b] + "Hz",
                NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB")));

            // Default frequency reproduces the old fixed ladder exactly for a save with no value
            // for this new param -- freely draggable afterward (see Eq8Param's own comment).
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { eq8BandFreqParamId (slotIndex, b), 1 },
                "Slot " + String (slotIndex + 1) + " EQ 8 " + bandLabels[b] + " Freq",
                skewedRange (20.0f, 20000.0f, kEq8BandFrequencies[(size_t) b]), kEq8BandFrequencies[(size_t) b],
                AudioParameterFloatAttributes().withLabel ("Hz")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { eq8BandQParamId (slotIndex, b), 1 },
                "Slot " + String (slotIndex + 1) + " EQ 8 " + bandLabels[b] + " Q",
                skewedRange (0.1f, 18.0f, 1.0f), 1.0f));

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { eq8BandTypeParamId (slotIndex, b), 1 },
                "Slot " + String (slotIndex + 1) + " EQ 8 " + bandLabels[b] + " Type",
                getEq8FilterTypeChoices(), 0));

            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { eq8BandEnabledParamId (slotIndex, b), 1 },
                "Slot " + String (slotIndex + 1) + " EQ 8 " + bandLabels[b] + " Enabled",
                true));
        }

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq8ParamId (slotIndex, Eq8Param::mix), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 8 Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq8ParamId (slotIndex, Eq8Param::output), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 8 Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addChorusParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::mode), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Mode",
            getChorusModeChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::rate), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Rate",
            skewedRange (0.02f, 10.0f, 0.5f), 0.8f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::depth), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Depth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::delay), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Delay",
            skewedRange (0.5f, 30.0f, 8.0f), 15.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::feedback), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Feedback",
            NormalisableRange<float> (-0.95f, 0.95f, 0.001f), 0.3f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { chorusParamId (slotIndex, ChorusParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Chorus Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addEq3Params (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq3ParamId (slotIndex, Eq3Param::low), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 3 Low",
            NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq3ParamId (slotIndex, Eq3Param::mid), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 3 Mid",
            NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq3ParamId (slotIndex, Eq3Param::high), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 3 High",
            NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq3ParamId (slotIndex, Eq3Param::mix), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 3 Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { eq3ParamId (slotIndex, Eq3Param::output), 1 },
            "Slot " + String (slotIndex + 1) + " EQ 3 Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addMultibandConvolutionParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { multibandConvolutionParamId (slotIndex, MultibandConvolutionParam::splitHz1), 1 },
            "Slot " + String (slotIndex + 1) + " Multiband Convolution Split 1",
            skewedRange (20.0f, 20000.0f, 1000.0f), 300.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { multibandConvolutionParamId (slotIndex, MultibandConvolutionParam::splitHz2), 1 },
            "Slot " + String (slotIndex + 1) + " Multiband Convolution Split 2",
            skewedRange (20.0f, 20000.0f, 1000.0f), 3000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        for (int band = 0; band < kNumConvolutionBands; ++band)
        {
            const auto bandLabel = getConvolutionBandLabels()[band];

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::irIndex), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " IR",
                IRLibrary::getCatalogDisplayNames(), 0));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::tone), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Tone",
                NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::fadeIn), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Fade In",
                NormalisableRange<float> (0.0f, 500.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("ms")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::fadeOut), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Fade Out",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("%")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::stretch), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Stretch",
                skewedRange (0.25f, 4.0f, 1.0f), 1.0f,
                AudioParameterFloatAttributes().withLabel ("x")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::mix), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Mix",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                AudioParameterFloatAttributes().withLabel ("%")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multibandConvolutionBandParamId (slotIndex, band, MultibandConvolutionBandParam::output), 1 },
                "Slot " + String (slotIndex + 1) + " Multiband Convolution " + bandLabel + " Output",
                NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB")));
        }
    }

    static void addMultipassParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { multipassParamId (slotIndex, MultipassParam::splitHz1), 1 },
            "Slot " + String (slotIndex + 1) + " Multipass Split 1",
            skewedRange (20.0f, 20000.0f, 1000.0f), 300.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { multipassParamId (slotIndex, MultipassParam::splitHz2), 1 },
            "Slot " + String (slotIndex + 1) + " Multipass Split 2",
            skewedRange (20.0f, 20000.0f, 1000.0f), 3000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        const auto bandLabels = getMultipassBandLabels();
        for (int band = 0; band < kNumMultipassBands; ++band)
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { multipassBandParamId (slotIndex, band, MultipassBandParam::mix), 1 },
                "Slot " + String (slotIndex + 1) + " Multipass " + bandLabels[band] + " Mix",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                AudioParameterFloatAttributes().withLabel ("%")));
        }
    }

    static void addThreeOscParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::attack), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Attack",
            skewedRange (0.001f, 5.0f, 0.3f), 0.005f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::decay), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Decay",
            skewedRange (0.001f, 5.0f, 0.3f), 0.1f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::sustain), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Sustain",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Release",
            skewedRange (0.001f, 5.0f, 0.3f), 0.2f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::fm1to2), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc FM 1>2",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::fm2to3), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc FM 2>3",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::monoLegato), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Mono/Legato",
            false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::glide), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Glide",
            false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { threeOscParamId (slotIndex, ThreeOscParam::glideTimeMs), 1 },
            "Slot " + String (slotIndex + 1) + " 3xOsc Glide Time",
            skewedRange (1.0f, 2000.0f, 100.0f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            const auto oscLabel = getThreeOscOscLabels()[osc];
            // Osc 1 defaults to a Saw at full level so a freshly-added node is immediately
            // audible with a single clean voice; Osc 2/3 default to silent (Level 0%) so bringing
            // them in is a deliberate choice rather than an automatic thick/detuned default stack.
            const float defaultLevel = osc == 0 ? 100.0f : 0.0f;
            const int defaultWaveform = osc == 0 ? 2 : 0; // 2 = Saw, 0 = Sine

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::waveform), 1 },
                "Slot " + String (slotIndex + 1) + " 3xOsc " + oscLabel + " Waveform",
                getThreeOscWaveformChoices(), defaultWaveform));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::coarse), 1 },
                "Slot " + String (slotIndex + 1) + " 3xOsc " + oscLabel + " Coarse",
                NormalisableRange<float> (-36.0f, 36.0f, 1.0f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("st")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::fine), 1 },
                "Slot " + String (slotIndex + 1) + " 3xOsc " + oscLabel + " Fine",
                NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("ct")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::pan), 1 },
                "Slot " + String (slotIndex + 1) + " 3xOsc " + oscLabel + " Pan",
                NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::level), 1 },
                "Slot " + String (slotIndex + 1) + " 3xOsc " + oscLabel + " Level",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), defaultLevel,
                AudioParameterFloatAttributes().withLabel ("%")));
        }
    }

    static void addAdsrParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { adsrParamId (slotIndex, AdsrParam::attack), 1 },
            "Slot " + String (slotIndex + 1) + " ADSR Attack",
            skewedRange (0.001f, 5.0f, 0.3f), 0.005f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { adsrParamId (slotIndex, AdsrParam::decay), 1 },
            "Slot " + String (slotIndex + 1) + " ADSR Decay",
            skewedRange (0.001f, 5.0f, 0.3f), 0.1f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { adsrParamId (slotIndex, AdsrParam::sustain), 1 },
            "Slot " + String (slotIndex + 1) + " ADSR Sustain",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { adsrParamId (slotIndex, AdsrParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " ADSR Release",
            skewedRange (0.001f, 5.0f, 0.3f), 0.2f,
            AudioParameterFloatAttributes().withLabel ("s")));
    }

    static void addEnvelopeParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { envelopeParamId (slotIndex, EnvelopeParam::length), 1 },
            "Slot " + String (slotIndex + 1) + " Envelope Length",
            skewedRange (0.01f, 10.0f, 1.0f), 0.5f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { envelopeParamId (slotIndex, EnvelopeParam::depth), 1 },
            "Slot " + String (slotIndex + 1) + " Envelope Depth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;

        AudioProcessorValueTreeState::ParameterLayout layout;

        for (int slot = 0; slot < kMaxSlots; ++slot)
        {
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { slotTypeParamId (slot), 1 },
                "Slot " + String (slot + 1) + " Type",
                getModuleTypeChoices(), 0));

            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { slotBypassParamId (slot), 1 },
                "Slot " + String (slot + 1) + " Bypass",
                false));

            addWaveshaperParams (layout, slot);
            addFilterParams (layout, slot);
            addDelayParams (layout, slot);
            addDynamicsParams (layout, slot);
            addConvolutionParams (layout, slot);
            addUtilityParams (layout, slot);
            addRingModParams (layout, slot);
            addLfoParams (layout, slot);
            addLossyParams (layout, slot);
            addEq8Params (layout, slot);
            addChorusParams (layout, slot);
            addEq3Params (layout, slot);
            addMultibandConvolutionParams (layout, slot);
            addThreeOscParams (layout, slot);
            addAdsrParams (layout, slot);
            addEnvelopeParams (layout, slot);
            addMultipassParams (layout, slot);
        }

        // Master safety limiter -- not per-slot, always runs last. Default ON: the goal is to
        // stop an aggressively-pushed waveshaper from turning into a speaker/headphone hazard,
        // without capping how hard the waveshaper itself can be driven.
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { masterLimiterEnabledParamId(), 1 },
            "Safety Limiter",
            true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { masterLimiterCeilingParamId(), 1 },
            "Safety Limiter Ceiling",
            NormalisableRange<float> (-12.0f, 0.0f, 0.01f), -0.3f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        // MIDI mod matrix -- not per-slot, kNumModRoutes fixed routes.
        for (int r = 0; r < kNumModRoutes; ++r)
        {
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { modRouteSourceParamId (r), 1 },
                "Mod Route " + String (r + 1) + " Source",
                getModSourceChoices(), 0));

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { modRouteDestinationParamId (r), 1 },
                "Mod Route " + String (r + 1) + " Destination",
                getModDestinationChoices(), 0));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { modRouteDepthParamId (r), 1 },
                "Mod Route " + String (r + 1) + " Depth",
                NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        }

        return layout;
    }
}
