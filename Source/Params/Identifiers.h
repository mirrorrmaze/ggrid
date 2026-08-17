#pragma once

#include <juce_core/juce_core.h>
#include <array>

// Parameter ID scheme: "slot{n}_type" / "slot{n}_bypass" pick and bypass whichever module
// occupies rack slot n; "slot{n}_{moduleType}_{paramName}" are that module type's own params,
// pre-declared for every slot regardless of the slot's current type so a slot's parameter
// identity (and therefore DAW automation/preset recall) never moves when its type changes.
// Chain order (which slot runs first/second/...) is NOT a parameter -- it's structural state,
// see PluginProcessor::getStateInformation.
namespace GGrid
{
    constexpr int kMaxSlots = 24;

    // Appended-only: never insert or reorder entries, only add new ones at the end, so a
    // saved slot-type choice index keeps meaning the same thing across plugin versions.
    enum class ModuleType
    {
        none = 0,
        waveshaper = 1,
        filter = 2,
        delay = 3,
        dynamics = 4,
        convolution = 5,
        utility = 6,
        ringMod = 7,
        lfo = 8,
        lossy = 9,
        eq8 = 10,
        chorus = 11,
        eq3 = 12,
        input = 13,
        output = 14,
    };

    inline juce::StringArray getModuleTypeChoices()
    {
        return { "None", "Waveshaper", "Filter", "Delay", "Dynamics", "Convolution", "Utility", "Ring Mod", "LFO",
                 "Lossy", "EQ 8", "Chorus/Flanger", "EQ 3", "Input", "Output" };
    }

    inline juce::String slotTypeParamId (int slotIndex)   { return "slot" + juce::String (slotIndex) + "_type"; }
    inline juce::String slotBypassParamId (int slotIndex) { return "slot" + juce::String (slotIndex) + "_bypass"; }

    inline juce::String waveshaperParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_waveshaper_" + paramName;
    }

    namespace WaveshaperParam
    {
        static const juce::String drive      = "drive";
        static const juce::String shape      = "shape";
        static const juce::String symmetry   = "symmetry";
        static const juce::String foldAmount = "fold";
        static const juce::String oversample = "oversample";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    inline juce::StringArray getWaveshaperShapeChoices()
    {
        return { "Hard Clip", "Soft Clip (tanh)", "Soft Clip (cubic)", "Foldback Wavefolder", "Sine Fold", "Rectify/Asymmetric" };
    }

    inline juce::String filterParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_filter_" + paramName;
    }

    namespace FilterParam
    {
        static const juce::String frequency  = "freq";
        static const juce::String type       = "type";
        static const juce::String resonance  = "resonance";
        static const juce::String feedback   = "feedback";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    // First 4 are biquad SVF-style modes (Frequency/Resonance); next 3 are delay-line based
    // (Frequency maps to pitch via sampleRate/freq, Feedback controls their character). The
    // Allpass Diffusor is a Schroeder allpass -- the classic building block of algorithmic
    // reverb diffusion, standing in for a dedicated "reverb filter" until the convolution
    // module lands. Last 3 (Serum-2-inspired) are their own DSP category: Ladder Low/High Pass
    // are a saturating 4-stage nonlinear-feedback ladder (Frequency/Resonance, Resonance driving
    // how hard the feedback path saturates rather than a clean Q), Formant repurposes Frequency
    // as a vowel-sweep position (A->E->I->O->U) through a bank of resonant peaks rather than a
    // literal cutoff -- see FilterModule for all three.
    inline juce::StringArray getFilterTypeChoices()
    {
        return { "Low Pass", "High Pass", "Band Pass", "Notch", "Comb (Feedback)", "Comb (Feedforward)", "Allpass (Diffusor)",
                 "Ladder Low Pass", "Ladder High Pass", "Formant" };
    }

    inline juce::String delayParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_delay_" + paramName;
    }

    namespace DelayParam
    {
        static const juce::String time       = "time";
        static const juce::String sync       = "sync";
        static const juce::String division   = "division";
        static const juce::String feedback   = "feedback";
        static const juce::String saturation = "saturation";
        static const juce::String lowCut     = "lowCut";
        static const juce::String hiCut      = "hiCut";
        static const juce::String pingPong   = "pingPong";
        static const juce::String mix        = "mix";
        static const juce::String output     = "output";
    }

    // Multiplier applied to a quarter-note length (60000/bpm ms) to get the synced delay time.
    // Dotted = *1.5, triplet = *2/3, matching standard DAW delay-sync conventions.
    inline juce::StringArray getDelayDivisionChoices()
    {
        return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4.", "1/8.", "1/16.", "1/4T", "1/8T", "1/16T" };
    }

    inline float getDelayDivisionMultiplier (int index)
    {
        static const float multipliers[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f,
                                              1.5f, 0.75f, 0.375f,
                                              (2.0f / 3.0f), (1.0f / 3.0f), (1.0f / 6.0f) };
        return multipliers[(size_t) juce::jlimit (0, 11, index)];
    }

    inline juce::String dynamicsParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_dynamics_" + paramName;
    }

    namespace DynamicsParam
    {
        static const juce::String threshold = "threshold";
        static const juce::String ratio     = "ratio";
        static const juce::String attack    = "attack";
        static const juce::String release   = "release";
        static const juce::String makeup    = "makeup";
        static const juce::String mix       = "mix";
    }

    inline juce::String convolutionParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_convolution_" + paramName;
    }

    // irIndex picks from IRLibrary::getCatalog() (factory library + anything dropped into the
    // Custom folder) -- see IR/IRLibrary.h. No "IR file path" parameter, since a catalog index
    // is stable/automatable the same way any other choice parameter is.
    namespace ConvolutionParam
    {
        static const juce::String irIndex  = "irIndex";
        static const juce::String tone     = "tone";
        static const juce::String fadeIn   = "fadeIn";
        static const juce::String fadeOut  = "fadeOut";
        static const juce::String stretch  = "stretch";
        static const juce::String mix      = "mix";
        static const juce::String output   = "output";
    }

    inline juce::String utilityParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_utility_" + paramName;
    }

    // Mirrors Ableton's Utility device: pure gain-staging/imaging, no coloration.
    namespace UtilityParam
    {
        static const juce::String gain        = "gain";
        static const juce::String pan         = "pan";
        static const juce::String width       = "width";
        static const juce::String mono        = "mono";
        static const juce::String phaseInvertL = "phaseInvertL";
        static const juce::String phaseInvertR = "phaseInvertR";
    }

    inline juce::String ringModParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_ringMod_" + paramName;
    }

    namespace RingModParam
    {
        static const juce::String mode      = "mode";
        static const juce::String frequency = "frequency";
        static const juce::String fine      = "fine";
        static const juce::String mix       = "mix";
        static const juce::String output    = "output";
    }

    // Ring Mod: plain multiply by a sine carrier. Freq Shift: true single-sideband shift (via a
    // Hilbert transform), not a pitch shift -- moves every partial by the same Hz amount rather
    // than the same ratio, so harmonic content becomes inharmonic (the classic "shifter" sound).
    inline juce::StringArray getRingModModeChoices()
    {
        return { "Ring Mod", "Freq Shift" };
    }

    inline juce::String lfoParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_lfo_" + paramName;
    }

    namespace LfoParam
    {
        static const juce::String shape    = "shape";
        static const juce::String rateMode = "rateMode";
        static const juce::String rateHz   = "rateHz";
        static const juce::String division = "division";
        static const juce::String depth    = "depth";
    }

    inline juce::StringArray getLfoShapeChoices()
    {
        return { "Sine", "Triangle", "Square", "Saw", "Sample & Hold" };
    }

    inline juce::String lossyParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_lossy_" + paramName;
    }

    // A spectral "codec-style" lo-fi degradation effect (ported from SPANDEX's LossyProcessor) --
    // see LossyModule's class comment for the full algorithm.
    namespace LossyParam
    {
        static const juce::String bits    = "bits";
        static const juce::String rate    = "rate";
        static const juce::String jitter  = "jitter";
        static const juce::String mix     = "mix";
        static const juce::String output  = "output";
    }

    inline juce::String eq8ParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_eq8_" + paramName;
    }

    // 8 fixed-frequency peaking bands, one octave apart -- a classic graphic-EQ frequency ladder
    // (100Hz-12.8kHz), not a parametric EQ (no per-band frequency/Q controls). See
    // kEq8BandFrequencies for the actual Hz values these knobs correspond to.
    namespace Eq8Param
    {
        static const juce::String band1  = "band1";  // 100 Hz
        static const juce::String band2  = "band2";  // 200 Hz
        static const juce::String band3  = "band3";  // 400 Hz
        static const juce::String band4  = "band4";  // 800 Hz
        static const juce::String band5  = "band5";  // 1.6 kHz
        static const juce::String band6  = "band6";  // 3.2 kHz
        static const juce::String band7  = "band7";  // 6.4 kHz
        static const juce::String band8  = "band8";  // 12.8 kHz
        static const juce::String mix    = "mix";
        static const juce::String output = "output";
    }

    constexpr int kNumEq8Bands = 8;
    constexpr std::array<float, kNumEq8Bands> kEq8BandFrequencies {
        100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f, 12800.0f
    };
    inline juce::StringArray getEq8BandLabels()
    {
        return { "100", "200", "400", "800", "1.6k", "3.2k", "6.4k", "12.8k" };
    }
    inline const juce::String& eq8BandParam (int bandIndex)
    {
        static const juce::String bandParams[kNumEq8Bands] = {
            Eq8Param::band1, Eq8Param::band2, Eq8Param::band3, Eq8Param::band4,
            Eq8Param::band5, Eq8Param::band6, Eq8Param::band7, Eq8Param::band8
        };
        return bandParams[(size_t) juce::jlimit (0, kNumEq8Bands - 1, bandIndex)];
    }

    inline juce::String chorusParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_chorus_" + paramName;
    }

    // One modulated delay line per channel; Mode picks Chorus (no feedback path -- lush,
    // doubling character) or Flanger (feedback path active -- resonant, "jet plane" comb sweep).
    // Depth swings the delay time as a fraction of Delay itself (see ChorusModule::process), so
    // the same knob works proportionally whether Delay is dialed short (flange) or long (chorus).
    namespace ChorusParam
    {
        static const juce::String mode     = "mode";
        static const juce::String rate     = "rate";
        static const juce::String depth    = "depth";
        static const juce::String delay    = "delay";
        static const juce::String feedback = "feedback";
        static const juce::String mix      = "mix";
        static const juce::String output   = "output";
    }

    inline juce::StringArray getChorusModeChoices()
    {
        return { "Chorus", "Flanger" };
    }

    inline juce::String eq3ParamId (int slotIndex, const juce::String& paramName)
    {
        return "slot" + juce::String (slotIndex) + "_eq3_" + paramName;
    }

    // A simple 3-band tone EQ (Low Shelf / Mid Bell / High Shelf), each just a gain knob --
    // EQ 8's lighter sibling. See Eq3Module for the exact filter frequencies/Q.
    namespace Eq3Param
    {
        static const juce::String low    = "low";
        static const juce::String mid    = "mid";
        static const juce::String high   = "high";
        static const juce::String mix    = "mix";
        static const juce::String output = "output";
    }

    // Master safety limiter -- always the last stage after the rack chain, not a rack slot
    // itself (it must never be reorderable away from "last", or it stops protecting anything).
    inline juce::String masterLimiterEnabledParamId() { return "master_limiterEnabled"; }
    inline juce::String masterLimiterCeilingParamId() { return "master_limiterCeiling"; }
}
