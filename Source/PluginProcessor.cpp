#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params/ParameterLayout.h"

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
            nodePositions[(size_t) i] = { 40.0f + (float) i * 30.0f, 40.0f + (float) i * 30.0f };
        }

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
        rawInputCopy.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        outputScope.clear();
        outputScope.setSamplesPerBlock (juce::jmax (1, samplesPerBlock));
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
        bool anyActive = false;
        for (int i = 0; i < kMaxSlots; ++i)
        {
            active[(size_t) i] = slots[(size_t) i]->getActiveType() != ModuleType::none;
            anyActive |= active[(size_t) i];
        }

        if (anyActive)
        {
            const auto graph = buildProcessingOrder (connections, numConnections, active);

            // Root nodes (no active predecessor) get a copy of the plugin's raw dry input; every
            // other node's buffer is built purely by summing its predecessors' outputs, which
            // the topological order guarantees have already run by the time this node's turn
            // comes up.
            rawInputCopy.setSize (numChannels, numSamples, false, false, true);
            for (int ch = 0; ch < numChannels; ++ch)
                rawInputCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

            for (int i = 0; i < kMaxSlots; ++i)
            {
                if (! active[(size_t) i]) continue;
                nodeBuffers[(size_t) i].setSize (numChannels, numSamples, false, false, true);
                nodeBuffers[(size_t) i].clear();
            }

            for (int idx = 0; idx < graph.orderCount; ++idx)
            {
                const int i = graph.order[(size_t) idx];

                if (graph.isRoot[(size_t) i])
                {
                    for (int ch = 0; ch < numChannels; ++ch)
                        nodeBuffers[(size_t) i].copyFrom (ch, 0, rawInputCopy, ch, 0, numSamples);
                }
                else
                {
                    for (int c = 0; c < numConnections; ++c)
                    {
                        const auto& conn = connections[(size_t) c];
                        if (conn.to != i || ! active[(size_t) conn.from]) continue;

                        for (int ch = 0; ch < numChannels; ++ch)
                            nodeBuffers[(size_t) i].addFrom (ch, 0, nodeBuffers[(size_t) conn.from], ch, 0, numSamples);
                    }
                }

                juce::dsp::AudioBlock<float> nodeBlock (nodeBuffers[(size_t) i]);
                slots[(size_t) i]->process (nodeBlock, midiMessages, modulationMatrix);
            }

            // Sink nodes (no active successor) sum into the final mix -- a DAG always has at
            // least one, so this can't silently drop the whole signal in normal use.
            buffer.clear();
            for (int i = 0; i < kMaxSlots; ++i)
                if (graph.isSink[(size_t) i])
                    for (int ch = 0; ch < numChannels; ++ch)
                        buffer.addFrom (ch, 0, nodeBuffers[(size_t) i], ch, 0, numSamples);
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
