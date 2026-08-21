#include "NodeComponent.h"
#include "GGridLookAndFeel.h"
#include "../Rack/ConnectionGraph.h"

namespace GGrid
{
    NodeComponent::RandomizeButton::RandomizeButton()
        : juce::Button ("Randomize parameters")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void NodeComponent::RandomizeButton::paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
    {
        auto area = getLocalBounds().toFloat().reduced (3.0f);
        const auto base = shouldDrawButtonAsDown ? Palette::bright : (shouldDrawButtonAsHighlighted ? Palette::accent : Palette::dim);

        g.setColour (base.withAlpha (shouldDrawButtonAsDown ? 0.22f : 0.12f));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour (base);
        g.drawRoundedRectangle (area, 3.0f, 1.4f);

        auto pip = [&g, base] (float x, float y)
        {
            g.setColour (base);
            g.fillEllipse (x - 1.4f, y - 1.4f, 2.8f, 2.8f);
        };

        const auto left = area.getX() + area.getWidth() * 0.28f;
        const auto centre = area.getCentreX();
        const auto right = area.getRight() - area.getWidth() * 0.28f;
        const auto top = area.getY() + area.getHeight() * 0.28f;
        const auto middle = area.getCentreY();
        const auto bottom = area.getBottom() - area.getHeight() * 0.28f;

        pip (left, top);
        pip (right, top);
        pip (centre, middle);
        pip (left, bottom);
        pip (right, bottom);
    }

    void NodeComponent::OutputNub::paint (juce::Graphics& g)
    {
        const bool isMultipassBandPort = ! isMod && owner.isMultipassType() && portIndex >= 0 && portIndex < kNumMultipassBands;
        const auto colour = isMod ? Palette::modAccent : owner.outputPortColour (portIndex);

        g.setColour (colour);
        g.fillEllipse (getLocalBounds().toFloat().reduced (isMultipassBandPort ? 1.0f : 2.0f));

        if (isMultipassBandPort)
        {
            g.setColour (Palette::bg.withAlpha (0.72f));
            g.drawEllipse (getLocalBounds().toFloat().reduced (4.0f), 1.0f);
            g.setColour (Palette::bright.withAlpha (0.38f));
            g.drawEllipse (getLocalBounds().toFloat().reduced (1.0f), 1.0f);
        }
    }

    NodeComponent::ResizeHandle::ResizeHandle (NodeComponent& ownerIn) : owner (ownerIn)
    {
        setMouseCursor (juce::MouseCursor::BottomRightCornerResizeCursor);
    }

    void NodeComponent::ResizeHandle::paint (juce::Graphics& g)
    {
        g.setColour ((isHovering || isDragging) ? Palette::bright : Palette::dim.withAlpha (0.78f));

        const auto b = getLocalBounds().toFloat().reduced (4.0f);
        for (int i = 0; i < 3; ++i)
        {
            const float inset = (float) i * 4.0f;
            g.drawLine (b.getRight() - 10.0f + inset, b.getBottom(),
                        b.getRight(), b.getBottom() - 10.0f + inset, 1.4f);
        }
    }

    void NodeComponent::ResizeHandle::mouseDown (const juce::MouseEvent& e)
    {
        isDragging = true;
        repaint();
        if (owner.onNodeResizeGrabbed) owner.onNodeResizeGrabbed (owner.slotIndex, e);
    }

    void NodeComponent::ResizeHandle::mouseDrag (const juce::MouseEvent& e)
    {
        if (owner.onNodeResizeDragged) owner.onNodeResizeDragged (owner.slotIndex, e);
    }

    void NodeComponent::ResizeHandle::mouseUp (const juce::MouseEvent& e)
    {
        isDragging = false;
        repaint();
        if (owner.onNodeResizeReleased) owner.onNodeResizeReleased (owner.slotIndex, e);
    }

    NodeComponent::NodeComponent (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, RackSlot& rackSlot,
                                   juce::AudioVisualiserComponent& scopeIn)
        : apvts (apvtsIn), slotIndex (slotIndexIn), scope (scopeIn)
    {
        // titleBar added first (behind, in z-order) so it only receives clicks that fall through
        // the header's actual controls (added after, in front) -- clicking blank header space
        // drags the node; clicking the type box/bypass/delete hits those instead.
        addAndMakeVisible (titleBar);
        addChildComponent (scope); // visibility toggled in resized() -- only shown for Input/Output
        scope.setInterceptsMouseClicks (false, false);

        titleLabel.setText ("Slot " + juce::String (slotIndex + 1), juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        titleLabel.setInterceptsMouseClicks (false, false); // let drags started on the label through to titleBar
        addAndMakeVisible (titleLabel);

        // Every choice is added, including retired types (e.g. Dynamics) -- ComboBoxAttachment
        // syncs by ITEM POSITION, not by the id passed to addItem (confirmed the hard way: an
        // earlier attempt skipped adding retired entries here to keep them out of this dropdown,
        // which shifted every later entry's position by one and made the box display the wrong
        // type for anything past the gap, e.g. an Input slot showing as "Output"). Skipping an
        // entry here is therefore never safe, regardless of id bookkeeping -- retired types stay
        // selectable through this one dropdown (picking one is harmless: RackSlot::
        // createModuleForType has no case for it, so the slot just ends up with no module, same
        // as picking nothing). AddModuleSearchPopup is a separate, hand-built list keyed directly
        // off ModuleType with no positional dependency, so it can and does exclude them safely.
        int itemId = 1;
        for (auto& choice : getModuleTypeChoices())
            typeBox.addItem (choice, itemId++);
        addAndMakeVisible (typeBox);

        foldButton.onClick = [this]
        {
            if (onFoldToggled)
                onFoldToggled (slotIndex, ! isFoldedFlag);
        };
        addAndMakeVisible (foldButton);

        randomizeButton.onClick = [this] { randomizeCurrentModuleParameters(); };
        addAndMakeVisible (randomizeButton);

        addAndMakeVisible (bypassButton);

        deleteButton.onClick = [this] { if (onDeleteRequested) onDeleteRequested (slotIndex); };
        addAndMakeVisible (deleteButton);

        addAndMakeVisible (outputNub0);
        addAndMakeVisible (outputNub1);
        addAndMakeVisible (outputNub2);
        addAndMakeVisible (outputNub3);
        addAndMakeVisible (modOutputNub);
        addAndMakeVisible (resizeHandle);

        typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvtsIn, slotTypeParamId (slotIndex), typeBox);
        bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvtsIn, slotBypassParamId (slotIndex), bypassButton);

        waveshaperPanel  = std::make_unique<WaveshaperControlsPanel> (apvtsIn, slotIndex);
        filterPanel      = std::make_unique<FilterControlsPanel> (apvtsIn, slotIndex);
        nonlinearFilterPanel = std::make_unique<NonlinearFilterControlsPanel> (apvtsIn, slotIndex);
        mackityPanel     = std::make_unique<MackityControlsPanel> (apvtsIn, slotIndex);
        shimmerReverbPanel = std::make_unique<ShimmerReverbControlsPanel> (apvtsIn, slotIndex);
        delayPanel       = std::make_unique<DelayControlsPanel> (apvtsIn, slotIndex);
        compressorPanel  = std::make_unique<CompressorControlsPanel> (apvtsIn, slotIndex);
        limiterPanel     = std::make_unique<LimiterControlsPanel> (apvtsIn, slotIndex);
        granularPanel    = std::make_unique<GranularControlsPanel> (apvtsIn, slotIndex);
        samplerPanel     = std::make_unique<SamplerControlsPanel> (apvtsIn, slotIndex, rackSlot);
        convolutionPanel = std::make_unique<ConvolutionControlsPanel> (apvtsIn, slotIndex, rackSlot);
        utilityPanel     = std::make_unique<UtilityControlsPanel> (apvtsIn, slotIndex);
        ringModPanel     = std::make_unique<RingModControlsPanel> (apvtsIn, slotIndex);
        lfoPanel         = std::make_unique<LfoControlsPanel> (apvtsIn, slotIndex, rackSlot);
        lossyPanel       = std::make_unique<LossyControlsPanel> (apvtsIn, slotIndex);
        spectralClipperPanel = std::make_unique<SpectralClipperControlsPanel> (apvtsIn, slotIndex);
        eq8Panel         = std::make_unique<Eq8ControlsPanel> (apvtsIn, slotIndex, rackSlot);
        chorusPanel      = std::make_unique<ChorusControlsPanel> (apvtsIn, slotIndex);
        eq3Panel         = std::make_unique<Eq3ControlsPanel> (apvtsIn, slotIndex);
        multibandConvolutionPanel = std::make_unique<MultibandConvolutionControlsPanel> (apvtsIn, slotIndex, rackSlot);
        threeOscPanel    = std::make_unique<ThreeOscControlsPanel> (apvtsIn, slotIndex);
        wavetableSynthPanel = std::make_unique<WavetableSynthControlsPanel> (apvtsIn, slotIndex);
        adsrPanel        = std::make_unique<AdsrControlsPanel> (apvtsIn, slotIndex);
        envelopePanel    = std::make_unique<EnvelopeControlsPanel> (apvtsIn, slotIndex, rackSlot);
        multipassPanel   = std::make_unique<MultipassControlsPanel> (apvtsIn, slotIndex, rackSlot);
        lfoTablePanel    = std::make_unique<LfoTableControlsPanel> (apvtsIn, slotIndex);

        auto letDirectLabelsPassThrough = [] (juce::Component& panel)
        {
            for (int i = 0; i < panel.getNumChildComponents(); ++i)
                if (auto* label = dynamic_cast<juce::Label*> (panel.getChildComponent (i)))
                    label->setInterceptsMouseClicks (false, false);
        };

        wavetableSynthPanel->setInterceptsMouseClicks (false, true);
        waveshaperPanel->setInterceptsMouseClicks (false, true);
        filterPanel->setInterceptsMouseClicks (false, true);
        nonlinearFilterPanel->setInterceptsMouseClicks (false, true);
        mackityPanel->setInterceptsMouseClicks (false, true);
        shimmerReverbPanel->setInterceptsMouseClicks (false, true);
        delayPanel->setInterceptsMouseClicks (false, true);
        compressorPanel->setInterceptsMouseClicks (false, true);
        limiterPanel->setInterceptsMouseClicks (false, true);
        granularPanel->setInterceptsMouseClicks (false, true);
        samplerPanel->setInterceptsMouseClicks (false, true);
        convolutionPanel->setInterceptsMouseClicks (false, true);
        utilityPanel->setInterceptsMouseClicks (false, true);
        ringModPanel->setInterceptsMouseClicks (false, true);
        lfoPanel->setInterceptsMouseClicks (false, true);
        lossyPanel->setInterceptsMouseClicks (false, true);
        spectralClipperPanel->setInterceptsMouseClicks (false, true);
        eq8Panel->setInterceptsMouseClicks (false, true);
        chorusPanel->setInterceptsMouseClicks (false, true);
        eq3Panel->setInterceptsMouseClicks (false, true);
        multibandConvolutionPanel->setInterceptsMouseClicks (false, true);
        threeOscPanel->setInterceptsMouseClicks (false, true);
        adsrPanel->setInterceptsMouseClicks (false, true);
        envelopePanel->setInterceptsMouseClicks (false, true);
        multipassPanel->setInterceptsMouseClicks (false, true);
        lfoTablePanel->setInterceptsMouseClicks (false, true);

        letDirectLabelsPassThrough (*wavetableSynthPanel);
        letDirectLabelsPassThrough (*waveshaperPanel);
        letDirectLabelsPassThrough (*filterPanel);
        letDirectLabelsPassThrough (*nonlinearFilterPanel);
        letDirectLabelsPassThrough (*mackityPanel);
        letDirectLabelsPassThrough (*shimmerReverbPanel);
        letDirectLabelsPassThrough (*delayPanel);
        letDirectLabelsPassThrough (*compressorPanel);
        letDirectLabelsPassThrough (*limiterPanel);
        letDirectLabelsPassThrough (*granularPanel);
        letDirectLabelsPassThrough (*samplerPanel);
        letDirectLabelsPassThrough (*convolutionPanel);
        letDirectLabelsPassThrough (*utilityPanel);
        letDirectLabelsPassThrough (*ringModPanel);
        letDirectLabelsPassThrough (*lfoPanel);
        letDirectLabelsPassThrough (*lossyPanel);
        letDirectLabelsPassThrough (*spectralClipperPanel);
        letDirectLabelsPassThrough (*eq8Panel);
        letDirectLabelsPassThrough (*chorusPanel);
        letDirectLabelsPassThrough (*eq3Panel);
        letDirectLabelsPassThrough (*multibandConvolutionPanel);
        letDirectLabelsPassThrough (*threeOscPanel);
        letDirectLabelsPassThrough (*adsrPanel);
        letDirectLabelsPassThrough (*envelopePanel);
        letDirectLabelsPassThrough (*multipassPanel);
        letDirectLabelsPassThrough (*lfoTablePanel);
        addAndMakeVisible (*waveshaperPanel);
        addAndMakeVisible (*filterPanel);
        addAndMakeVisible (*nonlinearFilterPanel);
        addAndMakeVisible (*mackityPanel);
        addAndMakeVisible (*shimmerReverbPanel);
        addAndMakeVisible (*delayPanel);
        addAndMakeVisible (*compressorPanel);
        addAndMakeVisible (*limiterPanel);
        addAndMakeVisible (*granularPanel);
        addAndMakeVisible (*samplerPanel);
        addAndMakeVisible (*convolutionPanel);
        addAndMakeVisible (*utilityPanel);
        addAndMakeVisible (*ringModPanel);
        addAndMakeVisible (*lfoPanel);
        addAndMakeVisible (*lossyPanel);
        addAndMakeVisible (*spectralClipperPanel);
        addAndMakeVisible (*eq8Panel);
        addAndMakeVisible (*chorusPanel);
        addAndMakeVisible (*eq3Panel);
        addAndMakeVisible (*multibandConvolutionPanel);
        addAndMakeVisible (*threeOscPanel);
        addAndMakeVisible (*wavetableSynthPanel);
        addAndMakeVisible (*adsrPanel);
        addAndMakeVisible (*envelopePanel);
        addAndMakeVisible (*multipassPanel);
        addAndMakeVisible (*lfoTablePanel);

        typeBox.onChange = [this]
        {
            updateVisiblePanel();
            resized();
        };
        updateVisiblePanel();
    }

    void NodeComponent::mouseDown (const juce::MouseEvent& e)
    {
        if (onNodeGrabbed) onNodeGrabbed (slotIndex, e);
    }

    void NodeComponent::mouseDrag (const juce::MouseEvent& e)
    {
        if (onNodeDragged) onNodeDragged (slotIndex, e);
    }

    void NodeComponent::mouseUp (const juce::MouseEvent& e)
    {
        if (onNodeReleased) onNodeReleased (slotIndex, e);
    }

    juce::String NodeComponent::getCurrentModuleParameterPrefix() const
    {
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);
        const juce::String slotPrefix = "slot" + juce::String (slotIndex) + "_";

        switch (type)
        {
            case ModuleType::waveshaper:             return slotPrefix + "waveshaper_";
            case ModuleType::filter:                 return slotPrefix + "filter_";
            case ModuleType::delay:                  return slotPrefix + "delay_";
            case ModuleType::convolution:            return slotPrefix + "convolution_";
            case ModuleType::utility:                return slotPrefix + "utility_";
            case ModuleType::ringMod:                return slotPrefix + "ringMod_";
            case ModuleType::lfo:                    return slotPrefix + "lfo_";
            case ModuleType::lossy:                  return slotPrefix + "lossy_";
            case ModuleType::eq8:                    return slotPrefix + "eq8_";
            case ModuleType::chorus:                 return slotPrefix + "chorus_";
            case ModuleType::eq3:                    return slotPrefix + "eq3_";
            case ModuleType::multibandConvolution:   return slotPrefix + "multibandConvolution_";
            case ModuleType::threeOsc:               return slotPrefix + "threeOsc_";
            case ModuleType::envelope:               return slotPrefix + "envelope_";
            case ModuleType::adsr:                   return slotPrefix + "adsr_";
            case ModuleType::multipass:              return slotPrefix + "multipass_";
            case ModuleType::lfoTable:               return slotPrefix + "lfoTable_";
            case ModuleType::wavetableSynth:         return slotPrefix + "wavetableSynth_";
            case ModuleType::nonlinearFilter:        return slotPrefix + "nonlinear_filter_";
            case ModuleType::mackity:                return slotPrefix + "mackity_";
            case ModuleType::shimmerReverb:          return slotPrefix + "shimmer_reverb_";
            case ModuleType::spectralClipper:        return slotPrefix + "spectralClipper_";
            case ModuleType::compressor:              return slotPrefix + "compressor_";
            case ModuleType::limiter:                 return slotPrefix + "limiter_";
            case ModuleType::sampler:                 return slotPrefix + "sampler_";
            case ModuleType::granular:                return slotPrefix + "granular_";
            case ModuleType::none:
            case ModuleType::input:
            case ModuleType::output:
            case ModuleType::dynamics: // retired -- see ModuleType's own comment
                break;
        }

        return {};
    }

    void NodeComponent::randomizeCurrentModuleParameters()
    {
        const auto prefix = getCurrentModuleParameterPrefix();
        if (prefix.isEmpty())
            return;

        auto& random = juce::Random::getSystemRandom();
        for (auto* param : apvts.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (ranged == nullptr || ! ranged->paramID.startsWith (prefix))
                continue;

            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (random.nextFloat());
            ranged->endChangeGesture();
        }
    }

    void NodeComponent::updateVisiblePanel()
    {
        const auto type = static_cast<ModuleType> (typeBox.getSelectedId() - 1);
        const bool showPanel = ! isFoldedFlag;
        randomizeButton.setVisible (type != ModuleType::none && type != ModuleType::input && type != ModuleType::output);
        waveshaperPanel->setVisible (showPanel && type == ModuleType::waveshaper);
        filterPanel->setVisible (showPanel && type == ModuleType::filter);
        nonlinearFilterPanel->setVisible (showPanel && type == ModuleType::nonlinearFilter);
        mackityPanel->setVisible (showPanel && type == ModuleType::mackity);
        shimmerReverbPanel->setVisible (showPanel && type == ModuleType::shimmerReverb);
        delayPanel->setVisible (showPanel && type == ModuleType::delay);
        compressorPanel->setVisible (showPanel && type == ModuleType::compressor);
        limiterPanel->setVisible (showPanel && type == ModuleType::limiter);
        granularPanel->setVisible (showPanel && type == ModuleType::granular);
        samplerPanel->setVisible (showPanel && type == ModuleType::sampler);
        convolutionPanel->setVisible (showPanel && type == ModuleType::convolution);
        utilityPanel->setVisible (showPanel && type == ModuleType::utility);
        ringModPanel->setVisible (showPanel && type == ModuleType::ringMod);
        lfoPanel->setVisible (showPanel && type == ModuleType::lfo);
        lossyPanel->setVisible (showPanel && type == ModuleType::lossy);
        spectralClipperPanel->setVisible (showPanel && type == ModuleType::spectralClipper);
        eq8Panel->setVisible (showPanel && type == ModuleType::eq8);
        chorusPanel->setVisible (showPanel && type == ModuleType::chorus);
        eq3Panel->setVisible (showPanel && type == ModuleType::eq3);
        multibandConvolutionPanel->setVisible (showPanel && type == ModuleType::multibandConvolution);
        threeOscPanel->setVisible (showPanel && type == ModuleType::threeOsc);
        wavetableSynthPanel->setVisible (showPanel && type == ModuleType::wavetableSynth);
        adsrPanel->setVisible (showPanel && type == ModuleType::adsr);
        envelopePanel->setVisible (showPanel && type == ModuleType::envelope);
        multipassPanel->setVisible (showPanel && type == ModuleType::multipass);
        lfoTablePanel->setVisible (showPanel && type == ModuleType::lfoTable);
    }

    void NodeComponent::setFolded (bool shouldBeFolded)
    {
        if (isFoldedFlag == shouldBeFolded)
            return;

        isFoldedFlag = shouldBeFolded;
        foldButton.setButtonText (isFoldedFlag ? ">" : "v");
        updateVisiblePanel();
        resized();
        repaint();
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
            case ModuleType::filter:      contentHeight = 366; break; // response(92)+gap+2 knob rows+gap+bottomRow(44)
            case ModuleType::nonlinearFilter:
                contentHeight = 366; break; // response(92)+gap+2 knob rows+gap+bottomRow(44)
            case ModuleType::mackity:     contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::shimmerReverb:
                contentHeight = 374; break; // 3 knob rows + selector row
            case ModuleType::delay:       contentHeight = 248; break; // knobRow(106) + gap(6) + filterRow(106) + gap(6) + bottomRow(24)
            case ModuleType::compressor:  contentHeight = 268; break; // topRow(106) + gap(6) + bottomRow(106) + gap(6) + detectionRow(44)
            case ModuleType::limiter:     contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::granular:    contentHeight = 306; break; // preview(74)+gap+2 knob rows(106 each)+gap+freeze row(24)
            case ModuleType::sampler:
                contentHeight = 460; break; // zoneStrip(54)+gap+waveform(60)+gap+zoneRow(7-col,106)+gap+loopRow(4-col,106)+gap(10)+globalRow(8-col,106)
            case ModuleType::convolution: contentHeight = 324; break; // irRow(24)+gap+waveform(70)+gap+2 knob rows(106 each)
            case ModuleType::utility:     contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::ringMod:     contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::lfo:         contentHeight = 300; break; // curve editor + knob row + selector/sync row
            case ModuleType::lossy:       contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::spectralClipper: contentHeight = 156; break; // knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::eq8:
                contentHeight = 394; break; // curveEditor(140)+gap(6)+selectRow(24)+gap(6)+knobRow(106)+gap(6)+knobRow(106)
            case ModuleType::chorus:      contentHeight = 268; break; // knobRow(106) + gap(6) + knobRow(106) + gap(6) + bottomRow(44)
            case ModuleType::eq3:         contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::multibandConvolution:
                contentHeight = 308; break; // splitBar(50)+gap(10)+irRow(24)+gap(6)+knobRow(106)+gap(6)+knobRow(106) -- one shared knob set, retargeted per selected band
            case ModuleType::threeOsc:
                contentHeight = 714; break; // 3x[waveformRow(24)+gap(6)+knobRow(106)+gap(10)] + envRow(106)+gap(6)+fmRow(106)
                                             // + gap(6)+monoRow(24)+gap(4)+glideTimeRow(24)
            case ModuleType::wavetableSynth:
                contentHeight = 568; break; // preview/browser + oscillator row + unison/spread algorithm row + voice/output rows
            case ModuleType::adsr:        contentHeight = 106; break; // knobRow(106), no bottom row
            case ModuleType::envelope:
                contentHeight = 276; break; // editor(160) + gap(10) + knobRow(106)
            case ModuleType::multipass:
                contentHeight = 192; break; // splitBar(76, including frequency axis) + gap(10) + knobRow(106)
            case ModuleType::lfoTable:
                contentHeight = 398; break; // preview(120)+gap+picker(24)+gap+2 knob rows(106 each)+bottom row(24)
            case ModuleType::input:
            case ModuleType::output:      contentHeight = 80;  break; // just the oscilloscope
            case ModuleType::none:
            default:                      contentHeight = 0;   break;
        }

        return header + headerGap + contentHeight + padding;
    }

    int NodeComponent::getPreferredWidth() const
    {
        if (isSamplerType())
            return 900; // wide enough for the zone strip's 7/8-column knob rows without cramping
        return (isInputType() || isOutputType()) ? 150 : 380;
    }

    int NodeComponent::getMinimumWidth() const
    {
        if (isWavetableSynthType())
            return 360;
        if (isSamplerType())
            return 640;
        return (isInputType() || isOutputType()) ? 120 : 240;
    }

    int NodeComponent::getMinimumExpandedHeight() const
    {
        if (isWavetableSynthType())
            return 600;
        if (isSamplerType())
            return 300;
        return (isInputType() || isOutputType()) ? 72 : 118;
    }

    juce::Colour NodeComponent::outputPortColour (int portIndex) const
    {
        if (! isMultipassType())
            return Palette::accent;

        switch (portIndex)
        {
            case 0:  return juce::Colour (0xff4fc3f7); // Low
            case 1:  return juce::Colour (0xffffd166); // Mid
            case 2:  return juce::Colour (0xffff6b6b); // High
            default: return Palette::accent;
        }
    }

    juce::Point<int> NodeComponent::getInputConnectorPosition (int portIndex) const
    {
        if (isWavetableSynthType())
        {
            // WT Synth's audio input is FM, so keep it visually attached to the wavetable
            // preview instead of the node centre where it collides with the lower controls.
            return { 0, contentAreaOrigin.y + 70 };
        }
        return { 0, getHeight() * (portIndex + 1) / (kMaxPortsPerSide + 1) };
    }

    juce::Point<int> NodeComponent::getOutputConnectorPosition (int portIndex) const
    {
        if (isWavetableSynthType())
        {
            return { getWidth(), contentAreaOrigin.y + 70 };
        }
        return { getWidth(), getHeight() * (portIndex + 1) / (kMaxPortsPerSide + 1) };
    }

    bool NodeComponent::isModulationSourceType() const
    {
        return GGrid::isModulationSourceType (static_cast<ModuleType> (typeBox.getSelectedId() - 1));
    }

    bool NodeComponent::isInputType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::input;
    }

    bool NodeComponent::isOutputType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::output;
    }

    bool NodeComponent::isThreeOscType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::threeOsc;
    }

    bool NodeComponent::isWavetableSynthType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::wavetableSynth;
    }

    bool NodeComponent::isMultipassType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::multipass;
    }

    bool NodeComponent::isSamplerType() const
    {
        return static_cast<ModuleType> (typeBox.getSelectedId() - 1) == ModuleType::sampler;
    }

    bool NodeComponent::hasFourOutputBuses() const
    {
        return false;
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
            case ModuleType::nonlinearFilter: return nonlinearFilterPanel->getModTargetCount();
            case ModuleType::mackity:     return mackityPanel->getModTargetCount();
            case ModuleType::shimmerReverb: return shimmerReverbPanel->getModTargetCount();
            case ModuleType::delay:       return delayPanel->getModTargetCount();
            case ModuleType::compressor:  return compressorPanel->getModTargetCount();
            case ModuleType::limiter:     return limiterPanel->getModTargetCount();
            case ModuleType::granular:    return granularPanel->getModTargetCount();
            case ModuleType::sampler:     return samplerPanel->getModTargetCount();
            case ModuleType::convolution: return convolutionPanel->getModTargetCount();
            case ModuleType::utility:     return utilityPanel->getModTargetCount();
            case ModuleType::ringMod:     return ringModPanel->getModTargetCount();
            case ModuleType::lossy:       return lossyPanel->getModTargetCount();
            case ModuleType::spectralClipper: return spectralClipperPanel->getModTargetCount();
            case ModuleType::eq8:         return eq8Panel->getModTargetCount();
            case ModuleType::chorus:      return chorusPanel->getModTargetCount();
            case ModuleType::eq3:         return eq3Panel->getModTargetCount();
            case ModuleType::multibandConvolution: return multibandConvolutionPanel->getModTargetCount();
            case ModuleType::threeOsc:    return threeOscPanel->getModTargetCount();
            case ModuleType::wavetableSynth: return wavetableSynthPanel->getModTargetCount();
            case ModuleType::multipass:   return multipassPanel->getModTargetCount();
            default:                      return 0;
        }
    }

    juce::String NodeComponent::getModTargetParamId (int index) const
    {
        switch (static_cast<ModuleType> (typeBox.getSelectedId() - 1))
        {
            case ModuleType::waveshaper:  return waveshaperPanel->getModTarget (index).paramId;
            case ModuleType::filter:      return filterPanel->getModTarget (index).paramId;
            case ModuleType::nonlinearFilter: return nonlinearFilterPanel->getModTarget (index).paramId;
            case ModuleType::mackity:     return mackityPanel->getModTarget (index).paramId;
            case ModuleType::shimmerReverb: return shimmerReverbPanel->getModTarget (index).paramId;
            case ModuleType::delay:       return delayPanel->getModTarget (index).paramId;
            case ModuleType::compressor:  return compressorPanel->getModTarget (index).paramId;
            case ModuleType::limiter:     return limiterPanel->getModTarget (index).paramId;
            case ModuleType::granular:    return granularPanel->getModTarget (index).paramId;
            case ModuleType::sampler:     return samplerPanel->getModTarget (index).paramId;
            case ModuleType::convolution: return convolutionPanel->getModTarget (index).paramId;
            case ModuleType::utility:     return utilityPanel->getModTarget (index).paramId;
            case ModuleType::ringMod:     return ringModPanel->getModTarget (index).paramId;
            case ModuleType::lossy:       return lossyPanel->getModTarget (index).paramId;
            case ModuleType::spectralClipper: return spectralClipperPanel->getModTarget (index).paramId;
            case ModuleType::eq8:         return eq8Panel->getModTarget (index).paramId;
            case ModuleType::chorus:      return chorusPanel->getModTarget (index).paramId;
            case ModuleType::eq3:         return eq3Panel->getModTarget (index).paramId;
            case ModuleType::multibandConvolution: return multibandConvolutionPanel->getModTarget (index).paramId;
            case ModuleType::threeOsc:    return threeOscPanel->getModTarget (index).paramId;
            case ModuleType::wavetableSynth: return wavetableSynthPanel->getModTarget (index).paramId;
            case ModuleType::multipass:   return multipassPanel->getModTarget (index).paramId;
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
            case ModuleType::nonlinearFilter: slider = nonlinearFilterPanel->getModTarget (index).slider; break;
            case ModuleType::mackity:     slider = mackityPanel->getModTarget (index).slider; break;
            case ModuleType::shimmerReverb: slider = shimmerReverbPanel->getModTarget (index).slider; break;
            case ModuleType::delay:       slider = delayPanel->getModTarget (index).slider; break;
            case ModuleType::compressor:  slider = compressorPanel->getModTarget (index).slider; break;
            case ModuleType::limiter:     slider = limiterPanel->getModTarget (index).slider; break;
            case ModuleType::granular:    slider = granularPanel->getModTarget (index).slider; break;
            case ModuleType::sampler:     slider = samplerPanel->getModTarget (index).slider; break;
            case ModuleType::convolution: slider = convolutionPanel->getModTarget (index).slider; break;
            case ModuleType::utility:     slider = utilityPanel->getModTarget (index).slider; break;
            case ModuleType::ringMod:     slider = ringModPanel->getModTarget (index).slider; break;
            case ModuleType::lossy:       slider = lossyPanel->getModTarget (index).slider; break;
            case ModuleType::spectralClipper: slider = spectralClipperPanel->getModTarget (index).slider; break;
            case ModuleType::eq8:         slider = eq8Panel->getModTarget (index).slider; break;
            case ModuleType::chorus:      slider = chorusPanel->getModTarget (index).slider; break;
            case ModuleType::eq3:         slider = eq3Panel->getModTarget (index).slider; break;
            case ModuleType::multibandConvolution: slider = multibandConvolutionPanel->getModTarget (index).slider; break;
            case ModuleType::threeOsc:    slider = threeOscPanel->getModTarget (index).slider; break;
            case ModuleType::wavetableSynth: slider = wavetableSynthPanel->getModTarget (index).slider; break;
            case ModuleType::multipass:   slider = multipassPanel->getModTarget (index).slider; break;
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
        if (slider->getSliderStyle() == juce::Slider::LinearHorizontal)
            return contentAreaOrigin + juce::Point<int> (bounds.getX() + 10, bounds.getY() - 9);

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
        // interactive) output nub, never from here. Modulation-source nodes (LFO/Envelope/ADSR)
        // have no audio ports at all (they aren't part of the audio graph -- see
        // isModulationSourceType()); Input/ThreeOsc nodes have no input ports at all (they're both
        // sources, not destinations -- see hasNoInputPorts()).
        if (! isModulationSourceType() && ! hasNoInputPorts())
        {
            g.setColour (Palette::accent);
            const int inputPortCount = isWavetableSynthType() ? 1 : kMaxPortsPerSide;
            for (int port = 0; port < inputPortCount; ++port)
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (getInputConnectorPosition (port).toFloat()));
        }

        const int modTargetCount = isFoldedFlag ? 0 : getModTargetCount();
        if (modTargetCount > 0)
        {
            g.setColour (Palette::modAccent);
            for (int i = 0; i < modTargetCount; ++i)
                g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (getModTargetPosition (i).toFloat()));
        }

        if (isMultipassType())
        {
            const int portCount = kNumMultipassBands;
            for (int port = 0; port < portCount; ++port)
            {
                const auto pos = getOutputConnectorPosition (port).toFloat();
                const auto colour = outputPortColour (port);
                g.setColour (colour.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> ((float) getWidth() - 5.0f, pos.y - 15.0f, 4.0f, 30.0f));
                g.setColour (colour.withAlpha (0.34f));
                g.drawLine ((float) getWidth() - 22.0f, pos.y, (float) getWidth() - 5.0f, pos.y, 1.0f);
            }
        }
    }

    void NodeComponent::paintOverChildren (juce::Graphics& g)
    {
        if (isFoldedFlag)
            return;

        if (! isModulationSourceType() && ! hasNoInputPorts() && isWavetableSynthType())
        {
            const auto inputPos = getInputConnectorPosition (0).toFloat();
            g.setColour (Palette::accent);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (inputPos));

            const auto labelBounds = juce::Rectangle<float> (10.0f, inputPos.y - 10.0f, 54.0f, 20.0f);
            g.setColour (Palette::bg.withAlpha (0.82f));
            g.fillRoundedRectangle (labelBounds, 4.0f);
            g.setColour (Palette::bright);
            g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
            g.drawText ("FM In", labelBounds.toNearestInt(), juce::Justification::centred);
        }
    }

    void NodeComponent::resized()
    {
        constexpr int headerHeight = 28;
        constexpr int padding = 8;

        titleBar.setBounds (0, 0, getWidth(), headerHeight);

        const bool isIOType = isInputType() || isOutputType();

        auto header = getLocalBounds().removeFromTop (headerHeight).reduced (padding, 4);
        const auto selectedTypeId = typeBox.getSelectedId();
        const auto currentType = selectedTypeId > 0 ? static_cast<ModuleType> (selectedTypeId - 1) : ModuleType::none;
        const bool showRandomize = currentType != ModuleType::none && ! isIOType;
        randomizeButton.setVisible (showRandomize);

        // The compact Input/Output box has no room for a title label alongside a usable type
        // dropdown -- every other type keeps it (matches "Slot N" everywhere else in the rack).
        titleLabel.setVisible (! isIOType);
        if (! isIOType)
            titleLabel.setBounds (header.removeFromLeft (50));

        deleteButton.setBounds (header.removeFromRight (24));
        header.removeFromRight (4);

        if (showRandomize)
        {
            randomizeButton.setBounds (header.removeFromRight (24));
            header.removeFromRight (4);
        }

        foldButton.setBounds (header.removeFromRight (24));
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
        nonlinearFilterPanel->setBounds (contentArea);
        mackityPanel->setBounds (contentArea);
        shimmerReverbPanel->setBounds (contentArea);
        delayPanel->setBounds (contentArea);
        compressorPanel->setBounds (contentArea);
        limiterPanel->setBounds (contentArea);
        granularPanel->setBounds (contentArea);
        samplerPanel->setBounds (contentArea);
        convolutionPanel->setBounds (contentArea);
        utilityPanel->setBounds (contentArea);
        ringModPanel->setBounds (contentArea);
        lfoPanel->setBounds (contentArea);
        lossyPanel->setBounds (contentArea);
        spectralClipperPanel->setBounds (contentArea);
        eq8Panel->setBounds (contentArea);
        chorusPanel->setBounds (contentArea);
        eq3Panel->setBounds (contentArea);
        multibandConvolutionPanel->setBounds (contentArea);
        threeOscPanel->setBounds (contentArea);
        wavetableSynthPanel->setBounds (contentArea);
        adsrPanel->setBounds (contentArea);
        envelopePanel->setBounds (contentArea);
        multipassPanel->setBounds (contentArea);
        lfoTablePanel->setBounds (contentArea);

        scope.setBounds (contentArea);
        scope.setVisible (isIOType && ! isFoldedFlag);

        const bool modSource = isModulationSourceType();
        // Output nodes have no output ports at all -- their whole purpose is collecting incoming
        // audio into the final mix, not producing any (see isOutputType()).
        const bool hideOutputPorts = modSource || isOutputType();
        // Multipass has exactly 3 meaningful output buses (Low/Mid/High) -- the 4th dot would be
        // silence if wired (see RackModule::getOutputBusBuffer's out-of-range nullptr fallback),
        // so it's hidden entirely rather than left as a footgun. WT Synth is a single-output
        // oscillator; incoming audio is FM, not a multi-bus lane system.
        const bool singleOutputOnly = isWavetableSynthType();
        const bool hideFourthOutputPort = (isMultipassType() && ! hasFourOutputBuses()) || singleOutputOnly;
        outputNub0.setVisible (! hideOutputPorts);
        outputNub1.setVisible (! hideOutputPorts && ! singleOutputOnly);
        outputNub2.setVisible (! hideOutputPorts && ! singleOutputOnly);
        outputNub3.setVisible (! hideOutputPorts && ! hideFourthOutputPort);
        modOutputNub.setVisible (modSource);

        OutputNub* outputNubs[kMaxPortsPerSide] = { &outputNub0, &outputNub1, &outputNub2, &outputNub3 };
        for (int port = 0; port < kMaxPortsPerSide; ++port)
        {
            const auto pos = getOutputConnectorPosition (port);
            outputNubs[port]->setBounds (pos.x - 8, pos.y - 8, 16, 16);
        }
        modOutputNub.setBounds (getWidth() - 8, getHeight() / 2 - 8, 16, 16);
        resizeHandle.setBounds (getWidth() - 22, getHeight() - 22, 22, 22);
        resizeHandle.setVisible (! isFoldedFlag);
        resizeHandle.toFront (false);
    }
}
