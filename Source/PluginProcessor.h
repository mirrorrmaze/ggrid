#pragma once

#include "Rack/RackSlot.h"
#include "Rack/ConnectionGraph.h"
#include "Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    class GGridAudioProcessor : public juce::AudioProcessor
    {
    public:
        GGridAudioProcessor();
        ~GGridAudioProcessor() override = default;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        // Routing graph: a directed edge from one slot's output to another's input. Input and
        // Output are ordinary, addable/deletable module types (ModuleType::input/output) rather
        // than fixed pseudo-nodes -- any number of slots can be Input or Output type at once,
        // every Input slot provides the plugin's raw dry signal and every Output slot's summed
        // input contributes to the final mix, so several independent racks/parallel signal paths
        // can coexist on one canvas. Each slot allows at most kMaxPortsPerSide (4) outgoing and 4
        // incoming edges -- enough to split a signal into several parallel chains and merge them
        // back together without needing a general N-port graph. A regular module with no path
        // from any Input slot simply never runs -- there's no implicit per-node dry-through.
        // Cycles are rejected at connect-time (see wouldCreateCycle) since block-based processing
        // has no notion of a same-block feedback loop.
        //
        // Deliberately NOT AudioProcessorValueTreeState parameters -- structural/preset-level
        // config edited via canvas cable gestures, not automated mid-performance. Persisted
        // alongside APVTS state in get/setStateInformation. The GUI mutates this directly
        // (message thread, via addConnection/removeConnection/removeAllConnectionsForSlot);
        // processBlock only reads it each block -- safe to read concurrently with the rare
        // message-thread writes a cable-drag gesture produces, same tradeoff the old chainOrder
        // array made. Connection/kMaxConnections and the topological-order algorithm itself live
        // in ConnectionGraph.h, kept dependency-free so the offline test harness can exercise the
        // graph algorithm directly without constructing a full AudioProcessor.
        std::array<Connection, kMaxConnections> connections {};
        int numConnections = 0;

        int getOutDegree (int node) const
        {
            int n = 0;
            for (int i = 0; i < numConnections; ++i)
                if (connections[(size_t) i].from == node) ++n;
            return n;
        }

        int getInDegree (int node) const
        {
            int n = 0;
            for (int i = 0; i < numConnections; ++i)
                if (connections[(size_t) i].to == node) ++n;
            return n;
        }

        bool hasConnection (int from, int to) const
        {
            for (int i = 0; i < numConnections; ++i)
                if (connections[(size_t) i].from == from && connections[(size_t) i].to == to)
                    return true;
            return false;
        }

        // Would adding from->to create a cycle -- i.e. can `to` already reach `from` by following
        // existing edges forward? A plain DFS/BFS over at most kMaxSlots nodes.
        bool wouldCreateCycle (int from, int to) const
        {
            std::array<bool, kMaxSlots> visited {};
            std::array<int, kMaxSlots> stack {};
            int stackSize = 0;

            stack[(size_t) stackSize++] = to;
            visited[(size_t) to] = true;

            while (stackSize > 0)
            {
                const int node = stack[(size_t) --stackSize];
                if (node == from)
                    return true;

                for (int i = 0; i < numConnections; ++i)
                {
                    if (connections[(size_t) i].from != node) continue;
                    const int next = connections[(size_t) i].to;
                    if (! visited[(size_t) next])
                    {
                        visited[(size_t) next] = true;
                        stack[(size_t) stackSize++] = next;
                    }
                }
            }
            return false;
        }

        bool canAddConnection (int from, int to) const
        {
            if (from < 0 || to < 0 || from == to) return false;
            if (from >= kMaxSlots || to >= kMaxSlots) return false;
            // Input/ThreeOsc-type slots have no input ports at all (they're both graph sources --
            // Input the raw dry signal, ThreeOsc its own MIDI-generated audio); Output-type slots
            // have no output ports at all (their summed input is what reaches the final mix) --
            // matches their port sets in the GUI (see NodeComponent::isInputType/isOutputType/
            // isThreeOscType).
            const auto toType = slots[(size_t) to]->getActiveType();
            if (toType == ModuleType::input || toType == ModuleType::threeOsc) return false;
            if (slots[(size_t) from]->getActiveType() == ModuleType::output) return false;
            if (numConnections >= kMaxConnections) return false;
            if (getOutDegree (from) >= kMaxPortsPerSide || getInDegree (to) >= kMaxPortsPerSide) return false;
            if (hasConnection (from, to)) return false;
            if (wouldCreateCycle (from, to)) return false;
            return true;
        }

        // Silently no-ops if the connection is invalid (capacity/cycle/duplicate) -- matches the
        // rest of the plugin's philosophy of never blocking a canvas gesture with a modal.
        bool addConnection (int from, int to)
        {
            if (! canAddConnection (from, to)) return false;
            connections[(size_t) numConnections++] = { from, to };
            return true;
        }

        void removeConnection (int from, int to)
        {
            for (int i = 0; i < numConnections; ++i)
            {
                if (connections[(size_t) i].from == from && connections[(size_t) i].to == to)
                {
                    for (int j = i; j + 1 < numConnections; ++j)
                        connections[(size_t) j] = connections[(size_t) j + 1];
                    --numConnections;
                    return;
                }
            }
        }

        void removeAllConnectionsForSlot (int slot)
        {
            int w = 0;
            for (int r = 0; r < numConnections; ++r)
                if (connections[(size_t) r].from != slot && connections[(size_t) r].to != slot)
                    connections[(size_t) w++] = connections[(size_t) r];
            numConnections = w;
        }

        // Canvas position of each slot's node in the node-graph editor. Purely a GUI layout
        // concern (doesn't affect audio at all) but persisted alongside APVTS so a saved project
        // reopens with nodes where you left them instead of re-cascading.
        std::array<juce::Point<float>, kMaxSlots> nodePositions;

        juce::AudioProcessorValueTreeState apvts;

        // Owned here (not by the editor) so it keeps accumulating/rendering correctly across
        // editor open/close -- the editor just adds it as a child component when it exists.
        // Fed with the true final output (post rack chain, post safety limiter) at the end of
        // processBlock, so what you see is exactly what reaches your speakers.
        juce::AudioVisualiserComponent outputScope { 2 };

        // For GUI actions that need direct access to a slot's live module (e.g. Convolution's
        // IR waveform display polling) -- see RackSlot::getCurrentModule.
        RackSlot& getSlot (int index) { return *slots[(size_t) index]; }

        // One oscilloscope per slot, owned here for the same reason outputScope is (keeps
        // rendering correctly across editor open/close) -- only ever pushed to for slots
        // currently playing the Input or Output role (see processBlock), but always present for
        // every slot so NodeComponent can just take a reference at construction regardless of
        // that slot's current type. Displayed on the node itself only when its type is Input or
        // Output -- see NodeComponent::isInputType/isOutputType.
        juce::AudioVisualiserComponent& getNodeScope (int index) { return *nodeScopes[(size_t) index]; }

        // For the node canvas's modulation-cable gestures (add/remove LFO -> destination
        // routes) -- see NodeGraphEditor.
        ModulationMatrix& getModulationMatrix() { return modulationMatrix; }

    private:
        // Declared before `slots` (constructed first) since RackSlot construction needs
        // sharedServices to already exist -- see SharedServices.h. convolutionMessageQueue,
        // irReshapeWorker, currentBpm, and pendingModuleExtraState must in turn precede
        // sharedServices, which only holds references to them.
        juce::dsp::ConvolutionMessageQueue convolutionMessageQueue;
        IRReshapeWorker irReshapeWorker;
        std::atomic<double> currentBpm { 120.0 }; // updated from the host playhead each block
        // See SharedServices.h's own comment -- a just-loaded save's per-slot RackModule extra
        // state (currently only EnvelopeModule's breakpoints), waiting to be applied once a
        // matching module instance actually exists.
        std::array<std::unique_ptr<juce::XmlElement>, kMaxSlots> pendingModuleExtraState;
        SharedServices sharedServices { convolutionMessageQueue, irReshapeWorker, currentBpm, pendingModuleExtraState };

        std::array<std::unique_ptr<RackSlot>, kMaxSlots> slots;
        std::array<std::unique_ptr<juce::AudioVisualiserComponent>, kMaxSlots> nodeScopes;

        // Per-slot scratch buffers for graph processing -- preallocated in prepareToPlay and just
        // resized (never reallocated, since maximumBlockSize never shrinks below what's
        // requested) each block, so building/summing the graph never allocates on the audio
        // thread. Every Input-type slot's buffer is seeded with the plugin's raw dry input each
        // block; every Output-type slot's buffer is summed into the final mix.
        std::array<juce::AudioBuffer<float>, kMaxSlots> nodeBuffers;

        // Scratch buffer LFO slots process into each block -- LFOModule ignores its content
        // entirely (see LFOModule's class comment), this just gives RackSlot::process() a
        // validly-shaped block to call it with. LFO slots are excluded from the audio graph
        // proper (nodeBuffers/connections above) since they aren't audio processors.
        juce::AudioBuffer<float> lfoScratchBuffer;

        // Master safety limiter -- always the last stage, not a rack slot (see
        // Identifiers::masterLimiterEnabledParamId for why it can't be reorderable).
        juce::dsp::Limiter<float> masterLimiter;
        std::atomic<float>* limiterEnabledParam = nullptr;
        std::atomic<float>* limiterCeilingParam = nullptr;

        // Updated from incoming MIDI once per block, before the rack chain runs, then handed to
        // every slot's process() so Filter/Delay modules can read their modulation offsets.
        ModulationMatrix modulationMatrix;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GGridAudioProcessor)
    };
}
