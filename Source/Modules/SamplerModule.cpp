#include "SamplerModule.h"
#include <cmath>

namespace GGrid
{
    SamplerModule::SamplerModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : slotIndex (slotIndexIn),
          voicesParam    (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::voices))),
          glideParam     (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::glide))),
          glideTimeParam (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::glideTime))),
          attackParam    (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::attack))),
          decayParam     (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::decay))),
          sustainParam   (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::sustain))),
          releaseParam   (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::release))),
          outputParam    (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::output))),
          startModParam  (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::startMod))),
          endModParam    (apvtsIn.getRawParameterValue (samplerParamId (slotIndexIn, SamplerParam::endMod)))
    {
    }

    void SamplerModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
        scrubCrossfadeSamples = juce::jmax (1, (int) (sampleRate * 0.0087));
        for (auto& voice : voices)
        {
            voice.active = false;
            voice.crossfading = false;
            voice.envelope.setSampleRate (sampleRate);
            voice.envelope.reset();
        }
        heldNoteStackSize = 0;
        // Real Glide Time is set fresh at each legato retarget -- this just satisfies
        // juce::SmoothedValue's "must call reset() before use" contract (see ThreeOscModule's
        // identical comment on its own monoNoteNumberSmoothed).
        monoNoteNumberSmoothed.reset (sampleRate, 0.001);
        monoNoteNumberSmoothed.setCurrentAndTargetValue (60.0f);
    }

    void SamplerModule::reset()
    {
        for (auto& voice : voices)
        {
            voice.active = false;
            voice.crossfading = false;
            voice.envelope.reset();
        }
        heldNoteStackSize = 0;
    }

    SamplerModule::ResolvedParams SamplerModule::resolveParams (const ModulationMatrix& modMatrix) const
    {
        ResolvedParams r;
        r.voices = juce::jlimit (1, kMaxSamplerVoices, (int) voicesParam->load());
        r.glide = glideParam->load() >= 0.5f;
        r.glideTimeMs = juce::jlimit (1.0f, 2000.0f, glideTimeParam->load());
        r.attack = juce::jlimit (0.001f, 5.0f, attackParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::attack), 1.0f));
        r.decay = juce::jlimit (0.001f, 5.0f, decayParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::decay), 1.0f));
        r.sustain = juce::jlimit (0.0f, 100.0f, sustainParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::sustain), 50.0f));
        r.release = juce::jlimit (0.001f, 5.0f, releaseParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::release), 1.0f));

        const float outputOffset = modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::output), 12.0f);
        r.outputGain = juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 24.0f, outputParam->load() + outputOffset));

        r.startMod = juce::jlimit (-1.0f, 1.0f, startModParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::startMod), 1.0f));
        r.endMod = juce::jlimit (-1.0f, 1.0f, endModParam->load()
            + modMatrix.getOffsetForParam (samplerParamId (slotIndex, SamplerParam::endMod), 1.0f));

        return r;
    }

    bool SamplerModule::captureZoneForNote (int note, float velocity, Zone& outZone,
                                             std::shared_ptr<const SamplerSampleLibrary::LoadedSample>& outSample) const
    {
        const int velocity127 = juce::jlimit (0, 127, juce::roundToInt (velocity * 127.0f));
        juce::ScopedLock lock (zonesLock);
        for (int i = 0; i < numZones; ++i)
        {
            const auto& z = zones[(size_t) i];
            if (note >= z.keyLow && note <= z.keyHigh && velocity127 >= z.velLow && velocity127 <= z.velHigh)
            {
                outZone = z;
                outSample = loadedSamples[(size_t) i];
                return outSample != nullptr && outSample->buffer.getNumSamples() > 0;
            }
        }
        return false;
    }

    void SamplerModule::startVoiceFromZone (Voice& voice, int note, float velocity, const Zone& zone,
                                             std::shared_ptr<const SamplerSampleLibrary::LoadedSample> sample,
                                             const ResolvedParams& resolved)
    {
        voice.envelope.reset();
        voice.active = true;
        voice.released = false;
        voice.crossfading = false;
        voice.noteNumber = note;
        voice.velocity = velocity;
        voice.triggerOrder = nextTriggerOrder++;

        voice.rootNote = zone.rootNote;
        voice.startSample = zone.startSample;
        voice.endSample = zone.endSample;
        voice.loopEnabled = zone.loopEnabled;
        voice.loopStart = zone.loopStart;
        voice.loopEnd = zone.loopEnd;
        voice.sample = sample;
        voice.playbackPosition = (double) zone.startSample;

        const double pitchRatio = std::pow (2.0, ((double) note - (double) zone.rootNote) / 12.0);
        const double sampleRateRatio = sample->sampleRate / sampleRate;
        voice.playbackIncrement = pitchRatio * sampleRateRatio;

        voice.envelope.setSampleRate (sampleRate);
        voice.envelope.setParameters ({ resolved.attack, resolved.decay,
                                         resolved.sustain / 100.0f, resolved.release });
        voice.envelope.noteOn();
    }

    int SamplerModule::findVoiceForNoteOn (int voiceLimit)
    {
        // Index 0 is reserved exclusively for mono (Voices <= 1) -- see the class comment.
        for (int i = 1; i <= voiceLimit; ++i)
            if (! voices[(size_t) i].active)
                return i;

        int oldest = 1;
        for (int i = 2; i <= voiceLimit; ++i)
            if (voices[(size_t) i].triggerOrder < voices[(size_t) oldest].triggerOrder)
                oldest = i;
        return oldest;
    }

    void SamplerModule::handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved)
    {
        if (message.isNoteOn())
        {
            const int note = message.getNoteNumber();
            const float velocity = message.getFloatVelocity();

            if (resolved.voices <= 1)
            {
                handleMonoNoteOn (note, velocity, resolved);
                return;
            }

            Zone zone;
            std::shared_ptr<const SamplerSampleLibrary::LoadedSample> sample;
            if (! captureZoneForNote (note, velocity, zone, sample))
                return; // no zone covers this key/velocity -- silence, matches a real multisample instrument

            auto& voice = voices[(size_t) findVoiceForNoteOn (resolved.voices)];
            startVoiceFromZone (voice, note, velocity, zone, sample, resolved);
        }
        else if (message.isNoteOff())
        {
            const int note = message.getNoteNumber();

            // Deliberately checks the FULL poly range regardless of the current Voices setting,
            // not just resolved.voices -- if Voices was turned down (or toggled to/from mono)
            // while this note was held, its note-on could have gone through a path that's no
            // longer "current," and only checking today's setting would leave it stuck sounding
            // forever. Same reasoning as ThreeOscModule's identical note-off handling.
            for (int i = 1; i <= kMaxSamplerVoices; ++i)
                if (voices[(size_t) i].active && voices[(size_t) i].noteNumber == note)
                {
                    voices[(size_t) i].envelope.noteOff();
                    voices[(size_t) i].released = true;
                }

            if (heldNoteStackSize > 0 || (voices[0].active && voices[0].noteNumber == note))
                handleMonoNoteOff (note, resolved);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            for (auto& voice : voices)
                if (voice.active)
                {
                    voice.envelope.noteOff();
                    voice.released = true;
                }
            heldNoteStackSize = 0;
        }
    }

    void SamplerModule::handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved)
    {
        const bool wasEmpty = (heldNoteStackSize == 0);

        for (int i = 0; i < heldNoteStackSize; ++i)
            if (heldNoteStack[(size_t) i] == note)
            {
                for (int j = i; j < heldNoteStackSize - 1; ++j)
                    heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
                --heldNoteStackSize;
                break;
            }

        if (heldNoteStackSize == kMaxSamplerVoices)
        {
            for (int j = 0; j < heldNoteStackSize - 1; ++j)
                heldNoteStack[(size_t) j] = heldNoteStack[(size_t) (j + 1)];
            --heldNoteStackSize;
        }
        heldNoteStack[(size_t) heldNoteStackSize++] = note;

        auto& voice = voices[0];

        if (wasEmpty)
        {
            Zone zone;
            std::shared_ptr<const SamplerSampleLibrary::LoadedSample> sample;
            if (! captureZoneForNote (note, velocity, zone, sample))
            {
                // No zone matches -- track the note in the stack (so its later note-off is still
                // a clean no-op) but never actually trigger a voice.
                voice.active = false;
                voice.noteNumber = note;
                monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
                return;
            }

            startVoiceFromZone (voice, note, velocity, zone, sample, resolved);
            monoNoteNumberSmoothed.setCurrentAndTargetValue ((float) note);
        }
        else
        {
            // Legato retarget -- keeps playing the SAME zone/sample that was originally
            // triggered (re-selecting a different zone mid-note without an envelope retrigger
            // would sound broken), just bends pitch toward the new note.
            voice.noteNumber = note;
            voice.velocity = velocity;

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

    void SamplerModule::handleMonoNoteOff (int note, const ResolvedParams& resolved)
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

        if (voice.noteNumber != note)
            return;

        if (heldNoteStackSize == 0)
        {
            voice.released = true;
            voice.envelope.noteOff();
        }
        else
        {
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

    float SamplerModule::readInterpolated (const juce::AudioBuffer<float>& buffer, int channel, double position)
    {
        const int numSamples = buffer.getNumSamples();
        const int i0 = (int) std::floor (position);
        if (i0 < 0 || i0 >= numSamples)
            return 0.0f;
        const int i1 = juce::jmin (i0 + 1, numSamples - 1);
        const float frac = (float) (position - (double) i0);
        const float s0 = buffer.getSample (channel, i0);
        const float s1 = buffer.getSample (channel, i1);
        return s0 + frac * (s1 - s0);
    }

    void SamplerModule::renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved)
    {
        const int numChannels = (int) block.getNumChannels();
        const bool monoMode = resolved.voices <= 1;
        const int loopFrom = monoMode ? 0 : 1;
        const int loopTo = monoMode ? 0 : resolved.voices;

        for (int i = startSample; i < endSample; ++i)
        {
            float mixL = 0.0f, mixR = 0.0f;

            for (int v = loopFrom; v <= loopTo; ++v)
            {
                auto& voice = voices[(size_t) v];
                if (! voice.active)
                    continue;

                if (voice.sample == nullptr || voice.sample->buffer.getNumSamples() == 0)
                {
                    voice.active = false;
                    continue;
                }

                double increment = voice.playbackIncrement;
                if (v == 0)
                {
                    // Mono voice -- recompute every sample so Glide's continuously-sliding pitch
                    // is honoured (matches ThreeOscModule::renderRange's identical approach).
                    const double pitchRatio = std::pow (2.0, ((double) monoNoteNumberSmoothed.getNextValue() - (double) voice.rootNote) / 12.0);
                    increment = pitchRatio * (voice.sample->sampleRate / sampleRate);
                }

                const int fileLength = voice.sample->buffer.getNumSamples();
                const int endBound = voice.endSample > 0 ? juce::jmin (voice.endSample, fileLength) : fileLength;
                const bool loopActive = voice.loopEnabled && ! voice.released && voice.loopEnd > voice.loopStart;

                if (loopActive)
                {
                    // Start Mod/End Mod (resolved.startMod/endMod, -1..1) scrub THIS voice's own
                    // loop window as a fraction of its own file length every block -- an LFO
                    // patched into either knob continuously relocates where the loop lands, a
                    // granular-style scan. Recomputed fresh each sample (cheap arithmetic) rather
                    // than cached, since it must react the instant resolved.startMod/endMod change.
                    const double len = (double) fileLength;
                    const int loopEndBound = juce::jmin (voice.loopEnd, endBound);
                    double modLoopStart = (double) voice.loopStart + (double) resolved.startMod * len;
                    double modLoopEnd = (double) loopEndBound + (double) resolved.endMod * len;
                    modLoopStart = juce::jlimit (0.0, len - 1.0, modLoopStart);
                    modLoopEnd = juce::jlimit (modLoopStart + 1.0, len, modLoopEnd);

                    if (voice.playbackPosition >= modLoopEnd || voice.playbackPosition < modLoopStart)
                    {
                        // Any discontinuous relocation -- a natural end-of-loop reach, or the
                        // window having shifted out from under the current position because
                        // Start/End Mod moved -- gets a short crossfade instead of a hard jump, so
                        // LFO-driven scrubbing stays free of clicks/dropouts. Clamped to at most
                        // half the live window so very tight grain loops still fade cleanly
                        // instead of overlapping themselves.
                        const int fadeLen = juce::jlimit (1, scrubCrossfadeSamples,
                            (int) juce::jmax (1.0, (modLoopEnd - modLoopStart) * 0.5));
                        voice.crossfadeFromPosition = voice.playbackPosition;
                        voice.crossfadeFromIncrement = increment;
                        voice.playbackPosition = modLoopStart;
                        voice.crossfading = true;
                        voice.crossfadeSamplesRemaining = fadeLen;
                        voice.crossfadeSamplesTotal = fadeLen;
                    }
                }
                else if (voice.playbackPosition >= (double) endBound)
                {
                    voice.active = false;
                    continue;
                }

                const int numFileChannels = voice.sample->buffer.getNumChannels();
                float sL, sR;
                if (voice.crossfading)
                {
                    const float t = 1.0f - (float) voice.crossfadeSamplesRemaining / (float) voice.crossfadeSamplesTotal;
                    const float oldL = readInterpolated (voice.sample->buffer, 0, voice.crossfadeFromPosition);
                    const float oldR = numFileChannels > 1 ? readInterpolated (voice.sample->buffer, 1, voice.crossfadeFromPosition) : oldL;
                    const float newL = readInterpolated (voice.sample->buffer, 0, voice.playbackPosition);
                    const float newR = numFileChannels > 1 ? readInterpolated (voice.sample->buffer, 1, voice.playbackPosition) : newL;
                    sL = oldL * (1.0f - t) + newL * t;
                    sR = oldR * (1.0f - t) + newR * t;
                    voice.crossfadeFromPosition += voice.crossfadeFromIncrement;
                    if (--voice.crossfadeSamplesRemaining <= 0)
                        voice.crossfading = false;
                }
                else
                {
                    sL = readInterpolated (voice.sample->buffer, 0, voice.playbackPosition);
                    sR = numFileChannels > 1 ? readInterpolated (voice.sample->buffer, 1, voice.playbackPosition) : sL;
                }

                const float envVal = voice.envelope.getNextSample();
                const float voiceGain = envVal * voice.velocity;

                mixL += sL * voiceGain;
                mixR += sR * voiceGain;

                voice.playbackPosition += increment;

                if (! voice.envelope.isActive())
                    voice.active = false; // fully released -- free for the next noteOn/steal
            }

            mixL *= resolved.outputGain;
            mixR *= resolved.outputGain;

            block.getChannelPointer (0)[i] += mixL;
            if (numChannels > 1)
                block.getChannelPointer (1)[i] += mixR;
            for (int ch = 2; ch < numChannels; ++ch)
                block.getChannelPointer ((size_t) ch)[i] += mixL;
        }
    }

    void SamplerModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix)
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

    // -- Zone editing (GUI thread only) --

    bool SamplerModule::addZoneFromFile (const juce::File& file, int keyLow, int keyHigh)
    {
        auto sample = SamplerSampleLibrary::loadFromFile (file);
        if (sample == nullptr)
            return false;

        juce::ScopedLock lock (zonesLock);
        if (numZones >= kMaxSamplerZones)
            return false;

        Zone zone;
        zone.filePath = file.getFullPathName();
        zone.keyLow = juce::jlimit (0, 127, juce::jmin (keyLow, keyHigh));
        zone.keyHigh = juce::jlimit (0, 127, juce::jmax (keyLow, keyHigh));
        zone.velLow = 0;
        zone.velHigh = 127;
        zone.rootNote = 60;
        zone.startSample = 0;
        zone.endSample = sample->buffer.getNumSamples();
        zone.loopEnabled = false;
        zone.loopStart = 0;
        zone.loopEnd = 0;

        zones[(size_t) numZones] = zone;
        loadedSamples[(size_t) numZones] = sample;
        ++numZones;
        return true;
    }

    void SamplerModule::removeZone (int zoneIndex)
    {
        juce::ScopedLock lock (zonesLock);
        if (zoneIndex < 0 || zoneIndex >= numZones)
            return;

        for (int i = zoneIndex; i < numZones - 1; ++i)
        {
            zones[(size_t) i] = zones[(size_t) (i + 1)];
            loadedSamples[(size_t) i] = loadedSamples[(size_t) (i + 1)];
        }
        --numZones;
        loadedSamples[(size_t) numZones] = nullptr;
    }

    int SamplerModule::getNumZones() const
    {
        juce::ScopedLock lock (zonesLock);
        return numZones;
    }

    SamplerModule::Zone SamplerModule::getZone (int zoneIndex) const
    {
        juce::ScopedLock lock (zonesLock);
        if (zoneIndex < 0 || zoneIndex >= numZones)
            return {};
        return zones[(size_t) zoneIndex];
    }

    void SamplerModule::setZone (int zoneIndex, const Zone& newZone)
    {
        juce::ScopedLock lock (zonesLock);
        if (zoneIndex < 0 || zoneIndex >= numZones)
            return;

        // filePath changing means the loaded-sample cache entry needs refreshing too -- everything
        // else (key/vel range, root, start/end, loop) is a plain field swap.
        if (newZone.filePath != zones[(size_t) zoneIndex].filePath)
            loadedSamples[(size_t) zoneIndex] = SamplerSampleLibrary::loadFromFile (juce::File (newZone.filePath));

        zones[(size_t) zoneIndex] = newZone;
    }

    std::shared_ptr<const SamplerSampleLibrary::LoadedSample> SamplerModule::getZoneSample (int zoneIndex) const
    {
        juce::ScopedLock lock (zonesLock);
        if (zoneIndex < 0 || zoneIndex >= numZones)
            return nullptr;
        return loadedSamples[(size_t) zoneIndex];
    }

    void SamplerModule::writeExtraState (juce::XmlElement& parent) const
    {
        juce::ScopedLock lock (zonesLock);
        juce::StringArray tokens;
        for (int i = 0; i < numZones; ++i)
        {
            const auto& z = zones[(size_t) i];
            // Pipe-delimited fields; the file path is base64-encoded so an embedded ':'/'|' (a
            // Windows drive letter, or any path containing one) can't corrupt the token split.
            tokens.add (juce::Base64::toBase64 (z.filePath) + "|"
                + juce::String (z.keyLow) + "|" + juce::String (z.keyHigh) + "|"
                + juce::String (z.velLow) + "|" + juce::String (z.velHigh) + "|"
                + juce::String (z.rootNote) + "|"
                + juce::String (z.startSample) + "|" + juce::String (z.endSample) + "|"
                + juce::String ((int) z.loopEnabled) + "|"
                + juce::String (z.loopStart) + "|" + juce::String (z.loopEnd));
        }
        parent.setAttribute ("samplerZones", tokens.joinIntoString (";"));
    }

    void SamplerModule::readExtraState (const juce::XmlElement& parent)
    {
        auto tokens = juce::StringArray::fromTokens (parent.getStringAttribute ("samplerZones"), ";", "");

        std::array<Zone, kMaxSamplerZones> newZones;
        std::array<std::shared_ptr<const SamplerSampleLibrary::LoadedSample>, kMaxSamplerZones> newSamples;
        int newCount = 0;

        for (auto& token : tokens)
        {
            if (newCount >= kMaxSamplerZones)
                break;

            auto parts = juce::StringArray::fromTokens (token, "|", "");
            if (parts.size() != 11)
                continue;

            Zone z;
            juce::MemoryOutputStream decodedStream;
            if (juce::Base64::convertFromBase64 (decodedStream, parts[0]))
                z.filePath = decodedStream.toString();
            z.keyLow = parts[1].getIntValue();
            z.keyHigh = parts[2].getIntValue();
            z.velLow = parts[3].getIntValue();
            z.velHigh = parts[4].getIntValue();
            z.rootNote = parts[5].getIntValue();
            z.startSample = parts[6].getIntValue();
            z.endSample = parts[7].getIntValue();
            z.loopEnabled = parts[8].getIntValue() != 0;
            z.loopStart = parts[9].getIntValue();
            z.loopEnd = parts[10].getIntValue();

            newZones[(size_t) newCount] = z;
            newSamples[(size_t) newCount] = z.filePath.isNotEmpty()
                ? SamplerSampleLibrary::loadFromFile (juce::File (z.filePath)) : nullptr;
            ++newCount;
        }

        juce::ScopedLock lock (zonesLock);
        zones = newZones;
        loadedSamples = newSamples;
        numZones = newCount;
    }
}
