#include "FilterModule.h"
#include "../Params/Identifiers.h"

namespace GGrid
{
    namespace
    {
        // Filter Type indices, matching getFilterTypeChoices() order.
        constexpr int typeLowPass = 0;
        constexpr int typeHighPass = 1;
        constexpr int typeBandPass = 2;
        constexpr int typeNotch = 3;
        constexpr int typeCombFeedback = 4;
        constexpr int typeCombFeedforward = 5;
        constexpr int typeAllpassDiffusor = 6;
        constexpr int typeLadderLowPass = 7;
        constexpr int typeLadderHighPass = 8;
        constexpr int typeFormant = 9;

        bool isBiquadType (int type) { return type <= typeNotch; }
        bool isDelayBasedType (int type) { return type >= typeCombFeedback && type <= typeAllpassDiffusor; }
        bool isLadderType (int type) { return type == typeLadderLowPass || type == typeLadderHighPass; }

        // First three formants (F1/F2/F3, Hz) for five vowels, a standard rough table used in
        // vowel/formant-filter sound design (not aiming for linguistic precision) -- swept
        // through in this order (A->E->I->O->U) as the Formant type's vowel-position value goes
        // 0..1, see updateFormantCoefficients.
        constexpr int kNumVowels = 5;
        constexpr float kVowelFormants[kNumVowels][3] = {
            { 800.0f, 1150.0f, 2900.0f }, // A
            { 400.0f, 1600.0f, 2700.0f }, // E
            { 250.0f, 1750.0f, 2600.0f }, // I
            { 400.0f,  750.0f, 2600.0f }, // O
            { 325.0f,  700.0f, 2400.0f }, // U
        };
    }

    FilterModule::FilterModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          frequencyParam (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::frequency))),
          typeParam      (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::type))),
          resonanceParam (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::resonance))),
          feedbackParam  (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::feedback))),
          mixParam       (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::mix))),
          outputParam    (apvtsIn.getRawParameterValue (filterParamId (slotIndexIn, FilterParam::output)))
    {
    }

    void FilterModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;

        juce::dsp::ProcessSpec monoSpec = spec;
        monoSpec.numChannels = 1;

        // Supports pitches down to 1 Hz -- comfortably below our 20 Hz parameter floor -- at
        // whatever sample rate the host is running.
        const int maxDelaySamples = juce::jmax (64, (int) std::ceil (spec.sampleRate));

        for (auto& line : delayLine)
        {
            line.setMaximumDelayInSamples (maxDelaySamples);
            line.prepare (monoSpec);
        }

        for (auto& b : biquad)
            b.prepare (monoSpec);

        for (auto& channelFormants : formantBiquad)
            for (auto& f : channelFormants)
                f.prepare (monoSpec);

        for (auto& stage : ladderStage)
            stage.fill (0.0f);

        dryBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        updateBiquadCoefficients (frequencyParam->load(), resonanceParam->load());
        updateFormantCoefficients ((frequencyParam->load() - 20.0f) / 7980.0f, resonanceParam->load());
    }

    void FilterModule::reset()
    {
        for (auto& line : delayLine)
            line.reset();

        for (auto& b : biquad)
            b.reset();

        for (auto& channelFormants : formantBiquad)
            for (auto& f : channelFormants)
                f.reset();

        for (auto& stage : ladderStage)
            stage.fill (0.0f);
    }

    void FilterModule::updateBiquadCoefficients (float frequency, float resonance)
    {
        const int type = (int) typeParam->load();
        if (! isBiquadType (type))
            return;

        const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), frequency);
        const float q = juce::jlimit (0.1f, 20.0f, resonance);

        juce::dsp::IIR::Coefficients<float>::Ptr coeffs;
        switch (type)
        {
            case typeLowPass:  coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass  (currentSampleRate, freq, q); break;
            case typeHighPass: coeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass (currentSampleRate, freq, q); break;
            case typeBandPass: coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, freq, q); break;
            case typeNotch:    coeffs = juce::dsp::IIR::Coefficients<float>::makeNotch    (currentSampleRate, freq, q); break;
            default: return;
        }

        for (auto& b : biquad)
            *b.coefficients = *coeffs;
    }

    void FilterModule::updateFormantCoefficients (float vowelPosition, float resonance)
    {
        const float q = juce::jlimit (0.1f, 20.0f, resonance);
        const float pos = juce::jlimit (0.0f, 1.0f, vowelPosition) * (float) (kNumVowels - 1);
        const int vowelA = juce::jlimit (0, kNumVowels - 1, (int) pos);
        const int vowelB = juce::jmin (kNumVowels - 1, vowelA + 1);
        const float t = pos - (float) vowelA;

        for (int f = 0; f < kNumFormants; ++f)
        {
            const float freqA = kVowelFormants[vowelA][f];
            const float freqB = kVowelFormants[vowelB][f];
            const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), freqA + t * (freqB - freqA));

            auto coeffs = juce::dsp::IIR::Coefficients<float>::makeBandPass (currentSampleRate, freq, q);
            for (auto& channelFormants : formantBiquad)
                *channelFormants[(size_t) f].coefficients = *coeffs;
        }
    }

    // 4-stage nonlinear-feedback ladder, the standard "cheap Moog ladder" recipe: each stage is
    // a one-pole lowpass with a tanh() saturator ahead of it, and the last stage's output feeds
    // back (scaled by resonanceAmount) into the first stage's input -- the saturation is what
    // makes resonance sound like it's driving the filter into distortion rather than just
    // ringing louder, and bounds the whole structure so it can't blow up numerically even as
    // resonanceAmount approaches self-oscillation. High Pass reuses the exact same lowpass core
    // and just taps (input - lowpass output) instead -- a standard way to get a complementary
    // response out of one ladder rather than needing a second, differently-tuned one.
    float FilterModule::processLadderSample (int channel, float x, bool highPass, float cutoffCoefficient, float resonanceAmount)
    {
        auto& stage = ladderStage[(size_t) channel];
        const float g = cutoffCoefficient;
        const float k = resonanceAmount;

        const float fed = std::tanh (x - k * stage[3]);
        stage[0] += g * (fed - stage[0]);
        stage[1] += g * (std::tanh (stage[0]) - stage[1]);
        stage[2] += g * (std::tanh (stage[1]) - stage[2]);
        stage[3] += g * (std::tanh (stage[2]) - stage[3]);

        return highPass ? (x - stage[3]) : stage[3];
    }

    float FilterModule::processDelayBasedSample (int channel, float x, int type, float feedback)
    {
        auto& line = delayLine[(size_t) channel];

        if (type == typeCombFeedback)
        {
            // v[n] = x[n] + fb * v[n-D]; output is the delayed tap v[n-D], which already carries
            // the accumulated echo history. Dry/wet blending happens later via the Mix param.
            const float delayed = line.popSample (0);
            line.pushSample (0, x + feedback * delayed);
            return delayed;
        }

        if (type == typeCombFeedforward)
        {
            // Pure FIR comb: the delay line only ever stores raw input, so this is unconditionally
            // stable even at the parameter's extremes.
            const float delayed = line.popSample (0);
            line.pushSample (0, x);
            return x + feedback * delayed;
        }

        // typeAllpassDiffusor -- classic Schroeder allpass:
        //   w[n] = x[n] + g*w[n-D]
        //   y[n] = -g*x[n] + w[n-D]
        const float wDelayed = line.popSample (0);
        const float w = x + feedback * wDelayed;
        line.pushSample (0, w);
        return (-feedback * x) + wDelayed;
    }

    void FilterModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix& modMatrix)
    {
        const int type = (int) typeParam->load();

        const float mixOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::mix), 50.0f);
        const float mix = juce::jlimit (0.0f, 100.0f, mixParam->load() + mixOffset) / 100.0f;

        const float outputOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::output), 12.0f);
        const float outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        const float freqOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::filterFrequency))
                                + modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::frequency), 3000.0f);
        const float feedbackOffset = modMatrix.getOffsetForDestination (modDestinationIndex (slotIndex, ModDestinationParam::filterFeedback))
                                    + modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::feedback), 0.9f);
        const float resonanceOffset = modMatrix.getOffsetForParam (filterParamId (slotIndex, FilterParam::resonance), 3.0f);

        const auto numChannels = juce::jmin (block.getNumChannels(), (size_t) kMaxFilterChannels);
        const auto numSamples = block.getNumSamples();

        for (size_t ch = 0; ch < numChannels; ++ch)
            dryBuffer.copyFrom ((int) ch, 0, block.getChannelPointer (ch), (int) numSamples);

        if (isBiquadType (type))
        {
            const float resonance = juce::jlimit (0.1f, 20.0f, resonanceParam->load() + resonanceOffset);
            updateBiquadCoefficients (frequencyParam->load() + freqOffset, resonance);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                for (size_t i = 0; i < numSamples; ++i)
                    data[i] = biquad[ch].processSample (data[i]);
            }
        }
        else if (isDelayBasedType (type))
        {
            const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), frequencyParam->load() + freqOffset);
            const float delaySamples = juce::jlimit (1.0f, (float) (currentSampleRate - 1.0), (float) (currentSampleRate / freq));
            const float feedback = juce::jlimit (-0.95f, 0.95f, feedbackParam->load() + feedbackOffset);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                delayLine[ch].setDelay (delaySamples);
                auto* data = block.getChannelPointer (ch);

                for (size_t i = 0; i < numSamples; ++i)
                    data[i] = processDelayBasedSample ((int) ch, data[i], type, feedback);
            }
        }
        else if (isLadderType (type))
        {
            const float resonance = juce::jlimit (0.1f, 20.0f, resonanceParam->load() + resonanceOffset);
            const float freq = juce::jlimit (20.0f, (float) (currentSampleRate * 0.45), frequencyParam->load() + freqOffset);

            // One-pole coefficient per stage from cutoff frequency -- the standard stable digital
            // one-pole approximation, accurate enough across this filter's whole useful range.
            const float g = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * freq / (float) currentSampleRate);
            // Resonance's 0.1-20 range remapped to a 0..~4.2 feedback amount -- above ~4 a 4-stage
            // ladder starts self-oscillating, matching real ladder filter behaviour.
            const float k = juce::jmap (resonance, 0.1f, 20.0f, 0.0f, 4.2f);
            const bool highPass = (type == typeLadderHighPass);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                for (size_t i = 0; i < numSamples; ++i)
                    data[i] = processLadderSample ((int) ch, data[i], highPass, g, k);
            }
        }
        else // typeFormant
        {
            const float resonance = juce::jlimit (0.1f, 20.0f, resonanceParam->load() + resonanceOffset);
            const float vowelPosition = (juce::jlimit (20.0f, 8000.0f, frequencyParam->load() + freqOffset) - 20.0f) / 7980.0f;
            updateFormantCoefficients (vowelPosition, resonance);

            for (size_t ch = 0; ch < numChannels; ++ch)
            {
                auto* data = block.getChannelPointer (ch);
                for (size_t i = 0; i < numSamples; ++i)
                {
                    // Three formant peaks run in parallel on the same input sample (not chained),
                    // then averaged -- each still reads the original data[i] here since it isn't
                    // overwritten until after all three have processed it.
                    float sum = 0.0f;
                    for (int f = 0; f < kNumFormants; ++f)
                        sum += formantBiquad[ch][f].processSample (data[i]);
                    data[i] = sum / (float) kNumFormants;
                }
            }
        }

        for (size_t ch = 0; ch < numChannels; ++ch)
        {
            auto* data = block.getChannelPointer (ch);
            const auto* dry = dryBuffer.getReadPointer ((int) ch);
            for (size_t i = 0; i < numSamples; ++i)
                data[i] = data[i] * outputGain * mix + dry[i] * (1.0f - mix);
        }
    }
}
