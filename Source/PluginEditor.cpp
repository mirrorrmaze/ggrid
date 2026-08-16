#include "PluginEditor.h"

namespace GGrid
{
    GGridAudioProcessorEditor::GGridAudioProcessorEditor (GGridAudioProcessor& p)
        : AudioProcessorEditor (&p), processorRef (p), modMatrixPanel (p.apvts),
          nodeGraphEditor (p)
    {
        setLookAndFeel (&lookAndFeel);

        addAndMakeVisible (processorRef.outputScope);

        menuButton.onClick = [this] { showHeaderMenu(); };
        addAndMakeVisible (menuButton);

        rackTabButton.setClickingTogglesState (true);
        modMatrixTabButton.setClickingTogglesState (true);
        rackTabButton.setRadioGroupId (1);
        modMatrixTabButton.setRadioGroupId (1);
        rackTabButton.setToggleState (true, juce::dontSendNotification);
        rackTabButton.onClick = [this] { setActiveTab (false); };
        modMatrixTabButton.onClick = [this] { setActiveTab (true); };
        addAndMakeVisible (rackTabButton);
        addAndMakeVisible (modMatrixTabButton);

        addAndMakeVisible (modMatrixPanel);
        modMatrixPanel.setVisible (false); // Rack tab is active by default

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
            safeThis->menuButton.setColour (juce::TextButton::textColourOffId, Palette::accent);
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

    void GGridAudioProcessorEditor::showHeaderMenu()
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

        menu.addItem ("Save Patch...", [this] { savePatch(); });
        menu.addItem ("Load Patch...", [this] { loadPatch(); });
        menu.addSeparator();

        menu.addItem ("GGrid v" + juce::String (GGRID_VERSION), false, false, nullptr);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
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

    void GGridAudioProcessorEditor::setActiveTab (bool showModMatrix)
    {
        modMatrixPanel.setVisible (showModMatrix);

        nodeCanvasViewport.setVisible (! showModMatrix);
        zoomOutButton.setVisible (! showModMatrix);
        zoomInButton.setVisible (! showModMatrix);
        zoomResetButton.setVisible (! showModMatrix);
        zoomLabel.setVisible (! showModMatrix);
    }

    void GGridAudioProcessorEditor::resized()
    {
        // Slim header row: tab buttons on the left, live output scope filling the rest. The
        // safety limiter used to anchor this row (see MasterControlsPanel) but now lives at the
        // bottom of the Mod Matrix tab, so the header only needs to be tall enough for the tabs
        // + a usable scope strip -- everything below it goes to whichever page is active.
        constexpr int tabButtonHeight = 24;
        const int tabButtonTop = margin + (headerHeight - tabButtonHeight) / 2;
        rackTabButton.setBounds (margin, tabButtonTop, 80, tabButtonHeight);
        modMatrixTabButton.setBounds (margin + 80 + 4, tabButtonTop, 110, tabButtonHeight);

        constexpr int menuButtonWidth = 28;
        menuButton.setBounds (getWidth() - margin - menuButtonWidth, tabButtonTop, menuButtonWidth, tabButtonHeight);

        const int scopeLeft = margin + 80 + 4 + 110 + gap;
        const int scopeRight = getWidth() - margin - menuButtonWidth - gap;
        processorRef.outputScope.setBounds (scopeLeft, margin, scopeRight - scopeLeft, headerHeight);

        const int contentTop = margin + headerHeight + margin;
        const int contentHeight = getHeight() - contentTop;

        nodeCanvasViewport.setBounds (0, contentTop, getWidth(), contentHeight);
        modMatrixPanel.setBounds (margin, contentTop, getWidth() - margin * 2, contentHeight);

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
