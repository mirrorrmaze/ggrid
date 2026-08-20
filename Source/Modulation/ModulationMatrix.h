#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
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

    // The modulatable parameters each slot exposes today. Appended-only, same rule as
    // ModuleType -- see Identifiers.h.
    enum class ModDestinationParam
    {
        filterFrequency = 0,
        filterFeedback = 1,
        delayTime = 2,
        delayFeedback = 3,
        waveshaperDrive = 4,
        convolutionMix = 5,
    };

    constexpr int kNumDestinationParamsPerSlot = 6;

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

    // A cable-based LFO -> destination-parameter route (see the node canvas's modulation cables,
    // distinct from the 6 fixed MIDI routes above). fromSlot must be an LFO-type slot; toSlot's
    // depth-scaled contribution is read fresh each block via getCurrentValue() on that slot's
    // live LFOModule -- see PluginProcessor::processBlock, which ticks every active LFO slot and
    // feeds its value in via setLfoValue() before the rest of the graph runs.
    //
    // destinationParamId is the exact APVTS parameter ID string (e.g. "slot3_waveshaper_drive")
    // rather than a fixed enum -- unlike the 6 MIDI routes above (a small, hand-picked, UI-facing
    // choice list), cables can target ANY continuous knob any module exposes, and a hand-picked
    // enum for that would mean a new enum entry for every single knob on every module. toSlot is
    // redundant with what's encoded in the ID string but kept explicit for cheap GUI lookups
    // (which node currently owns this cable's destination) without string-parsing it back out.
    struct ModConnection
    {
        int fromSlot = -1;
        int toSlot = -1;
        juce::String destinationParamId;
    };

    // Generous fan-out cap (an LFO driving several destinations at once is a normal, useful
    // patch). A destination can also receive more than one incoming cable at once -- e.g. two
    // LFOs, or an LFO and an Envelope, both modulating the same knob -- getOffsetForParam sums
    // every connection targeting a given destination, so this is additive/stacked, not a
    // last-one-wins override. The only thing still rejected is a literal duplicate edge (the
    // same source cabled to the same destination twice, see canAddModConnection) -- that would
    // just double-count one source's contribution for no reason, not add a second independent
    // one.
    constexpr int kMaxModConnections = kMaxSlots * 8;

    class ModulationMatrix
    {
    public:
        explicit ModulationMatrix (juce::AudioProcessorValueTreeState& apvts);

        // Scans incoming MIDI for note-on / mod wheel / cc2 / cc3 and updates live source
        // values. Call once per block, before the rack chain processes.
        void processMidi (const juce::MidiBuffer& midi);

        // Called once per block, before the rack graph runs, for every currently-active LFO
        // slot -- see PluginProcessor::processBlock. Slots that aren't currently an LFO (or
        // whose LFO cables have all been removed) simply never get a nonzero value read back.
        void setLfoValue (int slotIndex, float value) { lfoValues[(size_t) slotIndex].store (value, std::memory_order_relaxed); }

        // Live depth-scaled output (-1..1) of an LFO slot, for the canvas to animate a mod
        // cable's traveling pulse in sync with the LFO -- the only reader of this that isn't the
        // audio thread, hence the atomic (everything else here is audio-thread-only by
        // convention). Meaningless (stale/zero) for a slot that isn't currently an LFO.
        float getLfoValue (int slotIndex) const { return lfoValues[(size_t) slotIndex].load (std::memory_order_relaxed); }

        // Returns the summed offset (in the destination's own natural units, e.g. Hz for
        // frequency) from every one of the 6 fixed MIDI routes currently targeting this
        // destination. 0 if none do. Only covers the 6 hand-picked MIDI-route destinations (see
        // ModDestinationParam) -- for anything cable-driven, see getOffsetForParam below.
        float getOffsetForDestination (int destinationIndex) const;

        // Returns the summed offset from every modulation cable targeting this exact APVTS
        // parameter ID, scaled by `range` (the offset's full natural-unit swing at 100% LFO
        // depth/throw -- e.g. pass 20.0f for a dB knob to let a cable swing it +/-20dB at full
        // throw). Every knob a node exposes a modulation-destination nub for goes through this,
        // not just the 6 MIDI-route destinations -- see NodeComponent's per-panel getModTarget*
        // methods for the full list per module type.
        float getOffsetForParam (const juce::String& paramId, float range) const;

        // -- LFO modulation cables --
        bool canAddModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId) const;
        bool addModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId);
        void removeModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId);
        void removeAllModConnectionsForSlot (int slot);

        std::array<ModConnection, kMaxModConnections> modConnections {};
        int numModConnections = 0;

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

        std::array<std::atomic<float>, kMaxSlots> lfoValues {};
    };
}
