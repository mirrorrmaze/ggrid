#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // A horizontal strip showing MultibandConvolutionModule's 3 bands (Low/Mid/High) with 2
    // draggable vertical markers for the crossover split points, in place of knobs -- per the
    // user's explicit requirement that the bands "still [be] able to drag them around" like the
    // original MultibandConvolver's own split-point UI. Markers sit at their parameter's own
    // normalized [0,1] position (the params use a log-ish skewed range centred at 1000Hz -- see
    // ParameterLayout.cpp's addMultibandConvolutionParams), so the visual spacing and the drag
    // math always agree with each other and with how a host would animate automation.
    //
    // Writes directly to the two RangedAudioParameters (beginChangeGesture/setValueNotifyingHost/
    // endChangeGesture) rather than going through a juce::Slider, since there's no sensible linear
    // control this maps onto. Polls both params on a Timer (matching IRWaveformComponent's
    // established pattern) so host automation or a preset load moving either split point outside
    // of a drag still repaints promptly.
    //
    // Also doubles as the band tab-switcher: clicking anywhere in a band's region (away from
    // either marker) selects it -- MultibandConvolutionControlsPanel listens via onBandSelected
    // and retargets its single shared knob set to that band's parameters, matching the original
    // MultibandConvolver desktop app's tabbed layout instead of showing all 3 bands' controls
    // stacked at once.
    class CrossoverSplitBar : public juce::Component, private juce::Timer
    {
    public:
        CrossoverSplitBar (juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~CrossoverSplitBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        int getSelectedBand() const { return selectedBand; }

        // Fired when a click (not a marker drag) picks a different band -- see class comment.
        std::function<void (int)> onBandSelected;

    private:
        void timerCallback() override;

        // x position in local pixels for a param's current normalized value.
        float xForParam (const juce::RangedAudioParameter& param) const;

        // Index (0 or 1) of whichever marker is nearest the given x, if within grab range.
        int hitTestMarker (float x) const;

        // Which of the 3 band regions (0/1/2) a given x falls into, ignoring markers entirely.
        int bandForX (float x) const;

        juce::RangedAudioParameter* splitParams[2] = { nullptr, nullptr };
        float lastSeenValue[2] = { -1.0f, -1.0f };

        int draggingMarker = -1; // -1 = not dragging
        int hoveredMarker = -1;
        int selectedBand = 0;

        static constexpr float grabToleranceX = 7.0f;
        // Split 2's Hz must stay at least this multiple above Split 1's -- mirrors
        // MultibandConvolutionModule::process()'s own defensive clamp exactly, so dragging can
        // never produce an order the DSP would silently correct behind the GUI's back.
        static constexpr float minSplitRatio = 1.05f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverSplitBar)
    };
}
