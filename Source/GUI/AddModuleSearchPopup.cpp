#include "AddModuleSearchPopup.h"
#include "GGridLookAndFeel.h"

namespace GGrid
{
    namespace
    {
        constexpr int searchHeight = 30;
    }

    bool AddModuleSearchPopup::SearchEditor::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::upKey)      { if (onArrowKey) onArrowKey (-1); return true; }
        if (key == juce::KeyPress::downKey)    { if (onArrowKey) onArrowKey (1);  return true; }
        if (key == juce::KeyPress::returnKey)  { if (onReturnKey) onReturnKey();  return true; }
        if (key == juce::KeyPress::escapeKey)  { if (onEscapeKey) onEscapeKey();  return true; }

        return juce::TextEditor::keyPressed (key);
    }

    void AddModuleSearchPopup::ResultsList::setRows (const std::vector<Row>* rowsIn)
    {
        rows = rowsIn;
        setSize (getWidth(), getContentHeight());
        repaint();
    }

    void AddModuleSearchPopup::ResultsList::setSelectedIndex (int index)
    {
        selectedIndex = index;
        repaint();
    }

    int AddModuleSearchPopup::ResultsList::getContentHeight() const
    {
        return rows == nullptr ? 0 : (int) rows->size() * rowHeight;
    }

    void AddModuleSearchPopup::ResultsList::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::bg);

        if (rows == nullptr)
            return;

        for (int i = 0; i < (int) rows->size(); ++i)
        {
            const auto& row = (*rows)[(size_t) i];
            const auto bounds = juce::Rectangle<int> (0, i * rowHeight, getWidth(), rowHeight);

            if (row.isHeader)
            {
                g.setColour (Palette::dimmer);
                g.fillRect (bounds);
                g.setColour (Palette::dim);
                g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
                g.drawText (row.text, bounds.reduced (8, 0), juce::Justification::centredLeft);
                continue;
            }

            if (i == selectedIndex)
            {
                g.setColour (Palette::accent);
                g.fillRect (bounds);
            }

            g.setColour (i == selectedIndex ? Palette::bright : Palette::dim);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (row.text, bounds.reduced (12, 0), juce::Justification::centredLeft);
        }
    }

    void AddModuleSearchPopup::ResultsList::mouseDown (const juce::MouseEvent& e)
    {
        if (rows == nullptr)
            return;

        const int index = e.y / rowHeight;
        if (index < 0 || index >= (int) rows->size() || (*rows)[(size_t) index].isHeader)
            return;

        if (onRowClicked)
            onRowClicked (index);
    }

    void AddModuleSearchPopup::ResultsList::mouseMove (const juce::MouseEvent& e)
    {
        if (rows == nullptr)
            return;

        const int index = e.y / rowHeight;
        if (index < 0 || index >= (int) rows->size() || (*rows)[(size_t) index].isHeader)
            return;

        if (index != selectedIndex && onRowHovered)
            onRowHovered (index);
    }

    const std::vector<AddModuleSearchPopup::Entry>& AddModuleSearchPopup::allEntries()
    {
        // Same module list and grouping the old right-click submenus used -- kept in this one
        // place now instead of scattered across per-category PopupMenu::addItem calls, so this
        // is the only spot that needs updating when a new module type is added.
        static const std::vector<Entry> entries = {
            { ModuleType::threeOsc,              "3xOsc",                 "Generators" },
            { ModuleType::wavetableSynth,        "WT Synth",              "Generators" },
            { ModuleType::sampler,               "Sampler",               "Generators" },

            { ModuleType::filter,                "Filter",                "Filter & EQ" },
            { ModuleType::nonlinearFilter,        "Nonlinear Filter",      "Filter & EQ" },
            { ModuleType::eq8,                   "EQ 8",                  "Filter & EQ" },
            { ModuleType::eq3,                   "EQ 3",                  "Filter & EQ" },
            { ModuleType::multipass,             "Multipass",             "Filter & EQ" },

            { ModuleType::waveshaper,            "Waveshaper",            "Distortion" },
            { ModuleType::lossy,                 "Lossy",                 "Distortion" },
            { ModuleType::mackity,               "Mackity",               "Distortion" },
            { ModuleType::spectralClipper,       "Spectral Clipper",      "Distortion" },

            { ModuleType::delay,                 "Delay",                 "Time & Space" },
            { ModuleType::shimmerReverb,         "Shimmer Reverb",        "Time & Space" },
            { ModuleType::convolution,           "Convolution",           "Time & Space" },
            { ModuleType::multibandConvolution,  "Multiband Convolution", "Time & Space" },

            { ModuleType::chorus,                "Chorus/Flanger",        "Modulation" },
            { ModuleType::ringMod,               "Ring Mod",              "Modulation" },
            { ModuleType::lfo,                   "LFO",                   "Modulation" },
            { ModuleType::lfoTable,              "LFO Table",             "Modulation" },
            { ModuleType::envelope,              "Envelope",              "Modulation" },
            { ModuleType::adsr,                  "ADSR",                  "Modulation" },

            { ModuleType::compressor,            "Compressor",            "Dynamics" },
            { ModuleType::limiter,               "Limiter",               "Dynamics" },

            { ModuleType::utility,               "Utility",               "Utility" },

            { ModuleType::input,                 "Input",                 "I/O" },
            { ModuleType::output,                "Output",                "I/O" },
        };

        return entries;
    }

    AddModuleSearchPopup::AddModuleSearchPopup()
    {
        searchEditor.setTextToShowWhenEmpty ("Search modules...", Palette::dim);
        searchEditor.setColour (juce::TextEditor::backgroundColourId, Palette::dimmer);
        searchEditor.setColour (juce::TextEditor::textColourId, Palette::bright);
        searchEditor.setColour (juce::TextEditor::outlineColourId, Palette::dim);
        searchEditor.setColour (juce::TextEditor::focusedOutlineColourId, Palette::accent);
        searchEditor.onTextChange = [this] { rebuildRows(); };
        searchEditor.onArrowKey = [this] (int delta) { moveSelection (delta); };
        searchEditor.onReturnKey = [this] { commitSelection(); };
        searchEditor.onEscapeKey = [this] { if (onCancelled) onCancelled(); };
        addAndMakeVisible (searchEditor);

        resultsList.onRowClicked = [this] (int index)
        {
            selectedIndex = index;
            commitSelection();
        };

        resultsList.onRowHovered = [this] (int index)
        {
            selectedIndex = index;
            resultsList.setSelectedIndex (selectedIndex);
        };

        viewport.setViewedComponent (&resultsList, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        rebuildRows();
        setSize (popupWidth, searchHeight + juce::jmin (maxListHeight, resultsList.getContentHeight()));
    }

    void AddModuleSearchPopup::visibilityChanged()
    {
        if (isVisible())
            searchEditor.grabKeyboardFocus();
    }

    void AddModuleSearchPopup::resized()
    {
        searchEditor.setBounds (getLocalBounds().removeFromTop (searchHeight).reduced (4, 3));
        viewport.setBounds (getLocalBounds().withTrimmedTop (searchHeight));
        resultsList.setSize (viewport.getWidth(), resultsList.getContentHeight());
    }

    void AddModuleSearchPopup::rebuildRows()
    {
        const auto query = searchEditor.getText().trim();

        rows.clear();
        juce::String lastCategory;

        for (const auto& entry : allEntries())
        {
            if (query.isNotEmpty()
                && ! entry.name.containsIgnoreCase (query)
                && ! entry.category.containsIgnoreCase (query))
                continue;

            if (entry.category != lastCategory)
            {
                rows.push_back ({ true, entry.category, ModuleType::none });
                lastCategory = entry.category;
            }

            rows.push_back ({ false, entry.name, entry.type });
        }

        // Default the highlight to the first real (non-header) row, so Enter works immediately
        // without requiring an explicit arrow-key press first -- matches Spotlight/Start-menu
        // search, where the top hit is pre-selected as soon as you start typing.
        selectedIndex = -1;
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            if (! rows[(size_t) i].isHeader)
            {
                selectedIndex = i;
                break;
            }
        }

        resultsList.setRows (&rows);
        resultsList.setSelectedIndex (selectedIndex);

        if (auto* parentComp = getParentComponent())
            parentComp->resized();

        resized();
    }

    void AddModuleSearchPopup::moveSelection (int delta)
    {
        if (rows.empty())
            return;

        int index = selectedIndex;
        for (int step = 0; step < (int) rows.size(); ++step)
        {
            index = (index + delta + (int) rows.size()) % (int) rows.size();
            if (! rows[(size_t) index].isHeader)
            {
                selectedIndex = index;
                break;
            }
        }

        resultsList.setSelectedIndex (selectedIndex);

        const auto rowBounds = juce::Rectangle<int> (0, selectedIndex * ResultsList::rowHeight,
                                                       resultsList.getWidth(), ResultsList::rowHeight);
        viewport.setViewPosition (viewport.getViewPosition().x,
                                   juce::jlimit (juce::jmax (0, rowBounds.getBottom() - viewport.getHeight()),
                                                 rowBounds.getY(),
                                                 viewport.getViewPositionY()));
    }

    void AddModuleSearchPopup::commitSelection()
    {
        if (selectedIndex < 0 || selectedIndex >= (int) rows.size())
            return;

        const auto& row = rows[(size_t) selectedIndex];
        if (row.isHeader)
            return;

        if (onModuleChosen)
            onModuleChosen (row.type);
    }
}
