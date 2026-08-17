#include "LFOModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    LFOModule::LFOModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : services (servicesIn),
          shapeParam    (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::shape))),
          rateModeParam (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::rateMode))),
          rateHzParam   (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::rateHz))),
          divisionParam (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::division))),
          depthParam    (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::depth))),
          random ((juce::int64) (juce::Random::getSystemRandom().nextInt64()))
    {
    }

    void LFOModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        phase = 0.0;
        sampleHoldValue = random.nextFloat() * 2.0f - 1.0f;
        currentValue.store (0.0f);
    }

    void LFOModule::reset()
    {
        phase = 0.0;
    }

    float LFOModule::computeShapeValue (float phase01) const
    {
        const int shape = (int) shapeParam->load();
        switch (shape)
        {
            case 0: // Sine
                return (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase01);

            case 1: // Triangle -- -1 at phase 0, +1 at phase 0.5, back to -1 at phase 1
                return phase01 < 0.5f ? (4.0f * phase01 - 1.0f) : (3.0f - 4.0f * phase01);

            case 2: // Square
                return phase01 < 0.5f ? 1.0f : -1.0f;

            case 3: // Saw
                return 2.0f * phase01 - 1.0f;

            case 4: // Sample & Hold -- handled by the caller (holds a value across the block,
                    // only re-rolled on phase wrap); this branch shouldn't normally be reached.
                return sampleHoldValue;

            case 5: // Ramp Up -- identical math to Saw, kept as a separate named entry (see
                    // getLfoShapeChoices' own comment)
                return 2.0f * phase01 - 1.0f;

            case 6: // Ramp Down -- the mirror image of Ramp Up/Saw
                return 1.0f - 2.0f * phase01;

            default:
                return 0.0f;
        }
    }

    void LFOModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix&)
    {
        // Deliberately does not touch the audio block's content at all -- see the class comment.
        const bool synced = ((int) rateModeParam->load()) == 1;

        double rateHz;
        if (synced)
        {
            const double quarterNoteSeconds = 60.0 / juce::jmax (1.0, services.hostBpm.load());
            const double divisionSeconds = quarterNoteSeconds * (double) getDelayDivisionMultiplier ((int) divisionParam->load());
            rateHz = 1.0 / juce::jmax (0.001, divisionSeconds);
        }
        else
        {
            rateHz = (double) rateHzParam->load();
        }

        // Block-rate tick: the block's total sample count stands in for elapsed time, so the
        // value is constant across this block and updates once per block rather than per sample.
        const double blockSeconds = (double) block.getNumSamples() / sampleRate;
        const double newPhase = phase + rateHz * blockSeconds;

        const bool wrapped = newPhase >= 1.0;
        phase = newPhase - std::floor (newPhase);

        if (wrapped && (int) shapeParam->load() == 4)
            sampleHoldValue = random.nextFloat() * 2.0f - 1.0f;

        const float depth = depthParam->load() / 100.0f;
        const float shapeValue = ((int) shapeParam->load() == 4) ? sampleHoldValue : computeShapeValue ((float) phase);

        currentValue.store (shapeValue * depth);
    }
}
