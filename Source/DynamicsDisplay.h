#pragma once

#include "Graphs.h"
#include "Dynamics.h"
#include "ParamKnob.h"

/** Displays for Pulse Compressor, Limiter and Multiband Dynamics. Everything that is drawn as a
    line or a point can be dragged, and moves the matching knob. */
namespace DynamicsUI
{
    using Param = Graphs::Param;

    /** Shared: a scrolling level history (input, output, gain reduction) with a draggable line. */
    inline void drawHistory (Graphics& g, Rectangle<float> area, const pulse::LevelHistory& h, float floorDb, Colour outCol, bool large)
    {
        auto yFor = [&] (float db) { return area.getY() + (0.0f - jlimit (floorDb, 0.0f, db)) / -floorDb * area.getHeight(); };
        // grid
        for (float db = 0; db >= floorDb; db -= (large ? 6.0f : 12.0f))
        {
            g.setColour (Theme::ink.withAlpha (db == 0 ? 0.12f : 0.04f));
            g.drawHorizontalLine ((int) yFor (db), area.getX(), area.getRight());
            if (large) Graphs::label (g, String ((int) db), { area.getX() - 30.0f, yFor (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.6f);
        }
        const int n = pulse::LevelHistory::size;
        const int w = h.write.load();
        const float step = area.getWidth() / (float) (n - 1);
        Path inP, outP, grP;
        inP.startNewSubPath (area.getX(), area.getBottom());
        outP.startNewSubPath (area.getX(), area.getBottom());
        grP.startNewSubPath (area.getX(), area.getY());
        for (int i = 0; i < n; ++i)
        {
            const int idx = (w + i) % n;
            const float x = area.getX() + step * i;
            inP.lineTo (x, yFor (h.in[(size_t) idx].load()));
            outP.lineTo (x, yFor (h.out[(size_t) idx].load()));
            grP.lineTo (x, area.getY() + jlimit (0.0f, 1.0f, h.gr[(size_t) idx].load() / 24.0f) * area.getHeight() * 0.5f);
        }
        inP.lineTo (area.getRight(), area.getBottom()); inP.closeSubPath();
        outP.lineTo (area.getRight(), area.getBottom()); outP.closeSubPath();
        grP.lineTo (area.getRight(), area.getY()); grP.closeSubPath();
        g.setColour (Theme::ink.withAlpha (0.10f));
        g.fillPath (inP);
        g.setGradientFill (ColourGradient (outCol.withAlpha (0.42f), 0.0f, area.getY(), outCol.withAlpha (0.06f), 0.0f, area.getBottom(), false));
        g.fillPath (outP);
        g.setGradientFill (ColourGradient (Colour (0xffff5a4a).withAlpha (0.45f), 0.0f, area.getY(), Colour (0xffff5a4a).withAlpha (0.05f), 0.0f, area.getY() + area.getHeight() * 0.5f, false));
        g.fillPath (grP);
        {
            Path grLine;
            for (int i = 0; i < n; ++i)
            {
                const int idx = (w + i) % n;
                const float x = area.getX() + step * i, y = area.getY() + jlimit (0.0f, 1.0f, h.gr[(size_t) idx].load() / 24.0f) * area.getHeight() * 0.5f;
                if (i == 0) grLine.startNewSubPath (x, y); else grLine.lineTo (x, y);
            }
            g.setColour (Colour (0xffff5a4a));
            g.strokePath (grLine, PathStrokeType (1.6f));
        }
    }

    //==============================================================================
    /** Compressor: level history with the threshold line, the transfer curve (threshold, ratio and
        knee handles) and a gain-reduction meter. */
    class CompressorView : public Component, private Timer
    {
    public:
        using C = pulse::ProCompressor;
        explicit CompressorView (C& c) : comp (c), holder (&c) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            large = Graphs::isLarge (*this);
            if (large) Graphs::proDisplay (g, b); else Graphs::panel (g, b);
            auto r = b.reduced (large ? 10.0f : 6.0f);
            meter = r.removeFromRight (large ? 18.0f : 10.0f);
            r.removeFromRight (6.0f);
            const bool showCurve = r.getWidth() > 300.0f;
            curveArea = showCurve ? r.removeFromRight (jmin (r.getHeight(), r.getWidth() * 0.4f)).reduced (large ? 22.0f : 4.0f, large ? 14.0f : 4.0f) : Rectangle<float>();
            r.removeFromRight (showCurve ? 8.0f : 0.0f);
            hist = r.withTrimmedLeft (large ? 32.0f : 0.0f).withTrimmedBottom (large ? 14.0f : 0.0f).withTrimmedTop (large ? 14.0f : 10.0f);

            drawHistory (g, hist, comp.history, -60.0f, Theme::accent, large);
            // range: how deep the reduction may go (a line in the gain-reduction area, 0..24 dB shown)
            if (comp.get (C::pRange) < 24.0f)
            {
                const float ry = rangeY();
                g.setColour (hot == 3 || drag == 3 ? Theme::ink : Theme::orange);
                g.drawHorizontalLine ((int) ry, hist.getX(), hist.getRight());
                Graphs::label (g, "Range " + comp.param (C::pRange)->getCurrentValueAsString(), { hist.getRight() - 110.0f, ry + 1.0f, 106.0f, 12.0f }, Justification::centredRight, 0.9f);
            }
            // threshold line
            const float ty = yHist (comp.get (C::pThreshold));
            g.setColour (hot == 0 || drag == 0 ? Theme::ink : Theme::yellow);
            const float dash[] = { 5.0f, 3.0f };
            g.drawDashedLine (Line<float> (hist.getX(), ty, hist.getRight(), ty), dash, 2, 1.5f);
            Graphs::label (g, "Threshold " + comp.param (C::pThreshold)->getCurrentValueAsString(), { hist.getX() + 4.0f, ty - 14.0f, 160.0f, 12.0f }, Justification::centredLeft, 0.9f);
            if (large) Graphs::label (g, "Input (grey)   Output (blue)   Gain reduction (red, from the top)", hist.withHeight (12.0f).translated (0, -13.0f), Justification::centredLeft, 0.6f);

            if (! curveArea.isEmpty()) drawCurve (g);
            drawMeter (g);
        }

        void drawCurve (Graphics& g)
        {
            auto a = curveArea;
            g.setColour (Pro::shade (0.35f));
            g.fillRoundedRectangle (a.expanded (4.0f), 4.0f);
            for (float db = -60; db <= 0; db += 12)
            {
                g.setColour (Theme::ink.withAlpha (db == 0 ? 0.12f : 0.05f));
                g.drawVerticalLine ((int) xCurve (db), a.getY(), a.getBottom());
                g.drawHorizontalLine ((int) yCurve (db), a.getX(), a.getRight());
                if (large)
                {
                    Graphs::label (g, String ((int) db), { xCurve (db) - 12.0f, a.getBottom() + 2.0f, 24.0f, 11.0f }, Justification::centred, 0.55f);
                    Graphs::label (g, String ((int) db), { a.getX() - 22.0f, yCurve (db) - 6.0f, 20.0f, 12.0f }, Justification::centredRight, 0.55f);
                }
            }
            g.setColour (Theme::ink.withAlpha (0.1f));
            g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
            // knee zone
            const float th = comp.get (C::pThreshold), knee = comp.get (C::pKnee);
            if (knee > 0.1f)
            {
                g.setColour (Theme::yellow.withAlpha (0.08f));
                g.fillRect (Rectangle<float> (xCurve (th - knee * 0.5f), a.getY(), xCurve (th + knee * 0.5f) - xCurve (th - knee * 0.5f), a.getHeight()));
            }
            Path p;
            for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
            {
                const float in = dbCurveX (x);
                const float y = jlimit (a.getY(), a.getBottom(), yCurve (comp.transfer (in)));
                if (x == a.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Graphs::glowStroke (g, p, Theme::accent, 2.0f);
            // live point: the newest input level
            const int w = (comp.history.write.load() - 1 + pulse::LevelHistory::size) % pulse::LevelHistory::size;
            const float liveIn = comp.history.in[(size_t) w].load();
            if (liveIn > -60.0f)
            {
                const float lx = xCurve (liveIn), ly = yCurve (comp.transfer (liveIn));
                g.setColour (Theme::orange);
                g.fillEllipse (lx - 3.5f, ly - 3.5f, 7.0f, 7.0f);
            }
            Graphs::node (g, thresholdNode(), Theme::yellow, hot == 1 || drag == 1);
            Graphs::node (g, makeupNode(), Theme::green, hot == 4 || drag == 4);   // drag up / down: makeup gain
            if (large) Graphs::label (g, "Makeup " + comp.param (C::pMakeup)->getCurrentValueAsString(), { makeupNode().x - 90.0f, makeupNode().y + 8.0f, 86.0f, 12.0f },
                                      Justification::centredRight, 0.8f);
            if (knee > 0.1f)
                for (float side : { -1.0f, 1.0f })
                {
                    const float kx = xCurve (th + side * knee * 0.5f), ky = yCurve (comp.transfer (th + side * knee * 0.5f));
                    g.setColour (hot == 2 || drag == 2 ? Theme::ink : Theme::yellow.withAlpha (0.7f));
                    g.fillRect (kx - 3.0f, ky - 3.0f, 6.0f, 6.0f);
                }
            Graphs::label (g, comp.param (C::pRatio)->getCurrentValueAsString() + "   knee " + comp.param (C::pKnee)->getCurrentValueAsString(),
                           a.reduced (4.0f).removeFromTop (12.0f), Justification::topLeft, 0.85f);
        }

        void drawMeter (Graphics& g)
        {
            g.setColour (Theme::well.darker (0.2f));
            g.fillRoundedRectangle (meter, 2.0f);
            const float gr = jlimit (0.0f, 24.0f, comp.gainReductionDb.load());
            g.setColour (Colour (0xffff5a4a));
            g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * gr / 24.0f), 2.0f);
            if (large)
                Graphs::label (g, String (-gr, 1), meter.withHeight (14.0f).translated (0, meter.getHeight() + 0.0f).expanded (8.0f, 0.0f), Justification::centred, 0.9f);
        }

        // coordinate helpers
        float yHist (float db) const    { return hist.getY() + (-jlimit (-60.0f, 0.0f, db)) / 60.0f * hist.getHeight(); }
        float dbHist (float y) const    { return -(y - hist.getY()) / hist.getHeight() * 60.0f; }
        float xCurve (float db) const   { return curveArea.getX() + (db + 60.0f) / 60.0f * curveArea.getWidth(); }
        float yCurve (float db) const   { return curveArea.getBottom() - (jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f * curveArea.getHeight(); }
        float dbCurveX (float x) const  { return (x - curveArea.getX()) / curveArea.getWidth() * 60.0f - 60.0f; }
        juce::Point<float> thresholdNode() const
        {
            const float th = comp.get (C::pThreshold);
            return { xCurve (th), yCurve (comp.transfer (th)) };
        }
        juce::Point<float> makeupNode() const { return { curveArea.getRight() - 6.0f, jlimit (curveArea.getY() + 6.0f, curveArea.getBottom() - 6.0f, yCurve (comp.transfer (-6.0f))) }; }
        float rangeY() const { return hist.getY() + jlimit (0.0f, 1.0f, comp.get (C::pRange) / 24.0f) * hist.getHeight() * 0.5f; }

        int hitTest (juce::Point<float> p) const
        {
            if (! curveArea.isEmpty())
            {
                if (thresholdNode().getDistanceFrom (p) < 10.0f) return 1;
                const float th = comp.get (C::pThreshold), knee = comp.get (C::pKnee);
                if (knee > 0.1f && curveArea.contains (p) && (std::abs (p.x - xCurve (th - knee * 0.5f)) < 6.0f || std::abs (p.x - xCurve (th + knee * 0.5f)) < 6.0f)) return 2;
            }
            if (! curveArea.isEmpty() && makeupNode().getDistanceFrom (p) < 10.0f) return 4;
            if (hist.expanded (0, 4).contains (p) && std::abs (p.y - yHist (comp.get (C::pThreshold))) < 6.0f) return 0;
            if (hist.contains (p) && comp.get (C::pRange) < 24.0f && std::abs (p.y - rangeY()) < 5.0f) return 3;
            return -1;
        }

        void mouseMove (const MouseEvent& e) override
        {
            const int h = hitTest (e.position);
            if (h != hot) { hot = h; repaint(); }
            setMouseCursor (h == 0 || h == 3 || h == 4 ? MouseCursor::UpDownResizeCursor : h == 2 ? MouseCursor::LeftRightResizeCursor : h == 1 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        }
        void mouseExit (const MouseEvent&) override { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            drag = hitTest (e.position);
            if (drag < 0) return;
            comp.edit.getUndoManager().beginNewTransaction ("Compressor");
            startRatio = Graphs::norm (comp.param (C::pRatio));
            startMakeup = comp.get (C::pMakeup);
            Graphs::begin ({ comp.param (C::pThreshold), comp.param (C::pRatio), comp.param (C::pKnee), comp.param (C::pRange), comp.param (C::pMakeup) });
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (drag == 0) Graphs::setValue (comp.param (C::pThreshold), jlimit (-60.0f, 0.0f, dbHist (e.position.y)));
            if (drag == 1)
            {
                Graphs::setValue (comp.param (C::pThreshold), jlimit (-60.0f, 0.0f, dbCurveX (e.position.x)));
                if (e.mouseWasDraggedSinceMouseDown()) Graphs::setNorm (comp.param (C::pRatio), startRatio - e.getDistanceFromDragStartY() / 150.0f);
            }
            if (drag == 2) Graphs::setValue (comp.param (C::pKnee), jlimit (0.0f, 24.0f, std::abs (dbCurveX (e.position.x) - comp.get (C::pThreshold)) * 2.0f));
            if (drag == 3) Graphs::setValue (comp.param (C::pRange), jlimit (0.0f, 24.0f, (e.position.y - hist.getY()) / (hist.getHeight() * 0.5f) * 24.0f));
            if (drag == 4) Graphs::setValue (comp.param (C::pMakeup), jlimit (-12.0f, 36.0f, startMakeup - e.getDistanceFromDragStartY() / curveArea.getHeight() * 60.0f));
            repaint();
        }
        void mouseUp (const MouseEvent&) override
        {
            if (drag >= 0) Graphs::end ({ comp.param (C::pThreshold), comp.param (C::pRatio), comp.param (C::pKnee), comp.param (C::pRange), comp.param (C::pMakeup) });
            drag = -1;
        }
        void mouseDoubleClick (const MouseEvent&) override
        {
            comp.edit.getUndoManager().beginNewTransaction ("Reset");
            Graphs::reset (comp.param (C::pThreshold)); Graphs::reset (comp.param (C::pRatio)); Graphs::reset (comp.param (C::pKnee));
        }

    private:
        void timerCallback() override { repaint(); }
        C& comp;
        te::Plugin::Ptr holder;
        Rectangle<float> hist, curveArea, meter;
        bool large = false;
        int hot = -1, drag = -1;
        float startRatio = 0, startMakeup = 0;
    };

    //==============================================================================
    /** Limiter: level history with a draggable ceiling, gain reduction, and loudness readouts
        (short-term and integrated LUFS, true peak; click them to reset). */
    class LimiterView : public Component, private Timer
    {
    public:
        using L = pulse::Limiter;
        explicit LimiterView (L& l) : lim (l), holder (&l) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            large = Graphs::isLarge (*this);
            if (large) Graphs::proDisplay (g, b); else Graphs::panel (g, b);
            auto r = b.reduced (large ? 10.0f : 6.0f);
            readouts = r.removeFromRight (large ? 150.0f : 78.0f);
            r.removeFromRight (6.0f);
            hist = r.withTrimmedLeft (large ? 32.0f : 0.0f).withTrimmedTop (large ? 14.0f : 4.0f);
            drawHistory (g, hist, lim.history, -36.0f, Theme::green, large);
            // input gain: an arrow at the left edge, drag it up / down
            const float gy = gainY();
            Path arrow;
            arrow.addTriangle (hist.getX(), gy - 7.0f, hist.getX() + 12.0f, gy, hist.getX(), gy + 7.0f);
            g.setColour (hotGain || dragGain ? Theme::ink : Theme::accent);
            g.fillPath (arrow);
            Graphs::label (g, "Gain +" + String (lim.get (L::pGain), 1) + " dB", { hist.getX() + 14.0f, gy - 6.0f, 120.0f, 12.0f }, Justification::centredLeft, 0.9f);
            const float cy = yHist (lim.get (L::pCeiling));
            g.setColour (hot || dragging ? Theme::ink : Theme::red);
            g.drawLine (hist.getX(), cy, hist.getRight(), cy, 1.5f);
            Graphs::label (g, "Ceiling " + lim.param (L::pCeiling)->getCurrentValueAsString() + (lim.getInt (L::pTruePeak) > 0 ? " (true peak)" : ""),
                           { hist.getX() + 4.0f, cy + 2.0f, 220.0f, 12.0f }, Justification::centredLeft, 0.9f);

            auto ro = readouts;
            auto line = [&] (const String& name, const String& value, Colour c)
            {
                auto row = ro.removeFromTop (large ? 40.0f : 26.0f);
                Graphs::label (g, name, row.removeFromTop (large ? 14.0f : 10.0f), Justification::centredLeft, 0.7f);
                g.setColour (c);
                g.setFont (FontOptions (large ? 19.0f : 12.0f, Font::bold));
                g.drawText (value, row, Justification::centredLeft);
            };
            auto lufs = [] (float v) { return v <= -199.0f ? String ("--") : String (v, 1); };
            const float gr = lim.gainReductionDb.load(), tp = lim.truePeakDb.load();
            line ("Gain reduction", String (-gr, 1) + " dB", Theme::orange);
            line ("Short-term", lufs (lim.shortTermLufs.load()) + " LUFS", Theme::text);
            line ("Integrated", lufs (lim.integratedLufs.load()) + " LUFS", Theme::accent);
            line ("True peak", (tp <= -199.0f ? String ("--") : String (tp, 1)) + " dBTP", tp > -1.0f ? Theme::red : Theme::green);
            if (large)
            {
                g.setColour (Theme::muted.withAlpha (0.6f));
                g.setFont (FontOptions (10.5f));
                g.drawFittedText ("Click the readouts to reset them. Drag the red line to set the ceiling.", ro.removeFromTop (44.0f).toNearestInt(), Justification::topLeft, 3);
            }
        }

        float yHist (float db) const { return hist.getY() + (-jlimit (-36.0f, 0.0f, db)) / 36.0f * hist.getHeight(); }
        float gainY() const { return hist.getBottom() - lim.get (L::pGain) / 24.0f * hist.getHeight() * 0.9f - 8.0f; }
        bool overGain (juce::Point<float> p) const { return p.x < hist.getX() + 60.0f && std::abs (p.y - gainY()) < 9.0f; }
        float dbHist (float y) const { return -(y - hist.getY()) / hist.getHeight() * 36.0f; }

        void mouseMove (const MouseEvent& e) override
        {
            const bool h = hist.contains (e.position) && std::abs (e.position.y - yHist (lim.get (L::pCeiling))) < 6.0f;
            hotGain = overGain (e.position);
            if (h != hot) { hot = h; }
            repaint();
            setMouseCursor (h || hotGain ? MouseCursor::UpDownResizeCursor : readouts.contains (e.position) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
        }
        void mouseDown (const MouseEvent& e) override
        {
            if (readouts.contains (e.position)) { lim.resetLoudness = true; return; }
            dragGain = overGain (e.position);
            if (dragGain) { lim.edit.getUndoManager().beginNewTransaction ("Limiter gain"); startGain = lim.get (L::pGain); Graphs::begin ({ lim.param (L::pGain) }); return; }
            dragging = hist.contains (e.position) && std::abs (e.position.y - yHist (lim.get (L::pCeiling))) < 8.0f;
            if (! dragging) return;
            lim.edit.getUndoManager().beginNewTransaction ("Ceiling");
            Graphs::begin ({ lim.param (L::pCeiling) });
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging) Graphs::setValue (lim.param (L::pCeiling), jlimit (-12.0f, 0.0f, dbHist (e.position.y)));
            if (dragGain) Graphs::setValue (lim.param (L::pGain), jlimit (0.0f, 24.0f, startGain - e.getDistanceFromDragStartY() / (hist.getHeight() * 0.9f) * 24.0f));
        }
        void mouseUp (const MouseEvent&) override
        {
            if (dragging) Graphs::end ({ lim.param (L::pCeiling) });
            if (dragGain) Graphs::end ({ lim.param (L::pGain) });
            dragging = dragGain = false;
        }

    private:
        void timerCallback() override { repaint(); }
        L& lim;
        te::Plugin::Ptr holder;
        Rectangle<float> hist, readouts;
        bool large = false, hot = false, dragging = false, hotGain = false, dragGain = false;
        float startGain = 0;
    };

    //==============================================================================
    /** Multiband: the analyser across the spectrum, the bands as coloured regions with draggable
        crossovers, each band's threshold as a draggable line and its gain change as a bar. */
    class MultibandView : public Component, private Timer
    {
    public:
        using M = pulse::MultibandDynamics;
        explicit MultibandView (M& m) : mb (m), holder (&m), fft (12) { spec.fill (-120.0f); startTimerHz (30); }

        static Colour bandColour (int b) { const uint32 c[] = { 0xffff6b6b, 0xffffd43b, 0xff69db7c, 0xff4dabf7 }; return Colour (c[b % 4]); }

        float xFor (double f) const  { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); }
        double freqAt (float x) const { return 20.0 * std::pow (1000.0, (x - plot.getX()) / plot.getWidth()); }
        float yFor (float db) const   { return plot.getY() + (-jlimit (-60.0f, 0.0f, db)) / 60.0f * plot.getHeight(); }
        float dbAt (float y) const    { return -(y - plot.getY()) / plot.getHeight() * 60.0f; }

        std::vector<std::pair<float, float>> bandRanges() const
        {
            const auto x = mb.crossovers();
            const int nb = mb.numBands();
            std::vector<float> edges { plot.getX() };
            for (int i = 0; i < nb - 1; ++i) edges.push_back (xFor (x[(size_t) i]));
            edges.push_back (plot.getRight());
            std::vector<std::pair<float, float>> out;
            for (int b = 0; b < nb; ++b) out.push_back ({ edges[(size_t) b], edges[(size_t) b + 1] });
            return out;
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            large = Graphs::isLarge (*this);
            if (large) Graphs::proDisplay (g, b); else Graphs::panel (g, b);
            plot = b.reduced (large ? 34.0f : 8.0f, large ? 22.0f : 8.0f).withTrimmedBottom (large ? 0.0f : 10.0f);

            // analyser
            Path sp;
            for (int i = 0; i <= (int) plot.getWidth(); i += 2)
            {
                const double f = freqAt (plot.getX() + (float) i);
                const int bin = jlimit (1, fftSize / 2 - 1, (int) (f / 48000.0 * fftSize));
                const float db = spec[(size_t) bin] + 4.5f * (float) std::log2 (f / 1000.0);
                const float y = jlimit (plot.getY(), plot.getBottom(), plot.getBottom() - (db + 90.0f) / 90.0f * plot.getHeight());
                if (i == 0) { sp.startNewSubPath (plot.getX(), plot.getBottom()); }
                sp.lineTo (plot.getX() + (float) i, y);
            }
            sp.lineTo (plot.getRight(), plot.getBottom()); sp.closeSubPath();
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.fillPath (sp);

            // frequency labels
            for (auto [f, t] : { std::pair<double, const char*> { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" }, { 1000.0, "1k" },
                                 { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" } })
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
                Graphs::label (g, t, { xFor (f) - 14.0f, plot.getBottom() + 1.0f, 28.0f, 11.0f }, Justification::centred, 0.6f);
            }
            if (large)
                for (int db = 0; db >= -60; db -= 12)
                    Graphs::label (g, String (db), { plot.getX() - 30.0f, yFor ((float) db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.6f);

            const auto ranges = bandRanges();
            for (int bi = 0; bi < (int) ranges.size(); ++bi)
            {
                auto [x0, x1] = ranges[(size_t) bi];
                const auto c = bandColour (bi);
                const bool on = mb.getInt (M::idx (bi, M::bOn)) > 0;
                auto region = Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight());
                {
                    // the band's own response (Linkwitz-Riley edges) as a soft shape
                    Path shape;
                    const auto xs = mb.crossovers();
                    const int nb = mb.numBands();
                    shape.startNewSubPath (plot.getX(), plot.getBottom());
                    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
                    {
                        const double f = freqAt (x);
                        double mag = 1.0;
                        if (bi > 0)      { const double r = f / xs[(size_t) bi - 1]; mag *= (r * r * r * r) / (1.0 + r * r * r * r); }
                        if (bi < nb - 1) { const double r = f / xs[(size_t) bi];     mag *= 1.0 / (1.0 + r * r * r * r); }
                        shape.lineTo (x, plot.getBottom() - (float) mag * plot.getHeight() * 0.92f);
                    }
                    shape.lineTo (plot.getRight(), plot.getBottom());
                    shape.closeSubPath();
                    g.setGradientFill (ColourGradient (c.withAlpha (on ? (bi == selected ? 0.30f : 0.16f) : 0.05f), 0.0f, plot.getY(),
                                                       c.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
                    g.fillPath (shape);
                    g.setColour (c.withAlpha (on ? 0.7f : 0.25f));
                    g.strokePath (shape, PathStrokeType (bi == selected ? 1.6f : 1.0f));
                }
                // the gain change now: a bright bar across the band's top (down = reduction, up = boost)
                const float change = mb.bandGr[(size_t) bi].load();
                {
                    const float y0 = plot.getY() + plot.getHeight() * 0.08f;
                    const float dy = jlimit (-1.0f, 1.0f, -change / 24.0f) * plot.getHeight() * 0.4f;
                    auto barArea = Rectangle<float> (region.getX() + 8.0f, jmin (y0, y0 + dy), region.getWidth() - 16.0f, std::abs (dy));
                    g.setGradientFill (ColourGradient (c.withAlpha (0.65f), 0.0f, barArea.getY(), c.withAlpha (0.15f), 0.0f, barArea.getBottom(), false));
                    g.fillRect (barArea);
                    g.setColour (c);
                    g.drawHorizontalLine ((int) (y0 + dy), region.getX() + 8.0f, region.getRight() - 8.0f);
                }
                // threshold line
                const float ty = yFor (mb.get (M::idx (bi, M::bThreshold)));
                g.setColour (on ? c : c.withAlpha (0.35f));
                g.drawLine (x0 + 2.0f, ty, x1 - 2.0f, ty, (hotBand == bi || dragBand == bi) ? 2.5f : 1.5f);
                // the band's gain as a node at its centre (drag up / down)
                const auto gn = gainNode (bi);

                Graphs::node (g, gn, c, hotGain == bi || dragGain == bi);
                // label
                String t = "B" + String (bi + 1) + "  " + M::modeNames[mb.getInt (M::idx (bi, M::bMode))].upToFirstOccurrenceOf (" ", false, false);
                if (large) t << "  " << mb.param (M::idx (bi, M::bThreshold))->getCurrentValueAsString() << "  " << mb.param (M::idx (bi, M::bRatio))->getCurrentValueAsString();
                t << "   " << String (change, 1) << " dB";
                if (mb.getInt (M::idx (bi, M::bSolo)) > 0) t << "  SOLO";
                if (! on) t << "  (bypassed)";
                g.setColour (c);
                g.setFont (FontOptions (large ? 11.5f : 9.5f, Font::bold));
                g.drawText (t, Rectangle<float> (x0 + 4.0f, plot.getY() + 2.0f, x1 - x0 - 8.0f, 14.0f), Justification::centredLeft, true);
            }
            if (large && (int) ranges.size() < M::maxBands)
                Graphs::label (g, "Double-click to add a band. Drag a band up / down for its threshold, its dot for gain, scroll for ratio. Right-click for mode, solo, bypass.",
                               Rectangle<float> (plot.getX(), plot.getBottom() - 14.0f, plot.getWidth(), 12.0f), Justification::centred, 0.5f);
            // crossover handles
            for (int i = 0; i < (int) ranges.size() - 1; ++i)
            {
                const float x = ranges[(size_t) i].second;
                g.setColour (hotX == i || dragX == i ? Theme::ink : Theme::ink.withAlpha (0.5f));
                g.drawLine (x, plot.getY(), x, plot.getBottom(), hotX == i ? 2.0f : 1.2f);
                Path tri; tri.addTriangle (x - 5.0f, plot.getBottom(), x + 5.0f, plot.getBottom(), x, plot.getBottom() - 7.0f);
                g.fillPath (tri);
            }
        }

        int selected = 0;
        std::function<void (int)> onSelect;

        juce::Point<float> gainNode (int b) const
        {
            const auto r = bandRanges();
            if (! isPositiveAndBelow (b, (int) r.size())) return {};
            const float x = (r[(size_t) b].first + r[(size_t) b].second) * 0.5f;
            return { x, plot.getCentreY() - mb.get (M::idx (b, M::bGain)) / 24.0f * plot.getHeight() * 0.45f };
        }
        int hitGain (juce::Point<float> p) const
        {
            for (int b = 0; b < mb.numBands(); ++b) if (gainNode (b).getDistanceFrom (p) < 10.0f) return b;
            return -1;
        }

        int hitCrossover (juce::Point<float> p) const
        {
            const auto r = bandRanges();
            for (int i = 0; i < (int) r.size() - 1; ++i) if (std::abs (p.x - r[(size_t) i].second) < 6.0f && plot.contains (p)) return i;
            return -1;
        }
        int bandAt (juce::Point<float> p) const
        {
            const auto r = bandRanges();
            for (int i = 0; i < (int) r.size(); ++i) if (p.x >= r[(size_t) i].first && p.x < r[(size_t) i].second && plot.contains (p)) return i;
            return -1;
        }

        void mouseMove (const MouseEvent& e) override
        {
            hotGain = hitGain (e.position);
            hotX = hotGain < 0 ? hitCrossover (e.position) : -1;
            hotBand = hotX < 0 && hotGain < 0 ? bandAt (e.position) : -1;
            setMouseCursor (hotX >= 0 ? MouseCursor::LeftRightResizeCursor : hotBand >= 0 ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
            repaint();
        }
        void mouseExit (const MouseEvent&) override { hotX = hotBand = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            dragGain = hitGain (e.position);
            dragX = dragGain < 0 ? hitCrossover (e.position) : -1;
            dragBand = dragX < 0 && dragGain < 0 ? bandAt (e.position) : -1;
            const int sel = dragGain >= 0 ? dragGain : dragBand;
            if (sel >= 0) { selected = sel; if (onSelect) onSelect (sel); }
            mb.edit.getUndoManager().beginNewTransaction ("Multiband");
            if (dragGain >= 0) { startGain = mb.get (M::idx (dragGain, M::bGain)); return; }
            if (dragBand >= 0 && e.mods.isPopupMenu())
            {
                const int bi = dragBand;
                dragBand = -1;
                PopupMenu m;
                for (int k = 0; k < M::modeNames.size(); ++k)
                    m.addItem (M::modeNames[k], true, mb.getInt (M::idx (bi, M::bMode)) == k, [this, bi, k] { mb.set (M::idx (bi, M::bMode), (float) k); });
                m.addSeparator();
                m.addItem ("Solo", true, mb.getInt (M::idx (bi, M::bSolo)) > 0, [this, bi] { mb.set (M::idx (bi, M::bSolo), mb.getInt (M::idx (bi, M::bSolo)) > 0 ? 0.0f : 1.0f); });
                m.addItem ("Bypass", true, mb.getInt (M::idx (bi, M::bOn)) == 0, [this, bi] { mb.set (M::idx (bi, M::bOn), mb.getInt (M::idx (bi, M::bOn)) > 0 ? 0.0f : 1.0f); });
                m.addSeparator();
                m.addItem ("Remove a band", mb.numBands() > 2, false, [this] { mb.set (mb.g (M::gBands), (float) (mb.numBands() - 3)); });
                m.showMenuAsync ({});
                return;
            }
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragX >= 0)
            {
                // the handle moves the crossover that sits at that edge
                const auto x = mb.crossovers();
                const double target = jlimit (30.0, 18000.0, freqAt (e.position.x));
                const int params[] = { M::gX1, M::gX2, M::gX3 };
                // find which parameter holds that (sorted) crossover
                int which = 0; double best = 1e9;
                for (int k = 0; k < 3; ++k)
                    if (auto d = std::abs (mb.get (mb.g (params[k])) - x[(size_t) dragX]); d < best) { best = d; which = k; }
                mb.set (mb.g (params[which]), (float) target);
            }
            else if (dragBand >= 0)
                mb.set (M::idx (dragBand, M::bThreshold), jlimit (-60.0f, 0.0f, dbAt (e.position.y)));
            else if (dragGain >= 0)
                mb.set (M::idx (dragGain, M::bGain), jlimit (-24.0f, 24.0f, startGain - e.getDistanceFromDragStartY() / (plot.getHeight() * 0.45f) * 24.0f));
            repaint();
        }
        void mouseUp (const MouseEvent&) override { dragX = dragBand = dragGain = -1; }

        /** Scroll on a band: ratio. */
        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            const int b = bandAt (e.position);
            if (b < 0) return;
            mb.edit.getUndoManager().beginNewTransaction ("Ratio");
            auto* prm = mb.param (M::idx (b, M::bRatio));
            Graphs::setNorm (prm, Graphs::norm (prm) + w.deltaY * 0.15f);
        }

        /** Double-click: add a band there (up to four). */
        void mouseDoubleClick (const MouseEvent& e) override
        {
            if (! plot.contains (e.position) || mb.numBands() >= M::maxBands) return;
            mb.edit.getUndoManager().beginNewTransaction ("Add band");
            const int nb = mb.numBands();
            const int params[] = { M::gX1, M::gX2, M::gX3 };
            // the crossover that is not in use yet moves to where you clicked
            const auto used = mb.crossovers();
            mb.set (mb.g (M::gBands), (float) (nb - 1));   // one more band
            const double f = jlimit (30.0, 18000.0, freqAt (e.position.x));
            juce::ignoreUnused (used);
            mb.set (mb.g (params[nb - 1]), (float) f);
        }

    private:
        void timerCallback() override
        {
            std::array<float, fftSize * 2> buf {};
            mb.post.read (buf.data(), fftSize);
            for (int i = 0; i < fftSize; ++i) buf[(size_t) i] *= 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (fftSize - 1));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int i = 0; i < fftSize / 2; ++i)
            {
                const float db = Decibels::gainToDecibels (buf[(size_t) i] * 4.0f / fftSize, -120.0f);
                auto& o = spec[(size_t) i];
                o = db > o ? db : o * 0.85f + db * 0.15f;
            }
            repaint();
        }
        static constexpr int fftSize = 4096;
        M& mb;
        te::Plugin::Ptr holder;
        dsp::FFT fft;
        std::array<float, fftSize / 2> spec {};
        Rectangle<float> plot;
        bool large = false;
        int hotX = -1, hotBand = -1, dragX = -1, dragBand = -1, hotGain = -1, dragGain = -1;
        float startGain = 0;
    };

    //==============================================================================
    /** A labelled strip of controls under a big display (the full editors below). */
    struct Strip : public Component
    {
        struct Item { Component* c; int width; };
        std::vector<Item> items;
        OwnedArray<Component> owned;
        Label title;

        explicit Strip (const String& t = {})
        {
            title.setText (t, dontSendNotification);
            title.setFont (FontOptions (11.0f, Font::bold));
            title.setColour (Label::textColourId, Theme::muted);
            if (t.isNotEmpty()) addAndMakeVisible (title);
        }
        template <typename T> T* add (T* c, int width) { owned.add (c); items.push_back ({ c, width }); addAndMakeVisible (c); return c; }
        int idealWidth() const { int w = 16; for (auto& i : items) w += i.width + 6; return w; }
        void paint (Graphics& g) override
        {
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawVerticalLine (getWidth() - 1, 6.0f, (float) getHeight() - 6.0f);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 4);
            if (title.isVisible()) title.setBounds (r.removeFromTop (16));
            for (auto& i : items)
            {
                auto cell = r.removeFromLeft (i.width);
                if (dynamic_cast<ParamKnob*> (i.c) == nullptr)
                    cell = cell.withSizeKeepingCentre (i.width, jmin (cell.getHeight(), dynamic_cast<ParamChoice*> (i.c) != nullptr ? 30 : 28));
                i.c->setBounds (cell);
                r.removeFromLeft (6);
            }
        }
    };

    inline ParamKnob* knob (EditViewState& evs, te::AutomatableParameter& p, const String& label)
    {
        auto* k = new ParamKnob (evs, p);
        k->setLabel (label);
        return k;
    }

    /** Lays out a big display on top and strips of controls underneath, side by side. */
    struct Panel : public Component
    {
        std::unique_ptr<Component> display;
        OwnedArray<Strip> strips;
        int stripHeight = 100;
        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto bar = getLocalBounds().removeFromBottom (stripHeight + 8).toFloat();
            g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, bar.getY(), Pro::barBot(), 0.0f, bar.getBottom(), false));
            g.fillRect (bar);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawHorizontalLine ((int) bar.getY(), 0.0f, (float) getWidth());
        }
        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (stripHeight + 8).reduced (10, 4);
            if (display) display->setBounds (r);
            int total = 0;
            for (auto* st : strips) total += st->idealWidth() + 8;
            const float scale = total > bottom.getWidth() ? (float) bottom.getWidth() / (float) total : 1.0f;
            for (auto* st : strips)
            {
                st->setBounds (bottom.removeFromLeft ((int) (st->idealWidth() * scale)));
                bottom.removeFromLeft (8);
            }
        }
    };

    //==============================================================================
    /** Pulse Compressor's full editor: everything on the graph (threshold, ratio, knee, range,
        makeup), the character as buttons and a handful of knobs. */
    class CompressorPanel : public Panel
    {
    public:
        using C = pulse::ProCompressor;
        CompressorPanel (EditViewState& evs, C& c)
        {
            display = std::make_unique<CompressorView> (c);
            addAndMakeVisible (*display);
            auto* ch = strips.add (new Strip ("CHARACTER"));
            ch->add (new ParamChoice (*c.param (C::pCharacter), Theme::accent, false), 380);
            auto* tm = strips.add (new Strip ("TIMING"));
            tm->add (knob (evs, *c.param (C::pAttack), "Attack"), 64);
            tm->add (knob (evs, *c.param (C::pRelease), "Release"), 64);
            tm->add (new ParamToggle (*c.param (C::pAutoRelease), "Auto"), 56);
            tm->add (knob (evs, *c.param (C::pLookahead), "Lookahead"), 64);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (new ParamToggle (*c.param (C::pAutoMakeup), "Auto makeup"), 92);
            out->add (knob (evs, *c.param (C::pMix), "Dry/Wet"), 64);
            out->add (knob (evs, *c.param (C::pLink), "Link"), 56);
            auto* sc = strips.add (new Strip ("SIDECHAIN"));
            sc->add (knob (evs, *c.param (C::pScHpf), "Low cut"), 60);
            sc->add (knob (evs, *c.param (C::pScLpf), "High cut"), 60);
            sc->add (new ParamToggle (*c.param (C::pScListen), "Listen", Theme::yellow), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    /** The Limiter's full editor. */
    class LimiterPanel : public Panel
    {
    public:
        using L = pulse::Limiter;
        LimiterPanel (EditViewState& evs, L& l)
        {
            display = std::make_unique<LimiterView> (l);
            addAndMakeVisible (*display);
            auto* st = strips.add (new Strip ("STYLE"));
            st->add (new ParamChoice (*l.param (L::pStyle), Theme::green, false), 280);
            auto* det = strips.add (new Strip ("DETECTION"));
            det->add (new ParamToggle (*l.param (L::pTruePeak), "True peak", Theme::green), 84);
            det->add (knob (evs, *l.param (L::pLink), "Link"), 56);
            auto* rel = strips.add (new Strip ("RELEASE"));
            rel->add (new ParamToggle (*l.param (L::pAutoRelease), "Auto"), 56);
            rel->add (knob (evs, *l.param (L::pRelease), "Release"), 64);
            for (auto* x : strips) addAndMakeVisible (x);
        }
    };

    /** A floating card for the selected band's controls (it follows the band). */
    struct FloatingCard : public Component
    {
        Colour colour { Theme::accent };
        FloatingCard() { setAlwaysOnTop (true); }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            DropShadow (Pro::shade (0.6f), 18, { 0, 6 }).drawForRectangle (g, getLocalBounds().reduced (6));
            g.setGradientFill (ColourGradient (Pro::cardTop(), 0.0f, r.getY(), Pro::cardBot(), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (colour.withAlpha (0.8f));
            g.drawRoundedRectangle (r, 8.0f, 1.2f);
            g.setColour (colour);
            g.fillRoundedRectangle (r.withHeight (3.0f).reduced (8.0f, 0.0f), 1.5f);
        }
    };

    /** Multiband Dynamics' full editor: the band display fills the window; the selected band's
        mode, solo, bypass and timing float on a card over it; the globals sit in the bottom bar. */
    class MultibandPanel : public Panel, private Timer
    {
    public:
        using M = pulse::MultibandDynamics;
        MultibandPanel (EditViewState& e, M& m) : evs (e), mb (m)
        {
            auto* v = new MultibandView (m);
            display.reset (v);
            v->onSelect = [this] (int b) { showBand (b); };
            addAndMakeVisible (*display);
            auto* gl = strips.add (new Strip ("BANDS"));
            gl->add (new ParamChoice (*m.param (m.g (M::gBands)), Theme::accent, false), 260);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *m.param (m.g (M::gOutput)), "Output"), 60);
            out->add (knob (evs, *m.param (m.g (M::gMix)), "Dry/Wet"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
            addAndMakeVisible (card);
            showBand (0);
            startTimerHz (15);
        }

        void showBand (int b)
        {
            band = b;
            controls.clear();
            card.colour = MultibandView::bandColour (b);
            title.setText ("BAND " + String (b + 1), dontSendNotification);
            title.setColour (Label::textColourId, card.colour);
            title.setFont (FontOptions (12.0f, Font::bold));
            card.addAndMakeVisible (title);
            mode = new ParamChoice (*mb.param (M::idx (b, M::bMode)), card.colour, false);
            solo = new ParamToggle (*mb.param (M::idx (b, M::bSolo)), "Solo", Theme::yellow);
            active = new ParamToggle (*mb.param (M::idx (b, M::bOn)), "Active", Theme::green);
            for (auto* c : std::initializer_list<Component*> { mode, solo, active }) { controls.add (c); card.addAndMakeVisible (c); }
            const std::pair<int, const char*> ks[] = { { M::bThreshold, "Threshold" }, { M::bRatio, "Ratio" }, { M::bAttack, "Attack" },
                                                       { M::bRelease, "Release" }, { M::bGain, "Gain" } };
            knobsStart = controls.size();
            for (auto& [k, label] : ks) { auto* kn = knob (evs, *mb.param (M::idx (b, k)), label); controls.add (kn); card.addAndMakeVisible (kn); }
            if (auto* v = dynamic_cast<MultibandView*> (display.get())) v->selected = b;
            layoutCard();
        }

        void resized() override { Panel::resized(); layoutCard(); }

    private:
        void layoutCard()
        {
            auto* v = dynamic_cast<MultibandView*> (display.get());
            if (v == nullptr || mode == nullptr) return;
            const auto ranges = v->bandRanges();
            if (! isPositiveAndBelow (band, (int) ranges.size())) { card.setVisible (false); return; }
            card.setVisible (true);
            constexpr int w = 380, h = 160;
            const float cx = (ranges[(size_t) band].first + ranges[(size_t) band].second) * 0.5f + (float) display->getX();
            auto area = display->getBounds().reduced (12);
            const int x = jlimit (area.getX(), area.getRight() - w, (int) cx - w / 2);
            const int y = area.getBottom() - h - 18;
            card.setBounds (x, y, w, h);
            auto r = card.getLocalBounds().reduced (10, 8);
            auto top = r.removeFromTop (22);
            active->setBounds (top.removeFromRight (64));
            top.removeFromRight (6);
            solo->setBounds (top.removeFromRight (54));
            title.setBounds (top);
            r.removeFromTop (6);
            mode->setBounds (r.removeFromTop (26));
            r.removeFromTop (6);
            for (int i = knobsStart; i < controls.size(); ++i) controls[i]->setBounds (r.removeFromLeft (r.getWidth() / (controls.size() - i)));
            lastX = x;
        }

        void timerCallback() override
        {
            auto* v = dynamic_cast<MultibandView*> (display.get());
            if (v == nullptr) return;
            const auto ranges = v->bandRanges();
            if (isPositiveAndBelow (band, (int) ranges.size()))
            {
                const int cx = (int) ((ranges[(size_t) band].first + ranges[(size_t) band].second) * 0.5f);
                if (cx != lastCx || ! card.isVisible()) { lastCx = cx; layoutCard(); }
            }
            else if (card.isVisible()) layoutCard();
        }

        EditViewState& evs;
        M& mb;
        FloatingCard card;
        Label title;
        OwnedArray<Component> controls;
        ParamChoice* mode = nullptr;
        ParamToggle* solo = nullptr;
        ParamToggle* active = nullptr;
        int band = 0, knobsStart = 0, lastX = -1, lastCx = -1;
    };
}
