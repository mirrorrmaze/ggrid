#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Rack/RackSlot.h"
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
        NodeComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndexIn, RackSlot& rackSlot);

        void resized() override;
        void paint (juce::Graphics&) override;

        int getSlotIndex() const { return slotIndex; }
        int getPreferredHeight() const;
        static constexpr int preferredWidth = 380;

        // Each node has 2 input dots and 2 output nubs (portIndex 0/1) -- capacity for 2 parallel
        // connections per side, not functionally distinct ports; which physical dot a given cable
        // lands on is purely which ordinal connection it is (see NodeGraphEditor's port-counting
        // when drawing/hit-testing), not something the engine cares about.
        juce::Point<int> getInputConnectorPosition (int portIndex) const;
        juce::Point<int> getOutputConnectorPosition (int portIndex) const;

        // Title-bar gesture callbacks -- NodeGraphEditor owns the resulting selection/position
        // logic entirely (single click select, shift-click toggle, drag to move -- possibly the
        // whole current selection at once). See the class comment above for why this component
        // doesn't do any of that math itself.
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeGrabbed;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeDragged;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onNodeReleased;

        // Fired when the delete ("X") button is clicked.
        std::function<void (int slotIndex)> onDeleteRequested;

        // Highlights the node's border when it's part of the current selection.
        void setSelected (bool shouldBeSelected);

        // Fired as the output connector is dragged; the MouseEvent is relative to this
        // NodeComponent and the owner re-targets it to canvas coordinates via
        // MouseEvent::getEventRelativeTo().
        std::function<void (int slotIndex, const juce::MouseEvent&)> onOutputDragStart;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onOutputDrag;
        std::function<void (int slotIndex, const juce::MouseEvent&)> onOutputDragEnd;

    private:
        void updateVisiblePanel();

        struct TitleBar : public juce::Component
        {
            explicit TitleBar (NodeComponent& ownerIn) : owner (ownerIn) {}
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onNodeGrabbed) owner.onNodeGrabbed (owner.slotIndex, e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onNodeDragged) owner.onNodeDragged (owner.slotIndex, e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onNodeReleased) owner.onNodeReleased (owner.slotIndex, e); }

            NodeComponent& owner;
        };

        struct OutputNub : public juce::Component
        {
            explicit OutputNub (NodeComponent& ownerIn) : owner (ownerIn) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onOutputDragStart) owner.onOutputDragStart (owner.slotIndex, e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onOutputDrag) owner.onOutputDrag (owner.slotIndex, e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onOutputDragEnd) owner.onOutputDragEnd (owner.slotIndex, e); }

            NodeComponent& owner;
        };

        int slotIndex;

        TitleBar titleBar { *this };
        juce::Label titleLabel;
        juce::ComboBox typeBox;
        juce::ToggleButton bypassButton { "Bypass" };
        juce::TextButton deleteButton { "X" };
        OutputNub outputNubTop { *this }, outputNubBottom { *this };

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

        std::unique_ptr<WaveshaperControlsPanel> waveshaperPanel;
        std::unique_ptr<FilterControlsPanel> filterPanel;
        std::unique_ptr<DelayControlsPanel> delayPanel;
        std::unique_ptr<DynamicsControlsPanel> dynamicsPanel;
        std::unique_ptr<ConvolutionControlsPanel> convolutionPanel;

        bool isSelectedFlag = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeComponent)
    };
}
