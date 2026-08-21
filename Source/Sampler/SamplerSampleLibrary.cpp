#include "SamplerSampleLibrary.h"
#include <map>
#include <mutex>

namespace GGrid::SamplerSampleLibrary
{
    std::shared_ptr<const LoadedSample> loadFromFile (const juce::File& file)
    {
        static std::mutex mutex;
        static std::map<juce::String, std::weak_ptr<const LoadedSample>> cache;

        const auto key = file.getFullPathName();

        {
            std::lock_guard<std::mutex> lock (mutex);
            if (auto cached = cache[key].lock())
                return cached;
        }

        if (! file.existsAsFile())
            return nullptr;

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        auto sample = std::make_shared<LoadedSample>();
        sample->sampleRate = reader->sampleRate > 0.0 ? reader->sampleRate : 44100.0;
        sample->buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
        if (! reader->read (&sample->buffer, 0, (int) reader->lengthInSamples, 0, true, true))
            return nullptr;

        std::lock_guard<std::mutex> lock (mutex);
        cache[key] = sample;
        return sample;
    }
}
