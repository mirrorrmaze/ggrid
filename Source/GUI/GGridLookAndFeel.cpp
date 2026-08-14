#include "GGridLookAndFeel.h"

namespace GGrid
{
    namespace
    {
        juce::String getSystemFontName()
        {
           #if JUCE_WINDOWS
            return "Segoe UI";
           #elif JUCE_MAC
            return "Helvetica Neue";
           #else
            return "Verdana";
           #endif
        }
    }

    GGridLookAndFeel::GGridLookAndFeel()
    {
        using namespace Palette;

        setColour (juce::ResizableWindow::backgroundColourId, bg);

        setColour (juce::Label::textColourId, dim);

        setColour (juce::ComboBox::backgroundColourId, bg);
        setColour (juce::ComboBox::outlineColourId, dim);
        setColour (juce::ComboBox::textColourId, bright);
        setColour (juce::ComboBox::arrowColourId, bright);

        setColour (juce::TextButton::buttonColourId, bg);
        setColour (juce::TextButton::buttonOnColourId, dimmer);
        setColour (juce::TextButton::textColourOffId, bright);
        setColour (juce::TextButton::textColourOnId, bright);

        setColour (juce::ToggleButton::textColourId, bright);
        setColour (juce::ToggleButton::tickColourId, bright);
        setColour (juce::ToggleButton::tickDisabledColourId, dimmer);

        setColour (juce::Slider::textBoxTextColourId, bright);
        setColour (juce::Slider::textBoxOutlineColourId, dim);
        setColour (juce::Slider::textBoxBackgroundColourId, bg);

        setColour (juce::PopupMenu::backgroundColourId, bg);
        setColour (juce::PopupMenu::textColourId, bright);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, dimmer);
        setColour (juce::PopupMenu::highlightedTextColourId, bright);
    }

    void GGridLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                              float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
    {
        using namespace Palette;

        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
        const float diameter = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float radius = diameter * 0.5f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const bool enabled = slider.isEnabled();

        g.setColour (bg);
        g.fillEllipse (juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre));
        g.setColour (enabled ? dim : dimmer);
        g.drawEllipse (juce::Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre), 1.2f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius * 0.95f, radius * 0.95f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (dimmer);
        g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

        if (sliderPos > 0.0001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius * 0.95f, radius * 0.95f, 0.0f, rotaryStartAngle, angle, true);
            g.setColour (enabled ? bright : dim);
            g.strokePath (value, juce::PathStrokeType (2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }

        juce::Path pointer;
        pointer.startNewSubPath (0.0f, -radius * 0.15f);
        pointer.lineTo (0.0f, -radius * 0.82f);
        g.setColour (enabled ? bright : dim);
        g.strokePath (pointer, juce::PathStrokeType (2.0f), juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

        g.setColour (dim);
        for (int t = 0; t < 9; ++t)
        {
            const float tickProportion = (float) t / 8.0f;
            const float tickAngle = rotaryStartAngle + tickProportion * (rotaryEndAngle - rotaryStartAngle);
            const auto p1 = centre.getPointOnCircumference (radius * 0.98f, tickAngle);
            const auto p2 = centre.getPointOnCircumference (radius * 1.08f, tickAngle);
            g.drawLine ({ p1, p2 }, 1.0f);
        }
    }

    void GGridLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float sliderPos, float minSliderPos, float maxSliderPos,
                                              const juce::Slider::SliderStyle style, juce::Slider& slider)
    {
        using namespace Palette;
        juce::ignoreUnused (minSliderPos, maxSliderPos, style);

        const bool enabled = slider.isEnabled();
        const float trackThickness = 4.0f;
        const auto trackY = (float) y + (float) height * 0.5f - trackThickness * 0.5f;

        g.setColour (dimmer);
        g.fillRect (juce::Rectangle<float> ((float) x, trackY, (float) width, trackThickness));

        const bool isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float zeroX = isBipolar
            ? (float) x + (float) width * (float) ((0.0 - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum()))
            : (float) x;

        g.setColour (enabled ? bright : dim);
        g.fillRect (juce::Rectangle<float> (juce::jmin (zeroX, sliderPos), trackY,
                                             std::abs (sliderPos - zeroX), trackThickness));

        const float thumbWidth = 6.0f;
        g.setColour (enabled ? bright : dim);
        g.fillRect (juce::Rectangle<float> (sliderPos - thumbWidth * 0.5f, (float) y, thumbWidth, (float) height));
    }

    void GGridLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
    {
        using namespace Palette;

        auto bounds = button.getLocalBounds().toFloat();
        const bool isOn = button.getToggleState() || shouldDrawButtonAsDown;

        g.setColour (isOn ? dimmer : bg);
        g.fillRect (bounds);

        g.setColour (shouldDrawButtonAsHighlighted ? bright : dim);
        g.drawRect (bounds, 1.5f);
    }

    void GGridLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
    {
        using namespace Palette;

        auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

        g.setColour (bg);
        g.fillRect (bounds);
        g.setColour (box.isEnabled() ? dim : dimmer);
        g.drawRect (bounds, 1.5f);

        const float arrowSize = 5.0f;
        const auto arrowCentre = juce::Point<float> ((float) width - 14.0f, (float) height * 0.5f);

        juce::Path arrow;
        arrow.addTriangle (arrowCentre.x - arrowSize, arrowCentre.y - arrowSize * 0.5f,
                            arrowCentre.x + arrowSize, arrowCentre.y - arrowSize * 0.5f,
                            arrowCentre.x, arrowCentre.y + arrowSize * 0.5f);
        g.setColour (bright);
        g.fillPath (arrow);
    }

    juce::Font GGridLookAndFeel::getLabelFont (juce::Label&)
    {
        return juce::Font (juce::FontOptions (getSystemFontName(), 13.0f, juce::Font::plain));
    }

    juce::Font GGridLookAndFeel::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (getSystemFontName(), 13.0f, juce::Font::plain));
    }

    juce::Font GGridLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
    {
        return juce::Font (juce::FontOptions (getSystemFontName(), (float) juce::jmin (14, buttonHeight - 6), juce::Font::plain));
    }

    juce::Font GGridLookAndFeel::getPopupMenuFont()
    {
        return juce::Font (juce::FontOptions (getSystemFontName(), 13.0f, juce::Font::plain));
    }
}
