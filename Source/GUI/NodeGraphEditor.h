#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "NodeComponent.h"
#include "../PluginProcessor.h"

namespace GGrid
{
    // The patch-bay canvas: click blank space to add a node (Waveshaper/Filter/Delay/Dynamics/
    // Convolution), drag a node's title bar to reposition it, drag a cable from a node's output
    // to another node's input to connect them. Click-drag blank space to pan; scroll wheel zooms
    // toward the cursor. Grab an existing cable (not just a node's output nub) to rewire it
    // elsewhere, or drop it on blank space to genuinely disconnect it.
    //
    // Selection: click a node's title bar to select just it; shift-click to toggle it in/out of
    // the current selection; shift-drag blank space for a rubber-band select. Dragging a title
    // bar that's already part of the selection moves every selected node together, by a shared
    // delta -- see handleNodeGrabbed/Dragged/Released, which mirror the click-vs-pan threshold
    // pattern used for blank canvas below (a plain click on an already-selected node collapses
    // the selection to just that node; a drag preserves the group). Delete/Backspace removes
    // every selected node.
    //
    // Modulation cables: LFO nodes have no audio ports (see NodeComponent::isLfoType) -- instead
    // a single violet modulation-output nub, dragged onto a violet modulation-destination dot on
    // whichever knob a Filter/Waveshaper/Convolution node exposes (Frequency/Drive/Mix). These
    // are a genuinely separate graph from the orange audio cables (GGridAudioProcessor::
    // connections) -- see ModulationMatrix::modConnections -- with their own capacity rule (one
    // source cable per destination knob) and their own cycle-freeness by construction (a
    // modulation destination is never itself draggable as a source). isDraggingModCable is set
    // the moment a drag starts (either grabbing an LFO's output nub, or regrabbing an existing
    // mod cable) and just changes which hit-testing/creation path mouseUp takes -- the gesture
    // itself reuses the exact same onOutputDragStart/Drag/DragEnd callbacks as audio cables.
    //
    // The underlying engine (GGridAudioProcessor::connections) is a real directed graph, not a
    // single linear chain -- each node allows up to 2 outgoing and 2 incoming edges (matching the
    // 2 output nubs / 2 input dots each NodeComponent draws), enough to split a signal into two
    // parallel chains and sum them back together (Bitwig Grid / Ableton parallel-chain style)
    // without a general N-port model. Cycles are rejected when a connection is attempted (see
    // GGridAudioProcessor::canAddConnection) -- block-based processing has no notion of a
    // same-block feedback loop. A freshly added node is auto-wired after whichever node was added
    // immediately before it (see lastAddedSlot), so simple serial chains still "just work" the
    // way they did before this canvas supported branching; explicit rewiring is how you diverge
    // from that into something parallel.
    //
    // Node existence mirrors each slot's type parameter (a node is visible iff its slot's type !=
    // None); node canvas position is stored in ::nodePositions, cosmetic only. Both are
    // reconciled from processor state on a timer rather than through fine-grained callbacks,
    // since several different things can change them (this canvas, automation, undo, project
    // load) and polling is simple and cheap for 8 slots.
    class NodeGraphEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit NodeGraphEditor (GGridAudioProcessor& processorIn);
        ~NodeGraphEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
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
        // 2 output/input port positions it should be drawn/hit-tested at -- just its ordinal
        // among other edges sharing the same endpoint, so multiple cables into/out of one node
        // fan out to visually distinct dots instead of stacking on top of each other.
        int outputPortIndexForConnection (int connectionIndex) const;
        int inputPortIndexForConnection (int connectionIndex) const;

        void handleOutputDragStart (int slotIndex, const juce::MouseEvent&);
        void handleOutputDrag (int slotIndex, const juce::MouseEvent&);
        void handleOutputDragEnd (int slotIndex, const juce::MouseEvent&);

        juce::Path buildCablePath (juce::Point<float> start, juce::Point<float> end) const;
        bool hitTestCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot) const;
        int findInputConnectorNear (juce::Point<float> canvasPoint, int excludeSlot) const;

        // Modulation-cable counterparts of the audio-cable methods just above.
        bool hitTestModCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot, ModDestinationParam& outParam) const;
        int findModDestinationNear (juce::Point<float> canvasPoint, int excludeSlot) const;

        juce::Point<float> getViewportRelativePosition (const juce::MouseEvent&) const;
        void zoomAroundViewportPoint (float newZoom, juce::Point<float> viewportPoint);

        GGridAudioProcessor& processor;
        std::array<std::unique_ptr<NodeComponent>, kMaxSlots> nodes;

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
        ModDestinationParam cableDragModParam = ModDestinationParam::filterFrequency;
        juce::Point<float> cableDragCurrentPos;

        // The most recently added node this session, so a freshly added node auto-chains after
        // it by default (matching the old chainOrder behaviour for simple serial patches) rather
        // than appearing fully disconnected every time. -1 when there's nothing to chain after
        // (fresh instance, or the last-added node was since deleted).
        int lastAddedSlot = -1;

        // Blank-canvas gesture state: a plain click adds a node; a drag pans the canvas; a
        // shift-drag rubber-band selects. Click vs. pan is told apart by a small movement
        // threshold, checked in the owning Viewport's coordinate space (stable regardless of
        // zoom) -- see mouseDrag().
        enum class BlankDragMode { none, pendingClickOrPan, panning };
        BlankDragMode blankDragMode = BlankDragMode::none;
        juce::Point<float> dragStartViewportPos, lastPanViewportPos;

        bool rubberBandActive = false;
        juce::Point<float> rubberBandStart, rubberBandCurrent;

        std::array<bool, kMaxSlots> selected {};

        // Node-drag gesture state, mirroring BlankDragMode's click-vs-drag threshold: grabbing an
        // already-selected node's title bar preserves the current (possibly multi-node)
        // selection tentatively -- it only collapses to just that node if the gesture resolves
        // as a plain click (see handleNodeReleased). Crossing the threshold moves every selected
        // node by the same canvas-space delta.
        enum class NodeDragMode { none, pendingClickOrMove, movingGroup };
        NodeDragMode nodeDragMode = NodeDragMode::none;
        juce::Point<float> nodeDragStartCanvasPos, nodeDragStartViewportPos;
        std::array<juce::Point<float>, kMaxSlots> nodeDragStartPositions {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeGraphEditor)
    };
}
