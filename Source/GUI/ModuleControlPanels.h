#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include "../Rack/RackSlot.h"
#include "../Modules/LFOModule.h"
#include "../Wavetable/WavetableLibrary.h"
#include "IRWaveformComponent.h"
#include "CrossoverSplitBar.h"
#include "EnvelopeBreakpointEditor.h"
#include "Eq8CurveEditor.h"
#include <vector>
#include <array>

namespace GGrid
{
    // One entry per knob a panel exposes as a modulation-cable destination: its exact APVTS
    // parameter ID (what ModulationMatrix::getOffsetForParam matches cables against), a short
    // label (tooltip/debug use), and the live juce::Slider so NodeComponent can ask its current
    // on-screen bounds each time it needs to place/hit-test that knob's destination nub. Built
    // once per panel, in its constructor (slotIndex is only available there).
    struct ModTarget
    {
        juce::String paramId;
        juce::String label;
        juce::Slider* slider = nullptr;
    };

    // Controls for whichever module type is currently active in a node -- one panel class per
    // rack module type, all shown/hidden by NodeComponent depending on the node's current type.
    class WaveshaperControlsPanel : public juce::Component, private juce::Timer
    {
    public:
        WaveshaperControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~WaveshaperControlsPanel() override;

        void resized() override;
        void paint (juce::Graphics&) override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        // Repaints the curve preview at a modest rate so it stays live as Shape/Symmetry/Fold
        // move -- polling rather than a value-change listener, since a Slider::Listener would
        // need to coexist with the APVTS SliderAttachment already driving these same sliders
        // (and a raw onValueChange callback would just overwrite the attachment's own one).
        // Matches the same polling pattern IRWaveformComponent already uses for the same reason.
        void timerCallback() override { repaint (curveArea); }

        float shapeSample (float x, int shapeIndex, float symmetry, float foldAmount) const;

        juce::Label driveLabel { {}, "Drive" }, shapeLabel { {}, "Shape" }, symmetryLabel { {}, "Symmetry" },
                    foldLabel { {}, "Fold" }, oversampleLabel { {}, "Oversample" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider driveSlider, symmetrySlider, foldSlider, mixSlider, outputSlider;
        juce::ComboBox shapeBox, oversampleBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            driveAttachment, symmetryAttachment, foldAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            shapeAttachment, oversampleAttachment;

        // Transfer-curve preview area -- see paint().
        juce::Rectangle<int> curveArea;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveshaperControlsPanel)
    };

    // Filter module controls: Frequency/Resonance/Feedback/Mix/Output knobs plus the Filter
    // Type dropdown that picks which of the 7 algorithms (biquad SVF modes or delay-line
    // comb/allpass) is active -- see FilterModule for what each does with these parameters.
    class FilterControlsPanel : public juce::Component
    {
    public:
        FilterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label frequencyLabel { {}, "Frequency" }, resonanceLabel { {}, "Resonance" }, feedbackLabel { {}, "Feedback" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" }, typeLabel { {}, "Type" };

        juce::Slider frequencySlider, resonanceSlider, feedbackSlider, mixSlider, outputSlider;
        juce::ComboBox typeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, resonanceAttachment, feedbackAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterControlsPanel)
    };

    // Delay module controls: Time/Feedback/Saturation/Mix/Output knobs, plus a Sync toggle (locks
    // Time to a host-tempo note Division instead of the free-running ms value), a Ping-Pong
    // toggle (cross-feeds L/R so echoes alternate channels), and Low Cut/Hi Cut filters in the
    // feedback path so repeats get progressively darker/thinner like a real tape delay.
    class DelayControlsPanel : public juce::Component
    {
    public:
        DelayControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label timeLabel { {}, "Time" }, feedbackLabel { {}, "Feedback" }, saturationLabel { {}, "Saturation" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" }, lowCutLabel { {}, "Low Cut" }, hiCutLabel { {}, "Hi Cut" };

        juce::Slider timeSlider, feedbackSlider, saturationSlider, mixSlider, outputSlider, lowCutSlider, hiCutSlider;
        juce::ToggleButton syncButton { "Sync" }, pingPongButton { "Ping-Pong" };
        juce::ComboBox divisionBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            timeAttachment, feedbackAttachment, saturationAttachment, mixAttachment, outputAttachment,
            lowCutAttachment, hiCutAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, pingPongAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayControlsPanel)
    };

    // Dynamics module controls: Threshold/Ratio/Attack/Release/Makeup/Mix.
    class DynamicsControlsPanel : public juce::Component
    {
    public:
        DynamicsControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label thresholdLabel { {}, "Threshold" }, ratioLabel { {}, "Ratio" }, attackLabel { {}, "Attack" },
                    releaseLabel { {}, "Release" }, makeupLabel { {}, "Makeup" }, mixLabel { {}, "Mix" };

        juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, makeupSlider, mixSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment, makeupAttachment, mixAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DynamicsControlsPanel)
    };

    // Convolution module controls: pick an IR from the curated factory library (or anything
    // dropped into the Custom folder) via a dropdown grouped by category, see its waveform, and
    // shape it with Tone/Fade In/Fade Out/Stretch, plus Mix/Output.
    class ConvolutionControlsPanel : public juce::Component
    {
    public:
        ConvolutionControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        void stepIr (int direction);

        juce::ComboBox irBox;
        juce::TextButton prevIrButton { "<" }, nextIrButton { ">" };
        juce::TextButton openFolderButton { "..." };
        juce::AudioParameterChoice* irIndexParam = nullptr; // for the </> step buttons -- see stepIr()

        IRWaveformComponent waveform;

        juce::Label toneLabel { {}, "Tone" }, fadeInLabel { {}, "Fade In" }, fadeOutLabel { {}, "Fade Out" },
                    stretchLabel { {}, "Stretch" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider toneSlider, fadeInSlider, fadeOutSlider, stretchSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            toneAttachment, fadeInAttachment, fadeOutAttachment, stretchAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvolutionControlsPanel)
    };

    // Utility module controls: Gain/Pan/Width knobs plus Mono/Phase Invert L/R toggles --
    // mirrors Ableton's Utility device.
    class UtilityControlsPanel : public juce::Component
    {
    public:
        UtilityControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label gainLabel { {}, "Gain" }, panLabel { {}, "Pan" }, widthLabel { {}, "Width" };
        juce::Slider gainSlider, panSlider, widthSlider;
        juce::ToggleButton monoButton { "Mono" }, phaseInvertLButton { "Phase L" }, phaseInvertRButton { "Phase R" };

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, panAttachment, widthAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment, phaseInvertLAttachment, phaseInvertRAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UtilityControlsPanel)
    };

    // Ring Mod / Frequency Shifter controls: Frequency/Fine/Mix/Output knobs plus the Mode
    // dropdown that switches between plain ring modulation and true single-sideband frequency
    // shifting -- see RingModModule for what each mode actually does with Frequency/Fine.
    class RingModControlsPanel : public juce::Component
    {
    public:
        RingModControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label frequencyLabel { {}, "Frequency" }, fineLabel { {}, "Fine" }, mixLabel { {}, "Mix" },
                    outputLabel { {}, "Output" }, modeLabel { {}, "Mode" };

        juce::Slider frequencySlider, fineSlider, mixSlider, outputSlider;
        juce::ComboBox modeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, fineAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingModControlsPanel)
    };

    class LfoCurveEditor : public juce::Component, private juce::Timer
    {
    public:
        LfoCurveEditor (RackSlot& rackSlotIn, juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~LfoCurveEditor() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        LFOModule* getModule() const;
        void beginEditFromCurrentShape();
        bool isCurveModifierDown (const juce::ModifierKeys& mods) const;
        void updateHoverCurveSegment (const juce::MouseEvent& e);
        juce::Point<float> pixelToPoint (juce::Point<float> pixel) const;
        juce::Point<float> pointToPixel (juce::Point<float> point) const;
        int hitTestPoint (juce::Point<float> pixel) const;
        int segmentAtX (float normalisedX) const;
        float previewValueAt (float phase01) const;

        RackSlot& rackSlot;
        std::atomic<float>* shapeParam = nullptr;
        int draggingPoint = -1;
        int draggingCurveSegment = -1;
        int hoverCurveSegment = -1;
        juce::Point<float> curveDragStart;
        float curveStartValue = 0.0f;
        bool drawMode = false;

        static constexpr int customShapeIndex = 7;
        static constexpr float grabToleranceSquaredPx = 10.0f * 10.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoCurveEditor)
    };

    // LFO controls: drawable shape editor/preview, Rate/Depth knobs, a Shape dropdown, and the
    // same Sync toggle + Division dropdown pattern DelayControlsPanel uses. No getModTarget* --
    // LFO is a modulation SOURCE, not a destination (see NodeComponent::isModulationSourceType).
    class LfoControlsPanel : public juce::Component, private juce::ComboBox::Listener, private juce::Timer
    {
    public:
        LfoControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);
        ~LfoControlsPanel() override;

        void resized() override;

    private:
        void comboBoxChanged (juce::ComboBox* comboBoxThatHasChanged) override;
        void timerCallback() override;
        LFOModule* getModule() const;
        void refreshShapeLabels();

        RackSlot& rackSlot;
        LfoCurveEditor curveEditor;
        juce::Label rateLabel { {}, "Rate" }, depthLabel { {}, "Depth" };
        juce::Slider rateSlider, depthSlider;
        juce::ComboBox shapeBox, divisionBox;
        juce::ToggleButton syncButton { "Sync" };
        bool ignoreShapeBoxChange = false;
        int lastShapeIndex = -1;
        bool lastCustomEdited = false;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment, depthAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment, divisionAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoControlsPanel)
    };

    class LfoTablePreviewComponent : public juce::Component, private juce::Timer
    {
    public:
        LfoTablePreviewComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~LfoTablePreviewComponent() override;

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;
        std::shared_ptr<const WavetableLibrary::Table> getTable();

        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* frameParam = nullptr;
        std::atomic<float>* smoothParam = nullptr;
        std::atomic<float>* phaseParam = nullptr;

        int loadedIndex = -1;
        std::shared_ptr<const WavetableLibrary::Table> table;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoTablePreviewComponent)
    };

    class LfoTableControlsPanel : public juce::Component
    {
    public:
        LfoTableControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

    private:
        void stepTable (int direction);

        LfoTablePreviewComponent preview;

        juce::ComboBox tableBox, divisionBox;
        juce::TextButton prevTableButton { "<" }, nextTableButton { ">" };
        juce::ToggleButton syncButton { "Sync" }, retriggerButton { "Retrig" };
        juce::Label frameLabel { {}, "Frame" }, smoothLabel { {}, "Smooth" }, phaseLabel { {}, "Phase" },
                    rateLabel { {}, "Rate" }, depthLabel { {}, "Depth" };
        juce::Slider frameSlider, smoothSlider, phaseSlider, rateSlider, depthSlider;

        juce::AudioParameterChoice* tableIndexParam = nullptr;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tableAttachment, divisionAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, retriggerAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frameAttachment, smoothAttachment, phaseAttachment, rateAttachment, depthAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoTableControlsPanel)
    };

    // ADSR controls: Attack/Decay/Sustain/Release knobs -- see AdsrModule for the sustain-while-
    // held/release-on-note-off behaviour these drive. No getModTarget* -- ADSR is a modulation
    // SOURCE, not a destination (see NodeComponent::isModulationSourceType), same reasoning as
    // LfoControlsPanel above.
    class AdsrControlsPanel : public juce::Component
    {
    public:
        AdsrControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

    private:
        juce::Label attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" }, sustainLabel { {}, "Sustain" }, releaseLabel { {}, "Release" };
        juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            attackAttachment, decayAttachment, sustainAttachment, releaseAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdsrControlsPanel)
    };

    // Envelope controls: the freeform breakpoint editor (EnvelopeBreakpointEditor) plus Length/
    // Depth knobs -- see EnvelopeModule for the one-shot-per-note-on playback these drive. No
    // getModTarget* -- Envelope is a modulation SOURCE, not a destination, same reasoning as
    // LfoControlsPanel above.
    class EnvelopeControlsPanel : public juce::Component
    {
    public:
        EnvelopeControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);

        void resized() override;

    private:
        EnvelopeBreakpointEditor editor;

        juce::Label lengthLabel { {}, "Length" }, depthLabel { {}, "Depth" };
        juce::Slider lengthSlider, depthSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lengthAttachment, depthAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeControlsPanel)
    };

    // Lossy controls: Bits/Rate/Jitter/Mix/Output -- see LossyModule for the full spectral
    // "codec-style" algorithm each of these drives.
    class LossyControlsPanel : public juce::Component
    {
    public:
        LossyControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label bitsLabel { {}, "Bits" }, rateLabel { {}, "Rate" }, jitterLabel { {}, "Jitter" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider bitsSlider, rateSlider, jitterSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            bitsAttachment, rateAttachment, jitterAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LossyControlsPanel)
    };

    // EQ 8 controls: Eq8CurveEditor (draggable graphic-EQ curve -- primary interaction) on top,
    // then one shared Enable/Type/Freq/Gain/Q knob set that retargets to whichever band was just
    // clicked on the curve (exactly like MultibandConvolutionControlsPanel's own band-select
    // pattern -- 8 full knob columns don't fit this panel's ~380px width, so a single retargeting
    // set stands in as the precise/numeric fallback to the curve's drag gestures), plus Mix/
    // Output. See Eq8Module/Eq8CurveEditor.
    class Eq8ControlsPanel : public juce::Component
    {
    public:
        Eq8ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        // Rebuilds the Enable/Type/Freq/Gain/Q attachments to point at `band`'s parameters --
        // called once at construction and again every time the curve editor's onBandSelected
        // fires. See MultibandConvolutionControlsPanel::setActiveBand's identical reasoning.
        void setActiveBand (int band);

        juce::AudioProcessorValueTreeState& apvtsRef;
        int slotIndexValue;

        Eq8CurveEditor curveEditor;

        juce::Label bandNameLabel;
        juce::ToggleButton enableButton { "On" };
        juce::ComboBox typeBox;
        juce::Label freqLabel { {}, "Freq" }, gainLabel { {}, "Gain" }, qLabel { {}, "Q" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };
        juce::Slider freqSlider, gainSlider, qSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            freqAttachment, gainAttachment, qAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Eq8ControlsPanel)
    };

    // Chorus/Flanger controls: Rate/Depth/Delay knobs, Feedback/Mix/Output, and the Mode
    // dropdown that switches between the two related characters -- see ChorusModule for what
    // Mode actually changes in the signal path (not just a cosmetic label).
    class ChorusControlsPanel : public juce::Component
    {
    public:
        ChorusControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label rateLabel { {}, "Rate" }, depthLabel { {}, "Depth" }, delayLabel { {}, "Delay" },
                    feedbackLabel { {}, "Feedback" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" },
                    modeLabel { {}, "Mode" };

        juce::Slider rateSlider, depthSlider, delaySlider, feedbackSlider, mixSlider, outputSlider;
        juce::ComboBox modeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            rateAttachment, depthAttachment, delayAttachment, feedbackAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChorusControlsPanel)
    };

    // EQ 3 controls: Low/Mid/High gain knobs (shelf/bell/shelf, fixed frequencies) plus
    // Mix/Output -- EQ 8's lighter sibling, see Eq3Module.
    class Eq3ControlsPanel : public juce::Component
    {
    public:
        Eq3ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label lowLabel { {}, "Low" }, midLabel { {}, "Mid" }, highLabel { {}, "High" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider lowSlider, midSlider, highSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            lowAttachment, midAttachment, highAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Eq3ControlsPanel)
    };

    // Multiband Convolution controls: a CrossoverSplitBar for the 2 draggable split points, which
    // doubles as a Low/Mid/High tab switcher -- ONE shared IR dropdown + Tone/Fade In/Fade Out/
    // Stretch/Mix/Output knob set (matching ConvolutionControlsPanel's own knob set, minus its
    // waveform display and prev/next/open-folder buttons) that retargets to whichever band is
    // currently selected, exactly like the original MultibandConvolver desktop app's tabbed
    // layout -- rather than showing all 3 bands' controls stacked at once, which was the first
    // version built here and is what this replaced.
    class MultibandConvolutionControlsPanel : public juce::Component
    {
    public:
        MultibandConvolutionControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);

        void resized() override;

        // Scoped to the currently selected band's 6 knobs only -- a cable already patched to a
        // hidden band's parameter keeps working (ModulationMatrix is keyed by paramId, not by
        // whether this GUI happens to be showing it), it just has nowhere valid to draw its nub
        // to until that band is selected again (NodeGraphEditor::modTargetPositionFor falls back
        // to the node's own corner in that case rather than failing).
        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        // Rebuilds the 6 attachments (and modTargets) to point at `band`'s parameters instead of
        // whichever band was previously showing -- called once at construction and again every
        // time CrossoverSplitBar's onBandSelected fires. Recreating a SliderAttachment/
        // ComboBoxAttachment both rewires the knob and immediately syncs its displayed value from
        // the new parameter, so the knobs visibly snap to the newly-selected band's values.
        void setActiveBand (int band);

        juce::AudioProcessorValueTreeState& apvtsRef;
        int slotIndexValue;

        CrossoverSplitBar splitBar;

        juce::Label nameLabel;
        juce::ComboBox irBox;
        juce::Label toneLabel { {}, "Tone" }, fadeInLabel { {}, "Fade In" }, fadeOutLabel { {}, "Fade Out" },
                    stretchLabel { {}, "Stretch" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };
        juce::Slider toneSlider, fadeInSlider, fadeOutSlider, stretchSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> irAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            toneAttachment, fadeInAttachment, fadeOutAttachment, stretchAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultibandConvolutionControlsPanel)
    };

    // Multipass controls: a CrossoverSplitBar for the 2 draggable split points (band-select
    // clicks are simply ignored here -- unlike Multiband Convolution there's no per-band knob set
    // to retarget) plus all 3 bands' Gain knobs shown together, since 3 knobs comfortably fit side
    // by side without needing a tab-selection scheme. See MultipassModule/Identifiers.h's
    // Multipass* comment for why Gain (a plain dB trim, for re-levelling bands relative to each
    // other) rather than Mix is the only knob this module has.
    class MultipassControlsPanel : public juce::Component
    {
    public:
        MultipassControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        CrossoverSplitBar splitBar;

        std::array<juce::Label, kNumMultipassBands> gainLabels;
        std::array<juce::Slider, kNumMultipassBands> gainSliders;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kNumMultipassBands> gainAttachments;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultipassControlsPanel)
    };

    // 3xOsc controls: 3 oscillator sections (Waveform dropdown + Coarse/Fine/Pan/Level knobs
    // each), then a shared ADSR amp-envelope row and an FM/Output row -- see ThreeOscModule for
    // the synth engine these drive. Unlike Convolution/Multiband Convolution, this module has no
    // waveform/IR display -- the "instrument" here is purely the knob values, nothing to preview.
    class ThreeOscControlsPanel : public juce::Component
    {
    public:
        ThreeOscControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        struct OscControls
        {
            juce::Label nameLabel;
            juce::ComboBox waveformBox;
            juce::Label coarseLabel { {}, "Coarse" }, fineLabel { {}, "Fine" }, panLabel { {}, "Pan" }, levelLabel { {}, "Level" };
            juce::Slider coarseSlider, fineSlider, panSlider, levelSlider;

            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
                coarseAttachment, fineAttachment, panAttachment, levelAttachment;
        };

        std::array<OscControls, kNumThreeOscOscillators> oscs;

        juce::Label attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" }, sustainLabel { {}, "Sustain" }, releaseLabel { {}, "Release" },
                    fm1to2Label { {}, "FM 1>2" }, fm2to3Label { {}, "FM 2>3" }, outputLabel { {}, "Output" };
        juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider, fm1to2Slider, fm2to3Slider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            attackAttachment, decayAttachment, sustainAttachment, releaseAttachment,
            fm1to2Attachment, fm2to3Attachment, outputAttachment;

        juce::ToggleButton monoLegatoButton { "Mono/Legato" }, glideButton { "Glide" };
        juce::Label glideTimeLabel { {}, "Glide Time" };
        juce::Slider glideTimeSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoLegatoAttachment, glideAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideTimeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeOscControlsPanel)
    };
}
