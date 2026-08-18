#include "LFOModule.h"
#include "../Params/Identifiers.h"
#include <algorithm>
#include <cmath>

namespace GGrid
{
    LFOModule::LFOModule (juce::AudioProcessorValueTreeState& apvtsIn, int slotIndexIn, SharedServices& servicesIn)
        : services (servicesIn),
          shapeParam    (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::shape))),
          rateModeParam (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::rateMode))),
          rateHzParam   (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::rateHz))),
          divisionParam (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::division))),
          depthParam    (apvtsIn.getRawParameterValue (lfoParamId (slotIndexIn, LfoParam::depth))),
          random ((juce::int64) (juce::Random::getSystemRandom().nextInt64()))
    {
        customPoints[0] = { 0.0f,  0.0f,  0.0f };
        customPoints[1] = { 0.25f, 1.0f,  0.0f };
        customPoints[2] = { 0.5f,  0.0f,  0.0f };
        customPoints[3] = { 0.75f, -1.0f, 0.0f };
        customPoints[4] = { 1.0f,  0.0f,  0.0f };
    }

    void LFOModule::prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        phase = 0.0;
        sampleHoldValue = random.nextFloat() * 2.0f - 1.0f;
        currentValue.store (0.0f);
    }

    void LFOModule::reset()
    {
        phase = 0.0;
    }

    float LFOModule::computePresetShapeValue (int shape, float phase01, float sampleHoldFallback)
    {
        switch (shape)
        {
            case 0: // Sine
                return (float) std::sin (2.0 * juce::MathConstants<double>::pi * phase01);

            case 1: // Triangle -- -1 at phase 0, +1 at phase 0.5, back to -1 at phase 1
                return phase01 < 0.5f ? (4.0f * phase01 - 1.0f) : (3.0f - 4.0f * phase01);

            case 2: // Square
                return phase01 < 0.5f ? 1.0f : -1.0f;

            case 3: // Saw
                return 2.0f * phase01 - 1.0f;

            case 4: // Sample & Hold -- handled by the caller (holds a value across the block,
                    // only re-rolled on phase wrap); this branch shouldn't normally be reached.
                return sampleHoldFallback;

            case 5: // Ramp Up -- identical math to Saw, kept as a separate named entry (see
                    // getLfoShapeChoices' own comment)
                return 2.0f * phase01 - 1.0f;

            case 6: // Ramp Down -- the mirror image of Ramp Up/Saw
                return 1.0f - 2.0f * phase01;

            default:
                return 0.0f;
        }
    }

    float LFOModule::computeShapeValue (float phase01) const
    {
        const int shape = (int) shapeParam->load();
        if (customEdited.load() || shape == 7)
        {
            juce::ScopedLock lock (customPointsLock);
            return evaluateCustomAtLocked (phase01);
        }

        return computePresetShapeValue (shape, phase01, sampleHoldValue);
    }

    float LFOModule::evaluateCustomAtLocked (float phase01) const
    {
        const float wrappedPhase = phase01 - std::floor (phase01);

        if (wrappedPhase <= customPoints[0].x)
            return customPoints[0].y;

        for (int i = 1; i < numCustomPoints; ++i)
        {
            const auto& a = customPoints[(size_t) (i - 1)];
            const auto& b = customPoints[(size_t) i];
            if (wrappedPhase <= b.x)
            {
                const float linearT = (b.x > a.x) ? (wrappedPhase - a.x) / (b.x - a.x) : 0.0f;
                const float curve = juce::jlimit (-1.0f, 1.0f, a.curve);
                float shapedT = linearT;

                if (curve > 0.001f)
                    shapedT = 1.0f - std::pow (1.0f - linearT, 1.0f + curve * 4.0f);
                else if (curve < -0.001f)
                    shapedT = std::pow (linearT, 1.0f + (-curve) * 4.0f);

                return a.y + shapedT * (b.y - a.y);
            }
        }

        return customPoints[(size_t) (numCustomPoints - 1)].y;
    }

    float LFOModule::evaluateCustomAt (float phase01) const
    {
        juce::ScopedLock lock (customPointsLock);
        return evaluateCustomAtLocked (phase01);
    }

    void LFOModule::process (juce::dsp::AudioBlock<float>& block, juce::MidiBuffer&, const ModulationMatrix&)
    {
        // Deliberately does not touch the audio block's content at all -- see the class comment.
        const bool synced = ((int) rateModeParam->load()) == 1;

        double rateHz;
        if (synced)
        {
            const double quarterNoteSeconds = 60.0 / juce::jmax (1.0, services.hostBpm.load());
            const double divisionSeconds = quarterNoteSeconds * (double) getDelayDivisionMultiplier ((int) divisionParam->load());
            rateHz = 1.0 / juce::jmax (0.001, divisionSeconds);
        }
        else
        {
            rateHz = (double) rateHzParam->load();
        }

        // Block-rate tick: the block's total sample count stands in for elapsed time, so the
        // value is constant across this block and updates once per block rather than per sample.
        const double blockSeconds = (double) block.getNumSamples() / sampleRate;
        const double newPhase = phase + rateHz * blockSeconds;

        const bool wrapped = newPhase >= 1.0;
        phase = newPhase - std::floor (newPhase);

        if (wrapped && (int) shapeParam->load() == 4)
            sampleHoldValue = random.nextFloat() * 2.0f - 1.0f;

        const float depth = depthParam->load() / 100.0f;
        const float shapeValue = computeShapeValue ((float) phase);

        currentValue.store (shapeValue * depth);
    }

    void LFOModule::writeExtraState (juce::XmlElement& parent) const
    {
        juce::ScopedLock lock (customPointsLock);
        juce::StringArray tokens;
        for (int i = 0; i < numCustomPoints; ++i)
        {
            const auto& p = customPoints[(size_t) i];
            tokens.add (juce::String (p.x, 6) + ":" + juce::String (p.y, 6) + ":" + juce::String (p.curve, 6));
        }
        parent.setAttribute ("lfoCustomPoints", tokens.joinIntoString (","));
        parent.setAttribute ("lfoCustomEdited", customEdited.load());
    }

    void LFOModule::sortAndClampCustomPoints()
    {
        std::sort (customPoints.begin(), customPoints.begin() + numCustomPoints,
                   [] (const CustomPoint& a, const CustomPoint& b) { return a.x < b.x; });

        customPoints[0].x = 0.0f;
        customPoints[(size_t) (numCustomPoints - 1)].x = 1.0f;

        for (int i = 0; i < numCustomPoints; ++i)
        {
            customPoints[(size_t) i].y = juce::jlimit (-1.0f, 1.0f, customPoints[(size_t) i].y);
            customPoints[(size_t) i].curve = juce::jlimit (-1.0f, 1.0f, customPoints[(size_t) i].curve);
        }

        for (int i = 1; i < numCustomPoints - 1; ++i)
        {
            const float lo = customPoints[(size_t) (i - 1)].x + minPointGap;
            const float hi = customPoints[(size_t) (i + 1)].x - minPointGap;
            customPoints[(size_t) i].x = juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), customPoints[(size_t) i].x);
        }
    }

    void LFOModule::readExtraState (const juce::XmlElement& parent)
    {
        auto tokens = juce::StringArray::fromTokens (parent.getStringAttribute ("lfoCustomPoints"), ",", "");

        std::array<CustomPoint, kMaxEnvelopePoints> newPoints;
        int newCount = 0;
        for (auto& token : tokens)
        {
            auto parts = juce::StringArray::fromTokens (token, ":", "");
            if ((parts.size() == 2 || parts.size() == 3) && newCount < kMaxEnvelopePoints)
            {
                newPoints[(size_t) newCount].x = parts[0].getFloatValue();
                newPoints[(size_t) newCount].y = parts[1].getFloatValue();
                newPoints[(size_t) newCount].curve = parts.size() == 3 ? parts[2].getFloatValue() : 0.0f;
                ++newCount;
            }
        }

        if (newCount >= 2)
        {
            juce::ScopedLock lock (customPointsLock);
            customPoints = newPoints;
            numCustomPoints = newCount;
            sortAndClampCustomPoints();
        }

        customEdited.store (parent.getBoolAttribute ("lfoCustomEdited", false));
    }

    void LFOModule::seedCustomFromShape (int shapeIndex)
    {
        juce::ScopedLock lock (customPointsLock);

        auto setPoint = [this] (int index, float x, float y, float curve = 0.0f)
        {
            customPoints[(size_t) index] = { juce::jlimit (0.0f, 1.0f, x), juce::jlimit (-1.0f, 1.0f, y), curve };
        };

        switch (shapeIndex)
        {
            case 0: // Sine
                numCustomPoints = 17;
                for (int i = 0; i < numCustomPoints; ++i)
                {
                    const float x = (float) i / (float) (numCustomPoints - 1);
                    setPoint (i, x, computePresetShapeValue (0, x, 0.0f));
                }
                break;

            case 1: // Triangle
                numCustomPoints = 3;
                setPoint (0, 0.0f, -1.0f);
                setPoint (1, 0.5f, 1.0f);
                setPoint (2, 1.0f, -1.0f);
                break;

            case 2: // Square
                numCustomPoints = 4;
                setPoint (0, 0.0f, 1.0f);
                setPoint (1, 0.5f - minPointGap, 1.0f);
                setPoint (2, 0.5f + minPointGap, -1.0f);
                setPoint (3, 1.0f, -1.0f);
                break;

            case 3: // Saw
            case 5: // Ramp Up
                numCustomPoints = 2;
                setPoint (0, 0.0f, -1.0f);
                setPoint (1, 1.0f, 1.0f);
                break;

            case 4: // Sample & Hold
                numCustomPoints = 16;
                for (int i = 0; i < numCustomPoints; ++i)
                {
                    const float x = (float) i / (float) (numCustomPoints - 1);
                    const int step = juce::jlimit (0, 15, (int) std::floor (x * 16.0f));
                    const float y = juce::Random ((juce::int64) (0x51f0 + step * 7919)).nextFloat() * 2.0f - 1.0f;
                    setPoint (i, x, y);
                }
                break;

            case 6: // Ramp Down
                numCustomPoints = 2;
                setPoint (0, 0.0f, 1.0f);
                setPoint (1, 1.0f, -1.0f);
                break;

            default:
                break;
        }

        sortAndClampCustomPoints();
    }

    int LFOModule::getNumCustomPoints() const
    {
        juce::ScopedLock lock (customPointsLock);
        return numCustomPoints;
    }

    LFOModule::CustomPoint LFOModule::getCustomPoint (int index) const
    {
        juce::ScopedLock lock (customPointsLock);
        if (index < 0 || index >= numCustomPoints)
            return {};
        return customPoints[(size_t) index];
    }

    int LFOModule::addCustomPoint (juce::Point<float> p)
    {
        juce::ScopedLock lock (customPointsLock);
        if (numCustomPoints >= kMaxEnvelopePoints)
            return -1;

        const float x = juce::jlimit (0.0f, 1.0f, p.x);
        const float y = juce::jlimit (-1.0f, 1.0f, p.y);

        int insertAt = numCustomPoints - 1;
        for (int i = 0; i < numCustomPoints; ++i)
            if (customPoints[(size_t) i].x >= x)
            {
                insertAt = i;
                break;
            }

        if (insertAt > 0 && (x - customPoints[(size_t) (insertAt - 1)].x) < minPointGap)
            return -1;
        if ((customPoints[(size_t) insertAt].x - x) < minPointGap)
            return -1;

        for (int i = numCustomPoints; i > insertAt; --i)
            customPoints[(size_t) i] = customPoints[(size_t) (i - 1)];

        customPoints[(size_t) insertAt] = { x, y, 0.0f };
        ++numCustomPoints;
        return insertAt;
    }

    void LFOModule::moveCustomPoint (int index, juce::Point<float> p)
    {
        juce::ScopedLock lock (customPointsLock);
        if (index < 0 || index >= numCustomPoints)
            return;

        const float y = juce::jlimit (-1.0f, 1.0f, p.y);
        float x = customPoints[(size_t) index].x;

        if (index == 0)
            x = 0.0f;
        else if (index == numCustomPoints - 1)
            x = 1.0f;
        else
        {
            const float lo = customPoints[(size_t) (index - 1)].x + minPointGap;
            const float hi = customPoints[(size_t) (index + 1)].x - minPointGap;
            x = juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), p.x);
        }

        customPoints[(size_t) index].x = x;
        customPoints[(size_t) index].y = y;
    }

    void LFOModule::removeCustomPoint (int index)
    {
        juce::ScopedLock lock (customPointsLock);
        if (index <= 0 || index >= numCustomPoints - 1)
            return;

        for (int i = index; i < numCustomPoints - 1; ++i)
            customPoints[(size_t) i] = customPoints[(size_t) (i + 1)];
        --numCustomPoints;
    }

    void LFOModule::setSegmentCurve (int startPointIndex, float curve)
    {
        juce::ScopedLock lock (customPointsLock);
        if (startPointIndex < 0 || startPointIndex >= numCustomPoints - 1)
            return;

        customPoints[(size_t) startPointIndex].curve = juce::jlimit (-1.0f, 1.0f, curve);
    }
}
