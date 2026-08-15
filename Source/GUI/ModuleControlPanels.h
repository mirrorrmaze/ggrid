#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include "../Rack/RackSlot.h"
#include "IRWaveformComponent.h"

namespace GGrid
{
    // Controls for whichever module type is currently active in a node -- one panel class per
    // rack module type, all shown/hidden by NodeComponent depending on the node's current type.
    class WaveshaperControlsPanel : public juce::Component
    {
    public:
        WaveshaperControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        // Drive's knob bounds, in this panel's own coordinate space -- for NodeComponent to
        // position a modulation-destination nub against (see NodeComponent::getModDestinationPosition).
        juce::Rectangle<int> getModTargetKnobBounds() const { return driveSlider.getBounds(); }

    private:
        juce::Label driveLabel { {}, "Drive" }, shapeLabel { {}, "Shape" }, symmetryLabel { {}, "Symmetry" },
                    foldLabel { {}, "Fold" }, oversampleLabel { {}, "Oversample" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider driveSlider, symmetrySlider, foldSlider, mixSlider, outputSlider;
        juce::ComboBox shapeBox, oversampleBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            driveAttachment, symmetryAttachment, foldAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            shapeAttachment, oversampleAttachment;

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

        juce::Rectangle<int> getModTargetKnobBounds() const { return frequencySlider.getBounds(); }

    private:
        juce::Label frequencyLabel { {}, "Frequency" }, resonanceLabel { {}, "Resonance" }, feedbackLabel { {}, "Feedback" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" }, typeLabel { {}, "Type" };

        juce::Slider frequencySlider, resonanceSlider, feedbackSlider, mixSlider, outputSlider;
        juce::ComboBox typeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, resonanceAttachment, feedbackAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;

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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DelayControlsPanel)
    };

    // Dynamics module controls: Threshold/Ratio/Attack/Release/Makeup/Mix.
    class DynamicsControlsPanel : public juce::Component
    {
    public:
        DynamicsControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

    private:
        juce::Label thresholdLabel { {}, "Threshold" }, ratioLabel { {}, "Ratio" }, attackLabel { {}, "Attack" },
                    releaseLabel { {}, "Release" }, makeupLabel { {}, "Makeup" }, mixLabel { {}, "Mix" };

        juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, makeupSlider, mixSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment, makeupAttachment, mixAttachment;

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

        juce::Rectangle<int> getModTargetKnobBounds() const { return mixSlider.getBounds(); }

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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ConvolutionControlsPanel)
    };

    // Utility module controls: Gain/Pan/Width knobs plus Mono/Phase Invert L/R toggles --
    // mirrors Ableton's Utility device.
    class UtilityControlsPanel : public juce::Component
    {
    public:
        UtilityControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

    private:
        juce::Label gainLabel { {}, "Gain" }, panLabel { {}, "Pan" }, widthLabel { {}, "Width" };
        juce::Slider gainSlider, panSlider, widthSlider;
        juce::ToggleButton monoButton { "Mono" }, phaseInvertLButton { "Phase L" }, phaseInvertRButton { "Phase R" };

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment, panAttachment, widthAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoAttachment, phaseInvertLAttachment, phaseInvertRAttachment;

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

    private:
        juce::Label frequencyLabel { {}, "Frequency" }, fineLabel { {}, "Fine" }, mixLabel { {}, "Mix" },
                    outputLabel { {}, "Output" }, modeLabel { {}, "Mode" };

        juce::Slider frequencySlider, fineSlider, mixSlider, outputSlider;
        juce::ComboBox modeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, fineAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingModControlsPanel)
    };

    // LFO controls: Rate/Depth knobs, a Shape dropdown, and the same Sync toggle + Division
    // dropdown pattern DelayControlsPanel uses (Division only matters while Sync is on, but stays
    // visible either way for consistency with how Delay already does this).
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
}
