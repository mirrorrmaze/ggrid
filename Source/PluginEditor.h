#pragma once

#include "PluginProcessor.h"
#include "GUI/NodeGraphEditor.h"
#include "GUI/ModMatrixPanel.h"
#include "GUI/GGridLookAndFeel.h"
#include "UpdateChecker.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace GGrid
{
    // A slim header strip (tab buttons + live output scope) stays fixed at the top; below it,
    // the tab buttons switch the content area between the node-based patch-bay canvas (Rack) and
    // the MIDI mod matrix. The safety limiter used to live in its own box in this header, but
    // it's an on-and-forget setting for most users, so it now lives at the bottom of the Mod
    // Matrix tab instead (see ModMatrixPanel) -- freeing the header down to just the tabs + scope,
    // and freeing the canvas from the space the old master box used to take. Styled to match
    // SPANDEX (D:\Claude Projects\RepitchDeck) via GGridLookAndFeel -- flat/hairline/two-tone, no
    // gradients or rounded corners.
    class GGridAudioProcessorEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit GGridAudioProcessorEditor (GGridAudioProcessor&);
        ~GGridAudioProcessorEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void setActiveTab (bool showModMatrix);
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

        juce::TextButton rackTabButton { "Rack" }, modMatrixTabButton { "Mod Matrix" };
        ModMatrixPanel modMatrixPanel;

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
