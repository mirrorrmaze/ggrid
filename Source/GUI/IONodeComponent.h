#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Rack/ConnectionGraph.h"

namespace GGrid
{
    // One of the two fixed pseudo-nodes on the patch-bay canvas: Input (the plugin's raw dry
    // signal -- output ports only, nothing to configure) or Output (the final mix -- input ports
    // only). Always present, never deletable, no type dropdown/bypass/control panel -- unlike
    // NodeComponent, which wraps a swappable module instance, an IONodeComponent's identity never
    // changes, so it's a much smaller, purpose-built sibling rather than a NodeComponent variant.
    // Draggable by grabbing anywhere on its body (no separate title bar needed, since there's no
    // competing control to click) -- NodeGraphEditor drives the actual position math, same as it
    // does for NodeComponent (see that class's comment for why).
    class IONodeComponent : public juce::Component
    {
    public:
        explicit IONodeComponent (bool isInputIn);

        void resized() override;
        void paint (juce::Graphics&) override;

        bool isInput() const { return isInputNode; }

        // Highlights the node's border when it's part of the current canvas selection -- mirrors
        // NodeComponent::setSelected so both node kinds behave identically under multi-select/
        // group-move, even though they don't share a common base beyond juce::Component.
        void setSelected (bool shouldBeSelected);

        static constexpr int width = 120;
        static constexpr int height = 150;

        // Port positions: Input's are output ports (right edge, draggable -- see onOutputDrag*
        // below); Output's are input ports (left edge, static targets only).
        juce::Point<int> getPortPosition (int portIndex) const;

        std::function<void (const juce::MouseEvent&)> onNodeGrabbed;
        std::function<void (const juce::MouseEvent&)> onNodeDragged;
        std::function<void (const juce::MouseEvent&)> onNodeReleased;

        // Only fired when isInput() -- Output has no draggable output port of its own.
        std::function<void (const juce::MouseEvent&)> onOutputDragStart;
        std::function<void (const juce::MouseEvent&)> onOutputDrag;
        std::function<void (const juce::MouseEvent&)> onOutputDragEnd;

    private:
        struct Body : public juce::Component
        {
            explicit Body (IONodeComponent& ownerIn) : owner (ownerIn) {}
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onNodeGrabbed) owner.onNodeGrabbed (e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onNodeDragged) owner.onNodeDragged (e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onNodeReleased) owner.onNodeReleased (e); }

            IONodeComponent& owner;
        };

        struct OutputNub : public juce::Component
        {
            explicit OutputNub (IONodeComponent& ownerIn) : owner (ownerIn) {}
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent& e) override { if (owner.onOutputDragStart) owner.onOutputDragStart (e); }
            void mouseDrag (const juce::MouseEvent& e) override { if (owner.onOutputDrag) owner.onOutputDrag (e); }
            void mouseUp (const juce::MouseEvent& e) override { if (owner.onOutputDragEnd) owner.onOutputDragEnd (e); }

            IONodeComponent& owner;
        };

        bool isInputNode;
        bool isSelectedFlag = false;
        Body body { *this };
        juce::Label titleLabel;
        OutputNub outputNub0 { *this }, outputNub1 { *this }, outputNub2 { *this }, outputNub3 { *this };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IONodeComponent)
    };
}
