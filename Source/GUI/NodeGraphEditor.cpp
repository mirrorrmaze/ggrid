#include "NodeGraphEditor.h"
#include "GGridLookAndFeel.h"

namespace GGrid
{
    NodeGraphEditor::NodeGraphEditor (GGridAudioProcessor& processorIn)
        : processor (processorIn)
    {
        setSize (canvasWidth, canvasHeight);
        setWantsKeyboardFocus (true);

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto node = std::make_unique<NodeComponent> (processor.apvts, i, processor.getSlot (i), processor.getNodeScope (i));

            node->onNodeGrabbed = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeGrabbed (slotIndex, e); };
            node->onNodeDragged = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeDragged (slotIndex, e); };
            node->onNodeReleased = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeReleased (slotIndex, e); };
            node->onNodeResizeGrabbed = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeResizeGrabbed (slotIndex, e); };
            node->onNodeResizeDragged = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeResizeDragged (slotIndex, e); };
            node->onNodeResizeReleased = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeResizeReleased (slotIndex, e); };
            node->onFoldToggled = [this] (int slotIndex, bool shouldBeFolded) { handleNodeFoldToggled (slotIndex, shouldBeFolded); };
            node->onDeleteRequested = [this] (int slotIndex) { deleteNode (slotIndex); };
            node->onOutputDragStart = [this] (int slotIndex, int portIndex, const juce::MouseEvent& e) { handleOutputDragStart (slotIndex, portIndex, e); };
            node->onOutputDrag = [this] (int slotIndex, int portIndex, const juce::MouseEvent& e) { handleOutputDrag (slotIndex, portIndex, e); };
            node->onOutputDragEnd = [this] (int slotIndex, int portIndex, const juce::MouseEvent& e) { handleOutputDragEnd (slotIndex, portIndex, e); };

            addAndMakeVisible (*node);
            nodes[(size_t) i] = std::move (node);

            // Seed from the actual current type (already reflects any loaded project state by
            // the time the editor is constructed) so refreshLayout()'s change-detection doesn't
            // mistake "state that was already there" for a fresh type change on the first tick.
            lastKnownType[(size_t) i] = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load());
        }

        refreshLayout();
        startTimerHz (30);
    }

    NodeGraphEditor::~NodeGraphEditor()
    {
        stopTimer();
    }

    void NodeGraphEditor::timerCallback()
    {
        refreshLayout();

        // refreshLayout() only repaints on an actual layout change -- but a mod cable's
        // traveling pulse (see paintOverChildren()) needs a steady redraw purely to animate,
        // even when nothing about the graph itself has changed. Only pay for that continuous
        // repaint while at least one mod cable exists to animate.
        if (processor.getModulationMatrix().numModConnections > 0)
            repaint();
    }

    void NodeGraphEditor::refreshLayout()
    {
        bool anyChanged = false;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto* node = nodes[(size_t) i].get();
            const auto type = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load());

            if (type != lastKnownType[(size_t) i])
            {
                lastKnownType[(size_t) i] = type;
                pruneStaleConnectionsForSlot (i, type);
                processor.nodeSizes[(size_t) i] = {};
                processor.nodeFolded[(size_t) i] = false;
            }

            const bool shouldBeVisible = (type != ModuleType::none);

            if (node->isVisible() != shouldBeVisible)
            {
                node->setVisible (shouldBeVisible);
                anyChanged = true;
            }

            if (shouldBeVisible)
            {
                node->setFolded (processor.nodeFolded[(size_t) i]);

                const auto pos = processor.nodePositions[(size_t) i];
                const int preferredWidth = node->getPreferredWidth();
                const int preferredHeight = node->getPreferredHeight();
                const int minWidth = node->getMinimumWidth();
                const int minHeight = node->getMinimumExpandedHeight();
                auto storedSize = processor.nodeSizes[(size_t) i];

                if (storedSize.x <= 0.0f || storedSize.y <= 0.0f)
                    storedSize = { (float) preferredWidth, (float) preferredHeight };

                const int width = juce::jmax (minWidth, juce::roundToInt (storedSize.x));
                const int expandedHeight = juce::jmax (minHeight, juce::roundToInt (storedSize.y));
                const int height = node->isFolded() ? node->getFoldedHeight() : expandedHeight;

                const juce::Point<float> clampedSize { (float) width, (float) expandedHeight };
                if (processor.nodeSizes[(size_t) i] != clampedSize)
                    processor.nodeSizes[(size_t) i] = clampedSize;

                if (node->getPosition() != pos.toInt() || node->getWidth() != width || node->getHeight() != height)
                {
                    node->setBounds ((int) pos.x, (int) pos.y, width, height);
                    anyChanged = true;
                }
            }
        }

        if (anyChanged)
            repaint();
    }

    juce::Point<float> NodeGraphEditor::getViewportRelativePosition (const juce::MouseEvent& e) const
    {
        return ownerViewport != nullptr ? e.getEventRelativeTo (ownerViewport).position : e.position;
    }

    void NodeGraphEditor::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus(); // so Delete/Backspace routes here right after any canvas click

        if (e.mods.isRightButtonDown())
        {
            showAddNodeMenu (e.position.toInt());
            return;
        }

        int grabFrom = -1, grabTo = -1, grabFromPort = -1;
        if (hitTestCable (e.position, grabFrom, grabTo, grabFromPort))
        {
            // Detach immediately -- the cable is now "held" by the drag and will only reappear
            // if it lands on a valid target at mouseUp. This also frees up the capacity it was
            // using, so dropping it right back where it came from just works. Passing the exact
            // grabbed fromPort matters once a multi-bus module can have several edges to the same
            // target (Low->X and Mid->X) -- without it, detaching one could ambiguously remove
            // whichever matches first.
            processor.removeConnection (grabFrom, grabTo, grabFromPort);
            isDraggingCable = true;
            isDraggingModCable = false;
            cableDragSourceSlot = grabFrom;
            cableDragSourcePort = grabFromPort;
            cableDragCurrentPos = e.position;
            repaint();
            return;
        }

        juce::String grabParamId;
        if (hitTestModCable (e.position, grabFrom, grabTo, grabParamId))
        {
            processor.getModulationMatrix().removeModConnection (grabFrom, grabTo, grabParamId);
            isDraggingCable = true;
            isDraggingModCable = true;
            cableDragSourceSlot = grabFrom;
            cableDragModParam = grabParamId;
            cableDragCurrentPos = e.position;
            repaint();
            return;
        }

        if (e.mods.isShiftDown())
        {
            // Instant select, bypassing the hold-before-select timing below -- a shortcut for
            // anyone who'd rather not wait out holdBeforeSelectMs.
            rubberBandActive = true;
            rubberBandStart = e.position;
            rubberBandCurrent = e.position;
            repaint();
            return;
        }

        if (hasSelection())
        {
            clearSelection();
            repaint();
        }

        blankDragMode = BlankDragMode::pendingGesture;
        dragStartViewportPos = getViewportRelativePosition (e);
        lastPanViewportPos = dragStartViewportPos;
        blankDragStartCanvasPos = e.position;
        blankDragHoldStartMs = juce::Time::getMillisecondCounterHiRes();
    }

    void NodeGraphEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (rubberBandActive)
        {
            rubberBandCurrent = e.position;
            updateSelectionFromRubberBand();
            repaint();
            return;
        }

        if (isDraggingCable)
        {
            cableDragCurrentPos = e.position;
            repaint();
            return;
        }

        if (blankDragMode == BlankDragMode::none)
            return;

        const auto currentViewportPos = getViewportRelativePosition (e);

        if (blankDragMode == BlankDragMode::pendingGesture)
        {
            if (currentViewportPos.getDistanceFrom (dragStartViewportPos) < 5.0f)
                return;

            // Crossed the movement threshold -- decide pan vs. select by how long the button was
            // held before this first real movement, not by a modifier key: a quick flick pans
            // (grab-and-go stays the fast default), holding briefly first selects instead.
            const double heldMs = juce::Time::getMillisecondCounterHiRes() - blankDragHoldStartMs;

            if (heldMs >= holdBeforeSelectMs)
            {
                blankDragMode = BlankDragMode::none;
                rubberBandActive = true;
                rubberBandStart = blankDragStartCanvasPos;
                rubberBandCurrent = e.position;
                updateSelectionFromRubberBand();
                repaint();
                return;
            }

            blankDragMode = BlankDragMode::panning;
            lastPanViewportPos = currentViewportPos;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

        // panning
        if (ownerViewport != nullptr)
        {
            const auto deltaScreen = currentViewportPos - lastPanViewportPos;
            const auto deltaContent = deltaScreen / zoomLevel;
            const auto newScrollPos = ownerViewport->getViewPosition().toFloat() - deltaContent;
            ownerViewport->setViewPosition (newScrollPos.roundToInt());
        }

        lastPanViewportPos = currentViewportPos;
    }

    void NodeGraphEditor::mouseUp (const juce::MouseEvent& e)
    {
        if (rubberBandActive)
        {
            rubberBandActive = false;
            updateSelectionFromRubberBand();
            repaint();
            return;
        }

        if (isDraggingCable && isDraggingModCable)
        {
            juce::String targetParamId;
            const int targetSlot = findModDestinationNear (e.position, cableDragSourceSlot, targetParamId);
            if (targetSlot >= 0)
                processor.getModulationMatrix().addModConnection (cableDragSourceSlot, targetSlot, targetParamId);

            isDraggingCable = false;
            isDraggingModCable = false;
            cableDragSourceSlot = -1;
            cableDragSourcePort = -1;
            repaint();
            return;
        }

        if (isDraggingCable)
        {
            const int targetSlot = findInputConnectorNear (e.position, cableDragSourceSlot);

            // Silently no-ops if the target is already at capacity, would form a cycle, or no
            // target was found at all -- if it was a rewire, the old edge is already gone (see
            // mouseDown), so "no valid target" just means "stays disconnected."
            if (targetSlot >= 0)
                processor.addConnection (cableDragSourceSlot, targetSlot, cableDragSourcePort);

            isDraggingCable = false;
            cableDragSourceSlot = -1;
            cableDragSourcePort = -1;
            repaint();
            return;
        }

        const bool wasPanning = (blankDragMode == BlankDragMode::panning);
        blankDragMode = BlankDragMode::none;

        if (wasPanning)
            setMouseCursor (juce::MouseCursor::NormalCursor);

        // A plain left click on blank space (no pan, no select) just deselects -- already done
        // in mouseDown. Adding a node is a right-click action now (see mouseDown).
    }

    void NodeGraphEditor::zoomAroundViewportPoint (float newZoom, juce::Point<float> viewportPoint)
    {
        const float clamped = juce::jlimit (minZoom, maxZoom, newZoom);

        if (ownerViewport == nullptr)
        {
            zoomLevel = clamped;
            setTransform (juce::AffineTransform::scale (zoomLevel));
            if (onZoomChanged)
                onZoomChanged (zoomLevel);
            return;
        }

        // Keep the canvas point currently under `viewportPoint` fixed on screen across the zoom
        // change -- Viewport's scroll position is in the child's own unscaled (pre-transform)
        // units, so this is just "where is that point now, and where must scroll be so it's
        // still there after zoom changes."
        const auto oldScrollPos = ownerViewport->getViewPosition().toFloat();
        const auto canvasPointUnderPivot = oldScrollPos + viewportPoint / zoomLevel;

        zoomLevel = clamped;
        setTransform (juce::AffineTransform::scale (zoomLevel));

        const auto newScrollPos = canvasPointUnderPivot - viewportPoint / zoomLevel;
        ownerViewport->setViewPosition (newScrollPos.roundToInt());

        if (onZoomChanged)
            onZoomChanged (zoomLevel);
    }

    void NodeGraphEditor::setZoom (float newZoom)
    {
        const auto pivot = ownerViewport != nullptr
            ? ownerViewport->getLocalBounds().getCentre().toFloat()
            : juce::Point<float>();
        zoomAroundViewportPoint (newZoom, pivot);
    }

    void NodeGraphEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        // Plain wheel/two-finger scroll pans -- both axes, whatever the wheel event reports
        // (a touchpad's two-finger scroll naturally carries both deltaX and deltaY). Delegates to
        // Viewport's own wheel-scroll logic rather than reimplementing it; the scrollbars being
        // hidden (see PluginEditor's setScrollBarsShown call) would normally stop that from
        // working at all, which is exactly what the trailing `true, true` there is for.
        // Viewport::useMouseWheelMoveIfNeeded refuses to scroll while Alt/Ctrl/Cmd is held, which
        // leaves Ctrl+scroll free below as a zoom shortcut for plain-mouse (no pinch gesture)
        // users -- the primary zoom gesture is the trackpad pinch, see mouseMagnify.
        if (ownerViewport != nullptr && ownerViewport->useMouseWheelMoveIfNeeded (e, wheel))
            return;

        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
            zoomAroundViewportPoint (zoomLevel + wheel.deltaY * 0.5f, getViewportRelativePosition (e));
    }

    void NodeGraphEditor::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
    {
        // Trackpad pinch -- scaleFactor is a multiplicative step for this frame of a continuous
        // gesture (values close to 1.0), not an absolute target, so it multiplies the current
        // zoom rather than adding to it like the wheel-based zoom above does.
        zoomAroundViewportPoint (zoomLevel * scaleFactor, getViewportRelativePosition (e));
    }

    void NodeGraphEditor::showAddNodeMenu (juce::Point<int> canvasPosition)
    {
        // addNode() silently no-ops if every slot is already occupied (matches the rest of the
        // plugin's "never block with a modal" philosophy) -- but a menu that opens normally and
        // then does nothing when you click an item is indistinguishable from a bug. Replace the
        // whole menu with one disabled, explanatory line in that case instead.
        bool anyFreeSlot = false;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load()) == ModuleType::none)
            {
                anyFreeSlot = true;
                break;
            }
        }

        juce::PopupMenu menu;

        if (! anyFreeSlot)
        {
            menu.addItem (0, "Rack full (" + juce::String (kMaxSlots) + "/" + juce::String (kMaxSlots)
                              + " slots) -- delete a node first", false);

            const auto screenPos = localPointToGlobal (canvasPosition);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos, screenPos)));
            return;
        }

        // Grouped into submenus by category rather than one flat list -- item IDs still map
        // directly to ModuleType (see addNode's static_cast below), that mapping doesn't care
        // about nesting depth, only which numeric ID got clicked. Add new module types to
        // whichever category they fit, or a new category if none do -- this is meant to scale as
        // the rack grows, not stay a fixed 6-way split.
        juce::PopupMenu distortionMenu;
        distortionMenu.addItem (1, "Waveshaper");
        distortionMenu.addItem (9, "Lossy");
        distortionMenu.addItem ((int) ModuleType::mackity, "Mackity");

        juce::PopupMenu filterEqMenu;
        filterEqMenu.addItem (2, "Filter");
        filterEqMenu.addItem ((int) ModuleType::nonlinearFilter, "Nonlinear Filter");
        filterEqMenu.addItem (10, "EQ 8");
        filterEqMenu.addItem (12, "EQ 3");
        filterEqMenu.addItem ((int) ModuleType::multipass, "Multipass");

        juce::PopupMenu dynamicsMenu;
        dynamicsMenu.addItem (4, "Dynamics");

        juce::PopupMenu modulationMenu;
        modulationMenu.addItem (11, "Chorus/Flanger");
        modulationMenu.addItem (7, "Ring Mod");
        modulationMenu.addItem (8, "LFO");
        modulationMenu.addItem ((int) ModuleType::lfoTable, "LFO Table");
        modulationMenu.addItem ((int) ModuleType::envelope, "Envelope");
        modulationMenu.addItem ((int) ModuleType::adsr, "ADSR");

        juce::PopupMenu timeSpaceMenu;
        timeSpaceMenu.addItem (3, "Delay");
        timeSpaceMenu.addItem ((int) ModuleType::shimmerReverb, "Shimmer Reverb");
        timeSpaceMenu.addItem (5, "Convolution");
        timeSpaceMenu.addItem ((int) ModuleType::multibandConvolution, "Multiband Convolution");

        juce::PopupMenu utilityMenu;
        utilityMenu.addItem (6, "Utility");

        // 3xOsc is a MIDI-driven synth generator, not an effect -- gets its own category rather
        // than living in I/O (it's a graph source like Input structurally, but semantically an
        // instrument, not a routing utility -- see ModuleType::threeOsc's class comment).
        juce::PopupMenu generatorsMenu;
        generatorsMenu.addItem ((int) ModuleType::threeOsc, "3xOsc");
        generatorsMenu.addItem ((int) ModuleType::wavetableSynth, "WT Synth");

        // Input/Output are ordinary addable module types like any other here -- adding another
        // of either just gives you a second entry/exit point for another parallel rack sharing
        // this canvas (see NodeComponent::isInputType/isOutputType), not something structurally
        // special about this menu item.
        juce::PopupMenu ioMenu;
        ioMenu.addItem ((int) ModuleType::input, "Input");
        ioMenu.addItem ((int) ModuleType::output, "Output");

        menu.addSubMenu ("Distortion", distortionMenu);
        menu.addSubMenu ("Filter & EQ", filterEqMenu);
        menu.addSubMenu ("Dynamics", dynamicsMenu);
        menu.addSubMenu ("Modulation", modulationMenu);
        menu.addSubMenu ("Time & Space", timeSpaceMenu);
        menu.addSubMenu ("Utility", utilityMenu);
        menu.addSubMenu ("Generators", generatorsMenu);
        menu.addSubMenu ("I/O", ioMenu);

        const auto screenPos = localPointToGlobal (canvasPosition);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos, screenPos)),
            [this, canvasPosition] (int result)
            {
                if (result == 0)
                    return;
                addNode (static_cast<ModuleType> (result), canvasPosition);
            });
    }

    int NodeGraphEditor::findFirstSlotOfType (ModuleType wantedType) const
    {
        for (int j = 0; j < kMaxSlots; ++j)
            if (static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (j))->load()) == wantedType)
                return j;
        return -1;
    }

    void NodeGraphEditor::addNode (ModuleType type, juce::Point<int> position)
    {
        // Only the very first regular processing module added to an otherwise-module-free rack
        // gets auto-wired straight through (Input -> node -> Output, to whichever Input/Output
        // nodes already exist), so it "just works" the instant you add it. Every module added
        // after that -- including any extra Input/Output nodes themselves -- arrives fully
        // unpatched: explicit cable gestures are how it joins the signal path, since past the
        // first node there's no single "obvious" default worth guessing at.
        bool anyRegularModuleExists = false;
        for (int j = 0; j < kMaxSlots; ++j)
        {
            const auto t = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (j))->load());
            if (t != ModuleType::none && t != ModuleType::input && t != ModuleType::output)
            {
                anyRegularModuleExists = true;
                break;
            }
        }

        for (int i = 0; i < kMaxSlots; ++i)
        {
            const auto currentType = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load());
            if (currentType != ModuleType::none)
                continue;

            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (slotTypeParamId (i))))
                *choiceParam = (int) type;

            processor.nodePositions[(size_t) i] = position.toFloat();

            refreshLayout();

            // Modulation sources (LFO/Envelope/ADSR), and Input/Output themselves, are never
            // auto-wired here -- modulation sources have no audio ports at all (see
            // isModulationSourceType() in Identifiers.h); wiring "the new Input to itself"
            // wouldn't mean anything.
            if (! anyRegularModuleExists && ! isModulationSourceType (type) && type != ModuleType::input && type != ModuleType::output)
            {
                const int outputSlot = findFirstSlotOfType (ModuleType::output);

                if (type == ModuleType::threeOsc || type == ModuleType::wavetableSynth)
                {
                    // ThreeOsc is itself a source (like Input, see ModuleType::threeOsc's class
                    // comment) -- there's no "from Input" leg to wire (it has no input port at
                    // all), just connect it straight to the first Output so it's audible the
                    // instant it's added.
                    if (outputSlot >= 0) processor.addConnection (i, outputSlot);
                }
                else
                {
                    const int inputSlot = findFirstSlotOfType (ModuleType::input);
                    if (inputSlot >= 0) processor.addConnection (inputSlot, i);
                    if (outputSlot >= 0) processor.addConnection (i, outputSlot);
                }
            }

            refreshLayout();
            repaint();
            return;
        }

        // Every slot in use -- rack is full. Silent no-op rather than a dialog: matches the
        // rest of the plugin's philosophy of never blocking the user with a modal.
    }

    void NodeGraphEditor::deleteNode (int slotIndex)
    {
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (slotTypeParamId (slotIndex))))
            *choiceParam = (int) ModuleType::none;

        processor.removeAllConnectionsForSlot (slotIndex);
        processor.getModulationMatrix().removeAllModConnectionsForSlot (slotIndex);
        processor.nodeSizes[(size_t) slotIndex] = {};
        processor.nodeFolded[(size_t) slotIndex] = false;

        if (selected[(size_t) slotIndex])
        {
            selected[(size_t) slotIndex] = false;
            nodes[(size_t) slotIndex]->setSelected (false);
        }

        refreshLayout();
        repaint();
    }

    void NodeGraphEditor::pruneStaleConnectionsForSlot (int slotIndex, ModuleType newType)
    {
        // Modulation sources (LFO/Envelope/ADSR) have no audio ports at all -- any audio edge
        // touching one (either direction) is stale the moment a slot becomes one, whether that
        // happened via Add Node's auto-chain or by picking a source type directly on an existing
        // node's own type dropdown.
        if (isModulationSourceType (newType))
            processor.removeAllConnectionsForSlot (slotIndex);

        // Input/ThreeOsc have no input ports (only their outgoing edges are still valid); Output
        // has no output ports (only its incoming edges are still valid) -- prune whichever side
        // just became invalid for the new type. Iterated backward since removeConnection shifts
        // later entries down.
        if (newType == ModuleType::input || newType == ModuleType::threeOsc || newType == ModuleType::wavetableSynth)
        {
            for (int c = processor.numConnections - 1; c >= 0; --c)
                if (processor.connections[(size_t) c].to == slotIndex)
                    processor.removeConnection (processor.connections[(size_t) c].from, slotIndex, processor.connections[(size_t) c].fromPort);
        }
        else if (newType == ModuleType::output)
        {
            for (int c = processor.numConnections - 1; c >= 0; --c)
                if (processor.connections[(size_t) c].from == slotIndex)
                    processor.removeConnection (slotIndex, processor.connections[(size_t) c].to, processor.connections[(size_t) c].fromPort);
        }

        // Modulation connections are pruned unconditionally on ANY type change (not just
        // to/from LFO): as a source, only LFO slots are ever valid; as a destination, which
        // specific knob a cable was aimed at is meaningless once the slot's become a different
        // module type. Cheaper and safer than trying to prove a specific old connection is still
        // valid for the new type -- rewiring is a small ask, a silently-wrong modulation target
        // is not.
        processor.getModulationMatrix().removeAllModConnectionsForSlot (slotIndex);
    }

    void NodeGraphEditor::clearSelection()
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (selected[(size_t) i])
            {
                selected[(size_t) i] = false;
                nodes[(size_t) i]->setSelected (false);
            }
        }
    }

    void NodeGraphEditor::setSelectionToSingle (int slotIndex)
    {
        clearSelection();
        selected[(size_t) slotIndex] = true;
        nodes[(size_t) slotIndex]->setSelected (true);
    }

    void NodeGraphEditor::toggleSelection (int slotIndex)
    {
        selected[(size_t) slotIndex] = ! selected[(size_t) slotIndex];
        nodes[(size_t) slotIndex]->setSelected (selected[(size_t) slotIndex]);
    }

    void NodeGraphEditor::updateSelectionFromRubberBand()
    {
        const juce::Rectangle<float> band (rubberBandStart, rubberBandCurrent);

        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto* node = nodes[(size_t) i].get();
            const bool shouldBeSelected = node->isVisible() && band.intersects (node->getBounds().toFloat());

            if (selected[(size_t) i] != shouldBeSelected)
            {
                selected[(size_t) i] = shouldBeSelected;
                node->setSelected (shouldBeSelected);
            }
        }
    }

    void NodeGraphEditor::deleteSelectedNodes()
    {
        for (int i = 0; i < kMaxSlots; ++i)
            if (selected[(size_t) i])
                deleteNode (i); // handles connections/selection cleanup per node
    }

    void NodeGraphEditor::initPatch()
    {
        // Wipes the rack back to a genuinely empty canvas -- every slot (including any Input/
        // Output nodes) back to None, connections and mod cables gone with them via deleteNode.
        // Doesn't touch a slot's own parameter values once it's None again (those are only ever
        // meaningful while a slot is that type -- see the parameter ID scheme's class comment in
        // Identifiers.h), just like retyping any single slot away from a type never has.
        for (int i = 0; i < kMaxSlots; ++i)
            deleteNode (i);
    }

    bool NodeGraphEditor::hasSelection() const
    {
        for (bool s : selected)
            if (s) return true;
        return false;
    }

    void NodeGraphEditor::copySelection()
    {
        clipboard.numModules = 0;
        clipboard.numAudioConnections = 0;
        clipboard.numModConnections = 0;
        clipboard.pasteCount = 0;

        // Maps a real slot index to its position within the clipboard (only set for selected,
        // populated slots) so connections between two copied modules can be recorded relative to
        // the clipboard rather than to slot indices that won't exist at paste time.
        std::array<int, kMaxSlots> clipboardIndexForSlot;
        clipboardIndexForSlot.fill (-1);

        juce::Point<float> topLeft (1.0e9f, 1.0e9f);
        for (int i = 0; i < kMaxSlots; ++i)
            if (selected[(size_t) i])
            {
                topLeft.x = juce::jmin (topLeft.x, processor.nodePositions[(size_t) i].x);
                topLeft.y = juce::jmin (topLeft.y, processor.nodePositions[(size_t) i].y);
            }
        clipboard.sourceTopLeft = topLeft;

        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (! selected[(size_t) i]) continue;

            const auto type = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load());
            if (type == ModuleType::none) continue;

            auto& mod = clipboard.modules[(size_t) clipboard.numModules];
            mod.type = type;
            mod.relativePosition = processor.nodePositions[(size_t) i] - topLeft;
            mod.size = processor.nodeSizes[(size_t) i];
            if (mod.size.x <= 0.0f || mod.size.y <= 0.0f)
                mod.size = { (float) nodes[(size_t) i]->getWidth(), (float) nodes[(size_t) i]->getHeight() };
            mod.folded = processor.nodeFolded[(size_t) i];
            mod.paramValues.clear();

            // Every one of this slot's parameters, whatever module type each belongs to (not
            // just the currently-active type) -- a byte-for-byte clone of this slot's entire
            // persisted parameter footprint, generic across module types via
            // RangedAudioParameter's normalised get/set rather than a hardcoded per-type list.
            const juce::String prefix = "slot" + juce::String (i) + "_";
            for (auto* param : processor.getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (ranged->paramID.startsWith (prefix))
                        mod.paramValues.add ({ ranged->paramID.substring (prefix.length()), ranged->getValue() });

            clipboardIndexForSlot[(size_t) i] = clipboard.numModules;
            ++clipboard.numModules;
        }

        for (int c = 0; c < processor.numConnections; ++c)
        {
            const auto& conn = processor.connections[(size_t) c];
            const int fromIdx = clipboardIndexForSlot[(size_t) conn.from];
            const int toIdx = clipboardIndexForSlot[(size_t) conn.to];
            if (fromIdx >= 0 && toIdx >= 0 && clipboard.numAudioConnections < (int) clipboard.audioConnections.size())
                clipboard.audioConnections[(size_t) clipboard.numAudioConnections++] = { fromIdx, toIdx, conn.fromPort };
        }

        const auto& modMatrix = processor.getModulationMatrix();
        for (int c = 0; c < modMatrix.numModConnections; ++c)
        {
            const auto& conn = modMatrix.modConnections[(size_t) c];
            const int fromIdx = clipboardIndexForSlot[(size_t) conn.fromSlot];
            const int toIdx = clipboardIndexForSlot[(size_t) conn.toSlot];
            if (fromIdx < 0 || toIdx < 0) continue;

            const juce::String toPrefix = "slot" + juce::String (conn.toSlot) + "_";
            if (! conn.destinationParamId.startsWith (toPrefix)) continue;

            if (clipboard.numModConnections < (int) clipboard.modConnections.size())
                clipboard.modConnections[(size_t) clipboard.numModConnections++] =
                    { fromIdx, toIdx, conn.destinationParamId.substring (toPrefix.length()) };
        }
    }

    void NodeGraphEditor::pasteClipboard()
    {
        if (clipboard.numModules == 0) return;

        // Offset from the copied modules' original position so repeated pastes visibly cascade
        // rather than stacking exactly on top of the originals (or each other).
        constexpr float pasteOffset = 40.0f;
        const auto pasteOrigin = clipboard.sourceTopLeft + juce::Point<float> (pasteOffset * (float) (clipboard.pasteCount + 1),
                                                                               pasteOffset * (float) (clipboard.pasteCount + 1));

        std::array<int, kMaxSlots> targetSlot;
        targetSlot.fill (-1);

        // Finds enough free slots up front and pastes as many of the clipboard as fit rather
        // than partially failing partway through if the rack doesn't have room for all of it.
        int searchFrom = 0;
        for (int m = 0; m < clipboard.numModules; ++m)
        {
            int found = -1;
            for (int s = searchFrom; s < kMaxSlots; ++s)
            {
                if (static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (s))->load()) == ModuleType::none)
                {
                    found = s;
                    searchFrom = s + 1;
                    break;
                }
            }
            if (found < 0) break;
            targetSlot[(size_t) m] = found;

            const auto& mod = clipboard.modules[(size_t) m];
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (slotTypeParamId (found))))
                *choiceParam = (int) mod.type;

            processor.nodePositions[(size_t) found] = pasteOrigin + mod.relativePosition;
            processor.nodeSizes[(size_t) found] = mod.size;
            processor.nodeFolded[(size_t) found] = mod.folded;

            const juce::String newPrefix = "slot" + juce::String (found) + "_";
            for (auto& kv : mod.paramValues)
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (processor.apvts.getParameter (newPrefix + kv.first)))
                    ranged->setValueNotifyingHost (kv.second);
        }

        // Settle refreshLayout()'s type-change detection (pruneStaleConnectionsForSlot etc.)
        // before wiring anything -- otherwise the connections just added below would look like
        // stale leftovers from the type change that just happened and get immediately pruned.
        refreshLayout();

        for (int c = 0; c < clipboard.numAudioConnections; ++c)
        {
            const auto& conn = clipboard.audioConnections[(size_t) c];
            if (targetSlot[(size_t) conn.fromIndex] >= 0 && targetSlot[(size_t) conn.toIndex] >= 0)
                processor.addConnection (targetSlot[(size_t) conn.fromIndex], targetSlot[(size_t) conn.toIndex], conn.fromPort);
        }

        for (int c = 0; c < clipboard.numModConnections; ++c)
        {
            const auto& conn = clipboard.modConnections[(size_t) c];
            if (targetSlot[(size_t) conn.fromIndex] < 0 || targetSlot[(size_t) conn.toIndex] < 0) continue;

            const juce::String destinationParamId = "slot" + juce::String (targetSlot[(size_t) conn.toIndex]) + "_" + conn.destinationSuffix;
            processor.getModulationMatrix().addModConnection (targetSlot[(size_t) conn.fromIndex], targetSlot[(size_t) conn.toIndex], destinationParamId);
        }

        // Select the freshly pasted modules, matching most programs' paste behaviour -- an
        // immediate drag or another Ctrl+V/Ctrl+D acts on what was just pasted, not the originals.
        clearSelection();
        for (int m = 0; m < clipboard.numModules; ++m)
            if (targetSlot[(size_t) m] >= 0)
                toggleSelection (targetSlot[(size_t) m]);

        refreshLayout();
        repaint();
        ++clipboard.pasteCount;
    }

    void NodeGraphEditor::duplicateSelection()
    {
        if (! hasSelection()) return;
        copySelection();
        pasteClipboard();
    }

    bool NodeGraphEditor::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            if (hasSelection())
            {
                deleteSelectedNodes();
                return true;
            }
        }

        if (key.getModifiers().isCommandDown())
        {
            if (key.getKeyCode() == 'C') { copySelection(); return true; }
            if (key.getKeyCode() == 'V') { pasteClipboard(); return true; }
            if (key.getKeyCode() == 'D') { duplicateSelection(); return true; }
        }

        return false;
    }

    void NodeGraphEditor::handleNodeGrabbed (int slotIndex, const juce::MouseEvent& e)
    {
        grabKeyboardFocus();

        if (e.mods.isShiftDown())
        {
            toggleSelection (slotIndex);
            nodeDragMode = NodeDragMode::none; // shift-click toggles selection only, never moves
            repaint();
            return;
        }

        if (! isSelected (slotIndex))
            setSelectionToSingle (slotIndex);
        // else: already part of the selection (possibly multi-node) -- preserve it for now; it
        // only collapses to just this node if the gesture resolves as a plain click, below.

        if (e.mods.isAltDown())
        {
            copySelection();
            pasteClipboard();
        }

        nodeDragMode = NodeDragMode::pendingClickOrMove;
        nodeDragStartCanvasPos = e.getEventRelativeTo (this).position;
        nodeDragStartViewportPos = getViewportRelativePosition (e);

        for (int i = 0; i < kMaxSlots; ++i)
            if (selected[(size_t) i])
                nodeDragStartPositions[(size_t) i] = processor.nodePositions[(size_t) i];

        repaint();
    }

    void NodeGraphEditor::handleNodeDragged (int, const juce::MouseEvent& e)
    {
        if (nodeDragMode == NodeDragMode::none)
            return;

        if (nodeDragMode == NodeDragMode::pendingClickOrMove)
        {
            // Threshold checked in Viewport-relative (zoom-independent) space, same pattern as
            // the blank-canvas click-vs-pan gesture -- a fixed distance in canvas units would
            // feel wildly different at 20% vs 200% zoom.
            if (getViewportRelativePosition (e).getDistanceFrom (nodeDragStartViewportPos) < 5.0f)
                return;
            nodeDragMode = NodeDragMode::movingGroup;
        }

        const auto delta = e.getEventRelativeTo (this).position - nodeDragStartCanvasPos;

        for (int i = 0; i < kMaxSlots; ++i)
            if (selected[(size_t) i])
                processor.nodePositions[(size_t) i] = nodeDragStartPositions[(size_t) i] + delta;

        refreshLayout();
        repaint();
    }

    void NodeGraphEditor::handleNodeReleased (int slotIndex, const juce::MouseEvent&)
    {
        if (nodeDragMode == NodeDragMode::pendingClickOrMove)
        {
            // Resolved as a plain click, not a drag -- collapse the selection to just this node.
            setSelectionToSingle (slotIndex);
            repaint();
        }

        nodeDragMode = NodeDragMode::none;
    }

    void NodeGraphEditor::handleNodeResizeGrabbed (int slotIndex, const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        setSelectionToSingle (slotIndex);

        nodeDragMode = NodeDragMode::none;
        blankDragMode = BlankDragMode::none;
        resizingSlot = slotIndex;
        nodeResizeStartCanvasPos = e.getEventRelativeTo (this).position;

        auto storedSize = processor.nodeSizes[(size_t) slotIndex];
        if (storedSize.x <= 0.0f || storedSize.y <= 0.0f)
            storedSize = { (float) nodes[(size_t) slotIndex]->getWidth(), (float) nodes[(size_t) slotIndex]->getHeight() };
        nodeResizeStartSize = storedSize;

        repaint();
    }

    void NodeGraphEditor::handleNodeResizeDragged (int slotIndex, const juce::MouseEvent& e)
    {
        if (resizingSlot != slotIndex)
            return;

        const auto delta = e.getEventRelativeTo (this).position - nodeResizeStartCanvasPos;
        auto* node = nodes[(size_t) slotIndex].get();
        const float newWidth = juce::jmax ((float) node->getMinimumWidth(), nodeResizeStartSize.x + delta.x);
        const float newHeight = juce::jmax ((float) node->getMinimumExpandedHeight(), nodeResizeStartSize.y + delta.y);

        processor.nodeSizes[(size_t) slotIndex] = { newWidth, newHeight };
        refreshLayout();
        repaint();
    }

    void NodeGraphEditor::handleNodeResizeReleased (int slotIndex, const juce::MouseEvent&)
    {
        if (resizingSlot == slotIndex)
            resizingSlot = -1;
    }

    void NodeGraphEditor::handleNodeFoldToggled (int slotIndex, bool shouldBeFolded)
    {
        processor.nodeFolded[(size_t) slotIndex] = shouldBeFolded;
        refreshLayout();
        repaint();
    }

    void NodeGraphEditor::handleOutputDragStart (int slotIndex, int portIndex, const juce::MouseEvent& e)
    {
        isDraggingCable = true;
        isDraggingModCable = nodes[(size_t) slotIndex]->isModulationSourceType();
        cableDragSourceSlot = slotIndex;
        // Pin to the exact nub grabbed only for a multi-bus module (Multipass) -- every ordinary
        // single-bus module keeps the cosmetic auto-spread (see outputPortIndexForConnection), so
        // dragging from whichever dot happens to be easiest to grab doesn't change anything.
        cableDragSourcePort = nodes[(size_t) slotIndex]->isMultipassType() ? portIndex : -1;
        cableDragCurrentPos = e.getEventRelativeTo (this).position;
        repaint();
    }

    void NodeGraphEditor::handleOutputDrag (int, int, const juce::MouseEvent& e)
    {
        cableDragCurrentPos = e.getEventRelativeTo (this).position;
        repaint();
    }

    void NodeGraphEditor::handleOutputDragEnd (int slotIndex, int, const juce::MouseEvent& e)
    {
        const auto dropPos = e.getEventRelativeTo (this).position;

        if (isDraggingModCable)
        {
            juce::String targetParamId;
            const int targetSlot = findModDestinationNear (dropPos, slotIndex, targetParamId);
            if (targetSlot >= 0)
                processor.getModulationMatrix().addModConnection (slotIndex, targetSlot, targetParamId);
        }
        else
        {
            const int targetSlot = findInputConnectorNear (dropPos, slotIndex);
            if (targetSlot >= 0)
                processor.addConnection (slotIndex, targetSlot, cableDragSourcePort);
        }
        // Dropped on blank space, or the target's already full/would cycle -- just cancel.

        isDraggingCable = false;
        isDraggingModCable = false;
        cableDragSourceSlot = -1;
        cableDragSourcePort = -1;
        repaint();
    }

    int NodeGraphEditor::findInputConnectorNear (juce::Point<float> canvasPoint, int excludeSlot) const
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto* candidate = nodes[(size_t) i].get();
            // Modulation-source (LFO/Envelope/ADSR), Input, and ThreeOsc nodes have no input ports
            // at all -- modulation sources aren't part of the audio graph (see
            // isModulationSourceType() in Identifiers.h), Input/ThreeOsc are sources only (see
            // hasNoInputPorts()). Output nodes DO have input ports (that's their entire purpose)
            // so aren't excluded here.
            if (i == excludeSlot || ! candidate->isVisible() || candidate->isModulationSourceType() || candidate->hasNoInputPorts())
                continue;

            for (int port = 0; port < kMaxPortsPerSide; ++port)
            {
                const auto inputPos = (candidate->getPosition() + candidate->getInputConnectorPosition (port)).toFloat();
                if (inputPos.getDistanceFrom (canvasPoint) < 20.0f)
                    return i;
            }
        }
        return -1;
    }

    int NodeGraphEditor::findModDestinationNear (juce::Point<float> canvasPoint, int excludeSlot, juce::String& outParamId) const
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto* candidate = nodes[(size_t) i].get();
            if (i == excludeSlot || ! candidate->isVisible())
                continue;

            const int targetCount = candidate->getModTargetCount();
            for (int t = 0; t < targetCount; ++t)
            {
                const auto destPos = (candidate->getPosition() + candidate->getModTargetPosition (t)).toFloat();
                if (destPos.getDistanceFrom (canvasPoint) < 20.0f)
                {
                    outParamId = candidate->getModTargetParamId (t);
                    return i;
                }
            }
        }
        return -1;
    }

    juce::Point<float> NodeGraphEditor::modTargetPositionFor (const NodeComponent* node, const juce::String& paramId) const
    {
        const int targetCount = node->getModTargetCount();
        for (int t = 0; t < targetCount; ++t)
            if (node->getModTargetParamId (t) == paramId)
                return (node->getPosition() + node->getModTargetPosition (t)).toFloat();

        return node->getPosition().toFloat();
    }

    bool NodeGraphEditor::hitTestModCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot, juce::String& outParamId) const
    {
        const auto& modMatrix = processor.getModulationMatrix();

        for (int c = 0; c < modMatrix.numModConnections; ++c)
        {
            const auto& conn = modMatrix.modConnections[(size_t) c];
            auto* fromNode = nodes[(size_t) conn.fromSlot].get();
            auto* toNode = nodes[(size_t) conn.toSlot].get();
            if (! fromNode->isVisible() || ! toNode->isVisible())
                continue;

            const auto start = (fromNode->getPosition() + fromNode->getModOutputPosition()).toFloat();
            const auto end = modTargetPositionFor (toNode, conn.destinationParamId);

            auto cablePath = buildCablePath (start, end);
            juce::Path strokedPath;
            juce::PathStrokeType (14.0f).createStrokedPath (strokedPath, cablePath);

            if (strokedPath.contains (canvasPoint))
            {
                outFromSlot = conn.fromSlot;
                outToSlot = conn.toSlot;
                outParamId = conn.destinationParamId;
                return true;
            }
        }

        return false;
    }

    int NodeGraphEditor::outputPortIndexForConnection (int connectionIndex) const
    {
        const auto& conn = processor.connections[(size_t) connectionIndex];
        // A pinned port (multi-bus module, e.g. Multipass) always draws/hit-tests at its own
        // specific dot -- never the cosmetic ordinal auto-spread below.
        if (conn.fromPort >= 0)
            return juce::jmin (conn.fromPort, kMaxPortsPerSide - 1);

        int ordinal = 0;
        for (int i = 0; i < connectionIndex; ++i)
            if (processor.connections[(size_t) i].from == conn.from && processor.connections[(size_t) i].fromPort < 0)
                ++ordinal;
        return juce::jmin (ordinal, kMaxPortsPerSide - 1);
    }

    int NodeGraphEditor::inputPortIndexForConnection (int connectionIndex) const
    {
        const int toSlot = processor.connections[(size_t) connectionIndex].to;
        int ordinal = 0;
        for (int i = 0; i < connectionIndex; ++i)
            if (processor.connections[(size_t) i].to == toSlot)
                ++ordinal;
        return juce::jmin (ordinal, kMaxPortsPerSide - 1);
    }

    bool NodeGraphEditor::hitTestCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot, int& outFromPort) const
    {
        for (int c = 0; c < processor.numConnections; ++c)
        {
            const auto& conn = processor.connections[(size_t) c];
            auto* fromNode = nodes[(size_t) conn.from].get();
            auto* toNode = nodes[(size_t) conn.to].get();
            if (! fromNode->isVisible() || ! toNode->isVisible())
                continue;

            const auto start = (fromNode->getPosition() + fromNode->getOutputConnectorPosition (outputPortIndexForConnection (c))).toFloat();
            const auto end = (toNode->getPosition() + toNode->getInputConnectorPosition (inputPortIndexForConnection (c))).toFloat();

            auto cablePath = buildCablePath (start, end);
            juce::Path strokedPath;
            juce::PathStrokeType (14.0f).createStrokedPath (strokedPath, cablePath);

            if (strokedPath.contains (canvasPoint))
            {
                outFromSlot = conn.from;
                outToSlot = conn.to;
                outFromPort = conn.fromPort;
                return true;
            }
        }

        return false;
    }

    juce::Path NodeGraphEditor::buildCablePath (juce::Point<float> start, juce::Point<float> end) const
    {
        juce::Path cable;
        cable.startNewSubPath (start);
        const float curveOffset = juce::jmax (30.0f, std::abs (end.x - start.x) * 0.5f);
        cable.cubicTo (start.x + curveOffset, start.y, end.x - curveOffset, end.y, end.x, end.y);
        return cable;
    }

    void NodeGraphEditor::paint (juce::Graphics& g)
    {
        g.fillAll (Palette::bg);

        // Faint grid, purely decorative, to make the free-form canvas read as a "sandbox" rather
        // than an empty void.
        g.setColour (Palette::dimmer.withAlpha (0.4f));
        constexpr int gridStep = 40;
        for (int x = 0; x < getWidth(); x += gridStep)
            g.drawVerticalLine (x, 0.0f, (float) getHeight());
        for (int y = 0; y < getHeight(); y += gridStep)
            g.drawHorizontalLine (y, 0.0f, (float) getWidth());
    }

    void NodeGraphEditor::paintOverChildren (juce::Graphics& g)
    {
        // Cables (and the rubber-band overlay) are drawn here rather than in paint() so they
        // render on top of every NodeComponent instead of underneath -- JUCE always paints a
        // component's own paint() before its children, so anything drawn there would get
        // covered by node bodies wherever a cable's path happens to cross one (which mod cables
        // routinely do now, since their destination dots sit on knobs inside a node rather than
        // only ever at a node's edge like audio ports do).
        g.setColour (Palette::accent);
        for (int c = 0; c < processor.numConnections; ++c)
        {
            const auto& conn = processor.connections[(size_t) c];
            auto* fromNode = nodes[(size_t) conn.from].get();
            auto* toNode = nodes[(size_t) conn.to].get();
            if (! fromNode->isVisible() || ! toNode->isVisible())
                continue;

            const auto start = (fromNode->getPosition() + fromNode->getOutputConnectorPosition (outputPortIndexForConnection (c))).toFloat();
            const auto end = (toNode->getPosition() + toNode->getInputConnectorPosition (inputPortIndexForConnection (c))).toFloat();

            g.strokePath (buildCablePath (start, end), juce::PathStrokeType (2.0f));
        }

        {
            const auto& modMatrix = processor.getModulationMatrix();
            for (int c = 0; c < modMatrix.numModConnections; ++c)
            {
                const auto& conn = modMatrix.modConnections[(size_t) c];
                auto* fromNode = nodes[(size_t) conn.fromSlot].get();
                auto* toNode = nodes[(size_t) conn.toSlot].get();
                if (! fromNode->isVisible() || ! toNode->isVisible())
                    continue;

                const auto start = (fromNode->getPosition() + fromNode->getModOutputPosition()).toFloat();
                const auto end = modTargetPositionFor (toNode, conn.destinationParamId);

                auto cablePath = buildCablePath (start, end);
                g.setColour (Palette::modAccent);
                g.strokePath (cablePath, juce::PathStrokeType (2.0f));

                // A little glowing pulse riding along the cable, positioned by the source LFO's
                // live depth-scaled output (-1..1, mapped onto the path's 0..1 length) rather
                // than a free-running clock -- this way its back-and-forth motion visibly tracks
                // both the LFO's Rate (how fast it travels) and Shape (a square LFO's pulse
                // snaps between the two ends; a sine's eases) with no separate animation clock to
                // keep in sync.
                const float pathLength = cablePath.getLength();
                if (pathLength > 0.0f)
                {
                    const float lfoValue = juce::jlimit (-1.0f, 1.0f, modMatrix.getLfoValue (conn.fromSlot));
                    const auto pulsePos = cablePath.getPointAlongPath ((lfoValue + 1.0f) * 0.5f * pathLength);

                    g.setColour (Palette::modAccent.withAlpha (0.25f));
                    g.fillEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (pulsePos));
                    g.setColour (Palette::modAccent.brighter (0.6f));
                    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (pulsePos));
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (pulsePos));
                }
            }
        }

        if (isDraggingCable && cableDragSourceSlot >= 0)
        {
            auto* fromNode = nodes[(size_t) cableDragSourceSlot].get();
            const int previewPort = cableDragSourcePort >= 0
                ? cableDragSourcePort
                : juce::jmin (processor.getOutDegree (cableDragSourceSlot), kMaxPortsPerSide - 1);
            const auto start = isDraggingModCable
                ? (fromNode->getPosition() + fromNode->getModOutputPosition()).toFloat()
                : (fromNode->getPosition() + fromNode->getOutputConnectorPosition (previewPort)).toFloat();

            g.setColour (isDraggingModCable ? Palette::modAccent : Palette::bright);
            g.strokePath (buildCablePath (start, cableDragCurrentPos), juce::PathStrokeType (1.5f));
        }

        if (rubberBandActive)
        {
            const juce::Rectangle<float> band (rubberBandStart, rubberBandCurrent);
            g.setColour (Palette::accent.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (Palette::accent);
            g.drawRect (band, 1.0f);
        }
    }

    void NodeGraphEditor::resized()
    {
        // Fixed virtual canvas size (set once in the constructor) -- nothing to relayout here;
        // node positions/sizes are reconciled continuously by refreshLayout().
    }
}
