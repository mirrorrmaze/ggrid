#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace GGrid
{
    // Matches SPANDEX's (D:\Claude Projects\RepitchDeck) AppLookAndFeel: flat rectangular
    // widgets with hairline borders instead of gradients or glows -- no shadows, no soft
    // edges, high-contrast two-tone everything. This is a fixed single palette (SPANDEX's
    // "Default" theme) rather than SPANDEX's runtime-switchable Theme struct -- GGrid doesn't
    // need multiple themes yet, but the 5-role naming is kept the same so a Theme abstraction
    // could be lifted in later if that's ever wanted.
    namespace Palette
    {
        const juce::Colour bg     { 0xff202022 };
        const juce::Colour bright { 0xffffffff };
        const juce::Colour dim    { 0xffa5a5a8 };
        const juce::Colour dimmer { 0xff4b4b4e };
        const juce::Colour accent { 0xffbf5727 };
    }

    class GGridLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        GGridLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                                float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                                float sliderPos, float minSliderPos, float maxSliderPos,
                                const juce::Slider::SliderStyle, juce::Slider&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                    bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                            int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

        juce::Font getLabelFont (juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        juce::Font getPopupMenuFont() override;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GGridLookAndFeel)
    };
}
