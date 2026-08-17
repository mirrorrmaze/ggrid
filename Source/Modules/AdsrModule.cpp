#include "AdsrModule.h"

namespace GGrid
{
    AdsrModule::AdsrModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : attackParam  (apvtsIn.getRawParameterValue (adsrParamId (slotIndexIn, AdsrParam::attack))),
          decayParam   (apvtsIn.getRawParameterValue (adsrParamId (slotIndexIn, AdsrParam::decay))),
          sustainParam (apvtsIn.getRawParameterValue (adsrParamId (slotIndexIn, AdsrParam::sustain))),
          releaseParam (apvtsIn.getRawParameterValue (adsrParamId (slotIndexIn, AdsrParam::release)))
    {
    }

    void AdsrModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        envelope.setSampleRate (sampleRate);
        envelope.reset();
        heldNoteCount = 0;
        currentValue.store (0.0f);
    }

    void AdsrModule::reset()
    {
        envelope.reset();
        heldNoteCount = 0;
        currentValue.store (0.0f);
    }

    void AdsrModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix&)
    {
        // Params are only re-read at the moment of a fresh noteOn(), not continuously every block
        // -- matches ThreeOscModule's own per-voice ADSR usage, the safe pattern per juce::ADSR's
        // documented contract (changing parameters on an already-active envelope without an
        // intervening reset() is undefined).
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                ++heldNoteCount;
                envelope.setParameters ({ attackParam->load(), decayParam->load(),
                                           sustainParam->load() / 100.0f, releaseParam->load() });
                envelope.noteOn();
            }
            else if (message.isNoteOff())
            {
                heldNoteCount = juce::jmax (0, heldNoteCount - 1);
                if (heldNoteCount == 0)
                    envelope.noteOff();
            }
            else if (message.isAllNotesOff() || message.isAllSoundOff())
            {
                heldNoteCount = 0;
                envelope.noteOff();
            }
        }

        // Block-rate, like every other modulation source (LFO/ThreeOsc's own per-voice envelope
        // read the same way) -- but the underlying juce::ADSR is still stepped through the WHOLE
        // block at full sample-rate granularity internally (getNextSample() called once per
        // sample), just reported as a single value for this block, not evaluated as if one block
        // were one sample of envelope time (that would make Attack/Decay/Release times ~samples-
        // per-block times slower than the knobs say).
        float lastValue = 0.0f;
        const int numSamples = (int) block.getNumSamples();
        for (int i = 0; i < numSamples; ++i)
            lastValue = envelope.getNextSample();

        currentValue.store (lastValue);
    }
}
