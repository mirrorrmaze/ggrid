#pragma once

#include "../Rack/RackModule.h"
#include "../Params/Identifiers.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace GGrid
{
    // A freeform-breakpoint modulation source -- see EnvelopeParam's own comment in Identifiers.h
    // for how this differs from Adsr (fixed-length one-shot here, vs. Adsr's sustain-while-held).
    // Plays the drawn shape once per MIDI note-on over the Length knob's duration, linearly
    // interpolating between breakpoints, then holds at the final point's value regardless of how
    // long the note is held or when it's released -- note-off is deliberately ignored entirely.
    // Reports a unipolar 0-1 value (before the Depth knob scales it) via
    // RackModule::getCurrentModulationValue().
    //
    // Points are NOT APVTS parameters -- a variable-length, user-drawn point list can't be
    // represented as a fixed set of automatable scalars -- so they're plain module-owned state,
    // guarded by pointsLock since EnvelopeBreakpointEditor (message thread, via user clicks/drags)
    // and process() (audio thread, once per block) both touch them. Persisted across
    // save/reload via writeExtraState/readExtraState rather than the APVTS tree -- see
    // RackModule's own comment on why.
    //
    // The first and last points are permanently pinned to x=0 and x=1 (only their Y is
    // draggable, and they can't be deleted) -- the drawn curve always spans the full Length,
    // rather than allowing gaps of silence/held-value at either end that the user would have to
    // manage themselves.
    class EnvelopeModule : public RackModule
    {
    public:
        EnvelopeModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn);

        void prepare (const juce::dsp::ProcessSpec& spec) override;
        void reset() override;
        void process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix& modMatrix) override;

        bool isModulationSource() const override { return true; }
        float getCurrentModulationValue() const override { return currentValue.load(); }

        void writeExtraState (juce::XmlElement& parent) const override;
        void readExtraState (const juce::XmlElement& parent) override;

        // --- Editor-facing point access (message thread; internally locked) ---
        int getNumPoints() const;
        juce::Point<float> getPoint (int index) const;

        // Inserts a new interior point near (x, y) (x clamped into (0,1), away from existing
        // points by a small minimum gap so it can't land exactly on/past a neighbour) and returns
        // its index, or -1 if rejected (at capacity, or no room left between existing points).
        int addPoint (juce::Point<float> p);

        // x is ignored (forced to 0/1) for the first/last point; otherwise clamped strictly
        // between its immediate neighbours so the sorted point order never changes mid-drag. y is
        // always clamped to [0, 1] for every point, endpoints included.
        void movePoint (int index, juce::Point<float> newPos);

        // No-op if index is the first or last point (permanent) or out of range.
        void removePoint (int index);

        // Current normalized playback position [0, 1] (1 once finished/idle) and whether a
        // one-shot is actively playing -- for the editor's live playhead line.
        float getPlayheadPosition() const { return playheadTime.load(); }
        bool isPlaying() const { return playing.load(); }

    private:
        float evaluateAt (float normalisedTime) const; // pointsLock must already be held

        std::atomic<float>* lengthParam;
        std::atomic<float>* depthParam;

        mutable juce::CriticalSection pointsLock;
        std::array<juce::Point<float>, kMaxEnvelopePoints> points;
        int numPoints = 2;

        double sampleRate = 44100.0;
        std::atomic<float> playheadTime { 1.0f }; // 1.0 = idle/finished, matches "hold at final point"
        std::atomic<bool> playing { false };
        std::atomic<float> currentValue { 0.0f };

        static constexpr float minPointGap = 0.01f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeModule)
    };
}
