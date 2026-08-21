#pragma once

#include "PluginProcessor.h"
#include "GUI/NodeGraphEditor.h"
#include "GUI/GGridLookAndFeel.h"
#include "UpdateChecker.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace GGrid
{
    // A slim header strip (File/Edit menus + live output scope) stays fixed at the top; below it,
    // the node-based patch-bay canvas fills the rest of the window -- there's no other page/tab
    // to switch to (the old MIDI Mod Matrix tab and its fixed 6-route system, plus the master
    // safety limiter it had ended up hosting, are gone; limiting is now just the Limiter module,
    // a real rack module you place like any other alongside Compressor -- see LimiterModule/
    // CompressorModule). Styled to match SPANDEX (D:\Claude Projects\RepitchDeck) via
    // GGridLookAndFeel -- flat/hairline/two-tone, no gradients or rounded corners.
    class GGridAudioProcessorEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit GGridAudioProcessorEditor (GGridAudioProcessor&);
        ~GGridAudioProcessorEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void updateZoomLabel (float zoom);
        void showFileMenu();
        void showEditMenu();

        // Save/load the full plugin state (every module's params, the connection graph, node
        // positions) to/from a file on disk -- independent of whatever preset mechanism the host
        // provides (Standalone mode has none at all), and works identically in both since both
        // just call GGridAudioProcessor's existing get/setStateInformation, the same pair a host
        // already calls for its own preset save/recall. fileChooser is a member (not a local) so
        // it stays alive across the async dialog's callback -- see FileChooser::launchAsync's own
        // docs for why a locally-scoped one would be destroyed before the user finishes picking.
        void savePatch();
        void loadPatch();
        std::unique_ptr<juce::FileChooser> fileChooser;

        // Declared first so it's constructed before, and destroyed after, every other member --
        // components must never outlive the LookAndFeel they're pointing at.
        GGridLookAndFeel lookAndFeel;

        GGridAudioProcessor& processorRef;

        // outputScope itself lives on the processor (see PluginProcessor.h) so it keeps
        // rendering correctly across editor open/close; this editor just parents it.

        // Checked once on launch (background thread, fail-silent -- see UpdateChecker); if a
        // newer GitHub release is found, menuButton's text recolors to accent and the header
        // menu gets an "Update available" item at the top that opens the release page.
        UpdateChecker updateChecker;
        juce::String availableUpdateVersion;
        juce::URL availableUpdateUrl;

        // File (Init Patch/Save/Load/version) and Edit (Copy/Paste/Duplicate/Delete) -- a
        // conventional menu bar rather than the single "..." catch-all button this used to be,
        // now that there's enough here (Init Patch, copy/paste) to warrant it. Edit menu item
        // enabled-state reflects NodeGraphEditor::hasSelection()/hasClipboardContent() each time
        // it opens, same as any other program's Edit menu.
        juce::TextButton fileMenuButton { "File" }, editMenuButton { "Edit" };

        juce::Viewport nodeCanvasViewport;
        NodeGraphEditor nodeGraphEditor;

        // Floating overlay in the corner of the canvas area (siblings added after the viewport,
        // so they sit in front of it and never scroll away with the content).
        juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, zoomResetButton { "Reset" };
        juce::Label zoomLabel;

        static constexpr int margin = 10;
        static constexpr int gap = 10;
        static constexpr int headerHeight = 40;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GGridAudioProcessorEditor)
    };
}
