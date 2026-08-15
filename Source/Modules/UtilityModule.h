#pragma once

#include "../Rack/RackModule.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    // Pure gain-staging/imaging utility, mirroring Ableton's Utility device: Gain trim, Pan
    // (balance, for already-stereo material), Width (mid/side stereo width), Mono (collapse to
    // mono), and independent Phase Invert per channel. No coloration/saturation of its own --
    // this is a plumbing tool, not a distortion module.
    class UtilityModule : public RackModule
    {
    public:
        UtilityModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        int slotIndex;

        std::atomic<float>* gainParam;
        std::atomic<float>* panParam;
        std::atomic<float>* widthParam;
        std::atomic<float>* monoParam;
        std::atomic<float>* phaseInvertLParam;
        std::atomic<float>* phaseInvertRParam;
    };
}
