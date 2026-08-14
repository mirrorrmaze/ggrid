#pragma once

#include <juce_core/juce_core.h>

// Parameter ID scheme: "slot{n}_type" / "slot{n}_bypass" pick and bypass whichever module
// occupies rack slot n; "slot{n}_{moduleType}_{paramName}" are that module type's own params,
// pre-declared for every slot regardless of the slot's current type so a slot's parameter
// identity (and therefore DAW automation/preset recall) never moves when its type changes.
// Chain order (which slot runs first/second/...) is NOT a parameter -- it's structural state,
// see PluginProcessor::getStateInformation.
namespace GGrid
{
    constexpr int kMaxSlots = 8;

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
    };

    inline juce::StringArray getModuleTypeChoices()
    {
        return { "None", "Waveshaper", "Filter", "Delay", "Dynamics", "Convolution" };
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

    // First 4 are biquad SVF-style modes (Frequency/Resonance); last 3 are delay-line based
    // (Frequency maps to pitch via sampleRate/freq, Feedback controls their character). The
    // Allpass Diffusor is a Schroeder allpass -- the classic building block of algorithmic
    // reverb diffusion, standing in for a dedicated "reverb filter" until the convolution
    // module lands.
    inline juce::StringArray getFilterTypeChoices()
    {
        return { "Low Pass", "High Pass", "Band Pass", "Notch", "Comb (Feedback)", "Comb (Feedforward)", "Allpass (Diffusor)" };
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

    // Master safety limiter -- always the last stage after the rack chain, not a rack slot
    // itself (it must never be reorderable away from "last", or it stops protecting anything).
    inline juce::String masterLimiterEnabledParamId() { return "master_limiterEnabled"; }
    inline juce::String masterLimiterCeilingParamId() { return "master_limiterCeiling"; }
}
