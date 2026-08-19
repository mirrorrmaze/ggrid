#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include "../Wavetable/WavetableLibrary.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace GGrid
{
    class WavetableSynthModule : public RackModule
    {
    public:
        WavetableSynthModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        int getNumOutputBuses() const override { return kNumWavetableSynthOutputs; }
        const juce::AudioBuffer<float>* getOutputBusBuffer (int busIndex) const override;

    private:
        struct Voice
        {
            bool active = false;
            int noteNumber = -1;
            float velocity = 0.0f;
            juce::int64 triggerOrder = 0;
            std::array<double, kNumWavetableSynthGenerators> phase {};
            juce::ADSR envelope;
        };

        struct GenParams
        {
            bool enabled = false;
            int table = 0;
            float frame = 0.0f;
            float smooth = 0.0f;
            float freqMultiplier = 1.0f;
            float pan = 0.0f;
            float level = 0.0f;
            float fm = 0.0f;
            int output = 0;
        };

        struct ResolvedParams
        {
            float attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f;
            float outputGain = 1.0f;
            int algorithm = 0;
            bool monoLegato = false;
            bool glide = false;
            float glideTimeMs = 50.0f;
            std::array<GenParams, kNumWavetableSynthGenerators> gens;
        };

        ResolvedParams resolveParams (const ModulationMatrix& modMatrix) const;
        std::shared_ptr<const WavetableLibrary::Table> getTable (int index);
        int findVoiceForNoteOn();
        void handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved);
        void handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved);
        void handleMonoNoteOff (int note, const ResolvedParams& resolved);
        void renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved);

        static float noteNumberToHz (float fractionalNoteNumber);

        int slotIndex;
        double sampleRate = 44100.0;

        std::atomic<float>* attackParam = nullptr;
        std::atomic<float>* decayParam = nullptr;
        std::atomic<float>* sustainParam = nullptr;
        std::atomic<float>* releaseParam = nullptr;
        std::atomic<float>* outputParam = nullptr;
        std::atomic<float>* algorithmParam = nullptr;
        std::atomic<float>* monoLegatoParam = nullptr;
        std::atomic<float>* glideParam = nullptr;
        std::atomic<float>* glideTimeMsParam = nullptr;

        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> enabledParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> tableParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> frameParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> smoothParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> coarseParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> fineParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> panParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> levelParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> fmParams {};
        std::array<std::atomic<float>*, kNumWavetableSynthGenerators> outputBusParams {};

        std::array<Voice, kMaxWavetableSynthVoices> voices;
        juce::int64 nextTriggerOrder = 0;
        std::array<int, kMaxWavetableSynthVoices> heldNoteStack {};
        int heldNoteStackSize = 0;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoNoteNumberSmoothed;

        std::array<int, kNumWavetableSynthGenerators> loadedTableIndices {};
        std::array<std::shared_ptr<const WavetableLibrary::Table>, kNumWavetableSynthGenerators> loadedTables;
        std::array<juce::AudioBuffer<float>, kNumWavetableSynthOutputs> outputBusBuffers;

        static constexpr float kFmModIndexScale = 5.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavetableSynthModule)
    };
}
