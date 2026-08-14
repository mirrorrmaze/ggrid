#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Master strip: the safety limiter's enable toggle + ceiling knob. Deliberately NOT a rack
    // slot -- see Identifiers::masterLimiterEnabledParamId for why it has to stay pinned as the
    // last stage rather than being reorderable.
    class MasterControlsPanel : public juce::Component
    {
    public:
        explicit MasterControlsPanel (juce::AudioProcessorValueTreeState& apvts);

        void resized() override;
        void paint (juce::Graphics&) override;

        static constexpr int preferredWidth = 200;
        static constexpr int preferredHeight = 90;

    private:
        juce::Label titleLabel { {}, "Safety" };
        juce::ToggleButton limiterEnabledButton { "Limiter On" };
        juce::Label ceilingLabel { {}, "Ceiling" };
        juce::Slider ceilingSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enabledAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ceilingAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterControlsPanel)
    };
}
