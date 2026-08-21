#include "PluginEditor.h"

namespace GGrid
{
    // Where "Skip This Version" persists so the update popup doesn't nag on every launch once
    // dismissed for a specific version -- a plain one-line text file (matching the project's
    // general preference for simple flat storage over heavier machinery like PropertiesFile),
    // holding just the last skipped version tag. A newer release than the skipped one still
    // prompts -- this only silences the exact version the user already said no to.
    static juce::File getSkippedUpdateVersionFile()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GGrid");
        dir.createDirectory();
        return dir.getChildFile ("skipped_update_version.txt");
    }

    static juce::String getSkippedUpdateVersion()
    {
        return getSkippedUpdateVersionFile().loadFileAsString().trim();
    }

    static void setSkippedUpdateVersion (const juce::String& version)
    {
        getSkippedUpdateVersionFile().replaceWithText (version);
    }

    GGridAudioProcessorEditor::GGridAudioProcessorEditor (GGridAudioProcessor& p)
        : AudioProcessorEditor (&p), processorRef (p),
          nodeGraphEditor (p)
    {
        setLookAndFeel (&lookAndFeel);
        setWantsKeyboardFocus (true);

        addAndMakeVisible (processorRef.outputScope);

        fileMenuButton.onClick = [this] { showFileMenu(); };
        editMenuButton.onClick = [this] { showEditMenu(); };
        addAndMakeVisible (fileMenuButton);
        addAndMakeVisible (editMenuButton);

        nodeCanvasViewport.setViewedComponent (&nodeGraphEditor, false);
        // Scrollbars hidden -- panning is click-drag on blank canvas (see
        // NodeGraphEditor::mouseDrag) or two-finger/wheel scroll (see
        // NodeGraphEditor::mouseWheelMove), which reads as an infinite/boundless sandbox rather
        // than a bounded scrollable area. The last two `true`s keep wheel-scroll panning working
        // despite the scrollbars being hidden -- normally Viewport only honours the wheel when a
        // scrollbar is actually visible.
        nodeCanvasViewport.setScrollBarsShown (false, false, true, true);
        addAndMakeVisible (nodeCanvasViewport);

        nodeGraphEditor.setOwnerViewport (&nodeCanvasViewport);
        nodeGraphEditor.onZoomChanged = [this] (float zoom) { updateZoomLabel (zoom); };

        zoomOutButton.onClick = [this] { nodeGraphEditor.setZoom (nodeGraphEditor.getZoom() - 0.1f); };
        zoomInButton.onClick = [this] { nodeGraphEditor.setZoom (nodeGraphEditor.getZoom() + 0.1f); };
        zoomResetButton.onClick = [this] { nodeGraphEditor.setZoom (1.0f); };
        zoomLabel.setJustificationType (juce::Justification::centred);
        updateZoomLabel (nodeGraphEditor.getZoom());

        addAndMakeVisible (zoomOutButton);
        addAndMakeVisible (zoomInButton);
        addAndMakeVisible (zoomResetButton);
        addAndMakeVisible (zoomLabel);

        setResizable (true, true);
        setResizeLimits (420, 320, NodeGraphEditor::canvasWidth, NodeGraphEditor::canvasHeight);

        setSize (900, 700);

        resized();

        juce::Component::SafePointer<GGridAudioProcessorEditor> safeThis (this);
        updateChecker.checkAsync (GGRID_VERSION, [safeThis] (juce::String newVersion, juce::URL releaseUrl)
        {
            if (safeThis == nullptr)
                return;
            safeThis->availableUpdateVersion = newVersion;
            safeThis->availableUpdateUrl = releaseUrl;
            safeThis->fileMenuButton.setColour (juce::TextButton::textColourOffId, Palette::accent);

            // The passive indicator above always lights up; the popup below only interrupts once
            // per version -- "Skip This Version" persists so it won't ask again for this exact
            // release (a later one will still prompt), and just closing/escaping the dialog (or
            // "Not Now"-equivalent) asks again next launch rather than being remembered as a skip.
            if (newVersion == getSkippedUpdateVersion())
                return;

            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::InfoIcon)
                    .withTitle ("Update Available")
                    .withMessage ("GGrid " + newVersion + " is available -- you're running v"
                                      + juce::String (GGRID_VERSION) + ".")
                    .withButton ("Download")
                    .withButton ("Skip This Version"),
                [safeThis, newVersion, releaseUrl] (int result)
                {
                    if (safeThis == nullptr)
                        return;
                    if (result == 1)
                        releaseUrl.launchInDefaultBrowser();
                    else if (result == 2)
                        setSkippedUpdateVersion (newVersion);
                });
        });
    }

    GGridAudioProcessorEditor::~GGridAudioProcessorEditor()
    {
        setLookAndFeel (nullptr);
    }

    void GGridAudioProcessorEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::bg);
    }

    void GGridAudioProcessorEditor::updateZoomLabel (float zoom)
    {
        zoomLabel.setText (juce::String (juce::roundToInt (zoom * 100.0f)) + "%", juce::dontSendNotification);
    }

    void GGridAudioProcessorEditor::showFileMenu()
    {
        juce::PopupMenu menu;

        if (availableUpdateVersion.isNotEmpty())
        {
            menu.addItem ("Update available: " + availableUpdateVersion, [this]
            {
                availableUpdateUrl.launchInDefaultBrowser();
            });
            menu.addSeparator();
        }

        menu.addItem ("Init Patch", [this] { nodeGraphEditor.initPatch(); });
        menu.addSeparator();
        menu.addItem ("Save Patch...", [this] { savePatch(); });
        menu.addItem ("Load Patch...", [this] { loadPatch(); });
        menu.addSeparator();

        menu.addItem ("GGrid v" + juce::String (GGRID_VERSION), false, false, nullptr);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (fileMenuButton));
    }

    void GGridAudioProcessorEditor::showEditMenu()
    {
        juce::PopupMenu menu;

        const bool hasSelection = nodeGraphEditor.hasSelection();
        menu.addItem ("Copy (Cmd+C)", hasSelection, false, [this] { nodeGraphEditor.copySelection(); });
        menu.addItem ("Paste (Cmd+V)", nodeGraphEditor.hasClipboardContent(), false, [this] { nodeGraphEditor.pasteClipboard(); });
        menu.addItem ("Duplicate (Cmd+D)", hasSelection, false, [this] { nodeGraphEditor.duplicateSelection(); });
        menu.addSeparator();
        menu.addItem ("Delete (Del)", hasSelection, false, [this] { nodeGraphEditor.deleteSelectedNodes(); });

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (editMenuButton));
    }

    bool GGridAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
    {
        return nodeGraphEditor.keyPressed (key);
    }

    static juce::File getPatchesDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                       .getChildFile ("GGrid").getChildFile ("Patches");
        dir.createDirectory();
        return dir;
    }

    void GGridAudioProcessorEditor::savePatch()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Save GGrid Patch", getPatchesDirectory(), "*.ggridpatch");

        fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File {})
                    return;

                if (! file.hasFileExtension (".ggridpatch"))
                    file = file.withFileExtension (".ggridpatch");

                juce::MemoryBlock data;
                processorRef.getStateInformation (data);
                file.replaceWithData (data.getData(), data.getSize());
            });
    }

    void GGridAudioProcessorEditor::loadPatch()
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load GGrid Patch", getPatchesDirectory(), "*.ggridpatch");

        fileChooser->launchAsync (juce::FileBrowserComponent::openMode,
            [this] (const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file == juce::File {})
                    return;

                juce::MemoryBlock data;
                if (file.loadFileAsData (data))
                    // NodeGraphEditor picks up the new connections/node positions/param values
                    // on its next timer tick (see its class comment on why reconciliation is
                    // poll-based) -- no explicit refresh call needed here.
                    processorRef.setStateInformation (data.getData(), (int) data.getSize());
            });
    }

    void GGridAudioProcessorEditor::resized()
    {
        // Slim header row: File/Edit menus in the top-left corner (where the Rack/Mod Matrix tab
        // buttons used to live), live output scope filling the rest of the space to their right.
        constexpr int menuButtonHeight = 24;
        const int menuButtonTop = margin + (headerHeight - menuButtonHeight) / 2;

        constexpr int menuButtonWidth = 40, menuButtonGap = 2;
        fileMenuButton.setBounds (margin, menuButtonTop, menuButtonWidth, menuButtonHeight);
        editMenuButton.setBounds (fileMenuButton.getRight() + menuButtonGap, menuButtonTop, menuButtonWidth, menuButtonHeight);

        const int scopeLeft = editMenuButton.getRight() + gap;
        const int scopeRight = getWidth() - margin;
        processorRef.outputScope.setBounds (scopeLeft, margin, scopeRight - scopeLeft, headerHeight);

        const int contentTop = margin + headerHeight + margin;
        const int contentHeight = getHeight() - contentTop;

        nodeCanvasViewport.setBounds (0, contentTop, getWidth(), contentHeight);

        // Zoom controls float in the canvas area's top-right corner, added after the viewport so
        // they're always in front of it and never scroll away with the canvas content.
        constexpr int buttonSize = 24, labelWidth = 44, zoomStripGap = 4;
        int right = getWidth() - margin;
        zoomResetButton.setBounds (right - 56, contentTop + gap, 56, buttonSize);
        right -= 56 + zoomStripGap;
        zoomInButton.setBounds (right - buttonSize, contentTop + gap, buttonSize, buttonSize);
        right -= buttonSize + zoomStripGap;
        zoomLabel.setBounds (right - labelWidth, contentTop + gap, labelWidth, buttonSize);
        right -= labelWidth + zoomStripGap;
        zoomOutButton.setBounds (right - buttonSize, contentTop + gap, buttonSize, buttonSize);
    }
}
