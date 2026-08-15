#include "ModuleControlPanels.h"
#include "GGridLookAndFeel.h"
#include "../IR/IRLibrary.h"

namespace GGrid
{
    WaveshaperControlsPanel::WaveshaperControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (driveSlider, driveLabel, "Drive");
        setupRotary (symmetrySlider, symmetryLabel, "Symmetry");
        setupRotary (foldSlider, foldLabel, "Fold");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        shapeLabel.setText ("Shape", juce::dontSendNotification);
        shapeLabel.setJustificationType (juce::Justification::centred);
        oversampleLabel.setText ("Oversample", juce::dontSendNotification);
        oversampleLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getWaveshaperShapeChoices())
            shapeBox.addItem (choice, itemId++);

        oversampleBox.addItem ("Off", 1);
        oversampleBox.addItem ("2x", 2);
        oversampleBox.addItem ("4x", 3);

        addAndMakeVisible (shapeBox);
        addAndMakeVisible (oversampleBox);
        addAndMakeVisible (shapeLabel);
        addAndMakeVisible (oversampleLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        driveAttachment      = std::make_unique<SliderAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::drive), driveSlider);
        symmetryAttachment   = std::make_unique<SliderAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::symmetry), symmetrySlider);
        foldAttachment       = std::make_unique<SliderAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::foldAmount), foldSlider);
        mixAttachment        = std::make_unique<SliderAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::mix), mixSlider);
        outputAttachment     = std::make_unique<SliderAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::output), outputSlider);
        shapeAttachment      = std::make_unique<ComboBoxAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::shape), shapeBox);
        oversampleAttachment = std::make_unique<ComboBoxAttachment> (apvts, waveshaperParamId (slotIndex, WaveshaperParam::oversample), oversampleBox);
    }

    void WaveshaperControlsPanel::resized()
    {
        // Explicit fixed heights for each row, top-down, rather than "give the knobs 100px and
        // whatever's left to the dropdowns" -- that left the shape/oversample row only ~20px
        // tall, so those two dropdowns were rendering as unclickable slivers.
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), driveLabel, driveSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), symmetryLabel, symmetrySlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), foldLabel, foldSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);

        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);
        auto right = bottomRow.reduced (4, 0);

        shapeLabel.setBounds (left.removeFromTop (16));
        shapeBox.setBounds (left.removeFromTop (24));

        oversampleLabel.setBounds (right.removeFromTop (16));
        oversampleBox.setBounds (right.removeFromTop (24));
    }

    FilterControlsPanel::FilterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (frequencySlider, frequencyLabel, "Frequency");
        setupRotary (resonanceSlider, resonanceLabel, "Resonance");
        setupRotary (feedbackSlider, feedbackLabel, "Feedback");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        typeLabel.setText ("Type", juce::dontSendNotification);
        typeLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getFilterTypeChoices())
            typeBox.addItem (choice, itemId++);

        addAndMakeVisible (typeBox);
        addAndMakeVisible (typeLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        frequencyAttachment = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::frequency), frequencySlider);
        resonanceAttachment = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::resonance), resonanceSlider);
        feedbackAttachment  = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::feedback), feedbackSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::mix), mixSlider);
        outputAttachment    = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::output), outputSlider);
        typeAttachment      = std::make_unique<ComboBoxAttachment> (apvts, filterParamId (slotIndex, FilterParam::type), typeBox);
    }

    void FilterControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), frequencyLabel, frequencySlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), resonanceLabel, resonanceSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), feedbackLabel, feedbackSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);

        typeLabel.setBounds (left.removeFromTop (16));
        typeBox.setBounds (left.removeFromTop (24));
    }

    DelayControlsPanel::DelayControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (timeSlider, timeLabel, "Time");
        setupRotary (feedbackSlider, feedbackLabel, "Feedback");
        setupRotary (saturationSlider, saturationLabel, "Saturation");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");
        setupRotary (lowCutSlider, lowCutLabel, "Low Cut");
        setupRotary (hiCutSlider, hiCutLabel, "Hi Cut");

        addAndMakeVisible (syncButton);
        addAndMakeVisible (pingPongButton);

        for (auto& choice : getDelayDivisionChoices())
            divisionBox.addItem (choice, divisionBox.getNumItems() + 1);
        addAndMakeVisible (divisionBox);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        timeAttachment       = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::time), timeSlider);
        feedbackAttachment   = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::feedback), feedbackSlider);
        saturationAttachment = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::saturation), saturationSlider);
        mixAttachment        = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::mix), mixSlider);
        outputAttachment     = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::output), outputSlider);
        lowCutAttachment     = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::lowCut), lowCutSlider);
        hiCutAttachment      = std::make_unique<SliderAttachment> (apvts, delayParamId (slotIndex, DelayParam::hiCut), hiCutSlider);
        syncAttachment       = std::make_unique<ButtonAttachment> (apvts, delayParamId (slotIndex, DelayParam::sync), syncButton);
        pingPongAttachment   = std::make_unique<ButtonAttachment> (apvts, delayParamId (slotIndex, DelayParam::pingPong), pingPongButton);
        divisionAttachment   = std::make_unique<ComboBoxAttachment> (apvts, delayParamId (slotIndex, DelayParam::division), divisionBox);
    }

    void DelayControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), timeLabel, timeSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), feedbackLabel, feedbackSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), saturationLabel, saturationSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto filterRow = area.removeFromTop (96);
        const int filterKnobWidth = filterRow.getWidth() / 2;
        layoutKnob (filterRow.removeFromLeft (filterKnobWidth), lowCutLabel, lowCutSlider);
        layoutKnob (filterRow, hiCutLabel, hiCutSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (24);
        syncButton.setBounds (bottomRow.removeFromLeft (60));
        bottomRow.removeFromLeft (4);
        divisionBox.setBounds (bottomRow.removeFromLeft (80));
        bottomRow.removeFromLeft (8);
        pingPongButton.setBounds (bottomRow);
    }

    DynamicsControlsPanel::DynamicsControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (thresholdSlider, thresholdLabel, "Threshold");
        setupRotary (ratioSlider, ratioLabel, "Ratio");
        setupRotary (attackSlider, attackLabel, "Attack");
        setupRotary (releaseSlider, releaseLabel, "Release");
        setupRotary (makeupSlider, makeupLabel, "Makeup");
        setupRotary (mixSlider, mixLabel, "Mix");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        thresholdAttachment = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::threshold), thresholdSlider);
        ratioAttachment     = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::ratio), ratioSlider);
        attackAttachment    = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::attack), attackSlider);
        releaseAttachment   = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::release), releaseSlider);
        makeupAttachment    = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::makeup), makeupSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, dynamicsParamId (slotIndex, DynamicsParam::mix), mixSlider);
    }

    void DynamicsControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        auto topRow = area.removeFromTop (96);
        const int topKnobWidth = topRow.getWidth() / 3;
        layoutKnob (topRow.removeFromLeft (topKnobWidth), thresholdLabel, thresholdSlider);
        layoutKnob (topRow.removeFromLeft (topKnobWidth), ratioLabel, ratioSlider);
        layoutKnob (topRow, attackLabel, attackSlider);

        area.removeFromTop (6);

        auto bottomRow = area.removeFromTop (96);
        const int bottomKnobWidth = bottomRow.getWidth() / 3;
        layoutKnob (bottomRow.removeFromLeft (bottomKnobWidth), releaseLabel, releaseSlider);
        layoutKnob (bottomRow.removeFromLeft (bottomKnobWidth), makeupLabel, makeupSlider);
        layoutKnob (bottomRow, mixLabel, mixSlider);
    }

    ConvolutionControlsPanel::ConvolutionControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot)
        : waveform (rackSlot)
    {
        int itemId = 1;
        juce::String lastCategory;
        for (auto& entry : IRLibrary::getCatalog())
        {
            if (entry.category != lastCategory)
            {
                irBox.addSectionHeading (entry.category);
                lastCategory = entry.category;
            }
            irBox.addItem (entry.displayName, itemId++);
        }
        addAndMakeVisible (irBox);

        irIndexParam = dynamic_cast<juce::AudioParameterChoice*> (
            apvts.getParameter (convolutionParamId (slotIndex, ConvolutionParam::irIndex)));

        prevIrButton.onClick = [this] { stepIr (-1); };
        nextIrButton.onClick = [this] { stepIr (1); };
        addAndMakeVisible (prevIrButton);
        addAndMakeVisible (nextIrButton);

        openFolderButton.onClick = [] { IRLibrary::getCustomIRDirectory().revealToUser(); };
        addAndMakeVisible (openFolderButton);

        addAndMakeVisible (waveform);

        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (toneSlider, toneLabel, "Tone");
        setupRotary (fadeInSlider, fadeInLabel, "Fade In");
        setupRotary (fadeOutSlider, fadeOutLabel, "Fade Out");
        setupRotary (stretchSlider, stretchLabel, "Stretch");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        irAttachment      = std::make_unique<ComboBoxAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::irIndex), irBox);
        toneAttachment    = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::tone), toneSlider);
        fadeInAttachment  = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::fadeIn), fadeInSlider);
        fadeOutAttachment = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::fadeOut), fadeOutSlider);
        stretchAttachment = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::stretch), stretchSlider);
        mixAttachment     = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::mix), mixSlider);
        outputAttachment  = std::make_unique<SliderAttachment> (apvts, convolutionParamId (slotIndex, ConvolutionParam::output), outputSlider);
    }

    void ConvolutionControlsPanel::stepIr (int direction)
    {
        if (irIndexParam == nullptr)
            return;

        const int numChoices = irIndexParam->choices.size();
        const int newIndex = juce::jlimit (0, numChoices - 1, irIndexParam->getIndex() + direction);
        *irIndexParam = newIndex;
    }

    void ConvolutionControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto topRow = area.removeFromTop (24);
        openFolderButton.setBounds (topRow.removeFromRight (28));
        topRow.removeFromRight (4);
        nextIrButton.setBounds (topRow.removeFromRight (24));
        topRow.removeFromRight (2);
        prevIrButton.setBounds (topRow.removeFromLeft (24));
        topRow.removeFromLeft (2);
        irBox.setBounds (topRow);

        area.removeFromTop (6);
        waveform.setBounds (area.removeFromTop (70));
        area.removeFromTop (6);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        auto topKnobRow = area.removeFromTop (96);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), toneLabel, toneSlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), fadeInLabel, fadeInSlider);
        layoutKnob (topKnobRow, fadeOutLabel, fadeOutSlider);

        area.removeFromTop (6);

        auto bottomKnobRow = area.removeFromTop (96);
        const int bottomKnobWidth = bottomKnobRow.getWidth() / 3;
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), stretchLabel, stretchSlider);
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), mixLabel, mixSlider);
        layoutKnob (bottomKnobRow, outputLabel, outputSlider);
    }

    UtilityControlsPanel::UtilityControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (gainSlider, gainLabel, "Gain");
        setupRotary (panSlider, panLabel, "Pan");
        setupRotary (widthSlider, widthLabel, "Width");

        addAndMakeVisible (monoButton);
        addAndMakeVisible (phaseInvertLButton);
        addAndMakeVisible (phaseInvertRButton);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

        gainAttachment           = std::make_unique<SliderAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::gain), gainSlider);
        panAttachment            = std::make_unique<SliderAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::pan), panSlider);
        widthAttachment          = std::make_unique<SliderAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::width), widthSlider);
        monoAttachment           = std::make_unique<ButtonAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::mono), monoButton);
        phaseInvertLAttachment   = std::make_unique<ButtonAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::phaseInvertL), phaseInvertLButton);
        phaseInvertRAttachment   = std::make_unique<ButtonAttachment> (apvts, utilityParamId (slotIndex, UtilityParam::phaseInvertR), phaseInvertRButton);
    }

    void UtilityControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 3;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), gainLabel, gainSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), panLabel, panSlider);
        layoutKnob (knobRow, widthLabel, widthSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        const int buttonWidth = bottomRow.getWidth() / 3;
        monoButton.setBounds (bottomRow.removeFromLeft (buttonWidth).reduced (2, 10));
        phaseInvertLButton.setBounds (bottomRow.removeFromLeft (buttonWidth).reduced (2, 10));
        phaseInvertRButton.setBounds (bottomRow.reduced (2, 10));
    }

    RingModControlsPanel::RingModControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (frequencySlider, frequencyLabel, "Frequency");
        setupRotary (fineSlider, fineLabel, "Fine");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        modeLabel.setText ("Mode", juce::dontSendNotification);
        modeLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getRingModModeChoices())
            modeBox.addItem (choice, itemId++);

        addAndMakeVisible (modeBox);
        addAndMakeVisible (modeLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        frequencyAttachment = std::make_unique<SliderAttachment> (apvts, ringModParamId (slotIndex, RingModParam::frequency), frequencySlider);
        fineAttachment      = std::make_unique<SliderAttachment> (apvts, ringModParamId (slotIndex, RingModParam::fine), fineSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, ringModParamId (slotIndex, RingModParam::mix), mixSlider);
        outputAttachment    = std::make_unique<SliderAttachment> (apvts, ringModParamId (slotIndex, RingModParam::output), outputSlider);
        modeAttachment      = std::make_unique<ComboBoxAttachment> (apvts, ringModParamId (slotIndex, RingModParam::mode), modeBox);
    }

    void RingModControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 4;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), frequencyLabel, frequencySlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), fineLabel, fineSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);

        modeLabel.setBounds (left.removeFromTop (16));
        modeBox.setBounds (left.removeFromTop (24));
    }

    LfoControlsPanel::LfoControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
    {
        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (rateSlider, rateLabel, "Rate");
        setupRotary (depthSlider, depthLabel, "Depth");

        int itemId = 1;
        for (auto& choice : getLfoShapeChoices())
            shapeBox.addItem (choice, itemId++);
        addAndMakeVisible (shapeBox);

        addAndMakeVisible (syncButton);

        for (auto& choice : getDelayDivisionChoices())
            divisionBox.addItem (choice, divisionBox.getNumItems() + 1);
        addAndMakeVisible (divisionBox);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        rateAttachment     = std::make_unique<SliderAttachment> (apvts, lfoParamId (slotIndex, LfoParam::rateHz), rateSlider);
        depthAttachment    = std::make_unique<SliderAttachment> (apvts, lfoParamId (slotIndex, LfoParam::depth), depthSlider);
        shapeAttachment    = std::make_unique<ComboBoxAttachment> (apvts, lfoParamId (slotIndex, LfoParam::shape), shapeBox);
        divisionAttachment = std::make_unique<ComboBoxAttachment> (apvts, lfoParamId (slotIndex, LfoParam::division), divisionBox);
        syncAttachment     = std::make_unique<ButtonAttachment> (apvts, lfoParamId (slotIndex, LfoParam::rateMode), syncButton);
    }

    void LfoControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (96);
        const int knobWidth = knobRow.getWidth() / 2;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), rateLabel, rateSlider);
        layoutKnob (knobRow, depthLabel, depthSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto top = bottomRow.removeFromTop (20);
        shapeBox.setBounds (top);

        bottomRow.removeFromTop (2);
        syncButton.setBounds (bottomRow.removeFromLeft (60));
        bottomRow.removeFromLeft (4);
        divisionBox.setBounds (bottomRow);
    }
}
