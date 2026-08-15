#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include "../Rack/RackSlot.h"
#include "IRWaveformComponent.h"
#include <vector>

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

    // LFO controls: Rate/Depth knobs, a Shape dropdown, and the same Sync toggle + Division
    // dropdown pattern DelayControlsPanel uses (Division only matters while Sync is on, but stays
    // visible either way for consistency with how Delay already does this). No getModTarget* --
    // LFO is a modulation SOURCE, not a destination (see NodeComponent::isLfoType).
    class LfoControlsPanel : public juce::Component
    {
    public:
        LfoControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

    private:
        juce::Label rateLabel { {}, "Rate" }, depthLabel { {}, "Depth" };
        juce::Slider rateSlider, depthSlider;
        juce::ComboBox shapeBox, divisionBox;
        juce::ToggleButton syncButton { "Sync" };

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment, depthAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment, divisionAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LfoControlsPanel)
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

    // EQ 8 controls: 8 fixed-frequency band gain knobs (100Hz-12.8kHz, one octave apart,
    // see kEq8BandFrequencies) plus Mix/Output -- see Eq8Module.
    class Eq8ControlsPanel : public juce::Component
    {
    public:
        Eq8ControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        std::array<juce::Label, kNumEq8Bands> bandLabels;
        std::array<juce::Slider, kNumEq8Bands> bandSliders;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, kNumEq8Bands> bandAttachments;

        juce::Label mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };
        juce::Slider mixSlider, outputSlider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, outputAttachment;

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
}
