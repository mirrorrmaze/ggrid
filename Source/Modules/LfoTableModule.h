#pragma once

#include "../Rack/RackModule.h"
#include "../Rack/SharedServices.h"
#include "../Wavetable/WavetableLibrary.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    class LfoTableModule : public RackModule
    {
    public:
        LfoTableModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        bool isModulationSource() const override { return true; }
        float getCurrentModulationValue() const override { return currentValue.load(); }

    private:
        std::shared_ptr<const WavetableLibrary::Table> getCurrentTable();

        SharedServices& services;

        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* frameParam = nullptr;
        std::atomic<float>* smoothParam = nullptr;
        std::atomic<float>* phaseParam = nullptr;
        std::atomic<float>* rateModeParam = nullptr;
        std::atomic<float>* rateHzParam = nullptr;
        std::atomic<float>* divisionParam = nullptr;
        std::atomic<float>* depthParam = nullptr;
        std::atomic<float>* retriggerParam = nullptr;

        double sampleRate = 44100.0;
        double phase = 0.0;
        int loadedTableIndex = -1;
        std::shared_ptr<const WavetableLibrary::Table> currentTable;
        std::atomic<float> currentValue { 0.0f };
    };
}
