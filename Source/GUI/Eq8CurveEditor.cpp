#include "Eq8CurveEditor.h"
#include "GGridLookAndFeel.h"
#include "../Params/Identifiers.h"
#include "../Modules/Eq8Module.h"
#include <cmath>

namespace GGrid
{
    Eq8CurveEditor::Eq8CurveEditor (juce::AudioProcessorValueTreeState& apvts, int slotIndex,
                                     std::function<SpectrumAnalyzer*()> getAnalyzerIn)
        : getAnalyzer (std::move (getAnalyzerIn))
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            auto& r = bands[(size_t) b];
            r.freq = apvts.getParameter (eq8BandFreqParamId (slotIndex, b));
            r.gain = apvts.getParameter (eq8ParamId (slotIndex, eq8BandParam (b)));
            r.q = apvts.getParameter (eq8BandQParamId (slotIndex, b));
            r.typeParam = apvts.getParameter (eq8BandTypeParamId (slotIndex, b));
            r.enabledParam = apvts.getParameter (eq8BandEnabledParamId (slotIndex, b));
            r.type = apvts.getRawParameterValue (eq8BandTypeParamId (slotIndex, b));
            r.enabled = apvts.getRawParameterValue (eq8BandEnabledParamId (slotIndex, b));
        }

        setInterceptsMouseClicks (true, false);
        // 30Hz now (was 20) -- also drives performFFTIfReady() below, matching
        // CrossoverSplitBar's own spectrum redraw cadence, not just param-change polling.
        startTimerHz (30);
    }

    Eq8CurveEditor::~Eq8CurveEditor()
    {
        stopTimer();
    }

    void Eq8CurveEditor::timerCallback()
    {
        if (auto* analyzer = getAnalyzer ? getAnalyzer() : nullptr)
            analyzer->performFFTIfReady();

        // Params can move from automation/preset load, not just this component's own drags, and
        // the spectrum (when present) animates continuously regardless -- simplest to just
        // repaint unconditionally on a timer (matches CrossoverSplitBar's own approach) rather
        // than tracking last-seen values per band per param.
        repaint();
    }

    float Eq8CurveEditor::freqToX (float freqHz) const
    {
        const float logMin = std::log10 (minFreqHz), logMax = std::log10 (maxFreqHz);
        const float t = (std::log10 (juce::jlimit (minFreqHz, maxFreqHz, freqHz)) - logMin) / (logMax - logMin);
        return (float) getLocalBounds().getX() + t * (float) getWidth();
    }

    float Eq8CurveEditor::xToFreq (float x) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - (float) getLocalBounds().getX()) / (float) getWidth());
        const float logMin = std::log10 (minFreqHz), logMax = std::log10 (maxFreqHz);
        return std::pow (10.0f, logMin + t * (logMax - logMin));
    }

    float Eq8CurveEditor::gainToY (float gainDb) const
    {
        const float t = (juce::jlimit (minDb, maxDb, gainDb) - minDb) / (maxDb - minDb);
        return (float) getLocalBounds().getBottom() - t * (float) getHeight();
    }

    float Eq8CurveEditor::yToGain (float y) const
    {
        const float t = juce::jlimit (0.0f, 1.0f, ((float) getLocalBounds().getBottom() - y) / (float) getHeight());
        return minDb + t * (maxDb - minDb);
    }

    int Eq8CurveEditor::findNodeNear (juce::Point<float> pos) const
    {
        int best = -1;
        float bestDist = grabToleranceX;

        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            if (bands[(size_t) b].enabled->load() < 0.5f)
                continue;

            const float freq = bands[(size_t) b].freq->convertFrom0to1 (bands[(size_t) b].freq->getValue());
            const int type = (int) bands[(size_t) b].type->load();
            const float gain = eq8BandTypeHasGain (type) ? bands[(size_t) b].gain->convertFrom0to1 (bands[(size_t) b].gain->getValue()) : 0.0f;

            const juce::Point<float> nodePos (freqToX (freq), gainToY (gain));
            const float dist = nodePos.getDistanceFrom (pos);
            if (dist <= bestDist)
            {
                bestDist = dist;
                best = b;
            }
        }

        return best;
    }

    int Eq8CurveEditor::findFirstDisabledBand() const
    {
        for (int b = 0; b < kNumEq8Bands; ++b)
            if (bands[(size_t) b].enabled->load() < 0.5f)
                return b;

        return -1;
    }

    int Eq8CurveEditor::findNextEnabledBand (int afterBand) const
    {
        for (int offset = 1; offset <= kNumEq8Bands; ++offset)
        {
            const int candidate = (afterBand + offset) % kNumEq8Bands;
            if (bands[(size_t) candidate].enabled->load() >= 0.5f)
                return candidate;
        }

        return -1;
    }

    void Eq8CurveEditor::setParamPlain (juce::RangedAudioParameter* param, float value)
    {
        if (param == nullptr)
            return;

        param->setValueNotifyingHost (param->convertTo0to1 (param->getNormalisableRange().snapToLegalValue (value)));
    }

    void Eq8CurveEditor::beginDragForBand (int band)
    {
        if (band < 0)
            return;

        auto& refs = bands[(size_t) band];
        refs.freq->beginChangeGesture();
        if (eq8BandTypeHasGain ((int) refs.type->load()))
            refs.gain->beginChangeGesture();
        refs.q->beginChangeGesture();
        draggingBand = band;
    }

    void Eq8CurveEditor::endDragForBand (int band)
    {
        if (band < 0)
            return;

        auto& refs = bands[(size_t) band];
        refs.freq->endChangeGesture();
        if (eq8BandTypeHasGain ((int) refs.type->load()))
            refs.gain->endChangeGesture();
        refs.q->endChangeGesture();
    }

    void Eq8CurveEditor::createBandAt (int band, juce::Point<float> pos, bool startDragging)
    {
        if (band < 0)
            return;

        auto& refs = bands[(size_t) band];
        if (refs.enabledParam != nullptr) refs.enabledParam->beginChangeGesture();
        if (refs.typeParam != nullptr) refs.typeParam->beginChangeGesture();
        refs.freq->beginChangeGesture();
        refs.gain->beginChangeGesture();
        refs.q->beginChangeGesture();

        setParamPlain (refs.enabledParam, 1.0f);
        setParamPlain (refs.typeParam, 0.0f);
        setParamPlain (refs.freq, xToFreq (pos.x));
        setParamPlain (refs.gain, yToGain (pos.y));
        setParamPlain (refs.q, 1.0f);

        refs.q->endChangeGesture();
        refs.gain->endChangeGesture();
        refs.freq->endChangeGesture();
        if (refs.typeParam != nullptr) refs.typeParam->endChangeGesture();
        if (refs.enabledParam != nullptr) refs.enabledParam->endChangeGesture();

        selectedBand = band;
        if (onBandSelected)
            onBandSelected (selectedBand);

        if (startDragging)
            beginDragForBand (band);

        repaint();
    }

    void Eq8CurveEditor::disableBand (int band)
    {
        if (band < 0)
            return;

        auto& refs = bands[(size_t) band];
        if (refs.enabledParam == nullptr)
            return;

        refs.enabledParam->beginChangeGesture();
        setParamPlain (refs.enabledParam, 0.0f);
        refs.enabledParam->endChangeGesture();

        if (selectedBand == band)
        {
            const int next = findNextEnabledBand (band);
            if (next >= 0)
            {
                selectedBand = next;
                if (onBandSelected)
                    onBandSelected (selectedBand);
            }
        }

        repaint();
    }

    juce::Colour Eq8CurveEditor::colourForBand (int index) const
    {
        // 8 evenly-spaced hues around the wheel, anchored at the theme accent's own hue rather
        // than an arbitrary starting point, so the palette still reads as "this app's colours."
        return Palette::accent.withRotatedHue ((float) index / (float) kNumEq8Bands);
    }

    void Eq8CurveEditor::mouseDown (const juce::MouseEvent& e)
    {
        const int hit = findNodeNear (e.position);
        if (hit < 0)
            return;

        selectedBand = hit;
        if (onBandSelected)
            onBandSelected (selectedBand);

        beginDragForBand (hit);
        repaint();
    }

    void Eq8CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (draggingBand >= 0)
        {
            endDragForBand (draggingBand);
            draggingBand = -1;
        }

        const int hit = findNodeNear (e.position);
        if (hit < 0)
        {
            const int disabledBand = findFirstDisabledBand();
            createBandAt (disabledBand >= 0 ? disabledBand : selectedBand, e.position, true);
            return;
        }

        disableBand (hit);
    }

    void Eq8CurveEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (draggingBand < 0)
            return;

        auto& band = bands[(size_t) draggingBand];

        const float freq = xToFreq (e.position.x);
        const float snappedFreq = band.freq->getNormalisableRange().snapToLegalValue (freq);
        band.freq->setValueNotifyingHost (band.freq->convertTo0to1 (snappedFreq));

        if (e.mods.isAltDown() || e.mods.isCommandDown())
        {
            const float y01 = juce::jlimit (0.0f, 1.0f, ((float) getLocalBounds().getBottom() - e.position.y) / (float) getHeight());
            const float q = 0.1f * std::pow (180.0f, y01);
            const float snappedQ = band.q->getNormalisableRange().snapToLegalValue (q);
            band.q->setValueNotifyingHost (band.q->convertTo0to1 (snappedQ));
            repaint();
            return;
        }

        // Vertical movement only means anything for gain-shaped types (Bell/Low Shelf/High
        // Shelf) -- dragging a High Pass/Low Pass/Notch/Band Pass node only ever moves Frequency,
        // matching how those types have no meaningful Gain in the first place (see
        // eq8BandTypeHasGain).
        if (eq8BandTypeHasGain ((int) band.type->load()))
        {
            const float gain = yToGain (e.position.y);
            const float snappedGain = band.gain->getNormalisableRange().snapToLegalValue (gain);
            band.gain->setValueNotifyingHost (band.gain->convertTo0to1 (snappedGain));
        }

        repaint();
    }

    void Eq8CurveEditor::mouseUp (const juce::MouseEvent&)
    {
        if (draggingBand < 0)
            return;

        endDragForBand (draggingBand);
        draggingBand = -1;
        repaint();
    }

    void Eq8CurveEditor::mouseMove (const juce::MouseEvent& e)
    {
        const int newHovered = findNodeNear (e.position);
        if (newHovered != hoveredBand)
        {
            hoveredBand = newHovered;
            repaint();
        }
    }

    void Eq8CurveEditor::mouseExit (const juce::MouseEvent&)
    {
        if (hoveredBand != -1)
        {
            hoveredBand = -1;
            repaint();
        }
    }

    void Eq8CurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        const int target = findNodeNear (e.position);
        if (target < 0)
            return;

        auto& band = bands[(size_t) target];
        const float currentQ = band.q->convertFrom0to1 (band.q->getValue());
        const float newQ = juce::jlimit (0.1f, 18.0f, currentQ * (wheel.deltaY > 0.0f ? 1.12f : 1.0f / 1.12f));

        band.q->beginChangeGesture();
        band.q->setValueNotifyingHost (band.q->convertTo0to1 (newQ));
        band.q->endChangeGesture();
        repaint();
    }

    void Eq8CurveEditor::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (Palette::bg);
        g.fillRect (bounds);

        // Log-frequency gridlines.
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (float f : { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
        {
            const float x = freqToX (f);
            g.setColour (Palette::dimmer.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, bounds.getY(), bounds.getBottom());
            g.setColour (Palette::dim.withAlpha (0.7f));
            g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + "k" : juce::String ((int) f),
                        (int) x + 2, (int) bounds.getBottom() - 12, 40, 12, juce::Justification::left);
        }

        // Linear-dB gridlines, 0dB brighter.
        for (float d : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        {
            const float y = gainToY (d);
            g.setColour (juce::approximatelyEqual (d, 0.0f) ? Palette::dim.withAlpha (0.5f) : Palette::dimmer.withAlpha (0.4f));
            g.drawHorizontalLine ((int) y, bounds.getX(), bounds.getRight());
        }

        // Live spectrum of the incoming signal, drawn behind the response curve -- so you can see
        // what the EQ is actually doing to the material passing through it, not just the abstract
        // curve shape. Own independent dB scale (fixed -100..0, NOT gainToY's -12..+12 curve
        // axis -- program material would otherwise render as an imperceptibly thin sliver hugging
        // the bottom) mapped onto this same rectangle; matches CrossoverSplitBar's own spectrum
        // treatment exactly, including starting at bin 1 rather than skipping ahead a few bins --
        // see that class's own comment on why skipping further left content than necessary just
        // leaves a dead gap at the low end instead of fixing anything.
        if (auto* analyzer = getAnalyzer ? getAnalyzer() : nullptr)
        {
            const double sr = analyzer->getSampleRate();
            juce::Path linePath, fillPath;
            bool spectrumStarted = false;
            float lastX = bounds.getX();

            for (int i = 1; i < SpectrumAnalyzer::numBins; ++i)
            {
                const float freq = (float) (i * sr / SpectrumAnalyzer::fftSize);
                if (freq < minFreqHz || freq > maxFreqHz)
                    continue;

                const float x = freqToX (freq);
                const float db = juce::jlimit (-100.0f, 0.0f, analyzer->getMagnitudeDb (i));
                const float y = juce::jmap (db, -100.0f, 0.0f, bounds.getBottom(), bounds.getY());

                if (! spectrumStarted)
                {
                    linePath.startNewSubPath (x, y);
                    fillPath.startNewSubPath (x, bounds.getBottom());
                    fillPath.lineTo (x, y);
                    spectrumStarted = true;
                }
                else
                {
                    linePath.lineTo (x, y);
                    fillPath.lineTo (x, y);
                }
                lastX = x;
            }

            if (spectrumStarted)
            {
                fillPath.lineTo (lastX, bounds.getBottom());
                fillPath.closeSubPath();

                juce::ColourGradient gradient (Palette::accent.withAlpha (0.35f), 0, bounds.getY(),
                                                Palette::accent.withAlpha (0.01f), 0, bounds.getBottom(), false);
                g.setGradientFill (gradient);
                g.fillPath (fillPath);

                g.setColour (Palette::accent.withAlpha (0.5f));
                g.strokePath (linePath, juce::PathStrokeType (1.0f));
            }
        }

        // Combined response of every enabled band, sampled every 2px across the width -- the
        // exact same per-band coefficients Eq8Module::process() would compute this block, since
        // both call the same static Eq8Module::makeCoefficients (see class comment).
        juce::Path responsePath;
        bool started = false;
        for (float x = bounds.getX(); x <= bounds.getRight(); x += 2.0f)
        {
            const float freq = xToFreq (x);
            double totalMagnitude = 1.0;

            for (int b = 0; b < kNumEq8Bands; ++b)
            {
                const auto& band = bands[(size_t) b];
                if (band.enabled->load() < 0.5f)
                    continue;

                const int type = (int) band.type->load();
                const float bFreq = band.freq->convertFrom0to1 (band.freq->getValue());
                const float bQ = band.q->convertFrom0to1 (band.q->getValue());
                const float bGainDb = band.gain->convertFrom0to1 (band.gain->getValue());
                const float gainLinear = eq8BandTypeHasGain (type) ? juce::Decibels::decibelsToGain (bGainDb) : 1.0f;

                auto coeffs = Eq8Module::makeCoefficients (type, sampleRate, bFreq, bQ, gainLinear);
                totalMagnitude *= coeffs->getMagnitudeForFrequency (freq, sampleRate);
            }

            const float y = gainToY ((float) (20.0 * std::log10 (juce::jmax (1.0e-6, totalMagnitude))));
            if (! started) { responsePath.startNewSubPath (x, y); started = true; }
            else responsePath.lineTo (x, y);
        }
        g.setColour (Palette::bright.withAlpha (0.85f));
        g.strokePath (responsePath, juce::PathStrokeType (2.0f));

        // Per-band node markers -- disabled bands aren't drawn at all (matches how they're
        // excluded from hit-testing and the response curve above).
        for (int b = 0; b < kNumEq8Bands; ++b)
        {
            const auto& band = bands[(size_t) b];
            if (band.enabled->load() < 0.5f)
                continue;

            const int type = (int) band.type->load();
            const float freq = band.freq->convertFrom0to1 (band.freq->getValue());
            const float gain = eq8BandTypeHasGain (type) ? band.gain->convertFrom0to1 (band.gain->getValue()) : 0.0f;
            const juce::Point<float> pos (freqToX (freq), gainToY (gain));

            const bool isSelected = b == selectedBand;
            const bool isActive = b == hoveredBand || b == draggingBand;
            auto colour = colourForBand (b);

            if (isSelected)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre (pos));
            }

            g.setColour (colour.withAlpha (isActive ? 1.0f : 0.85f));
            g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (pos), 1.5f);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pos));
        }

        g.setColour (Palette::dim);
        g.drawRect (bounds, 1.0f);
    }

    void Eq8CurveEditor::resized()
    {
    }
}
