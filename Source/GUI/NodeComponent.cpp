#include "NodeComponent.h"
#include "GGridLookAndFeel.h"

namespace GGrid
{
    void NodeComponent::OutputNub::paint (juce::Graphics& g)
    {
        g.setColour (isMod ? Palette::modAccent : Palette::accent);
        g.fillEllipse (getLocalBounds().toFloat().reduced (2.0f));
    }

    NodeComponent::NodeComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndexIn, RackSlot& rackSlot)
        : slotIndex (slotIndexIn)
    {
        // titleBar added first (behind, in z-order) so it only receives clicks that fall through
        // the header's actual controls (added after, in front) -- clicking blank header space
        // drags the node; clicking the type box/bypass/delete hits those instead.
        addAndMakeVisible (titleBar);

        titleLabel.setText ("Slot " + juce::String (slotIndex + 1), juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        titleLabel.setInterceptsMouseClicks (false, false); // let drags started on the label through to titleBar
        addAndMakeVisible (titleLabel);

        int itemId = 1;
        for (auto& choice : getModuleTypeChoices())
            typeBox.addItem (choice, itemId++);
        addAndMakeVisible (typeBox);

        addAndMakeVisible (bypassButton);

        deleteButton.onClick = [this] { if (onDeleteRequested) onDeleteRequested (slotIndex); };
        addAndMakeVisible (deleteButton);

        addAndMakeVisible (outputNubTop);
        addAndMakeVisible (outputNubBottom);
        addAndMakeVisible (modOutputNub);

        typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, slotTypeParamId (slotIndex), typeBox);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, slotBypassParamId (slotIndex), bypassButton);

        waveshaperPanel  = std::make_unique<WaveshaperControlsPanel> (apvts, slotIndex);
        filterPanel      = std::make_unique<FilterControlsPanel> (apvts, slotIndex);
        delayPanel       = std::make_unique<DelayControlsPanel> (apvts, slotIndex);
        dynamicsPanel    = std::make_unique<DynamicsControlsPanel> (apvts, slotIndex);
        convolutionPanel = std::make_unique<ConvolutionControlsPanel> (apvts, slotIndex, rackSlot);
        utilityPanel     = std::make_unique<UtilityControlsPanel> (apvts, slotIndex);
        ringModPanel     = std::make_unique<RingModControlsPanel> (apvts, slotIndex);
        lfoPanel         = std::make_unique<LfoControlsPanel> (apvts, slotIndex);
        addAndMakeVisible (*waveshaperPanel);
        addAndMakeVisible (*filterPanel);
        addAndMakeVisible (*delayPanel);
        addAndMakeVisible (*dynamicsPanel);
        addAndMakeVisible (*convolutionPanel);
        addAndMakeVisible (*utilityPanel);
        addAndMakeVisible (*ringModPanel);
        addAndMakeVisible (*lfoPanel);

        typeBox.onChange = [this] { updateVisiblePanel(); };
        updateVisiblePanel();
    }

    void NodeComponent::updateVisiblePanel()
    {
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);
        waveshaperPanel->setVisible (type == ModuleType::waveshaper);
        filterPanel->setVisible (type == ModuleType::filter);
        delayPanel->setVisible (type == ModuleType::delay);
        dynamicsPanel->setVisible (type == ModuleType::dynamics);
        convolutionPanel->setVisible (type == ModuleType::convolution);
        utilityPanel->setVisible (type == ModuleType::utility);
        ringModPanel->setVisible (type == ModuleType::ringMod);
        lfoPanel->setVisible (type == ModuleType::lfo);
    }

    int NodeComponent::getPreferredHeight() const
    {
        constexpr int header = 28, headerGap = 6, padding = 16;
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);

        // Matches each panel's own resized() row math exactly -- see ModuleControlPanels.cpp.
        int contentHeight;
        switch (type)
        {
            case ModuleType::waveshaper:  contentHeight = 146; break; // knobRow(96) + gap(6) + bottomRow(44)
            case ModuleType::filter:      contentHeight = 146; break; // knobRow(96) + gap(6) + bottomRow(44)
            case ModuleType::delay:       contentHeight = 228; break; // knobRow(96) + gap(6) + filterRow(96) + gap(6) + bottomRow(24)
            case ModuleType::dynamics:    contentHeight = 198; break; // topRow(96) + gap(6) + bottomRow(96)
            case ModuleType::convolution: contentHeight = 304; break; // irRow(24)+gap+waveform(70)+gap+2 knob rows(96 each)
            case ModuleType::utility:     contentHeight = 146; break; // knobRow(96) + gap(6) + bottomRow(44)
            case ModuleType::ringMod:     contentHeight = 146; break; // knobRow(96) + gap(6) + bottomRow(44)
            case ModuleType::lfo:         contentHeight = 146; break; // knobRow(96) + gap(6) + bottomRow(44)
            case ModuleType::none:
            default:                      contentHeight = 0;   break;
        }

        return header + headerGap + contentHeight + padding;
    }

    juce::Point<int> NodeComponent::getInputConnectorPosition (int portIndex) const
    {
        return { 0, getHeight() * (portIndex == 0 ? 1 : 2) / 3 };
    }

    juce::Point<int> NodeComponent::getOutputConnectorPosition (int portIndex) const
    {
        return { getWidth(), getHeight() * (portIndex == 0 ? 1 : 2) / 3 };
    }

    bool NodeComponent::isLfoType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::lfo;
    }

    juce::Point<int> NodeComponent::getModOutputPosition() const
    {
        return { getWidth(), getHeight() / 2 };
    }

    bool NodeComponent::hasModDestination() const
    {
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);
        return type == ModuleType::filter || type == ModuleType::waveshaper || type == ModuleType::convolution;
    }

    ModDestinationParam NodeComponent::getModDestinationParam() const
    {
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::filter:      return ModDestinationParam::filterFrequency;
            case ModuleType::waveshaper:  return ModDestinationParam::waveshaperDrive;
            case ModuleType::convolution: return ModDestinationParam::convolutionMix;
            default:                      return ModDestinationParam::filterFrequency; // unreachable if hasModDestination() was checked first
        }
    }

    juce::Point<int> NodeComponent::getModDestinationPosition() const
    {
        juce::Rectangle<int> knobBounds;
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::filter:      knobBounds = filterPanel->getModTargetKnobBounds(); break;
            case ModuleType::waveshaper:  knobBounds = waveshaperPanel->getModTargetKnobBounds(); break;
            case ModuleType::convolution: knobBounds = convolutionPanel->getModTargetKnobBounds(); break;
            default:                      return {};
        }

        // Top-right corner of the knob, translated from the panel's own coordinates into this
        // component's.
        return contentAreaOrigin + juce::Point<int> (knobBounds.getRight(), knobBounds.getY());
    }

    void NodeComponent::setSelected (bool shouldBeSelected)
    {
        if (isSelectedFlag == shouldBeSelected) return;
        isSelectedFlag = shouldBeSelected;
        repaint();
    }

    void NodeComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (isSelectedFlag ? Palette::bright : Palette::dim);
        g.drawRect (bounds, isSelectedFlag ? 2.5f : 1.5f);

        // Input dots are a static visual only -- dragging always starts from an (separately
        // interactive) output nub, never from here. LFO nodes have no audio ports at all (they
        // aren't part of the audio graph -- see LFOModule).
        if (! isLfoType())
        {
            g.setColour (Palette::accent);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (getInputConnectorPosition (0).toFloat()));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (getInputConnectorPosition (1).toFloat()));
        }

        if (hasModDestination())
        {
            g.setColour (Palette::modAccent);
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (getModDestinationPosition().toFloat()));
        }
    }

    void NodeComponent::resized()
    {
        constexpr int headerHeight = 28;
        constexpr int padding = 8;

        titleBar.setBounds (0, 0, getWidth(), headerHeight);

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 4);
        titleLabel.setBounds (header.removeFromLeft (50));

        deleteButton.setBounds (header.removeFromRight (24));
        header.removeFromRight (4);
        bypassButton.setBounds (header.removeFromRight (70));
        header.removeFromRight (6);

        typeBox.setBounds (header);

        auto contentArea = getLocalBounds();
        contentArea.removeFromTop (headerHeight + 6);
        contentArea = contentArea.reduced (padding, 0);
        contentArea.removeFromBottom (padding);
        contentAreaOrigin = contentArea.getPosition();

        waveshaperPanel->setBounds (contentArea);
        filterPanel->setBounds (contentArea);
        delayPanel->setBounds (contentArea);
        dynamicsPanel->setBounds (contentArea);
        convolutionPanel->setBounds (contentArea);
        utilityPanel->setBounds (contentArea);
        ringModPanel->setBounds (contentArea);
        lfoPanel->setBounds (contentArea);

        const bool lfo = isLfoType();
        outputNubTop.setVisible (! lfo);
        outputNubBottom.setVisible (! lfo);
        modOutputNub.setVisible (lfo);

        outputNubTop.setBounds (getWidth() - 8, getHeight() / 3 - 8, 16, 16);
        outputNubBottom.setBounds (getWidth() - 8, (getHeight() * 2) / 3 - 8, 16, 16);
        modOutputNub.setBounds (getWidth() - 8, getHeight() / 2 - 8, 16, 16);
    }
}
