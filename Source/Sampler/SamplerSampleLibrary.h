#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

namespace GGrid::SamplerSampleLibrary
{
    struct LoadedSample
    {
        juce::AudioBuffer<float> buffer; // as stored on disk (mono or stereo), unresampled
        double sampleRate = 44100.0;
    };

    // Loads (and caches, keyed by absolute path) an audio file into memory. Returns nullptr if
    // the file doesn't exist or isn't a format juce::AudioFormatManager recognises. Called from
    // the message thread only (SamplerControlsPanel, when a file is dropped or Hot-Swapped) --
    // SamplerModule's audio-thread side only ever reads the shared_ptr this hands back, already
    // resolved by the time a note-on can reference it (see SamplerModule::addZoneFromFile), so
    // the audio thread never touches disk. Same caching approach as WavetableLibrary::loadTable,
    // just keyed by an arbitrary file path instead of a catalog index, since Sampler zones
    // reference files dropped in from anywhere, not a bundled set.
    std::shared_ptr<const LoadedSample> loadFromFile (const juce::File& file);
}
