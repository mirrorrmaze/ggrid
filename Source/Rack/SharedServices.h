#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../IR/IRReshapeWorker.h"

namespace GGrid
{
    // Resources shared across every rack slot/module in a plugin instance -- owned by
    // PluginProcessor, handed down through RackSlot to whichever module needs them: Convolution
    // uses convolutionQueue/irReshapeWorker for its background IR reshape thread and the
    // JUCE-internal convolution engine's shared message queue; Delay uses hostBpm for tempo sync.
    // hostBpm is updated from the host's playhead once per block, before the rack chain runs.
    struct SharedServices
    {
        juce::dsp::ConvolutionMessageQueue& convolutionQueue;
        IRReshapeWorker& irReshapeWorker;
        std::atomic<double>& hostBpm;
    };
}
