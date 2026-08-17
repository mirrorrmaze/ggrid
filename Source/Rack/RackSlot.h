#pragma once

#include "RackModule.h"
#include "SharedServices.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Owns one rack slot: its type/bypass parameters, and whichever RackModule instance
    // currently matches the type parameter. Swaps the instance when the type parameter changes.
    //
    // That swap happens on the audio thread, at the start of process() -- which allocates
    // (constructing + preparing a new module) rather than being hard real-time safe. That's a
    // deliberate simplification: type changes are a rare, user-driven UI action (not a per-block
    // event), and this matches how plenty of small/hobby plugins handle occasional structural
    // changes. Revisit with a lock-free swap if this ever causes an audible glitch.
    class RackSlot
    {
    public:
        RackSlot (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec);
        void reset();
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix);

        int getSlotIndex() const { return slotIndex; }

        // Reads the type parameter directly (not currentModule, which only updates at the start
        // of process()) -- used by PluginProcessor to build this block's connection graph before
        // any slot has run.
        ModuleType getActiveType() const { return static_cast<ModuleType> ((int) typeParam->load()); }

        // For GUI actions that need to reach the live module directly (e.g. Convolution's IR
        // waveform display polling) rather than going through an APVTS parameter. May be
        // nullptr (slot is None) or a different concrete type than the caller expects if the
        // slot's type changed since the GUI was built -- callers must dynamic_cast and check.
        RackModule* getCurrentModule() const { return currentModule.get(); }

    private:
        std::unique_ptr<RackModule> createModuleForType (ModuleType type) const;

        // Consumes services.pendingModuleExtraState[slotIndex] into a freshly (re)created
        // currentModule, if any is waiting -- see SharedServices.h's own comment on why this
        // can't just happen synchronously inside GGridAudioProcessor::setStateInformation.
        void applyPendingExtraState();

        juce::AudioProcessorValueTreeState& apvts;
        SharedServices& services;
        int slotIndex;

        std::atomic<float>* typeParam;
        std::atomic<float>* bypassParam;

        ModuleType currentType = ModuleType::none;
        std::unique_ptr<RackModule> currentModule;

        juce::dsp::ProcessSpec lastSpec {};
        bool hasSpec = false;
    };
}
