#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params/ParameterLayout.h"
#include "Modules/LFOModule.h"

namespace GGrid
{
    GGridAudioProcessor::GGridAudioProcessor()
        : AudioProcessor (BusesProperties()
                             .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
          modulationMatrix (apvts)
    {
        for (int i = 0; i < kMaxSlots; ++i)
        {
            slots[(size_t) i] = std::make_unique<RackSlot> (apvts, i, sharedServices);
            nodePositions[(size_t) i] = { 260.0f + (float) i * 30.0f, 40.0f + (float) i * 30.0f };

            nodeScopes[(size_t) i] = std::make_unique<juce::AudioVisualiserComponent> (2);
            nodeScopes[(size_t) i]->setColours (juce::Colour (0xff202022), juce::Colour (0xffbf5727));
            nodeScopes[(size_t) i]->setRepaintRate (30);
            nodeScopes[(size_t) i]->setBufferSize (512);
        }

        // A brand-new instance (no saved project/host state loaded yet) starts with one Input and
        // one Output already in place -- Input/Output are ordinary addable/deletable module types
        // (see ModuleType::input/output), not fixed pseudo-nodes, but seeding one of each here
        // means a first real module still auto-wires straight through immediately (see
        // NodeGraphEditor::addNode) the way it always has, rather than starting from a completely
        // blank rack. Loading a project overwrites this via apvts.replaceState() in
        // setStateInformation, same as every other default parameter value.
        nodePositions[0] = { 40.0f, 260.0f };
        nodePositions[1] = { 1100.0f, 260.0f };
        if (auto* inputType = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (slotTypeParamId (0))))
            *inputType = (int) ModuleType::input;
        if (auto* outputType = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (slotTypeParamId (1))))
            *outputType = (int) ModuleType::output;

        limiterEnabledParam = apvts.getRawParameterValue (masterLimiterEnabledParamId());
        limiterCeilingParam = apvts.getRawParameterValue (masterLimiterCeilingParamId());

        // Matches GGridLookAndFeel's Palette::bg/accent (see Source/GUI/GGridLookAndFeel.h) --
        // hardcoded rather than pulling in a GUI header from the processor, but keep these two
        // in sync if the palette ever changes.
        outputScope.setColours (juce::Colour (0xff202022), juce::Colour (0xffbf5727));
        outputScope.setRepaintRate (30);
        outputScope.setBufferSize (1024);
    }

    bool GGridAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto mainOut = layouts.getMainOutputChannelSet();

        if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
            return false;

        return mainOut == layouts.getMainInputChannelSet();
    }

    void GGridAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.numChannels = (juce::uint32) getTotalNumOutputChannels();

        for (auto& slot : slots)
            slot->prepare (spec);

        masterLimiter.prepare (spec);

        for (auto& nodeBuffer : nodeBuffers)
            nodeBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);
        lfoScratchBuffer.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        outputScope.clear();
        outputScope.setSamplesPerBlock (juce::jmax (1, samplesPerBlock));

        for (auto& scope : nodeScopes)
        {
            scope->clear();
            scope->setSamplesPerBlock (juce::jmax (1, samplesPerBlock));
        }
    }

    void GGridAudioProcessor::releaseResources()
    {
        for (auto& slot : slots)
            slot->reset();

        masterLimiter.reset();
    }

    void GGridAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;

        for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear (ch, 0, buffer.getNumSamples());

        if (auto* currentPlayHead = getPlayHead())
        {
            if (auto position = currentPlayHead->getPosition())
            {
                if (auto bpm = position->getBpm())
                    currentBpm.store (*bpm);
            }
        }

        modulationMatrix.processMidi (midiMessages);

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        std::array<bool, kMaxSlots> active {};
        std::array<bool, kMaxSlots> isInputRole {};
        std::array<bool, kMaxSlots> isOutputRole {};
        // Topological roots: both Input (raw dry passthrough) and ThreeOsc (MIDI-driven
        // generator) start the processing order and seed graph reachability -- see
        // buildProcessingOrder's isRootRole parameter. They differ in what happens once a root's
        // turn comes up in the graph loop below (isInputRole gates that split), not in whether
        // they're a root at all.
        std::array<bool, kMaxSlots> isSourceRole {};
        for (int i = 0; i < kMaxSlots; ++i)
        {
            const auto type = slots[(size_t) i]->getActiveType();
            // LFO slots are excluded from the audio graph entirely -- they're modulation
            // sources, not audio processors (see LFOModule's class comment).
            active[(size_t) i] = (type != ModuleType::none) && (type != ModuleType::lfo);
            isInputRole[(size_t) i] = (type == ModuleType::input);
            isOutputRole[(size_t) i] = (type == ModuleType::output);
            isSourceRole[(size_t) i] = isInputRole[(size_t) i] || (type == ModuleType::threeOsc);
        }

        // Tick every active LFO slot once per block, before the graph below runs, so every
        // modulation destination reads a fresh value this block regardless of where an LFO would
        // otherwise fall in audio topological order (which has nothing to do with modulation
        // routing). RackSlot::process() is still what actually advances LFOModule's phase and
        // handles type-swap/prepare timing, so it's called normally here -- just against a
        // scratch buffer whose content LFOModule ignores entirely, rather than a real graph node.
        for (int i = 0; i < kMaxSlots; ++i)
        {
            if (slots[(size_t) i]->getActiveType() != ModuleType::lfo) continue;

            lfoScratchBuffer.setSize (numChannels, numSamples, false, false, true);
            juce::dsp::AudioBlock<float> lfoBlock (lfoScratchBuffer);
            slots[(size_t) i]->process (lfoBlock, midiMessages, modulationMatrix);

            if (auto* lfo = dynamic_cast<LFOModule*> (slots[(size_t) i]->getCurrentModule()))
                modulationMatrix.setLfoValue (i, lfo->getCurrentValue());
        }

        {
            const auto graph = buildProcessingOrder (connections, numConnections, active, isSourceRole, isOutputRole);

            for (int i = 0; i < kMaxSlots; ++i)
            {
                if (! active[(size_t) i]) continue;
                nodeBuffers[(size_t) i].setSize (numChannels, numSamples, false, false, true);
                nodeBuffers[(size_t) i].clear();
            }

            for (int idx = 0; idx < graph.orderCount; ++idx)
            {
                const int i = graph.order[(size_t) idx];

                if (graph.isRoot[(size_t) i] && isInputRole[(size_t) i])
                {
                    // Every Input-type slot is seeded with the plugin's raw dry input every
                    // block, regardless of what's patched into it.
                    for (int ch = 0; ch < numChannels; ++ch)
                        nodeBuffers[(size_t) i].copyFrom (ch, 0, buffer, ch, 0, numSamples);
                    continue;
                }

                // Every other node's buffer (including an Output-type slot, or a generator-type
                // root like ThreeOsc, which simply has no incoming edges to sum since it has no
                // input ports -- see canAddConnection) is built by summing its predecessors'
                // outputs, which the topological order guarantees have already run by the time
                // this node's turn comes up.
                for (int c = 0; c < numConnections; ++c)
                {
                    const auto& conn = connections[(size_t) c];
                    if (conn.to != i || ! active[(size_t) conn.from]) continue;

                    for (int ch = 0; ch < numChannels; ++ch)
                        nodeBuffers[(size_t) i].addFrom (ch, 0, nodeBuffers[(size_t) conn.from], ch, 0, numSamples);
                }

                if (graph.isSink[(size_t) i])
                    continue; // Output-type slots only collect -- no DSP module of their own to run.

                juce::dsp::AudioBlock<float> nodeBlock (nodeBuffers[(size_t) i]);
                slots[(size_t) i]->process (nodeBlock, midiMessages, modulationMatrix);
            }

            // Every Output-type slot's buffer sums into the final mix. If nothing's patched
            // through to any of them (including if there isn't one at all), the buffer stays
            // cleared and the plugin goes silent, matching Bitwig Grid's fully-explicit-patching
            // model.
            buffer.clear();
            for (int i = 0; i < kMaxSlots; ++i)
                if (graph.isSink[(size_t) i])
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.addFrom (ch, 0, nodeBuffers[(size_t) i], ch, 0, numSamples);

            // Feeds each Input/Output node's own on-canvas oscilloscope -- only for slots
            // actually playing that role right now, so this costs nothing for the far more
            // common case of a rack with no Input/Output nodes' scopes actually visible.
            for (int i = 0; i < kMaxSlots; ++i)
                if (isInputRole[(size_t) i] || isOutputRole[(size_t) i])
                    nodeScopes[(size_t) i]->pushBuffer (nodeBuffers[(size_t) i]);
        }

        juce::dsp::AudioBlock<float> block (buffer);

        // Safety limiter always runs last, after the full rack graph, regardless of routing --
        // it exists to protect ears/speakers/gear from an aggressively-pushed waveshaper (which
        // deliberately has no ceiling of its own), not to shape the sound.
        if (limiterEnabledParam->load() >= 0.5f)
        {
            masterLimiter.setThreshold (limiterCeilingParam->load());
            masterLimiter.setRelease (50.0f);
            juce::dsp::ProcessContextReplacing<float> context (block);
            masterLimiter.process (context);
        }

        outputScope.pushBuffer (buffer);
    }

    juce::AudioProcessorEditor* GGridAudioProcessor::createEditor()
    {
        return new GGridAudioProcessorEditor (*this);
    }

    void GGridAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto state = apvts.copyState();
        juce::XmlElement xml ("GGridState");

        if (auto paramsXml = state.createXml())
            xml.addChildElement (paramsXml.release());

        juce::StringArray connectionTokens;
        for (int i = 0; i < numConnections; ++i)
            connectionTokens.add (juce::String (connections[(size_t) i].from) + "-" + juce::String (connections[(size_t) i].to));
        xml.setAttribute ("connections", connectionTokens.joinIntoString (","));

        // "|" as the field separator (not "-") since destinationParamId is an arbitrary APVTS
        // parameter ID string, not a fixed enum -- kept unambiguous regardless of what future
        // parameter names look like.
        juce::StringArray modConnectionTokens;
        for (int i = 0; i < modulationMatrix.numModConnections; ++i)
        {
            const auto& conn = modulationMatrix.modConnections[(size_t) i];
            modConnectionTokens.add (juce::String (conn.fromSlot) + "|" + juce::String (conn.toSlot) + "|" + conn.destinationParamId);
        }
        xml.setAttribute ("modConnections", modConnectionTokens.joinIntoString (","));

        juce::StringArray positionTokens;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            positionTokens.add (juce::String (nodePositions[(size_t) i].x));
            positionTokens.add (juce::String (nodePositions[(size_t) i].y));
        }
        xml.setAttribute ("nodePositions", positionTokens.joinIntoString (","));

        copyXmlToBinary (xml, destData);
    }

    void GGridAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
        if (xml == nullptr || ! xml->hasTagName ("GGridState"))
            return;

        if (auto* paramsXml = xml->getChildElement (0))
            apvts.replaceState (juce::ValueTree::fromXml (*paramsXml));

        auto connectionTokens = juce::StringArray::fromTokens (xml->getStringAttribute ("connections"), ",", "");
        numConnections = 0;
        for (auto& token : connectionTokens)
        {
            auto parts = juce::StringArray::fromTokens (token, "-", "");
            if (parts.size() == 2 && numConnections < kMaxConnections)
                connections[(size_t) numConnections++] = { parts[0].getIntValue(), parts[1].getIntValue() };
        }

        auto modConnectionTokens = juce::StringArray::fromTokens (xml->getStringAttribute ("modConnections"), ",", "");
        modulationMatrix.numModConnections = 0;
        for (auto& token : modConnectionTokens)
        {
            auto parts = juce::StringArray::fromTokens (token, "|", "");
            if (parts.size() == 3 && modulationMatrix.numModConnections < kMaxModConnections)
                modulationMatrix.modConnections[(size_t) modulationMatrix.numModConnections++] = {
                    parts[0].getIntValue(), parts[1].getIntValue(), parts[2]
                };
        }

        // Note: saves from before Input/Output became ordinary addable/deletable module types
        // (this is still unreleased/actively-iterated software, see the project README) aren't
        // migrated -- connection endpoints referencing the old fixed pseudo-node IDs are simply
        // out of range for the current slot array and silently dropped below, so such a save
        // reopens with its modules in place but disconnected, needing Input/Output re-added and
        // everything rewired.
        auto positionTokens = juce::StringArray::fromTokens (xml->getStringAttribute ("nodePositions"), ",", "");
        if (positionTokens.size() == kMaxSlots * 2)
            for (int i = 0; i < kMaxSlots; ++i)
                nodePositions[(size_t) i] = { positionTokens[i * 2].getFloatValue(), positionTokens[i * 2 + 1].getFloatValue() };
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GGrid::GGridAudioProcessor();
}
