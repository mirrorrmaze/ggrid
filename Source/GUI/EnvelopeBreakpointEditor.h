#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Rack/RackSlot.h"
#include "../Modules/EnvelopeModule.h"

namespace GGrid
{
    // The freeform breakpoint editor for EnvelopeModule: click empty space to add a point, drag
    // an existing point to move it (x clamped between its neighbours, y clamped to [0,1] --
    // enforced by EnvelopeModule itself, this just forwards raw pixel positions), right-click an
    // existing point to delete it (the first/last points can't be deleted -- see EnvelopeModule's
    // own comment on why they're pinned). Polls the module on a Timer (matching
    // IRWaveformComponent/CrossoverSplitBar's established pattern) to redraw both the drawn shape
    // itself (which can change from automation/preset load, not just this editor's own drags) and
    // a live playhead line while a one-shot is actually playing.
    //
    // Holds the owning RackSlot rather than an EnvelopeModule reference directly, and
    // dynamic_casts to EnvelopeModule fresh on every timer tick/mouse event -- RackSlot swaps out
    // its module instance whenever the slot's type changes, so holding a module reference from
    // construction time would go stale (matches IRWaveformComponent's own established reasoning).
    class EnvelopeBreakpointEditor : public juce::Component, private juce::Timer
    {
    public:
        explicit EnvelopeBreakpointEditor (RackSlot& rackSlotIn);
        ~EnvelopeBreakpointEditor() override;

        void paint (juce::Graphics&) override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;

        EnvelopeModule* getModule() const;

        juce::Point<float> pixelToNormalised (juce::Point<float> pixel) const;
        juce::Point<float> normalisedToPixel (juce::Point<float> normalised) const;

        int hitTestPoint (juce::Point<float> pixel) const; // -1 if none within grab tolerance

        RackSlot& rackSlot;
        int draggingIndex = -1;

        static constexpr float grabToleranceSquaredPx = 10.0f * 10.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeBreakpointEditor)
    };
}
