#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Rack/RackSlot.h"
#include "../Modulation/ModulationMatrix.h"
#include "ModuleControlPanels.h"

namespace GGrid
{
    // One rack slot's node in the patch-bay canvas: a draggable box with a title bar (module
    // type picker, bypass, delete), an output connector on the right edge you drag a cable from,
    // an input connector on the left edge you drop a cable onto, and whichever module-specific
    // controls panel (see ModuleControlPanels.h) matches the current type. Sizes itself to fit
    // that panel's natural content height -- no shared uniform height across node types, since
    // each node here is free-floating rather than a cell in a fixed grid.
    //
    // Position, connection, and selection are NOT this component's own state: NodeGraphEditor
    // owns GGridAudioProcessor::nodePositions (canvas position) and ::connections (which node
    // feeds which), and reconciles this component's bounds/visibility against them every frame.
    // This component only reports gestures (grabbed/dragged/released, deleted, cable drawn
    // from/to) via callbacks -- see the public std::function members below, wired up by
    // NodeGraphEditor. Position math in particular is entirely NodeGraphEditor's job (not this
    // component's own ComponentDragger) because a title-bar drag can mean "move this one node"
    // or "move every currently-selected node together," and only NodeGraphEditor knows which.
    class NodeComponent : public juce::Component
    {
    public:
        NodeComponent (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, RackSlot& rackSlot,
                        juce::AudioVisualiserComponent& scopeIn);

        void resized() override;
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        int getSlotIndex() const { return slotIndex; }
        int getPreferredHeight() const;
        int getFoldedHeight() const { return 28; }
        int getMinimumExpandedHeight() const;
        bool isFolded() const { return isFoldedFlag; }
        void setFolded (bool shouldBeFolded);

        // Input/Output nodes are a small fixed cube -- no control panel to size around, and a
        // compact box matching their "just a wire endpoint" role reads better on a canvas that
        // can otherwise have many of them (see the class-level ModuleType::input/output note).
        // Every other type keeps the wider box sized to fit its control panel comfortably.
        int getPreferredWidth() const;
        int getMinimumWidth() const;

        // Each node has up to kMaxPortsPerSide (4) input dots and 4 output nubs, evenly spaced
        // down each edge -- not functionally distinct ports; which physical dot a given cable
        // lands on is purely which ordinal connection it is (see NodeGraphEditor's port-counting
        // when drawing/hit-testing), not something the engine cares about. Modulation-source
        // nodes (LFO/Envelope/ADSR) don't participate in the audio graph at all (see
        // isModulationSourceType() in Identifiers.h) and hide these entirely, showing a single
        // modulation-output nub instead -- see isModulationSourceType()/getModOutputPosition().
        // Input/Output nodes are one-sided -- see isInputType()/isOutputType() -- an Input node
        // hides its input dots (it has none), an Output node hides its output nubs (it has none).
        juce::Point<int> getInputConnectorPosition (int portIndex) const;
        juce::Point<int> getOutputConnectorPosition (int portIndex) const;

        // True for LFO/Envelope/ADSR/LFO Table nodes -- no audio ports, one modulation-output nub instead
        // (see isModulationSourceType() in Identifiers.h).
        bool isModulationSourceType() const;

        // True for Input/Output nodes -- ordinary addable/deletable module types (not fixed
        // pseudo-nodes) whose only unusual behaviour is a one-sided port set and no control
        // panel: an Input node is a source of the plugin's raw dry signal (output ports only,
        // no bypass toggle since RackSlot::process() never runs for it -- see
        // GGridAudioProcessor::processBlock), an Output node collects into the final mix (input
        // ports only, same reasoning).
        bool isInputType() const;
        bool isOutputType() const;

        // True for 3xOsc nodes -- a graph source like Input (no input ports, generates its own
        // audio rather than processing whatever reaches it -- see ModuleType::threeOsc's own
        // comment), but UNLIKE Input it keeps the full title/bypass/control-panel treatment every
        // regular module gets (it's a real instrument with real knobs, not a compact wire
        // endpoint) -- so it's a separate predicate from isInputType(), not folded into it.
        bool isThreeOscType() const;
        bool isWavetableSynthType() const;

        // True for Multipass nodes -- the only module type with more than one output BUS (Low/
        // Mid/High bands, see RackModule::getNumOutputBuses). Unlike every other type, its output
        // nubs aren't interchangeable/cosmetic: only 3 of the 4 are shown (see resized()) and each
        // one's port index is functionally pinned to a specific band, not auto-assigned ordinally
        // -- see NodeGraphEditor::handleOutputDragStart.
        bool isMultipassType() const;
        bool hasFourOutputBuses() const;

        // No input ports at all: Input (a source of the raw dry signal) and ThreeOsc (a source of
        // its own MIDI-generated audio) alike -- see isInputType()/isThreeOscType(). WT Synth
        // keeps its input ports because incoming audio acts as external FM. Used to hide
        // the input-side connector dots and exclude a node from being a valid cable-drop target.
        bool hasNoInputPorts() const { return isInputType() || isThreeOscType(); }

        juce::Point<int> getModOutputPosition() const;

        // Every continuous knob the currently active control panel exposes is a valid cable
        // modulation destination -- delegates to whichever panel is active (see each panel's
        // getModTargetCount()/getModTarget() in ModuleControlPanels.h). Index-based rather than
        // a single fixed destination since most module types now expose several.
        int getModTargetCount() const;
        juce::String getModTargetParamId (int index) const;
        juce::Point<int> getModTargetPosition (int index) const;

        // Title-bar gesture callbacks -- NodeGraphEditor owns the resulting selection/position
        // logic entirely (single click select, shift-click toggle, drag to move -- possibly the
        // whole current selection at once). See the class comment above for why this component
        // doesn't do any of that math itself.
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeGrabbed;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeDragged;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeReleased;

        // Bottom-right resize-handle gesture callbacks -- kept separate from title dragging so
        // resizing a module cannot accidentally start moving the node or pulling a cable.
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeResizeGrabbed;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeResizeDragged;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeResizeReleased;

        // Fired when the delete ("X") button is clicked.
        std::function<void (int slotIndex)> onDeleteRequested;
        std::function<void (int slotIndex, bool shouldBeFolded)> onFoldToggled;

        // Highlights the node's border when it's part of the current selection.
        void setSelected (bool shouldBeSelected);

        // Fired as the output connector is dragged; the MouseEvent is relative to this
        // NodeComponent and the owner re-targets it to canvas coordinates via
        // MouseEvent::getEventRelativeTo(). portIndex is which of the (up to 4) output nubs was
        // actually grabbed -- ignored by NodeGraphEditor for every ordinary single-bus module
        // (which still auto-spreads cables across dots cosmetically, see
        // outputPortIndexForConnection), only meaningful for a multi-bus module like Multipass.
        std::function<void (int slotIndex, int portIndex, const juce::MouseEvent&)> onOutputDragStart;
        std::function<void (int slotIndex, int portIndex, const juce::MouseEvent&)> onOutputDrag;
        std::function<void (int slotIndex, int portIndex, const juce::MouseEvent&)> onOutputDragEnd;

    private:
        void updateVisiblePanel();
        juce::Colour outputPortColour (int portIndex) const;

        struct TitleBar : public juce::Component
        {
            explicit TitleBar (NodeComponent& ownerIn) : owner (ownerIn) {}
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onNodeGrabbed) owner.onNodeGrabbed (owner.slotIndex, e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onNodeDragged) owner.onNodeDragged (owner.slotIndex, e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onNodeReleased) owner.onNodeReleased (owner.slotIndex, e); }

            NodeComponent& owner;
        };

        // isMod distinguishes color only (violet vs. accent) -- the drag gesture wired to it is
        // identical either way; NodeGraphEditor tells audio and modulation cables apart by
        // checking whether the source node is an LFO, not by anything this component reports.
        struct OutputNub : public juce::Component
        {
            OutputNub (NodeComponent& ownerIn, bool isModIn, int portIndexIn = -1) : owner (ownerIn), isMod (isModIn), portIndex (portIndexIn) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onOutputDragStart) owner.onOutputDragStart (owner.slotIndex, portIndex, e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onOutputDrag) owner.onOutputDrag (owner.slotIndex, portIndex, e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onOutputDragEnd) owner.onOutputDragEnd (owner.slotIndex, portIndex, e); }

            NodeComponent& owner;
            bool isMod;
            int portIndex; // which of the (up to 4) output dots this is -- -1 for modOutputNub (no port concept)
        };

        struct ResizeHandle : public juce::Component
        {
            explicit ResizeHandle (NodeComponent& ownerIn);
            void paint (juce::Graphics&) override;
            void mouseEnter (const juce::MouseEvent&) override { isHovering = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override { isHovering = false; repaint(); }
            void mouseDown (const juce::MouseEvent& e) override;
            void mouseDrag (const juce::MouseEvent& e) override;
            void mouseUp (const juce::MouseEvent& e) override;

            NodeComponent& owner;
            bool isHovering = false;
            bool isDragging = false;
        };

        struct RandomizeButton : public juce::Button
        {
            RandomizeButton();
            void paintButton (juce::Graphics&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RandomizeButton)
        };

        void randomizeCurrentModuleParameters();
        juce::String getCurrentModuleParameterPrefix() const;

        juce::AudioProcessorValueTreeState& apvts;
        int slotIndex;

        // Live waveform for Input/Output nodes only -- owned on the processor (see
        // GGridAudioProcessor::getNodeScope) so it keeps accumulating/rendering correctly across
        // editor open/close the same way outputScope does; this just parents it and only shows
        // it while isInputType()/isOutputType().
        juce::AudioVisualiserComponent& scope;

        TitleBar titleBar { *this };
        juce::Label titleLabel;
        juce::ComboBox typeBox;
        juce::TextButton foldButton { "v" };
        RandomizeButton randomizeButton;
        juce::ToggleButton bypassButton { "Bypass" };
        juce::TextButton deleteButton { "X" };
        OutputNub outputNub0 { *this, false, 0 }, outputNub1 { *this, false, 1 },
                  outputNub2 { *this, false, 2 }, outputNub3 { *this, false, 3 };
        OutputNub modOutputNub { *this, true };
        ResizeHandle resizeHandle { *this };

        // Top-left of wherever the active control panel is placed within this component -- lets
        // getModTargetPosition() translate a panel-local knob position (see ModuleControlPanels'
        // ModTarget::slider bounds) into this component's own coordinates.
        juce::Point<int> contentAreaOrigin;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

        std::unique_ptr<WaveshaperControlsPanel> waveshaperPanel;
        std::unique_ptr<FilterControlsPanel> filterPanel;
        std::unique_ptr<NonlinearFilterControlsPanel> nonlinearFilterPanel;
        std::unique_ptr<MackityControlsPanel> mackityPanel;
        std::unique_ptr<ShimmerReverbControlsPanel> shimmerReverbPanel;
        std::unique_ptr<DelayControlsPanel> delayPanel;
        std::unique_ptr<DynamicsControlsPanel> dynamicsPanel;
        std::unique_ptr<ConvolutionControlsPanel> convolutionPanel;
        std::unique_ptr<UtilityControlsPanel> utilityPanel;
        std::unique_ptr<RingModControlsPanel> ringModPanel;
        std::unique_ptr<LfoControlsPanel> lfoPanel;
        std::unique_ptr<LossyControlsPanel> lossyPanel;
        std::unique_ptr<SpectralClipperControlsPanel> spectralClipperPanel;
        std::unique_ptr<Eq8ControlsPanel> eq8Panel;
        std::unique_ptr<ChorusControlsPanel> chorusPanel;
        std::unique_ptr<Eq3ControlsPanel> eq3Panel;
        std::unique_ptr<MultibandConvolutionControlsPanel> multibandConvolutionPanel;
        std::unique_ptr<ThreeOscControlsPanel> threeOscPanel;
        std::unique_ptr<WavetableSynthControlsPanel> wavetableSynthPanel;
        std::unique_ptr<AdsrControlsPanel> adsrPanel;
        std::unique_ptr<EnvelopeControlsPanel> envelopePanel;
        std::unique_ptr<MultipassControlsPanel> multipassPanel;
        std::unique_ptr<LfoTableControlsPanel> lfoTablePanel;

        bool isSelectedFlag = false;
        bool isFoldedFlag = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
    };
}
