#include "IONodeComponent.h"
#include "GGridLookAndFeel.h"

namespace GGrid
{
    void IONodeComponent::OutputNub::paint (juce::Graphics& g)
    {
        g.setColour (Palette::accent);
        g.fillEllipse (getLocalBounds().toFloat().reduced (2.0f));
    }

    IONodeComponent::IONodeComponent (bool isInputIn)
        : isInputNode (isInputIn)
    {
        addAndMakeVisible (body);

        titleLabel.setText (isInputNode ? "Input" : "Output", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        titleLabel.setJustificationType (juce::Justification::centred);
        titleLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (titleLabel);

        if (isInputNode)
        {
            addAndMakeVisible (outputNub0);
            addAndMakeVisible (outputNub1);
            addAndMakeVisible (outputNub2);
            addAndMakeVisible (outputNub3);
        }
    }

    juce::Point<int> IONodeComponent::getPortPosition (int portIndex) const
    {
        const int x = isInputNode ? getWidth() : 0;
        return { x, getHeight() * (portIndex + 1) / (kMaxPortsPerSide + 1) };
    }

    void IONodeComponent::setSelected (bool shouldBeSelected)
    {
        if (isSelectedFlag == shouldBeSelected) return;
        isSelectedFlag = shouldBeSelected;
        repaint();
    }

    void IONodeComponent::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::bg);
        g.fillRect (bounds);
        g.setColour (isSelectedFlag ? Palette::bright : Palette::dim);
        g.drawRect (bounds, isSelectedFlag ? 2.5f : 1.5f);

        // Output's ports are static visual-only targets (a cable is dropped onto them, never
        // dragged from) -- Input's are separate interactive OutputNub children instead, added in
        // the constructor, so they paint themselves.
        if (! isInputNode)
        {
            g.setColour (Palette::accent);
            for (int port = 0; port < kMaxPortsPerSide; ++port)
                g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (getPortPosition (port).toFloat()));
        }
    }

    void IONodeComponent::resized()
    {
        body.setBounds (getLocalBounds());
        titleLabel.setBounds (getLocalBounds().withHeight (24).withY (8));

        if (isInputNode)
        {
            OutputNub* nubs[kMaxPortsPerSide] = { &outputNub0, &outputNub1, &outputNub2, &outputNub3 };
            for (int port = 0; port < kMaxPortsPerSide; ++port)
            {
                const auto pos = getPortPosition (port);
                nubs[port]->setBounds (pos.x - 8, pos.y - 8, 16, 16);
            }
        }
    }
}
