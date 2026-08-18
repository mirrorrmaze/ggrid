#pragma once

#include "../Rack/RackModule.h"
#include "../Rack/SharedServices.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    // A modulation-source module, not an audio effect: process() passes audio through completely
    // unchanged (so it's harmless if audio happens to be routed through it) and instead advances
    // a phase accumulator each block, producing a bipolar [-1, 1] value other modules read via
    // getCurrentValue() to modulate their own parameters over a modulation cable (see
    // ModulationMatrix's ModConnection list) -- NodeComponent hides this module's audio ports and
    // gives it a single modulation-output nub instead, matching that it isn't really part of the
    // audio graph.
    //
    // Ticked at block rate (once per process() call, using the block's total sample count), not
    // per-sample -- the value is constant within a block. Fine for the kind of slowly-moving
    // destinations this targets (filter cutoff, drive, mix); not sample-accurate for very fast
    // rates at large block sizes.
    class LFOModule : public RackModule
    {
    public:
        LFOModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        // Depth-scaled current output, [-1, 1] (or [0, 1] region-wise for Sample & Hold, which is
        // still bipolar over time). Safe to call from the audio thread only (no synchronisation --
        // this module's own process() is what updates it, and everything happens on the same
        // audio-processing call chain within a block).
        float getCurrentValue() const { return currentValue.load(); }

        bool isModulationSource() const override { return true; }
        float getCurrentModulationValue() const override { return getCurrentValue(); }

        void writeExtraState (juce::XmlElement& parent) const override;
        void readExtraState (const juce::XmlElement& parent) override;

        struct CustomPoint
        {
            float x = 0.0f;
            float y = 0.0f;     // bipolar [-1, 1]
            float curve = 0.0f; // segment curve from this point to the next, [-1, 1]
        };

        int getNumCustomPoints() const;
        CustomPoint getCustomPoint (int index) const;
        int addCustomPoint (juce::Point<float> p);
        void moveCustomPoint (int index, juce::Point<float> p);
        void removeCustomPoint (int index);
        void setSegmentCurve (int startPointIndex, float curve);
        float evaluateCustomAt (float phase01) const;
        float getPhasePosition() const { return (float) phase; }
        bool isCustomEdited() const { return customEdited.load(); }
        void setCustomEdited (bool shouldUseCustom) { customEdited.store (shouldUseCustom); }
        void seedCustomFromShape (int shapeIndex);

    private:
        static float computePresetShapeValue (int shape, float phase01, float sampleHoldFallback);
        float computeShapeValue (float phase01) const;
        float evaluateCustomAtLocked (float phase01) const;
        void sortAndClampCustomPoints();

        SharedServices& services;

        std::atomic<float>* shapeParam;
        std::atomic<float>* rateModeParam;
        std::atomic<float>* rateHzParam;
        std::atomic<float>* divisionParam;
        std::atomic<float>* depthParam;

        double sampleRate = 44100.0;
        double phase = 0.0; // 0..1

        juce::Random random;
        float sampleHoldValue = 0.0f;

        mutable juce::CriticalSection customPointsLock;
        std::array<CustomPoint, kMaxEnvelopePoints> customPoints;
        int numCustomPoints = 5;
        std::atomic<bool> customEdited { false };
        static constexpr float minPointGap = 0.005f;

        std::atomic<float> currentValue { 0.0f };
    };
}
