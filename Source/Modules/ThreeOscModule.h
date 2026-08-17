#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

namespace GGrid
{
    // A 16-voice polyphonic MIDI synth inspired by FL Studio's 3xOsc -- see Identifiers.h's
    // ThreeOscParam/ThreeOscOscParam comment for the full parameter scheme. Unlike every other
    // RackModule, this one is a graph SOURCE (see ModuleType::threeOsc's own comment): its
    // process() ignores whatever's in the incoming AudioBlock entirely and adds audio generated
    // from the MIDI note-on/off events in that same block's MidiBuffer -- see
    // GGridAudioProcessor::processBlock's isSourceRole handling, which is what routes it to
    // actually run instead of being fed the plugin's raw dry input like an Input-type slot.
    //
    // Each voice: 3 independently tuned/panned/leveled oscillators (Sine/Triangle/Saw/Square,
    // saw/square PolyBLEP-corrected against audio-rate aliasing), phase-modulated in sequence
    // (osc 1 -> osc 2 -> osc 3, via the FM 1>2/FM 2>3 depth knobs) and shaped by a shared
    // juce::ADSR amp envelope. Voice allocation is oldest-triggered-first stealing once all 16
    // are busy -- simple and predictable rather than an elaborate priority scheme.
    //
    // Mono/Legato collapses everything down to voices[0] alone: a new note-on while another is
    // already held retargets that one voice's pitch (via monoNoteNumberSmoothed, in semitone/
    // note-number space so equal steps sound like equal pitch steps) rather than triggering fresh
    // -- no envelope retrigger, no phase reset. heldNoteStack tracks press order so releasing one
    // note out of an overlapping run falls back to whichever other held note was pressed most
    // recently (last-note priority), not silence, as long as one remains held. Glide only affects
    // this retargeting -- poly voices (Mono/Legato off) always start directly at their own note's
    // pitch, no glide.
    class ThreeOscModule : public RackModule
    {
    public:
        ThreeOscModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

    private:
        struct Voice
        {
            bool active = false;
            int noteNumber = -1;
            float velocity = 0.0f;
            juce::int64 triggerOrder = 0;
            std::array<double, kNumThreeOscOscillators> phase { 0.0, 0.0, 0.0 };
            juce::ADSR envelope;
        };

        // Per-block, mod-matrix-resolved snapshot of every knob -- computed once per process()
        // call rather than per-sample, matching every other module's convention (the mod
        // matrix's own sources -- LFO/note-pitch/velocity -- only update once per block anyway).
        struct ResolvedParams
        {
            float attack = 0.0f, decay = 0.0f, sustain = 0.0f, release = 0.0f;
            float fm1to2 = 0.0f, fm2to3 = 0.0f;
            float outputGain = 1.0f;
            std::array<int, kNumThreeOscOscillators> waveform {};
            std::array<float, kNumThreeOscOscillators> freqMultiplier {}; // from coarse + fine
            std::array<float, kNumThreeOscOscillators> pan {};
            std::array<float, kNumThreeOscOscillators> level {};
            bool monoLegato = false;
            bool glide = false;
            float glideTimeMs = 50.0f;
        };

        ResolvedParams resolveParams (const ModulationMatrix& modMatrix) const;
        int findVoiceForNoteOn();
        void handleMidiEvent (const juce::MidiMessage& message, const ResolvedParams& resolved);
        void handleMonoNoteOn (int note, float velocity, const ResolvedParams& resolved);
        void handleMonoNoteOff (int note, const ResolvedParams& resolved);
        void renderRange (juce::dsp::AudioBlock<float>& block, int startSample, int endSample, const ResolvedParams& resolved);

        // Fractional-note-number-to-Hz -- juce::MidiMessage::getMidiNoteInHertz only accepts an
        // int, but glide needs a continuously-sliding note number.
        static float noteNumberToHz (float fractionalNoteNumber);

        // waveform: 0=Sine, 1=Triangle, 2=Saw, 3=Square (see getThreeOscWaveformChoices).
        // modOffset is added to phase before generating (osc N-1's current sample, scaled by the
        // FM depth knob) -- a read-time phase shift rather than an integrated frequency change,
        // i.e. classic phase-modulation synthesis (stable, no feedback into the running phase).
        static float oscillatorSample (int waveform, double phase, double phaseIncrement, double modOffset);

        int slotIndex;

        std::atomic<float>* attackParam;
        std::atomic<float>* decayParam;
        std::atomic<float>* sustainParam;
        std::atomic<float>* releaseParam;
        std::atomic<float>* fm1to2Param;
        std::atomic<float>* fm2to3Param;
        std::atomic<float>* outputParam;
        std::atomic<float>* monoLegatoParam;
        std::atomic<float>* glideParam;
        std::atomic<float>* glideTimeMsParam;

        std::array<std::atomic<float>*, kNumThreeOscOscillators> waveformParams {};
        std::array<std::atomic<float>*, kNumThreeOscOscillators> coarseParams {};
        std::array<std::atomic<float>*, kNumThreeOscOscillators> fineParams {};
        std::array<std::atomic<float>*, kNumThreeOscOscillators> panParams {};
        std::array<std::atomic<float>*, kNumThreeOscOscillators> levelParams {};

        std::array<Voice, kMaxThreeOscVoices> voices;
        juce::int64 nextTriggerOrder = 0;
        double sampleRate = 44100.0;

        // Mono/Legato state -- only voices[0] is used when active. heldNoteStack is press-order
        // (last element = most recently pressed still-held note); monoNoteNumberSmoothed is the
        // continuously-gliding pitch, in note-number/semitone space.
        std::array<int, kMaxThreeOscVoices> heldNoteStack {};
        int heldNoteStackSize = 0;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monoNoteNumberSmoothed;

        // How many full phase cycles' worth of shift 100% FM depth injects -- chosen empirically
        // for a clearly audible, DX7-ish timbral range without needing a separate "index" knob.
        static constexpr float kFmModIndexScale = 4.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ThreeOscModule)
    };
}
