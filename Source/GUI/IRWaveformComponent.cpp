#include "IRWaveformComponent.h"
#include "GGridLookAndFeel.h"
#include "../Modules/ConvolutionModule.h"

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
                repaint();
        }
        else if (displayBuffer.getNumSamples() != 0)
        {
            // Slot switched away from Convolution -- stop showing stale content.
            displayBuffer.setSize (0, 0);
            lastSeenGeneration = -1;
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
            g.setColour (Palette::dim);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText ("(loading...)", bounds, juce::Justification::centred);
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
