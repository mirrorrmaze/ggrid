#include "WavetableSynthModule.h"
#include <cmath>

namespace GGrid
{
    namespace
    {
        int modulatorForGenerator (int algorithm, int gen)
        {
            switch (algorithm)
            {
                case 0: // Series: 1 > 2 > 3 > ... > 8
                    return gen > 0 ? gen - 1 : -1;

                case 1: // Pairs: 1 > 2, 3 > 4, 5 > 6, 7 > 8
                    return (gen % 2 == 1) ? gen - 1 : -1;

                case 2: // Two Stacks: 1 > 2 > 3 > 4, 5 > 6 > 7 > 8
                    return (gen % 4 != 0) ? gen - 1 : -1;

                case 3: // Carriers: every generator outputs directly, no internal FM routing.
                default:
                    return -1;
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
          glideTimeMsParam (apvtsIn.getRawParameterValue (wavetableSynthParamId (slotIndexIn, WavetableSynthParam::glideTimeMs)))
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
            voice.phase.fill (0.0);
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
            voice.phase.fill (0.0);
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
        r.algorithm = juce::jlimit (0, getWavetableSynthAlgorithmChoices().size() - 1, (int) algorithmParam->load());
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

    int WavetableSynthModule::findVoiceForNoteOn()
    {
        for (int i = 0; i < kMaxWavetableSynthVoices; ++i)
            if (! voices[(size_t) i].active)
                return i;

        int oldest = 0;
        for (int i = 1; i < kMaxWavetableSynthVoices; ++i)
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

            auto& voice = voices[(size_t) findVoiceForNoteOn()];
            voice.envelope.reset();
            voice.active = true;
            voice.noteNumber = message.getNoteNumber();
            voice.velocity = message.getFloatVelocity();
            voice.triggerOrder = nextTriggerOrder++;
            voice.phase.fill (0.0);
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.setParameters ({ resolved.attack, resolved.decay, resolved.sustain / 100.0f, resolved.release });
            voice.envelope.noteOn();
        }
        else if (message.isNoteOff())
        {
            if (resolved.monoLegato)
            {
                handleMonoNoteOff (message.getNoteNumber(), resolved);
                return;
            }

            for (auto& voice : voices)
                if (voice.active && voice.noteNumber == message.getNoteNumber())
                    voice.envelope.noteOff();
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& voice : voices)
                if (voice.active)
                    voice.envelope.noteOff();
            heldNoteStackSize = 0;
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

        if (heldNoteStackSize < kMaxWavetableSynthVoices)
            heldNoteStack[(size_t) heldNoteStackSize++] = note;

        auto& voice = voices[0];
        voice.velocity = velocity;
        voice.noteNumber = note;
        if (wasEmpty)
        {
            voice.envelope.reset();
            voice.active = true;
            voice.triggerOrder = nextTriggerOrder++;
            voice.phase.fill (0.0);
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

    void WavetableSynthModule::renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved)
    {
        const int numChannels = (int) block.getNumChannels();
        const int voiceCount = resolved.monoLegato ? 1 : kMaxWavetableSynthVoices;

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

                const double baseFreq = resolved.monoLegato
                    ? (double) noteNumberToHz (monoNoteNumberSmoothed.getNextValue())
                    : juce::MidiMessage::getMidiNoteInHertz (voice.noteNumber);

                const auto isLastEnabledGeneratorInRange = [&resolved] (int gen, int endGen)
                {
                    for (int candidate = gen + 1; candidate <= endGen; ++candidate)
                    {
                        const auto& laterParams = resolved.gens[(size_t) candidate];
                        if (laterParams.enabled && laterParams.level > 0.0f)
                            return false;
                    }

                    return true;
                };

                const auto isCarrierGenerator = [&resolved, &isLastEnabledGeneratorInRange] (int gen)
                {
                    switch (resolved.algorithm)
                    {
                        case 0:  return isLastEnabledGeneratorInRange (gen, kNumWavetableSynthGenerators - 1);
                        case 1:  return isLastEnabledGeneratorInRange (gen, juce::jmin ((gen / 2) * 2 + 1, kNumWavetableSynthGenerators - 1));
                        case 2:  return isLastEnabledGeneratorInRange (gen, juce::jmin ((gen / 4) * 4 + 3, kNumWavetableSynthGenerators - 1));
                        case 3:  return true;
                        default: return true;
                    }
                };

                float genSamples[kNumWavetableSynthGenerators] {};
                for (int gen = 0; gen < kNumWavetableSynthGenerators; ++gen)
                {
                    const auto& params = resolved.gens[(size_t) gen];
                    if (! params.enabled || params.level <= 0.0f)
                        continue;

                    const auto table = getTable (gen);
                    const float frame = table != nullptr ? juce::jlimit (0.0f, (float) table->numFrames - 1.0f, params.frame) : 0.0f;
                    const int modulator = modulatorForGenerator (resolved.algorithm, gen);
                    const float modSample = modulator >= 0 ? genSamples[modulator] : 0.0f;
                    const float phaseMod = modSample * params.fm * kFmModIndexScale;
                    const float sample = table != nullptr ? table->sample (frame, (float) voice.phase[(size_t) gen] + phaseMod, params.smooth) : 0.0f;
                    genSamples[gen] = sample;

                    if (isCarrierGenerator (gen))
                    {
                        const float panL = std::sqrt (0.5f * (1.0f - params.pan));
                        const float panR = std::sqrt (0.5f * (1.0f + params.pan));
                        const float scaled = sample * params.level;
                        voiceBusL[params.output] += scaled * panL;
                        voiceBusR[params.output] += scaled * panR;
                    }

                    const double dt = (baseFreq * (double) params.freqMultiplier) / sampleRate;
                    voice.phase[(size_t) gen] += dt;
                    voice.phase[(size_t) gen] -= std::floor (voice.phase[(size_t) gen]);
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
