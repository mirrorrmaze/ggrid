#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NodeComponent.h"
#include "IONodeComponent.h"
#include "../PluginProcessor.h"

namespace GGrid
{
    // The patch-bay canvas: right-click blank space to add a node (Waveshaper/Filter/Delay/
    // Dynamics/Convolution/...), drag a node's title bar to reposition it, drag a cable from a
    // node's output to another node's input to connect them. Two fixed pseudo-nodes -- Input
    // (the plugin's raw dry signal) and Output (the final mix) -- are always present and can't be
    // deleted (see IONodeComponent); nothing reaches the speakers unless it's patched all the way
    // from Input through to Output, Bitwig-Grid-style -- there's no implicit per-node dry-through
    // the way there used to be. Left-click-drag blank space to pan;
    // two-finger/wheel scroll also pans, in whichever direction(s) it reports (see
    // mouseWheelMove) -- zoom is a separate gesture (trackpad pinch via mouseMagnify, Ctrl+scroll,
    // or the +/-/reset buttons), not something plain scroll does, so a touchpad's natural
    // two-finger scroll doesn't get misread as pinch-to-zoom. Grab an existing cable (not just a
    // node's output nub) to rewire it elsewhere, or drop it on blank space to genuinely
    // disconnect it.
    //
    // Pan vs. select on blank space is told apart by how long the button was held before the
    // first real movement, not by a modifier key: a quick drag pans (the fast/default gesture --
    // grab and go), while holding briefly first (see holdBeforeSelectMs) starts a rubber-band
    // select instead -- see the pendingGesture branch in mouseDrag().
    //
    // Selection: click a node's title bar to select just it; shift-click to toggle it in/out of
    // the current selection; hold-then-drag blank space for a rubber-band select. Dragging a
    // title bar that's already part of the selection moves every selected node together, by a
    // shared delta -- see handleNodeGrabbed/Dragged/Released, which mirror the click-vs-pan
    // threshold pattern used for blank canvas below (a plain click on an already-selected node
    // collapses the selection to just that node; a drag preserves the group). Delete/Backspace
    // removes every selected node.
    //
    // Modulation cables: LFO nodes have no audio ports (see NodeComponent::isLfoType) -- instead
    // a single violet modulation-output nub, dragged onto a violet modulation-destination dot on
    // any continuous knob any other node exposes (see each panel's modTargets in
    // ModuleControlPanels.h -- essentially every knob now, not a fixed few). These
    // are a genuinely separate graph from the orange audio cables (GGridAudioProcessor::
    // connections) -- see ModulationMatrix::modConnections -- with their own capacity rule (one
    // source cable per destination knob) and their own cycle-freeness by construction (a
    // modulation destination is never itself draggable as a source). isDraggingModCable is set
    // the moment a drag starts (either grabbing an LFO's output nub, or regrabbing an existing
    // mod cable) and just changes which hit-testing/creation path mouseUp takes -- the gesture
    // itself reuses the exact same onOutputDragStart/Drag/DragEnd callbacks as audio cables.
    //
    // The underlying engine (GGridAudioProcessor::connections) is a real directed graph, not a
    // single linear chain -- each node (including Input/Output) allows up to kMaxPortsPerSide (4)
    // outgoing and 4 incoming edges (matching the 4 output nubs / 4 input dots each NodeComponent
    // draws), enough to split a signal into several parallel chains and sum them back together
    // (Bitwig Grid / Ableton parallel-chain style) without a general N-port model. Cycles are
    // rejected when a connection is attempted (see GGridAudioProcessor::canAddConnection) --
    // block-based processing has no notion of a same-block feedback loop. A freshly added node is
    // spliced in after whichever node was added immediately before it (see lastAddedSlot),
    // pushing that node's Output connection onto the new node instead -- so simple serial chains
    // still "just work" the way they did before this canvas supported branching, always running
    // from Input to Output; explicit rewiring is how you diverge from that into something
    // parallel.
    //
    // Node existence for the kMaxSlots module nodes mirrors each slot's type parameter (a node is
    // visible iff its slot's type != None); Input/Output are always present. Node canvas position
    // is stored in ::nodePositions (indexed by slot, or by kInputNodeId/kOutputNodeId for the two
    // pseudo-nodes), cosmetic only. Both are reconciled from processor state on a timer rather
    // than through fine-grained callbacks, since several different things can change them (this
    // canvas, automation, undo, project load) and polling is simple and cheap for this few nodes.
    class NodeGraphEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit NodeGraphEditor (GGridAudioProcessor& processorIn);
        ~NodeGraphEditor() override;

        void paint (juce::Graphics&) override;
        // Cables render here, not in paint() -- see the comment on the .cpp definition. JUCE
        // calls this after every child NodeComponent has painted, so anything drawn here sits on
        // top of node bodies instead of getting covered by them.
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        // Trackpad pinch gesture -- see the .cpp definition for why zoom lives here and not in
        // mouseWheelMove alongside panning.
        void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
        bool keyPressed (const juce::KeyPress&) override;

        // Generously large virtual canvas -- not literally infinite (JUCE components need a
        // concrete size), but big enough relative to where nodes actually cluster that hitting
        // the edge in normal use isn't a realistic concern. The owning Viewport clips/scrolls
        // it; scrollbars are hidden in favour of click-drag panning, so it *feels* boundless.
        static constexpr int canvasWidth = 6000;
        static constexpr int canvasHeight = 4000;

        // Zoom is a pure rendering transform (Component::setTransform) pivoted around a chosen
        // point in the owning Viewport's coordinate space -- node positions, drag math, and
        // cable hit-testing all stay in unscaled canvas units and don't need to know zoom
        // exists, since JUCE automatically inverse-transforms incoming mouse events for a
        // transformed component.
        void setZoom (float newZoom);
        float getZoom() const { return zoomLevel; }
        static constexpr float minZoom = 0.2f, maxZoom = 2.0f;

        // Set once by the owning editor so pan/zoom can read and drive the Viewport's scroll
        // position directly.
        void setOwnerViewport (juce::Viewport* viewport) { ownerViewport = viewport; }

        std::function<void (float newZoom)> onZoomChanged;

    private:
        void timerCallback() override;
        void refreshLayout();

        void showAddNodeMenu (juce::Point<int> canvasPosition);
        void addNode (ModuleType type, juce::Point<int> position);
        void deleteNode (int slotIndex);

        // Removes any connection (audio or modulation) that no longer makes sense now that
        // slotIndex's type is newType -- see the lastKnownType comment above.
        void pruneStaleConnectionsForSlot (int slotIndex, ModuleType newType);

        // Selection helpers -- all of them keep `selected` and each NodeComponent's own
        // setSelected() highlight in sync, so callers never touch `selected` directly.
        bool isSelected (int slotIndex) const { return selected[(size_t) slotIndex]; }
        bool hasSelection() const;
        void clearSelection();
        void setSelectionToSingle (int slotIndex);
        void toggleSelection (int slotIndex);
        void updateSelectionFromRubberBand();
        void deleteSelectedNodes();

        void handleNodeGrabbed (int slotIndex, const juce::MouseEvent&);
        void handleNodeDragged (int slotIndex, const juce::MouseEvent&);
        void handleNodeReleased (int slotIndex, const juce::MouseEvent&);

        // For a specific edge (identified by its index into processor.connections), which of the
        // kMaxPortsPerSide output/input port positions it should be drawn/hit-tested at -- just
        // its ordinal among other edges sharing the same endpoint, so multiple cables into/out of
        // one node fan out to visually distinct dots instead of stacking on top of each other.
        int outputPortIndexForConnection (int connectionIndex) const;
        int inputPortIndexForConnection (int connectionIndex) const;

        // Canvas-space port positions/visibility for any graph node id (0..kMaxSlots-1 for a
        // regular module, or kInputNodeId/kOutputNodeId) -- the single place that knows how to
        // resolve a generic node id to either a NodeComponent or one of the two IONodeComponent
        // pseudo-nodes, so the cable drawing/hit-testing code below doesn't need to care which
        // kind of node either endpoint is. Input has no input ports and Output has no output
        // ports (see GGridAudioProcessor::canAddConnection) -- callers only ever ask for the port
        // kind a given node actually has.
        bool nodeExistsAndVisible (int nodeId) const;
        juce::Point<float> outputPortCanvasPos (int nodeId, int port) const;
        juce::Point<float> inputPortCanvasPos (int nodeId, int port) const;

        // Selection-highlight dispatch for a generic node id, mirroring the position helpers
        // above -- NodeComponent and IONodeComponent both have their own setSelected(), but
        // aren't related by a common base beyond juce::Component.
        void setNodeSelected (int nodeId, bool shouldBeSelected);
        juce::Component& componentForNode (int nodeId);

        void handleOutputDragStart (int slotIndex, const juce::MouseEvent&);
        void handleOutputDrag (int slotIndex, const juce::MouseEvent&);
        void handleOutputDragEnd (int slotIndex, const juce::MouseEvent&);

        juce::Path buildCablePath (juce::Point<float> start, juce::Point<float> end) const;
        bool hitTestCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot) const;
        int findInputConnectorNear (juce::Point<float> canvasPoint, int excludeSlot) const;

        // Modulation-cable counterparts of the audio-cable methods just above. A destination is
        // now a (slot, paramId) pair rather than just a slot, since a node can expose several
        // modulatable knobs -- findModDestinationNear reports which one via outParamId.
        bool hitTestModCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot, juce::String& outParamId) const;
        int findModDestinationNear (juce::Point<float> canvasPoint, int excludeSlot, juce::String& outParamId) const;

        // Canvas-space position of the destination dot a given node draws for a specific
        // paramId -- used to draw/hit-test an already-existing mod cable's fixed endpoint.
        // Falls back to the node's own position if that node no longer exposes paramId (e.g. its
        // type just changed, and this connection is about to be pruned).
        juce::Point<float> modTargetPositionFor (const NodeComponent* node, const juce::String& paramId) const;

        juce::Point<float> getViewportRelativePosition (const juce::MouseEvent&) const;
        void zoomAroundViewportPoint (float newZoom, juce::Point<float> viewportPoint);

        GGridAudioProcessor& processor;
        std::array<std::unique_ptr<NodeComponent>, kMaxSlots> nodes;

        // The two fixed pseudo-nodes -- always present, never deleted, so plain members rather
        // than living in `nodes` (which mirrors the type-swappable module slots and is indexed
        // 0..kMaxSlots-1 to match APVTS's slot parameter IDs).
        IONodeComponent inputNode { true }, outputNode { false };

        // Detected in refreshLayout() -- when a slot's type changes (whether via Add/Delete or
        // by picking a new type directly on an existing node's own dropdown), prunes any
        // audio/modulation connections that no longer make sense for the new type (e.g. it just
        // became -- or stopped being -- an LFO). Seeded from the actual loaded state at
        // construction time, not defaulted to None, so a freshly opened project with existing
        // wiring doesn't get treated as "everything just changed type" on the first tick.
        std::array<ModuleType, kMaxSlots> lastKnownType {};

        float zoomLevel = 1.0f;
        juce::Viewport* ownerViewport = nullptr;

        // Cable-drag state, shared by both gesture sources: dragging fresh from a node's output
        // nub (handleOutputDrag*), and grabbing/rewiring an existing cable (this component's own
        // mouseDown/Drag/Up, when the gesture starts on a cable rather than blank space). When
        // rewiring, the grabbed edge is removed immediately at grab time (see mouseDown), so both
        // sources look identical from mouseUp onward: "if a valid target is found, add the edge;
        // otherwise there's nothing left to do" -- no separate "detach" bookkeeping needed.
        bool isDraggingCable = false;
        bool isDraggingModCable = false;
        int cableDragSourceSlot = -1;
        // Only meaningful mid-drag when isDraggingModCable and the drag started by regrabbing an
        // existing mod cable (not a fresh drag from an LFO's output nub) -- lets mouseUp know
        // which destination param the grabbed cable belonged to, in case it needs to reattach to
        // the same node it came from (dropping back where you picked up from should just work).
        juce::String cableDragModParam;
        juce::Point<float> cableDragCurrentPos;

        // The most recently added node this session, so a freshly added node auto-chains after
        // it by default (matching the old chainOrder behaviour for simple serial patches) rather
        // than appearing fully disconnected every time. -1 when there's nothing to chain after
        // (fresh instance, or the last-added node was since deleted).
        int lastAddedSlot = -1;

        // Blank-canvas gesture state: right-click adds a node (handled entirely within
        // mouseDown, doesn't touch this state machine); a left-drag pans or rubber-band selects,
        // decided in mouseDrag() once the movement threshold is crossed -- see
        // blankDragHoldStartMs/holdBeforeSelectMs.
        enum class BlankDragMode { none, pendingGesture, panning };
        BlankDragMode blankDragMode = BlankDragMode::none;
        juce::Point<float> dragStartViewportPos, lastPanViewportPos;

        // Canvas-space point the current blank-drag gesture started at, and the wall-clock time
        // it started -- used only to decide, once movement crosses the threshold, whether this
        // resolves as a pan (quick) or a rubber-band select (held briefly first). See mouseDrag().
        juce::Point<float> blankDragStartCanvasPos;
        double blankDragHoldStartMs = 0.0;
        static constexpr double holdBeforeSelectMs = 250.0;

        bool rubberBandActive = false;
        juce::Point<float> rubberBandStart, rubberBandCurrent;

        // Sized for every graph node, including the two pseudo-nodes -- Input/Output can be
        // selected and dragged around like any other node (see handleNodeGrabbed/Dragged), just
        // never deleted (deleteSelectedNodes only ever iterates the real module slots).
        std::array<bool, kNumGraphNodes> selected {};

        // Node-drag gesture state, mirroring BlankDragMode's click-vs-drag threshold: grabbing an
        // already-selected node's title bar preserves the current (possibly multi-node)
        // selection tentatively -- it only collapses to just that node if the gesture resolves
        // as a plain click (see handleNodeReleased). Crossing the threshold moves every selected
        // node by the same canvas-space delta.
        enum class NodeDragMode { none, pendingClickOrMove, movingGroup };
        NodeDragMode nodeDragMode = NodeDragMode::none;
        juce::Point<float> nodeDragStartCanvasPos, nodeDragStartViewportPos;
        std::array<juce::Point<float>, kNumGraphNodes> nodeDragStartPositions {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeGraphEditor)
    };
}
