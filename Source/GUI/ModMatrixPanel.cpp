#include "ModMatrixPanel.h"
#include "GGridLookAndFeel.h"

namespace GGrid
{
    ModMatrixPanel::ModMatrixPanel (juce::AudioProcessorValueTreeState& apvts)
        : limiterPanel (apvts)
    {
        titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        addAndMakeVisible (titleLabel);

        addAndMakeVisible (limiterPanel);

        for (int i = 0; i < kNumModRoutes; ++i)
        {
            auto row = std::make_unique<RouteRow>();

            row->indexLabel.setText ("R" + juce::String (i + 1), juce::dontSendNotification);
            row->indexLabel.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (row->indexLabel);

            int itemId = 1;
            for (auto& choice : getModSourceChoices())
                row->sourceBox.addItem (choice, itemId++);
            addAndMakeVisible (row->sourceBox);

            itemId = 1;
            for (auto& choice : getModDestinationChoices())
                row->destinationBox.addItem (choice, itemId++);
            addAndMakeVisible (row->destinationBox);

            row->depthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
            row->depthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            addAndMakeVisible (row->depthSlider);

            row->sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, modRouteSourceParamId (i), row->sourceBox);
            row->destinationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                apvts, modRouteDestinationParamId (i), row->destinationBox);
            row->depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                apvts, modRouteDepthParamId (i), row->depthSlider);

            rows.add (row.release());
        }
    }

    void ModMatrixPanel::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.5f);
    }

    void ModMatrixPanel::resized()
    {
        auto area = getLocalBounds().reduced (8);

        titleLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);

        for (auto* row : rows)
        {
            auto rowArea = area.removeFromTop (rowHeight - 2);

            row->indexLabel.setBounds (rowArea.removeFromLeft (28));
            rowArea.removeFromLeft (4);
            row->sourceBox.setBounds (rowArea.removeFromLeft (120));
            rowArea.removeFromLeft (6);
            row->destinationBox.setBounds (rowArea.removeFromLeft (220));
            rowArea.removeFromLeft (6);
            row->depthSlider.setBounds (rowArea);

            area.removeFromTop (2);
        }

        area.removeFromTop (10);
        limiterPanel.setBounds (area.removeFromTop (MasterControlsPanel::preferredHeight)
                                     .removeFromLeft (MasterControlsPanel::preferredWidth));
    }
}
