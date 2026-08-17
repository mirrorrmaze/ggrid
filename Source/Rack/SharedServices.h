#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../IR/IRReshapeWorker.h"
#include "../Params/Identifiers.h"
#include <array>
#include <memory>

namespace GGrid
{
    // Resources shared across every rack slot/module in a plugin instance -- owned by
    // PluginProcessor, handed down through RackSlot to whichever module needs them: Convolution
    // uses convolutionQueue/irReshapeWorker for its background IR reshape thread and the
    // JUCE-internal convolution engine's shared message queue; Delay uses hostBpm for tempo sync.
    // hostBpm is updated from the host's playhead once per block, before the rack chain runs.
    //
    // pendingModuleExtraState holds, per slot, a just-loaded save's RackModule::writeExtraState
    // output waiting to be re-applied to a freshly (re)created module instance -- see
    // RackSlot::prepare()/process(), which check-and-consume their own slot's entry immediately
    // after createModuleForType() succeeds. This exists because setStateInformation can run
    // before OR after the module instances matching the just-loaded type parameters actually
    // exist (host-dependent call order), so the state can't just be applied synchronously inside
    // setStateInformation -- it has to wait, safely, for whichever moment a matching module is
    // actually constructed.
    struct SharedServices
    {
        juce::dsp::ConvolutionMessageQueue& convolutionQueue;
        IRReshapeWorker& irReshapeWorker;
        std::atomic<double>& hostBpm;
        std::array<std::unique_ptr<juce::XmlElement>, kMaxSlots>& pendingModuleExtraState;
    };
}
