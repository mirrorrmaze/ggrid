#include "UpdateChecker.h"
#include <juce_events/juce_events.h>

namespace GGrid
{
    namespace
    {
        // Minimal, dependency-free semver-ish comparison for this project's own straightforward
        // X.Y.Z tags ("0.2.0", optionally "v0.2.0") -- not a general-purpose semver parser.
        std::array<int, 3> parseVersion (juce::String v)
        {
            if (v.startsWithChar ('v') || v.startsWithChar ('V'))
                v = v.substring (1);

            std::array<int, 3> parts { 0, 0, 0 };
            auto tokens = juce::StringArray::fromTokens (v, ".", "");
            for (int i = 0; i < 3 && i < tokens.size(); ++i)
                parts[(size_t) i] = tokens[i].getIntValue();
            return parts;
        }

        bool isNewer (const juce::String& latest, const juce::String& current)
        {
            return parseVersion (latest) > parseVersion (current); // std::array has lexicographic operator>
        }
    }

    UpdateChecker::UpdateChecker() : juce::Thread ("Update Check")
    {
    }

    UpdateChecker::~UpdateChecker()
    {
        stopThread (2000);
    }

    void UpdateChecker::checkAsync (juce::String currentVersion,
                                     std::function<void (juce::String, juce::URL)> onUpdateAvailable)
    {
        currentVersionToCompare = std::move (currentVersion);
        callback = std::move (onUpdateAvailable);
        startThread (juce::Thread::Priority::background);
    }

    void UpdateChecker::run()
    {
        const juce::URL apiUrl ("https://api.github.com/repos/mirrorrmaze/ggrid/releases/latest");

        // GitHub's API requires a User-Agent header on every request or it responds 403.
        const auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                  .withConnectionTimeoutMs (4000)
                                  .withExtraHeaders ("User-Agent: GGrid-UpdateCheck");

        auto stream = apiUrl.createInputStream (options);
        if (stream == nullptr || threadShouldExit())
            return;

        const auto json = stream->readEntireStreamAsString();
        if (threadShouldExit() || json.isEmpty())
            return;

        auto parsed = juce::JSON::parse (json);
        if (! parsed.isObject())
            return;

        const auto tagName = parsed.getProperty ("tag_name", {}).toString();
        const auto htmlUrl = parsed.getProperty ("html_url", {}).toString();
        if (tagName.isEmpty() || ! isNewer (tagName, currentVersionToCompare))
            return;

        if (callback == nullptr)
            return;

        auto cb = callback;
        const juce::URL releaseUrl (htmlUrl.isNotEmpty()
                                         ? htmlUrl
                                         : "https://github.com/mirrorrmaze/ggrid/releases/latest");

        juce::MessageManager::callAsync ([cb, tagName, releaseUrl] { cb (tagName, releaseUrl); });
    }
}
