#pragma once

#include "../Modulation/ModulationMatrix.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

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

        // Modulation-source modules (LFO/Envelope/ADSR -- see isModulationSourceType() in
        // Identifiers.h) override these instead of touching the AudioBlock in process() at all.
        // GGridAudioProcessor's per-block source-tick loop reads getCurrentModulationValue() once
        // per active source slot and pushes it into ModulationMatrix::setLfoValue -- the name is a
        // holdover from when LFO was the only source type; the mechanism itself is generic
        // (keyed by slot index, not module type), so any RackModule can opt in here.
        virtual bool isModulationSource() const { return false; }
        virtual float getCurrentModulationValue() const { return 0.0f; }

        // Optional non-parameter state a module needs to persist beyond its APVTS parameters --
        // currently only EnvelopeModule's freeform breakpoint list, which can't be represented as
        // a fixed set of automatable scalar params. Written/read as attributes on a per-slot child
        // element of the plugin's saved state; see GGridAudioProcessor::getStateInformation/
        // setStateInformation. Default no-op for every module that doesn't need this.
        virtual void writeExtraState (juce::XmlElement&) const {}
        virtual void readExtraState (const juce::XmlElement&) {}
    };
}
