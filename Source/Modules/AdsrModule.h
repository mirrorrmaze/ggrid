#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

namespace GGrid
{
    // A standalone ADSR modulation source -- see AdsrParam's own comment in Identifiers.h for how
    // this differs from Envelope (sustain-while-held/release-on-note-off here, vs. Envelope's
    // fixed-length one-shot). Mono: a single juce::ADSR shared across however many notes are
    // held at once, last-note-wins for retriggering (every note-on restarts the attack), but the
    // release stage only begins once EVERY held note has been released -- releasing one note out
    // of a held chord doesn't cut the envelope early. Reports a unipolar 0-1 value via
    // RackModule::getCurrentModulationValue(), matching a classic amp/filter envelope's own range
    // (as opposed to LFO's bipolar convention).
    class AdsrModule : public RackModule
    {
    public:
        AdsrModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        bool isModulationSource() const override { return true; }
        float getCurrentModulationValue() const override { return currentValue.load(); }

    private:
        std::atomic<float>* attackParam;
        std::atomic<float>* decayParam;
        std::atomic<float>* sustainParam;
        std::atomic<float>* releaseParam;

        juce::ADSR envelope;
        double sampleRate = 44100.0;
        int heldNoteCount = 0;

        std::atomic<float> currentValue { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AdsrModule)
    };
}
