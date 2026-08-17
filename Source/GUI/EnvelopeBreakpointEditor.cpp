#include "EnvelopeBreakpointEditor.h"
#include "GGridLookAndFeel.h"
#include <vector>

namespace GGrid
{
    EnvelopeBreakpointEditor::EnvelopeBreakpointEditor (RackSlot& rackSlotIn)
        : rackSlot (rackSlotIn)
    {
        setInterceptsMouseClicks (true, false);
        startTimerHz (30); // smoother playhead motion than the usual 15Hz polling rate elsewhere
    }

    EnvelopeBreakpointEditor::~EnvelopeBreakpointEditor()
    {
        stopTimer();
    }

    EnvelopeModule* EnvelopeBreakpointEditor::getModule() const
    {
        return dynamic_cast<EnvelopeModule*> (rackSlot.getCurrentModule());
    }

    void EnvelopeBreakpointEditor::timerCallback()
    {
        // Always repaint -- points can change from automation/preset load (not just this editor's
        // own drags), and the playhead moves continuously while playing; there's no cheap "did
        // anything change" check worth doing for a component this simple, matches
        // CrossoverSplitBar's own drag-marker repaint being unconditional per tick too.
        repaint();
    }

    juce::Point<float> EnvelopeBreakpointEditor::pixelToNormalised (juce::Point<float> pixel) const
    {
        auto area = getLocalBounds().toFloat();
        const float x = area.getWidth() > 0.0f ? (pixel.x - area.getX()) / area.getWidth() : 0.0f;
        const float y = area.getHeight() > 0.0f ? 1.0f - (pixel.y - area.getY()) / area.getHeight() : 0.0f;
        return { x, y };
    }

    juce::Point<float> EnvelopeBreakpointEditor::normalisedToPixel (juce::Point<float> normalised) const
    {
        auto area = getLocalBounds().toFloat();
        return { area.getX() + normalised.x * area.getWidth(),
                 area.getY() + (1.0f - normalised.y) * area.getHeight() };
    }

    int EnvelopeBreakpointEditor::hitTestPoint (juce::Point<float> pixel) const
    {
        auto* module = getModule();
        if (module == nullptr)
            return -1;

        int best = -1;
        float bestDistSquared = grabToleranceSquaredPx;

        const int numPoints = module->getNumPoints();
        for (int i = 0; i < numPoints; ++i)
        {
            const auto p = normalisedToPixel (module->getPoint (i));
            const float d = p.getDistanceSquaredFrom (pixel);
            if (d <= bestDistSquared)
            {
                bestDistSquared = d;
                best = i;
            }
        }

        return best;
    }

    void EnvelopeBreakpointEditor::mouseDown (const juce::MouseEvent& e)
    {
        auto* module = getModule();
        if (module == nullptr)
            return;

        const int hit = hitTestPoint (e.position);

        if (e.mods.isRightButtonDown())
        {
            if (hit >= 0)
                module->removePoint (hit);
            repaint();
            return;
        }

        draggingIndex = hit >= 0 ? hit : module->addPoint (pixelToNormalised (e.position));
        repaint();
    }

    void EnvelopeBreakpointEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggingIndex < 0)
            return;

        if (auto* module = getModule())
            module->movePoint (draggingIndex, pixelToNormalised (e.position));

        repaint();
    }

    void EnvelopeBreakpointEditor::mouseUp (const juce::MouseEvent&)
    {
        draggingIndex = -1;
    }

    void EnvelopeBreakpointEditor::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        auto* module = getModule();
        if (module == nullptr)
            return;

        const int numPoints = module->getNumPoints();
        if (numPoints < 2)
            return;

        // Snapshot once (one lock per point) rather than re-locking inside the per-pixel-column
        // loop below.
        std::vector<juce::Point<float>> pts;
        pts.reserve ((size_t) numPoints);
        for (int i = 0; i < numPoints; ++i)
            pts.push_back (module->getPoint (i));

        juce::Path curvePath, fillPath;
        const int width = juce::jmax (1, (int) bounds.getWidth());

        for (int x = 0; x <= width; ++x)
        {
            const float normalisedX = (float) x / (float) width;

            float value = pts.back().y;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                if (normalisedX <= pts[i].x)
                {
                    const auto& a = pts[i - 1];
                    const auto& b = pts[i];
                    const float t = (b.x > a.x) ? (normalisedX - a.x) / (b.x - a.x) : 0.0f;
                    value = a.y + t * (b.y - a.y);
                    break;
                }
            }

            const auto pixel = normalisedToPixel ({ normalisedX, value });
            if (x == 0)
            {
                curvePath.startNewSubPath (pixel);
                fillPath.startNewSubPath (bounds.getX(), bounds.getBottom());
                fillPath.lineTo (pixel);
            }
            else
            {
                curvePath.lineTo (pixel);
                fillPath.lineTo (pixel);
            }
        }
        fillPath.lineTo (bounds.getRight(), bounds.getBottom());
        fillPath.closeSubPath();

        g.setColour (Palette::accent.withAlpha (0.25f));
        g.fillPath (fillPath);
        g.setColour (Palette::accent);
        g.strokePath (curvePath, juce::PathStrokeType (1.5f));

        for (int i = 0; i < numPoints; ++i)
        {
            const auto pixel = normalisedToPixel (pts[(size_t) i]);
            const bool isEndpoint = (i == 0 || i == numPoints - 1);
            const float size = isEndpoint ? 7.0f : 8.0f;
            g.setColour (i == draggingIndex ? Palette::bright : Palette::accent);
            g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (pixel));
        }

        if (module->isPlaying())
        {
            const float playX = normalisedToPixel ({ module->getPlayheadPosition(), 0.0f }).x;
            g.setColour (Palette::bright);
            g.drawLine (playX, bounds.getY(), playX, bounds.getBottom(), 1.5f);
        }
    }
}
