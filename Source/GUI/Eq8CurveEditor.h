#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include "SpectrumAnalyzer.h"
#include <array>
#include <functional>

namespace GGrid
{
    // A draggable graphic-EQ curve display, Ableton EQ Eight / SPANDEX EqCurveEditor style: 8
    // always-present band nodes on a log-frequency x / linear-dB y canvas, plus a stroked curve
    // showing the combined response of every enabled band. Drag a node to move its Frequency
    // (always) and Gain (only for gain-shaped types -- Bell/Low Shelf/High Shelf, see
    // eq8BandTypeHasGain); scroll while hovering a node to adjust its Q. Clicking (not dragging) a
    // node also selects it -- MultibandConvolutionControlsPanel's own tab-select pattern, wired up
    // the same way here via onBandSelected so Eq8ControlsPanel's numeric knob strip below can
    // retarget to whichever band was just clicked.
    //
    // Deliberately reads every band's params directly from APVTS on every repaint (matching
    // CrossoverSplitBar's own approach) rather than reaching into a live Eq8Module instance --
    // Eq8Module::makeCoefficients is public/static/pure specifically so this component can
    // recompute the exact same per-band response the DSP would, with no cross-thread state to
    // worry about.
    //
    // Also draws a live spectrum of the incoming signal behind the response curve, so you can see
    // what the EQ is actually doing to the material passing through it -- same SpectrumAnalyzer/
    // getAnalyzer-callback pattern as CrossoverSplitBar (see that class's own comment for why it's
    // a callback re-fetched each tick rather than a stored reference).
    class Eq8CurveEditor : public juce::Component, private juce::Timer
    {
    public:
        // getAnalyzerIn may be an empty std::function (no spectrum drawn) for a caller that
        // doesn't have one wired up.
        Eq8CurveEditor (juce::AudioProcessorValueTreeState& apvts, int slotIndex,
                         std::function<SpectrumAnalyzer*()> getAnalyzerIn = {});
        ~Eq8CurveEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        int getSelectedBand() const { return selectedBand; }

        // Fired when a click (not a drag) picks a different band -- see class comment.
        std::function<void (int)> onBandSelected;

    private:
        void timerCallback() override;

        float freqToX (float freqHz) const;
        float xToFreq (float x) const;
        float gainToY (float gainDb) const;
        float yToGain (float y) const;

        // Closest enabled band's node to a point, within grabToleranceX pixels, or -1.
        int findNodeNear (juce::Point<float> pos) const;

        juce::Colour colourForBand (int index) const;

        struct BandRefs
        {
            juce::RangedAudioParameter* freq = nullptr;
            juce::RangedAudioParameter* gain = nullptr;
            juce::RangedAudioParameter* q = nullptr;
            std::atomic<float>* type = nullptr;
            std::atomic<float>* enabled = nullptr;
        };
        std::array<BandRefs, kNumEq8Bands> bands;

        std::function<SpectrumAnalyzer*()> getAnalyzer;

        double sampleRate = 44100.0;

        int draggingBand = -1;
        int hoveredBand = -1;
        int selectedBand = 0;

        static constexpr float minFreqHz = 20.0f, maxFreqHz = 20000.0f;
        static constexpr float minDb = -12.0f, maxDb = 12.0f;
        static constexpr float grabToleranceX = 12.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Eq8CurveEditor)
    };
}
