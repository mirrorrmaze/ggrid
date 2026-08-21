#include "ModulationMatrix.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    float ModulationMatrix::getOffsetForParam (const juce::String& paramId, float range) const
    {
        float total = 0.0f;

        for (int c = 0; c < numModConnections; ++c)
        {
            const auto& conn = modConnections[(size_t) c];
            if (conn.destinationParamId != paramId)
                continue;

            // The LFO's own Depth knob is already baked into the value it reports (see
            // LFOModule::process) -- no separate per-cable depth term needed here.
            total += lfoValues[(size_t) conn.fromSlot].load (std::memory_order_relaxed) * range;
        }

        return total;
    }

    bool ModulationMatrix::canAddModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId) const
    {
        if (fromSlot < 0 || toSlot < 0 || fromSlot == toSlot) return false;
        if (numModConnections >= kMaxModConnections) return false;

        for (int c = 0; c < numModConnections; ++c)
        {
            const auto& conn = modConnections[(size_t) c];
            if (conn.fromSlot == fromSlot && conn.destinationParamId == destinationParamId)
                return false; // duplicate -- same source already cabled to this exact destination
        }

        return true;
    }

    bool ModulationMatrix::addModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId)
    {
        if (! canAddModConnection (fromSlot, toSlot, destinationParamId))
            return false;

        modConnections[(size_t) numModConnections++] = { fromSlot, toSlot, destinationParamId };
        return true;
    }

    void ModulationMatrix::removeModConnection (int fromSlot, int toSlot, const juce::String& destinationParamId)
    {
        for (int i = 0; i < numModConnections; ++i)
        {
            const auto& conn = modConnections[(size_t) i];
            if (conn.fromSlot == fromSlot && conn.toSlot == toSlot && conn.destinationParamId == destinationParamId)
            {
                for (int j = i; j + 1 < numModConnections; ++j)
                    modConnections[(size_t) j] = modConnections[(size_t) j + 1];
                --numModConnections;
                return;
            }
        }
    }

    void ModulationMatrix::removeAllModConnectionsForSlot (int slot)
    {
        int w = 0;
        for (int r = 0; r < numModConnections; ++r)
            if (modConnections[(size_t) r].fromSlot != slot && modConnections[(size_t) r].toSlot != slot)
                modConnections[(size_t) w++] = modConnections[(size_t) r];
        numModConnections = w;
    }
}
