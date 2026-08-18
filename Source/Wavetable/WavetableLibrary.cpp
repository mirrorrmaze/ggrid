#include "WavetableLibrary.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <map>
#include <mutex>

namespace GGrid::WavetableLibrary
{
    namespace
    {
        constexpr int kKiloheartsFrameSize = 2048;
        constexpr int kKiloheartsFrameCount = 256;

        juce::File kiloheartsFactoryRoot()
        {
            return juce::File ("/Library/Application Support/Kilohearts/dependencies/factory_wavetables");
        }

        juce::File userRoot()
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile ("GGrid").getChildFile ("Wavetables");
        }

        juce::File resourcesRoot()
        {
            auto exeDir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
            for (int i = 0; i < 8 && exeDir.isDirectory(); ++i)
            {
                auto candidate = exeDir.getChildFile ("Resources").getChildFile ("Wavetables");
                if (candidate.isDirectory())
                    return candidate;
                exeDir = exeDir.getParentDirectory();
            }
            return {};
        }

        juce::File bundledKiloheartsRoot()
        {
            return resourcesRoot().getChildFile ("Kilohearts");
        }

        juce::File bundledGGridRoot()
        {
            return resourcesRoot().getChildFile ("GGrid");
        }

        void appendFilesFromRoot (std::vector<Entry>& entries, const juce::File& root, const juce::String& prefix)
        {
            if (! root.isDirectory())
                return;

            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            juce::Array<juce::File> files;
            root.findChildFiles (files, juce::File::findFiles, true);
            files.sort();

            for (auto& file : files)
            {
                if (formatManager.findFormatForFileExtension (file.getFileExtension()) == nullptr)
                    continue;

                const auto relative = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');
                const auto category = relative.containsChar ('/')
                    ? relative.upToLastOccurrenceOf ("/", false, false)
                    : prefix;

                entries.push_back ({ prefix + "/" + relative.upToLastOccurrenceOf (".", false, true),
                                     category,
                                     file });
            }
        }

        int inferFrameSize (int totalSamples)
        {
            if (totalSamples >= kKiloheartsFrameSize && totalSamples % kKiloheartsFrameSize == 0)
                return kKiloheartsFrameSize;

            if (totalSamples >= kKiloheartsFrameCount && totalSamples % kKiloheartsFrameCount == 0)
                return totalSamples / kKiloheartsFrameCount;

            return juce::jmax (1, totalSamples);
        }

        std::shared_ptr<Table> makeFallbackTable()
        {
            auto table = std::make_shared<Table>();
            table->displayName = "Built-in/Sine";
            table->frameSize = kKiloheartsFrameSize;
            table->numFrames = 1;
            table->samples.resize ((size_t) table->frameSize);
            for (int i = 0; i < table->frameSize; ++i)
            {
                const float phase = (float) i / (float) table->frameSize;
                table->samples[(size_t) i] = std::sin (phase * juce::MathConstants<float>::twoPi);
            }
            return table;
        }

        float rawSample (const Table& table, int frame, float phase01)
        {
            const int frameSize = table.frameSize;
            const int clampedFrame = juce::jlimit (0, table.numFrames - 1, frame);
            const float wrappedPhase = phase01 - std::floor (phase01);
            const float pos = wrappedPhase * (float) frameSize;
            const int i0 = ((int) std::floor (pos)) % frameSize;
            const int i1 = (i0 + 1) % frameSize;
            const float frac = pos - std::floor (pos);
            const auto base = (size_t) clampedFrame * (size_t) frameSize;
            return juce::jmap (frac, table.samples[base + (size_t) i0], table.samples[base + (size_t) i1]);
        }
    }

    float Table::sample (float framePosition, float phase01, float smooth01) const
    {
        if (! isValid())
            return 0.0f;

        const float clampedFrame = juce::jlimit (0.0f, (float) numFrames - 1.0f, framePosition);
        const int f0 = (int) std::floor (clampedFrame);
        const int f1 = juce::jmin (numFrames - 1, f0 + 1);
        const float frameFrac = clampedFrame - (float) f0;

        const float smooth = juce::jlimit (0.0f, 1.0f, smooth01);
        const int radius = juce::roundToInt (smooth * 32.0f);

        float total = 0.0f;
        int count = 0;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            const float shiftedPhase = phase01 + (float) offset / (float) frameSize;
            const float a = rawSample (*this, f0, shiftedPhase);
            const float b = rawSample (*this, f1, shiftedPhase);
            total += juce::jmap (frameFrac, a, b);
            ++count;
        }

        // Kilohearts' Smooth visibly flattens the table as it rises; combine a small moving
        // average with amplitude attenuation so extreme Smooth approaches a calm line.
        return (total / (float) juce::jmax (1, count)) * (1.0f - 0.75f * smooth);
    }

    juce::File resolveFactoryRoot()
    {
        auto kh = kiloheartsFactoryRoot();
        if (kh.isDirectory())
            return kh;

        auto bundledKh = bundledKiloheartsRoot();
        if (bundledKh.isDirectory())
            return bundledKh;

        auto bundledGGrid = bundledGGridRoot();
        if (bundledGGrid.isDirectory())
            return bundledGGrid;

        auto user = userRoot();
        if (user.isDirectory())
            return user;

        return {};
    }

    const std::vector<Entry>& getCatalog()
    {
        static const std::vector<Entry> catalog = []
        {
            std::vector<Entry> entries;
            const auto kh = kiloheartsFactoryRoot();
            if (kh.isDirectory())
                appendFilesFromRoot (entries, kh, "Kilohearts");
            else
                appendFilesFromRoot (entries, bundledKiloheartsRoot(), "Kilohearts");

            appendFilesFromRoot (entries, bundledGGridRoot(), "GGrid");
            appendFilesFromRoot (entries, userRoot(), "Custom");

            if (entries.empty())
                entries.push_back ({ "Built-in/Sine", "Built-in", {} });

            return entries;
        }();

        return catalog;
    }

    juce::StringArray getCatalogDisplayNames()
    {
        juce::StringArray names;
        for (const auto& entry : getCatalog())
            names.add (entry.displayName);
        return names;
    }

    std::shared_ptr<const Table> loadTable (int index)
    {
        const auto& catalog = getCatalog();
        if (catalog.empty())
            return makeFallbackTable();

        const int clampedIndex = juce::jlimit (0, (int) catalog.size() - 1, index);

        static std::mutex mutex;
        static std::map<int, std::weak_ptr<const Table>> cache;

        {
            std::lock_guard<std::mutex> lock (mutex);
            if (auto cached = cache[clampedIndex].lock())
                return cached;
        }

        const auto& entry = catalog[(size_t) clampedIndex];
        if (! entry.file.existsAsFile())
            return makeFallbackTable();

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (entry.file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return makeFallbackTable();

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        if (! reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true))
            return makeFallbackTable();

        auto table = std::make_shared<Table>();
        table->displayName = entry.displayName;
        table->frameSize = inferFrameSize ((int) reader->lengthInSamples);
        table->numFrames = juce::jmax (1, (int) reader->lengthInSamples / table->frameSize);
        table->samples.resize ((size_t) table->frameSize * (size_t) table->numFrames);

        const float scale = 1.0f / (float) juce::jmax (1, buffer.getNumChannels());
        for (int i = 0; i < table->frameSize * table->numFrames; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                mono += buffer.getSample (ch, i);
            table->samples[(size_t) i] = juce::jlimit (-1.0f, 1.0f, mono * scale);
        }

        std::lock_guard<std::mutex> lock (mutex);
        cache[clampedIndex] = table;
        return table;
    }
}
