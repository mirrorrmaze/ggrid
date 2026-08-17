#include "NodeComponent.h"
#include "GGridLookAndFeel.h"
#include "../Rack/ConnectionGraph.h"

namespace GGrid
{
    void NodeComponent::OutputNub::paint (juce::Graphics& g)
    {
        g.setColour (isMod ? Palette::modAccent : Palette::accent);
        g.fillEllipse (getLocalBounds().toFloat().reduced (2.0f));
    }

    NodeComponent::NodeComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndexIn, RackSlot& rackSlot,
                                   juce::AudioVisualiserComponent& scopeIn)
        : slotIndex (slotIndexIn), scope (scopeIn)
    {
        // titleBar added first (behind, in z-order) so it only receives clicks that fall through
        // the header's actual controls (added after, in front) -- clicking blank header space
        // drags the node; clicking the type box/bypass/delete hits those instead.
        addAndMakeVisible (titleBar);
        addChildComponent (scope); // visibility toggled in resized() -- only shown for Input/Output

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

        addAndMakeVisible (outputNub0);
        addAndMakeVisible (outputNub1);
        addAndMakeVisible (outputNub2);
        addAndMakeVisible (outputNub3);
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
        lossyPanel       = std::make_unique<LossyControlsPanel> (apvts, slotIndex);
        eq8Panel         = std::make_unique<Eq8ControlsPanel> (apvts, slotIndex);
        chorusPanel      = std::make_unique<ChorusControlsPanel> (apvts, slotIndex);
        eq3Panel         = std::make_unique<Eq3ControlsPanel> (apvts, slotIndex);
        addAndMakeVisible (*waveshaperPanel);
        addAndMakeVisible (*filterPanel);
        addAndMakeVisible (*delayPanel);
        addAndMakeVisible (*dynamicsPanel);
        addAndMakeVisible (*convolutionPanel);
        addAndMakeVisible (*utilityPanel);
        addAndMakeVisible (*ringModPanel);
        addAndMakeVisible (*lfoPanel);
        addAndMakeVisible (*lossyPanel);
        addAndMakeVisible (*eq8Panel);
        addAndMakeVisible (*chorusPanel);
        addAndMakeVisible (*eq3Panel);

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
        lossyPanel->setVisible (type == ModuleType::lossy);
        eq8Panel->setVisible (type == ModuleType::eq8);
        chorusPanel->setVisible (type == ModuleType::chorus);
        eq3Panel->setVisible (type == ModuleType::eq3);
    }

    int NodeComponent::getPreferredHeight() const
    {
        constexpr int header = 28, headerGap = 6, padding = 16;
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);

        // Matches each panel's own resized() row math exactly -- see ModuleControlPanels.cpp.
        int contentHeight;
        switch (type)
        {
            case ModuleType::waveshaper:  contentHeight = 222; break; // curveArea(60) + gap(6) + knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::filter:      contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::delay:       contentHeight = 248; break; // knobRow(106) + gap(6) + filterRow(106) + gap(6) + bottomRow(24)
            case ModuleType::dynamics:    contentHeight = 218; break; // topRow(106) + gap(6) + bottomRow(106)
            case ModuleType::convolution: contentHeight = 324; break; // irRow(24)+gap+waveform(70)+gap+2 knob rows(106 each)
            case ModuleType::utility:     contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::ringMod:     contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::lfo:         contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::lossy:       contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::eq8:         contentHeight = 330; break; // 3 knob rows(106 each) + 2 gaps(6 each)
            case ModuleType::chorus:      contentHeight = 268; break; // knobRow(106) + gap(6) + knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::eq3:         contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::input:
            case ModuleType::output:      contentHeight = 80;  break; // just the oscilloscope
            case ModuleType::none:
            default:                      contentHeight = 0;   break;
        }

        return header + headerGap + contentHeight + padding;
    }

    int NodeComponent::getPreferredWidth() const
    {
        return (isInputType() || isOutputType()) ? 150 : 380;
    }

    juce::Point<int> NodeComponent::getInputConnectorPosition (int portIndex) const
    {
        return { 0, getHeight() * (portIndex + 1) / (kMaxPortsPerSide + 1) };
    }

    juce::Point<int> NodeComponent::getOutputConnectorPosition (int portIndex) const
    {
        return { getWidth(), getHeight() * (portIndex + 1) / (kMaxPortsPerSide + 1) };
    }

    bool NodeComponent::isLfoType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::lfo;
    }

    bool NodeComponent::isInputType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::input;
    }

    bool NodeComponent::isOutputType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::output;
    }

    juce::Point<int> NodeComponent::getModOutputPosition() const
    {
        return { getWidth(), getHeight() / 2 };
    }

    int NodeComponent::getModTargetCount() const
    {
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::waveshaper:  return waveshaperPanel->getModTargetCount();
            case ModuleType::filter:      return filterPanel->getModTargetCount();
            case ModuleType::delay:       return delayPanel->getModTargetCount();
            case ModuleType::dynamics:    return dynamicsPanel->getModTargetCount();
            case ModuleType::convolution: return convolutionPanel->getModTargetCount();
            case ModuleType::utility:     return utilityPanel->getModTargetCount();
            case ModuleType::ringMod:     return ringModPanel->getModTargetCount();
            case ModuleType::lossy:       return lossyPanel->getModTargetCount();
            case ModuleType::eq8:         return eq8Panel->getModTargetCount();
            case ModuleType::chorus:      return chorusPanel->getModTargetCount();
            case ModuleType::eq3:         return eq3Panel->getModTargetCount();
            default:                      return 0;
        }
    }

    juce::String NodeComponent::getModTargetParamId (int index) const
    {
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::waveshaper:  return waveshaperPanel->getModTarget (index).paramId;
            case ModuleType::filter:      return filterPanel->getModTarget (index).paramId;
            case ModuleType::delay:       return delayPanel->getModTarget (index).paramId;
            case ModuleType::dynamics:    return dynamicsPanel->getModTarget (index).paramId;
            case ModuleType::convolution: return convolutionPanel->getModTarget (index).paramId;
            case ModuleType::utility:     return utilityPanel->getModTarget (index).paramId;
            case ModuleType::ringMod:     return ringModPanel->getModTarget (index).paramId;
            case ModuleType::lossy:       return lossyPanel->getModTarget (index).paramId;
            case ModuleType::eq8:         return eq8Panel->getModTarget (index).paramId;
            case ModuleType::chorus:      return chorusPanel->getModTarget (index).paramId;
            case ModuleType::eq3:         return eq3Panel->getModTarget (index).paramId;
            default:                      return {};
        }
    }

    juce::Point<int> NodeComponent::getModTargetPosition (int index) const
    {
        juce::Slider* slider = nullptr;
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::waveshaper:  slider = waveshaperPanel->getModTarget (index).slider; break;
            case ModuleType::filter:      slider = filterPanel->getModTarget (index).slider; break;
            case ModuleType::delay:       slider = delayPanel->getModTarget (index).slider; break;
            case ModuleType::dynamics:    slider = dynamicsPanel->getModTarget (index).slider; break;
            case ModuleType::convolution: slider = convolutionPanel->getModTarget (index).slider; break;
            case ModuleType::utility:     slider = utilityPanel->getModTarget (index).slider; break;
            case ModuleType::ringMod:     slider = ringModPanel->getModTarget (index).slider; break;
            case ModuleType::lossy:       slider = lossyPanel->getModTarget (index).slider; break;
            case ModuleType::eq8:         slider = eq8Panel->getModTarget (index).slider; break;
            case ModuleType::chorus:      slider = chorusPanel->getModTarget (index).slider; break;
            case ModuleType::eq3:         slider = eq3Panel->getModTarget (index).slider; break;
            default:                      break;
        }

        if (slider == nullptr)
            return {};

        // Centered horizontally on the knob (not a corner -- a corner sits on the shared
        // boundary with an adjacent knob and reads as belonging to the wrong one, see task 66),
        // and vertically in the middle of the 16px gap each panel now leaves between a knob's
        // label and the knob itself (see the "mod-destination nub" comment in
        // ModuleControlPanels.cpp's layoutKnob), so the dot touches neither -- sitting right at
        // the knob's edge made it look fused to the rotary ring instead of a separate target.
        const auto bounds = slider->getBounds();
        constexpr int dotClearanceAboveKnob = 8;
        return contentAreaOrigin + juce::Point<int> (bounds.getCentreX(), bounds.getY() - dotClearanceAboveKnob);
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
        // aren't part of the audio graph -- see LFOModule); Input nodes have no input ports at
        // all (they're a source, not a destination -- see isInputType()).
        if (! isLfoType() && ! isInputType())
        {
            g.setColour (Palette::accent);
            for (int port = 0; port < kMaxPortsPerSide; ++port)
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (getInputConnectorPosition (port).toFloat()));
        }

        const int modTargetCount = getModTargetCount();
        if (modTargetCount > 0)
        {
            g.setColour (Palette::modAccent);
            for (int i = 0; i < modTargetCount; ++i)
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (getModTargetPosition (i).toFloat()));
        }
    }

    void NodeComponent::resized()
    {
        constexpr int headerHeight = 28;
        constexpr int padding = 8;

        titleBar.setBounds (0, 0, getWidth(), headerHeight);

        const bool isIOType = isInputType() || isOutputType();

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 4);

        // The compact Input/Output box has no room for a title label alongside a usable type
        // dropdown -- every other type keeps it (matches "Slot N" everywhere else in the rack).
        titleLabel.setVisible (! isIOType);
        if (! isIOType)
            titleLabel.setBounds (header.removeFromLeft (50));

        deleteButton.setBounds (header.removeFromRight (24));
        header.removeFromRight (4);

        // Bypass has no effect on Input/Output nodes -- RackSlot::process() never runs for them
        // at all (see GGridAudioProcessor::processBlock), so showing a toggle that silently does
        // nothing would be misleading, and reserving its space would starve the compact box's
        // already-tight header.
        bypassButton.setVisible (! isIOType);
        if (! isIOType)
        {
            bypassButton.setBounds (header.removeFromRight (70));
            header.removeFromRight (6);
        }

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
        lossyPanel->setBounds (contentArea);
        eq8Panel->setBounds (contentArea);
        chorusPanel->setBounds (contentArea);
        eq3Panel->setBounds (contentArea);

        scope.setBounds (contentArea);
        scope.setVisible (isIOType);

        const bool lfo = isLfoType();
        // Output nodes have no output ports at all -- their whole purpose is collecting incoming
        // audio into the final mix, not producing any (see isOutputType()).
        const bool hideOutputPorts = lfo || isOutputType();
        outputNub0.setVisible (! hideOutputPorts);
        outputNub1.setVisible (! hideOutputPorts);
        outputNub2.setVisible (! hideOutputPorts);
        outputNub3.setVisible (! hideOutputPorts);
        modOutputNub.setVisible (lfo);

        OutputNub* outputNubs[kMaxPortsPerSide] = { &outputNub0, &outputNub1, &outputNub2, &outputNub3 };
        for (int port = 0; port < kMaxPortsPerSide; ++port)
        {
            const auto pos = getOutputConnectorPosition (port);
            outputNubs[port]->setBounds (pos.x - 8, pos.y - 8, 16, 16);
        }
        modOutputNub.setBounds (getWidth() - 8, getHeight() / 2 - 8, 16, 16);
    }
}
