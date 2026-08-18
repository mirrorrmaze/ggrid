#include "LfoTableModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    LfoTableModule::LfoTableModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : services (servicesIn),
          tableParam     (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::tableIndex))),
          frameParam     (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::frame))),
          smoothParam    (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::smooth))),
          phaseParam     (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::phase))),
          rateModeParam  (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::rateMode))),
          rateHzParam    (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::rateHz))),
          divisionParam  (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::division))),
          depthParam     (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::depth))),
          retriggerParam (apvtsIn.getRawParameterValue (lfoTableParamId (slotIndexIn, LfoTableParam::retrigger)))
    {
    }

    void LfoTableModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        phase = 0.0;
        loadedTableIndex = -1;
        currentTable.reset();
        currentValue.store (0.0f);
    }

    void LfoTableModule::reset()
    {
        phase = 0.0;
        currentValue.store (0.0f);
    }

    std::shared_ptr<const WavetableLibrary::Table> LfoTableModule::getCurrentTable()
    {
        const int wanted = (int) tableParam->load();
        if (wanted != loadedTableIndex || currentTable == nullptr)
        {
            currentTable = WavetableLibrary::loadTable (wanted);
            loadedTableIndex = wanted;
        }

        return currentTable;
    }

    void LfoTableModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix&)
    {
        if (retriggerParam->load() >= 0.5f)
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                {
                    phase = 0.0;
                    break;
                }

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

        const double blockSeconds = (double) block.getNumSamples() / sampleRate;
        phase = phase + rateHz * blockSeconds;
        phase -= std::floor (phase);

        const auto table = getCurrentTable();
        const float maxFrame = table != nullptr ? (float) table->numFrames : 1.0f;
        const float frame = juce::jlimit (0.0f, maxFrame - 1.0f, frameParam->load() - 1.0f);
        const float phaseOffset = phaseParam->load() / 360.0f;
        const float smooth = smoothParam->load() / 100.0f;
        const float depth = depthParam->load() / 100.0f;

        const float tableValue = table != nullptr ? table->sample (frame, (float) phase + phaseOffset, smooth) : 0.0f;
        currentValue.store (juce::jlimit (-1.0f, 1.0f, tableValue * depth));
    }
}
