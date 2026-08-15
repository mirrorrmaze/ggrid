#include "ModulationMatrix.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    juce::StringArray getModSourceChoices()
    {
        return { "None", "Note Pitch", "Velocity", "Mod Wheel", "CC 2", "CC 3" };
    }

    namespace
    {
        juce::String destinationParamLabel (ModDestinationParam param)
        {
            switch (param)
            {
                case ModDestinationParam::filterFrequency: return "Filter Frequency";
                case ModDestinationParam::filterFeedback:  return "Filter Feedback";
                case ModDestinationParam::delayTime:       return "Delay Time";
                case ModDestinationParam::delayFeedback:   return "Delay Feedback";
                case ModDestinationParam::waveshaperDrive: return "Waveshaper Drive";
                case ModDestinationParam::convolutionMix:  return "Convolution Mix";
                default:                                   return {};
            }
        }
    }

    juce::StringArray getModDestinationChoices()
    {
        juce::StringArray choices { "None" };

        for (int slot = 0; slot < kMaxSlots; ++slot)
            for (int param = 0; param < kNumDestinationParamsPerSlot; ++param)
                choices.add ("Slot " + juce::String (slot + 1) + " " + destinationParamLabel (static_cast<ModDestinationParam> (param)));

        return choices;
    }

    juce::String modRouteSourceParamId (int routeIndex)      { return "mod" + juce::String (routeIndex) + "_source"; }
    juce::String modRouteDestinationParamId (int routeIndex) { return "mod" + juce::String (routeIndex) + "_destination"; }
    juce::String modRouteDepthParamId (int routeIndex)       { return "mod" + juce::String (routeIndex) + "_depth"; }

    ModulationMatrix::ModulationMatrix (juce::AudioProcessorValueTreeState& apvts)
    {
        for (int r = 0; r < kNumModRoutes; ++r)
        {
            sourceParams[(size_t) r]      = apvts.getRawParameterValue (modRouteSourceParamId (r));
            destinationParams[(size_t) r] = apvts.getRawParameterValue (modRouteDestinationParamId (r));
            depthParams[(size_t) r]       = apvts.getRawParameterValue (modRouteDepthParamId (r));
        }
    }

    void ModulationMatrix::processMidi (const juce::MidiBuffer& midi)
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn())
            {
                notePitch01 = juce::jlimit (0.0f, 1.0f, (float) message.getNoteNumber() / 127.0f);
                velocity01 = message.getFloatVelocity();
            }
            else if (message.isController())
            {
                const int cc = message.getControllerNumber();
                const float value01 = (float) message.getControllerValue() / 127.0f;

                if (cc == kModWheelCcNumber)      modWheel01 = value01;
                else if (cc == kCc2Number)        cc2_01 = value01;
                else if (cc == kCc3Number)        cc3_01 = value01;
            }
        }
    }

    float ModulationMatrix::getSourceValue (ModSource source) const
    {
        switch (source)
        {
            case ModSource::notePitch: return notePitch01;
            case ModSource::velocity:  return velocity01;
            case ModSource::modWheel:  return modWheel01;
            case ModSource::cc2:       return cc2_01;
            case ModSource::cc3:       return cc3_01;
            case ModSource::none:
            default:                   return 0.0f;
        }
    }

    float ModulationMatrix::getDestinationRange (ModDestinationParam param)
    {
        switch (param)
        {
            case ModDestinationParam::filterFrequency: return 3000.0f; // Hz
            case ModDestinationParam::filterFeedback:  return 0.9f;
            case ModDestinationParam::delayTime:       return 500.0f;  // ms
            case ModDestinationParam::delayFeedback:   return 0.9f;
            case ModDestinationParam::waveshaperDrive: return 20.0f;   // dB
            case ModDestinationParam::convolutionMix:  return 50.0f;   // %
            default:                                   return 0.0f;
        }
    }

    float ModulationMatrix::getOffsetForDestination (int destinationIndex) const
    {
        const auto destinationParam = static_cast<ModDestinationParam> (destinationIndex % kNumDestinationParamsPerSlot);
        const float range = getDestinationRange (destinationParam);

        float total = 0.0f;

        for (int r = 0; r < kNumModRoutes; ++r)
        {
            const int destinationChoice = (int) destinationParams[(size_t) r]->load(); // 0 = None
            if (destinationChoice - 1 != destinationIndex)
                continue;

            const auto source = static_cast<ModSource> ((int) sourceParams[(size_t) r]->load());
            const float depth = depthParams[(size_t) r]->load();

            total += depth * getSourceValue (source) * range;
        }

        return total;
    }

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
                return false; // duplicate
            if (conn.destinationParamId == destinationParamId)
                return false; // destination already has a source
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
