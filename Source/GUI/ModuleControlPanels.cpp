#include "ModuleControlPanels.h"
#include "GGridLookAndFeel.h"
#include "../IR/IRLibrary.h"
#include "../Modules/MultibandConvolutionModule.h"
#include "../Modules/MultipassModule.h"
#include "../Modules/Eq8Module.h"
#include <functional>
#include <cmath>

namespace GGrid
{
    namespace
    {
        class WavetableSearchPopup : public juce::Component
        {
        public:
            struct Row { int tableIndex = 0; juce::String name; };

            class SearchEditor : public juce::TextEditor
            {
            public:
                std::function<void (int)> onArrowKey;
                std::function<void()> onReturnPressed;
                std::function<void()> onEscapePressed;

                bool keyPressed (const juce::KeyPress& key) override
                {
                    if (key == juce::KeyPress::upKey)     { if (onArrowKey) onArrowKey (-1); return true; }
                    if (key == juce::KeyPress::downKey)   { if (onArrowKey) onArrowKey (1);  return true; }
                    if (key == juce::KeyPress::returnKey) { if (onReturnPressed) onReturnPressed();  return true; }
                    if (key == juce::KeyPress::escapeKey) { if (onEscapePressed) onEscapePressed();  return true; }
                    return juce::TextEditor::keyPressed (key);
                }
            };

            class ResultsList : public juce::Component
            {
            public:
                std::function<void (int)> onRowClicked;
                std::function<void (int)> onRowHovered;

                void setRows (const std::vector<Row>* rowsIn)
                {
                    rows = rowsIn;
                    setSize (getWidth(), getContentHeight());
                    repaint();
                }

                void setSelectedIndex (int index)
                {
                    selectedIndex = index;
                    repaint();
                }

                int getContentHeight() const
                {
                    return rows == nullptr ? 0 : (int) rows->size() * rowHeight;
                }

                void paint (juce::Graphics& g) override
                {
                    g.fillAll (Palette::bg);
                    if (rows == nullptr)
                        return;

                    for (int i = 0; i < (int) rows->size(); ++i)
                    {
                        const auto bounds = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);
                        if (i == selectedIndex)
                        {
                            g.setColour (Palette::accent);
                            g.fillRect (bounds);
                        }

                        const auto& row = (*rows)[(size_t) i];
                        g.setColour (i == selectedIndex ? Palette::bright : Palette::dim);
                        g.setFont (juce::Font (juce::FontOptions (13.0f)));
                        g.drawText (row.name, bounds.reduced (10, 0), juce::Justification::centredLeft);
                    }
                }

                void mouseDown (const juce::MouseEvent& e) override
                {
                    const int row = e.y / rowHeight;
                    if (rows != nullptr && row >= 0 && row < (int) rows->size())
                        if (onRowClicked) onRowClicked (row);
                }

                void mouseMove (const juce::MouseEvent& e) override
                {
                    const int row = e.y / rowHeight;
                    if (rows != nullptr && row >= 0 && row < (int) rows->size() && row != selectedIndex)
                        if (onRowHovered) onRowHovered (row);
                }

                static constexpr int rowHeight = 22;

            private:
                const std::vector<Row>* rows = nullptr;
                int selectedIndex = -1;
            };

            WavetableSearchPopup (int currentIndexIn, std::function<void (int)> onSelectedIn)
                : currentIndex (currentIndexIn), onSelected (std::move (onSelectedIn))
            {
                allNames = WavetableLibrary::getCatalogDisplayNames();

                searchEditor.setTextToShowWhenEmpty ("Search wavetables...", Palette::dim);
                searchEditor.setColour (juce::TextEditor::backgroundColourId, Palette::dimmer);
                searchEditor.setColour (juce::TextEditor::textColourId, Palette::bright);
                searchEditor.setColour (juce::TextEditor::outlineColourId, Palette::dim);
                searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
                searchEditor.onTextChange = [this] { rebuildRows(); };
                searchEditor.onArrowKey = [this] (int delta) { moveSelection (delta); };
                searchEditor.onReturnPressed = [this] { commitSelection(); };
                searchEditor.onEscapePressed = [this]
                {
                    if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                        callout->dismiss();
                };
                addAndMakeVisible (searchEditor);

                resultsList.onRowClicked = [this] (int row) { selectedIndex = row; commitSelection(); };
                resultsList.onRowHovered = [this] (int row)
                {
                    selectedIndex = row;
                    resultsList.setSelectedIndex (selectedIndex);
                };
                viewport.setViewedComponent (&resultsList, false);
                viewport.setScrollBarsShown (true, false);
                addAndMakeVisible (viewport);

                rebuildRows();
                setSize (360, 360);
            }

            void visibilityChanged() override
            {
                if (isVisible())
                    searchEditor.grabKeyboardFocus();
            }

            void resized() override
            {
                auto area = getLocalBounds().reduced (8);
                searchEditor.setBounds (area.removeFromTop (30));
                area.removeFromTop (8);
                viewport.setBounds (area);
                resultsList.setSize (viewport.getWidth() - 2, resultsList.getContentHeight());
            }

        private:
            void rebuildRows()
            {
                rows.clear();
                const auto query = searchEditor.getText().trim().toLowerCase();
                for (int i = 0; i < allNames.size(); ++i)
                    if (query.isEmpty() || allNames[i].toLowerCase().contains (query))
                        rows.push_back ({ i, allNames[i] });

                selectedIndex = rows.empty() ? -1 : 0;
                for (int i = 0; i < (int) rows.size(); ++i)
                    if (rows[(size_t) i].tableIndex == currentIndex)
                    {
                        selectedIndex = i;
                        break;
                    }

                resultsList.setRows (&rows);
                resultsList.setSelectedIndex (selectedIndex);
                resized();
            }

            void moveSelection (int delta)
            {
                if (rows.empty())
                    return;

                selectedIndex = juce::jlimit (0, (int) rows.size() - 1, selectedIndex + delta);
                resultsList.setSelectedIndex (selectedIndex);
                viewport.setViewPosition (0, juce::jlimit (0, juce::jmax (0, resultsList.getContentHeight() - viewport.getHeight()),
                                                           selectedIndex * ResultsList::rowHeight - viewport.getHeight() / 2));
            }

            void commitSelection()
            {
                if (selectedIndex < 0 || selectedIndex >= (int) rows.size())
                    return;

                if (onSelected)
                    onSelected (rows[(size_t) selectedIndex].tableIndex);

                if (auto* callout = findParentComponentOfClass<juce::CallOutBox>())
                    callout->dismiss();
            }

            juce::StringArray allNames;
            std::vector<Row> rows;
            SearchEditor searchEditor;
            juce::Viewport viewport;
            ResultsList resultsList;
            int currentIndex = 0;
            int selectedIndex = -1;
            std::function<void (int)> onSelected;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableSearchPopup)
        };
    }

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

    FilterResponseEditor::FilterResponseEditor (juce::AudioProcessorValueTreeState& apvts,
                                                juce::String frequencyParamId,
                                                juce::String resonanceParamId,
                                                juce::String driveParamId,
                                                juce::String modeParamId,
                                                juce::String morphParamId,
                                                juce::String distortionParamId)
        : frequencyParam (dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (frequencyParamId))),
          resonanceParam (dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (resonanceParamId))),
          driveParam (dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (driveParamId))),
          morphParam (dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (morphParamId))),
          modeParam (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (modeParamId))),
          distortionParam (dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (distortionParamId)))
    {
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        startTimerHz (24);
    }

    FilterResponseEditor::~FilterResponseEditor()
    {
        stopTimer();
    }

    float FilterResponseEditor::getFrequency() const
    {
        return frequencyParam != nullptr ? frequencyParam->get() : 1000.0f;
    }

    float FilterResponseEditor::getResonance01() const
    {
        return resonanceParam != nullptr ? resonanceParam->convertTo0to1 (resonanceParam->get()) : 0.35f;
    }

    float FilterResponseEditor::getDrive01() const
    {
        return driveParam != nullptr ? driveParam->convertTo0to1 (driveParam->get()) : 0.0f;
    }

    float FilterResponseEditor::getMorph01() const
    {
        return morphParam != nullptr ? morphParam->convertTo0to1 (morphParam->get()) : 0.0f;
    }

    int FilterResponseEditor::getMode() const
    {
        if (modeParam == nullptr)
            return 0;

        const int index = modeParam->getIndex();
        if (modeParam->choices.size() <= 4)
            return index;

        if (index == 8) return 1; // Ladder High Pass
        if (index == 9) return 2; // Formant
        if (index >= 4 && index <= 6) return 3; // comb/allpass character
        return juce::jlimit (0, 3, index);
    }

    int FilterResponseEditor::getDistortion() const
    {
        return distortionParam != nullptr ? distortionParam->getIndex() : 0;
    }

    void FilterResponseEditor::setFloatParam (juce::AudioParameterFloat* param, float value)
    {
        if (param == nullptr)
            return;

        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float FilterResponseEditor::responseAt (float hz, float cutoff, float resonance01, float drive01, float morph01, int mode, int distortion) const
    {
        const float x = juce::jlimit (0.05f, 20.0f, hz / juce::jmax (20.0f, cutoff));
        float slope = 2.0f + drive01 * 2.5f;
        float peakScale = 1.0f + resonance01 * (5.0f + drive01 * 5.0f);
        float tilt = 1.0f;
        float ripple = 1.0f;

        if (distortionParam != nullptr)
        {
            switch (distortion)
            {
                case 1: slope += 1.6f * morph01; peakScale += drive01 * 3.0f; break; // Hard Clip
                case 2: ripple += std::sin (std::log2 (x) * 12.0f) * drive01 * morph01 * 0.18f; break; // Sine Fold
                case 3: ripple += std::sin (std::log2 (x) * 18.0f) * drive01 * morph01 * 0.28f; slope += morph01; break; // Foldback
                case 4: tilt = std::pow (x, morph01 * drive01 * 0.18f); peakScale += morph01 * 1.5f; break; // Asymmetric
                case 5: tilt = 1.0f / std::pow (x, morph01 * drive01 * 0.12f); slope -= morph01 * 0.45f; break; // Warm
                case 6: peakScale += drive01 * (2.0f + morph01 * 4.0f); break; // Saturated
                case 7: tilt = std::pow (x, -0.08f + morph01 * drive01 * 0.28f); peakScale += morph01 * 2.0f; break; // Bias
                case 8: slope += 2.4f * morph01; ripple += std::sin (std::log2 (x) * 8.0f) * drive01 * 0.12f; break; // Clipped
                default: peakScale += morph01 * drive01 * 1.5f; break;
            }
        }

        slope = juce::jmax (0.8f, slope);
        ripple = juce::jlimit (0.35f, 1.75f, ripple);
        const float bell = std::exp (-std::pow (std::log2 (x) * (2.4f + resonance01 * 3.6f), 2.0f)) * resonance01 * peakScale;
        const float low = 1.0f / std::sqrt (1.0f + std::pow (x, slope * 2.0f));
        const float high = 1.0f / std::sqrt (1.0f + std::pow (1.0f / x, slope * 2.0f));
        const float band = std::exp (-std::abs (std::log2 (x)) * (1.2f + (1.0f - resonance01) * 4.0f));
        const float notch = 1.0f - band * (0.75f + resonance01 * 0.2f);

        switch (mode)
        {
            case 1:  return (high + bell * 0.45f) * tilt * ripple;
            case 2:  return band * (0.5f + resonance01 * 1.4f) * tilt * ripple;
            case 3:  return (notch + bell * 0.25f) * tilt * ripple;
            default: return (low + bell * 0.35f) * tilt * ripple;
        }
    }

    void FilterResponseEditor::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff10141b));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colour (0xff303846));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);

        auto plot = r.reduced (10.0f, 8.0f);
        g.setColour (juce::Colour (0xff252c36));
        for (float t : { 0.25f, 0.5f, 0.75f })
        {
            g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * t), plot.getY(), plot.getBottom());
            g.drawHorizontalLine ((int) (plot.getY() + plot.getHeight() * t), plot.getX(), plot.getRight());
        }

        const float cutoff = getFrequency();
        const float resonance01 = getResonance01();
        const float drive01 = getDrive01();
        const float morph01 = getMorph01();
        const int mode = getMode();
        const int distortion = getDistortion();

        juce::Path fill, curve;
        for (int x = 0; x < (int) plot.getWidth(); ++x)
        {
            const float norm = (float) x / juce::jmax (1.0f, plot.getWidth() - 1.0f);
            const float hz = 20.0f * std::pow (8000.0f / 20.0f, norm);
            const float db = juce::Decibels::gainToDecibels (juce::jmax (0.001f, responseAt (hz, cutoff, resonance01, drive01, morph01, mode, distortion)));
            const float yNorm = juce::jmap (juce::jlimit (-36.0f, 18.0f, db), -36.0f, 18.0f, 1.0f, 0.0f);
            const float px = plot.getX() + (float) x;
            const float py = plot.getY() + yNorm * plot.getHeight();

            if (x == 0)
            {
                curve.startNewSubPath (px, py);
                fill.startNewSubPath (px, plot.getBottom());
                fill.lineTo (px, py);
            }
            else
            {
                curve.lineTo (px, py);
                fill.lineTo (px, py);
            }
        }
        fill.lineTo (plot.getRight(), plot.getBottom());
        fill.closeSubPath();

        const auto accent = juce::Colour (0xffb7c8ff);
        g.setColour (accent.withAlpha (0.13f + drive01 * 0.14f));
        g.fillPath (fill);
        g.setColour (accent.withAlpha (0.92f));
        g.strokePath (curve, juce::PathStrokeType (2.0f + drive01 * 1.2f));

        const float cutoffNorm = std::log (cutoff / 20.0f) / std::log (8000.0f / 20.0f);
        const float handleX = plot.getX() + juce::jlimit (0.0f, 1.0f, cutoffNorm) * plot.getWidth();
        const float handleY = plot.getBottom() - resonance01 * plot.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawLine (handleX, plot.getY(), handleX, plot.getBottom(), 1.0f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ handleX, handleY }));
    }

    void FilterResponseEditor::mouseDown (const juce::MouseEvent& e)
    {
        if (frequencyParam != nullptr) frequencyParam->beginChangeGesture();
        if (resonanceParam != nullptr) resonanceParam->beginChangeGesture();
        if (driveParam != nullptr) driveParam->beginChangeGesture();
        updateFromMouse (e.position, e.mods);
    }

    void FilterResponseEditor::mouseDrag (const juce::MouseEvent& e)
    {
        updateFromMouse (e.position, e.mods);
    }

    void FilterResponseEditor::mouseUp (const juce::MouseEvent&)
    {
        if (frequencyParam != nullptr) frequencyParam->endChangeGesture();
        if (resonanceParam != nullptr) resonanceParam->endChangeGesture();
        if (driveParam != nullptr) driveParam->endChangeGesture();
    }

    void FilterResponseEditor::updateFromMouse (juce::Point<float> pos, const juce::ModifierKeys& mods)
    {
        auto plot = getLocalBounds().toFloat().reduced (11.0f, 9.0f);
        const float x01 = juce::jlimit (0.0f, 1.0f, (pos.x - plot.getX()) / juce::jmax (1.0f, plot.getWidth()));
        const float y01 = juce::jlimit (0.0f, 1.0f, 1.0f - (pos.y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()));

        if (mods.isAltDown() || mods.isCommandDown() || mods.isRightButtonDown())
        {
            if (driveParam != nullptr)
                setFloatParam (driveParam, driveParam->convertFrom0to1 (y01));
        }
        else
        {
            if (frequencyParam != nullptr)
                setFloatParam (frequencyParam, 20.0f * std::pow (8000.0f / 20.0f, x01));
            if (resonanceParam != nullptr)
                setFloatParam (resonanceParam, resonanceParam->convertFrom0to1 (y01));
        }

        repaint();
    }

    FilterControlsPanel::FilterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : responseEditor (apvts,
                          filterParamId (slotIndex, FilterParam::frequency),
                          filterParamId (slotIndex, FilterParam::resonance),
                          filterParamId (slotIndex, FilterParam::drive),
                          filterParamId (slotIndex, FilterParam::type))
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
        setupRotary (driveSlider, driveLabel, "Drive");
        setupRotary (feedbackSlider, feedbackLabel, "Feedback");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");
        addAndMakeVisible (responseEditor);

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
        driveAttachment     = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::drive), driveSlider);
        feedbackAttachment  = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::feedback), feedbackSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::mix), mixSlider);
        outputAttachment    = std::make_unique<SliderAttachment> (apvts, filterParamId (slotIndex, FilterParam::output), outputSlider);
        typeAttachment      = std::make_unique<ComboBoxAttachment> (apvts, filterParamId (slotIndex, FilterParam::type), typeBox);

        modTargets = {
            { filterParamId (slotIndex, FilterParam::frequency), "Frequency", &frequencySlider },
            { filterParamId (slotIndex, FilterParam::resonance), "Resonance", &resonanceSlider },
            { filterParamId (slotIndex, FilterParam::drive),      "Drive",      &driveSlider },
            { filterParamId (slotIndex, FilterParam::feedback),  "Feedback",  &feedbackSlider },
            { filterParamId (slotIndex, FilterParam::mix),       "Mix",       &mixSlider },
            { filterParamId (slotIndex, FilterParam::output),    "Output",    &outputSlider },
        };
    }

    void FilterControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16); // room for a mod-destination nub, clear of both the label and the knob
            slider.setBounds (col);
        };

        responseEditor.setBounds (area.removeFromTop (92));
        area.removeFromTop (6);

        auto topKnobRow = area.removeFromTop (106);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), frequencyLabel, frequencySlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), resonanceLabel, resonanceSlider);
        layoutKnob (topKnobRow, driveLabel, driveSlider);

        area.removeFromTop (6);
        auto bottomKnobRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomKnobRow.getWidth() / 3;
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), feedbackLabel, feedbackSlider);
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), mixLabel, mixSlider);
        layoutKnob (bottomKnobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);

        typeLabel.setBounds (left.removeFromTop (16));
        typeBox.setBounds (left.removeFromTop (24));
    }

    NonlinearFilterControlsPanel::NonlinearFilterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
        : responseEditor (apvts,
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::frequency),
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::resonance),
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::drive),
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mode),
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::morph),
                          nonlinearFilterParamId (slotIndex, NonlinearFilterParam::distortion))
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
        setupRotary (driveSlider, driveLabel, "Drive");
        setupRotary (morphSlider, morphLabel, "Morph");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        modeLabel.setText ("Mode", juce::dontSendNotification);
        modeLabel.setJustificationType (juce::Justification::centred);
        distortionLabel.setText ("Distortion", juce::dontSendNotification);
        distortionLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getNonlinearFilterModeChoices())
            modeBox.addItem (choice, itemId++);
        itemId = 1;
        for (auto& choice : getNonlinearFilterDistortionChoices())
            distortionBox.addItem (choice, itemId++);

        addAndMakeVisible (responseEditor);
        addAndMakeVisible (modeBox);
        addAndMakeVisible (modeLabel);
        addAndMakeVisible (distortionBox);
        addAndMakeVisible (distortionLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        frequencyAttachment = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::frequency), frequencySlider);
        resonanceAttachment = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::resonance), resonanceSlider);
        driveAttachment     = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::drive), driveSlider);
        morphAttachment     = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::morph), morphSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mix), mixSlider);
        outputAttachment    = std::make_unique<SliderAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::output), outputSlider);
        modeAttachment      = std::make_unique<ComboBoxAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mode), modeBox);
        distortionAttachment = std::make_unique<ComboBoxAttachment> (apvts, nonlinearFilterParamId (slotIndex, NonlinearFilterParam::distortion), distortionBox);

        modTargets = {
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::frequency), "Frequency", &frequencySlider },
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::resonance), "Resonance", &resonanceSlider },
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::drive),     "Drive",     &driveSlider },
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::morph),     "Morph",     &morphSlider },
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::mix),       "Mix",       &mixSlider },
            { nonlinearFilterParamId (slotIndex, NonlinearFilterParam::output),    "Output",    &outputSlider },
        };
    }

    void NonlinearFilterControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);

        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16);
            slider.setBounds (col);
        };

        responseEditor.setBounds (area.removeFromTop (92));
        area.removeFromTop (6);

        auto topKnobRow = area.removeFromTop (106);
        const int topKnobWidth = topKnobRow.getWidth() / 3;
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), frequencyLabel, frequencySlider);
        layoutKnob (topKnobRow.removeFromLeft (topKnobWidth), resonanceLabel, resonanceSlider);
        layoutKnob (topKnobRow, driveLabel, driveSlider);

        area.removeFromTop (6);
        auto bottomKnobRow = area.removeFromTop (106);
        const int bottomKnobWidth = bottomKnobRow.getWidth() / 3;
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), morphLabel, morphSlider);
        layoutKnob (bottomKnobRow.removeFromLeft (bottomKnobWidth), mixLabel, mixSlider);
        layoutKnob (bottomKnobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);
        auto right = bottomRow.reduced (4, 0);

        modeLabel.setBounds (left.removeFromTop (16));
        modeBox.setBounds (left.removeFromTop (24));
        distortionLabel.setBounds (right.removeFromTop (16));
        distortionBox.setBounds (right.removeFromTop (24));
    }

    MackityControlsPanel::MackityControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        setupRotary (inputSlider, inputLabel, "Input");
        setupRotary (padSlider, padLabel, "Out Pad");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        inputAttachment  = std::make_unique<SliderAttachment> (apvts, mackityParamId (slotIndex, MackityParam::input), inputSlider);
        padAttachment    = std::make_unique<SliderAttachment> (apvts, mackityParamId (slotIndex, MackityParam::pad), padSlider);
        mixAttachment    = std::make_unique<SliderAttachment> (apvts, mackityParamId (slotIndex, MackityParam::mix), mixSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, mackityParamId (slotIndex, MackityParam::output), outputSlider);

        modTargets = {
            { mackityParamId (slotIndex, MackityParam::input),  "Input",  &inputSlider },
            { mackityParamId (slotIndex, MackityParam::pad),    "Out Pad", &padSlider },
            { mackityParamId (slotIndex, MackityParam::mix),    "Mix",    &mixSlider },
            { mackityParamId (slotIndex, MackityParam::output), "Output", &outputSlider },
        };
    }

    void MackityControlsPanel::resized()
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

        layoutKnob (knobRow.removeFromLeft (knobWidth), inputLabel, inputSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), padLabel, padSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);
    }

    ShimmerReverbControlsPanel::ShimmerReverbControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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

        setupRotary (sizeSlider, sizeLabel, "Size");
        setupRotary (feedbackSlider, feedbackLabel, "Feedback");
        setupRotary (diffusionSlider, diffusionLabel, "Diffusion");
        setupRotary (shiftSlider, shiftLabel, "Shift");
        setupRotary (modRateSlider, modRateLabel, "Mod Rate");
        setupRotary (modDepthSlider, modDepthLabel, "Mod Depth");
        setupRotary (lowCutSlider, lowCutLabel, "Low Cut");
        setupRotary (highCutSlider, highCutLabel, "High Cut");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        pitchModeLabel.setJustificationType (juce::Justification::centred);
        colorLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (pitchModeLabel);
        addAndMakeVisible (colorLabel);

        int itemId = 1;
        for (auto& choice : getShimmerReverbPitchModeChoices())
            pitchModeBox.addItem (choice, itemId++);
        itemId = 1;
        for (auto& choice : getShimmerReverbColorChoices())
            colorBox.addItem (choice, itemId++);
        addAndMakeVisible (pitchModeBox);
        addAndMakeVisible (colorBox);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
        sizeAttachment      = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::size), sizeSlider);
        feedbackAttachment  = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::feedback), feedbackSlider);
        diffusionAttachment = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::diffusion), diffusionSlider);
        shiftAttachment     = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::shift), shiftSlider);
        modRateAttachment   = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::modRate), modRateSlider);
        modDepthAttachment  = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::modDepth), modDepthSlider);
        lowCutAttachment    = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::lowCut), lowCutSlider);
        highCutAttachment   = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::highCut), highCutSlider);
        mixAttachment       = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::mix), mixSlider);
        outputAttachment    = std::make_unique<SliderAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::output), outputSlider);
        pitchModeAttachment = std::make_unique<ComboBoxAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::pitchMode), pitchModeBox);
        colorAttachment     = std::make_unique<ComboBoxAttachment> (apvts, shimmerReverbParamId (slotIndex, ShimmerReverbParam::color), colorBox);

        modTargets = {
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::size),      "Size",      &sizeSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::feedback),  "Feedback",  &feedbackSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::diffusion), "Diffusion", &diffusionSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::shift),     "Shift",     &shiftSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::modRate),   "Mod Rate",  &modRateSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::modDepth),  "Mod Depth", &modDepthSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::lowCut),    "Low Cut",   &lowCutSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::highCut),   "High Cut",  &highCutSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::mix),       "Mix",       &mixSlider },
            { shimmerReverbParamId (slotIndex, ShimmerReverbParam::output),    "Output",    &outputSlider },
        };
    }

    void ShimmerReverbControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (4);
        auto layoutKnob = [&] (juce::Rectangle<int> col, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (col.removeFromTop (16));
            col.removeFromTop (16);
            slider.setBounds (col);
        };

        auto row1 = area.removeFromTop (106);
        const int w1 = row1.getWidth() / 4;
        layoutKnob (row1.removeFromLeft (w1), sizeLabel, sizeSlider);
        layoutKnob (row1.removeFromLeft (w1), feedbackLabel, feedbackSlider);
        layoutKnob (row1.removeFromLeft (w1), diffusionLabel, diffusionSlider);
        layoutKnob (row1, shiftLabel, shiftSlider);

        area.removeFromTop (6);
        auto row2 = area.removeFromTop (106);
        const int w2 = row2.getWidth() / 3;
        layoutKnob (row2.removeFromLeft (w2), modRateLabel, modRateSlider);
        layoutKnob (row2.removeFromLeft (w2), modDepthLabel, modDepthSlider);
        layoutKnob (row2, lowCutLabel, lowCutSlider);

        area.removeFromTop (6);
        auto row3 = area.removeFromTop (106);
        const int w3 = row3.getWidth() / 3;
        layoutKnob (row3.removeFromLeft (w3), highCutLabel, highCutSlider);
        layoutKnob (row3.removeFromLeft (w3), mixLabel, mixSlider);
        layoutKnob (row3, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto selectorRow = area.removeFromTop (44);
        auto left = selectorRow.removeFromLeft (selectorRow.getWidth() / 2).reduced (4, 0);
        auto right = selectorRow.reduced (4, 0);
        pitchModeLabel.setBounds (left.removeFromTop (16));
        pitchModeBox.setBounds (left.removeFromTop (24));
        colorLabel.setBounds (right.removeFromTop (16));
        colorBox.setBounds (right.removeFromTop (24));
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

    SpectralClipperControlsPanel::SpectralClipperControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex)
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
        setupRotary (ceilingSlider, ceilingLabel, "Ceiling");
        setupRotary (mixSlider, mixLabel, "Mix");
        setupRotary (outputSlider, outputLabel, "Output");

        shapeLabel.setText ("Shape", juce::dontSendNotification);
        shapeLabel.setJustificationType (juce::Justification::centred);

        int itemId = 1;
        for (auto& choice : getSpectralClipperShapeChoices())
            shapeBox.addItem (choice, itemId++);

        addAndMakeVisible (shapeBox);
        addAndMakeVisible (shapeLabel);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        driveAttachment   = std::make_unique<SliderAttachment> (apvts, spectralClipperParamId (slotIndex, SpectralClipperParam::drive), driveSlider);
        ceilingAttachment = std::make_unique<SliderAttachment> (apvts, spectralClipperParamId (slotIndex, SpectralClipperParam::ceiling), ceilingSlider);
        mixAttachment     = std::make_unique<SliderAttachment> (apvts, spectralClipperParamId (slotIndex, SpectralClipperParam::mix), mixSlider);
        outputAttachment  = std::make_unique<SliderAttachment> (apvts, spectralClipperParamId (slotIndex, SpectralClipperParam::output), outputSlider);
        shapeAttachment   = std::make_unique<ComboBoxAttachment> (apvts, spectralClipperParamId (slotIndex, SpectralClipperParam::shape), shapeBox);

        modTargets = {
            { spectralClipperParamId (slotIndex, SpectralClipperParam::drive),   "Drive",   &driveSlider },
            { spectralClipperParamId (slotIndex, SpectralClipperParam::ceiling), "Ceiling", &ceilingSlider },
            { spectralClipperParamId (slotIndex, SpectralClipperParam::mix),     "Mix",     &mixSlider },
            { spectralClipperParamId (slotIndex, SpectralClipperParam::output),  "Output",  &outputSlider },
        };
    }

    void SpectralClipperControlsPanel::resized()
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

        layoutKnob (knobRow.removeFromLeft (knobWidth), driveLabel, driveSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), ceilingLabel, ceilingSlider);
        layoutKnob (knobRow.removeFromLeft (knobWidth), mixLabel, mixSlider);
        layoutKnob (knobRow, outputLabel, outputSlider);

        area.removeFromTop (6);
        auto bottomRow = area.removeFromTop (44);
        auto left = bottomRow.removeFromLeft (bottomRow.getWidth() / 2).reduced (4, 0);

        shapeLabel.setBounds (left.removeFromTop (16));
        shapeBox.setBounds (left.removeFromTop (24));
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

    WavetableSynthPreviewComponent::WavetableSynthPreviewComponent (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : apvts (apvtsIn), slotIndex (slotIndexIn)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (15);
    }

    WavetableSynthPreviewComponent::~WavetableSynthPreviewComponent()
    {
        stopTimer();
    }

    std::shared_ptr<const WavetableLibrary::Table> WavetableSynthPreviewComponent::getTable()
    {
        const auto* tableParam = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, generatorIndex, WavetableSynthGenParam::table));
        const int wanted = tableParam != nullptr ? (int) tableParam->load() : 0;
        if (wanted != loadedIndex || table == nullptr)
        {
            table = WavetableLibrary::loadTable (wanted);
            loadedIndex = wanted;
        }
        return table;
    }

    void WavetableSynthPreviewComponent::setGeneratorIndex (int index)
    {
        const int next = juce::jlimit (0, kNumWavetableSynthGenerators - 1, index);
        if (generatorIndex == next)
            return;

        generatorIndex = next;
        loadedIndex = -1;
        repaint();
    }

    void WavetableSynthPreviewComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg.darker (0.35f));
        g.fillRect (bounds);
        g.setColour (Palette::dim.withAlpha (0.55f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

        const auto currentTable = getTable();
        if (currentTable == nullptr || ! currentTable->isValid())
            return;

        auto waveArea = bounds.reduced (14.0f, 12.0f);
        const auto* frameParam = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, generatorIndex, WavetableSynthGenParam::frame));
        const auto* smoothParam = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, generatorIndex, WavetableSynthGenParam::smooth));
        const auto* unisonParam = apvts.getRawParameterValue (wavetableSynthParamId (slotIndex, WavetableSynthParam::unison));
        const auto* spreadParam = apvts.getRawParameterValue (wavetableSynthParamId (slotIndex, WavetableSynthParam::spread));
        const auto* algorithmParam = apvts.getRawParameterValue (wavetableSynthParamId (slotIndex, WavetableSynthParam::algorithm));
        const auto* multiplierParam = apvts.getRawParameterValue (wavetableSynthParamId (slotIndex, WavetableSynthParam::multiplier));
        const float frame = juce::jlimit (0.0f, (float) currentTable->numFrames - 1.0f, (frameParam != nullptr ? frameParam->load() : 1.0f) - 1.0f);
        const float smooth = (smoothParam != nullptr ? smoothParam->load() : 0.0f) / 100.0f;
        const int unison = juce::jlimit (1, 16, unisonParam != nullptr ? (int) std::round (unisonParam->load()) : 1);
        const float spread01 = juce::jlimit (0.0f, 1.0f, (spreadParam != nullptr ? spreadParam->load() : 0.0f) / 100.0f);
        const int spreadMode = juce::jlimit (0, getWavetableSynthAlgorithmChoices().size() - 1, algorithmParam != nullptr ? (int) algorithmParam->load() : 0);
        const int multiplier = juce::jlimit (1, 8, multiplierParam != nullptr ? (int) multiplierParam->load() + 1 : 1);

        auto stackArea = waveArea.removeFromTop (juce::jmax (80.0f, waveArea.getHeight() * 0.72f));
        waveArea.removeFromTop (8.0f);
        auto stripArea = waveArea;

        g.setColour (Palette::dimmer.withAlpha (0.5f));
        for (int i = 0; i < 5; ++i)
        {
            const float y = juce::jmap ((float) i, 0.0f, 4.0f, stackArea.getBottom(), stackArea.getY());
            g.drawHorizontalLine ((int) y, stackArea.getX(), stackArea.getRight());
        }

        const float frame01Selected = currentTable->numFrames <= 1 ? 0.0f : frame / (float) (currentTable->numFrames - 1);
        const int framesToDraw = juce::jlimit (8, 34, currentTable->numFrames);
        for (int f = framesToDraw - 1; f >= 0; --f)
        {
            const float frame01 = framesToDraw <= 1 ? 0.0f : (float) f / (float) (framesToDraw - 1);
            const float tableFrame = frame01 * (float) (currentTable->numFrames - 1);
            const float xOffset = frame01 * stackArea.getWidth() * 0.28f;
            const float yOffset = frame01 * stackArea.getHeight() * 0.30f;
            const float alpha = f == 0 ? 0.95f : juce::jmap (frame01, 0.16f, 0.02f);
            juce::Path path;
            constexpr int points = 120;
            for (int i = 0; i < points; ++i)
            {
                const float x01 = (float) i / (float) (points - 1);
                const float y = currentTable->sample (tableFrame, x01, smooth);
                const auto x = juce::jmap (x01, stackArea.getX() + xOffset, stackArea.getRight() - stackArea.getWidth() * 0.28f + xOffset);
                const auto py = stackArea.getCentreY() - y * stackArea.getHeight() * 0.22f - yOffset + stackArea.getHeight() * 0.14f;
                if (i == 0) path.startNewSubPath (x, py);
                else        path.lineTo (x, py);
            }

            const bool selectedBand = std::abs (tableFrame - frame) < (float) currentTable->numFrames / (float) framesToDraw;
            g.setColour ((selectedBand ? Palette::bright : Palette::bright.withMultipliedSaturation (0.7f)).withAlpha (selectedBand ? 0.85f : alpha));
            g.strokePath (path, juce::PathStrokeType (selectedBand ? 2.0f : 0.8f));
        }

        const float markerX = juce::jmap (frame01Selected, stackArea.getX(), stackArea.getRight());
        g.setColour (Palette::bright.withAlpha (0.75f));
        g.drawLine (markerX, stackArea.getY(), markerX, stackArea.getBottom(), 1.4f);
        g.setColour (Palette::bright.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (markerX - 2.0f, stackArea.getY(), 4.0f, stackArea.getHeight()));

        g.setColour (Palette::dim.withAlpha (0.7f));
        g.drawRoundedRectangle (stripArea.reduced (0.5f), 4.0f, 1.0f);

        juce::Path path;
        constexpr int points = 220;
        for (int i = 0; i < points; ++i)
        {
            const float x01 = (float) i / (float) (points - 1);
            const float y = currentTable->sample (frame, x01, smooth);
            const auto x = juce::jmap (x01, stripArea.getX() + 8.0f, stripArea.getRight() - 8.0f);
            const auto py = juce::jmap (y, -1.0f, 1.0f, stripArea.getBottom() - 5.0f, stripArea.getY() + 5.0f);
            if (i == 0) path.startNewSubPath (x, py);
            else        path.lineTo (x, py);
        }

        g.setColour (Palette::bright);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        if (unison > 1 && spread01 > 0.0f)
        {
            auto laneVisualOffset = [] (int mode, int lane, int count, float amount, int mult)
            {
                const float lane01 = (float) lane / (float) juce::jmax (1, count - 1);
                const float centred = lane01 * 2.0f - 1.0f;
                if (mode == 1)  return std::sin (centred * juce::MathConstants<float>::halfPi) * amount * 8.0f;
                if (mode == 2)  return std::copysign (centred * centred, centred) * amount * 10.0f;
                if (mode == 3)  return lane01 * amount * 14.0f;
                if (mode == 4)  return std::round (centred * (float) mult) * amount * 4.0f;
                if (mode == 5 || mode >= 8) return std::round (centred * (float) juce::jmax (2, mult)) * amount * 6.0f;
                if (mode == 6 || mode == 7) return std::round (centred * 5.0f) * amount * 3.0f;
                return centred * amount * 10.0f;
            };

            for (int lane = 0; lane < unison; ++lane)
            {
                const float lane01 = (float) lane / (float) (unison - 1);
                const float centered = lane01 * 2.0f - 1.0f;
                const float yOffset = laneVisualOffset (spreadMode, lane, unison, spread01, multiplier);
                const float phaseOffset = (spreadMode == 3 ? std::sin ((float) (lane + 1) * 12.9898f) : centered)
                                          * spread01 * 0.0125f * (float) multiplier;
                juce::Path lanePath;
                for (int i = 0; i < points; ++i)
                {
                    const float x01 = (float) i / (float) (points - 1);
                    const float y = currentTable->sample (frame, x01 + phaseOffset, smooth);
                    const auto x = juce::jmap (x01, stripArea.getX() + 8.0f, stripArea.getRight() - 8.0f);
                    const auto py = juce::jmap (y, -1.0f, 1.0f, stripArea.getBottom() - 5.0f, stripArea.getY() + 5.0f) + yOffset;
                    if (i == 0) lanePath.startNewSubPath (x, py);
                    else        lanePath.lineTo (x, py);
                }
                g.setColour (Palette::bright.withAlpha (0.14f));
                g.strokePath (lanePath, juce::PathStrokeType (1.0f));
            }
        }

        g.setColour (Palette::bright.withAlpha (0.9f));
        g.fillEllipse (stripArea.getX() + 6.0f + frame01Selected * (stripArea.getWidth() - 12.0f) - 3.0f,
                       stripArea.getBottom() - 9.0f, 6.0f, 6.0f);
    }

    WavetableSynthControlsPanel::WavetableSynthControlsPanel (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : preview (apvtsIn, slotIndexIn), apvts (apvtsIn), slotIndex (slotIndexIn)
    {
        addAndMakeVisible (preview);
        addGeneratorButton.onClick = [this] { enableNextGenerator(); };
        addGeneratorButton.setColour (juce::TextButton::buttonColourId, Palette::bg.darker (0.2f));
        addGeneratorButton.setColour (juce::TextButton::buttonOnColourId, Palette::bright.withAlpha (0.18f));
        addGeneratorButton.setColour (juce::TextButton::textColourOffId, Palette::bright);

        const auto genLabels = getWavetableSynthGeneratorLabels();
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            auto& button = generatorButtons[(size_t) gen];
            button.setButtonText (genLabels[gen]);
            button.setClickingTogglesState (false);
            button.onClick = [this, gen] { selectGenerator (gen); };
            button.setColour (juce::TextButton::buttonColourId, Palette::bg.darker (0.1f));
            button.setColour (juce::TextButton::buttonOnColourId, Palette::bright.withAlpha (0.16f));
            button.setColour (juce::TextButton::textColourOffId, Palette::bright);
            button.setColour (juce::TextButton::textColourOnId, Palette::bright);
        }

        generatorEnabledButton.setButtonText ("On");
        generatorEnabledButton.onClick = [this] { refreshGeneratorButtons(); };

        int itemId = 1;
        for (auto& name : WavetableLibrary::getCatalogDisplayNames())
            tableBox.addItem (name, itemId++);
        addAndMakeVisible (prevTableButton);
        addAndMakeVisible (nextTableButton);
        addAndMakeVisible (searchTableButton);
        addAndMakeVisible (tableLabel);
        addAndMakeVisible (tableBox);
        tableLabel.setJustificationType (juce::Justification::centredLeft);
        tableBox.setTextWhenNothingSelected ("Choose wavetable...");
        tableNameLabel.setJustificationType (juce::Justification::centredLeft);
        tableNameLabel.setColour (juce::Label::textColourId, Palette::bright);
        prevTableButton.onClick = [this] { stepSelectedTable (-1); };
        nextTableButton.onClick = [this] { stepSelectedTable (1); };
        searchTableButton.onClick = [this] { showTableSearchPopup(); };

        const auto outputNames = getWavetableSynthOutputChoices();
        for (int out = 0; out < kNumWavetableSynthOutputs; ++out)
        {
            auto& button = outputButtons[(size_t) out];
            button.setButtonText (outputNames[out]);
            button.setClickingTogglesState (false);
            button.onClick = [this, out] { setSelectedGeneratorOutput (out); };
            button.setColour (juce::TextButton::buttonColourId, Palette::bg.darker (0.1f));
            button.setColour (juce::TextButton::buttonOnColourId, Palette::bright.withAlpha (0.18f));
            button.setColour (juce::TextButton::textColourOffId, Palette::bright);
            button.setColour (juce::TextButton::textColourOnId, Palette::bright);
        }

        int algorithmId = 1;
        for (auto& choice : getWavetableSynthAlgorithmChoices())
            algorithmBox.addItem (choice, algorithmId++);
        int multiplierId = 1;
        for (auto& choice : getWavetableSynthMultiplierChoices())
            multiplierBox.addItem (choice, multiplierId++);
        algorithmLabel.setText ("Algorithm", juce::dontSendNotification);
        algorithmLabel.setJustificationType (juce::Justification::centredLeft);
        multiplierLabel.setText ("Multiply", juce::dontSendNotification);
        multiplierLabel.setJustificationType (juce::Justification::centredLeft);
        algorithmHintLabel.setJustificationType (juce::Justification::centredLeft);
        algorithmHintLabel.setVisible (false);
        algorithmHintLabel.setInterceptsMouseClicks (false, false);
        algorithmBox.onChange = [this]
        {
            const int index = algorithmBox.getSelectedItemIndex();
            juce::String hint;
            if (index <= 2)       hint = "classic unison voice spread";
            else if (index <= 5)  hint = "creative pitch/frequency stack";
            else if (index <= 7)  hint = "scale-shaped oscillator stack";
            else                  hint = "chord and overtone intervals";
            algorithmHintText = hint;
            repaint (algorithmHintLabel.getBounds().expanded (8, 4));
        };
        addAndMakeVisible (algorithmLabel);
        addAndMakeVisible (multiplierLabel);
        addAndMakeVisible (algorithmHintLabel);
        addAndMakeVisible (algorithmBox);
        addAndMakeVisible (multiplierBox);

        auto setupRotary = [this] (juce::Slider& s, juce::Label& label, const juce::String& text)
        {
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 52, 16);
            label.setText (text, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            addAndMakeVisible (s);
            addAndMakeVisible (label);
        };

        setupRotary (frameSlider, frameLabel, "Frame");
        setupRotary (smoothSlider, smoothLabel, "Warp");
        setupRotary (coarseSlider, coarseLabel, "Coarse");
        setupRotary (fineSlider, fineLabel, "Fine");
        setupRotary (panSlider, panLabel, "Pan");
        setupRotary (levelSlider, levelLabel, "Level");
        setupRotary (unisonSlider, unisonLabel, "Unison");
        setupRotary (spreadSlider, spreadLabel, "Spread");
        setupRotary (fmSlider, fmLabel, "FM");
        fmSlider.setVisible (false);
        fmLabel.setVisible (false);
        setupRotary (attackSlider, attackLabel, "Amp Attack");
        setupRotary (decaySlider, decayLabel, "Decay");
        setupRotary (sustainSlider, sustainLabel, "Sustain");
        setupRotary (releaseSlider, releaseLabel, "Amp Release");
        setupRotary (polyphonySlider, polyphonyLabel, "Polyphony");
        setupRotary (masterPitchSlider, masterPitchLabel, "Master Pitch");
        setupRotary (bendRangeSlider, bendRangeLabel, "Bend Range");
        setupRotary (outputSlider, outputLabel, "Output");
        attackSlider.setVisible (false);
        attackLabel.setVisible (false);
        decaySlider.setVisible (false);
        decayLabel.setVisible (false);
        sustainSlider.setVisible (false);
        sustainLabel.setVisible (false);
        releaseSlider.setVisible (false);
        releaseLabel.setVisible (false);

        glideTimeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        glideTimeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        glideTimeLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (monoLegatoButton);
        addAndMakeVisible (glideButton);
        addAndMakeVisible (glideTimeLabel);
        addAndMakeVisible (glideTimeSlider);

        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        algorithmAttachment = std::make_unique<ComboBoxAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::algorithm), algorithmBox);
        multiplierAttachment = std::make_unique<ComboBoxAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::multiplier), multiplierBox);
        attackAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::attack), attackSlider);
        decayAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::decay), decaySlider);
        sustainAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::sustain), sustainSlider);
        releaseAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::release), releaseSlider);
        outputAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::output), outputSlider);
        monoLegatoAttachment = std::make_unique<ButtonAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::monoLegato), monoLegatoButton);
        glideAttachment = std::make_unique<ButtonAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::glide), glideButton);
        glideTimeAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::glideTimeMs), glideTimeSlider);
        polyphonyAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::polyphony), polyphonySlider);
        masterPitchAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::masterPitch), masterPitchSlider);
        bendRangeAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::bendRange), bendRangeSlider);
        unisonAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::unison), unisonSlider);
        spreadAttachment = std::make_unique<SliderAttachment> (apvts, wavetableSynthParamId (slotIndex, WavetableSynthParam::spread), spreadSlider);

        rebindGeneratorControls();
        refreshGeneratorButtons();
        updateTableName();
        algorithmBox.onChange();
    }

    void WavetableSynthControlsPanel::selectGenerator (int index)
    {
        const int next = juce::jlimit (0, kNumWavetableSynthGenerators - 1, index);
        if (selectedGenerator == next)
            return;

        selectedGenerator = next;
        preview.setGeneratorIndex (selectedGenerator);
        rebindGeneratorControls();
        refreshGeneratorButtons();
        resized();
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    void WavetableSynthControlsPanel::rebindGeneratorControls()
    {
        using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
        using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

        generatorEnabledAttachment.reset();
        tableAttachment.reset();
        frameAttachment.reset();
        smoothAttachment.reset();
        coarseAttachment.reset();
        fineAttachment.reset();
        panAttachment.reset();
        levelAttachment.reset();
        fmAttachment.reset();

        generatorEnabledAttachment = std::make_unique<ButtonAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::enabled), generatorEnabledButton);
        tableAttachment = std::make_unique<ComboBoxAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::table), tableBox);
        frameAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::frame), frameSlider);
        smoothAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::smooth), smoothSlider);
        coarseAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::coarse), coarseSlider);
        fineAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::fine), fineSlider);
        panAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::pan), panSlider);
        levelAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::level), levelSlider);
        fmAttachment = std::make_unique<SliderAttachment> (
            apvts, wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::fm), fmSlider);

        const auto label = getWavetableSynthGeneratorLabels()[selectedGenerator];
        modTargets.clear();
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::frame),  label + " Frame",  &frameSlider });
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::smooth), label + " Smooth", &smoothSlider });
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::coarse), label + " Coarse", &coarseSlider });
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::fine),   label + " Fine",   &fineSlider });
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::pan),    label + " Pan",    &panSlider });
        modTargets.push_back ({ wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::level),  label + " Level",  &levelSlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::spread), "Spread", &spreadSlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::masterPitch), "Master Pitch", &masterPitchSlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::polyphony), "Polyphony", &polyphonySlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::bendRange), "Bend Range", &bendRangeSlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::output), "Output", &outputSlider });
        modTargets.push_back ({ wavetableSynthParamId (slotIndex, WavetableSynthParam::glideTimeMs), "Glide Time", &glideTimeSlider });
        updateTableName();
    }

    void WavetableSynthControlsPanel::refreshGeneratorButtons()
    {
        const auto labels = getWavetableSynthGeneratorLabels();
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            const auto* enabled = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::enabled));
            const auto* output = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::output));
            const bool isOn = enabled != nullptr && enabled->load() >= 0.5f;
            const int out = output != nullptr ? (int) output->load() + 1 : 1;
            const auto state = isOn ? "  -> Out " : "  off  Out ";
            generatorButtons[(size_t) gen].setButtonText (labels[gen] + state + juce::String (out));
            generatorButtons[(size_t) gen].setToggleState (gen == selectedGenerator, juce::dontSendNotification);
        }

        const auto* selectedOut = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::output));
        const int outIndex = selectedOut != nullptr ? (int) selectedOut->load() : 0;
        for (int out = 0; out < kNumWavetableSynthOutputs; ++out)
        {
            juce::StringArray assigned;
            for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
            {
                const auto* enabled = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::enabled));
                const auto* output = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::output));
                if (enabled != nullptr && enabled->load() >= 0.5f && output != nullptr && (int) output->load() == out)
                    assigned.add (labels[gen]);
            }

            outputButtons[(size_t) out].setButtonText ("Out " + juce::String (out + 1)
                + (assigned.isEmpty() ? "  -" : "  " + assigned.joinIntoString (" + ")));
            outputButtons[(size_t) out].setToggleState (out == outIndex, juce::dontSendNotification);
            outputButtons[(size_t) out].repaint();
        }
        repaint();
    }

    void WavetableSynthControlsPanel::setSelectedGeneratorOutput (int outputIndex)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                apvts.getParameter (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::output))))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) juce::jlimit (0, kNumWavetableSynthOutputs - 1, outputIndex)));
        refreshGeneratorButtons();
    }

    void WavetableSynthControlsPanel::enableNextGenerator()
    {
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            auto* enabled = dynamic_cast<juce::AudioParameterBool*> (
                apvts.getParameter (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::enabled)));
            if (enabled != nullptr && ! enabled->get())
            {
                enabled->setValueNotifyingHost (1.0f);
                selectGenerator (gen);
                refreshGeneratorButtons();
                return;
            }
        }

        selectGenerator ((selectedGenerator + 1) % kNumWavetableSynthGenerators);
    }

    void WavetableSynthControlsPanel::stepSelectedTable (int direction)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                apvts.getParameter (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::table))))
            setSelectedTableIndex (param->getIndex() + direction);
        updateTableName();
    }

    void WavetableSynthControlsPanel::setSelectedTableIndex (int index)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                apvts.getParameter (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::table))))
        {
            const int next = juce::jlimit (0, param->choices.size() - 1, index);
            param->setValueNotifyingHost (param->convertTo0to1 ((float) next));
        }
        updateTableName();
    }

    void WavetableSynthControlsPanel::showTableSearchPopup()
    {
        const auto* tableParam = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::table));
        const int currentIndex = tableParam != nullptr ? (int) tableParam->load() : 0;
        auto content = std::make_unique<WavetableSearchPopup> (currentIndex, [this] (int index) { setSelectedTableIndex (index); });
        juce::CallOutBox::launchAsynchronously (std::move (content), searchTableButton.getScreenBounds(), nullptr);
    }

    void WavetableSynthControlsPanel::updateTableName()
    {
        const auto names = WavetableLibrary::getCatalogDisplayNames();
        const auto* tableParam = apvts.getRawParameterValue (wavetableSynthGenParamId (slotIndex, selectedGenerator, WavetableSynthGenParam::table));
        const int index = tableParam != nullptr ? juce::jlimit (0, names.size() - 1, (int) tableParam->load()) : 0;
        tableNameLabel.setText (names.isEmpty() ? "No tables" : names[index], juce::dontSendNotification);
    }

    void WavetableSynthControlsPanel::paint (juce::Graphics& g)
    {
        if (algorithmHintText.isEmpty())
            return;

        const auto hintBounds = algorithmHintLabel.getBounds().toFloat();
        constexpr float shear = -0.26f;

        g.saveState();
        g.addTransform (juce::AffineTransform::shear (shear, 0.0f));
        g.setColour (Palette::dim.brighter (0.2f));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::italic)));

        const auto compensatedX = juce::roundToInt (hintBounds.getX() - shear * hintBounds.getY());
        g.drawText (algorithmHintText,
                    compensatedX,
                    juce::roundToInt (hintBounds.getY()),
                    juce::roundToInt (hintBounds.getWidth()),
                    juce::roundToInt (hintBounds.getHeight()),
                    juce::Justification::centredLeft);
        g.restoreState();
    }

    void WavetableSynthControlsPanel::resized()
    {
        auto area = getLocalBounds().reduced (8);
        const int reservedBelowPreview = 430;
        preview.setBounds (area.removeFromTop (juce::jmax (120, area.getHeight() - reservedBelowPreview)));

        area.removeFromTop (8);
        auto browser = area.removeFromTop (30);
        tableLabel.setBounds (browser.removeFromLeft (42));
        prevTableButton.setBounds (browser.removeFromLeft (26));
        browser.removeFromLeft (3);
        nextTableButton.setBounds (browser.removeFromLeft (26));
        browser.removeFromLeft (8);
        searchTableButton.setBounds (browser.removeFromRight (62));
        browser.removeFromRight (6);
        tableBox.setBounds (browser);
        tableNameLabel.setBounds (tableBox.getBounds());

        area.removeFromTop (10);
        auto genControls = area.removeFromTop (96);
        const int genControlWidth = genControls.getWidth() / 6;
        auto layoutKnob = [] (juce::Rectangle<int> cell, juce::Label& label, juce::Slider& slider)
        {
            label.setBounds (cell.removeFromTop (16));
            cell.removeFromTop (16); // keep the modulation destination nub clear of the label text
            slider.setBounds (cell);
        };
        layoutKnob (genControls.removeFromLeft (genControlWidth), frameLabel, frameSlider);
        layoutKnob (genControls.removeFromLeft (genControlWidth), smoothLabel, smoothSlider);
        layoutKnob (genControls.removeFromLeft (genControlWidth), coarseLabel, coarseSlider);
        layoutKnob (genControls.removeFromLeft (genControlWidth), fineLabel, fineSlider);
        layoutKnob (genControls.removeFromLeft (genControlWidth), panLabel, panSlider);
        layoutKnob (genControls, levelLabel, levelSlider);

        area.removeFromTop (10);
        auto spreadRow = area.removeFromTop (96);
        const int spreadKnobWidth = juce::jmax (58, spreadRow.getWidth() / 5);
        layoutKnob (spreadRow.removeFromLeft (spreadKnobWidth), unisonLabel, unisonSlider);
        layoutKnob (spreadRow.removeFromLeft (spreadKnobWidth), spreadLabel, spreadSlider);
        auto multiplierArea = spreadRow.removeFromRight (juce::jmax (78, spreadRow.getWidth() / 4));
        multiplierLabel.setBounds (multiplierArea.removeFromTop (16));
        multiplierArea.removeFromTop (16);
        multiplierBox.setBounds (multiplierArea.removeFromTop (26));
        spreadRow.removeFromRight (8);
        auto algorithmArea = spreadRow;
        algorithmLabel.setBounds (algorithmArea.removeFromTop (16));
        algorithmArea.removeFromTop (16);
        auto algoTop = algorithmArea.removeFromTop (26);
        algorithmBox.setBounds (algoTop);
        algorithmArea.removeFromTop (4);
        algorithmHintLabel.setBounds (algorithmArea.removeFromTop (18));

        area.removeFromTop (10);
        auto modeRow = area.removeFromTop (62);
        monoLegatoButton.setBounds (modeRow.removeFromLeft (82).removeFromTop (28));
        modeRow.removeFromLeft (6);
        glideButton.setBounds (modeRow.removeFromLeft (76).removeFromTop (28));
        modeRow.removeFromLeft (10);
        glideTimeLabel.setBounds (modeRow.removeFromLeft (56).removeFromTop (18));
        glideTimeSlider.setBounds (modeRow.removeFromTop (26));

        area.removeFromTop (10);
        auto voiceKnobs = area.removeFromTop (96);
        const int voiceKnobWidth = voiceKnobs.getWidth() / 4;
        layoutKnob (voiceKnobs.removeFromLeft (voiceKnobWidth), polyphonyLabel, polyphonySlider);
        layoutKnob (voiceKnobs.removeFromLeft (voiceKnobWidth), masterPitchLabel, masterPitchSlider);
        layoutKnob (voiceKnobs.removeFromLeft (voiceKnobWidth), bendRangeLabel, bendRangeSlider);
        layoutKnob (voiceKnobs, outputLabel, outputSlider);
    }
}
