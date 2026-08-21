#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Params/Identifiers.h"
#include "../Rack/RackSlot.h"
#include "../Modules/LFOModule.h"
#include "../Modules/SamplerModule.h"
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

    class FilterResponseEditor : public juce::Component, private juce::Timer
    {
    public:
        FilterResponseEditor (juce::AudioProcessorValueTreeState& apvts,
                              juce::String frequencyParamId,
                              juce::String resonanceParamId,
                              juce::String driveParamId,
                              juce::String modeParamId = {},
                              juce::String morphParamId = {},
                              juce::String distortionParamId = {});
        ~FilterResponseEditor() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void timerCallback() override { repaint(); }
        void updateFromMouse (juce::Point<float> pos, const juce::ModifierKeys& mods);
        float getFrequency() const;
        float getResonance01() const;
        float getDrive01() const;
        float getMorph01() const;
        int getMode() const;
        int getDistortion() const;
        float responseAt (float hz, float cutoff, float resonance01, float drive01, float morph01, int mode, int distortion) const;
        void setFloatParam (juce::AudioParameterFloat* param, float value);

        juce::AudioParameterFloat* frequencyParam = nullptr;
        juce::AudioParameterFloat* resonanceParam = nullptr;
        juce::AudioParameterFloat* driveParam = nullptr;
        juce::AudioParameterFloat* morphParam = nullptr;
        juce::AudioParameterChoice* modeParam = nullptr;
        juce::AudioParameterChoice* distortionParam = nullptr;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterResponseEditor)
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
        FilterResponseEditor responseEditor;

        juce::Label frequencyLabel { {}, "Frequency" }, resonanceLabel { {}, "Resonance" }, driveLabel { {}, "Drive" }, feedbackLabel { {}, "Feedback" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" }, typeLabel { {}, "Type" };

        juce::Slider frequencySlider, resonanceSlider, driveSlider, feedbackSlider, mixSlider, outputSlider;
        juce::ComboBox typeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, resonanceAttachment, driveAttachment, feedbackAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterControlsPanel)
    };

    class NonlinearFilterControlsPanel : public juce::Component
    {
    public:
        NonlinearFilterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        FilterResponseEditor responseEditor;

        juce::Label frequencyLabel { {}, "Frequency" }, resonanceLabel { {}, "Resonance" }, driveLabel { {}, "Drive" },
                    morphLabel { {}, "Morph" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" },
                    modeLabel { {}, "Mode" }, distortionLabel { {}, "Distortion" };

        juce::Slider frequencySlider, resonanceSlider, driveSlider, morphSlider, mixSlider, outputSlider;
        juce::ComboBox modeBox, distortionBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frequencyAttachment, resonanceAttachment, driveAttachment, morphAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAttachment, distortionAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NonlinearFilterControlsPanel)
    };

    class MackityControlsPanel : public juce::Component
    {
    public:
        MackityControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label inputLabel { {}, "Input" }, padLabel { {}, "Out Pad" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };
        juce::Slider inputSlider, padSlider, mixSlider, outputSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            inputAttachment, padAttachment, mixAttachment, outputAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MackityControlsPanel)
    };

    class ShimmerReverbControlsPanel : public juce::Component
    {
    public:
        ShimmerReverbControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label sizeLabel { {}, "Size" }, feedbackLabel { {}, "Feedback" }, diffusionLabel { {}, "Diffusion" },
                    shiftLabel { {}, "Shift" }, modRateLabel { {}, "Mod Rate" }, modDepthLabel { {}, "Mod Depth" },
                    lowCutLabel { {}, "Low Cut" }, highCutLabel { {}, "High Cut" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" },
                    pitchModeLabel { {}, "Pitch Mode" }, colorLabel { {}, "Color" };

        juce::Slider sizeSlider, feedbackSlider, diffusionSlider, shiftSlider, modRateSlider, modDepthSlider,
                     lowCutSlider, highCutSlider, mixSlider, outputSlider;
        juce::ComboBox pitchModeBox, colorBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            sizeAttachment, feedbackAttachment, diffusionAttachment, shiftAttachment, modRateAttachment,
            modDepthAttachment, lowCutAttachment, highCutAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> pitchModeAttachment, colorAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShimmerReverbControlsPanel)
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

    // Compressor module controls: Threshold/Ratio/Attack/Release/Knee/Makeup/Mix, plus a Peak/RMS
    // Detection dropdown -- see CompressorModule for the actual soft-knee/decoupled-ballistics
    // DSP this drives.
    class CompressorControlsPanel : public juce::Component
    {
    public:
        CompressorControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label thresholdLabel { {}, "Threshold" }, ratioLabel { {}, "Ratio" }, attackLabel { {}, "Attack" },
                    releaseLabel { {}, "Release" }, kneeLabel { {}, "Knee" }, makeupLabel { {}, "Makeup" },
                    mixLabel { {}, "Mix" }, detectionLabel { {}, "Detection" };

        juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, kneeSlider, makeupSlider, mixSlider;
        juce::ComboBox detectionBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            thresholdAttachment, ratioAttachment, attackAttachment, releaseAttachment, kneeAttachment, makeupAttachment, mixAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> detectionAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorControlsPanel)
    };

    // Limiter module controls: Gain/Ceiling/Release -- see LimiterModule.
    class LimiterControlsPanel : public juce::Component
    {
    public:
        LimiterControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label gainLabel { {}, "Gain" }, ceilingLabel { {}, "Ceiling" }, releaseLabel { {}, "Release" };

        juce::Slider gainSlider, ceilingSlider, releaseSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            gainAttachment, ceilingAttachment, releaseAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LimiterControlsPanel)
    };

    class GranularControlsPanel : public juce::Component, private juce::Timer
    {
    public:
        GranularControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~GranularControlsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        void timerCallback() override { repaint (previewArea); }

        std::atomic<float>* sizeParam = nullptr;
        std::atomic<float>* densityParam = nullptr;
        std::atomic<float>* positionParam = nullptr;
        std::atomic<float>* jitterParam = nullptr;
        std::atomic<float>* pitchParam = nullptr;
        std::atomic<float>* spreadParam = nullptr;
        std::atomic<float>* freezeParam = nullptr;

        juce::Rectangle<int> previewArea;
        float animationPhase = 0.0f;

        juce::Label sizeLabel { {}, "Size" }, densityLabel { {}, "Density" }, positionLabel { {}, "Position" },
                    jitterLabel { {}, "Jitter" }, pitchLabel { {}, "Pitch" }, spreadLabel { {}, "Spread" },
                    feedbackLabel { {}, "Feedback" }, mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };
        juce::Slider sizeSlider, densitySlider, positionSlider, jitterSlider, pitchSlider, spreadSlider,
                     feedbackSlider, mixSlider, outputSlider;
        juce::ToggleButton freezeButton { "Freeze" };

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            sizeAttachment, densityAttachment, positionAttachment, jitterAttachment, pitchAttachment,
            spreadAttachment, feedbackAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranularControlsPanel)
    };

    // Sampler module controls: drag-and-drop a zone strip (click a zone to select it for
    // editing, drop audio files to create new ones), a waveform display of the selected zone
    // with Start/End/Loop markers, a knob row for the selected zone's key/velocity range, root/
    // start/end/loop, and a global knob row (Voices/Glide/amp envelope/Output). Zone data isn't
    // an APVTS parameter (see SamplerModule's own comment) -- the zone knobs below read/write
    // SamplerModule::getZone/setZone directly instead of using SliderAttachment, polled/pushed
    // via this panel's own Timer, the same reconciliation approach IRWaveformComponent already
    // uses for RackSlot's live module instance.
    class SamplerControlsPanel : public juce::Component, private juce::Timer
    {
    public:
        SamplerControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex, RackSlot& rackSlot);
        ~SamplerControlsPanel() override;

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        void timerCallback() override;
        void refreshFromModule();
        void pushZoneEdit();
        void selectZone (int zoneIndex);

        // The zone key-range strip: click a zone bar to select it, drop audio files onto it to
        // create new zones (auto-spread across the full key range if dropped with no existing
        // zones, otherwise each new zone starts full-range and is narrowed via the Key Lo/Hi
        // knobs below).
        class ZoneStrip : public juce::Component, public juce::FileDragAndDropTarget
        {
        public:
            explicit ZoneStrip (RackSlot& rackSlotIn) : rackSlot (rackSlotIn) {}

            std::function<void (int)> onZoneSelected;
            std::function<void (const juce::StringArray&)> onFilesDropped;
            // Fired continuously while a drag-edit (resize/move) is in progress, so the owning
            // panel's zone knobs can mirror the live edit instead of only updating once the mouse
            // is released.
            std::function<void()> onZoneEdited;

            int selectedZoneIndex = -1;

            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;

            bool isInterestedInFileDrag (const juce::StringArray&) override;
            void fileDragEnter (const juce::StringArray&, int, int) override { hovering = true; repaint(); }
            void fileDragExit (const juce::StringArray&) override { hovering = false; repaint(); }
            void filesDropped (const juce::StringArray&, int, int) override;

        private:
            // Piano-reference band pinned to the bottom of the strip -- everything above it is
            // the zone-bar area proper (see paint()/mouseDown()'s own key-position math, which
            // both use getHeight() minus this).
            static constexpr int keyReferenceHeight = 14;
            static constexpr int edgeGrabPixels = 5; // how close to a bar's edge counts as "grab the edge" vs. "grab the body"

            int noteForX (int x) const { return juce::jlimit (0, 127, (int) ((float) x / (float) getWidth() * 128.0f)); }
            int xForNote (int note) const { return (int) ((float) getWidth() * ((float) note / 128.0f)); }

            enum class DragMode { none, resizeLeft, resizeRight, move };
            DragMode dragMode = DragMode::none;
            int dragZoneIndex = -1;
            int dragStartNote = 0;
            int dragStartKeyLow = 0, dragStartKeyHigh = 0;

            RackSlot& rackSlot;
            bool hovering = false;
        };

        // Direct min/max-per-pixel waveform draw of the selected zone's loaded sample, plus
        // draggable Start/End/Loop markers -- same non-AudioThumbnail approach as
        // IRWaveformComponent (see that class's own comment on why: these buffers are small
        // enough that disk-backed thumbnail caching would be pure overhead).
        class WaveformDisplay : public juce::Component
        {
        public:
            explicit WaveformDisplay (RackSlot& rackSlotIn) : rackSlot (rackSlotIn) {}

            int zoneIndex = -1;
            std::function<void()> onZoneEdited;

            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseDrag (const juce::MouseEvent&) override;
            void mouseUp (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;

        private:
            static constexpr int markerGrabPixels = 5;

            int sampleForX (int x, int numSamples) const;
            int xForSample (int sample, int numSamples) const;

            enum class DragMode { none, start, end, loopStart, loopEnd };
            DragMode dragMode = DragMode::none;

            RackSlot& rackSlot;
        };

        juce::AudioProcessorValueTreeState& apvtsRef;
        int slotIndexValue;
        RackSlot& rackSlotRef;

        int selectedZoneIndex = -1;
        bool suppressZoneCallbacks = false;

        ZoneStrip zoneStrip;
        WaveformDisplay waveformDisplay;

        juce::Label keyLoLabel { {}, "Key Lo" }, keyHiLabel { {}, "Key Hi" }, velLoLabel { {}, "Vel Lo" }, velHiLabel { {}, "Vel Hi" },
                    rootLabel { {}, "Root" }, startLabel { {}, "Start" }, endLabel { {}, "End" },
                    loopStartLabel { {}, "Loop Start" }, loopEndLabel { {}, "Loop End" };
        juce::Slider keyLoSlider, keyHiSlider, velLoSlider, velHiSlider, rootSlider, startSlider, endSlider,
                     loopStartSlider, loopEndSlider;
        juce::ToggleButton loopEnabledButton { "Loop" };
        juce::TextButton removeZoneButton { "Remove Zone" };

        juce::Label voicesLabel { {}, "Voices" }, attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" },
                    sustainLabel { {}, "Sustain" }, releaseLabel { {}, "Release" }, outputLabel { {}, "Output" },
                    glideTimeLabel { {}, "Glide Time" };
        juce::Slider voicesSlider, attackSlider, decaySlider, sustainSlider, releaseSlider, outputSlider, glideTimeSlider;
        juce::ToggleButton glideButton { "Glide" };

        // Live scrub depth for the loop window (-1..1, see SamplerParam::startMod/endMod's own
        // comment) -- a plain automatable knob like any other, so it can be set as a static offset
        // or, more interestingly, driven by an LFO/Envelope mod cable for granular-style scanning.
        juce::Label startModLabel { {}, "Start Mod" }, endModLabel { {}, "End Mod" };
        juce::Slider startModSlider, endModSlider;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            voicesAttachment, attackAttachment, decayAttachment, sustainAttachment, releaseAttachment,
            outputAttachment, glideTimeAttachment, startModAttachment, endModAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> glideAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerControlsPanel)
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

    // Spectral Clipper controls: Drive/Ceiling/Mix/Output knobs plus a Shape dropdown (Hard/
    // Soft/Foldback/Sine Fold) picking how per-bin magnitude overshoot above Ceiling gets brought
    // back down -- see SpectralClipperModule for the actual FFT-domain clipping algorithm.
    class SpectralClipperControlsPanel : public juce::Component
    {
    public:
        SpectralClipperControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        juce::Label driveLabel { {}, "Drive" }, ceilingLabel { {}, "Ceiling" }, shapeLabel { {}, "Shape" },
                    mixLabel { {}, "Mix" }, outputLabel { {}, "Output" };

        juce::Slider driveSlider, ceilingSlider, mixSlider, outputSlider;
        juce::ComboBox shapeBox;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            driveAttachment, ceilingAttachment, mixAttachment, outputAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectralClipperControlsPanel)
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

    class WavetableSynthPreviewComponent : public juce::Component, private juce::Timer
    {
    public:
        WavetableSynthPreviewComponent (juce::AudioProcessorValueTreeState& apvts, int slotIndex);
        ~WavetableSynthPreviewComponent() override;

        void paint (juce::Graphics&) override;
        void setGeneratorIndex (int index);

    private:
        void timerCallback() override { repaint(); }
        std::shared_ptr<const WavetableLibrary::Table> getTable();

        juce::AudioProcessorValueTreeState& apvts;
        int slotIndex = 0;
        int generatorIndex = 0;
        int loadedIndex = -1;
        std::shared_ptr<const WavetableLibrary::Table> table;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableSynthPreviewComponent)
    };

    class WavetableSynthControlsPanel : public juce::Component
    {
    public:
        WavetableSynthControlsPanel (juce::AudioProcessorValueTreeState& apvts, int slotIndex);

        void paint (juce::Graphics& g) override;
        void resized() override;

        int getModTargetCount() const { return (int) modTargets.size(); }
        const ModTarget& getModTarget (int index) const { return modTargets[(size_t) index]; }

    private:
        void selectGenerator (int index);
        void rebindGeneratorControls();
        void refreshGeneratorButtons();
        void setSelectedGeneratorOutput (int outputIndex);
        void stepSelectedTable (int direction);
        void enableNextGenerator();
        void updateTableName();
        void showTableSearchPopup();
        void setSelectedTableIndex (int index);

        WavetableSynthPreviewComponent preview;

        juce::TextButton addGeneratorButton { "+  Add Generator" };
        std::array<juce::TextButton, kNumWavetableSynthGenerators> generatorButtons;
        juce::ToggleButton generatorEnabledButton { "On" };
        juce::TextButton prevTableButton { "<" }, nextTableButton { ">" }, searchTableButton { "Search" };
        juce::ComboBox tableBox;
        juce::Label tableLabel { {}, "Table" }, tableNameLabel;
        std::array<juce::TextButton, kNumWavetableSynthOutputs> outputButtons;
        juce::Label algorithmLabel { {}, "Algorithm" }, algorithmHintLabel, multiplierLabel { {}, "Multiply" };
        juce::ComboBox algorithmBox, multiplierBox;

        juce::Label frameLabel { {}, "Frame" }, smoothLabel { {}, "Smooth" }, coarseLabel { {}, "Coarse" }, fineLabel { {}, "Fine" },
                    panLabel { {}, "Pan" }, levelLabel { {}, "Level" }, fmLabel { {}, "FM" };
        juce::Slider frameSlider, smoothSlider, coarseSlider, fineSlider, panSlider, levelSlider, fmSlider;

        juce::Label attackLabel { {}, "A" }, decayLabel { {}, "D" }, sustainLabel { {}, "S" }, releaseLabel { {}, "R" },
                    outputLabel { {}, "Output" }, glideTimeLabel { {}, "Glide" },
                    polyphonyLabel { {}, "Polyphony" }, masterPitchLabel { {}, "Master Pitch" }, bendRangeLabel { {}, "Bend Range" },
                    unisonLabel { {}, "Unison" }, spreadLabel { {}, "Spread" };
        juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider, outputSlider, glideTimeSlider,
                     polyphonySlider, masterPitchSlider, bendRangeSlider, unisonSlider, spreadSlider;
        juce::ToggleButton monoLegatoButton { "Mono" }, glideButton { "Glide" };

        juce::Rectangle<int> generatorColumnBounds, previewBounds, outputColumnBounds, voiceStripBounds;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            attackAttachment, decayAttachment, sustainAttachment, releaseAttachment, outputAttachment, glideTimeAttachment,
            polyphonyAttachment, masterPitchAttachment, bendRangeAttachment, unisonAttachment, spreadAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> monoLegatoAttachment, glideAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> algorithmAttachment, multiplierAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> generatorEnabledAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> tableAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            frameAttachment, smoothAttachment, coarseAttachment, fineAttachment, panAttachment, levelAttachment, fmAttachment;

        juce::AudioProcessorValueTreeState& apvts;
        int slotIndex = 0;
        int selectedGenerator = 0;
        juce::String algorithmHintText;

        std::vector<ModTarget> modTargets;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableSynthControlsPanel)
    };
}
