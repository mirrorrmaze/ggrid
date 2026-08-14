#include "MasterControlsPanel.h"
#include "GGridLookAndFeel.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    MasterControlsPanel::MasterControlsPanel (juce::AudioProcessorValueTreeState& apvts)
    {
        titleLabel.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        addAndMakeVisible (titleLabel);

        addAndMakeVisible (limiterEnabledButton);

        ceilingSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        ceilingSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
        ceilingLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (ceilingSlider);
        addAndMakeVisible (ceilingLabel);

        enabledAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, masterLimiterEnabledParamId(), limiterEnabledButton);
        ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, masterLimiterCeilingParamId(), ceilingSlider);
    }

    void MasterControlsPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.5f);
    }

    void MasterControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (8);

        titleLabel.setBounds (area.removeFromTop (20));
        limiterEnabledButton.setBounds (area.removeFromTop (20));

        area.removeFromTop (2);
        ceilingLabel.setBounds (area.removeFromTop (14));
        ceilingSlider.setBounds (area);
    }
}
