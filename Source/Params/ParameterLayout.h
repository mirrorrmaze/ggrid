#pragma once

#include "../Modulation/ModulationMatrix.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace GGrid
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
