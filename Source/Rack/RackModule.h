#pragma once

#include "../Modulation/ModulationMatrix.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace GGrid
{
    // Common interface every rack-loadable effect module implements. A RackSlot owns exactly
    // one RackModule instance at a time (or none, when its type is ModuleType::none) and swaps
    // it out when the slot's type parameter changes.
    class RackModule
    {
    public:
        virtual ~RackModule() = default;

        virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;
        virtual void reset() = 0;

        // modMatrix is already up to date for this block (PluginProcessor calls
        // ModulationMatrix::processMidi before running the chain) -- modules that expose a
        // modulatable destination read their own offset via modMatrix.getOffsetForDestination().
        // Modules with nothing modulatable (e.g. WaveshaperModule) just ignore it.
        virtual void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) = 0;
    };
}
