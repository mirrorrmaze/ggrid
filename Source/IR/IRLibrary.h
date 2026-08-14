#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Ported from MultibandConvolver (D:\Claude Projects\MultibandConvolver\Source\DSP\IRLibrary.h)
// -- same 95-entry factory catalog and Custom-folder convention, just renamed to GGrid's own
// product folder. See CREDITS.md alongside the IR files for licensing/sourcing.
namespace GGrid::IRLibrary
{
    struct Entry
    {
        juce::String displayName;   // shown in the IR picker
        juce::String category;      // Residential / Commercial / Public / Historical / Outdoors / Textures
        juce::String relativePath;  // relative to the resolved IR root, e.g. "Residential/Arroyo House Living Room.wav"
    };

    // The full picker list: the curated factory library (indices 0..factoryCatalogSize-1, fixed
    // and load-bearing for saved presets/automation -- see below) followed by any user-added
    // files found under the IR root's Custom/ subfolder at the time this is first called (see
    // getCustomIRDirectory()). Custom entries are appended in alphabetical order, so their
    // indices stay consistent across sessions *as long as the custom file set itself doesn't
    // change* -- unlike the factory entries, there's no permanent identity behind them (they're
    // just "whatever's in the folder right now"), so adding/removing custom files between
    // sessions can shift which custom IR a given index points to. Scanned once and cached for
    // the process's lifetime; restart the plugin/DAW to pick up files added after that.
    const std::vector<Entry>& getCatalog();

    // Convenience for building the irIndex AudioParameterChoice's fixed choice list.
    juce::StringArray getCatalogDisplayNames();

    // The number of factory entries in getCatalog() (i.e. getCatalog().size() before any custom
    // entries are appended) -- 95 MIT-licensed IRs cherry-picked from itsmusician/IR-Library (see
    // Resources/IRs/CREDITS.md). This range's order/indices must never change after shipping a
    // version with saved presets/automation; append new factory entries at the end instead.
    constexpr int factoryCatalogSize = 95;

    // Resolves the root folder that contains the category subfolders (Residential/, Commercial/,
    // etc.) and the Custom/ subfolder. Searches, in order: next to the running plugin/standalone
    // binary, then the source tree's Resources/IRs (dev-build fallback), then a user Documents
    // folder (populated by the installer). Returns an invalid File if none of those exist.
    juce::File resolveIRRoot();

    // Where users can drop their own IR files to have them show up in the picker alongside the
    // factory library (see getCatalog()'s comment). Creates the folder if the IR root resolved
    // successfully and it doesn't exist yet.
    juce::File getCustomIRDirectory();

    // Loads entry `index`'s audio into `outBuffer` at `targetSampleRate` (resampled if needed).
    // Returns false (and leaves outBuffer empty) if the file can't be found or read.
    bool loadEntry (int index, double targetSampleRate, juce::AudioBuffer<float>& outBuffer);
}
