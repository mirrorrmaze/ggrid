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

        // Routing graph: a directed edge from one graph node's output to another's input. Graph
        // nodes are the kMaxSlots regular module slots plus two fixed pseudo-nodes, kInputNodeId
        // and kOutputNodeId (see ConnectionGraph.h) -- Bitwig-Grid-style dedicated In/Out, always
        // present, never deletable. Each node allows at most kMaxPortsPerSide (4) outgoing and 4
        // incoming edges -- enough to split a signal into several parallel chains and merge them
        // back together without needing a general N-port graph. Input is the ONLY root (it seeds
        // every block with the plugin's raw dry input) and Output is the ONLY sink (its summed
        // input is what reaches the final mix) -- a regular module with no path from Input simply
        // never runs; there's no implicit per-node dry-through the way there used to be. Cycles
        // are rejected at connect-time (see wouldCreateCycle) since block-based processing has no
        // notion of a same-block feedback loop.
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
        // existing edges forward? A plain DFS/BFS over at most kNumGraphNodes nodes.
        bool wouldCreateCycle (int from, int to) const
        {
            std::array<bool, kNumGraphNodes> visited {};
            std::array<int, kNumGraphNodes> stack {};
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
            if (from >= kNumGraphNodes || to >= kNumGraphNodes) return false;
            // Input has no input ports at all (it's the raw dry source); Output has no output
            // ports at all (it's the final collection point) -- matches their fixed single-side
            // port set in the GUI (see IONodeComponent).
            if (to == kInputNodeId || from == kOutputNodeId) return false;
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

        // Canvas position of each graph node (the kMaxSlots module slots plus the fixed Input/
        // Output pseudo-nodes at indices kInputNodeId/kOutputNodeId) in the node-graph editor.
        // Purely a GUI layout concern (doesn't affect audio at all) but persisted alongside APVTS
        // so a saved project reopens with nodes where you left them instead of re-cascading.
        std::array<juce::Point<float>, kNumGraphNodes> nodePositions;

        juce::AudioProcessorValueTreeState apvts;

        // Owned here (not by the editor) so it keeps accumulating/rendering correctly across
        // editor open/close -- the editor just adds it as a child component when it exists.
        // Fed with the true final output (post rack chain, post safety limiter) at the end of
        // processBlock, so what you see is exactly what reaches your speakers.
        juce::AudioVisualiserComponent outputScope { 2 };

        // For GUI actions that need direct access to a slot's live module (e.g. Convolution's
        // IR waveform display polling) -- see RackSlot::getCurrentModule.
        RackSlot& getSlot (int index) { return *slots[(size_t) index]; }

        // For the node canvas's modulation-cable gestures (add/remove LFO -> destination
        // routes) -- see NodeGraphEditor.
        ModulationMatrix& getModulationMatrix() { return modulationMatrix; }

    private:
        // Declared before `slots` (constructed first) since RackSlot construction needs
        // sharedServices to already exist -- see SharedServices.h. convolutionMessageQueue,
        // irReshapeWorker, and currentBpm must in turn precede sharedServices, which only holds
        // references to them.
        juce::dsp::ConvolutionMessageQueue convolutionMessageQueue;
        IRReshapeWorker irReshapeWorker;
        std::atomic<double> currentBpm { 120.0 }; // updated from the host playhead each block
        SharedServices sharedServices { convolutionMessageQueue, irReshapeWorker, currentBpm };

        std::array<std::unique_ptr<RackSlot>, kMaxSlots> slots;

        // Per-graph-node scratch buffers (one per module slot plus the Input/Output pseudo-nodes)
        // -- preallocated in prepareToPlay and just resized (never reallocated, since
        // maximumBlockSize never shrinks below what's requested) each block, so building/summing
        // the graph never allocates on the audio thread. nodeBuffers[kInputNodeId] is seeded with
        // the plugin's raw dry input each block; nodeBuffers[kOutputNodeId] accumulates whatever
        // reaches Output and becomes the final mix.
        std::array<juce::AudioBuffer<float>, kNumGraphNodes> nodeBuffers;

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
