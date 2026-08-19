#include "ShimmerReverbModule.h"
#include "../Params/Identifiers.h"
#include <cmath>

namespace GGrid
{
    namespace
    {
        constexpr float baseTapMs[4] = { 43.0f, 61.0f, 89.0f, 127.0f };
        constexpr float baseDiffMs[2] = { 7.0f, 13.0f };

        float onePoleCoeff (float hz, double sampleRate)
        {
            return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * hz / (float) sampleRate));
        }
    }

    void ShimmerReverbModule::DelayTap::prepare (int maxSamples)
    {
        buffer.assign ((size_t) juce::jmax (4, maxSamples), 0.0f);
        reset();
    }

    void ShimmerReverbModule::DelayTap::reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        write = 0;
        last = 0.0f;
    }

    float ShimmerReverbModule::DelayTap::read (float delaySamples) const
    {
        if (buffer.empty())
            return 0.0f;

        const float size = (float) buffer.size();
        float readPos = (float) write - delaySamples;
        while (readPos < 0.0f)
            readPos += size;

        const int i0 = ((int) readPos) % (int) buffer.size();
        const int i1 = (i0 + 1) % (int) buffer.size();
        return buffer[(size_t) i0] + (buffer[(size_t) i1] - buffer[(size_t) i0]) * (readPos - std::floor (readPos));
    }

    void ShimmerReverbModule::DelayTap::push (float sample)
    {
        if (buffer.empty())
            return;

        buffer[(size_t) write] = sample;
        write = (write + 1) % (int) buffer.size();
        last = sample;
    }

    void ShimmerReverbModule::PitchShifter::prepare (int maxSamples)
    {
        buffer.assign ((size_t) juce::jmax (2048, maxSamples), 0.0f);
        reset();
    }

    void ShimmerReverbModule::PitchShifter::reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        write = 0;
        phase = 0.0f;
    }

    float ShimmerReverbModule::PitchShifter::process (float input, float semitones, bool reverse)
    {
        if (buffer.empty())
            return input;

        buffer[(size_t) write] = input;
        const float ratio = std::pow (2.0f, semitones / 12.0f);
        const float grain = juce::jlimit (512.0f, (float) buffer.size() * 0.45f, 4096.0f);
        const float baseDelay = 32.0f;
        const float travel = juce::jmax (256.0f, grain - baseDelay - 2.0f);

        auto readAt = [this] (float offset)
        {
            const float size = (float) buffer.size();
            float readPos = (float) write - offset;
            while (readPos < 0.0f)
                readPos += size;
            const int i0 = ((int) readPos) % (int) buffer.size();
            const int i1 = (i0 + 1) % (int) buffer.size();
            return buffer[(size_t) i0] + (buffer[(size_t) i1] - buffer[(size_t) i0]) * (readPos - std::floor (readPos));
        };

        const float p1 = phase;
        const float p2 = std::fmod (phase + 0.5f, 1.0f);
        const float window1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p1);
        const float window2 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p2);
        auto offsetForPhase = [=] (float p)
        {
            if (reverse)
                return baseDelay + p * travel;

            return ratio >= 1.0f ? baseDelay + (1.0f - p) * travel
                                 : baseDelay + p * travel;
        };

        const float offset1 = offsetForPhase (p1);
        const float offset2 = offsetForPhase (p2);
        const float shifted = (readAt (juce::jlimit (1.0f, grain, offset1 + 1.0f)) * window1
                            + readAt (juce::jlimit (1.0f, grain, offset2 + 1.0f)) * window2)
                            / juce::jmax (0.001f, window1 + window2);

        phase += juce::jlimit (0.0001f, 0.25f, std::abs (ratio - 1.0f) / travel);
        if (phase >= 1.0f)
            phase -= 1.0f;
        write = (write + 1) % (int) buffer.size();
        return shifted;
    }

    ShimmerReverbModule::ShimmerReverbModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          sizeParam      (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::size))),
          feedbackParam  (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::feedback))),
          diffusionParam (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::diffusion))),
          shiftParam     (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::shift))),
          pitchModeParam (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::pitchMode))),
          colorParam     (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::color))),
          modRateParam   (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::modRate))),
          modDepthParam  (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::modDepth))),
          lowCutParam    (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::lowCut))),
          highCutParam   (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::highCut))),
          mixParam       (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::mix))),
          outputParam    (apvtsIn.getRawParameterValue (shimmerReverbParamId (slotIndexIn, ShimmerReverbParam::output)))
    {
    }

    void ShimmerReverbModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        const int maxDelay = (int) std::ceil (sampleRate * 3.0);
        const int maxPitch = (int) std::ceil (sampleRate * 0.25);

        for (auto& channel : taps)
            for (auto& tap : channel)
                tap.prepare (maxDelay);
        for (auto& channel : diffusers)
            for (auto& diffuser : channel)
                diffuser.prepare ((int) std::ceil (sampleRate * 0.2));
        for (auto& p : pitchUp) p.prepare (maxPitch);
        for (auto& p : pitchDown) p.prepare (maxPitch);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        reset();
    }

    void ShimmerReverbModule::reset()
    {
        for (auto& channel : taps)
            for (auto& tap : channel)
                tap.reset();
        for (auto& channel : diffusers)
            for (auto& diffuser : channel)
                diffuser.reset();
        for (auto& p : pitchUp) p.reset();
        for (auto& p : pitchDown) p.reset();
        for (auto& t : tone)
            t = {};
        lfoPhase = 0.0f;
    }

    float ShimmerReverbModule::shapeTone (int ch, float sample, float lowCutHz, float highCutHz, int color)
    {
        auto& state = tone[(size_t) ch];
        const float hp = onePoleCoeff (lowCutHz, sampleRate);
        state.lowCut += hp * (sample - state.lowCut);
        sample -= state.lowCut;

        const float darkScale = color == 1 ? 0.55f : 1.0f;
        const float lp = onePoleCoeff (highCutHz * darkScale, sampleRate);
        state.highCut += lp * (sample - state.highCut);
        return state.highCut;
    }

    float ShimmerReverbModule::processChannel (int ch, float input, float size, float feedback, float diffusion,
                                               float shift, int pitchMode, int color, float modDepth,
                                               float lowCutHz, float highCutHz)
    {
        auto& channelTaps = taps[(size_t) ch];
        auto& channelDiff = diffusers[(size_t) ch];
        float wet = 0.0f;
        for (auto& tap : channelTaps)
            wet += tap.read (1.0f) * 0.25f;

        float shifted = wet;
        const bool reverse = pitchMode == 3 || pitchMode == 4;
        if (pitchMode == 1 || pitchMode == 3)
            shifted = pitchUp[(size_t) ch].process (wet, shift, reverse);
        else if (pitchMode == 2 || pitchMode == 4)
            shifted = 0.5f * (pitchUp[(size_t) ch].process (wet, shift, reverse)
                            + pitchDown[(size_t) ch].process (wet, -shift, reverse));

        float fed = shapeTone (ch, shifted, lowCutHz, highCutHz, color) * feedback;
        fed = std::tanh (fed);

        float diffuse = input + fed;
        const float diffGain = juce::jmap (diffusion, 0.0f, 1.0f, 0.15f, 0.72f);
        for (int i = 0; i < 2; ++i)
        {
            auto& d = channelDiff[(size_t) i];
            const float delayed = d.read (baseDiffMs[i] * 0.001f * (float) sampleRate * (0.75f + size * 1.5f));
            d.push (diffuse + delayed * diffGain);
            diffuse = delayed - diffuse * diffGain;
        }

        const float mod = std::sin (lfoPhase + (float) ch * 1.7f) * modDepth * 0.015f * (float) sampleRate;
        for (int i = 0; i < kTaps; ++i)
        {
            const float delaySamples = (baseTapMs[i] * (0.55f + size * 2.6f) * 0.001f * (float) sampleRate)
                                     + mod * (0.3f + 0.18f * (float) i);
            const float tapOut = channelTaps[(size_t) i].read (juce::jmax (4.0f, delaySamples));
            const float sign = (i & 1) == 0 ? 1.0f : -1.0f;
            channelTaps[(size_t) i].push (diffuse + tapOut * sign * feedback * 0.36f);
        }

        return wet;
    }

    void ShimmerReverbModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const float size = juce::jlimit (0.0f, 100.0f, sizeParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::size), 50.0f)) / 100.0f;
        const float feedback = juce::jlimit (0.0f, 95.0f, feedbackParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::feedback), 50.0f)) / 100.0f;
        const float diffusion = juce::jlimit (0.0f, 100.0f, diffusionParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::diffusion), 50.0f)) / 100.0f;
        const float shift = juce::jlimit (-24.0f, 24.0f, shiftParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::shift), 12.0f));
        const int pitchMode = juce::jlimit (0, getShimmerReverbPitchModeChoices().size() - 1, (int) pitchModeParam->load());
        const int color = juce::jlimit (0, getShimmerReverbColorChoices().size() - 1, (int) colorParam->load());
        const float modRate = juce::jlimit (0.01f, 8.0f, modRateParam->load());
        const float modDepth = juce::jlimit (0.0f, 100.0f, modDepthParam->load()) / 100.0f;
        const float lowCut = juce::jlimit (20.0f, 2000.0f, lowCutParam->load());
        const float highCut = juce::jlimit (lowCut + 100.0f, (float) sampleRate * 0.45f, highCutParam->load());
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::mix), 50.0f)) / 100.0f;
        const float output = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load()
            + modMatrix.getOffsetForParam (shimmerReverbParamId (slotIndex, ShimmerReverbParam::output), 12.0f)));

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kChannels);
        const auto numSamples = block.getNumSamples();
        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        for (size_t i = 0; i < numSamples; ++i)
        {
            lfoPhase += juce::MathConstants<float>::twoPi * modRate / (float) sampleRate;
            if (lfoPhase >= juce::MathConstants<float>::twoPi)
                lfoPhase -= juce::MathConstants<float>::twoPi;

            const float inL = block.getChannelPointer (0)[i];
            const float inR = numChannels > 1 ? block.getChannelPointer (1)[i] : inL;
            const float wetL = processChannel (0, inL + inR * 0.15f, size, feedback, diffusion, shift, pitchMode, color, modDepth, lowCut, highCut);
            const float wetR = numChannels > 1 ? processChannel (1, inR + inL * 0.15f, size, feedback, diffusion, shift, pitchMode, color, modDepth, lowCut, highCut)
                                               : wetL;

            block.getChannelPointer (0)[i] = wetL;
            if (numChannels > 1)
                block.getChannelPointer (1)[i] = wetR;
        }

        const float dryGain = std::cos (mix * juce::MathConstants<float>::halfPi);
        const float wetGain = std::sin (mix * juce::MathConstants<float>::halfPi) * output;
        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = dry[i] * dryGain + data[i] * wetGain;
        }
    }
}
