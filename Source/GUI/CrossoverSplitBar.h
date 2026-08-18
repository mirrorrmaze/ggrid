#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "SpectrumAnalyzer.h"
#include <functional>

namespace GGrid
{
    // A horizontal strip showing MultibandConvolutionModule's (or MultipassModule's) 3 bands
    // (Low/Mid/High) with 2 draggable vertical markers for the crossover split points, in place
    // of knobs -- per the user's explicit requirement that the bands "still [be] able to drag
    // them around" like the original MultibandConvolver's own split-point UI. Markers sit at
    // their parameter's own normalized [0,1] position (the params use a log-ish skewed range
    // centred at 1000Hz -- see ParameterLayout.cpp's addMultibandConvolutionParams/
    // addMultipassParams), so the visual spacing and the drag math always agree with each other
    // and with how a host would animate automation.
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
    // stacked at once (MultipassControlsPanel ignores band selection entirely -- it shows all 3
    // of its Gain knobs at once, see that class's own comment).
    //
    // Also draws a live spectrum of whatever's arriving at the module -- same "SpectrumAnalyzer
    // owned by the DSP module, polled by a GUI Timer" pattern MultibandConvolver's own desktop app
    // uses (D:\Claude Projects\MultibandConvolver\Source\GUI\SpectrumAnalyzer.h/SpectrumBandStrip),
    // ported into GGrid's SpectrumAnalyzer.h. getAnalyzer is a callback rather than a stored
    // reference/pointer because the module instance it comes from can be destroyed and recreated
    // at any time (a slot's type can change, or the module gets re-prepared) -- see
    // RackSlot::getCurrentModule()'s own caveat about this; re-fetching defensively each tick
    // means this component never has to know or care when that happens. Returns nullptr (no
    // spectrum drawn) if the analyzer isn't available right now, e.g. before the module's first
    // prepare() call.
    class CrossoverSplitBar : public juce::Component, private juce::Timer
    {
    public:
        // Takes the 2 split-point parameters directly (rather than looking up a fixed paramId
        // scheme internally) so this is reusable by any module with 2 draggable crossover split
        // points -- currently MultibandConvolutionModule and MultipassModule, which have their
        // own distinct parameter ID namespaces (see Identifiers.h). getAnalyzerIn may be an empty
        // std::function (no spectrum drawn) for a caller that doesn't have one wired up.
        CrossoverSplitBar (juce::RangedAudioParameter& splitParam1, juce::RangedAudioParameter& splitParam2,
                            std::function<SpectrumAnalyzer*()> getAnalyzerIn = {}, bool useBandColoursIn = false);
        ~CrossoverSplitBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        int getSelectedBand() const { return selectedBand; }

        // Fired when a click (not a marker drag) picks a different band -- see class comment.
        std::function<void (int)> onBandSelected;

    private:
        void timerCallback() override;

        juce::Rectangle<float> getPlotBounds() const;

        // x position in local pixels for a param's current normalized value.
        float xForParam (const juce::RangedAudioParameter& param) const;
        float hzForParam (const juce::RangedAudioParameter& param) const;
        juce::String formatHz (float hz) const;

        // Index (0 or 1) of whichever marker is nearest the given x, if within grab range.
        int hitTestMarker (float x) const;
        int nearestMarker (float x) const;

        // Which of the 3 band regions (0/1/2) a given x falls into, ignoring markers entirely.
        int bandForX (float x) const;

        // Hz -> local x pixel for the spectrum curve and frequency reference gridlines -- a
        // literal log10(minHz, maxHz) formula, matching Eq8CurveEditor's own freqToX exactly.
        // Deliberately NOT the same mapping xForParam uses for the 2 draggable markers (which
        // goes through the split parameter's own convertTo0to1(), reflecting JUCE's power-law
        // NormalisableRange skew): that skew is calibrated for making a knob feel good, not for
        // display, and turns out to badly over-expand the low end -- a first attempt at reusing
        // it for the spectrum put its lowest bins over 10% of the width away from the true 20Hz
        // edge (skew factor ~4.35 for this range), reading as "the low end is just missing."
        // Literal log10 is what every real spectrum analyzer axis looks like, and is what
        // MultibandConvolver's own original SpectrumBandStrip used throughout (its markers sat on
        // that same literal log axis, so no such mismatch existed there in the first place). The
        // tradeoff: a marker and the spectrum's rendition of that same Hz value may now sit a few
        // pixels apart instead of pixel-identical -- worth it for a spectrum that isn't broken.
        float hzToX (float hz) const;

        juce::RangedAudioParameter* splitParams[2] = { nullptr, nullptr };

        std::function<SpectrumAnalyzer*()> getAnalyzer;
        bool useBandColours = false;
        static constexpr float minHz = 20.0f, maxHz = 20000.0f;

        int draggingMarker = -1; // -1 = not dragging
        int hoveredMarker = -1;
        int readoutMarker = -1;
        double readoutUntilMs = 0.0;
        int selectedBand = 0;

        static constexpr float grabToleranceX = 7.0f;
        // Split 2's Hz must stay at least this multiple above Split 1's -- mirrors
        // MultibandConvolutionModule::process()'s own defensive clamp exactly, so dragging can
        // never produce an order the DSP would silently correct behind the GUI's back.
        static constexpr float minSplitRatio = 1.05f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrossoverSplitBar)
    };
}
