#include "GranularModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    GranularModule::GranularModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          sizeParam     (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::size))),
          densityParam  (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::density))),
          positionParam (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::position))),
          jitterParam   (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::jitter))),
          pitchParam    (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::pitch))),
          spreadParam   (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::spread))),
          feedbackParam (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::feedback))),
          freezeParam   (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::freeze))),
          mixParam      (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::mix))),
          outputParam   (apvtsIn.getRawParameterValue (granularParamId (slotIndexIn, GranularParam::output)))
    {
        random.setSeedRandomly();
    }

    void GranularModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        bufferLength = juce::jmax (64, (int) std::ceil (currentSampleRate * kBufferSeconds));
        delayBuffer.setSize ((int) juce::jmin (spec.numChannels, (juce::uint32) kMaxChannels), bufferLength, false, false, true);
        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        reset();
    }

    void GranularModule::reset()
    {
        delayBuffer.clear();
        writePos = 0;
        spawnAccumulator = 0.0f;
        for (auto& grain : grains)
            grain = {};
    }

    float GranularModule::grainEnvelope (float phase01)
    {
        phase01 = juce::jlimit (0.0f, 1.0f, phase01);
        return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase01);
    }

    float GranularModule::readDelayBuffer (int channel, float readPos) const
    {
        if (bufferLength <= 1 || delayBuffer.getNumChannels() == 0)
            return 0.0f;

        const int ch = juce::jlimit (0, delayBuffer.getNumChannels() - 1, channel);
        while (readPos < 0.0f)
            readPos += (float) bufferLength;
        while (readPos >= (float) bufferLength)
            readPos -= (float) bufferLength;

        const int i0 = (int) readPos;
        const int i1 = (i0 + 1) % bufferLength;
        const float frac = readPos - (float) i0;
        const auto* data = delayBuffer.getReadPointer (ch);
        return data[i0] + (data[i1] - data[i0]) * frac;
    }

    void GranularModule::spawnGrain (int lengthSamples, float pitchRatio, float position01, float jitter01, float spread01)
    {
        auto* freeGrain = std::find_if (grains.begin(), grains.end(), [] (const Grain& grain) { return ! grain.active; });
        if (freeGrain == grains.end())
            return;

        const float maxDelaySamples = (float) juce::jmax (1, bufferLength - lengthSamples - 2);
        float delaySamples = position01 * maxDelaySamples;
        const float jitterSamples = jitter01 * maxDelaySamples * 0.35f;
        delaySamples += (random.nextFloat() * 2.0f - 1.0f) * jitterSamples;
        delaySamples = juce::jlimit ((float) lengthSamples, maxDelaySamples, delaySamples);

        const float pan = (random.nextFloat() * 2.0f - 1.0f) * spread01;
        const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;

        freeGrain->active = true;
        freeGrain->readPos = (float) writePos - delaySamples;
        freeGrain->step = pitchRatio;
        freeGrain->age = 0;
        freeGrain->length = juce::jmax (1, lengthSamples);
        freeGrain->gainL = std::cos (angle);
        freeGrain->gainR = std::sin (angle);
    }

    void GranularModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxChannels);
        const auto numSamples = block.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        const float sizeMs = juce::jlimit (5.0f, 500.0f,
            sizeParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::size), 180.0f));
        const int grainLength = juce::jlimit (8, bufferLength / 2, (int) (sizeMs * 0.001f * (float) currentSampleRate));

        const float density = juce::jlimit (1.0f, 120.0f,
            densityParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::density), 40.0f));
        const float position01 = juce::jlimit (0.0f, 100.0f,
            positionParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::position), 50.0f)) / 100.0f;
        const float jitter01 = juce::jlimit (0.0f, 100.0f,
            jitterParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::jitter), 50.0f)) / 100.0f;
        const float pitchSemis = juce::jlimit (-24.0f, 24.0f,
            pitchParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::pitch), 12.0f));
        const float pitchRatio = std::pow (2.0f, pitchSemis / 12.0f);
        const float spread01 = juce::jlimit (0.0f, 100.0f,
            spreadParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::spread), 50.0f)) / 100.0f;
        const float feedback = juce::jlimit (0.0f, 0.95f,
            (feedbackParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::feedback), 40.0f)) / 100.0f);
        const bool freeze = freezeParam->load() >= 0.5f;
        const float mix = juce::jlimit (0.0f, 100.0f,
            mixParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::mix), 50.0f)) / 100.0f;
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f,
            outputParam->load() + modMatrix.getOffsetForParam (granularParamId (slotIndex, GranularParam::output), 12.0f)));

        const float grainsPerSample = density / (float) currentSampleRate;

        for (size_t i = 0; i < numSamples; ++i)
        {
            spawnAccumulator += grainsPerSample;
            while (spawnAccumulator >= 1.0f)
            {
                spawnGrain (grainLength, pitchRatio, position01, jitter01, spread01);
                spawnAccumulator -= 1.0f;
            }

            float wet[kMaxChannels] = {};
            for (auto& grain : grains)
            {
                if (! grain.active)
                    continue;

                const float env = grainEnvelope ((float) grain.age / (float) grain.length);
                for (size_t ch = 0; ch < numChannels; ++ch)
                {
                    const float panGain = numChannels == 1 ? 1.0f : (ch == 0 ? grain.gainL : grain.gainR);
                    wet[ch] += readDelayBuffer ((int) ch, grain.readPos) * env * panGain;
                }

                grain.readPos += grain.step;
                if (++grain.age >= grain.length)
                    grain.active = false;
            }

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                const float dry = dryBuffer.getSample ((int) ch, (int) i);
                const float wetSample = std::tanh (wet[ch]);

                if (! freeze)
                    delayBuffer.setSample ((int) ch, writePos, dry + wetSample * feedback);

                data[i] = (dry * (1.0f - mix) + wetSample * mix) * outputGain;
            }

            if (! freeze)
                writePos = (writePos + 1) % bufferLength;
        }
    }
}
