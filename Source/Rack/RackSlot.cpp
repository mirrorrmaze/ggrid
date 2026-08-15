#include "RackSlot.h"
#include "../Modules/WaveshaperModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/DelayModule.h"
#include "../Modules/DynamicsModule.h"
#include "../Modules/ConvolutionModule.h"
#include "../Modules/UtilityModule.h"
#include "../Modules/RingModModule.h"
#include "../Modules/LFOModule.h"
#include "../Modules/LossyModule.h"
#include "../Modules/Eq8Module.h"
#include "../Modules/ChorusModule.h"
#include "../Modules/Eq3Module.h"

namespace GGrid
{
    RackSlot::RackSlot (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : apvts (apvtsIn), services (servicesIn), slotIndex (slotIndexIn),
          typeParam (apvtsIn.getRawParameterValue (slotTypeParamId (slotIndexIn))),
          bypassParam (apvtsIn.getRawParameterValue (slotBypassParamId (slotIndexIn)))
    {
    }

    std::unique_ptr<RackModule> RackSlot::createModuleForType (ModuleType type) const
    {
        switch (type)
        {
            case ModuleType::waveshaper:
                return std::make_unique<WaveshaperModule> (apvts, slotIndex);

            case ModuleType::filter:
                return std::make_unique<FilterModule> (apvts, slotIndex);

            case ModuleType::delay:
                return std::make_unique<DelayModule> (apvts, slotIndex, services);

            case ModuleType::dynamics:
                return std::make_unique<DynamicsModule> (apvts, slotIndex);

            case ModuleType::convolution:
                return std::make_unique<ConvolutionModule> (apvts, slotIndex, services);

            case ModuleType::utility:
                return std::make_unique<UtilityModule> (apvts, slotIndex);

            case ModuleType::ringMod:
                return std::make_unique<RingModModule> (apvts, slotIndex);

            case ModuleType::lfo:
                return std::make_unique<LFOModule> (apvts, slotIndex, services);

            case ModuleType::lossy:
                return std::make_unique<LossyModule> (apvts, slotIndex);

            case ModuleType::eq8:
                return std::make_unique<Eq8Module> (apvts, slotIndex);

            case ModuleType::chorus:
                return std::make_unique<ChorusModule> (apvts, slotIndex);

            case ModuleType::eq3:
                return std::make_unique<Eq3Module> (apvts, slotIndex);

            case ModuleType::none:
            default:
                return nullptr;
        }
    }

    void RackSlot::prepare (const juce::dsp::ProcessSpec& spec)
    {
        lastSpec = spec;
        hasSpec = true;

        currentType = static_cast<ModuleType> ((int) typeParam->load());
        currentModule = createModuleForType (currentType);

        if (currentModule != nullptr)
            currentModule->prepare (lastSpec);
    }

    void RackSlot::reset()
    {
        if (currentModule != nullptr)
            currentModule->reset();
    }

    void RackSlot::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix)
    {
        const auto wantedType = static_cast<ModuleType> ((int) typeParam->load());

        if (wantedType != currentType)
        {
            currentType = wantedType;
            currentModule = createModuleForType (currentType);

            if (currentModule != nullptr && hasSpec)
                currentModule->prepare (lastSpec);
        }

        if (currentModule == nullptr)
            return;

        const bool bypassed = bypassParam->load() >= 0.5f;
        if (bypassed)
            return;

        currentModule->process (block, midi, modMatrix);
    }
}
