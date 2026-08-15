#include "IRWaveformComponent.h"
#include "GGridLookAndFeel.h"
#include "../Modules/ConvolutionModule.h"
#include "../IR/IRLibrary.h"

namespace GGrid
{
    IRWaveformComponent::IRWaveformComponent (RackSlot& rackSlotIn)
        : rackSlot (rackSlotIn)
    {
        startTimerHz (15);
    }

    IRWaveformComponent::~IRWaveformComponent()
    {
        stopTimer();
    }

    void IRWaveformComponent::timerCallback()
    {
        if (auto* convolutionModule = dynamic_cast<ConvolutionModule*> (rackSlot.getCurrentModule()))
        {
            if (convolutionModule->copyDisplayBufferIfChanged (displayBuffer, lastSeenGeneration, preFadeOutLength))
            {
                irLibraryMissing = false;
                repaint();
            }
            else if (displayBuffer.getNumSamples() == 0)
            {
                // Still empty -- only bother checking the filesystem while that's true (once a
                // real IR has loaded, this can't be the problem anymore, so skip the stat calls).
                const bool missingNow = ! IRLibrary::resolveIRRoot().isDirectory();
                if (missingNow != irLibraryMissing)
                {
                    irLibraryMissing = missingNow;
                    repaint();
                }
            }
        }
        else if (displayBuffer.getNumSamples() != 0 || irLibraryMissing)
        {
            // Slot switched away from Convolution -- stop showing stale content.
            displayBuffer.setSize (0, 0);
            lastSeenGeneration = -1;
            irLibraryMissing = false;
            repaint();
        }
    }

    void IRWaveformComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);

        if (displayBuffer.getNumSamples() == 0)
        {
            if (irLibraryMissing)
            {
                const auto expectedPath = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                               .getChildFile ("GGrid").getChildFile ("IRs").getFullPathName();
                g.setColour (Palette::accent);
                g.setFont (juce::Font (juce::FontOptions (11.0f)));
                g.drawFittedText ("IR library not found -- expected at " + expectedPath,
                                   bounds.reduced (4.0f).toNearestInt(), juce::Justification::centred, 3);
            }
            else
            {
                g.setColour (Palette::dim);
                g.setFont (juce::Font (juce::FontOptions (12.0f)));
                g.drawText ("(loading...)", bounds, juce::Justification::centred);
            }
            return;
        }

        auto area = bounds.reduced (2.0f);
        const int numSamples = displayBuffer.getNumSamples();
        // Reference length the full width represents -- at least numSamples, so a stale/zero
        // preFadeOutLength (shouldn't happen, but don't divide by something smaller than what we
        // actually have) never clips real content.
        const int fullLength = juce::jmax (numSamples, preFadeOutLength);
        const int width = juce::jmax (1, (int) area.getWidth());
        const float midY = area.getCentreY();
        const float halfHeight = area.getHeight() * 0.5f;

        // Column where Fade Out's cut point falls, and a visual amplitude taper across the last
        // quarter of the kept region leading into it -- reads as a volume ramp-down into the cut
        // (symmetric with Fade In's ramp-up at the head) instead of a flat waveform that just
        // stops at a vertical line. Display-only: the real audio still does its short declick +
        // hard truncation right at the cut point (see IRProcessor::buildShapedIR), this only
        // changes how that transition is drawn.
        const int cutoffColumn = juce::jlimit (0, width, (int) (((juce::int64) numSamples * width) / fullLength));
        const int rampColumns = juce::jmin (cutoffColumn, juce::jmax (24, cutoffColumn / 4));
        const int rampStart = cutoffColumn - rampColumns;

        g.setColour (Palette::accent);

        for (int x = 0; x < width; ++x)
        {
            const int startSample = (int) (((juce::int64) x * fullLength) / width);
            if (startSample >= numSamples)
                continue; // past Fade Out's cut point -- leave blank rather than stretching to fill

            const int endSample = juce::jmax (startSample + 1, (int) (((juce::int64) (x + 1) * fullLength) / width));
            const int rangeLength = juce::jmin (endSample, numSamples) - startSample;
            if (rangeLength <= 0)
                continue;

            float minVal = 0.0f, maxVal = 0.0f;
            for (int ch = 0; ch < displayBuffer.getNumChannels(); ++ch)
            {
                const auto range = displayBuffer.findMinMax (ch, startSample, rangeLength);
                minVal = juce::jmin (minVal, range.getStart());
                maxVal = juce::jmax (maxVal, range.getEnd());
            }

            if (rampColumns > 0 && x >= rampStart)
            {
                const float taper = 1.0f - (float) (x - rampStart) / (float) rampColumns;
                minVal *= taper;
                maxVal *= taper;
            }

            const int xPos = (int) (area.getX() + (float) x);
            g.drawVerticalLine (xPos, midY - maxVal * halfHeight, midY - minVal * halfHeight);
        }
    }
}
