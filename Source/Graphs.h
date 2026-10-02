#pragma once

#include "Common.h"

/** Interactive device graphs. Every node can be dragged and is wired to the device's real
    parameters (so the knobs move too, with undo and automation gestures); double-click a node to
    reset what it controls. */
namespace Graphs
{
    using Param = te::AutomatableParameter::Ptr;

    inline float norm (const Param& p)             { return p->valueRange.convertTo0to1 (p->getCurrentBaseValue()); }
    inline void setNorm (const Param& p, float n)  { p->setParameter (p->valueRange.snapToLegalValue (p->valueRange.convertFrom0to1 (jlimit (0.0f, 1.0f, n))), sendNotificationSync); }
    inline void setValue (const Param& p, float v) { p->setParameter (p->valueRange.snapToLegalValue (jlimit (p->valueRange.start, p->valueRange.end, v)), sendNotificationSync); }

    inline void begin (std::initializer_list<Param> ps)
    {
        for (auto& p : ps) if (p != nullptr) { p->getEdit().getUndoManager().beginNewTransaction ("Change " + p->getParameterName()); break; }
        for (auto& p : ps) if (p != nullptr) p->parameterChangeGestureBegin();
    }
    inline void end (std::initializer_list<Param> ps) { for (auto& p : ps) if (p != nullptr) p->parameterChangeGestureEnd(); }
    inline void reset (const Param& p) { if (p != nullptr) if (auto d = p->getDefaultValue()) p->setParameter (*d, sendNotificationSync); }

    inline void panel (Graphics& g, Rectangle<float> r)
    {
        g.setGradientFill (ColourGradient (Theme::bg2, 0.0f, r.getY(), Theme::well, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }

    /** True when a graph has room for axis labels and a finer grid (in the big device editor). */
    inline bool isLarge (const Component& c) { return c.getWidth() >= 300 && c.getHeight() >= 150; }

    inline void label (Graphics& g, const String& t, Rectangle<float> r, Justification j, float alpha = 0.75f)
    {
        g.setColour (Theme::muted.withAlpha (alpha));
        g.setFont (FontOptions (9.5f));
        g.drawText (t, r, j, false);
    }

    /** A curve with a soft glow under it. */
    inline void glowStroke (Graphics& g, const Path& p, Colour c, float width)
    {
        g.setColour (c.withAlpha (0.12f));
        g.strokePath (p, PathStrokeType (width + 5.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, PathStrokeType (width + 2.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, PathStrokeType (width, PathStrokeType::curved, PathStrokeType::rounded));
    }

    inline void node (Graphics& g, juce::Point<float> c, Colour col, bool hot)
    {
        const float r = hot ? 5.5f : 4.5f;
        if (hot)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (c.x - r - 5.0f, c.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f);
        }
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillEllipse (c.x - r - 1.5f, c.y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
        g.setColour (hot ? Colours::white : col);
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        g.setColour (Colours::black.withAlpha (0.35f));
        g.fillEllipse (c.x - r * 0.4f, c.y - r * 0.4f, r * 0.8f, r * 0.8f);
    }

    /** Base: polls its parameters and repaints when they move; tracks hover / drag. */
    class GraphBase : public Component, public SettableTooltipClient, private Timer
    {
    public:
        GraphBase() { startTimerHz (20); }
    protected:
        virtual double signature() const = 0;
        int hot = -1, dragging = -1;
        String readout;
        juce::Point<float> readoutPos;
        void drawReadout (Graphics& g)
        {
            if (readout.isEmpty() || (hot < 0 && dragging < 0)) return;
            g.setFont (FontOptions (10.5f, Font::bold));
            const float w = (float) GlyphArrangement::getStringWidth (g.getCurrentFont(), readout) + 10.0f;
            auto r = Rectangle<float> (w, 16.0f).withCentre (readoutPos.translated (0.0f, -16.0f)).constrainedWithin (getLocalBounds().toFloat().reduced (2.0f));
            g.setColour (Colour (0xee22262d));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Theme::text);
            g.drawText (readout, r, Justification::centred);
        }
    private:
        void timerCallback() override
        {
            const double s = signature();
            if (s != lastSig) { lastSig = s; repaint(); }
        }
        double lastSig = -1;
    };

    //==============================================================================
    /** One or more ADSR envelopes drawn over each other. Nodes: attack peak (time), decay end
        (time + sustain level) and release end (time). */
    class EnvelopeGraph : public GraphBase
    {
    public:
        struct Env { Param a, d, s, r; Colour colour; String name; };

        explicit EnvelopeGraph (std::vector<Env> e) : envs (std::move (e))
        {
            setTooltip ("Drag the points to shape the envelope (double-click a point to reset it)");
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            panel (g, b);
            const bool large = isLarge (*this);
            area = b.reduced (8.0f, 10.0f).withTrimmedTop (6.0f);
            if (large) area = area.withTrimmedLeft (26.0f).withTrimmedBottom (16.0f).withTrimmedTop (8.0f);
            g.setColour (Theme::ink.withAlpha (0.05f));
            for (int i = 1; i < 4; ++i) g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * i / 4.0f), area.getX(), area.getRight());
            if (large)
            {
                for (int i = 0; i <= 4; ++i)
                    label (g, String (100 - i * 25) + "%", { b.getX() + 2.0f, area.getY() + area.getHeight() * i / 4.0f - 6.0f, 26.0f, 12.0f }, Justification::centredLeft, 0.55f);
                auto pts = points (0);
                const auto& e = envs[0];
                // phase bands, with their values under the axis
                const float xs[] = { area.getX(), pts[0].x, pts[1].x, pts[2].x, pts[3].x };
                const char* names[] = { "A", "D", "S", "R" };
                const Param ps[] = { e.a, e.d, e.s, e.r };
                String legend;
                for (int i = 0; i < 4; ++i)
                {
                    if (ps[i] == nullptr) continue;
                    if (i % 2 == 1) { g.setColour (Theme::ink.withAlpha (0.025f)); g.fillRect (Rectangle<float> (xs[i], area.getY(), xs[i + 1] - xs[i], area.getHeight())); }
                    legend << names[i] << " " << ps[i]->getCurrentValueAsStringWithLabel() << "     ";
                }
                label (g, (envs.size() > 1 ? e.name + "   " : String()) + legend.trimEnd(),
                       Rectangle<float> (area.getX(), area.getBottom() + 2.0f, area.getWidth(), 13.0f), Justification::centredLeft, 0.75f);
                if (e.s != nullptr)
                {
                    g.setColour (e.colour.withAlpha (0.35f));
                    const float dashes[] = { 3.0f, 3.0f };
                    g.drawDashedLine (Line<float> (area.getX(), pts[1].y, area.getRight(), pts[1].y), dashes, 2, 1.0f);
                }
            }

            for (int ei = (int) envs.size(); --ei >= 0;)
            {
                auto pts = points (ei);
                const auto& e = envs[(size_t) ei];
                Path p;
                p.startNewSubPath (area.getX(), area.getBottom());
                p.lineTo (pts[0]);
                p.quadraticTo ({ pts[0].x + (pts[1].x - pts[0].x) * 0.25f, pts[1].y }, pts[1]);   // exponential-looking decay
                p.lineTo (pts[2]);
                p.quadraticTo ({ pts[2].x + (pts[3].x - pts[2].x) * 0.25f, pts[3].y }, pts[3]);
                Path fill (p);
                fill.lineTo (area.getX(), area.getBottom());
                fill.closeSubPath();
                g.setGradientFill (ColourGradient (e.colour.withAlpha (ei == 0 ? 0.32f : 0.12f), 0.0f, area.getY(),
                                                   e.colour.withAlpha (0.02f), 0.0f, area.getBottom(), false));
                g.fillPath (fill);
                if (ei == 0) glowStroke (g, p, e.colour, large ? 2.2f : 1.6f);
                else { g.setColour (e.colour.withAlpha (0.7f)); g.strokePath (p, PathStrokeType (1.4f)); }
                for (int k : { 0, 1, 3 })
                    if (k != 1 || e.d != nullptr || e.s != nullptr)
                        node (g, pts[(size_t) k], e.colour, hot == ei * 4 + k || dragging == ei * 4 + k);
                if (envs.size() > 1)
                {
                    g.setFont (FontOptions (9.5f, Font::bold));
                    g.drawText (e.name, Rectangle<float> (area.getX() + ei * 70.0f, b.getY() + 2.0f, 68.0f, 12.0f), Justification::centredLeft);
                }
            }
            drawReadout (g);
        }

        void mouseMove (const MouseEvent& e) override { const int h = hit (e.position); if (h != hot) { hot = h; updateReadout(); repaint(); } }
        void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }

        void mouseDown (const MouseEvent& e) override
        {
            dragging = hit (e.position);
            if (dragging < 0) return;
            auto& en = envs[(size_t) (dragging / 4)];
            begin ({ en.a, en.d, en.s, en.r });
            mouseDrag (e);
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            const int ei = dragging / 4, k = dragging % 4;
            auto& en = envs[(size_t) ei];
            auto pts = points (ei);
            const float segW = area.getWidth() * segFrac;
            if (k == 0) setNorm (en.a, (e.position.x - area.getX()) / segW);
            if (k == 1)
            {
                if (en.d != nullptr) setNorm (en.d, (e.position.x - pts[0].x) / segW);
                if (en.s != nullptr) setValue (en.s, jlimit (0.0f, 1.0f, (area.getBottom() - e.position.y) / area.getHeight()));
            }
            if (k == 3) setNorm (en.r, (e.position.x - pts[2].x) / segW);
            updateReadout();
            repaint();
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dragging >= 0) { auto& en = envs[(size_t) (dragging / 4)]; end ({ en.a, en.d, en.s, en.r }); }
            dragging = -1;
        }

        void mouseDoubleClick (const MouseEvent& e) override
        {
            const int h = hit (e.position);
            if (h < 0) return;
            auto& en = envs[(size_t) (h / 4)];
            en.a->getEdit().getUndoManager().beginNewTransaction ("Reset envelope");
            if (h % 4 == 0) reset (en.a);
            if (h % 4 == 1) { reset (en.d); reset (en.s); }
            if (h % 4 == 3) reset (en.r);
        }

    private:
        static constexpr float segFrac = 0.28f, sustainFrac = 0.16f;

        std::array<juce::Point<float>, 4> points (int ei) const
        {
            const auto& e = envs[(size_t) ei];
            const float segW = area.getWidth() * segFrac;
            const float s = e.s != nullptr ? jlimit (0.0f, 1.0f, e.s->getCurrentBaseValue()) : 1.0f;
            const float xa = area.getX() + segW * norm (e.a);
            const float xd = xa + (e.d != nullptr ? segW * norm (e.d) : 0.0f);
            const float xs = xd + area.getWidth() * sustainFrac;
            const float xr = xs + segW * norm (e.r);
            const float ys = area.getBottom() - s * area.getHeight();
            return { juce::Point<float> (xa, area.getY()), { xd, ys }, { xs, ys }, { xr, area.getBottom() } };
        }

        int hit (juce::Point<float> p) const
        {
            for (int ei = 0; ei < (int) envs.size(); ++ei)
            {
                auto pts = points (ei);
                for (int k : { 0, 1, 3 })
                {
                    if (k == 1 && envs[(size_t) ei].d == nullptr && envs[(size_t) ei].s == nullptr) continue;
                    if (pts[(size_t) k].getDistanceFrom (p) < 9.0f) return ei * 4 + k;
                }
            }
            return -1;
        }

        void updateReadout()
        {
            const int h = dragging >= 0 ? dragging : hot;
            if (h < 0) { readout = {}; return; }
            const auto& e = envs[(size_t) (h / 4)];
            auto pts = points (h / 4);
            readoutPos = pts[(size_t) (h % 4)];
            if (h % 4 == 0) readout = "A " + e.a->getCurrentValueAsStringWithLabel();
            if (h % 4 == 1) readout = (e.d != nullptr ? "D " + e.d->getCurrentValueAsStringWithLabel() : String()) + (e.s != nullptr ? "  S " + e.s->getCurrentValueAsStringWithLabel() : String());
            if (h % 4 == 3) readout = "R " + e.r->getCurrentValueAsStringWithLabel();
        }

        double signature() const override
        {
            double s = 0;
            for (auto& e : envs) for (auto& p : { e.a, e.d, e.s, e.r }) if (p != nullptr) s = s * 1.37 + p->getCurrentBaseValue();
            return s;
        }

        std::vector<Env> envs;
        Rectangle<float> area;
    };

    //==============================================================================
    /** Filter response with a draggable cutoff / resonance node (drag up for more resonance,
        scroll to change resonance). `modeOf` maps the type parameter to 0 off, 1 low, 2 band,
        3 high, 4 notch, 5 low 24 dB. */
    class FilterGraph : public GraphBase
    {
    public:
        FilterGraph (Param cutoffP, Param resoP, Param typeP, std::function<int (int)> modeOfType, Colour c, Param envAmountP = nullptr)
            : cutoff (cutoffP), reso (resoP), type (typeP), envAmount (envAmountP), modeOf (std::move (modeOfType)), colour (c)
        {
            setTooltip ("Drag the point: left / right = cutoff, up / down = resonance (scroll also sets resonance; double-click resets)");
        }

        static float xForHz (Rectangle<float> a, double hz) { return a.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * a.getWidth(); }
        static double hzForX (Rectangle<float> a, float x) { return 20.0 * std::pow (1000.0, (x - a.getX()) / a.getWidth()); }

        int mode() const { return type != nullptr && modeOf ? modeOf (roundToInt (type->getCurrentBaseValue())) : 1; }

        /** Magnitude (dB) of a 2-pole state variable filter. */
        static float responseDb (int m, double f, double fc, float res)
        {
            const double q = 0.5 + res * res * 12.0, w = f / fc;
            const std::complex<double> s (0.0, w);
            const auto den = s * s + s / q + 1.0;
            std::complex<double> h;
            switch (m)
            {
                case 2:  h = (s / q) / den; break;
                case 3:  h = (s * s) / den; break;
                case 4:  h = (s * s + 1.0) / den; break;
                case 5:  h = 1.0 / den; h = h * h; break;
                default: h = 1.0 / den; break;
            }
            return (float) Decibels::gainToDecibels (std::abs (h), -60.0);
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            panel (g, b);
            const bool large = isLarge (*this);
            area = b.reduced (6.0f, 8.0f);
            if (large)
            {
                area = area.withTrimmedLeft (28.0f).withTrimmedBottom (14.0f);
                for (int dec = 10; dec <= 10000; dec *= 10)
                    for (int k = 1; k <= 9; ++k)
                    {
                        const double f = dec * k;
                        if (f < 20.0 || f > 20000.0) continue;
                        g.setColour (Theme::ink.withAlpha (k == 1 ? 0.08f : 0.03f));
                        g.drawVerticalLine ((int) xForHz (area, f), area.getY(), area.getBottom());
                    }
                for (auto [f, t] : { std::pair<double, const char*> { 20.0, "20" }, { 50.0, "50" }, { 100.0, "100" }, { 200.0, "200" }, { 500.0, "500" },
                                     { 1000.0, "1k" }, { 2000.0, "2k" }, { 5000.0, "5k" }, { 10000.0, "10k" }, { 20000.0, "20k" } })
                    label (g, t, Rectangle<float> (xForHz (area, f) - 14.0f, area.getBottom() + 2.0f, 28.0f, 11.0f), Justification::centred);
                for (int db = 18; db >= -24; db -= 6)
                {
                    g.setColour (Theme::ink.withAlpha (db == 0 ? 0.12f : 0.035f));
                    g.drawHorizontalLine ((int) yForDb ((float) db), area.getX(), area.getRight());
                    label (g, (db > 0 ? "+" : "") + String (db), Rectangle<float> (b.getX() + 2.0f, yForDb ((float) db) - 6.0f, 26.0f, 12.0f), Justification::centredRight, 0.55f);
                }
            }
            else
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xForHz (area, f), area.getY(), area.getBottom());
                g.drawHorizontalLine ((int) yForDb (0.0f), area.getX(), area.getRight());
                g.setColour (Theme::muted.withAlpha (0.6f));
                g.setFont (FontOptions (9.0f));
                for (auto [f, t] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
                    g.drawText (t, Rectangle<float> (xForHz (area, f) + 2.0f, area.getBottom() - 10.0f, 24.0f, 10.0f), Justification::left);
            }

            const int m = mode();
            const double fc = cutoff->getCurrentBaseValue();
            const float r = reso != nullptr ? reso->getCurrentBaseValue() : 0.2f;
            if (m == 0)
            {
                g.setColour (Theme::muted);
                g.setFont (FontOptions (11.0f));
                g.drawText ("Filter off", area, Justification::centred);
                return;
            }
            // envelope sweep range
            if (envAmount != nullptr && std::abs (envAmount->getCurrentBaseValue()) > 0.01f)
            {
                const double fe = jlimit (20.0, 20000.0, fc * std::pow (2.0, envAmount->getCurrentBaseValue() * 6.0));
                g.setColour (colour.withAlpha (0.12f));
                const float xa = xForHz (area, jmin (fc, fe)), xb = xForHz (area, jmax (fc, fe));
                g.fillRect (Rectangle<float> (xa, area.getY(), xb - xa, area.getHeight()));
            }
            Path p, fill;
            for (float x = area.getX(); x <= area.getRight(); x += large ? 1.0f : 1.5f)
            {
                const float y = jlimit (area.getY(), area.getBottom(), yForDb (responseDb (m, hzForX (area, x), fc, r)));
                if (x == area.getX()) { p.startNewSubPath (x, y); fill.startNewSubPath (x, area.getBottom()); fill.lineTo (x, y); }
                else { p.lineTo (x, y); fill.lineTo (x, y); }
            }
            fill.lineTo (area.getRight(), area.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (colour.withAlpha (0.35f), 0.0f, area.getY(), colour.withAlpha (0.03f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            glowStroke (g, p, colour, large ? 2.2f : 1.6f);
            if (large)
            {
                const float xc = xForHz (area, fc);
                g.setColour (colour.withAlpha (0.4f));
                const float dashes[] = { 3.0f, 3.0f };
                g.drawDashedLine (Line<float> (xc, area.getY(), xc, area.getBottom()), dashes, 2, 1.0f);
                const char* modeNames[] = { "Off", "Low pass 12", "Band pass", "High pass", "Notch", "Low pass 24" };
                label (g, String (modeNames[jlimit (0, 5, m)]) + "   " + cutoff->getCurrentValueAsStringWithLabel()
                          + (reso != nullptr ? "   Res " + reso->getCurrentValueAsStringWithLabel() : String()),
                       area.reduced (6.0f, 4.0f).removeFromTop (12.0f), Justification::topRight, 0.9f);
            }
            node (g, nodePos(), colour, hot >= 0 || dragging >= 0);
            drawReadout (g);
        }

        void mouseMove (const MouseEvent& e) override { const int h = nodePos().getDistanceFrom (e.position) < 10.0f ? 0 : -1; if (h != hot) { hot = h; updateReadout(); repaint(); } }
        void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            dragging = 0;   // grab from anywhere: the node jumps to the mouse
            begin ({ cutoff, reso });
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            setValue (cutoff, (float) hzForX (area, e.position.x));
            if (reso != nullptr && mode() != 0)
            {
                // the node sits on the resonant peak: height above 0 dB sets resonance
                const float db = dbForY (e.position.y);
                setValue (reso, jlimit (0.0f, 1.0f, std::sqrt (jmax (0.0f, db) / 18.0f)));
            }
            updateReadout();
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (dragging >= 0) end ({ cutoff, reso }); dragging = -1; }
        void mouseDoubleClick (const MouseEvent&) override
        {
            cutoff->getEdit().getUndoManager().beginNewTransaction ("Reset filter");
            reset (cutoff); reset (reso);
        }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            if (reso == nullptr) return;
            reso->getEdit().getUndoManager().beginNewTransaction ("Change resonance");
            setValue (reso, reso->getCurrentBaseValue() + w.deltaY * 0.1f);
        }

    private:
        float yForDb (float db) const { return area.getY() + area.getHeight() * (18.0f - db) / 42.0f; }   // +18 .. -24 dB
        float dbForY (float y) const  { return 18.0f - (y - area.getY()) / area.getHeight() * 42.0f; }

        juce::Point<float> nodePos() const
        {
            const double fc = cutoff->getCurrentBaseValue();
            const float r = reso != nullptr ? reso->getCurrentBaseValue() : 0.0f;
            return { xForHz (area, fc), jlimit (area.getY(), area.getBottom(), yForDb (r * r * 18.0f)) };
        }

        void updateReadout()
        {
            readoutPos = nodePos();
            readout = cutoff->getCurrentValueAsStringWithLabel() + (reso != nullptr ? "  Q " + reso->getCurrentValueAsStringWithLabel() : String());
        }

        double signature() const override
        {
            double s = cutoff->getCurrentBaseValue();
            for (auto& p : { reso, type, envAmount }) if (p != nullptr) s = s * 1.37 + p->getCurrentBaseValue();
            return s;
        }

        Param cutoff, reso, type, envAmount;
        std::function<int (int)> modeOf;
        Colour colour;
        Rectangle<float> area;
    };

    //==============================================================================
    /** Static transfer curve (input dB -> output dB) for compressors and limiters. One node: drag
        left / right for the threshold, up / down for the ratio (when given). */
    class DynamicsGraph : public GraphBase
    {
    public:
        /** curve: (inputDb) -> outputDb using the current settings. */
        DynamicsGraph (Param thresholdP, Param ratioP, std::function<float (float)> curve, Colour c, std::function<float()> grNow = {})
            : threshold (thresholdP), ratio (ratioP), transfer (std::move (curve)), colour (c), gr (std::move (grNow))
        {
            setTooltip ("Drag the point: left / right = threshold" + String (ratioP != nullptr ? ", up / down = ratio" : String()) + " (double-click resets)");
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            panel (g, b);
            const bool large = isLarge (*this);
            area = b.reduced (8.0f);
            if (gr) { auto bar = area.removeFromRight (large ? 12.0f : 6.0f); area.removeFromRight (4.0f); drawGR (g, bar); }
            if (large) area = area.withTrimmedLeft (26.0f).withTrimmedBottom (14.0f);
            for (float db : { -54.0f, -48.0f, -42.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f })
            {
                const bool major = std::fmod (-db, 12.0f) < 0.1f;
                if (! large && (! major || db == 0.0f)) continue;
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.12f : (major ? 0.06f : 0.025f)));
                g.drawVerticalLine ((int) xFor (db), area.getY(), area.getBottom());
                g.drawHorizontalLine ((int) yFor (db), area.getX(), area.getRight());
                if (large && major)
                {
                    label (g, String ((int) db), { xFor (db) - 14.0f, area.getBottom() + 2.0f, 28.0f, 11.0f }, Justification::centred, 0.55f);
                    label (g, String ((int) db), { b.getX() + 2.0f, yFor (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
                }
            }
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 0.8f);
            if (large)
            {
                const float xt = xFor (threshold->getCurrentBaseValue());
                g.setColour (colour.withAlpha (0.06f));
                g.fillRect (Rectangle<float> (xt, area.getY(), area.getRight() - xt, area.getHeight()));
                label (g, "IN (dB)", area.reduced (4.0f).removeFromBottom (12.0f), Justification::bottomRight, 0.45f);
                label (g, "OUT (dB)", area.reduced (4.0f).removeFromTop (12.0f), Justification::topLeft, 0.45f);
            }
            Path p;
            for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
            {
                const float in = dbForX (x), out = transfer (in);
                const float y = jlimit (area.getY(), area.getBottom(), yFor (out));
                if (x == area.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            glowStroke (g, p, colour, large ? 2.4f : 2.0f);
            node (g, nodePos(), colour, hot >= 0 || dragging >= 0);
            drawReadout (g);
        }

        void mouseMove (const MouseEvent& e) override { const int h = nodePos().getDistanceFrom (e.position) < 10.0f ? 0 : -1; if (h != hot) { hot = h; updateReadout(); repaint(); } }
        void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            dragging = 0;
            startRatio = ratio != nullptr ? norm (ratio) : 0.0f;
            begin ({ threshold, ratio });
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            setValue (threshold, dbForX (e.position.x));
            if (ratio != nullptr) setNorm (ratio, startRatio - e.getDistanceFromDragStartY() / 120.0f);
            updateReadout();
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (dragging >= 0) end ({ threshold, ratio }); dragging = -1; }
        void mouseDoubleClick (const MouseEvent&) override
        {
            threshold->getEdit().getUndoManager().beginNewTransaction ("Reset");
            reset (threshold); reset (ratio);
        }

    private:
        static constexpr float lo = -60.0f, hi = 6.0f;
        float xFor (float db) const   { return area.getX() + (db - lo) / (hi - lo) * area.getWidth(); }
        float yFor (float db) const   { return area.getBottom() - (db - lo) / (hi - lo) * area.getHeight(); }
        float dbForX (float x) const  { return lo + (x - area.getX()) / area.getWidth() * (hi - lo); }
        juce::Point<float> nodePos() const
        {
            const float t = threshold->getCurrentBaseValue();
            return { xFor (t), jlimit (area.getY(), area.getBottom(), yFor (transfer (t))) };
        }
        void drawGR (Graphics& g, Rectangle<float> bar)
        {
            g.setColour (Theme::raised);
            g.fillRect (bar);
            const float v = jlimit (0.0f, 24.0f, gr());
            g.setColour (Theme::orange);
            g.fillRect (bar.withHeight (bar.getHeight() * v / 24.0f));
        }
        void updateReadout()
        {
            readoutPos = nodePos();
            readout = threshold->getCurrentValueAsStringWithLabel() + (ratio != nullptr ? "  " + ratio->getCurrentValueAsStringWithLabel() : String());
        }
        double signature() const override
        {
            double s = threshold->getCurrentBaseValue() + (ratio != nullptr ? ratio->getCurrentBaseValue() * 7.0 : 0.0);
            if (gr) s += gr() * 0.01;
            return s;
        }

        Param threshold, ratio;
        std::function<float (float)> transfer;
        Colour colour;
        std::function<float()> gr;
        Rectangle<float> area;
        float startRatio = 0.0f;
    };

    //==============================================================================
    /** Shows one of several displays depending on which sections of the card are visible
        (e.g. the filter and envelopes when the filter page is open). Each entry can stack
        several graphs vertically. */
    class PagedDisplay : public Component
    {
    public:
        struct Entry { StringArray sections; std::vector<std::unique_ptr<Component>> parts; };

        void add (StringArray sections, std::vector<std::unique_ptr<Component>> parts)
        {
            for (auto& p : parts) addChildComponent (*p);
            entries.push_back ({ sections, std::move (parts) });
            if (entries.size() == 1) showEntry (0);
        }

        /** Called by the card with the names of the sections now visible. */
        void showSections (const StringArray& visible)
        {
            for (int i = 0; i < (int) entries.size(); ++i)
                for (auto& s : entries[(size_t) i].sections)
                    if (visible.contains (s)) { showEntry (i); return; }
            showEntry (0);
        }

        void resized() override
        {
            if (! isPositiveAndBelow (current, (int) entries.size())) return;
            auto r = getLocalBounds();
            auto& parts = entries[(size_t) current].parts;
            const int gap = 4, h = (r.getHeight() - gap * ((int) parts.size() - 1)) / jmax (1, (int) parts.size());
            for (auto& p : parts) { p->setBounds (r.removeFromTop (h)); r.removeFromTop (gap); }
        }

        int getCurrentEntry() const { return current; }

    private:
        void showEntry (int i)
        {
            if (i == current) return;
            current = i;
            for (int e = 0; e < (int) entries.size(); ++e)
                for (auto& p : entries[(size_t) e].parts) p->setVisible (e == i);
            resized();
        }

        std::vector<Entry> entries;
        int current = -1;
    };
}
