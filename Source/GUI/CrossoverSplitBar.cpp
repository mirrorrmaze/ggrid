#include "CrossoverSplitBar.h"
#include "GGridLookAndFeel.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    CrossoverSplitBar::CrossoverSplitBar (juce::RangedAudioParameter& splitParam1, juce::RangedAudioParameter& splitParam2)
    {
        splitParams[0] = &splitParam1;
        splitParams[1] = &splitParam2;

        setInterceptsMouseClicks (true, false);
        startTimerHz (15);
    }

    CrossoverSplitBar::~CrossoverSplitBar()
    {
        stopTimer();
    }

    void CrossoverSplitBar::timerCallback()
    {
        bool changed = false;
        for (int i = 0; i < 2; ++i)
        {
            const float v = splitParams[i]->getValue();
            if (! juce::approximatelyEqual (v, lastSeenValue[i]))
            {
                lastSeenValue[i] = v;
                changed = true;
            }
        }

        if (changed)
            repaint();
    }

    float CrossoverSplitBar::xForParam (const juce::RangedAudioParameter& param) const
    {
        auto area = getLocalBounds().toFloat();
        return area.getX() + param.getValue() * area.getWidth();
    }

    int CrossoverSplitBar::hitTestMarker (float x) const
    {
        int best = -1;
        float bestDist = grabToleranceX;

        for (int i = 0; i < 2; ++i)
        {
            const float dist = std::abs (x - xForParam (*splitParams[i]));
            if (dist <= bestDist)
            {
                bestDist = dist;
                best = i;
            }
        }

        return best;
    }

    int CrossoverSplitBar::bandForX (float x) const
    {
        if (x < xForParam (*splitParams[0]))
            return 0;
        if (x < xForParam (*splitParams[1]))
            return 1;
        return 2;
    }

    void CrossoverSplitBar::mouseDown (const juce::MouseEvent& e)
    {
        draggingMarker = hitTestMarker ((float) e.x);
        if (draggingMarker >= 0)
        {
            splitParams[(size_t) draggingMarker]->beginChangeGesture();
            return;
        }

        const int clickedBand = bandForX ((float) e.x);
        if (clickedBand != selectedBand)
        {
            selectedBand = clickedBand;
            if (onBandSelected)
                onBandSelected (selectedBand);
            repaint();
        }
    }

    void CrossoverSplitBar::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggingMarker < 0)
            return;

        auto area = getLocalBounds().toFloat();
        const float normalisedX = juce::jlimit (0.0f, 1.0f, ((float) e.x - area.getX()) / area.getWidth());

        auto& movingParam = *splitParams[(size_t) draggingMarker];
        auto& otherParam = *splitParams[(size_t) (1 - draggingMarker)];

        float hz = movingParam.convertFrom0to1 (normalisedX);
        const float otherHz = otherParam.convertFrom0to1 (otherParam.getValue());

        // Keep split 2 meaningfully above split 1 while dragging, matching
        // MultibandConvolutionModule::process()'s own defensive clamp -- dragging can never
        // produce an order the DSP would silently correct behind the GUI's back.
        if (draggingMarker == 0)
            hz = juce::jmin (hz, otherHz / minSplitRatio);
        else
            hz = juce::jmax (hz, otherHz * minSplitRatio);

        hz = movingParam.getNormalisableRange().snapToLegalValue (hz);
        movingParam.setValueNotifyingHost (movingParam.convertTo0to1 (hz));
    }

    void CrossoverSplitBar::mouseUp (const juce::MouseEvent&)
    {
        if (draggingMarker >= 0)
        {
            splitParams[(size_t) draggingMarker]->endChangeGesture();
            draggingMarker = -1;
        }
    }

    void CrossoverSplitBar::mouseMove (const juce::MouseEvent& e)
    {
        const int newHovered = hitTestMarker ((float) e.x);
        if (newHovered != hoveredMarker)
        {
            hoveredMarker = newHovered;
            repaint();
        }
    }

    void CrossoverSplitBar::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);

        const float x1 = xForParam (*splitParams[0]);
        const float x2 = xForParam (*splitParams[1]);

        const auto labels = getConvolutionBandLabels();
        const juce::Rectangle<float> bandAreas[3] {
            { bounds.getX(), bounds.getY(), x1 - bounds.getX(), bounds.getHeight() },
            { x1, bounds.getY(), x2 - x1, bounds.getHeight() },
            { x2, bounds.getY(), bounds.getRight() - x2, bounds.getHeight() }
        };

        for (int b = 0; b < 3; ++b)
        {
            if (bandAreas[b].getWidth() <= 0.0f)
                continue;

            // The selected band reads as a raised/lit tab (brighter fill, bold bright text) --
            // the other two sit back as faint, clickable-but-inactive regions. See
            // MultibandConvolutionControlsPanel, whose single shared knob set currently reflects
            // whichever band is lit here.
            const bool active = b == selectedBand;
            g.setColour (active ? Palette::dimmer.withAlpha (0.9f) : Palette::dimmer.withAlpha (0.3f));
            g.fillRect (bandAreas[b]);

            g.setColour (active ? Palette::bright : Palette::dim);
            g.setFont (juce::Font (juce::FontOptions (11.0f, active ? juce::Font::bold : juce::Font::plain)));
            g.drawText (labels[b], bandAreas[b].toNearestInt(), juce::Justification::centred);
        }

        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? x1 : x2;
            const bool active = draggingMarker == i || (draggingMarker < 0 && hoveredMarker == i);

            g.setColour (active ? Palette::bright : Palette::accent);
            g.drawLine (x, bounds.getY(), x, bounds.getBottom(), active ? 2.5f : 1.5f);

            const float handleSize = 8.0f;
            juce::Path handle;
            handle.addTriangle (x - handleSize * 0.5f, bounds.getY(),
                                 x + handleSize * 0.5f, bounds.getY(),
                                 x, bounds.getY() + handleSize);
            g.fillPath (handle);
        }
    }

    void CrossoverSplitBar::resized()
    {
    }
}
