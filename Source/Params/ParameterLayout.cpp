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
