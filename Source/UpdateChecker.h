#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <functional>

// Checks GitHub's Releases API for a version newer than the one this build was compiled with,
// entirely in the background (one GET request + a tiny JSON parse, off the message thread) and
// reports back via a callback on the message thread only if a genuinely newer version is found.
//
// Deliberately minimal and fail-silent: no network, GitHub down, rate-limited, or a malformed
// response all just mean no callback ever fires -- this is a nice-to-have notice, not something
// that should ever block startup, retry aggressively, or show an error to the user.
namespace GGrid
{
    class UpdateChecker : private juce::Thread
    {
    public:
        UpdateChecker();
        ~UpdateChecker() override;

        // One-shot; safe to call once per app/plugin instance launch. onUpdateAvailable is invoked
        // on the message thread with the newer version's tag (e.g. "v0.2.0") and its GitHub release
        // page URL, only when that version is actually newer than currentVersion (e.g. "0.1.0").
        void checkAsync (juce::String currentVersion,
                          std::function<void (juce::String newVersion, juce::URL releaseUrl)> onUpdateAvailable);

    private:
        void run() override;

        juce::String currentVersionToCompare;
        std::function<void (juce::String, juce::URL)> callback;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UpdateChecker)
    };
}
