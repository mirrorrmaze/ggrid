#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../Params/Identifiers.h"

namespace GGrid
{
    // Right-click-to-add-a-module popup: a Spotlight/Start-menu-style search box with the
    // matching module list right underneath it. An empty search shows every addable module
    // grouped under its category header (the same grouping the old submenu-based right-click
    // menu used); typing narrows the list to whatever matches, case-insensitively, against
    // either the module's own name or its category. Up/Down arrow keys move the highlight,
    // Enter adds whichever module is currently highlighted, and clicking a row adds that one
    // directly -- Escape or a click outside the popup (CallOutBox's own built-in behaviour)
    // dismisses it without adding anything. See NodeGraphEditor::showAddNodeMenu for how this
    // gets launched in a CallOutBox anchored to the right-click position.
    class AddModuleSearchPopup : public juce::Component
    {
    public:
        AddModuleSearchPopup();

        // Fired once the user commits a choice (Enter or a row click). The caller owns dismissing
        // the CallOutBox this component lives in -- see setDismissCallback.
        std::function<void (ModuleType)> onModuleChosen;

        // Called on Escape, so the caller can dismiss its CallOutBox the same way a click-outside
        // would (CallOutBox doesn't intercept Escape on our behalf since keyboard focus lives on
        // our child TextEditor, not the CallOutBox itself).
        std::function<void()> onCancelled;

        void resized() override;
        void visibilityChanged() override;

        static constexpr int popupWidth = 280;

    private:
        struct Entry { ModuleType type; juce::String name; juce::String category; };
        static const std::vector<Entry>& allEntries();

        struct Row { bool isHeader = false; juce::String text; ModuleType type = ModuleType::none; };

        void rebuildRows();
        void moveSelection (int delta);
        void commitSelection();

        class SearchEditor : public juce::TextEditor
        {
        public:
            std::function<void (int)> onArrowKey; // +1/-1
            std::function<void()> onReturnKey;
            std::function<void()> onEscapeKey;
            bool keyPressed (const juce::KeyPress& key) override;
        };

        class ResultsList : public juce::Component
        {
        public:
            std::function<void (int)> onRowClicked;
            // Fired as the mouse moves over a non-header row -- lets the highlight track the
            // cursor the same way it tracks the arrow keys, matching ordinary menu behaviour.
            std::function<void (int)> onRowHovered;
            void setRows (const std::vector<Row>* rowsIn);
            void setSelectedIndex (int index);
            void paint (juce::Graphics&) override;
            void mouseDown (const juce::MouseEvent&) override;
            void mouseMove (const juce::MouseEvent&) override;
            int getContentHeight() const;

            static constexpr int rowHeight = 22;

        private:
            const std::vector<Row>* rows = nullptr;
            int selectedIndex = -1;
        };

        SearchEditor searchEditor;
        juce::Viewport viewport;
        ResultsList resultsList;

        std::vector<Row> rows;
        int selectedIndex = -1;

        static constexpr int maxListHeight = 320;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AddModuleSearchPopup)
    };
}
