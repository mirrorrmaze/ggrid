#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <vector>

namespace GGrid::WavetableLibrary
{
    struct Entry
    {
        juce::String displayName;
        juce::String category;
        juce::File file;
    };

    struct Table
    {
        juce::String displayName;
        int frameSize = 2048;
        int numFrames = 1;
        std::vector<float> samples;

        bool isValid() const { return frameSize > 0 && numFrames > 0 && (int) samples.size() >= frameSize * numFrames; }
        float sample (float framePosition, float phase01, float smooth01) const;
    };

    const std::vector<Entry>& getCatalog();
    juce::StringArray getCatalogDisplayNames();
    juce::File resolveFactoryRoot();
    std::shared_ptr<const Table> loadTable (int index);
}
