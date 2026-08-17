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

        modTargets = {
            { waveshaperParamId (slotIndex, WaveshaperParam::drive),      "Drive",      &driveSlider },
            { waveshaperParamId (slotIndex, WaveshaperParam::symmetry),   "Symmetry",   &symmetrySlider },
            { waveshaperParamId (slotIndex, WaveshaperParam::foldAmount), "Fold",       &foldSlider },
            { waveshaperParamId (slotIndex, WaveshaperParam::mix),        "Mix",        &mixSlider },
            { waveshaperParamId (slotIndex, WaveshaperParam::output),     "Output",     &outputSlider },
        };

        startTimerHz (15);
    }

    WaveshaperControlsPanel::~WaveshaperControlsPanel()
    {
        stopTimer();
    }

    // Mirrors WaveshaperModule::shapeSample exactly (kept in sync by hand -- the DSP and this
    // preview are two different targets, no shared header for one formula isn't worth the
    // indirection). Deliberately doesn't apply Drive's gain -- this shows the shape *family*
    // (Shape/Symmetry/Fold) over its natural [-1, 1] domain, not how hard the signal is being
    // pushed into it.
    float WaveshaperControlsPanel::shapeSample (float x, int shapeIndex, float symmetry, float foldAmount) const
    {
        x += symmetry * 0.5f;

        float y;
        switch (shapeIndex)
        {
            case 0: y = juce::jlimit (-1.0f, 1.0f, x); break; // Hard Clip
            case 1: y = std::tanh (x); break; // Soft Clip (tanh)
            case 2: // Soft Clip (cubic)
            {
                const float xc = juce::jlimit (-1.5f, 1.5f, x);
                y = juce::jlimit (-1.0f, 1.0f, (xc - (xc * xc * xc) / 3.0f) * 1.5f);
                break;
            }
            case 3: // Foldback Wavefolder
            {
                const float drive = 1.0f + foldAmount * 7.0f;
                const float z = x * drive * 0.25f;
                const float frac = z - std::floor (z + 0.5f);
                y = 4.0f * std::abs (frac) - 1.0f;
                break;
            }
            case 4: // Sine Fold
            {
                const float drive = 1.0f + foldAmount * 8.0f;
                y = std::sin (x * drive * juce::MathConstants<float>::halfPi);
                break;
            }
            case 5: y = juce::jlimit (-1.0f, 1.0f, std::abs (x) * 2.0f - 1.0f); break; // Rectify/Asymmetric
            default: y = x; break;
        }

        return y - symmetry * 0.25f;
    }

    void WaveshaperControlsPanel::paint (juce::Graphics& g)
    {
        auto bounds = curveArea.toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        if (curveArea.isEmpty())
            return;

        const int shapeIndex = shapeBox.getSelectedId() - 1;
        const float symmetry = (float) symmetrySlider.getValue();
        const float foldAmount = (float) foldSlider.getValue();

        const int width = curveArea.getWidth();
        const float midY = bounds.getY() + bounds.getHeight() * 0.5f;
        const float halfH = bounds.getHeight() * 0.45f;

        g.setColour (Palette::dimmer.withAlpha (0.6f));
        g.drawHorizontalLine ((int) midY, bounds.getX(), bounds.getRight());
        g.drawVerticalLine ((int) (bounds.getX() + bounds.getWidth() * 0.5f), bounds.getY(), bounds.getBottom());

        juce::Path path;
        for (int x = 0; x < width; ++x)
        {
            const float xNorm = (float) x / (float) juce::jmax (1, width - 1) * 2.0f - 1.0f;
            const float y = shapeSample (xNorm, shapeIndex, symmetry, foldAmount);
            const float px = bounds.getX() + (float) x;
            const float py = juce::jlimit (bounds.getY(), bounds.getBottom(), midY - y * halfH);

            if (x == 0) path.startNewSubPath (px, py);
            else path.lineTo (px, py);
        }

        g.setColour (Palette::accent);
        g.strokePath (path, juce::PathStrokeType (1.5f));
    }

    void WaveshaperControlsPanel::resized()
    {
        // Explicit fixed heights for each row, top-down, rather than "give the knobs 100px and
        // whatever's left to the dropdowns" -- that left the shape/oversample row only ~20px
        // tall, so those two dropdowns were rendering as unclickable slivers.
        auto area = getLocalBounds().reduced (4);

        curveArea = area.removeFromTop (60);
        area.removeFromTop (6);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
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

        modTargets = {
            { filterParamId (slotIndex, FilterParam::frequency), "Frequency", &frequencySlider },
            { filterParamId (slotIndex, FilterParam::resonance), "Resonance", &resonanceSlider },
            { filterParamId (slotIndex, FilterParam::feedback),  "Feedback",  &feedbackSlider },
            { filterParamId (slotIndex, FilterParam::mix),       "Mix",       &mixSlider },
            { filterParamId (slotIndex, FilterParam::output),    "Output",    &outputSlider },
        };
    }

    void FilterControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
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

        modTargets = {
            { delayParamId (slotIndex, DelayParam::time),       "Time",       &timeSlider },
            { delayParamId (slotIndex, DelayParam::feedback),   "Feedback",   &feedbackSlider },
            { delayParamId (slotIndex, DelayParam::saturation), "Saturation", &saturationSlider },
            { delayParamId (slotIndex, DelayParam::lowCut),     "Low Cut",    &lowCutSlider },
            { delayParamId (slotIndex, DelayParam::hiCut),      "Hi Cut",     &hiCutSlider },
            { delayParamId (slotIndex, DelayParam::mix),        "Mix",        &mixSlider },
            { delayParamId (slotIndex, DelayParam::output),     "Output",     &outputSlider },
        };
    }

    void DelayControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), timeLabel, timeSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), feedbackLabel, feedbackSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), saturationLabel, saturationSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto filterRow = area.removeFromTop (106);
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

        modTargets = {
            { dynamicsParamId (slotIndex, DynamicsParam::threshold), "Threshold", &thresholdSlider },
            { dynamicsParamId (slotIndex, DynamicsParam::ratio),     "Ratio",     &ratioSlider },
            { dynamicsParamId (slotIndex, DynamicsParam::attack),    "Attack",    &attackSlider },
            { dynamicsParamId (slotIndex, DynamicsParam::release),   "Release",   &releaseSlider },
            { dynamicsParamId (slotIndex, DynamicsParam::makeup),    "Makeup",    &makeupSlider },
            { dynamicsParamId (slotIndex, DynamicsParam::mix),       "Mix",       &mixSlider },
        };
    }

    void DynamicsControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto topRow = area.removeFromTop (106);
        const int topKnobWidth = topRow.getWidth() / 3;
        layoutKnob (topRow.removeFromLeft (topKnobWidth), thresholdLabel, thresholdSlider);
        layoutKnob (topRow.removeFromLeft (topKnobWidth), ratioLabel, ratioSlider);
        layoutKnob (topRow, attackLabel, attackSlider);

        area.removeFromTop (6);

        auto bottomRow = area.removeFromTop (106);
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

        modTargets = {
            { convolutionParamId (slotIndex, ConvolutionParam::tone),     "Tone",     &toneSlider },
            { convolutionParamId (slotIndex, ConvolutionParam::fadeIn),   "Fade In",  &fadeInSlider },
            { convolutionParamId (slotIndex, ConvolutionParam::fadeOut),  "Fade Out", &fadeOutSlider },
            { convolutionParamId (slotIndex, ConvolutionParam::stretch),  "Stretch",  &stretchSlider },
            { convolutionParamId (slotIndex, ConvolutionParam::mix),      "Mix",      &mixSlider },
            { convolutionParamId (slotIndex, ConvolutionParam::output),   "Output",   &outputSlider },
        };
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
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto topKnobRow = area.removeFromTop (106);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), toneLabel, toneSlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), fadeInLabel, fadeInSlider);
        layoutKnob (topKnobRow, fadeOutLabel, fadeOutSlider);

        area.removeFromTop (6);

        auto bottomKnobRow = area.removeFromTop (106);
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

        modTargets = {
            { utilityParamId (slotIndex, UtilityParam::gain),  "Gain",  &gainSlider },
            { utilityParamId (slotIndex, UtilityParam::pan),   "Pan",   &panSlider },
            { utilityParamId (slotIndex, UtilityParam::width), "Width", &widthSlider },
        };
    }

    void UtilityControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 3;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
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

        modTargets = {
            { ringModParamId (slotIndex, RingModParam::frequency), "Frequency", &frequencySlider },
            { ringModParamId (slotIndex, RingModParam::fine),      "Fine",      &fineSlider },
            { ringModParamId (slotIndex, RingModParam::mix),       "Mix",       &mixSlider },
            { ringModParamId (slotIndex, RingModParam::output),    "Output",    &outputSlider },
        };
    }

    void RingModControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 4;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
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

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 2;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
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

    AdsrControlsPanel::AdsrControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        setupRotary (attackSlider, attackLabel, "Attack");
        setupRotary (decaySlider, decayLabel, "Decay");
        setupRotary (sustainSlider, sustainLabel, "Sustain");
        setupRotary (releaseSlider, releaseLabel, "Release");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        attackAttachment  = std::make_unique<SliderAttachment> (apvts, adsrParamId (slotIndex, AdsrParam::attack), attackSlider);
        decayAttachment   = std::make_unique<SliderAttachment> (apvts, adsrParamId (slotIndex, AdsrParam::decay), decaySlider);
        sustainAttachment = std::make_unique<SliderAttachment> (apvts, adsrParamId (slotIndex, AdsrParam::sustain), sustainSlider);
        releaseAttachment = std::make_unique<SliderAttachment> (apvts, adsrParamId (slotIndex, AdsrParam::release), releaseSlider);
    }

    void AdsrControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 4;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16);
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), attackLabel, attackSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), decayLabel, decaySlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), sustainLabel, sustainSlider);
        layoutKnob (knobRow, releaseLabel, releaseSlider);
    }

    EnvelopeControlsPanel::EnvelopeControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot)
        : editor (rackSlot)
    {
        addAndMakeVisible (editor);

        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (lengthSlider, lengthLabel, "Length");
        setupRotary (depthSlider, depthLabel, "Depth");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        lengthAttachment = std::make_unique<SliderAttachment> (apvts, envelopeParamId (slotIndex, EnvelopeParam::length), lengthSlider);
        depthAttachment  = std::make_unique<SliderAttachment> (apvts, envelopeParamId (slotIndex, EnvelopeParam::depth), depthSlider);
    }

    void EnvelopeControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        editor.setBounds (area.removeFromTop (160));
        area.removeFromTop (10);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 2;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16);
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), lengthLabel, lengthSlider);
        layoutKnob (knobRow, depthLabel, depthSlider);
    }

    LossyControlsPanel::LossyControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        setupRotary (bitsSlider, bitsLabel, "Bits");
        setupRotary (rateSlider, rateLabel, "Rate");
        setupRotary (jitterSlider, jitterLabel, "Jitter");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        bitsAttachment   = std::make_unique<SliderAttachment> (apvts, lossyParamId (slotIndex, LossyParam::bits), bitsSlider);
        rateAttachment   = std::make_unique<SliderAttachment> (apvts, lossyParamId (slotIndex, LossyParam::rate), rateSlider);
        jitterAttachment = std::make_unique<SliderAttachment> (apvts, lossyParamId (slotIndex, LossyParam::jitter), jitterSlider);
        mixAttachment    = std::make_unique<SliderAttachment> (apvts, lossyParamId (slotIndex, LossyParam::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, lossyParamId (slotIndex, LossyParam::output), outputSlider);

        modTargets = {
            { lossyParamId (slotIndex, LossyParam::bits),   "Bits",   &bitsSlider },
            { lossyParamId (slotIndex, LossyParam::rate),   "Rate",   &rateSlider },
            { lossyParamId (slotIndex, LossyParam::jitter), "Jitter", &jitterSlider },
            { lossyParamId (slotIndex, LossyParam::mix),    "Mix",    &mixSlider },
            { lossyParamId (slotIndex, LossyParam::output), "Output", &outputSlider },
        };
    }

    void LossyControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), bitsLabel, bitsSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), rateLabel, rateSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), jitterLabel, jitterSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);
    }

    Eq8ControlsPanel::Eq8ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        auto bandLabelText = getEq8BandLabels();
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            setupRotary (bandSliders[(size_t) b], bandLabels[(size_t) b], bandLabelText[b]);
            bandAttachments[(size_t) b] = std::make_unique<SliderAttachment> (
                apvts, eq8ParamId (slotIndex, eq8BandParam (b)), bandSliders[(size_t) b]);
            modTargets.push_back ({ eq8ParamId (slotIndex, eq8BandParam (b)),
                                     bandLabelText[b] + "Hz", &bandSliders[(size_t) b] });
        }

        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        mixAttachment    = std::make_unique<SliderAttachment> (apvts, eq8ParamId (slotIndex, Eq8Param::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, eq8ParamId (slotIndex, Eq8Param::output), outputSlider);

        modTargets.push_back ({ eq8ParamId (slotIndex, Eq8Param::mix), "Mix", &mixSlider });
        modTargets.push_back ({ eq8ParamId (slotIndex, Eq8Param::output), "Output", &outputSlider });
    }

    void Eq8ControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto topRow = area.removeFromTop (106);
        const int topKnobWidth = topRow.getWidth() / 4;
        for (int b = 0; b < 4; ++b)
            layoutKnob (topRow.removeFromLeft (topKnobWidth), bandLabels[(size_t) b], bandSliders[(size_t) b]);

        area.removeFromTop (6);

        auto bottomRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomRow.getWidth() / 4;
        for (int b = 4; b < 8; ++b)
            layoutKnob (bottomRow.removeFromLeft (bottomKnobWidth), bandLabels[(size_t) b], bandSliders[(size_t) b]);

        area.removeFromTop (6);

        auto mixRow = area.removeFromTop (106);
        const int mixKnobWidth = mixRow.getWidth() / 2;
        layoutKnob (mixRow.removeFromLeft (mixKnobWidth), mixLabel, mixSlider);
        layoutKnob (mixRow, outputLabel, outputSlider);
    }

    ChorusControlsPanel::ChorusControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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
        setupRotary (delaySlider, delayLabel, "Delay");
        setupRotary (feedbackSlider, feedbackLabel, "Feedback");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        modeLabel.setText ("Mode", juce::dontSendNotification);
        modeLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getChorusModeChoices())
            modeBox.addItem (choice, itemId++);

        addAndMakeVisible (modeBox);
        addAndMakeVisible (modeLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        rateAttachment     = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::rate), rateSlider);
        depthAttachment    = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::depth), depthSlider);
        delayAttachment    = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::delay), delaySlider);
        feedbackAttachment = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::feedback), feedbackSlider);
        mixAttachment      = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::mix), mixSlider);
        outputAttachment   = std::make_unique<SliderAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::output), outputSlider);
        modeAttachment     = std::make_unique<ComboBoxAttachment> (apvts, chorusParamId (slotIndex, ChorusParam::mode), modeBox);

        modTargets = {
            { chorusParamId (slotIndex, ChorusParam::rate),     "Rate",     &rateSlider },
            { chorusParamId (slotIndex, ChorusParam::depth),    "Depth",    &depthSlider },
            { chorusParamId (slotIndex, ChorusParam::delay),    "Delay",    &delaySlider },
            { chorusParamId (slotIndex, ChorusParam::feedback), "Feedback", &feedbackSlider },
            { chorusParamId (slotIndex, ChorusParam::mix),      "Mix",      &mixSlider },
            { chorusParamId (slotIndex, ChorusParam::output),   "Output",   &outputSlider },
        };
    }

    void ChorusControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto topRow = area.removeFromTop (106);
        const int topKnobWidth = topRow.getWidth() / 3;
        layoutKnob (topRow.removeFromLeft (topKnobWidth), rateLabel, rateSlider);
        layoutKnob (topRow.removeFromLeft (topKnobWidth), depthLabel, depthSlider);
        layoutKnob (topRow, delayLabel, delaySlider);

        area.removeFromTop (6);

        auto bottomRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomRow.getWidth() / 3;
        layoutKnob (bottomRow.removeFromLeft (bottomKnobWidth), feedbackLabel, feedbackSlider);
        layoutKnob (bottomRow.removeFromLeft (bottomKnobWidth), mixLabel, mixSlider);
        layoutKnob (bottomRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto modeRow = area.removeFromTop (44);
        auto left = modeRow.removeFromLeft (modeRow.getWidth() / 2).reduced (4, 0);

        modeLabel.setBounds (left.removeFromTop (16));
        modeBox.setBounds (left.removeFromTop (24));
    }

    Eq3ControlsPanel::Eq3ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        setupRotary (lowSlider, lowLabel, "Low");
        setupRotary (midSlider, midLabel, "Mid");
        setupRotary (highSlider, highLabel, "High");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        lowAttachment    = std::make_unique<SliderAttachment> (apvts, eq3ParamId (slotIndex, Eq3Param::low), lowSlider);
        midAttachment    = std::make_unique<SliderAttachment> (apvts, eq3ParamId (slotIndex, Eq3Param::mid), midSlider);
        highAttachment   = std::make_unique<SliderAttachment> (apvts, eq3ParamId (slotIndex, Eq3Param::high), highSlider);
        mixAttachment    = std::make_unique<SliderAttachment> (apvts, eq3ParamId (slotIndex, Eq3Param::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, eq3ParamId (slotIndex, Eq3Param::output), outputSlider);

        modTargets = {
            { eq3ParamId (slotIndex, Eq3Param::low),    "Low",    &lowSlider },
            { eq3ParamId (slotIndex, Eq3Param::mid),    "Mid",    &midSlider },
            { eq3ParamId (slotIndex, Eq3Param::high),   "High",   &highSlider },
            { eq3ParamId (slotIndex, Eq3Param::mix),    "Mix",    &mixSlider },
            { eq3ParamId (slotIndex, Eq3Param::output), "Output", &outputSlider },
        };
    }

    void Eq3ControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / 5;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        layoutKnob (knobRow.removeFromLeft (knobWidth), lowLabel, lowSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), midLabel, midSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), highLabel, highSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);
    }

    MultibandConvolutionControlsPanel::MultibandConvolutionControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : apvtsRef (apvts), slotIndexValue (slotIndex), splitBar (apvts, slotIndex)
    {
        addAndMakeVisible (splitBar);

        nameLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        nameLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (nameLabel);

        // The IR catalog itself doesn't depend on which band is selected -- only which entry is
        // currently chosen does (that's the attachment setActiveBand() rebuilds) -- so this list
        // is populated once, not per band-switch.
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

        splitBar.onBandSelected = [this] (int band) { setActiveBand (band); };
        setActiveBand (splitBar.getSelectedBand());
    }

    void MultibandConvolutionControlsPanel::setActiveBand (int band)
    {
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        nameLabel.setText (getConvolutionBandLabels()[band], juce::dontSendNotification);

        // Explicitly release the OLD attachments before constructing the new ones -- assigning
        // straight into the unique_ptrs (`irAttachment = std::make_unique<...>(...)`) builds the
        // new attachment first, while the old one is still alive and still a registered listener
        // on the same slider/combo box. The new attachment's constructor syncs the knob's display
        // via sendNotificationSync, which fires synchronously on EVERY current listener -- so the
        // still-alive old attachment would receive the new band's value as if the user had just
        // dragged the old band's knob there, and write it straight into the old band's parameter.
        // That's what caused values to appear to "reset": switching to Mid silently overwrote
        // Low's real values with Mid's, and switching back just displayed the damage.
        irAttachment.reset();
        toneAttachment.reset();
        fadeInAttachment.reset();
        fadeOutAttachment.reset();
        stretchAttachment.reset();
        mixAttachment.reset();
        outputAttachment.reset();

        irAttachment = std::make_unique<ComboBoxAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::irIndex), irBox);
        toneAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::tone), toneSlider);
        fadeInAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::fadeIn), fadeInSlider);
        fadeOutAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::fadeOut), fadeOutSlider);
        stretchAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::stretch), stretchSlider);
        mixAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (
            apvtsRef, multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::output), outputSlider);

        const auto bandLabel = getConvolutionBandLabels()[band];
        modTargets = {
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::tone),     bandLabel + " Tone",     &toneSlider },
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::fadeIn),   bandLabel + " Fade In",  &fadeInSlider },
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::fadeOut),  bandLabel + " Fade Out", &fadeOutSlider },
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::stretch),  bandLabel + " Stretch",  &stretchSlider },
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::mix),      bandLabel + " Mix",      &mixSlider },
            { multibandConvolutionBandParamId (slotIndexValue, band, MultibandConvolutionBandParam::output),   bandLabel + " Output",   &outputSlider },
        };
    }

    void MultibandConvolutionControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        splitBar.setBounds (area.removeFromTop (50));
        area.removeFromTop (10);

        auto irRow = area.removeFromTop (24);
        nameLabel.setBounds (irRow.removeFromLeft (40));
        irBox.setBounds (irRow);
        area.removeFromTop (6);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto topKnobRow = area.removeFromTop (106);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), toneLabel, toneSlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), fadeInLabel, fadeInSlider);
        layoutKnob (topKnobRow, fadeOutLabel, fadeOutSlider);
        area.removeFromTop (6);

        auto bottomKnobRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomKnobRow.getWidth() / 3;
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), stretchLabel, stretchSlider);
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), mixLabel, mixSlider);
        layoutKnob (bottomKnobRow, outputLabel, outputSlider);
    }

    ThreeOscControlsPanel::ThreeOscControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        const auto oscLabels = getThreeOscOscLabels();
        const auto waveformChoices = getThreeOscWaveformChoices();

        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            auto& o = oscs[(size_t) osc];

            o.nameLabel.setText (oscLabels[osc], juce::dontSendNotification);
            o.nameLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            o.nameLabel.setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (o.nameLabel);

            int itemId = 1;
            for (auto& choice : waveformChoices)
                o.waveformBox.addItem (choice, itemId++);
            addAndMakeVisible (o.waveformBox);

            setupRotary (o.coarseSlider, o.coarseLabel, "Coarse");
            setupRotary (o.fineSlider, o.fineLabel, "Fine");
            setupRotary (o.panSlider, o.panLabel, "Pan");
            setupRotary (o.levelSlider, o.levelLabel, "Level");

            o.waveformAttachment = std::make_unique<ComboBoxAttachment> (
                apvts, threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::waveform), o.waveformBox);
            o.coarseAttachment = std::make_unique<SliderAttachment> (
                apvts, threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::coarse), o.coarseSlider);
            o.fineAttachment = std::make_unique<SliderAttachment> (
                apvts, threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::fine), o.fineSlider);
            o.panAttachment = std::make_unique<SliderAttachment> (
                apvts, threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::pan), o.panSlider);
            o.levelAttachment = std::make_unique<SliderAttachment> (
                apvts, threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::level), o.levelSlider);

            modTargets.push_back ({ threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::coarse), oscLabels[osc] + " Coarse", &o.coarseSlider });
            modTargets.push_back ({ threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::fine),   oscLabels[osc] + " Fine",   &o.fineSlider });
            modTargets.push_back ({ threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::pan),    oscLabels[osc] + " Pan",    &o.panSlider });
            modTargets.push_back ({ threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::level),  oscLabels[osc] + " Level",  &o.levelSlider });
        }

        setupRotary (attackSlider, attackLabel, "Attack");
        setupRotary (decaySlider, decayLabel, "Decay");
        setupRotary (sustainSlider, sustainLabel, "Sustain");
        setupRotary (releaseSlider, releaseLabel, "Release");
        setupRotary (fm1to2Slider, fm1to2Label, "FM 1>2");
        setupRotary (fm2to3Slider, fm2to3Label, "FM 2>3");
        setupRotary (outputSlider, outputLabel, "Output");

        attackAttachment  = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::attack), attackSlider);
        decayAttachment   = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::decay), decaySlider);
        sustainAttachment = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::sustain), sustainSlider);
        releaseAttachment = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::release), releaseSlider);
        fm1to2Attachment  = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::fm1to2), fm1to2Slider);
        fm2to3Attachment  = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::fm2to3), fm2to3Slider);
        outputAttachment  = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::output), outputSlider);

        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::attack),  "Attack",  &attackSlider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::decay),   "Decay",   &decaySlider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::sustain), "Sustain", &sustainSlider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::release), "Release", &releaseSlider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::fm1to2),  "FM 1>2",  &fm1to2Slider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::fm2to3),  "FM 2>3",  &fm2to3Slider });
        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::output),  "Output",  &outputSlider });
    }

    void ThreeOscControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            auto& o = oscs[(size_t) osc];

            auto waveformRow = area.removeFromTop (24);
            o.nameLabel.setBounds (waveformRow.removeFromLeft (48));
            o.waveformBox.setBounds (waveformRow);
            area.removeFromTop (6);

            auto knobRow = area.removeFromTop (106);
            const int knobWidth = knobRow.getWidth() / 4;
            layoutKnob (knobRow.removeFromLeft (knobWidth), o.coarseLabel, o.coarseSlider);
            layoutKnob (knobRow.removeFromLeft (knobWidth), o.fineLabel, o.fineSlider);
            layoutKnob (knobRow.removeFromLeft (knobWidth), o.panLabel, o.panSlider);
            layoutKnob (knobRow, o.levelLabel, o.levelSlider);

            area.removeFromTop (10);
        }

        auto envRow = area.removeFromTop (106);
        const int envKnobWidth = envRow.getWidth() / 4;
        layoutKnob (envRow.removeFromLeft (envKnobWidth), attackLabel, attackSlider);
        layoutKnob (envRow.removeFromLeft (envKnobWidth), decayLabel, decaySlider);
        layoutKnob (envRow.removeFromLeft (envKnobWidth), sustainLabel, sustainSlider);
        layoutKnob (envRow, releaseLabel, releaseSlider);

        area.removeFromTop (6);

        auto fmRow = area.removeFromTop (106);
        const int fmKnobWidth = fmRow.getWidth() / 3;
        layoutKnob (fmRow.removeFromLeft (fmKnobWidth), fm1to2Label, fm1to2Slider);
        layoutKnob (fmRow.removeFromLeft (fmKnobWidth), fm2to3Label, fm2to3Slider);
        layoutKnob (fmRow, outputLabel, outputSlider);
    }
}
