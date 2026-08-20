#include "WavetableSynthModule.h"
#include <cmath>

namespace GGrid
{
    namespace
    {
        float intervalFromPattern (const int* intervals, int count, int lane, int unison)
        {
            if (count <= 0)
                return 0.0f;

            const int centre = unison / 2;
            const int relative = lane - centre;
            const int octave = std::abs (relative) / count;
            const int degree = std::abs (relative) % count;
            const float semis = (float) intervals[degree] + (float) octave * 12.0f;
            return relative < 0 ? -semis : semis;
        }

        float unisonOffsetSemis (int mode, int lane, int unison, float spreadCents, int multiplier)
        {
            if (unison <= 1 || spreadCents <= 0.0f)
                return 0.0f;

            const float spreadSemis = spreadCents * (float) multiplier / 100.0f;
            const float lane01 = (float) lane / (float) (unison - 1);
            const float centred = lane01 * 2.0f - 1.0f;
            constexpr int pentatonicMaj[] = { 0, 2, 4, 7, 9 };
            constexpr int pentatonicMin[] = { 0, 3, 5, 7, 10 };
            constexpr int octaves[]       = { 0, 12 };
            constexpr int fifths[]        = { 0, 7 };
            constexpr int minor[]         = { 0, 3, 7 };
            constexpr int minorMin7[]     = { 0, 3, 7, 10 };
            constexpr int minorMaj7[]     = { 0, 3, 7, 11 };
            constexpr int major[]         = { 0, 4, 7 };
            constexpr int majorMin7[]     = { 0, 4, 7, 10 };
            constexpr int majorMaj7[]     = { 0, 4, 7, 11 };
            constexpr int sus2[]          = { 0, 2, 7 };
            constexpr int sus4[]          = { 0, 5, 7 };
            constexpr int dim[]           = { 0, 3, 6 };
            constexpr int dim7[]          = { 0, 3, 6, 9 };
            constexpr int harmonics[]     = { 0, 12, 19, 24, 28, 31, 34, 36 };

            switch (mode)
            {
                case 1:  return std::sin (centred * juce::MathConstants<float>::halfPi) * spreadSemis; // Smooth
                case 2:  return std::copysign (centred * centred, centred) * spreadSemis;              // Synthetic
                case 3:  return lane01 * spreadSemis * 2.0f;                                           // Freq Stack
                case 4:  return std::round (centred * (float) multiplier) * spreadSemis;               // Pitch Stack
                case 5:  return std::round (centred * 4.0f) * 12.0f * spreadSemis;                     // Shepard
                case 6:  return intervalFromPattern (pentatonicMaj, 5, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 7:  return intervalFromPattern (pentatonicMin, 5, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 8:  return intervalFromPattern (octaves,       2, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 9:  return intervalFromPattern (fifths,        2, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 10: return intervalFromPattern (minor,         3, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 11: return intervalFromPattern (minorMin7,     4, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 12: return intervalFromPattern (minorMaj7,     4, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 13: return intervalFromPattern (major,         3, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 14: return intervalFromPattern (majorMin7,     4, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 15: return intervalFromPattern (majorMaj7,     4, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 16: return intervalFromPattern (sus2,          3, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 17: return intervalFromPattern (sus4,          3, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 18: return intervalFromPattern (dim,           3, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 19: return intervalFromPattern (dim7,          4, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 20: return intervalFromPattern (harmonics,     8, lane, unison) * (float) multiplier * juce::jlimit (0.0f, 1.0f, spreadCents / 100.0f);
                case 0:
                default: return centred * spreadSemis; // Hard
            }
        }
    }

    WavetableSynthModule::WavetableSynthModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          attackParam      (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::attack))),
          decayParam       (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::decay))),
          sustainParam     (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::sustain))),
          releaseParam     (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::release))),
          outputParam      (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::output))),
          algorithmParam   (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::algorithm))),
          monoLegatoParam  (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::monoLegato))),
          glideParam       (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::glide))),
          glideTimeMsParam (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::glideTimeMs))),
          polyphonyParam   (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::polyphony))),
          masterPitchParam (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::masterPitch))),
          bendRangeParam   (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::bendRange))),
          unisonParam      (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::unison))),
          spreadParam      (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::spread))),
          multiplierParam  (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::multiplier)))
    {
        loadedTableIndices.fill (-1);
        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            enabledParams[(size_t) gen]   = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::enabled));
            tableParams[(size_t) gen]     = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::table));
            frameParams[(size_t) gen]     = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::frame));
            smoothParams[(size_t) gen]    = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::smooth));
            coarseParams[(size_t) gen]    = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::coarse));
            fineParams[(size_t) gen]      = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::fine));
            panParams[(size_t) gen]       = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::pan));
            levelParams[(size_t) gen]     = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::level));
            fmParams[(size_t) gen]        = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::fm));
            outputBusParams[(size_t) gen] = apvtsIn.getRawParameterValue (wavetableSynthGenParamId (slotIndexIn, gen, WavetableSynthGenParam::output));
        }
    }

    void WavetableSynthModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        for (auto& voice : voices)
        {
            voice.active = false;
            for (auto& genPhases : voice.phase)
                genPhases.fill (0.0);
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.reset();
        }

        for (auto& bus : outputBusBuffers)
            bus.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);

        heldNoteStackSize = 0;
        monoNoteNumberSmoothed.reset (sampleRate, 0.001);
        monoNoteNumberSmoothed.setCurrentAndTargetValue (60.0f);
    }

    void WavetableSynthModule::reset()
    {
        for (auto& voice : voices)
        {
            voice.active = false;
            voice.envelope.reset();
            for (auto& genPhases : voice.phase)
                genPhases.fill (0.0);
        }
        heldNoteStackSize = 0;
        for (auto& bus : outputBusBuffers)
            bus.clear();
    }

    const juce::AudioBuffer<float>* WavetableSynthModule::getOutputBusBuffer (int busIndex) const
    {
        if (busIndex < 0 || busIndex >= kNumWavetableSynthOutputs)
            return nullptr;
        return &outputBusBuffers[(size_t) busIndex];
    }

    std::shared_ptr<const WavetableLibrary::Table> WavetableSynthModule::getTable (int index)
    {
        const int gen = juce::jlimit (0, kNumWavetableSynthGenerators - 1, index);
        const int wanted = (int) tableParams[(size_t) gen]->load();
        if (wanted != loadedTableIndices[(size_t) gen] || loadedTables[(size_t) gen] == nullptr)
        {
            loadedTables[(size_t) gen] = WavetableLibrary::loadTable (wanted);
            loadedTableIndices[(size_t) gen] = wanted;
        }
        return loadedTables[(size_t) gen];
    }

    WavetableSynthModule::ResolvedParams WavetableSynthModule::resolveParams (const ModulationMatrix& modMatrix) const
    {
        ResolvedParams r;
        r.attack = juce::jmax (0.001f, attackParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::attack), 1.0f));
        r.decay = juce::jmax (0.001f, decayParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::decay), 1.0f));
        r.sustain = juce::jlimit (0.0f, 100.0f, sustainParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::sustain), 50.0f));
        r.release = juce::jmax (0.001f, releaseParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::release), 1.0f));
        r.outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::output), 12.0f)));
        r.polyphony = juce::jlimit (1, kMaxWavetableSynthVoices, (int) std::round (polyphonyParam->load()));
        r.masterPitch = juce::jlimit (-48.0f, 48.0f, masterPitchParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::masterPitch), 12.0f));
        r.bendRange = juce::jlimit (0.0f, 24.0f, bendRangeParam->load());
        r.unison = juce::jlimit (1, kMaxUnison, (int) std::round (unisonParam->load()));
        r.spreadCents = juce::jlimit (0.0f, 100.0f, spreadParam->load()
            + modMatrix.getOffsetForParam (wavetableSynthParamId (slotIndex, WavetableSynthParam::spread), 50.0f));
        r.spreadMode = juce::jlimit (0, getWavetableSynthAlgorithmChoices().size() - 1, (int) algorithmParam->load());
        r.spreadMultiplier = juce::jlimit (1, 8, multiplierParam != nullptr ? (int) multiplierParam->load() + 1 : 1);
        r.monoLegato = monoLegatoParam->load() >= 0.5f;
        r.glide = glideParam->load() >= 0.5f;
        r.glideTimeMs = juce::jmax (1.0f, glideTimeMsParam->load());

        for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
        {
            auto& g = r.gens[(size_t) gen];
            g.enabled = enabledParams[(size_t) gen]->load() >= 0.5f;
            g.table = (int) tableParams[(size_t) gen]->load();
            g.frame = frameParams[(size_t) gen]->load() - 1.0f;
            g.smooth = juce::jlimit (0.0f, 1.0f, smoothParams[(size_t) gen]->load() / 100.0f);

            const float coarse = juce::jlimit (-48.0f, 48.0f, coarseParams[(size_t) gen]->load()
                + modMatrix.getOffsetForParam (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::coarse), 12.0f));
            const float fine = juce::jlimit (-100.0f, 100.0f, fineParams[(size_t) gen]->load()
                + modMatrix.getOffsetForParam (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::fine), 50.0f));
            g.freqMultiplier = std::pow (2.0f, (coarse + fine / 100.0f) / 12.0f);
            g.pan = juce::jlimit (-1.0f, 1.0f, panParams[(size_t) gen]->load()
                + modMatrix.getOffsetForParam (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::pan), 1.0f));
            g.level = juce::jlimit (0.0f, 100.0f, levelParams[(size_t) gen]->load()
                + modMatrix.getOffsetForParam (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::level), 50.0f)) / 100.0f;
            g.fm = juce::jlimit (0.0f, 100.0f, fmParams[(size_t) gen]->load()
                + modMatrix.getOffsetForParam (wavetableSynthGenParamId (slotIndex, gen, WavetableSynthGenParam::fm), 50.0f)) / 100.0f;
            g.output = juce::jlimit (0, kNumWavetableSynthOutputs - 1, (int) outputBusParams[(size_t) gen]->load());
        }
        return r;
    }

    int WavetableSynthModule::findVoiceForNoteOn (int maxPolyphony)
    {
        const int lastVoice = juce::jlimit (1, kMaxWavetableSynthVoices - 1, maxPolyphony);
        // Index 0 is reserved exclusively for Mono/Legato (handleMonoNoteOn/Off) -- if poly could
        // also land a note on it, a note-off arriving after a mode switch couldn't tell which path
        // owns voices[0], and note-off routing (see handleMidiEvent below) needs that to be
        // unambiguous to avoid ever orphaning a still-sounding voice.
        for (int i = 1; i <= lastVoice; ++i)
            if (! voices[(size_t) i].active)
                return i;

        int oldest = 1;
        for (int i = 2; i <= lastVoice; ++i)
            if (voices[(size_t) i].triggerOrder < voices[(size_t) oldest].triggerOrder)
                oldest = i;
        return oldest;
    }

    void WavetableSynthModule::handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved)
    {
        if (message.isNoteOn())
        {
            if (resolved.monoLegato)
            {
                handleMonoNoteOn (message.getNoteNumber(), message.getFloatVelocity(), resolved);
                return;
            }

            auto& voice = voices[(size_t) findVoiceForNoteOn (resolved.polyphony)];
            voice.envelope.reset();
            voice.active = true;
            voice.noteNumber = message.getNoteNumber();
            voice.velocity = message.getFloatVelocity();
            voice.triggerOrder = nextTriggerOrder++;
            for (auto& genPhases : voice.phase)
                genPhases.fill (0.0);
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
            for (int i = 1; i < kMaxWavetableSynthVoices; ++i)
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
        else if (message.isPitchWheel())
        {
            currentPitchBendSemis = ((float) message.getPitchWheelValue() - 8192.0f) * resolved.bendRange / 8192.0f;
        }
    }

    void WavetableSynthModule::handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved)
    {
        const bool wasEmpty = heldNoteStackSize == 0;
        for (int i = 0; i < heldNoteStackSize; ++i)
            if (heldNoteStack[(size_t) i] == note)
            {
                for (int j = i; j < heldNoteStackSize - 1; ++j)
                    heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
                --heldNoteStackSize;
                break;
            }

        // If the stack is already full, evict the oldest entry rather than silently failing to
        // track this note at all -- the note about to sound (below) must always be represented
        // in the stack, or its own eventual note-off has nothing to find, falls through to "some
        // other note must still be held," and retargets pitch instead of ever releasing (a real
        // stuck-note bug this used to produce whenever more than kMaxWavetableSynthVoices notes
        // were held in Mono/Legato at once).
        if (heldNoteStackSize == kMaxWavetableSynthVoices)
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
            voice.envelope.reset();
            voice.active = true;
            voice.triggerOrder = nextTriggerOrder++;
            for (auto& genPhases : voice.phase)
                genPhases.fill (0.0);
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.setParameters ({ resolved.attack, resolved.decay, resolved.sustain / 100.0f, resolved.release });
            voice.envelope.noteOn();
            monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
        }
        else if (resolved.glide)
        {
            monoNoteNumberSmoothed.reset (sampleRate, (double) resolved.glideTimeMs / 1000.0);
            monoNoteNumberSmoothed.setTargetValue ((float) note);
        }
        else
        {
            monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
        }
    }

    void WavetableSynthModule::handleMonoNoteOff (int note, const ResolvedParams& resolved)
    {
        for (int i = 0; i < heldNoteStackSize; ++i)
            if (heldNoteStack[(size_t) i] == note)
            {
                for (int j = i; j < heldNoteStackSize - 1; ++j)
                    heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
                --heldNoteStackSize;
                break;
            }

        auto& voice = voices[0];

        // Releasing a note that isn't the one actually sounding right now has nothing further to
        // do -- it's already not audible (a still-held note lower in the stack, or, before the
        // eviction fix above, an untracked overflow note), so there's no voice state to update.
        // Only the currently-sounding note's own release should ever retarget or stop the voice.
        if (voice.noteNumber != note)
            return;

        if (heldNoteStackSize == 0)
            voice.envelope.noteOff();
        else
        {
            const int newNote = heldNoteStack[(size_t) (heldNoteStackSize - 1)];
            voice.noteNumber = newNote;
            if (resolved.glide)
            {
                monoNoteNumberSmoothed.reset (sampleRate, (double) resolved.glideTimeMs / 1000.0);
                monoNoteNumberSmoothed.setTargetValue ((float) newNote);
            }
            else
            {
                monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) newNote);
            }
        }
    }

    float WavetableSynthModule::noteNumberToHz (float fractionalNoteNumber)
    {
        return 440.0f * std::pow (2.0f, (fractionalNoteNumber - 69.0f) / 12.0f);
    }

    void WavetableSynthModule::renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample,
                                            const ResolvedParams& resolved, const juce::AudioBuffer<float>* externalFm)
    {
        const int numChannels = (int) block.getNumChannels();
        const int voiceCount = resolved.monoLegato ? 1 : juce::jlimit (2, kMaxWavetableSynthVoices, resolved.polyphony + 1);

        for (int i = startSample; i < endSample; ++i)
        {
            float busL[kNumWavetableSynthOutputs] {};
            float busR[kNumWavetableSynthOutputs] {};

            for (int v = 0; v < voiceCount; ++v)
            {
                auto& voice = voices[(size_t) v];
                if (! voice.active)
                    continue;

                float voiceBusL[kNumWavetableSynthOutputs] {};
                float voiceBusR[kNumWavetableSynthOutputs] {};

                const float noteNumber = (resolved.monoLegato ? monoNoteNumberSmoothed.getNextValue() : (float) voice.noteNumber)
                    + resolved.masterPitch + currentPitchBendSemis;
                const double baseFreq = (double) noteNumberToHz (noteNumber);

                float externalFmSample = 0.0f;
                if (externalFm != nullptr && externalFm->getNumSamples() > i)
                {
                    for (int ch = 0; ch < externalFm->getNumChannels(); ++ch)
                        externalFmSample += externalFm->getReadPointer (ch)[i];
                    externalFmSample /= (float) juce::jmax (1, externalFm->getNumChannels());
                }

                for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
                {
                    const auto& params = resolved.gens[(size_t) gen];
                    if (! params.enabled || params.level <= 0.0f)
                        continue;

                    const auto table = getTable (gen);
                    const float frame = table != nullptr ? juce::jlimit (0.0f, (float) table->numFrames - 1.0f, params.frame) : 0.0f;
                    float genL = 0.0f, genR = 0.0f;
                    for (int lane = 0; lane < resolved.unison; ++lane)
                    {
                        const float lanePan = resolved.unison <= 1 ? 0.0f : ((float) lane / (float) (resolved.unison - 1)) * 2.0f - 1.0f;
                        const float pan = juce::jlimit (-1.0f, 1.0f, params.pan + lanePan * juce::jlimit (0.0f, 1.0f, resolved.spreadCents / 100.0f));
                        const float phaseMod = externalFmSample * kFmModIndexScale;
                        const float sample = table != nullptr ? table->sample (frame, (float) voice.phase[(size_t) gen][(size_t) lane] + phaseMod, params.smooth) : 0.0f;
                        const float panL = std::sqrt (0.5f * (1.0f - pan));
                        const float panR = std::sqrt (0.5f * (1.0f + pan));
                        genL += sample * panL;
                        genR += sample * panR;

                        const float detuneSemis = unisonOffsetSemis (resolved.spreadMode, lane, resolved.unison,
                                                                      resolved.spreadCents, resolved.spreadMultiplier);
                        const double detuneMul = std::pow (2.0, (double) detuneSemis / 12.0);
                        const double dt = (baseFreq * (double) params.freqMultiplier * detuneMul) / sampleRate;
                        voice.phase[(size_t) gen][(size_t) lane] += dt;
                        voice.phase[(size_t) gen][(size_t) lane] -= std::floor (voice.phase[(size_t) gen][(size_t) lane]);
                    }

                    const float scale = params.level / std::sqrt ((float) resolved.unison);
                    voiceBusL[params.output] += genL * scale;
                    voiceBusR[params.output] += genR * scale;
                }

                const float envGain = voice.envelope.getNextSample() * voice.velocity * resolved.outputGain;
                for (int bus = 0; bus < kNumWavetableSynthOutputs; ++bus)
                {
                    busL[bus] += voiceBusL[bus] * envGain;
                    busR[bus] += voiceBusR[bus] * envGain;
                }

                if (! voice.envelope.isActive())
                    voice.active = false;
            }

            float sumL = 0.0f, sumR = 0.0f;
            for (int bus = 0; bus < kNumWavetableSynthOutputs; ++bus)
            {
                outputBusBuffers[(size_t) bus].getWritePointer (0)[i] += busL[bus];
                if (numChannels > 1)
                    outputBusBuffers[(size_t) bus].getWritePointer (1)[i] += busR[bus];
                for (int ch = 2; ch < numChannels; ++ch)
                    outputBusBuffers[(size_t) bus].getWritePointer (ch)[i] += busL[bus];

                sumL += busL[bus];
                sumR += busR[bus];
            }

            block.getChannelPointer (0)[i] += sumL;
            if (numChannels > 1)
                block.getChannelPointer (1)[i] += sumR;
            for (int ch = 2; ch < numChannels; ++ch)
                block.getChannelPointer ((size_t) ch)[i] += sumL;
        }
    }

    void WavetableSynthModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix)
    {
        for (auto& bus : outputBusBuffers)
        {
            bus.setSize ((int) block.getNumChannels(), (int) block.getNumSamples(), false, false, true);
            bus.clear();
        }

        const bool hasExternalFm = [&block]
        {
            for (int ch = 0; ch < (int) block.getNumChannels(); ++ch)
                for (int i = 0; i < (int) block.getNumSamples(); ++i)
                    if (std::abs (block.getSample (ch, i)) > 0.000001f)
                        return true;
            return false;
        }();

        const juce::AudioBuffer<float>* externalFm = nullptr;
        if (hasExternalFm)
        {
            externalFmBuffer.setSize ((int) block.getNumChannels(), (int) block.getNumSamples(), false, false, true);
            for (int ch = 0; ch < (int) block.getNumChannels(); ++ch)
                externalFmBuffer.copyFrom (ch, 0, block.getChannelPointer ((size_t) ch), (int) block.getNumSamples());
            externalFm = &externalFmBuffer;
            block.clear();
        }

        const auto resolved = resolveParams (modMatrix);
        int samplePos = 0;
        for (const auto metadata : midi)
        {
            const int eventPos = juce::jlimit (0, (int) block.getNumSamples(), metadata.samplePosition);
            if (eventPos > samplePos)
                renderRange (block, samplePos, eventPos, resolved, externalFm);
            handleMidiEvent (metadata.getMessage(), resolved);
            samplePos = eventPos;
        }
        renderRange (block, samplePos, (int) block.getNumSamples(), resolved, externalFm);
    }
}
