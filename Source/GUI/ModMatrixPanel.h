#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Modulation/ModulationMatrix.h"
#include "MasterControlsPanel.h"

namespace GGrid
{
    // A compact table of kNumModRoutes rows: Source -> Destination, Depth, plus the safety
    // limiter controls tucked in along the bottom. The limiter used to live in the always-visible
    // master strip, but it's an on-and-forget setting for most users -- it lives here instead so
    // the canvas tab doesn't have to spend space on it.
    class ModMatrixPanel : public juce::Component
    {
    public:
        explicit ModMatrixPanel (juce::AudioProcessorValueTreeState& apvts);

        void resized() override;
        void paint (juce::Graphics&) override;

        static constexpr int rowHeight = 26;
        static constexpr int preferredHeight = 22 + kNumModRoutes * rowHeight + 10 + MasterControlsPanel::preferredHeight + 10;

    private:
        struct RouteRow
        {
            juce::Label indexLabel;
            juce::ComboBox sourceBox, destinationBox;
            juce::Slider depthSlider;

            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment, destinationAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
        };

        juce::Label titleLabel { {}, "MIDI Mod Matrix" };
        juce::OwnedArray<RouteRow> rows;

        MasterControlsPanel limiterPanel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrixPanel)
    };
}
