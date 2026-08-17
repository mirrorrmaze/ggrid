#include "EnvelopeModule.h"

namespace GGrid
{
    EnvelopeModule::EnvelopeModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn)
        : lengthParam (apvtsIn.getRawParameterValue (envelopeParamId (slotIndexIn, EnvelopeParam::length))),
          depthParam  (apvtsIn.getRawParameterValue (envelopeParamId (slotIndexIn, EnvelopeParam::depth)))
    {
        // A simple linear ramp by default -- an obviously-nonzero shape rather than a silent flat
        // line, without presuming any particular attack/decay character the user didn't ask for.
        points[0] = { 0.0f, 0.0f };
        points[1] = { 1.0f, 1.0f };
        numPoints = 2;
    }

    void EnvelopeModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        playheadTime.store (1.0f);
        playing.store (false);
        currentValue.store (0.0f);
    }

    void EnvelopeModule::reset()
    {
        playheadTime.store (1.0f);
        playing.store (false);
        currentValue.store (0.0f);
    }

    float EnvelopeModule::evaluateAt (float normalisedTime) const
    {
        // Caller (process()) already holds pointsLock.
        if (normalisedTime <= points[0].x)
            return points[0].y;

        for (int i = 1; i < numPoints; ++i)
        {
            if (normalisedTime <= points[(size_t) i].x)
            {
                const auto& a = points[(size_t) (i - 1)];
                const auto& b = points[(size_t) i];
                const float t = (b.x > a.x) ? (normalisedTime - a.x) / (b.x - a.x) : 0.0f;
                return a.y + t * (b.y - a.y);
            }
        }

        return points[(size_t) (numPoints - 1)].y;
    }

    void EnvelopeModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer& midi, const ModulationMatrix&)
    {
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn())
            {
                // Retrigger, regardless of whatever the shape was doing -- one-shot means every
                // new note-on restarts from the beginning. Note-off is deliberately never
                // consulted anywhere in this module -- see the class comment.
                playheadTime.store (0.0f);
                playing.store (true);
            }
        }

        const double lengthSeconds = juce::jmax (0.01, (double) lengthParam->load());
        // Block-rate advance, matching LFO/ADSR's own per-block granularity for a modulation
        // source's reported value -- the block's own sample count standing in for elapsed time.
        const double blockSeconds = (double) block.getNumSamples() / sampleRate;

        float newPlayhead = playheadTime.load();
        bool stillPlaying = playing.load();

        if (stillPlaying)
        {
            newPlayhead += (float) (blockSeconds / lengthSeconds);
            if (newPlayhead >= 1.0f)
            {
                newPlayhead = 1.0f;
                stillPlaying = false;
            }
            playheadTime.store (newPlayhead);
            playing.store (stillPlaying);
        }

        float shapeValue;
        {
            juce::ScopedLock lock (pointsLock);
            shapeValue = evaluateAt (newPlayhead);
        }

        const float depth = depthParam->load() / 100.0f;
        currentValue.store (shapeValue * depth);
    }

    void EnvelopeModule::writeExtraState (juce::XmlElement& parent) const
    {
        juce::ScopedLock lock (pointsLock);
        juce::StringArray tokens;
        for (int i = 0; i < numPoints; ++i)
            tokens.add (juce::String (points[(size_t) i].x, 6) + ":" + juce::String (points[(size_t) i].y, 6));
        parent.setAttribute ("envelopePoints", tokens.joinIntoString (","));
    }

    void EnvelopeModule::readExtraState (const juce::XmlElement& parent)
    {
        auto tokens = juce::StringArray::fromTokens (parent.getStringAttribute ("envelopePoints"), ",", "");

        std::array<juce::Point<float>, kMaxEnvelopePoints> newPoints;
        int newCount = 0;
        for (auto& token : tokens)
        {
            auto parts = juce::StringArray::fromTokens (token, ":", "");
            if (parts.size() == 2 && newCount < kMaxEnvelopePoints)
                newPoints[(size_t) newCount++] = { parts[0].getFloatValue(), parts[1].getFloatValue() };
        }

        // Malformed/empty saved state (or none at all) leaves the constructor's default shape in
        // place rather than adopting something unusable.
        if (newCount >= 2)
        {
            juce::ScopedLock lock (pointsLock);
            points = newPoints;
            numPoints = newCount;
        }
    }

    int EnvelopeModule::getNumPoints() const
    {
        juce::ScopedLock lock (pointsLock);
        return numPoints;
    }

    juce::Point<float> EnvelopeModule::getPoint (int index) const
    {
        juce::ScopedLock lock (pointsLock);
        if (index < 0 || index >= numPoints)
            return {};
        return points[(size_t) index];
    }

    int EnvelopeModule::addPoint (juce::Point<float> p)
    {
        juce::ScopedLock lock (pointsLock);

        if (numPoints >= kMaxEnvelopePoints)
            return -1;

        const float x = juce::jlimit (0.0f, 1.0f, p.x);
        const float y = juce::jlimit (0.0f, 1.0f, p.y);

        int insertAt = numPoints - 1; // always reachable -- points[last].x == 1 >= x
        for (int i = 0; i < numPoints; ++i)
        {
            if (points[(size_t) i].x >= x)
            {
                insertAt = i;
                break;
            }
        }

        // Reject if too close to either neighbour it would land between -- avoids a degenerate
        // zero-width segment and keeps drag hit-testing unambiguous. This also naturally rejects
        // landing on/before the first point or at/after the last (they're pinned at x=0/x=1, so
        // any x within minPointGap of either edge fails one of these checks).
        if (insertAt > 0 && (x - points[(size_t) (insertAt - 1)].x) < minPointGap)
            return -1;
        if ((points[(size_t) insertAt].x - x) < minPointGap)
            return -1;

        for (int i = numPoints; i > insertAt; --i)
            points[(size_t) i] = points[(size_t) (i - 1)];
        points[(size_t) insertAt] = { x, y };
        ++numPoints;

        return insertAt;
    }

    void EnvelopeModule::movePoint (int index, juce::Point<float> newPos)
    {
        juce::ScopedLock lock (pointsLock);
        if (index < 0 || index >= numPoints)
            return;

        const float y = juce::jlimit (0.0f, 1.0f, newPos.y);
        float x = points[(size_t) index].x;

        if (index == 0)
            x = 0.0f;
        else if (index == numPoints - 1)
            x = 1.0f;
        else
        {
            const float lo = points[(size_t) (index - 1)].x + minPointGap;
            const float hi = points[(size_t) (index + 1)].x - minPointGap;
            x = juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), newPos.x);
        }

        points[(size_t) index] = { x, y };
    }

    void EnvelopeModule::removePoint (int index)
    {
        juce::ScopedLock lock (pointsLock);
        if (index <= 0 || index >= numPoints - 1)
            return; // first/last points are permanent

        for (int i = index; i < numPoints - 1; ++i)
            points[(size_t) i] = points[(size_t) (i + 1)];
        --numPoints;
    }
}
