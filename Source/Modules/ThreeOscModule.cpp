#include "ThreeOscModule.h"
#include <cmath>

namespace GGrid
{
    namespace
    {
        // Standard 2-sample-wide polynomial BLEP correction, subtracted/added at a naive
        // waveform's discontinuities to band-limit it -- see oscillatorSample's Saw/Square cases.
        // dt is clamped away from 0 and 0.5 so a silent/DC or absurdly-high-pitched voice (coarse
        // tuning can push well past Nyquist) can't produce a degenerate correction width.
        float polyBlep (double t, double dt)
        {
            dt = juce::jlimit (1.0e-9, 0.49, dt);

            if (t < dt)
            {
                t /= dt;
                return (float) (t + t - t * t - 1.0);
            }
            if (t > 1.0 - dt)
            {
                t = (t - 1.0) / dt;
                return (float) (t * t + t + t + 1.0);
            }
            return 0.0f;
        }
    }

    ThreeOscModule::ThreeOscModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          attackParam  (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::attack))),
          decayParam   (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::decay))),
          sustainParam (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::sustain))),
          releaseParam (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::release))),
          fm1to2Param  (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::fm1to2))),
          fm2to3Param  (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::fm2to3))),
          outputParam  (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::output))),
          monoLegatoParam  (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::monoLegato))),
          glideParam       (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::glide))),
          glideTimeMsParam (apvtsIn.getRawParameterValue (threeOscParamId (slotIndexIn, ThreeOscParam::glideTimeMs)))
    {
        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            waveformParams[(size_t) osc] = apvtsIn.getRawParameterValue (threeOscOscParamId (slotIndexIn, osc, ThreeOscOscParam::waveform));
            coarseParams[(size_t) osc]   = apvtsIn.getRawParameterValue (threeOscOscParamId (slotIndexIn, osc, ThreeOscOscParam::coarse));
            fineParams[(size_t) osc]     = apvtsIn.getRawParameterValue (threeOscOscParamId (slotIndexIn, osc, ThreeOscOscParam::fine));
            panParams[(size_t) osc]      = apvtsIn.getRawParameterValue (threeOscOscParamId (slotIndexIn, osc, ThreeOscOscParam::pan));
            levelParams[(size_t) osc]    = apvtsIn.getRawParameterValue (threeOscOscParamId (slotIndexIn, osc, ThreeOscOscParam::level));
        }
    }

    void ThreeOscModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& voice : voices)
        {
            voice.active = false;
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.reset();
            for (auto& p : voice.phase)
                p = 0.0;
        }

        heldNoteStackSize = 0;
        // A short initial ramp length just to satisfy juce::SmoothedValue's "must call reset()
        // before use" contract -- the real Glide Time is set fresh at each legato retarget (see
        // handleMonoNoteOn/handleMonoNoteOff), so this value never actually matters in practice.
        monoNoteNumberSmoothed.reset (sampleRate, 0.001);
        monoNoteNumberSmoothed.setCurrentAndTargetValue (60.0f);
    }

    void ThreeOscModule::reset()
    {
        for (auto& voice : voices)
        {
            voice.active = false;
            voice.envelope.reset();
        }
        heldNoteStackSize = 0;
    }

    float ThreeOscModule::oscillatorSample (int waveform, double phase, double phaseIncrement, double modOffset)
    {
        double p = phase + modOffset;
        p -= std::floor (p);

        switch (waveform)
        {
            case 0: // Sine
                return (float) std::sin (2.0 * juce::MathConstants<double>::pi * p);

            case 1: // Triangle -- naive (mild aliasing at extreme high pitch, acceptable --
                    // matches LFOModule::computeShapeValue's own triangle formula)
                return p < 0.5 ? (float) (4.0 * p - 1.0) : (float) (3.0 - 4.0 * p);

            case 2: // Saw
                return (float) (2.0 * p - 1.0) - polyBlep (p, phaseIncrement);

            case 3: // Square (50% duty)
            {
                const float naive = p < 0.5 ? 1.0f : -1.0f;
                double p2 = p + 0.5;
                p2 -= std::floor (p2);
                return naive + polyBlep (p, phaseIncrement) - polyBlep (p2, phaseIncrement);
            }

            default:
                return 0.0f;
        }
    }

    ThreeOscModule::ResolvedParams ThreeOscModule::resolveParams (const ModulationMatrix& modMatrix) const
    {
        ResolvedParams r;

        r.attack = juce::jmax (0.001f, attackParam->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::attack), 1.0f));
        r.decay = juce::jmax (0.001f, decayParam->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::decay), 1.0f));
        r.sustain = juce::jlimit (0.0f, 100.0f, sustainParam->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::sustain), 50.0f));
        r.release = juce::jmax (0.001f, releaseParam->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::release), 1.0f));

        r.fm1to2 = juce::jlimit (0.0f, 100.0f, fm1to2Param->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::fm1to2), 50.0f)) / 100.0f;
        r.fm2to3 = juce::jlimit (0.0f, 100.0f, fm2to3Param->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::fm2to3), 50.0f)) / 100.0f;

        r.outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load()
            + modMatrix.getOffsetForParam (threeOscParamId (slotIndex, ThreeOscParam::output), 12.0f)));

        r.monoLegato = monoLegatoParam->load() >= 0.5f;
        r.glide = glideParam->load() >= 0.5f;
        r.glideTimeMs = juce::jmax (1.0f, glideTimeMsParam->load());

        for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
        {
            r.waveform[(size_t) osc] = (int) waveformParams[(size_t) osc]->load();

            const float coarse = juce::jlimit (-36.0f, 36.0f, coarseParams[(size_t) osc]->load()
                + modMatrix.getOffsetForParam (threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::coarse), 12.0f));
            const float fine = juce::jlimit (-100.0f, 100.0f, fineParams[(size_t) osc]->load()
                + modMatrix.getOffsetForParam (threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::fine), 50.0f));
            r.freqMultiplier[(size_t) osc] = std::pow (2.0f, (coarse + fine / 100.0f) / 12.0f);

            r.pan[(size_t) osc] = juce::jlimit (-1.0f, 1.0f, panParams[(size_t) osc]->load()
                + modMatrix.getOffsetForParam (threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::pan), 1.0f));
            r.level[(size_t) osc] = juce::jlimit (0.0f, 100.0f, levelParams[(size_t) osc]->load()
                + modMatrix.getOffsetForParam (threeOscOscParamId (slotIndex, osc, ThreeOscOscParam::level), 50.0f));
        }

        return r;
    }

    int ThreeOscModule::findVoiceForNoteOn()
    {
        // Index 0 is reserved exclusively for Mono/Legato (handleMonoNoteOn/Off) -- if poly could
        // also land a note on it, a note-off arriving after a mode switch couldn't tell which path
        // owns voices[0], and note-off routing (see handleMidiEvent below) needs that to be
        // unambiguous to avoid ever orphaning a still-sounding voice.
        for (int i = 1; i < kMaxThreeOscVoices; ++i)
            if (! voices[(size_t) i].active)
                return i;

        // All voices busy -- steal the oldest-triggered one (lowest triggerOrder), regardless of
        // whether it's still held or already releasing.
        int oldest = 1;
        for (int i = 2; i < kMaxThreeOscVoices; ++i)
            if (voices[(size_t) i].triggerOrder < voices[(size_t) oldest].triggerOrder)
                oldest = i;
        return oldest;
    }

    void ThreeOscModule::handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved)
    {
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();

            if (resolved.monoLegato)
            {
                handleMonoNoteOn (note, message.getFloatVelocity(), resolved);
                return;
            }

            auto& voice = voices[(size_t) findVoiceForNoteOn()];

            // Hard-reset rather than reusing whatever state a stolen voice was in -- juce::ADSR
            // requires reset() before changing parameters on a still-active envelope, and a
            // stolen voice is by definition being cut short, not given a graceful tail.
            voice.envelope.reset();
            voice.active = true;
            voice.noteNumber = note;
            voice.velocity = message.getFloatVelocity();
            voice.triggerOrder = nextTriggerOrder++;
            for (auto& p : voice.phase)
                p = 0.0;

            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.setParameters ({ resolved.attack, resolved.decay, resolved.sustain / 100.0f, resolved.release });
            voice.envelope.noteOn();
        }
        else if (message.isNoteOff())
        {
            const int note = message.getNoteNumber();

            // Deliberately NOT gated behind resolved.monoLegato -- if Mono/Legato was toggled
            // while this note was held, its note-on could have gone through either path (poly's
            // findVoiceForNoteOn never touches index 0, so there's no ambiguity), and only
            // checking whichever path is active *now* would leave the other one's voice with no
            // note-off ever coming, sounding forever. Always check both; each is a no-op if this
            // note isn't the one it's tracking.
            for (int i = 1; i < kMaxThreeOscVoices; ++i)
                if (voices[(size_t) i].active && voices[(size_t) i].noteNumber == note)
                    voices[(size_t) i].envelope.noteOff();

            if (heldNoteStackSize > 0 || (voices[0].active && voices[0].noteNumber == note))
                handleMonoNoteOff (note, resolved);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& voice : voices)
                if (voice.active)
                    voice.envelope.noteOff();
            heldNoteStackSize = 0;
        }
    }

    void ThreeOscModule::handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved)
    {
        const bool wasEmpty = (heldNoteStackSize == 0);

        // Remove any existing occurrence of this note (defensive against duplicate note-ons) then
        // push it as the new top of the stack.
        for (int i = 0; i < heldNoteStackSize; ++i)
        {
            if (heldNoteStack[(size_t) i] == note)
            {
                for (int j = i; j < heldNoteStackSize - 1; ++j)
                    heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
                --heldNoteStackSize;
                break;
            }
        }

        // If the stack is already full, evict the oldest entry rather than silently failing to
        // track this note at all -- the note about to sound (below) must always be represented
        // in the stack, or its own eventual note-off has nothing to find, falls through to "some
        // other note must still be held," and retargets pitch instead of ever releasing (a real
        // stuck-note bug this used to produce whenever more than kMaxThreeOscVoices notes were
        // held in Mono/Legato at once).
        if (heldNoteStackSize == kMaxThreeOscVoices)
        {
            for (int j = 0; j < heldNoteStackSize - 1; ++j)
                heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
            --heldNoteStackSize;
        }
        heldNoteStack[(size_t) heldNoteStackSize++] = note;

        auto& voice = voices[0];
        voice.velocity = velocity;
        voice.noteNumber = note;

        if (wasEmpty)
        {
            // First note of a run -- full trigger, same as a poly voice: envelope reset+noteOn,
            // phase reset, and the pitch snaps instantly (there's nothing to glide from yet).
            voice.envelope.reset();
            voice.active = true;
            voice.triggerOrder = nextTriggerOrder++;
            for (auto& p : voice.phase)
                p = 0.0;

            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.setParameters ({ resolved.attack, resolved.decay, resolved.sustain / 100.0f, resolved.release });
            voice.envelope.noteOn();
            monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
        }
        else
        {
            // Legato retarget -- envelope and phase are left running; only the pitch moves.
            if (resolved.glide)
            {
                monoNoteNumberSmoothed.reset (sampleRate, (double) resolved.glideTimeMs / 1000.0);
                monoNoteNumberSmoothed.setTargetValue ((float) note);
            }
            else
            {
                monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
            }
        }
    }

    void ThreeOscModule::handleMonoNoteOff (int note, const ResolvedParams& resolved)
    {
        for (int i = 0; i < heldNoteStackSize; ++i)
        {
            if (heldNoteStack[(size_t) i] == note)
            {
                for (int j = i; j < heldNoteStackSize - 1; ++j)
                    heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
                --heldNoteStackSize;
                break;
            }
        }

        auto& voice = voices[0];

        // Releasing a note that isn't the one actually sounding right now has nothing further to
        // do -- it's already not audible (a still-held note lower in the stack, or, before the
        // eviction fix above, an untracked overflow note), so there's no voice state to update.
        // Only the currently-sounding note's own release should ever retarget or stop the voice.
        if (voice.noteNumber != note)
            return;

        if (heldNoteStackSize == 0)
        {
            voice.envelope.noteOff();
        }
        else
        {
            // Fall back to whichever other held note was pressed most recently (last-note
            // priority) rather than cutting off -- classic mono/legato behaviour.
            const int newTopNote = heldNoteStack[(size_t) (heldNoteStackSize - 1)];
            voice.noteNumber = newTopNote;
            if (resolved.glide)
            {
                monoNoteNumberSmoothed.reset (sampleRate, (double) resolved.glideTimeMs / 1000.0);
                monoNoteNumberSmoothed.setTargetValue ((float) newTopNote);
            }
            else
            {
                monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) newTopNote);
            }
        }
    }

    float ThreeOscModule::noteNumberToHz (float fractionalNoteNumber)
    {
        return 440.0f * std::pow (2.0f, (fractionalNoteNumber - 69.0f) / 12.0f);
    }

    void ThreeOscModule::renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved)
    {
        const int numChannels = (int) block.getNumChannels();
        // Mono/Legato uses only voices[0] -- see the class comment. Restricting the loop bound
        // (rather than checking resolved.monoLegato per-voice) also guarantees
        // monoNoteNumberSmoothed.getNextValue() below is called exactly once per sample.
        const int voiceCount = resolved.monoLegato ? 1 : kMaxThreeOscVoices;

        for (int i = startSample; i < endSample; ++i)
        {
            float accumL = 0.0f, accumR = 0.0f;

            for (int v = 0; v < voiceCount; ++v)
            {
                auto& voice = voices[(size_t) v];
                if (! voice.active)
                    continue;

                const double baseFreq = resolved.monoLegato
                    ? (double) noteNumberToHz (monoNoteNumberSmoothed.getNextValue())
                    : juce::MidiMessage::getMidiNoteInHertz (voice.noteNumber);

                std::array<double, kNumThreeOscOscillators> dt {};
                for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
                    dt[(size_t) osc] = (baseFreq * (double) resolved.freqMultiplier[(size_t) osc]) / sampleRate;

                const float osc1 = oscillatorSample (resolved.waveform[0], voice.phase[0], dt[0], 0.0);
                const float mod1to2 = osc1 * resolved.fm1to2 * kFmModIndexScale;
                const float osc2 = oscillatorSample (resolved.waveform[1], voice.phase[1], dt[1], (double) mod1to2);
                const float mod2to3 = osc2 * resolved.fm2to3 * kFmModIndexScale;
                const float osc3 = oscillatorSample (resolved.waveform[2], voice.phase[2], dt[2], (double) mod2to3);

                const float oscSamples[kNumThreeOscOscillators] = { osc1, osc2, osc3 };

                float voiceL = 0.0f, voiceR = 0.0f;
                for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
                {
                    const float s = oscSamples[osc] * (resolved.level[(size_t) osc] / 100.0f);
                    const float panPos = resolved.pan[(size_t) osc];
                    const float panL = std::sqrt (0.5f * (1.0f - panPos));
                    const float panR = std::sqrt (0.5f * (1.0f + panPos));
                    voiceL += s * panL;
                    voiceR += s * panR;
                }

                for (int osc = 0; osc < kNumThreeOscOscillators; ++osc)
                {
                    voice.phase[(size_t) osc] += dt[(size_t) osc];
                    voice.phase[(size_t) osc] -= std::floor (voice.phase[(size_t) osc]);
                }

                const float envVal = voice.envelope.getNextSample();
                const float voiceGain = envVal * voice.velocity;

                accumL += voiceL * voiceGain;
                accumR += voiceR * voiceGain;

                if (! voice.envelope.isActive())
                    voice.active = false; // fully released -- free for the next noteOn/steal
            }

            accumL *= resolved.outputGain;
            accumR *= resolved.outputGain;

            block.getChannelPointer (0)[i] += accumL;
            if (numChannels > 1)
                block.getChannelPointer (1)[i] += accumR;
            for (int ch = 2; ch < numChannels; ++ch)
                block.getChannelPointer ((size_t) ch)[i] += accumL;
        }
    }

    void ThreeOscModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix)
    {
        const auto resolved = resolveParams (modMatrix);

        int samplePos = 0;
        for (const auto metadata : midi)
        {
            const int eventPos = juce::jlimit (0, (int) block.getNumSamples(), metadata.samplePosition);
            if (eventPos > samplePos)
                renderRange (block, samplePos, eventPos, resolved);
            handleMidiEvent (metadata.getMessage(), resolved);
            samplePos = eventPos;
        }
        renderRange (block, samplePos, (int) block.getNumSamples(), resolved);
    }
}
