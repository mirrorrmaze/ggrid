#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    // A small MIDI mod matrix: kNumModRoutes fixed routing slots, each mapping one MIDI-derived
    // source onto one destination parameter with a bipolar depth. Deliberately not a full
    // modulation IDE (no envelopes/LFOs, no user-configurable CC numbers) -- per-plan, this is
    // the "small mod matrix" scope, not the "full mod matrix" one.
    //
    // Modulation is applied as an ADDITIVE OFFSET at the point of use inside each destination
    // module (see FilterModule/DelayModule::process), never by overwriting the APVTS parameter's
    // own value -- that value is the knob position the user set (and what automation/presets
    // recall), and must stay exactly that. The matrix only ever hands back "how much to nudge
    // this destination away from its knob position right now."
    constexpr int kNumModRoutes = 6;

    enum class ModSource
    {
        none = 0,
        notePitch = 1,
        velocity = 2,
        modWheel = 3,
        cc2 = 4,
        cc3 = 5,
    };

    // Fixed CC numbers for cc2/cc3 (2 and 3) -- a CC-learn UI is future polish, not this pass.
    constexpr int kModWheelCcNumber = 1;
    constexpr int kCc2Number = 2;
    constexpr int kCc3Number = 3;

    // The modulatable parameters each Filter/Delay slot exposes today. Appended-only, same rule
    // as ModuleType -- see Identifiers.h.
    enum class ModDestinationParam
    {
        filterFrequency = 0,
        filterFeedback = 1,
        delayTime = 2,
        delayFeedback = 3,
    };

    constexpr int kNumDestinationParamsPerSlot = 4;

    // Global destination index encoding: slotIndex * kNumDestinationParamsPerSlot + param.
    // FilterModule/DelayModule use this to identify which offset is theirs.
    inline int modDestinationIndex (int slotIndex, ModDestinationParam param)
    {
        return slotIndex * kNumDestinationParamsPerSlot + (int) param;
    }

    juce::StringArray getModSourceChoices();

    // "None" followed by one entry per (slot, destinationParam) pair, in modDestinationIndex order.
    juce::StringArray getModDestinationChoices();

    juce::String modRouteSourceParamId (int routeIndex);
    juce::String modRouteDestinationParamId (int routeIndex);
    juce::String modRouteDepthParamId (int routeIndex);

    class ModulationMatrix
    {
    public:
        explicit ModulationMatrix (juce::AudioProcessorValueTreeState& apvts);

        // Scans incoming MIDI for note-on / mod wheel / cc2 / cc3 and updates live source
        // values. Call once per block, before the rack chain processes.
        void processMidi (const juce::MidiBuffer& midi);

        // Returns the summed offset (in the destination's own natural units, e.g. Hz for
        // frequency) from every route currently targeting this destination. 0 if none do.
        float getOffsetForDestination (int destinationIndex) const;

    private:
        float getSourceValue (ModSource source) const;
        static float getDestinationRange (ModDestinationParam param);

        std::array<std::atomic<float>*, kNumModRoutes> sourceParams {};
        std::array<std::atomic<float>*, kNumModRoutes> destinationParams {};
        std::array<std::atomic<float>*, kNumModRoutes> depthParams {};

        float notePitch01 = 0.5f;
        float velocity01 = 0.0f;
        float modWheel01 = 0.0f;
        float cc2_01 = 0.0f;
        float cc3_01 = 0.0f;
    };
}
