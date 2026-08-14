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
}
