#pragma once

#include "ParamKnob.h"
#include "Graphs.h"
#include "PrismEQ.h"

//==============================================================================
/** Prism EQ's editor: a big response display (analyser, every band's curve, the live moving
    curve, draggable numbered nodes with Q handles) and, under it, the selected band's controls. */
namespace PrismUI
{
    using EQ = pulse::PrismEQ;

    inline Colour bandColour (int b)
    {
        const uint32 cols[] = { 0xffff6b6b, 0xffffa94d, 0xffffd43b, 0xff69db7c, 0xff4dabf7, 0xffb197fc };
        return Colour (cols[b % 6]);
    }

    /** A small picture of a filter shape (used on the type buttons and the band chips). */
    inline Path shapeIcon (int type, Rectangle<float> r)
    {
        Path p;
        const float x0 = r.getX(), x1 = r.getRight(), y0 = r.getY(), y1 = r.getBottom(), cy = r.getCentreY(), cx = r.getCentreX(), w = r.getWidth();
        switch (type)
        {
            case EQ::bell:      p.startNewSubPath (x0, cy); p.cubicTo (cx - w * 0.2f, cy, cx - w * 0.12f, y0, cx, y0); p.cubicTo (cx + w * 0.12f, y0, cx + w * 0.2f, cy, x1, cy); break;
            case EQ::lowShelf:  p.startNewSubPath (x0, y0 + 2); p.lineTo (cx - w * 0.15f, y0 + 2); p.cubicTo (cx, y0 + 2, cx, cy + 3, cx + w * 0.15f, cy + 3); p.lineTo (x1, cy + 3); break;
            case EQ::highShelf: p.startNewSubPath (x0, cy + 3); p.lineTo (cx - w * 0.15f, cy + 3); p.cubicTo (cx, cy + 3, cx, y0 + 2, cx + w * 0.15f, y0 + 2); p.lineTo (x1, y0 + 2); break;
            case EQ::lowCut:    p.startNewSubPath (x0 + w * 0.15f, y1); p.cubicTo (cx - w * 0.1f, cy - 2, cx, cy - 4, cx + w * 0.1f, cy - 4); p.lineTo (x1, cy - 4); break;
            case EQ::highCut:   p.startNewSubPath (x0, cy - 4); p.lineTo (cx - w * 0.1f, cy - 4); p.cubicTo (cx, cy - 4, cx + w * 0.1f, cy - 2, x1 - w * 0.15f, y1); break;
            case EQ::notch:     p.startNewSubPath (x0, y0 + 3); p.lineTo (cx - w * 0.12f, y0 + 3); p.lineTo (cx, y1); p.lineTo (cx + w * 0.12f, y0 + 3); p.lineTo (x1, y0 + 3); break;
            case EQ::bandPass:  p.startNewSubPath (x0 + 2, y1); p.cubicTo (cx - w * 0.15f, y1, cx - w * 0.1f, y0 + 2, cx, y0 + 2); p.cubicTo (cx + w * 0.1f, y0 + 2, cx + w * 0.15f, y1, x1 - 2, y1); break;
            case EQ::tiltShelf: p.startNewSubPath (x0, y1 - 2); p.lineTo (cx - w * 0.15f, y1 - 2); p.cubicTo (cx, y1 - 2, cx, y0 + 2, cx + w * 0.15f, y0 + 2); p.lineTo (x1, y0 + 2); break;
            case EQ::allPass:   p.startNewSubPath (x0, cy); p.lineTo (x1, cy); p.addEllipse (cx - 3, cy - 3, 6, 6); break;
            case EQ::flatTilt:  p.startNewSubPath (x0, y1 - 1); p.lineTo (x1, y0 + 1); break;
            default: break;
        }
        return p;
    }

    //==============================================================================
    /** The response display. */
    class Graph : public Component, private Timer
    {
    public:
        static constexpr int fftSize = 4096;
        static constexpr double sampleRateGuess = 48000.0;

        Graph (EQ& e, std::function<void (int)> onSelect) : eq (e), holder (&e), select (std::move (onSelect)), fft (12)
        {
            pre.fill (-120.0f);
            post.fill (-120.0f);
            startTimerHz (30);
            setWantsKeyboardFocus (false);
        }

        int selected = 2;
        bool large = true;

        //==========================================================================
        float rangeDb() const { const float r[] = { 3.0f, 6.0f, 12.0f, 30.0f }; return r[jlimit (0, 3, eq.getInt (eq.g (EQ::gRange)))]; }
        float xFor (double f) const  { return plot.getX() + (float) (std::log (f / 10.0) / std::log (3000.0)) * plot.getWidth(); }      // 10 Hz .. 30 kHz
        double freqAt (float x) const { return 10.0 * std::pow (3000.0, (x - plot.getX()) / plot.getWidth()); }
        float yFor (double db) const  { return plot.getCentreY() - (float) (db / rangeDb()) * plot.getHeight() * 0.46f; }
        double dbAt (float y) const   { return (plot.getCentreY() - y) / (plot.getHeight() * 0.46f) * rangeDb(); }
        float yForSpec (float db) const { return plot.getBottom() - (db + 96.0f) / 96.0f * plot.getHeight(); }

        juce::Point<float> nodePos (int b, bool live = false) const
        {
            const int t = eq.bandType (b);
            const double f = live ? (double) eq.effFreq[(size_t) b].load() : (double) eq.get (EQ::idx (b, EQ::pFreq));
            double db = 0.0;
            if (EQ::typeUsesGain (t))
            {
                db = live ? (double) eq.effGain[(size_t) b].load() : (double) eq.get (EQ::idx (b, EQ::pGain));
                if (t == EQ::tiltShelf || t == EQ::flatTilt) db *= 0.5;
            }
            else if (t == EQ::lowCut || t == EQ::highCut || t == EQ::bandPass || t == EQ::notch || t == EQ::allPass)
                db = 0.0;
            return { xFor (f), jlimit (plot.getY() + 6.0f, plot.getBottom() - 6.0f, yFor (db)) };
        }

        /** Q handles: the band's edges (bandwidth from Q), for the selected band. */
        std::pair<float, float> qHandles (int b) const
        {
            const double q = eq.get (EQ::idx (b, EQ::pQ)), f = eq.get (EQ::idx (b, EQ::pFreq));
            const double bw = 2.0 / std::log (2.0) * std::asinh (1.0 / (2.0 * q));   // octaves
            return { xFor (f / std::pow (2.0, bw * 0.5)), xFor (f * std::pow (2.0, bw * 0.5)) };
        }

        //==========================================================================
        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            g.setGradientFill (ColourGradient (Theme::well.brighter (0.04f), 0.0f, b.getY(), Theme::well, 0.0f, b.getBottom(), false));
            g.fillRoundedRectangle (b, 5.0f);
            g.setColour (Theme::line);
            g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
            plot = b.reduced (large ? 34.0f : 22.0f, large ? 14.0f : 8.0f).withTrimmedBottom (large ? 10.0f : 6.0f);

            drawGrid (g);
            drawAnalyser (g);
            drawBands (g);
            drawNodes (g);
            drawHoverInfo (g);
        }

        void drawGrid (Graphics& g)
        {
            // frequency lines and labels (decades bright, steps faint)
            for (int dec = 10; dec <= 10000; dec *= 10)
                for (int k = 1; k <= 9; ++k)
                {
                    const double f = dec * k;
                    if (f < 10.0 || f > 30000.0) continue;
                    g.setColour (Theme::ink.withAlpha (k == 1 ? 0.10f : 0.035f));
                    g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
                }
            g.setFont (FontOptions (large ? 10.5f : 9.0f));
            for (auto [f, t] : { std::pair<double, const char*> { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                 { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } })
            {
                if (! large && (f == 50.0 || f == 200.0 || f == 2000.0)) continue;
                g.setColour (Theme::muted.withAlpha (0.8f));
                g.drawText (t, Rectangle<float> (xFor (f) - 16.0f, plot.getBottom() + 2.0f, 32.0f, 12.0f), Justification::centred);
            }
            // EQ dB lines (right) and analyser dB (left, large only)
            const float rg = rangeDb();
            const float step = rg <= 3.0f ? 1.0f : rg <= 6.0f ? 2.0f : rg <= 12.0f ? 3.0f : 6.0f;
            for (float db = -rg; db <= rg + 0.01f; db += step)
            {
                const float y = yFor (db);
                g.setColour (Theme::ink.withAlpha (std::abs (db) < 0.01f ? 0.16f : 0.045f));
                g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
                g.setColour (Theme::muted.withAlpha (0.85f));
                g.drawText ((db > 0 ? "+" : "") + String ((int) db), Rectangle<float> (plot.getRight() + 3.0f, y - 6.0f, 30.0f, 12.0f), Justification::centredLeft);
            }
            if (large)
                for (int db = -84; db <= 0; db += 12)
                {
                    g.setColour (Theme::muted.withAlpha (0.45f));
                    g.drawText (String (db), Rectangle<float> (getLocalBounds().toFloat().getX() + 2.0f, yForSpec ((float) db) - 6.0f, 30.0f, 12.0f), Justification::centredRight);
                }
        }

        void drawAnalyser (Graphics& g)
        {
            const int mode = eq.getInt (eq.g (EQ::gAnalyser));
            if (mode == 0) return;
            auto curve = [&] (const std::array<float, fftSize / 2>& spec, bool filled) -> Path
            {
                Path p;
                const int w = (int) plot.getWidth();
                bool started = false;
                for (int i = 0; i <= w; i += 2)
                {
                    const double f0 = freqAt (plot.getX() + (float) i), f1 = freqAt (plot.getX() + (float) (i + 2));
                    int b0 = jlimit (1, fftSize / 2 - 1, (int) (f0 / sampleRateGuess * fftSize)), b1 = jlimit (b0, fftSize / 2 - 1, (int) (f1 / sampleRateGuess * fftSize));
                    float db = -120.0f;
                    for (int k = b0; k <= b1; ++k) db = jmax (db, spec[(size_t) k]);
                    db += 4.5f * (float) std::log2 (jmax (10.0, f0) / 1000.0);   // 4.5 dB/oct tilt: pink noise reads flat
                    const float y = jlimit (plot.getY(), plot.getBottom(), yForSpec (db));
                    const float x = plot.getX() + (float) i;
                    if (! started) { if (filled) { p.startNewSubPath (x, plot.getBottom()); p.lineTo (x, y); } else p.startNewSubPath (x, y); started = true; }
                    else p.lineTo (x, y);
                }
                if (filled) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
                return p;
            };
            if (mode == 2)
            {
                g.setColour (Theme::ink.withAlpha (0.22f));
                g.strokePath (curve (pre, false), PathStrokeType (1.0f));
            }
            auto fill = curve (post, true);
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.28f), 0.0f, plot.getY(), Theme::accent.withAlpha (0.04f), 0.0f, plot.getBottom(), false));
            g.fillPath (fill);
            g.setColour (Theme::accent.withAlpha (0.45f));
            g.strokePath (curve (post, false), PathStrokeType (1.0f));
        }

        Path responsePath (std::function<double (double)> dbOf, float step = 1.5f) const
        {
            Path p;
            for (float x = plot.getX(); x <= plot.getRight(); x += step)
            {
                const float y = jlimit (plot.getY() - 2.0f, plot.getBottom() + 2.0f, yFor (dbOf (freqAt (x))));
                if (x == plot.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        }

        void drawBands (Graphics& g)
        {
            bool anyMoves = false;
            // each band's own curve; the selected one filled
            for (int b = 0; b < EQ::numBands; ++b)
            {
                if (! eq.isBandOn (b)) continue;
                anyMoves = anyMoves || (eq.bandMoves (b) && eq.isProcessing());
                auto p = responsePath ([&] (double f) { return eq.bandDb (b, f); }, 2.0f);
                const auto c = bandColour (b);
                if (b == selected)
                {
                    Path fill (p);
                    fill.lineTo (plot.getRight(), yFor (0.0));
                    fill.lineTo (plot.getX(), yFor (0.0));
                    fill.closeSubPath();
                    g.setColour (c.withAlpha (0.18f));
                    g.fillPath (fill);
                }
                g.setColour (c.withAlpha (b == selected ? 0.9f : 0.45f));
                g.strokePath (p, PathStrokeType (b == selected ? 1.4f : 1.0f));

                // a dynamic band: its range as a dashed curve
                if (eq.getInt (EQ::idx (b, EQ::pDyn)) > 0 && EQ::typeUsesGain (eq.bandType (b)))
                {
                    EQ::Design d;
                    EQ::design (d, eq.bandType (b), 48000.0, eq.get (EQ::idx (b, EQ::pFreq)), eq.get (EQ::idx (b, EQ::pGain)) + eq.get (EQ::idx (b, EQ::pRange)),
                                eq.get (EQ::idx (b, EQ::pQ)), eq.getInt (EQ::idx (b, EQ::pSlope)));
                    auto rp = responsePath ([&] (double f) { return Decibels::gainToDecibels (d.magnitude (f, 48000.0), -120.0); }, 3.0f);
                    Path dashed;
                    const float dash[] = { 4.0f, 3.0f };
                    PathStrokeType (1.0f).createDashedStroke (dashed, rp, dash, 2);
                    g.setColour (c.withAlpha (0.7f));
                    g.fillPath (dashed);
                }
            }
            // the whole EQ: static (bright) and, when something moves, the live curve
            auto sum = responsePath ([&] (double f) { return eq.responseDb (f, false); });
            Graphs::glowStroke (g, sum, Theme::text, large ? 2.2f : 1.8f);
            if (anyMoves)
            {
                auto live = responsePath ([&] (double f) { return eq.responseDb (f, true); }, 2.0f);
                Graphs::glowStroke (g, live, Theme::orange, 1.6f);
            }
        }

        void drawNodes (Graphics& g)
        {
            for (int b = 0; b < EQ::numBands; ++b)
            {
                const auto c = bandColour (b);
                const bool on = eq.isBandOn (b);
                auto pnt = nodePos (b);
                if (on && eq.bandMoves (b) && eq.isProcessing())   // where the band is right now
                {
                    auto lp = nodePos (b, true);
                    g.setColour (c.withAlpha (0.5f));
                    g.drawLine (pnt.x, pnt.y, lp.x, lp.y, 1.0f);
                    g.fillEllipse (lp.x - 3.5f, lp.y - 3.5f, 7.0f, 7.0f);
                }
                if (b == selected && on && EQ::typeUsesQ (eq.bandType (b)))
                {
                    auto [qa, qb] = qHandles (b);
                    g.setColour (c.withAlpha (0.55f));
                    g.drawLine (qa, pnt.y, qb, pnt.y, 1.0f);
                    for (float qx : { qa, qb })
                    {
                        Path d;
                        d.addTriangle (qx, pnt.y - 5.0f, qx + 4.0f, pnt.y, qx, pnt.y + 5.0f);
                        d.addTriangle (qx, pnt.y - 5.0f, qx - 4.0f, pnt.y, qx, pnt.y + 5.0f);
                        g.setColour (hoverQ == (qx == qa ? 0 : 1) || dragMode == 2 ? Theme::ink : c);
                        g.fillPath (d);
                    }
                }
                const float r = b == selected ? 9.0f : 8.0f;
                if (b == hover || b == selected)
                {
                    g.setColour (c.withAlpha (0.25f));
                    g.fillEllipse (pnt.x - r - 5.0f, pnt.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
                }
                g.setColour (Colours::black.withAlpha (0.55f));
                g.fillEllipse (pnt.x - r - 1.5f, pnt.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
                g.setColour (on ? c : c.withAlpha (0.25f));
                g.fillEllipse (pnt.x - r, pnt.y - r, r * 2.0f, r * 2.0f);
                if (! on) { g.setColour (c); g.drawEllipse (pnt.x - r, pnt.y - r, r * 2.0f, r * 2.0f, 1.5f); }
                if (b == selected) { g.setColour (Theme::ink); g.drawEllipse (pnt.x - r - 2.5f, pnt.y - r - 2.5f, (r + 2.5f) * 2.0f, (r + 2.5f) * 2.0f, 1.5f); }
                g.setColour (on ? Colour (0xff111317) : c);
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText (String (b + 1), Rectangle<float> (pnt.x - r, pnt.y - r, r * 2.0f, r * 2.0f), Justification::centred);
                if (eq.getInt (EQ::idx (b, EQ::pSolo)) > 0 && on)
                {
                    g.setColour (Theme::yellow);
                    g.drawText ("S", Rectangle<float> (pnt.x + r - 2.0f, pnt.y - r - 10.0f, 12.0f, 12.0f), Justification::centred);
                }
            }
        }

        String bandSummary (int b) const
        {
            const int t = eq.bandType (b);
            String s = "Band " + String (b + 1) + "  " + EQ::typeNames[t] + "   " + eq.param (EQ::idx (b, EQ::pFreq))->getCurrentValueAsString();
            if (EQ::typeUsesGain (t)) s << "   " << eq.param (EQ::idx (b, EQ::pGain))->getCurrentValueAsString();
            if (EQ::typeUsesQ (t)) s << "   Q " << eq.param (EQ::idx (b, EQ::pQ))->getCurrentValueAsString();
            if (EQ::typeUsesSlope (t)) s << "   " << EQ::slopeNames[eq.getInt (EQ::idx (b, EQ::pSlope))];
            const int pl = eq.getInt (EQ::idx (b, EQ::pPlace));
            if (pl != EQ::stereo) s << "   " << EQ::placeNames[pl];
            return s;
        }

        void drawHoverInfo (Graphics& g)
        {
            const int b = dragging >= 0 ? dragging : hover;
            String text;
            juce::Point<float> at;
            if (b >= 0)
            {
                text = bandSummary (b);
                if (! eq.isBandOn (b)) text << "  (off - double-click to turn on)";
                if (eq.getInt (EQ::idx (b, EQ::pDyn)) > 0) text << "\nDynamic: " << eq.param (EQ::idx (b, EQ::pRange))->getCurrentValueAsString() << " above "
                                                             << eq.param (EQ::idx (b, EQ::pThresh))->getCurrentValueAsString() << "  (now " << String (eq.dynGain[(size_t) b].load(), 1) << " dB)";
                if (eq.getInt (EQ::idx (b, EQ::pModSrc)) > 0) text << "\nMovement: " << EQ::modSources[eq.getInt (EQ::idx (b, EQ::pModSrc))] << "  now "
                                                                   << String (roundToInt (eq.effFreq[(size_t) b].load())) << " Hz, " << String (eq.effGain[(size_t) b].load(), 1) << " dB";
                at = nodePos (b);
            }
            else if (plot.contains (mouse))
            {
                const double f = freqAt (mouse.x);
                text = (f >= 1000.0 ? String (f / 1000.0, 2) + " kHz" : String (roundToInt (f)) + " Hz") + "   " + MidiMessage::getMidiNoteName (roundToInt (69 + 12 * std::log2 (f / 440.0)), true, true, 3)
                       + "   EQ " + String (eq.responseDb (f), 1) + " dB";
                if (large) text << "\nDouble-click: add a band here.  Right-click: analyser and display options.";
                at = mouse;
            }
            if (text.isEmpty()) return;
            g.setFont (FontOptions (11.5f));
            const auto lines = StringArray::fromLines (text);
            float w = 0;
            for (auto& l : lines) w = jmax (w, (float) GlyphArrangement::getStringWidth (g.getCurrentFont(), l));
            auto box = Rectangle<float> (w + 16.0f, 8.0f + 15.0f * lines.size()).withPosition (at.x + 14.0f, at.y - 18.0f - 15.0f * lines.size());
            if (box.getRight() > getWidth() - 4.0f) box.setX (at.x - box.getWidth() - 14.0f);
            box = box.constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
            g.setColour (Theme::header.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (b >= 0 ? bandColour (b) : Theme::line);
            g.drawRoundedRectangle (box, 4.0f, 1.0f);
            g.setColour (Theme::text);
            for (int i = 0; i < lines.size(); ++i)
                g.drawText (lines[i], box.reduced (8.0f, 4.0f).withHeight (15.0f).translated (0.0f, 15.0f * i), Justification::centredLeft);
        }

        //==========================================================================
        int hitNode (juce::Point<float> p) const
        {
            int best = -1; float bestD = 14.0f;
            for (int b = 0; b < EQ::numBands; ++b)
                if (auto d = nodePos (b).getDistanceFrom (p); d < bestD) { bestD = d; best = b; }
            return best;
        }

        int hitQ (juce::Point<float> p) const
        {
            if (selected < 0 || ! eq.isBandOn (selected) || ! EQ::typeUsesQ (eq.bandType (selected))) return -1;
            auto [qa, qb] = qHandles (selected);
            const float y = nodePos (selected).y;
            if (std::abs (p.y - y) > 8.0f) return -1;
            if (std::abs (p.x - qa) < 7.0f) return 0;
            if (std::abs (p.x - qb) < 7.0f) return 1;
            return -1;
        }

        void mouseMove (const MouseEvent& e) override
        {
            mouse = e.position;
            hover = hitNode (e.position);
            hoverQ = hover < 0 ? hitQ (e.position) : -1;
            setMouseCursor (hoverQ >= 0 ? MouseCursor::LeftRightResizeCursor : hover >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
            repaint();
        }
        void mouseExit (const MouseEvent&) override { hover = -1; hoverQ = -1; mouse = { -100.0f, -100.0f }; repaint(); }

        void mouseDown (const MouseEvent& e) override
        {
            const int q = hitQ (e.position);
            const int b = q >= 0 ? selected : hitNode (e.position);
            dragging = -1;
            dragMode = 0;
            if (b < 0)
            {
                if (e.mods.isPopupMenu()) showDisplayMenu();
                return;
            }
            selectBand (b);
            if (e.mods.isPopupMenu()) { showBandMenu (b); return; }
            eq.edit.getUndoManager().beginNewTransaction ("Move band " + String (b + 1));
            if (! eq.isBandOn (b)) eq.set (EQ::idx (b, EQ::pOn), 1.0f);
            dragging = b;
            dragMode = q >= 0 ? 2 : (e.mods.isAltDown() ? 1 : 0);
            startFreq = eq.get (EQ::idx (b, EQ::pFreq));
            startGain = eq.get (EQ::idx (b, EQ::pGain));
            startQ = eq.get (EQ::idx (b, EQ::pQ));
            for (int k : { EQ::pFreq, EQ::pGain, EQ::pQ }) eq.param (EQ::idx (b, k))->parameterChangeGestureBegin();
        }

        void mouseDrag (const MouseEvent& e) override
        {
            mouse = e.position;
            if (dragging < 0) return;
            const int b = dragging, t = eq.bandType (b);
            const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
            const float dx = (float) e.getDistanceFromDragStartX() * fine, dy = (float) e.getDistanceFromDragStartY() * fine;
            if (dragMode == 2)   // Q handle: wider apart = lower Q
            {
                auto [qa, qb] = qHandles (b);
                juce::ignoreUnused (qa, qb);
                const double f = eq.get (EQ::idx (b, EQ::pFreq));
                const double edge = freqAt (e.position.x);
                const double halfOct = jmax (0.02, std::abs (std::log2 (edge / f)));
                const double bw = halfOct * 2.0;
                const double q = std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0);
                eq.set (EQ::idx (b, EQ::pQ), (float) jlimit (0.025, 40.0, q));
                return;
            }
            if (dragMode == 1 || (e.mods.isCommandDown() && ! EQ::typeUsesGain (t)))   // Alt-drag: up / down = Q
            {
                eq.set (EQ::idx (b, EQ::pQ), (float) jlimit (0.025, 40.0, startQ * std::pow (2.0, -dy / 60.0)));
                return;
            }
            const bool lockX = e.mods.isCommandDown() && std::abs (dy) > std::abs (dx);
            const bool lockY = e.mods.isCommandDown() && ! lockX;
            if (! lockX)
            {
                const double f = startFreq * std::pow (3000.0, dx / plot.getWidth());
                eq.set (EQ::idx (b, EQ::pFreq), (float) jlimit (10.0, 30000.0, f));
            }
            if (! lockY)
            {
                if (EQ::typeUsesGain (t))
                {
                    double db = startGain - dy / (plot.getHeight() * 0.46f) * rangeDb() * ((t == EQ::tiltShelf || t == EQ::flatTilt) ? 2.0f : 1.0f);
                    eq.set (EQ::idx (b, EQ::pGain), (float) jlimit (-30.0, 30.0, db));
                }
                else   // cuts, notch, band pass, all pass: up / down sets Q (resonance)
                    eq.set (EQ::idx (b, EQ::pQ), (float) jlimit (0.025, 40.0, startQ * std::pow (2.0, -dy / 50.0)));
            }
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dragging >= 0)
                for (int k : { EQ::pFreq, EQ::pGain, EQ::pQ }) eq.param (EQ::idx (dragging, k))->parameterChangeGestureEnd();
            dragging = -1;
            dragMode = 0;
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            const int b = hitNode (e.position);
            eq.edit.getUndoManager().beginNewTransaction ("Band on / off");
            if (b >= 0) { eq.set (EQ::idx (b, EQ::pOn), eq.isBandOn (b) ? 0.0f : 1.0f); selectBand (b); return; }
            if (! plot.contains (e.position)) return;
            addBandAt (freqAt (e.position.x), dbAt (e.position.y));
        }

        /** Turns on the first free band at a frequency and gain (cuts at the edges, bells between). */
        int addBandAt (double f, double db)
        {
            for (int b = 0; b < EQ::numBands; ++b)
                if (! eq.isBandOn (b))
                {
                    const int type = f < 40.0 ? EQ::lowCut : (f > 14000.0 ? EQ::highCut : EQ::bell);
                    eq.set (EQ::idx (b, EQ::pType), (float) type);
                    eq.set (EQ::idx (b, EQ::pFreq), (float) jlimit (10.0, 30000.0, f));
                    eq.set (EQ::idx (b, EQ::pGain), type == EQ::bell ? (float) jlimit (-30.0, 30.0, db) : 0.0f);
                    eq.set (EQ::idx (b, EQ::pQ), 1.0f);
                    eq.set (EQ::idx (b, EQ::pOn), 1.0f);
                    selectBand (b);
                    return b;
                }
            return -1;
        }

        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            const int b = hitNode (e.position) >= 0 ? hitNode (e.position) : selected;
            if (b < 0 || ! eq.isBandOn (b)) return;
            eq.edit.getUndoManager().beginNewTransaction ("Band Q");
            if (e.mods.isShiftDown() && EQ::typeUsesGain (eq.bandType (b)))
                eq.set (EQ::idx (b, EQ::pGain), jlimit (-30.0f, 30.0f, eq.get (EQ::idx (b, EQ::pGain)) + w.deltaY * 3.0f));
            else
                eq.set (EQ::idx (b, EQ::pQ), (float) jlimit (0.025, 40.0, eq.get (EQ::idx (b, EQ::pQ)) * std::pow (2.0, w.deltaY * 1.5)));
        }

        void selectBand (int b) { selected = b; if (select) select (b); repaint(); }

        void showBandMenu (int b)
        {
            PopupMenu m, types, slopes, places, moves;
            auto set = [this, b] (int which, float v) { eq.edit.getUndoManager().beginNewTransaction ("Band setting"); eq.set (EQ::idx (b, which), v); };
            for (int t = 0; t < EQ::numTypes; ++t) types.addItem (EQ::typeNames[t], true, eq.bandType (b) == t, [set, t] { set (EQ::pType, (float) t); });
            for (int s = 0; s < EQ::slopeNames.size(); ++s) slopes.addItem (EQ::slopeNames[s], true, eq.getInt (EQ::idx (b, EQ::pSlope)) == s, [set, s] { set (EQ::pSlope, (float) s); });
            for (int p = 0; p < EQ::placeNames.size(); ++p) places.addItem (EQ::placeNames[p], true, eq.getInt (EQ::idx (b, EQ::pPlace)) == p, [set, p] { set (EQ::pPlace, (float) p); });
            for (int s = 0; s < EQ::modSources.size(); ++s) moves.addItem (EQ::modSources[s], true, eq.getInt (EQ::idx (b, EQ::pModSrc)) == s, [set, s] { set (EQ::pModSrc, (float) s); });
            m.addSectionHeader ("Band " + String (b + 1));
            m.addSubMenu ("Shape", types);
            m.addSubMenu ("Slope", slopes, EQ::typeUsesSlope (eq.bandType (b)));
            m.addSubMenu ("Placement", places);
            m.addItem ("Dynamic", true, eq.getInt (EQ::idx (b, EQ::pDyn)) > 0, [this, set, b] { set (EQ::pDyn, eq.getInt (EQ::idx (b, EQ::pDyn)) > 0 ? 0.0f : 1.0f); });
            m.addSubMenu ("Movement", moves);
            m.addItem ("Solo (listen to this band)", true, eq.getInt (EQ::idx (b, EQ::pSolo)) > 0, [this, set, b] { set (EQ::pSolo, eq.getInt (EQ::idx (b, EQ::pSolo)) > 0 ? 0.0f : 1.0f); });
            m.addItem ("Invert gain", EQ::typeUsesGain (eq.bandType (b)), false, [this, set, b] { set (EQ::pGain, -eq.get (EQ::idx (b, EQ::pGain))); });
            m.addSeparator();
            m.addItem (eq.isBandOn (b) ? "Turn off" : "Turn on", [this, set, b] { set (EQ::pOn, eq.isBandOn (b) ? 0.0f : 1.0f); });
            m.addItem ("Reset band", [this, b]
            {
                eq.edit.getUndoManager().beginNewTransaction ("Reset band");
                for (int k = 0; k < EQ::perBand; ++k)
                    if (k != EQ::pOn && k != EQ::pFreq && k != EQ::pType) Graphs::reset (eq.param (EQ::idx (b, k)));
            });
            m.showMenuAsync ({});
        }

        void showDisplayMenu()
        {
            PopupMenu m, an, rg;
            for (int i = 0; i < EQ::analyserNames.size(); ++i)
                an.addItem (EQ::analyserNames[i], true, eq.getInt (eq.g (EQ::gAnalyser)) == i, [this, i] { eq.set (eq.g (EQ::gAnalyser), (float) i); });
            const char* ranges[] = { "+/- 3 dB", "+/- 6 dB", "+/- 12 dB", "+/- 30 dB" };
            for (int i = 0; i < 4; ++i) rg.addItem (ranges[i], true, eq.getInt (eq.g (EQ::gRange)) == i, [this, i] { eq.set (eq.g (EQ::gRange), (float) i); });
            m.addSubMenu ("Analyser", an);
            m.addSubMenu ("Display range", rg);
            m.addItem ("Auto gain", true, eq.getInt (eq.g (EQ::gAutoGain)) > 0, [this] { eq.set (eq.g (EQ::gAutoGain), eq.getInt (eq.g (EQ::gAutoGain)) > 0 ? 0.0f : 1.0f); });
            m.addSeparator();
            m.addItem ("Turn all bands off", [this]
            {
                eq.edit.getUndoManager().beginNewTransaction ("All bands off");
                for (int b = 0; b < EQ::numBands; ++b) eq.set (EQ::idx (b, EQ::pOn), 0.0f);
            });
            m.showMenuAsync ({});
        }

    private:
        void timerCallback() override
        {
            const int mode = eq.getInt (eq.g (EQ::gAnalyser));
            bool moving = false;
            for (int b = 0; b < EQ::numBands; ++b) moving = moving || (eq.isBandOn (b) && eq.bandMoves (b));
            if (mode > 0)
            {
                analyse (eq.post, post);
                if (mode == 2) analyse (eq.pre, pre);
            }
            double sig = 0;
            for (int i = 0; i < eq.numParams(); ++i) sig = sig * 1.0001 + eq.get (i) * (i % 13 + 1);
            if (mode > 0 || moving || sig != lastSig) { lastSig = sig; repaint(); }
        }

        void analyse (const pulse::AnalyserFifo& fifo, std::array<float, fftSize / 2>& out)
        {
            std::array<float, fftSize * 2> buf {};
            fifo.read (buf.data(), fftSize);
            for (int i = 0; i < fftSize; ++i) buf[(size_t) i] *= 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (fftSize - 1));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int i = 0; i < fftSize / 2; ++i)
            {
                const float db = Decibels::gainToDecibels (buf[(size_t) i] * 4.0f / fftSize, -120.0f);
                auto& o = out[(size_t) i];
                o = db > o ? db : o * 0.85f + db * 0.15f;   // fast up, slow down
            }
        }

        EQ& eq;
        te::Plugin::Ptr holder;
        std::function<void (int)> select;
        dsp::FFT fft;
        std::array<float, fftSize / 2> pre {}, post {};
        Rectangle<float> plot;
        juce::Point<float> mouse { -100.0f, -100.0f };
        int hover = -1, hoverQ = -1, dragging = -1, dragMode = 0;
        double startFreq = 1000, startGain = 0, startQ = 1, lastSig = -1;
    };

    //==============================================================================
    /** A row of small toggle buttons for a choice parameter (e.g. placement). */
    class Choice : public Component, private Timer
    {
    public:
        Choice (te::AutomatableParameter& p, StringArray labels, Colour c) : param (&p)
        {
            for (int i = 0; i < labels.size(); ++i)
            {
                auto* b = buttons.add (new TextButton (labels[i]));
                b->setColour (TextButton::buttonOnColourId, c.withAlpha (0.85f));
                b->setColour (TextButton::textColourOnId, Colour (0xff111317));
                b->setConnectedEdges ((i > 0 ? Button::ConnectedOnLeft : 0) | (i < labels.size() - 1 ? Button::ConnectedOnRight : 0));
                b->onClick = [this, i] { param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName()); param->setParameter ((float) i, sendNotificationSync); };
                addAndMakeVisible (b);
            }
            startTimerHz (10);
            timerCallback();
        }
        void resized() override
        {
            auto r = getLocalBounds();
            const int w = r.getWidth() / jmax (1, buttons.size());
            for (auto* b : buttons) b->setBounds (r.removeFromLeft (w));
        }
    private:
        void timerCallback() override
        {
            const int v = roundToInt (param->getCurrentValue());
            for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == v, dontSendNotification);
        }
        te::AutomatableParameter::Ptr param;
        OwnedArray<TextButton> buttons;
    };

    /** Filter shape buttons (each draws its shape). */
    class TypeButtons : public Component, private Timer
    {
    public:
        TypeButtons (te::AutomatableParameter& p, Colour c) : param (&p), colour (c) { startTimerHz (10); }
        void paint (Graphics& g) override
        {
            const int n = EQ::numTypes;
            const float w = (float) getWidth() / n;
            const int v = roundToInt (param->getCurrentValue());
            for (int i = 0; i < n; ++i)
            {
                auto r = Rectangle<float> (w * i, 0.0f, w, (float) getHeight()).reduced (1.5f);
                g.setColour (i == v ? colour.withAlpha (0.85f) : (i == hover ? Theme::raised : Theme::panel2));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (i == v ? Colour (0xff111317) : Theme::text);
                g.strokePath (shapeIcon (i, r.reduced (r.getWidth() * 0.18f, r.getHeight() * 0.25f)), PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
            }
        }
        void mouseMove (const MouseEvent& e) override { const int h = e.x * EQ::numTypes / jmax (1, getWidth()); if (h != hover) { hover = h; setTooltip (EQ::typeNames[h]); repaint(); } }
        void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            const int t = jlimit (0, EQ::numTypes - 1, e.x * EQ::numTypes / jmax (1, getWidth()));
            param->getEdit().getUndoManager().beginNewTransaction ("Band shape");
            param->setParameter ((float) t, sendNotificationSync);
        }
        void setTooltip (const String& t) { tip = t; }
        String getTooltip() { return tip; }
    private:
        void timerCallback() override { const int v = roundToInt (param->getCurrentValue()); if (v != last) { last = v; repaint(); } }
        te::AutomatableParameter::Ptr param;
        Colour colour;
        String tip;
        int hover = -1, last = -1;
    };

    //==============================================================================
    /** The whole editor: graph, band chips, selected band panel, output section. */
    class View : public Component, private Timer
    {
    public:
        View (EditViewState& e, EQ& q, bool isLarge) : evs (e), eq (q), holder (&q), large (isLarge), graph (q, [this] (int b) { showBand (b); })
        {
            graph.large = large;
            addAndMakeVisible (graph);
            for (int b = 0; b < EQ::numBands; ++b)
            {
                auto* chip = chips.add (new Chip (*this, b));
                addAndMakeVisible (chip);
            }
            for (auto* l : { &bandTitle, &dynTitle, &moveTitle, &outTitle })
            {
                l->setFont (FontOptions (11.0f, Font::bold));
                l->setColour (Label::textColourId, Theme::muted);
                addAndMakeVisible (l);
            }
            dynTitle.setText ("DYNAMIC  (gain moves with the level)", dontSendNotification);
            moveTitle.setText ("MOVEMENT  (LFO or envelope moves the band)", dontSendNotification);
            outTitle.setText ("OUTPUT", dontSendNotification);
            for (int k : { EQ::gOutput, EQ::gMix })
                addAndMakeVisible (outKnobs.add (new ParamKnob (evs, *eq.param (eq.g (k)))));
            autoGain = std::make_unique<Choice> (*eq.param (eq.g (EQ::gAutoGain)), StringArray { "Auto gain off", "On" }, Theme::accent);
            addAndMakeVisible (*autoGain);
            showBand (2);
            startTimerHz (8);
        }

        void showBand (int b)
        {
            graph.selected = b;
            band = b;
            knobs.clear();
            dynKnobs.clear();
            modKnobs.clear();
            typeButtons = std::make_unique<TypeButtons> (*eq.param (EQ::idx (b, EQ::pType)), bandColour (b));
            addAndMakeVisible (*typeButtons);
            place = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pPlace)), StringArray { "St", "L", "R", "M", "S" }, bandColour (b));
            addAndMakeVisible (*place);
            slope = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pSlope)), StringArray { "6", "12", "18", "24", "30", "36", "48", "72", "96" }, bandColour (b));
            addAndMakeVisible (*slope);
            onOff = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pOn)), StringArray { "Off", "On" }, Theme::green);
            addAndMakeVisible (*onOff);
            solo = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pSolo)), StringArray { "Solo off", "Solo" }, Theme::yellow);
            addAndMakeVisible (*solo);
            dyn = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pDyn)), StringArray { "Off", "On" }, bandColour (b));
            addAndMakeVisible (*dyn);
            modSrc = std::make_unique<Choice> (*eq.param (EQ::idx (b, EQ::pModSrc)), EQ::modSources, bandColour (b));
            addAndMakeVisible (*modSrc);
            auto make = [&] (OwnedArray<ParamKnob>& list, int k, const char* label)
            {
                auto* kn = list.add (new ParamKnob (evs, *eq.param (EQ::idx (b, k))));
                kn->setLabel (label);
                addAndMakeVisible (kn);
            };
            make (knobs, EQ::pFreq, "Freq");      make (knobs, EQ::pGain, "Gain");          make (knobs, EQ::pQ, "Q");
            make (dynKnobs, EQ::pRange, "Range"); make (dynKnobs, EQ::pThresh, "Threshold"); make (dynKnobs, EQ::pAttack, "Attack"); make (dynKnobs, EQ::pRelease, "Release");
            make (modKnobs, EQ::pModShape, "Shape"); make (modKnobs, EQ::pModRate, "Rate"); make (modKnobs, EQ::pModSync, "Sync");
            make (modKnobs, EQ::pModFreq, "Freq move"); make (modKnobs, EQ::pModGain, "Gain move");
            bandTitle.setText ("BAND " + String (b + 1), dontSendNotification);
            bandTitle.setColour (Label::textColourId, bandColour (b));
            for (auto* c : chips) c->repaint();
            resized();
            graph.repaint();
        }

        int selectedBand() const { return band; }
        Graph& getGraph() { return graph; }

        void paint (Graphics& g) override
        {
            g.fillAll (Theme::panel);
            if (! large) return;
            g.setColour (Theme::line);
            for (auto r : sectionRects) g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (large ? 8 : 2);
            sectionRects.clear();
            if (! large)
            {
                // compact (device card): graph, chips, and the band's main row
                auto row = r.removeFromBottom (54);
                auto chipRow = r.removeFromBottom (22);
                graph.setBounds (r.withTrimmedBottom (2));
                layoutChips (chipRow);
                for (auto* c : std::initializer_list<Component*> { place.get(), slope.get(), solo.get(), dyn.get(), modSrc.get(), &dynTitle, &moveTitle, &outTitle, autoGain.get(), &bandTitle })
                    if (c != nullptr) c->setVisible (false);
                for (auto* k : dynKnobs) k->setVisible (false);
                for (auto* k : modKnobs) k->setVisible (false);
                for (auto* k : outKnobs) k->setVisible (false);
                auto left = row.removeFromLeft (row.getWidth() - 3 * 56);
                typeButtons->setBounds (left.removeFromTop (26).reduced (2, 2));
                onOff->setBounds (left.removeFromTop (22).withWidth (90).reduced (2, 1));
                for (auto* k : knobs) k->setBounds (row.removeFromLeft (56));
                return;
            }

            auto panelArea = r.removeFromBottom (jmin (230, r.getHeight() / 2));
            r.removeFromBottom (6);
            auto chipRow = r.removeFromBottom (26);
            r.removeFromBottom (4);
            graph.setBounds (r);
            layoutChips (chipRow);

            constexpr int kw = 66, kh = 78;
            // band section: shape, slope, placement, on / solo, freq / gain / Q
            auto bandArea = panelArea.removeFromLeft (jmax (380, panelArea.getWidth() * 4 / 10));
            sectionRects.add (bandArea);
            auto ba = bandArea.reduced (8);
            auto tr = ba.removeFromTop (18);
            bandTitle.setBounds (tr.removeFromLeft (80));
            onOff->setBounds (tr.removeFromRight (90));
            tr.removeFromRight (6);
            solo->setBounds (tr.removeFromRight (100));
            ba.removeFromTop (4);
            typeButtons->setBounds (ba.removeFromTop (30));
            ba.removeFromTop (4);
            auto sl = ba.removeFromTop (22);
            slope->setBounds (sl.removeFromLeft (sl.getWidth() * 62 / 100));
            sl.removeFromLeft (6);
            place->setBounds (sl);
            ba.removeFromTop (4);
            for (auto* k : knobs) k->setBounds (ba.removeFromLeft (kw).withHeight (kh));
            panelArea.removeFromLeft (6);

            // output section (right)
            auto outArea = panelArea.removeFromRight (2 * kw + 20);
            sectionRects.add (outArea);
            auto oa = outArea.reduced (8);
            outTitle.setBounds (oa.removeFromTop (18));
            autoGain->setBounds (oa.removeFromTop (22));
            oa.removeFromTop (4);
            for (auto* k : outKnobs) k->setBounds (oa.removeFromLeft (kw).withHeight (kh));
            panelArea.removeFromRight (6);

            // dynamic (top) and movement (bottom)
            auto dynArea = panelArea.removeFromTop (panelArea.getHeight() / 2 - 2);
            panelArea.removeFromTop (4);
            sectionRects.add (dynArea);
            sectionRects.add (panelArea);
            auto da = dynArea.reduced (8, 4);
            auto dt = da.removeFromTop (18);
            dyn->setBounds (dt.removeFromRight (80));
            dynTitle.setBounds (dt);
            for (auto* k : dynKnobs) k->setBounds (da.removeFromLeft (kw).withHeight (jmin (kh, da.getHeight())));
            auto ma = panelArea.reduced (8, 4);
            auto mt = ma.removeFromTop (18);
            modSrc->setBounds (mt.removeFromRight (180));
            moveTitle.setBounds (mt);
            for (auto* k : modKnobs) k->setBounds (ma.removeFromLeft (kw).withHeight (jmin (kh, ma.getHeight())));
        }

    private:
        /** A band's chip: number, shape and the main values; click selects, double-click turns on / off. */
        struct Chip : public Component
        {
            Chip (View& v, int b) : owner (v), band (b) {}
            void paint (Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (1.5f);
                const bool on = owner.eq.isBandOn (band), sel = owner.band == band;
                const auto c = bandColour (band);
                g.setColour (sel ? c.withAlpha (0.28f) : Theme::panel2);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (sel ? c : Theme::line);
                g.drawRoundedRectangle (r, 4.0f, sel ? 1.5f : 1.0f);
                auto dot = r.removeFromLeft (r.getHeight()).reduced (4.0f);
                g.setColour (on ? c : c.withAlpha (0.3f));
                g.fillEllipse (dot);
                g.setColour (Colour (0xff111317));
                g.setFont (FontOptions (10.0f, Font::bold));
                g.drawText (String (band + 1), dot, Justification::centred);
                auto icon = r.removeFromLeft (r.getHeight() * 1.3f).reduced (3.0f, 5.0f);
                g.setColour (on ? Theme::text : Theme::muted);
                g.strokePath (shapeIcon (owner.eq.bandType (band), icon), PathStrokeType (1.3f));
                g.setFont (FontOptions (11.0f));
                String t = owner.eq.param (EQ::idx (band, EQ::pFreq))->getCurrentValueAsString();
                if (EQ::typeUsesGain (owner.eq.bandType (band))) t << "  " << owner.eq.param (EQ::idx (band, EQ::pGain))->getCurrentValueAsString();
                if (owner.eq.bandMoves (band)) t << "  ~";
                g.drawText (on ? t : String ("off"), r.reduced (3.0f, 0.0f), Justification::centredLeft, true);
            }
            void mouseDown (const MouseEvent&) override { owner.showBand (band); }
            void mouseDoubleClick (const MouseEvent&) override
            {
                owner.eq.edit.getUndoManager().beginNewTransaction ("Band on / off");
                owner.eq.set (EQ::idx (band, EQ::pOn), owner.eq.isBandOn (band) ? 0.0f : 1.0f);
            }
            View& owner;
            int band;
        };

        void layoutChips (Rectangle<int> row)
        {
            const int w = row.getWidth() / EQ::numBands;
            for (auto* c : chips) c->setBounds (row.removeFromLeft (w));
        }

        void timerCallback() override { for (auto* c : chips) c->repaint(); }

        EditViewState& evs;
        EQ& eq;
        te::Plugin::Ptr holder;
        bool large;
        Graph graph;
        int band = 2;
        OwnedArray<Chip> chips;
        OwnedArray<ParamKnob> knobs, dynKnobs, modKnobs, outKnobs;
        std::unique_ptr<TypeButtons> typeButtons;
        std::unique_ptr<Choice> place, slope, onOff, solo, dyn, modSrc, autoGain;
        Label bandTitle, dynTitle, moveTitle, outTitle;
        Array<Rectangle<int>> sectionRects;
    };
}
