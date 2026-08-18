#include "ModuleControlPanels.h"
#include "GGridLookAndFeel.h"
#include "../IR/IRLibrary.h"
#include "../Modules/MultibandConvolutionModule.h"
#include "../Modules/MultipassModule.h"
#include "../Modules/Eq8Module.h"
#include <cmath>

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

    LfoCurveEditor::LfoCurveEditor (RackSlot& rackSlotIn, juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : rackSlot (rackSlotIn),
          shapeParam (apvts.getRawParameterValue (lfoParamId (slotIndex, LfoParam::shape)))
    {
        setInterceptsMouseClicks (true, false);
        startTimerHz (30);
    }

    LfoCurveEditor::~LfoCurveEditor()
    {
        stopTimer();
    }

    LFOModule* LfoCurveEditor::getModule() const
    {
        return dynamic_cast<LFOModule*> (rackSlot.getCurrentModule());
    }

    void LfoCurveEditor::timerCallback()
    {
        repaint();
    }

    bool LfoCurveEditor::isCurveModifierDown (const juce::ModifierKeys& mods) const
    {
        return mods.isAltDown() || mods.isCommandDown();
    }

    void LfoCurveEditor::updateHoverCurveSegment (const juce::MouseEvent& e)
    {
        const int next = isCurveModifierDown (e.mods) ? segmentAtX (pixelToPoint (e.position).x) : -1;
        if (next != hoverCurveSegment)
        {
            hoverCurveSegment = next;
            repaint();
        }
    }

    void LfoCurveEditor::beginEditFromCurrentShape()
    {
        auto* module = getModule();
        if (module == nullptr)
            return;

        const int shape = shapeParam != nullptr ? (int) shapeParam->load() : customShapeIndex;
        if (! module->isCustomEdited() && shape != customShapeIndex)
            module->seedCustomFromShape (shape);

        module->setCustomEdited (true);
    }

    juce::Point<float> LfoCurveEditor::pixelToPoint (juce::Point<float> pixel) const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        const float x = area.getWidth() > 0.0f ? (pixel.x - area.getX()) / area.getWidth() : 0.0f;
        const float y = area.getHeight() > 0.0f ? 1.0f - 2.0f * (pixel.y - area.getY()) / area.getHeight() : 0.0f;
        return { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (-1.0f, 1.0f, y) };
    }

    juce::Point<float> LfoCurveEditor::pointToPixel (juce::Point<float> point) const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f, 6.0f);
        return { area.getX() + point.x * area.getWidth(),
                 area.getY() + (1.0f - point.y) * 0.5f * area.getHeight() };
    }

    int LfoCurveEditor::hitTestPoint (juce::Point<float> pixel) const
    {
        auto* module = getModule();
        if (module == nullptr)
            return -1;

        int best = -1;
        float bestDistSquared = grabToleranceSquaredPx;
        const int numPoints = module->getNumCustomPoints();
        for (int i = 0; i < numPoints; ++i)
        {
            const auto p = module->getCustomPoint (i);
            const auto pixelPoint = pointToPixel ({ p.x, p.y });
            const float d = pixelPoint.getDistanceSquaredFrom (pixel);
            if (d <= bestDistSquared)
            {
                bestDistSquared = d;
                best = i;
            }
        }

        return best;
    }

    int LfoCurveEditor::segmentAtX (float normalisedX) const
    {
        auto* module = getModule();
        if (module == nullptr)
            return -1;

        const int numPoints = module->getNumCustomPoints();
        for (int i = 1; i < numPoints; ++i)
            if (normalisedX <= module->getCustomPoint (i).x)
                return i - 1;

        return juce::jmax (0, numPoints - 2);
    }

    float LfoCurveEditor::previewValueAt (float phase01) const
    {
        const int shape = shapeParam != nullptr ? (int) shapeParam->load() : 0;
        if (auto* module = getModule())
            if (module->isCustomEdited() || shape == customShapeIndex)
                return module->evaluateCustomAt (phase01);

        switch (shape)
        {
            case 0: return (float) std::sin (juce::MathConstants<float>::twoPi * phase01);
            case 1: return phase01 < 0.5f ? (4.0f * phase01 - 1.0f) : (3.0f - 4.0f * phase01);
            case 2: return phase01 < 0.5f ? 1.0f : -1.0f;
            case 3:
            case 5: return 2.0f * phase01 - 1.0f;
            case 4:
            {
                const int step = juce::jlimit (0, 15, (int) std::floor (phase01 * 16.0f));
                return juce::Random ((juce::int64) (0x51f0 + step * 7919)).nextFloat() * 2.0f - 1.0f;
            }
            case 6: return 1.0f - 2.0f * phase01;
            case customShapeIndex:
                return 0.0f;
            default:
                return 0.0f;
        }
    }

    void LfoCurveEditor::mouseDown (const juce::MouseEvent& e)
    {
        auto* module = getModule();
        if (module == nullptr)
            return;

        beginEditFromCurrentShape();
        const auto p = pixelToPoint (e.position);

        if (e.mods.isRightButtonDown())
        {
            const int hit = hitTestPoint (e.position);
            if (hit >= 0)
                module->removeCustomPoint (hit);
            repaint();
            return;
        }

        if (isCurveModifierDown (e.mods))
        {
            draggingCurveSegment = segmentAtX (p.x);
            hoverCurveSegment = draggingCurveSegment;
            curveDragStart = e.position;
            curveStartValue = draggingCurveSegment >= 0 ? module->getCustomPoint (draggingCurveSegment).curve : 0.0f;
            return;
        }

        drawMode = e.mods.isCtrlDown();
        draggingPoint = hitTestPoint (e.position);
        if (draggingPoint < 0)
            draggingPoint = module->addCustomPoint (p);
        else
            module->moveCustomPoint (draggingPoint, p);

        repaint();
    }

    void LfoCurveEditor::mouseDrag (const juce::MouseEvent& e)
    {
        auto* module = getModule();
        if (module == nullptr)
            return;

        const auto p = pixelToPoint (e.position);

        if (draggingCurveSegment >= 0)
        {
            const float delta = (curveDragStart.y - e.position.y) / 70.0f;
            module->setSegmentCurve (draggingCurveSegment, juce::jlimit (-1.0f, 1.0f, curveStartValue + delta));
            repaint();
            return;
        }

        if (drawMode)
        {
            const int newPoint = module->addCustomPoint (p);
            if (newPoint >= 0)
                draggingPoint = newPoint;
            else if (draggingPoint >= 0)
                module->moveCustomPoint (draggingPoint, p);
            repaint();
            return;
        }

        if (draggingPoint >= 0)
        {
            module->moveCustomPoint (draggingPoint, p);
            repaint();
        }
    }

    void LfoCurveEditor::mouseUp (const juce::MouseEvent&)
    {
        draggingPoint = -1;
        draggingCurveSegment = -1;
        drawMode = false;
    }

    void LfoCurveEditor::mouseMove (const juce::MouseEvent& e)
    {
        updateHoverCurveSegment (e);
    }

    void LfoCurveEditor::mouseExit (const juce::MouseEvent&)
    {
        if (hoverCurveSegment >= 0)
        {
            hoverCurveSegment = -1;
            repaint();
        }
    }

    void LfoCurveEditor::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        auto area = bounds.reduced (8.0f, 6.0f);

        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        g.setColour (Palette::dimmer);
        for (int i = 1; i < 4; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / 4.0f;
            g.drawLine (x, area.getY(), x, area.getBottom(), 1.0f);
        }
        g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 1.0f);

        juce::Path curvePath, fillPath;
        const int width = juce::jmax (1, (int) area.getWidth());
        for (int x = 0; x <= width; ++x)
        {
            const float phase01 = (float) x / (float) width;
            const float value = juce::jlimit (-1.0f, 1.0f, previewValueAt (phase01));
            const auto pixel = pointToPixel ({ phase01, value });
            if (x == 0)
            {
                curvePath.startNewSubPath (pixel);
                fillPath.startNewSubPath (area.getX(), area.getCentreY());
                fillPath.lineTo (pixel);
            }
            else
            {
                curvePath.lineTo (pixel);
                fillPath.lineTo (pixel);
            }
        }
        fillPath.lineTo (area.getRight(), area.getCentreY());
        fillPath.closeSubPath();

        g.setColour (Palette::modAccent.withAlpha (0.20f));
        g.fillPath (fillPath);
        g.setColour (Palette::modAccent);
        g.strokePath (curvePath, juce::PathStrokeType (2.0f));

        auto* currentModule = getModule();
        const bool isCustom = currentModule != nullptr
                              && (currentModule->isCustomEdited()
                                  || (shapeParam != nullptr && (int) shapeParam->load() == customShapeIndex));
        if (isCustom)
        {
            if (auto* module = currentModule)
            {
                const int numPoints = module->getNumCustomPoints();
                const int highlightedSegment = draggingCurveSegment >= 0 ? draggingCurveSegment : hoverCurveSegment;

                if (highlightedSegment >= 0 && highlightedSegment < numPoints - 1)
                {
                    const auto a = module->getCustomPoint (highlightedSegment);
                    const auto b = module->getCustomPoint (highlightedSegment + 1);
                    juce::Path highlightPath;
                    constexpr int segmentSamples = 48;
                    for (int s = 0; s <= segmentSamples; ++s)
                    {
                        const float t = (float) s / (float) segmentSamples;
                        const float x = a.x + t * (b.x - a.x);
                        const float y = module->evaluateCustomAt (x);
                        const auto pixel = pointToPixel ({ x, y });
                        if (s == 0) highlightPath.startNewSubPath (pixel);
                        else        highlightPath.lineTo (pixel);
                    }

                    g.setColour (Palette::bright);
                    g.strokePath (highlightPath, juce::PathStrokeType (3.0f));
                }

                for (int i = 0; i < numPoints; ++i)
                {
                    const auto p = module->getCustomPoint (i);
                    const auto pixel = pointToPixel ({ p.x, p.y });
                    const bool endpoint = i == 0 || i == numPoints - 1;
                    const float size = endpoint ? 7.0f : 8.0f;
                    g.setColour (i == draggingPoint ? Palette::bright : Palette::accent);
                    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (pixel));

                    if (i < numPoints - 1 && std::abs (p.curve) > 0.02f)
                    {
                        const auto next = module->getCustomPoint (i + 1);
                        const float midX = (p.x + next.x) * 0.5f;
                        const float midY = module->evaluateCustomAt (midX);
                        g.setColour (i == draggingCurveSegment ? Palette::bright : Palette::dim);
                        g.drawEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (pointToPixel ({ midX, midY })), 1.0f);
                    }
                }
            }
        }

        if (auto* module = currentModule)
        {
            const float phaseX = pointToPixel ({ module->getPhasePosition(), 0.0f }).x;
            g.setColour (Palette::bright);
            g.drawLine (phaseX, area.getY(), phaseX, area.getBottom(), 1.5f);
        }
    }

    LfoControlsPanel::LfoControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlotIn)
        : rackSlot (rackSlotIn),
          curveEditor (rackSlotIn, apvts, slotIndex)
    {
        addAndMakeVisible (curveEditor);

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
        shapeBox.addListener (this);

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

        lastShapeIndex = shapeBox.getSelectedId() - 1;
        startTimerHz (10);
    }

    LfoControlsPanel::~LfoControlsPanel()
    {
        stopTimer();
        shapeBox.removeListener (this);
    }

    LFOModule* LfoControlsPanel::getModule() const
    {
        return dynamic_cast<LFOModule*> (rackSlot.getCurrentModule());
    }

    void LfoControlsPanel::comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged)
    {
        if (comboBoxThatHasChanged != &shapeBox || ignoreShapeBoxChange)
            return;

        if (auto* module = getModule())
            module->setCustomEdited (false);

        lastShapeIndex = shapeBox.getSelectedId() - 1;
        refreshShapeLabels();
    }

    void LfoControlsPanel::timerCallback()
    {
        const int shapeIndex = shapeBox.getSelectedId() - 1;
        const bool edited = getModule() != nullptr && getModule()->isCustomEdited();

        if (shapeIndex != lastShapeIndex || edited != lastCustomEdited)
            refreshShapeLabels();
    }

    void LfoControlsPanel::refreshShapeLabels()
    {
        const int shapeIndex = shapeBox.getSelectedId() - 1;
        const bool edited = getModule() != nullptr && getModule()->isCustomEdited();
        const auto choices = getLfoShapeChoices();

        ignoreShapeBoxChange = true;
        for (int i = 0; i < choices.size(); ++i)
        {
            auto text = choices[i];
            if (edited && i == shapeIndex && i != 7)
                text += "*";
            shapeBox.changeItemText (i + 1, text);
        }
        ignoreShapeBoxChange = false;

        lastShapeIndex = shapeIndex;
        lastCustomEdited = edited;
    }

    void LfoControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        curveEditor.setBounds (area.removeFromTop (128));
        area.removeFromTop (8);

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

    LfoTablePreviewComponent::LfoTablePreviewComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : tableParam  (apvts.getRawParameterValue (lfoTableParamId (slotIndex, LfoTableParam::tableIndex))),
          frameParam  (apvts.getRawParameterValue (lfoTableParamId (slotIndex, LfoTableParam::frame))),
          smoothParam (apvts.getRawParameterValue (lfoTableParamId (slotIndex, LfoTableParam::smooth))),
          phaseParam  (apvts.getRawParameterValue (lfoTableParamId (slotIndex, LfoTableParam::phase)))
    {
        startTimerHz (15);
    }

    LfoTablePreviewComponent::~LfoTablePreviewComponent()
    {
        stopTimer();
    }

    std::shared_ptr<const WavetableLibrary::Table> LfoTablePreviewComponent::getTable()
    {
        const int wanted = (int) tableParam->load();
        if (wanted != loadedIndex || table == nullptr)
        {
            table = WavetableLibrary::loadTable (wanted);
            loadedIndex = wanted;
        }
        return table;
    }

    void LfoTablePreviewComponent::timerCallback()
    {
        repaint();
    }

    void LfoTablePreviewComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        const auto currentTable = getTable();
        if (currentTable == nullptr || ! currentTable->isValid())
            return;

        auto left = bounds.removeFromLeft (bounds.getWidth() * 0.46f).reduced (8.0f, 8.0f);
        auto right = bounds.reduced (8.0f, 10.0f);
        const float smooth = smoothParam->load() / 100.0f;
        const float phaseOffset = phaseParam->load() / 360.0f;
        const float frame = juce::jlimit (0.0f, (float) currentTable->numFrames - 1.0f, frameParam->load() - 1.0f);

        g.setColour (Palette::dimmer);
        g.drawLine (left.getX(), left.getCentreY(), left.getRight(), left.getCentreY(), 1.0f);
        g.drawLine (left.getX(), left.getY(), left.getX(), left.getBottom(), 1.0f);

        juce::Path framePath;
        constexpr int points = 160;
        for (int i = 0; i < points; ++i)
        {
            const float x01 = (float) i / (float) (points - 1);
            const float y = currentTable->sample (frame, x01 + phaseOffset, smooth);
            const auto x = juce::jmap (x01, left.getX(), left.getRight());
            const auto py = juce::jmap (y, -1.0f, 1.0f, left.getBottom(), left.getY());
            if (i == 0) framePath.startNewSubPath (x, py);
            else        framePath.lineTo (x, py);
        }

        g.setColour (Palette::bright);
        g.strokePath (framePath, juce::PathStrokeType (2.0f));

        const int stackFrames = juce::jmin (18, currentTable->numFrames);
        for (int s = stackFrames - 1; s >= 0; --s)
        {
            const float f01 = stackFrames > 1 ? (float) s / (float) (stackFrames - 1) : 0.0f;
            const float tableFrame = f01 * (float) (currentTable->numFrames - 1);
            const float xOffset = f01 * right.getWidth() * 0.24f;
            const float yOffset = -f01 * right.getHeight() * 0.38f;
            const float alpha = juce::jmap (f01, 0.18f, 0.55f);

            juce::Path stackPath;
            for (int i = 0; i < 52; ++i)
            {
                const float x01 = (float) i / 51.0f;
                const float y = currentTable->sample (tableFrame, x01 + phaseOffset, smooth);
                const auto x = juce::jmap (x01, right.getX(), right.getRight() - right.getWidth() * 0.24f) + xOffset;
                const auto py = juce::jmap (y, -1.0f, 1.0f, right.getBottom(), right.getY() + right.getHeight() * 0.28f) + yOffset;
                if (i == 0) stackPath.startNewSubPath (x, py);
                else        stackPath.lineTo (x, py);
            }

            g.setColour (Palette::accent.withAlpha (alpha));
            g.strokePath (stackPath, juce::PathStrokeType (1.0f));
        }

        const float frame01 = currentTable->numFrames > 1 ? frame / (float) (currentTable->numFrames - 1) : 0.0f;
        g.setColour (Palette::modAccent);
        const float markerX = juce::jmap (frame01, right.getX(), right.getRight());
        g.drawLine (markerX, right.getY(), markerX, right.getBottom(), 1.0f);
    }

    LfoTableControlsPanel::LfoTableControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : preview (apvts, slotIndex),
          tableIndexParam (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (lfoTableParamId (slotIndex, LfoTableParam::tableIndex))))
    {
        addAndMakeVisible (preview);

        int itemId = 1;
        for (auto& choice : WavetableLibrary::getCatalogDisplayNames())
            tableBox.addItem (choice, itemId++);
        addAndMakeVisible (tableBox);
        addAndMakeVisible (prevTableButton);
        addAndMakeVisible (nextTableButton);
        prevTableButton.onClick = [this] { stepTable (-1); };
        nextTableButton.onClick = [this] { stepTable (1); };

        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (frameSlider, frameLabel, "Frame");
        setupRotary (smoothSlider, smoothLabel, "Smooth");
        setupRotary (phaseSlider, phaseLabel, "Phase");
        setupRotary (rateSlider, rateLabel, "Rate");
        setupRotary (depthSlider, depthLabel, "Depth");

        addAndMakeVisible (syncButton);
        addAndMakeVisible (retriggerButton);

        for (auto& choice : getDelayDivisionChoices())
            divisionBox.addItem (choice, divisionBox.getNumItems() + 1);
        addAndMakeVisible (divisionBox);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        tableAttachment     = std::make_unique<ComboBoxAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::tableIndex), tableBox);
        frameAttachment     = std::make_unique<SliderAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::frame), frameSlider);
        smoothAttachment    = std::make_unique<SliderAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::smooth), smoothSlider);
        phaseAttachment     = std::make_unique<SliderAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::phase), phaseSlider);
        rateAttachment      = std::make_unique<SliderAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::rateHz), rateSlider);
        depthAttachment     = std::make_unique<SliderAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::depth), depthSlider);
        syncAttachment      = std::make_unique<ButtonAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::rateMode), syncButton);
        retriggerAttachment = std::make_unique<ButtonAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::retrigger), retriggerButton);
        divisionAttachment  = std::make_unique<ComboBoxAttachment> (apvts, lfoTableParamId (slotIndex, LfoTableParam::division), divisionBox);
    }

    void LfoTableControlsPanel::stepTable (int direction)
    {
        if (tableIndexParam == nullptr)
            return;

        const int numChoices = WavetableLibrary::getCatalogDisplayNames().size();
        if (numChoices <= 0)
            return;

        const int next = juce::jlimit (0, numChoices - 1, tableIndexParam->getIndex() + direction);
        tableIndexParam->setValueNotifyingHost (tableIndexParam->convertTo0to1 ((float) next));
    }

    void LfoTableControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        preview.setBounds (area.removeFromTop (120));
        area.removeFromTop (6);

        auto pickerRow = area.removeFromTop (24);
        prevTableButton.setBounds (pickerRow.removeFromLeft (24));
        pickerRow.removeFromLeft (4);
        nextTableButton.setBounds (pickerRow.removeFromLeft (24));
        pickerRow.removeFromLeft (6);
        tableBox.setBounds (pickerRow);

        area.removeFromTop (8);
        auto topKnobRow = area.removeFromTop (106);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        auto bottomKnobRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomKnobRow.getWidth() / 2;

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16);
            slider.setBounds (col);
        };

        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), frameLabel, frameSlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), smoothLabel, smoothSlider);
        layoutKnob (topKnobRow, phaseLabel, phaseSlider);

        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), rateLabel, rateSlider);
        layoutKnob (bottomKnobRow, depthLabel, depthSlider);

        area.removeFromTop (4);
        auto bottomRow = area.removeFromTop (24);
        syncButton.setBounds (bottomRow.removeFromLeft (60));
        bottomRow.removeFromLeft (4);
        divisionBox.setBounds (bottomRow.removeFromLeft (96));
        bottomRow.removeFromLeft (8);
        retriggerButton.setBounds (bottomRow.removeFromLeft (80));
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

    Eq8ControlsPanel::Eq8ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot)
        : apvtsRef (apvts), slotIndexValue (slotIndex),
          curveEditor (apvts, slotIndex,
                       [&rackSlot]() -> SpectrumAnalyzer*
                       {
                           if (auto* m = dynamic_cast<Eq8Module*> (rackSlot.getCurrentModule()))
                               return &m->getAnalyzer();
                           return nullptr;
                       })
    {
        addAndMakeVisible (curveEditor);
        curveEditor.onBandSelected = [this] (int band) { setActiveBand (band); };

        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        bandNameLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        bandNameLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (bandNameLabel);
        addAndMakeVisible (enableButton);

        int itemId = 1;
        for (auto& choice : getEq8FilterTypeChoices())
            typeBox.addItem (choice, itemId++);
        addAndMakeVisible (typeBox);

        setupRotary (freqSlider, freqLabel, "Freq");
        setupRotary (gainSlider, gainLabel, "Gain");
        setupRotary (qSlider, qLabel, "Q");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        mixAttachment    = std::make_unique<SliderAttachment> (apvts, eq8ParamId (slotIndex, Eq8Param::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, eq8ParamId (slotIndex, Eq8Param::output), outputSlider);
        modTargets.push_back ({ eq8ParamId (slotIndex, Eq8Param::mix), "Mix", &mixSlider });
        modTargets.push_back ({ eq8ParamId (slotIndex, Eq8Param::output), "Output", &outputSlider });

        setActiveBand (0);
    }

    void Eq8ControlsPanel::setActiveBand (int band)
    {
        // Drop this band's own 3 modTargets (Freq/Gain/Q always occupy the tail of the vector,
        // pushed after Mix/Output above and re-pushed fresh each call) before rebuilding the
        // attachments -- mirrors MultibandConvolutionControlsPanel::setActiveBand exactly.
        if (modTargets.size() > 2)
            modTargets.resize (2);

        const auto bandLabel = getEq8BandLabels()[band];
        bandNameLabel.setText (bandLabel + "Hz Band", juce::dontSendNotification);

        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        // Explicitly release the OLD attachments before constructing the new ones -- assigning
        // straight into the unique_ptrs builds the new attachment first, while the old one is
        // still alive and still a registered listener on the same slider/combo/button. The new
        // attachment's constructor syncs the control's display via sendNotificationSync, which
        // fires synchronously on EVERY current listener -- so the still-alive old attachment
        // would receive the new band's value as if the user had just dragged the old band's knob
        // there, and write it straight into the old band's parameter. That's what made a band's
        // position appear to "reset"/jump to whichever band was clicked next -- see
        // MultibandConvolutionControlsPanel::setActiveBand's identical fix/comment.
        enableAttachment.reset();
        typeAttachment.reset();
        freqAttachment.reset();
        gainAttachment.reset();
        qAttachment.reset();

        enableAttachment = std::make_unique<ButtonAttachment> (apvtsRef, eq8BandEnabledParamId (slotIndexValue, band), enableButton);
        typeAttachment   = std::make_unique<ComboBoxAttachment> (apvtsRef, eq8BandTypeParamId (slotIndexValue, band), typeBox);
        freqAttachment   = std::make_unique<SliderAttachment> (apvtsRef, eq8BandFreqParamId (slotIndexValue, band), freqSlider);
        gainAttachment   = std::make_unique<SliderAttachment> (apvtsRef, eq8ParamId (slotIndexValue, eq8BandParam (band)), gainSlider);
        qAttachment      = std::make_unique<SliderAttachment> (apvtsRef, eq8BandQParamId (slotIndexValue, band), qSlider);

        modTargets.push_back ({ eq8BandFreqParamId (slotIndexValue, band), bandLabel + " Freq", &freqSlider });
        modTargets.push_back ({ eq8ParamId (slotIndexValue, eq8BandParam (band)), bandLabel + " Gain", &gainSlider });
        modTargets.push_back ({ eq8BandQParamId (slotIndexValue, band), bandLabel + " Q", &qSlider });
    }

    void Eq8ControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        curveEditor.setBounds (area.removeFromTop (140));
        area.removeFromTop (6);

        auto selectRow = area.removeFromTop (24);
        bandNameLabel.setBounds (selectRow.removeFromLeft (100));
        enableButton.setBounds (selectRow.removeFromLeft (50));
        selectRow.removeFromLeft (6);
        typeBox.setBounds (selectRow);
        area.removeFromTop (6);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto bandKnobRow = area.removeFromTop (106);
        const int bandKnobWidth = bandKnobRow.getWidth() / 3;
        layoutKnob (bandKnobRow.removeFromLeft (bandKnobWidth), freqLabel, freqSlider);
        layoutKnob (bandKnobRow.removeFromLeft (bandKnobWidth), gainLabel, gainSlider);
        layoutKnob (bandKnobRow, qLabel, qSlider);

        area.removeFromTop (6);

        auto globalKnobRow = area.removeFromTop (106);
        const int globalKnobWidth = globalKnobRow.getWidth() / 2;
        layoutKnob (globalKnobRow.removeFromLeft (globalKnobWidth), mixLabel, mixSlider);
        layoutKnob (globalKnobRow, outputLabel, outputSlider);
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

    MultibandConvolutionControlsPanel::MultibandConvolutionControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot)
        : apvtsRef (apvts), slotIndexValue (slotIndex),
          splitBar (*apvts.getParameter (multibandConvolutionParamId (slotIndex, MultibandConvolutionParam::splitHz1)),
                    *apvts.getParameter (multibandConvolutionParamId (slotIndex, MultibandConvolutionParam::splitHz2)),
                    [&rackSlot]() -> SpectrumAnalyzer*
                    {
                        // Fresh dynamic_cast every call (not cached) -- rackSlot's module instance
                        // can be destroyed/recreated at any time (type change, re-prepare), same
                        // caveat as IRWaveformComponent's own polling. nullptr just means "don't
                        // draw a spectrum right now" (e.g. before the module's first prepare()).
                        if (auto* m = dynamic_cast<MultibandConvolutionModule*> (rackSlot.getCurrentModule()))
                            return &m->getAnalyzer();
                        return nullptr;
                    })
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

        splitBar.setBounds (area.removeFromTop (76));
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

    MultipassControlsPanel::MultipassControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot)
        : splitBar (*apvts.getParameter (multipassParamId (slotIndex, MultipassParam::splitHz1)),
                     *apvts.getParameter (multipassParamId (slotIndex, MultipassParam::splitHz2)),
                     [&rackSlot]() -> SpectrumAnalyzer*
                     {
                         if (auto* m = dynamic_cast<MultipassModule*> (rackSlot.getCurrentModule()))
                             return &m->getAnalyzer();
                         return nullptr;
                     },
                     true)
    {
        addAndMakeVisible (splitBar);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

        const auto bandLabels = getMultipassBandLabels();
        for (int b = 0; b < kNumMultipassBands; ++b)
        {
            auto& slider = gainSliders[(size_t) b];
            auto& label = gainLabels[(size_t) b];

            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 56, 16);
            label.setText (bandLabels[b] + " Gain", juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (slider);
            addAndMakeVisible (label);

            gainAttachments[(size_t) b] = std::make_unique<SliderAttachment> (
                apvts, multipassBandParamId (slotIndex, b, MultipassBandParam::gain), slider);

            modTargets.push_back ({ multipassBandParamId (slotIndex, b, MultipassBandParam::gain), bandLabels[b] + " Gain", &slider });
        }
    }

    void MultipassControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        splitBar.setBounds (area.removeFromTop (50));
        area.removeFromTop (10);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        auto knobRow = area.removeFromTop (106);
        const int knobWidth = knobRow.getWidth() / kNumMultipassBands;
        for (int b = 0; b < kNumMultipassBands; ++b)
            layoutKnob (b == kNumMultipassBands - 1 ? knobRow : knobRow.removeFromLeft (knobWidth), gainLabels[(size_t) b], gainSliders[(size_t) b]);
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
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
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

        addAndMakeVisible (monoLegatoButton);
        addAndMakeVisible (glideButton);

        glideTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        glideTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        glideTimeLabel.setText ("Glide Time", juce::dontSendNotification);
        glideTimeLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (glideTimeSlider);
        addAndMakeVisible (glideTimeLabel);

        monoLegatoAttachment = std::make_unique<ButtonAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::monoLegato), monoLegatoButton);
        glideAttachment      = std::make_unique<ButtonAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::glide), glideButton);
        glideTimeAttachment  = std::make_unique<SliderAttachment> (apvts, threeOscParamId (slotIndex, ThreeOscParam::glideTimeMs), glideTimeSlider);

        modTargets.push_back ({ threeOscParamId (slotIndex, ThreeOscParam::glideTimeMs), "Glide Time", &glideTimeSlider });
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

        area.removeFromTop (6);
        auto monoRow = area.removeFromTop (24);
        monoLegatoButton.setBounds (monoRow.removeFromLeft (110));
        monoRow.removeFromLeft (8);
        auto glideCol = monoRow.removeFromLeft (110);
        glideButton.setBounds (glideCol);

        area.removeFromTop (4);
        auto glideTimeRow = area.removeFromTop (24);
        glideTimeRow.removeFromLeft (110 + 8); // align under the Glide button, past Mono/Legato + gap
        glideTimeLabel.setBounds (glideTimeRow.removeFromLeft (66));
        glideTimeSlider.setBounds (glideTimeRow);
    }
}
