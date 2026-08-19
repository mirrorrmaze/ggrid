#include "RackSlot.h"
#include "../Modules/WaveshaperModule.h"
#include "../Modules/FilterModule.h"
#include "../Modules/NonlinearFilterModule.h"
#include "../Modules/MackityModule.h"
#include "../Modules/ShimmerReverbModule.h"
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
#include "../Modules/MultibandConvolutionModule.h"
#include "../Modules/ThreeOscModule.h"
#include "../Modules/WavetableSynthModule.h"
#include "../Modules/AdsrModule.h"
#include "../Modules/EnvelopeModule.h"
#include "../Modules/MultipassModule.h"
#include "../Modules/LfoTableModule.h"

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

            case ModuleType::nonlinearFilter:
                return std::make_unique<NonlinearFilterModule> (apvts, slotIndex);

            case ModuleType::mackity:
                return std::make_unique<MackityModule> (apvts, slotIndex);

            case ModuleType::shimmerReverb:
                return std::make_unique<ShimmerReverbModule> (apvts, slotIndex);

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

            case ModuleType::multibandConvolution:
                return std::make_unique<MultibandConvolutionModule> (apvts, slotIndex, services);

            // ThreeOsc is a graph SOURCE role like Input (see ModuleType::threeOsc's own
            // comment), but unlike Input it IS a real DSP/MIDI processor -- process() generates
            // its audio from MIDI notes rather than being skipped for a raw-dry-buffer copy, so
            // it needs an actual module instance here, not the nullptr Input/Output get below.
            case ModuleType::threeOsc:
                return std::make_unique<ThreeOscModule> (apvts, slotIndex);

            case ModuleType::wavetableSynth:
                return std::make_unique<WavetableSynthModule> (apvts, slotIndex);

            // LFO/Envelope/ADSR are modulation SOURCE roles (see isModulationSourceType() in
            // Identifiers.h) -- excluded from the audio ConnectionGraph entirely, but (like
            // ThreeOsc) they ARE real DSP processors that need an actual instance here.
            case ModuleType::adsr:
                return std::make_unique<AdsrModule> (apvts, slotIndex);

            case ModuleType::envelope:
                return std::make_unique<EnvelopeModule> (apvts, slotIndex);

            case ModuleType::multipass:
                return std::make_unique<MultipassModule> (apvts, slotIndex);

            case ModuleType::lfoTable:
                return std::make_unique<LfoTableModule> (apvts, slotIndex, services);

            // Input/Output are structural graph roles, not DSP processors -- process() is never
            // called on a slot playing either role (see GGridAudioProcessor::processBlock's
            // isRoot/isSink handling), so there's no RackModule to construct for them.
            case ModuleType::input:
            case ModuleType::output:
            case ModuleType::none:
            default:
                return nullptr;
        }
    }

    void RackSlot::applyPendingExtraState()
    {
        auto& pending = services.pendingModuleExtraState[(size_t) slotIndex];
        if (currentModule != nullptr && pending != nullptr)
        {
            currentModule->readExtraState (*pending);
            pending.reset();
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

        applyPendingExtraState();
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

            applyPendingExtraState();
        }

        if (currentModule == nullptr)
            return;

        const bool bypassed = bypassParam->load() >= 0.5f;
        if (bypassed)
            return;

        currentModule->process (block, midi, modMatrix);
    }
}
