#include "CrossoverSplitBar.h"
#include "GGridLookAndFeel.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    CrossoverSplitBar::CrossoverSplitBar (juce::RangedAudioParameter& splitParam1, juce::RangedAudioParameter& splitParam2,
                                           std::function<SpectrumAnalyzer*()> getAnalyzerIn)
        : getAnalyzer (std::move (getAnalyzerIn))
    {
        splitParams[0] = &splitParam1;
        splitParams[1] = &splitParam2;

        setInterceptsMouseClicks (true, false);
        // 30Hz matches MultibandConvolver's own spectrum redraw cadence -- also drives
        // performFFTIfReady() below, not just param-change polling like the original 15Hz did.
        startTimerHz (30);
    }

    CrossoverSplitBar::~CrossoverSplitBar()
    {
        stopTimer();
    }

    void CrossoverSplitBar::timerCallback()
    {
        if (auto* analyzer = getAnalyzer ? getAnalyzer() : nullptr)
            analyzer->performFFTIfReady();

        // Unconditional now (used to only repaint when a split param's value actually changed) --
        // the spectrum animates continuously regardless of whether either marker has moved, so
        // there's no longer a cheaper condition worth gating on.
        repaint();
    }

    float CrossoverSplitBar::xForParam (const juce::RangedAudioParameter& param) const
    {
        auto area = getLocalBounds().toFloat();
        return area.getX() + param.getValue() * area.getWidth();
    }

    float CrossoverSplitBar::hzToX (float hz) const
    {
        auto area = getLocalBounds().toFloat();
        const float logMin = std::log10 (minHz), logMax = std::log10 (maxHz);
        const float t = (std::log10 (juce::jlimit (minHz, maxHz, hz)) - logMin) / (logMax - logMin);
        return area.getX() + t * area.getWidth();
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

        // Frequency reference gridlines, drawn first/behind everything -- log-ish-spaced, on the
        // exact same axis as the spectrum and the 2 markers (see hzToX's own comment).
        for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (Palette::dimmer.withAlpha (0.5f));
            g.drawVerticalLine ((int) hzToX (hz), bounds.getY(), bounds.getBottom());
        }

        // Live spectrum of whatever's arriving at the module -- see the class comment. Ported
        // from MultibandConvolver's own SpectrumBandStrip::paint(): a stroked line over a
        // vertical-gradient fill, gated on the analyzer callback actually resolving to something
        // (nullptr before the module's first prepare(), or if this CrossoverSplitBar was
        // constructed without one at all).
        if (auto* analyzer = getAnalyzer ? getAnalyzer() : nullptr)
        {
            const double sr = analyzer->getSampleRate();
            juce::Path linePath, fillPath;
            bool started = false;
            float lastX = bounds.getX();

            // Starts at bin 1 (the true DC bin, index 0, is skipped) -- bin 1 alone is already
            // close enough to minHz (~21.5Hz at 44.1kHz vs. a 20Hz floor) that the plotted curve
            // reaches all the way to this component's true left edge, so it reads as the chart's
            // own border rather than a stray line partway across with a dead gap before it (an
            // earlier attempt here skipped bins 1-2 to dodge a suspected near-DC spike, but that
            // just pushed the visible starting edge out to ~65-80Hz and left the low end blank
            // instead -- worse, not better).
            for (int i = 1; i < SpectrumAnalyzer::numBins; ++i)
            {
                const float freq = (float) (i * sr / SpectrumAnalyzer::fftSize);
                if (freq < minHz || freq > maxHz)
                    continue;

                const float x = hzToX (freq);
                const float db = juce::jlimit (-100.0f, 0.0f, analyzer->getMagnitudeDb (i));
                const float y = juce::jmap (db, -100.0f, 0.0f, bounds.getBottom(), bounds.getY());

                if (! started)
                {
                    linePath.startNewSubPath (x, y);
                    fillPath.startNewSubPath (x, bounds.getBottom());
                    fillPath.lineTo (x, y);
                    started = true;
                }
                else
                {
                    linePath.lineTo (x, y);
                    fillPath.lineTo (x, y);
                }
                lastX = x;
            }

            if (started)
            {
                fillPath.lineTo (lastX, bounds.getBottom());
                fillPath.closeSubPath();

                juce::ColourGradient gradient (Palette::accent.withAlpha (0.55f), 0, bounds.getY(),
                                                Palette::accent.withAlpha (0.02f), 0, bounds.getBottom(), false);
                g.setGradientFill (gradient);
                g.fillPath (fillPath);

                g.setColour (Palette::accent.withAlpha (0.85f));
                g.strokePath (linePath, juce::PathStrokeType (1.5f));
            }
        }

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

            // The selected band reads as a raised/lit tab (brighter wash, bold bright text) --
            // the other two sit back as faint, clickable-but-inactive regions. Both washes are
            // low-alpha now (used to be near-opaque) so the spectrum drawn above stays visible
            // underneath -- matches MultibandConvolver's own "colour washes over the spectrum,
            // not solid blocks under it" layering. See MultibandConvolutionControlsPanel, whose
            // single shared knob set currently reflects whichever band is lit here.
            const bool active = b == selectedBand;
            g.setColour (active ? Palette::dimmer.withAlpha (0.35f) : Palette::dimmer.withAlpha (0.12f));
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
