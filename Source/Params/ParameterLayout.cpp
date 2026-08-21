#include "ParameterLayout.h"
#include "Identifiers.h"
#include "../IR/IRLibrary.h"
#include "../Wavetable/WavetableLibrary.h"

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
            ParameterID { filterParamId (slotIndex, FilterParam::drive), 1 },
            "Slot " + String (slotIndex + 1) + " Filter Drive",
            NormalisableRange<float> (0.0f, 36.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

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

    static void addNonlinearFilterParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::frequency), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Frequency",
            skewedRange (20.0f, 8000.0f, 1000.0f), 1000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::resonance), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Resonance",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::drive), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Drive",
            NormalisableRange<float> (0.0f, 36.0f, 0.01f), 6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::morph), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Morph",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mode), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Mode",
            getNonlinearFilterModeChoices(), 0));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::distortion), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Distortion",
            getNonlinearFilterDistortionChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Nonlinear Filter Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addMackityParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mackityParamId (slotIndex, MackityParam::input), 1 },
            "Slot " + String (slotIndex + 1) + " Mackity Input",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mackityParamId (slotIndex, MackityParam::pad), 1 },
            "Slot " + String (slotIndex + 1) + " Mackity Out Pad",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 75.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mackityParamId (slotIndex, MackityParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Mackity Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { mackityParamId (slotIndex, MackityParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Mackity Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));
    }

    static void addShimmerReverbParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::size), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Size",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 75.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::feedback), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Feedback",
            NormalisableRange<float> (0.0f, 95.0f, 0.1f), 62.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::diffusion), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Diffusion",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 78.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::shift), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Shift",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 12.0f,
            AudioParameterFloatAttributes().withLabel ("st")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::pitchMode), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Pitch Mode",
            getShimmerReverbPitchModeChoices(), 1));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::color), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Color",
            getShimmerReverbColorChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::modRate), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Mod Rate",
            NormalisableRange<float> (0.01f, 8.0f, 0.001f, 0.45f), 0.25f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::modDepth), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Mod Depth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 18.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::lowCut), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Low Cut",
            skewedRange (20.0f, 2000.0f, 120.0f), 120.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::highCut), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb High Cut",
            skewedRange (1000.0f, 20000.0f, 9000.0f), 12000.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { shimmerReverbParamId (slotIndex, ShimmerReverbParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Shimmer Reverb Output",
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

    static void addCompressorParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::threshold), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -18.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::ratio), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Ratio",
            skewedRange (1.0f, 20.0f, 4.0f), 4.0f, AudioParameterFloatAttributes().withLabel (":1")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::attack), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Attack",
            skewedRange (0.1f, 200.0f, 10.0f), 10.0f, AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Release",
            skewedRange (5.0f, 1000.0f, 100.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::knee), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Knee",
            NormalisableRange<float> (0.0f, 24.0f, 0.01f), 6.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::makeup), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Makeup",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { compressorParamId (slotIndex, CompressorParam::detection), 1 },
            "Slot " + String (slotIndex + 1) + " Compressor Detection",
            getCompressorDetectionChoices(), 0));
    }

    static void addLimiterParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { limiterParamId (slotIndex, LimiterParam::gain), 1 },
            "Slot " + String (slotIndex + 1) + " Limiter Gain",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { limiterParamId (slotIndex, LimiterParam::ceiling), 1 },
            "Slot " + String (slotIndex + 1) + " Limiter Ceiling",
            NormalisableRange<float> (-12.0f, 0.0f, 0.01f), -0.3f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { limiterParamId (slotIndex, LimiterParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " Limiter Release",
            skewedRange (1.0f, 1000.0f, 50.0f), 50.0f, AudioParameterFloatAttributes().withLabel ("ms")));
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

    static void addSpectralClipperParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { spectralClipperParamId (slotIndex, SpectralClipperParam::drive), 1 },
            "Slot " + String (slotIndex + 1) + " Spectral Clipper Drive",
            NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { spectralClipperParamId (slotIndex, SpectralClipperParam::ceiling), 1 },
            "Slot " + String (slotIndex + 1) + " Spectral Clipper Ceiling",
            NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { spectralClipperParamId (slotIndex, SpectralClipperParam::shape), 1 },
            "Slot " + String (slotIndex + 1) + " Spectral Clipper Shape",
            getSpectralClipperShapeChoices(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { spectralClipperParamId (slotIndex, SpectralClipperParam::mix), 1 },
            "Slot " + String (slotIndex + 1) + " Spectral Clipper Mix",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { spectralClipperParamId (slotIndex, SpectralClipperParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " Spectral Clipper Output",
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
                ParameterID { multipassBandParamId (slotIndex, band, MultipassBandParam::gain), 1 },
                "Slot " + String (slotIndex + 1) + " Multipass " + bandLabels[band] + " Gain",
                NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("dB")));
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

    static void addLfoTableParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::tableIndex), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table",
            WavetableLibrary::getCatalogDisplayNames(), 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::frame), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Frame",
            NormalisableRange<float> (1.0f, 256.0f, 0.01f), 1.0f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::smooth), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Smooth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::phase), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Phase",
            NormalisableRange<float> (0.0f, 360.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("deg")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::rateMode), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Sync",
            false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::rateHz), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Rate",
            skewedRange (0.01f, 50.0f, 1.0f), 1.0f,
            AudioParameterFloatAttributes().withLabel ("Hz")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::division), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Division",
            getDelayDivisionChoices(), 2));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::depth), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Depth",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { lfoTableParamId (slotIndex, LfoTableParam::retrigger), 1 },
            "Slot " + String (slotIndex + 1) + " LFO Table Retrigger",
            false));
    }

    static void addWavetableSynthParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout, int slotIndex)
    {
        using namespace juce;

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::attack), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Attack",
            skewedRange (0.001f, 5.0f, 0.3f), 0.005f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::decay), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Decay",
            skewedRange (0.001f, 5.0f, 0.3f), 0.1f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::sustain), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Sustain",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 80.0f,
            AudioParameterFloatAttributes().withLabel ("%")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::release), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Release",
            skewedRange (0.001f, 5.0f, 0.3f), 0.2f,
            AudioParameterFloatAttributes().withLabel ("s")));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::output), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Output",
            NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("dB")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::algorithm), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Spread Algorithm",
            getWavetableSynthAlgorithmChoices(), 0));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::monoLegato), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Mono/Legato",
            false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::glide), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Glide",
            false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::glideTimeMs), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Glide Time",
            skewedRange (1.0f, 2000.0f, 100.0f), 50.0f,
            AudioParameterFloatAttributes().withLabel ("ms")));

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::polyphony), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Polyphony",
            1, kMaxWavetableSynthVoices, 8));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::masterPitch), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Master Pitch",
            NormalisableRange<float> (-48.0f, 48.0f, 0.01f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("st")));

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::bendRange), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Bend Range",
            0, 24, 2));

        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::unison), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Unison",
            1, 16, 1));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::spread), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Spread",
            NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withLabel ("ct")));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { wavetableSynthParamId (slotIndex, WavetableSynthParam::multiplier), 1 },
            "Slot " + String (slotIndex + 1) + " WT Synth Spread Multiplier",
            getWavetableSynthMultiplierChoices(), 0));

        const auto genLabels = getWavetableSynthGeneratorLabels();
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            const auto label = genLabels[gen];

            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::enabled), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Enabled",
                gen == 0));

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::table), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Table",
                WavetableLibrary::getCatalogDisplayNames(), gen == 0 ? 0 : juce::jmin (3, WavetableLibrary::getCatalogDisplayNames().size() - 1)));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::frame), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Frame",
                NormalisableRange<float> (1.0f, 256.0f, 0.01f), 1.0f));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::smooth), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Smooth",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("%")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::coarse), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Coarse",
                NormalisableRange<float> (-48.0f, 48.0f, 1.0f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("st")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::fine), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Fine",
                NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("ct")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::pan), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Pan",
                NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::level), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Level",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), gen == 0 ? 100.0f : 0.0f,
                AudioParameterFloatAttributes().withLabel ("%")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::fm), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " FM",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f,
                AudioParameterFloatAttributes().withLabel ("%")));

            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::output), 1 },
                "Slot " + String (slotIndex + 1) + " WT Synth " + label + " Output",
                getWavetableSynthOutputChoices(), 0));
        }
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
            addCompressorParams (layout, slot);
            addLimiterParams (layout, slot);
            addConvolutionParams (layout, slot);
            addUtilityParams (layout, slot);
            addRingModParams (layout, slot);
            addLfoParams (layout, slot);
            addLossyParams (layout, slot);
            addSpectralClipperParams (layout, slot);
            addEq8Params (layout, slot);
            addChorusParams (layout, slot);
            addEq3Params (layout, slot);
            addMultibandConvolutionParams (layout, slot);
            addThreeOscParams (layout, slot);
            addAdsrParams (layout, slot);
            addEnvelopeParams (layout, slot);
            addMultipassParams (layout, slot);
            addLfoTableParams (layout, slot);
            addWavetableSynthParams (layout, slot);
            addNonlinearFilterParams (layout, slot);
            addMackityParams (layout, slot);
            addShimmerReverbParams (layout, slot);
        }

        return layout;
    }
}
