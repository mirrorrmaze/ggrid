#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include <array>

namespace GGrid
{
    // A cable-based LFO -> destination-parameter route (see the node canvas's modulation
    // cables). fromSlot must be an LFO-type slot; toSlot's depth-scaled contribution is read
    // fresh each block via getCurrentValue() on that slot's live LFOModule -- see
    // PluginProcessor::processBlock, which ticks every active LFO slot and feeds its value in
    // via setLfoValue() before the rest of the graph runs.
    //
    // destinationParamId is the exact APVTS parameter ID string (e.g. "slot3_waveshaper_drive")
    // rather than a fixed enum, since cables can target ANY continuous knob any module exposes --
    // a hand-picked enum for that would mean a new enum entry for every single knob on every
    // module. toSlot is redundant with what's encoded in the ID string but kept explicit for
    // cheap GUI lookups (which node currently owns this cable's destination) without
    // string-parsing it back out.
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
        // Called once per block, before the rack graph runs, for every currently-active LFO
        // slot -- see PluginProcessor::processBlock. Slots that aren't currently an LFO (or
        // whose LFO cables have all been removed) simply never get a nonzero value read back.
        void setLfoValue (int slotIndex, float value) { lfoValues[(size_t) slotIndex].store (value, std::memory_order_relaxed); }

        // Live depth-scaled output (-1..1) of an LFO slot, for the canvas to animate a mod
        // cable's traveling pulse in sync with the LFO -- the only reader of this that isn't the
        // audio thread, hence the atomic (everything else here is audio-thread-only by
        // convention). Meaningless (stale/zero) for a slot that isn't currently an LFO.
        float getLfoValue (int slotIndex) const { return lfoValues[(size_t) slotIndex].load (std::memory_order_relaxed); }

        // Returns the summed offset from every modulation cable targeting this exact APVTS
        // parameter ID, scaled by `range` (the offset's full natural-unit swing at 100% LFO
        // depth/throw -- e.g. pass 20.0f for a dB knob to let a cable swing it +/-20dB at full
        // throw). See NodeComponent's per-panel getModTarget* methods for the full list of
        // modulatable knobs per module type.
        float getOffsetForParam (const juce::String& paramId, float range) const;

        // -- LFO modulation cables --
        bool canAddModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId) const;
        bool addModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId);
        void removeModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId);
        void removeAllModConnectionsForSlot (int slot);

        std::array<ModConnection, kMaxModConnections> modConnections {};
        int numModConnections = 0;

    private:
        std::array<std::atomic<float>, kMaxSlots> lfoValues {};
    };
}
