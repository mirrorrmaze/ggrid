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
            if (convolutionModule->copyDisplayBufferIfChanged (displayBuffer, lastSeenGeneration))
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
        const int width = juce::jmax (1, (int) area.getWidth());
        const float midY = area.getCentreY();
        const float halfHeight = area.getHeight() * 0.5f;

        g.setColour (Palette::accent);

        for (int x = 0; x < width; ++x)
        {
            const int startSample = (int) (((juce::int64) x * numSamples) / width);
            const int endSample = juce::jmax (startSample + 1, (int) (((juce::int64) (x + 1) * numSamples) / width));
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

            const int xPos = (int) (area.getX() + (float) x);
            g.drawVerticalLine (xPos, midY - maxVal * halfHeight, midY - minVal * halfHeight);
        }
    }
}
