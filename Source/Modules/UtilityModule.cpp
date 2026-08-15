#include "UtilityModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    UtilityModule::UtilityModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : gainParam          (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::gain))),
          panParam           (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::pan))),
          widthParam         (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::width))),
          monoParam          (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::mono))),
          phaseInvertLParam  (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::phaseInvertL))),
          phaseInvertRParam  (apvtsIn.getRawParameterValue (utilityParamId (slotIndexIn, UtilityParam::phaseInvertR)))
    {
    }

    void UtilityModule::prepare (const juce::dsp::ProcessSpec&) {}
    void UtilityModule::reset() {}

    void UtilityModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix&)
    {
        const auto numChannels = block.getNumChannels();
        const auto numSamples = block.getNumSamples();

        const float signL = phaseInvertLParam->load() >= 0.5f ? -1.0f : 1.0f;
        const float signR = phaseInvertRParam->load() >= 0.5f ? -1.0f : 1.0f;
        const float gain = juce::Decibels::decibelsToGain (gainParam->load());

        if (numChannels < 2)
        {
            auto* data = block.getChannelPointer (0);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] *= signL * gain;
            return;
        }

        const bool monoOn = monoParam->load() >= 0.5f;
        const float widthAmt = widthParam->load() / 100.0f;
        const float panVal = panParam->load();

        // Balance law (input is already stereo, so this trims one side rather than panning a
        // mono source) -- simple linear taper, not constant-power, matching what a plain
        // "balance" control on a mixing desk does.
        const float panGainL = panVal <= 0.0f ? 1.0f : (1.0f - panVal);
        const float panGainR = panVal >= 0.0f ? 1.0f : (1.0f + panVal);

        auto* left = block.getChannelPointer (0);
        auto* right = block.getChannelPointer (1);

        for (size_t i = 0; i < numSamples; ++i)
        {
            float l = left[i] * signL;
            float r = right[i] * signR;

            // Width: mid/side, scaling only the side (difference) component. 0% collapses to
            // mono via the side term alone; 100% is unchanged; up to 200% exaggerates the image.
            const float mid = (l + r) * 0.5f;
            const float side = (l - r) * 0.5f * widthAmt;
            l = mid + side;
            r = mid - side;

            if (monoOn)
            {
                const float m = (l + r) * 0.5f;
                l = m;
                r = m;
            }

            left[i] = l * panGainL * gain;
            right[i] = r * panGainR * gain;
        }
    }
}
