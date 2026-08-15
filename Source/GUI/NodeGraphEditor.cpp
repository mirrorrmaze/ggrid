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
            auto node = std::make_unique<NodeComponent> (processor.apvts, i, processor.getSlot (i));

            node->onNodeGrabbed = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeGrabbed (slotIndex, e); };
            node->onNodeDragged = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeDragged (slotIndex, e); };
            node->onNodeReleased = [this] (int slotIndex, const juce::MouseEvent& e) { handleNodeReleased (slotIndex, e); };
            node->onDeleteRequested = [this] (int slotIndex) { deleteNode (slotIndex); };
            node->onOutputDragStart = [this] (int slotIndex, const juce::MouseEvent& e) { handleOutputDragStart (slotIndex, e); };
            node->onOutputDrag = [this] (int slotIndex, const juce::MouseEvent& e) { handleOutputDrag (slotIndex, e); };
            node->onOutputDragEnd = [this] (int slotIndex, const juce::MouseEvent& e) { handleOutputDragEnd (slotIndex, e); };

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
            }

            const bool shouldBeVisible = (type != ModuleType::none);

            if (node->isVisible() != shouldBeVisible)
            {
                node->setVisible (shouldBeVisible);
                anyChanged = true;
            }

            if (shouldBeVisible)
            {
                const auto pos = processor.nodePositions[(size_t) i];
                const int height = node->getPreferredHeight();

                if (node->getPosition() != pos.toInt() || node->getHeight() != height)
                {
                    node->setBounds ((int) pos.x, (int) pos.y, NodeComponent::preferredWidth, height);
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

        int grabFrom = -1, grabTo = -1;
        if (hitTestCable (e.position, grabFrom, grabTo))
        {
            // Detach immediately -- the cable is now "held" by the drag and will only reappear
            // if it lands on a valid target at mouseUp. This also frees up the capacity it was
            // using, so dropping it right back where it came from just works.
            processor.removeConnection (grabFrom, grabTo);
            isDraggingCable = true;
            isDraggingModCable = false;
            cableDragSourceSlot = grabFrom;
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
                processor.addConnection (cableDragSourceSlot, targetSlot);

            isDraggingCable = false;
            cableDragSourceSlot = -1;
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
        // Plain scroll zooms toward the cursor -- no modifier needed. Scrollbars are hidden and
        // panning is click-drag, so the wheel is free for this; matches TouchDesigner/Blender.
        zoomAroundViewportPoint (zoomLevel + wheel.deltaY * 0.5f, getViewportRelativePosition (e));
    }

    void NodeGraphEditor::showAddNodeMenu (juce::Point<int> canvasPosition)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Waveshaper");
        menu.addItem (2, "Filter");
        menu.addItem (3, "Delay");
        menu.addItem (4, "Dynamics");
        menu.addItem (5, "Convolution");
        menu.addItem (6, "Utility");
        menu.addItem (7, "Ring Mod");
        menu.addItem (8, "LFO");
        menu.addItem (9, "Lossy");
        menu.addItem (10, "Graphic EQ");
        menu.addItem (11, "Chorus/Flanger");

        const auto screenPos = localPointToGlobal (canvasPosition);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (screenPos, screenPos)),
            [this, canvasPosition] (int result)
            {
                if (result == 0)
                    return;
                addNode (static_cast<ModuleType> (result), canvasPosition);
            });
    }

    void NodeGraphEditor::addNode (ModuleType type, juce::Point<int> position)
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const auto currentType = static_cast<ModuleType> ((int) processor.apvts.getRawParameterValue (slotTypeParamId (i))->load());
            if (currentType != ModuleType::none)
                continue;

            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (slotTypeParamId (i))))
                *choiceParam = (int) type;

            processor.nodePositions[(size_t) i] = position.toFloat();

            // Detect this None -> `type` change (and reset slot i's connection history, see
            // GGridAudioProcessor::everConnected) right now, before the auto-chain wiring below
            // -- otherwise pruneStaleConnectionsForSlot's generic type-change handling would
            // instead fire during the *next* refreshLayout() tick and wipe out the
            // everConnected flag the auto-chain connection is about to set.
            refreshLayout();

            // Auto-chain after whatever was added right before this, if it's still around and
            // has a spare output -- keeps simple serial patches "just working" without the user
            // having to wire every single node by hand. Explicit rewiring is how you branch off
            // from that default into something parallel. Never auto-chains an LFO on either end
            // -- it has no audio ports at all (see LFOModule), so an audio edge to/from one would
            // just be an invisible, inert connection sitting in the graph.
            if (lastAddedSlot >= 0 && nodes[(size_t) lastAddedSlot]->isVisible()
                && ! nodes[(size_t) lastAddedSlot]->isLfoType() && type != ModuleType::lfo)
                processor.addConnection (lastAddedSlot, i);
            lastAddedSlot = i;

            refreshLayout();
            repaint();
            return;
        }

        // All 8 slots in use -- rack is full. Silent no-op rather than a dialog: matches the
        // rest of the plugin's philosophy of never blocking the user with a modal.
    }

    void NodeGraphEditor::deleteNode (int slotIndex)
    {
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (slotTypeParamId (slotIndex))))
            *choiceParam = (int) ModuleType::none;

        processor.removeAllConnectionsForSlot (slotIndex);
        processor.getModulationMatrix().removeAllModConnectionsForSlot (slotIndex);
        if (lastAddedSlot == slotIndex)
            lastAddedSlot = -1;

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
        // LFO has no audio ports at all -- any audio edge touching it (either direction) is
        // stale the moment a slot becomes one, whether that happened via Add Node's auto-chain
        // or by picking "LFO" directly on an existing node's own type dropdown.
        if (newType == ModuleType::lfo)
            processor.removeAllConnectionsForSlot (slotIndex);

        // Modulation connections are pruned unconditionally on ANY type change (not just
        // to/from LFO): as a source, only LFO slots are ever valid; as a destination, which
        // specific knob a cable was aimed at is meaningless once the slot's become a different
        // module type. Cheaper and safer than trying to prove a specific old connection is still
        // valid for the new type -- rewiring is a small ask, a silently-wrong modulation target
        // is not.
        processor.getModulationMatrix().removeAllModConnectionsForSlot (slotIndex);

        // A retyped slot is a structurally new module -- it shouldn't inherit its predecessor's
        // "was once connected, so a fully disconnected state now means silent" history (see
        // GGridAudioProcessor::everConnected), or the freshly chosen module would incorrectly
        // start out silent instead of working standalone.
        processor.resetEverConnected (slotIndex);
    }

    bool NodeGraphEditor::hasSelection() const
    {
        for (bool s : selected)
            if (s) return true;
        return false;
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
                deleteNode (i); // handles connections/lastAddedSlot/selection cleanup per node
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

    void NodeGraphEditor::handleOutputDragStart (int slotIndex, const juce::MouseEvent& e)
    {
        isDraggingCable = true;
        isDraggingModCable = nodes[(size_t) slotIndex]->isLfoType();
        cableDragSourceSlot = slotIndex;
        cableDragCurrentPos = e.getEventRelativeTo (this).position;
        repaint();
    }

    void NodeGraphEditor::handleOutputDrag (int, const juce::MouseEvent& e)
    {
        cableDragCurrentPos = e.getEventRelativeTo (this).position;
        repaint();
    }

    void NodeGraphEditor::handleOutputDragEnd (int slotIndex, const juce::MouseEvent& e)
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
                processor.addConnection (slotIndex, targetSlot);
        }
        // Dropped on blank space, or the target's already full/would cycle -- just cancel.

        isDraggingCable = false;
        isDraggingModCable = false;
        cableDragSourceSlot = -1;
        repaint();
    }

    int NodeGraphEditor::findInputConnectorNear (juce::Point<float> canvasPoint, int excludeSlot) const
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            auto* candidate = nodes[(size_t) i].get();
            if (i == excludeSlot || ! candidate->isVisible())
                continue;

            for (int port = 0; port < 2; ++port)
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
        const int fromSlot = processor.connections[(size_t) connectionIndex].from;
        int ordinal = 0;
        for (int i = 0; i < connectionIndex; ++i)
            if (processor.connections[(size_t) i].from == fromSlot)
                ++ordinal;
        return juce::jmin (ordinal, 1);
    }

    int NodeGraphEditor::inputPortIndexForConnection (int connectionIndex) const
    {
        const int toSlot = processor.connections[(size_t) connectionIndex].to;
        int ordinal = 0;
        for (int i = 0; i < connectionIndex; ++i)
            if (processor.connections[(size_t) i].to == toSlot)
                ++ordinal;
        return juce::jmin (ordinal, 1);
    }

    bool NodeGraphEditor::hitTestCable (juce::Point<float> canvasPoint, int& outFromSlot, int& outToSlot) const
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
            const auto start = isDraggingModCable
                ? (fromNode->getPosition() + fromNode->getModOutputPosition()).toFloat()
                : (fromNode->getPosition() + fromNode->getOutputConnectorPosition (juce::jmin (processor.getOutDegree (cableDragSourceSlot), 1))).toFloat();

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
