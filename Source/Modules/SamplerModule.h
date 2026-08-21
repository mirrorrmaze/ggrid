#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include "../Sampler/SamplerSampleLibrary.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace GGrid
{
    // A multi-sample instrument, Phase 1 of an Ableton-Sampler-referenced build (see this
    // project's plan history) -- drag-and-drop audio files onto the module to create zones, each
    // covering a key/velocity range; playback pitches the loaded sample via resampling relative
    // to each zone's Root note (no time-stretch/Warp -- confirmed Ableton's actual multi-sample
    // Sampler works the same way, unlike Simpler). Like ThreeOsc/WT Synth, a Sampler slot is a
    // graph SOURCE, not a signal processor -- see ModuleType::sampler's own comment.
    //
    // Deliberately does NOT have its own filter section, pitch envelope, FM oscillator, LFOs, or
    // MIDI-learn tab the way Ableton's Sampler does -- GGrid already has separate Filter/
    // Nonlinear Filter modules (chain one after Sampler in the rack) and a modulation-cable
    // system (LFO/Envelope/ADSR modules patch into almost any knob on any module, Sampler's own
    // knobs included) that cover that ground generically for every module already, so building
    // duplicate copies into this one module would just be redundant DSP.
    //
    // Voices/glide/held-note-stack mirrors ThreeOscModule/WavetableSynthModule's now-corrected
    // pattern exactly: voices[0] is reserved for mono (Voices <= 1) and never claimed by the poly
    // voice-stealing search, and note-off always checks both the poly voices AND the mono stack
    // regardless of the CURRENT Voices setting -- see ThreeOscModule::handleMidiEvent's own
    // comment for why (a stuck-note bug this session found and fixed there, reused correctly here
    // from the start rather than re-derived).
    class SamplerModule : public RackModule
    {
    public:
        SamplerModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        void writeExtraState (juce::XmlElement& parent) const override;
        void readExtraState (const juce::XmlElement& parent) override;

        // -- Zone editing, called from the GUI thread only --

        struct Zone
        {
            juce::String filePath;
            int keyLow = 0, keyHigh = 127;
            int velLow = 0, velHigh = 127;
            int rootNote = 60;
            int startSample = 0, endSample = 0; // endSample == 0 means "the loaded buffer's length"
            bool loopEnabled = false;
            int loopStart = 0, loopEnd = 0; // loopEnd == 0 (or <= loopStart) means "not a real loop"
        };

        // Loads file (via SamplerSampleLibrary, safe to call from the message thread) and appends
        // a new zone covering [keyLow, keyHigh]/[0, 127] velocity if there's room and the file is
        // a recognised audio format. Returns false (no-op) otherwise.
        bool addZoneFromFile (const juce::File& file, int keyLow, int keyHigh);
        void removeZone (int zoneIndex);
        int getNumZones() const;
        Zone getZone (int zoneIndex) const; // by value -- a locked-and-copied snapshot
        void setZone (int zoneIndex, const Zone& newZone); // whole-struct replace, e.g. after a knob edit
        std::shared_ptr<const SamplerSampleLibrary::LoadedSample> getZoneSample (int zoneIndex) const;

    private:
        // Per-block, mod-matrix-resolved snapshot -- see ThreeOscModule::ResolvedParams's own
        // comment for why this is computed once per process() call rather than per-sample.
        struct ResolvedParams
        {
            int voices = 16;
            bool glide = false;
            float glideTimeMs = 50.0f;
            float attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f;
            float outputGain = 1.0f;
            float startMod = 0.0f, endMod = 0.0f;
        };

        struct Voice
        {
            bool active = false;
            bool released = false;
            int noteNumber = -1;
            float velocity = 0.0f;
            juce::int64 triggerOrder = 0;
            juce::ADSR envelope;

            // Snapshot of the zone this voice is playing, captured at note-on -- playback never
            // re-reads the live zones array while sounding: a zone edit mid-note shouldn't
            // retroactively change an already-triggered voice, and this keeps the hot per-sample
            // path lock-free (only note-on locks zonesLock, via captureZoneForNote).
            int rootNote = 60;
            int startSample = 0, endSample = 0;
            bool loopEnabled = false;
            int loopStart = 0, loopEnd = 0;
            std::shared_ptr<const SamplerSampleLibrary::LoadedSample> sample;

            double playbackPosition = 0.0;
            // Fixed for poly voices (computed once at note-on); recomputed every sample for
            // voices[0] alone, since Glide needs its pitch to keep sliding while held.
            double playbackIncrement = 1.0;

            // Click-free relocation for loop wraps -- every wrap (a natural end-of-loop reach, or
            // Start Mod/End Mod having moved the live loop window out from under the current
            // position) crossfades from the old read stream into the new one over
            // kScrubCrossfadeSeconds instead of jumping instantaneously. See renderRange.
            bool crossfading = false;
            double crossfadeFromPosition = 0.0;
            double crossfadeFromIncrement = 1.0;
            int crossfadeSamplesRemaining = 0;
            int crossfadeSamplesTotal = 1;
        };

        ResolvedParams resolveParams (const ModulationMatrix& modMatrix) const;
        int findVoiceForNoteOn (int voiceLimit);
        void handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved);
        void handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved);
        void handleMonoNoteOff (int note, const ResolvedParams& resolved);
        void renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved);

        // Locates the first zone whose key/velocity range contains (note, velocity) -- first
        // match wins if ranges overlap, matching the plan's "simple, predictable" choice over an
        // elaborate priority scheme (same philosophy as ThreeOsc's oldest-first voice stealing).
        // Locks zonesLock; only ever called from handleMidiEvent/handleMonoNoteOn (note-on time),
        // never per-sample.
        bool captureZoneForNote (int note, float velocity, Zone& outZone,
                                  std::shared_ptr<const SamplerSampleLibrary::LoadedSample>& outSample) const;

        void startVoiceFromZone (Voice& voice, int note, float velocity, const Zone& zone,
                                  std::shared_ptr<const SamplerSampleLibrary::LoadedSample> sample,
                                  const ResolvedParams& resolved);

        static float readInterpolated (const juce::AudioBuffer<float>& buffer, int channel, double position);

        int slotIndex;

        std::atomic<float>* voicesParam;
        std::atomic<float>* glideParam;
        std::atomic<float>* glideTimeParam;
        std::atomic<float>* attackParam;
        std::atomic<float>* decayParam;
        std::atomic<float>* sustainParam;
        std::atomic<float>* releaseParam;
        std::atomic<float>* outputParam;
        std::atomic<float>* startModParam;
        std::atomic<float>* endModParam;

        double sampleRate = 44100.0;
        // ~8.7ms at 44.1kHz -- short enough to read as a clean grain retrigger rather than a
        // blurred crossfade, long enough to fully mask the discontinuity. Recomputed in prepare()
        // for the actual host sample rate; clamped per-voice to half the live loop window so very
        // short/tight grain loops still get a (shorter) click-free fade rather than overlapping.
        int scrubCrossfadeSamples = 384;

        // voices[0] is the dedicated mono voice; voices[1..kMaxSamplerVoices] are the poly pool
        // (see the class comment on why index 0 is reserved rather than shared).
        std::array<Voice, kMaxSamplerVoices + 1> voices;
        juce::int64 nextTriggerOrder = 0;

        std::array<int, kMaxSamplerVoices> heldNoteStack {};
        int heldNoteStackSize = 0;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoNoteNumberSmoothed;

        juce::CriticalSection zonesLock;
        std::array<Zone, kMaxSamplerZones> zones;
        std::array<std::shared_ptr<const SamplerSampleLibrary::LoadedSample>, kMaxSamplerZones> loadedSamples;
        int numZones = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerModule)
    };
}
