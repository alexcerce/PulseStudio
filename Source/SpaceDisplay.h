#pragma once

#include "Graphs.h"
#include "DynamicsDisplay.h"

/** Full editors for Pulse Reverb, Pulse Delay and the Saturator. Each is one big display where the
    important controls are handles on the graph; only the leftovers sit in the bottom bar. */
namespace SpaceUI
{
    using Param = Graphs::Param;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    inline float xForFreq (Rectangle<float> a, float f) { return a.getX() + std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f) * a.getWidth(); }
    inline float freqForX (Rectangle<float> a, float x) { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth())); }

    /** A tidy step for axis ticks (0.1, 0.2, 0.25, 0.5, 1, 2 ... seconds). */
    inline double niceStep (double range, int ticks)
    {
        const double raw = range / jmax (1, ticks);
        const double mag = std::pow (10.0, std::floor (std::log10 (raw)));
        for (double m : { 1.0, 2.0, 2.5, 5.0, 10.0 })
            if (raw <= m * mag) return m * mag;
        return 10.0 * mag;
    }

    inline void freqGrid (Graphics& g, Rectangle<float> a, bool labels)
    {
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            const float x = xForFreq (a, f);
            g.setColour (Theme::ink.withAlpha (f == 100.0f || f == 1000.0f || f == 10000.0f ? 0.08f : 0.04f));
            g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            if (labels) Graphs::label (g, f >= 1000.0f ? String (f / 1000.0f, 0) + "k" : String ((int) f), { x - 14.0f, a.getBottom() + 2.0f, 28.0f, 11.0f }, Justification::centred, 0.55f);
        }
    }

    inline void areaTitle (Graphics& g, Rectangle<float> a, const String& t, const String& hint = {})
    {
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (t, a.withHeight (14.0f).translated (0, -18.0f), Justification::centredLeft);
        if (hint.isNotEmpty()) Graphs::label (g, hint, a.withHeight (14.0f).translated (0, -18.0f), Justification::centredRight, 0.55f);
    }

    //==============================================================================
    /** A display with draggable handles: each handle knows where it sits, what it changes when it
        is dragged (and by the wheel), and which parameters to reset on a double-click. */
    class HandleView : public Component, private Timer
    {
    public:
        struct Handle
        {
            String name;
            Colour colour;
            std::vector<Param> params;
            std::function<juce::Point<float>()> pos;
            std::function<void (juce::Point<float> now, juce::Point<float> delta)> drag;
            std::function<String()> readout;
            std::function<void (float)> wheel;
        };

        HandleView (pulse::PulsePlugin& p) : plugin (p), holder (&p) { startTimerHz (30); }
        int getNumHandles() const { return (int) handles.size(); }
        const Handle& handle (int i) const { return handles[(size_t) i]; }

        /** Drags a handle by (dx, dy) pixels as if with the mouse (also used by the self-test). */
        void dragHandle (int i, float dx, float dy)
        {
            auto& h = handles[(size_t) i];
            beginDrag (i);
            const auto start = h.pos();
            h.drag (start + juce::Point<float> (dx, dy), { dx, dy });
            endDrag();
        }

        void mouseMove (const MouseEvent& e) override
        {
            const int h = hit (e.position);
            if (h != hot) { hot = h; repaint(); }
            setMouseCursor (h >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
        }
        void mouseExit (const MouseEvent&) override { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            const int h = hit (e.position);
            if (h < 0) return;
            beginDrag (h);
            downPos = handles[(size_t) h].pos();
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            auto d = e.getOffsetFromDragStart().toFloat();
            if (e.mods.isShiftDown()) d *= 0.2f;   // fine
            handles[(size_t) dragging].drag (downPos + d, d);
            repaint();
        }
        void mouseUp (const MouseEvent&) override { endDrag(); }
        void mouseDoubleClick (const MouseEvent& e) override
        {
            const int h = hit (e.position);
            if (h < 0) return;
            plugin.edit.getUndoManager().beginNewTransaction ("Reset " + handles[(size_t) h].name);
            for (auto& p : handles[(size_t) h].params) Graphs::reset (p);
        }
        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            const int h = hit (e.position) >= 0 ? hit (e.position) : hot;
            if (h >= 0 && handles[(size_t) h].wheel) handles[(size_t) h].wheel (w.deltaY);
        }

    protected:
        pulse::PulsePlugin& plugin;
        te::Plugin::Ptr holder;
        std::vector<Handle> handles;
        int hot = -1, dragging = -1;
        juce::Point<float> downPos;

        Param P (int i) const { return plugin.param (i); }

        int hit (juce::Point<float> p) const
        {
            int best = -1;
            float bestD = 12.0f;
            for (size_t i = 0; i < handles.size(); ++i)
            {
                const float d = handles[i].pos().getDistanceFrom (p);
                if (d < bestD) { bestD = d; best = (int) i; }
            }
            return best;
        }

        void beginDrag (int h)
        {
            dragging = h;
            plugin.edit.getUndoManager().beginNewTransaction (handles[(size_t) h].name);
            for (auto& p : handles[(size_t) h].params) if (p != nullptr) p->parameterChangeGestureBegin();
        }
        void endDrag()
        {
            if (dragging >= 0)
                for (auto& p : handles[(size_t) dragging].params) if (p != nullptr) p->parameterChangeGestureEnd();
            dragging = -1;
        }

        void drawHandles (Graphics& g)
        {
            for (size_t i = 0; i < handles.size(); ++i)
            {
                auto& h = handles[i];
                const auto c = h.pos();
                const bool on = (int) i == hot || (int) i == dragging;
                Graphs::node (g, c, h.colour, on);
                if (on && h.readout)
                {
                    const String t = h.name + "  " + h.readout();
                    const float w = GlyphArrangement::getStringWidth (FontOptions (11.0f, Font::bold), t) + 16.0f;
                    auto box = Rectangle<float> (c.x - w * 0.5f, c.y - 32.0f, w, 20.0f).constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
                    g.setColour (Colour (0xee0b0d11));
                    g.fillRoundedRectangle (box, 4.0f);
                    g.setColour (h.colour.withAlpha (0.8f));
                    g.drawRoundedRectangle (box, 4.0f, 1.0f);
                    g.setColour (Colours::white);
                    g.setFont (FontOptions (11.0f, Font::bold));
                    g.drawText (t, box, Justification::centred);
                }
            }
        }

        /** A strip of value readouts along the bottom of a display: "NAME value" pairs. */
        void drawReadouts (Graphics& g, Rectangle<float> r, std::initializer_list<std::pair<String, String>> items)
        {
            const float w = r.getWidth() / (float) jmax<size_t> (1, items.size());
            for (auto& [name, value] : items)
            {
                auto cell = r.removeFromLeft (w);
                Graphs::label (g, name, cell.removeFromTop (12.0f), Justification::centredLeft, 0.6f);
                g.setColour (Theme::text);
                g.setFont (FontOptions (14.0f, Font::bold));
                g.drawText (value, cell, Justification::centredLeft);
            }
        }

    private:
        void timerCallback() override { repaint(); }
    };

    //==============================================================================
    /** Reverb: the decay over time (predelay, early reflections, the tail down to -60 dB) and the
        tail's tone (low cut and damping) as curves with handles. */
    class ReverbView : public HandleView
    {
    public:
        using R = pulse::Reverb;
        explicit ReverbView (R& r) : HandleView (r), rev (r)
        {
            handles.push_back ({ "Predelay", Theme::ink, { P (R::pPredelay) },
                                 [this] { return juce::Point<float> (xT (predelay()), yDb (0.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pPredelay), (float) (tFor (p.x) * 1000.0)); },
                                 [this] { return rev.param (R::pPredelay)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Size / diffusion", Theme::accent, { P (R::pSize), P (R::pDiffusion) },
                                 [this] { return juce::Point<float> (xT (predelay() + earlyLen()), yDb (-6.0f - (1.0f - rev.get (R::pDiffusion) / 0.9f) * 24.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     const double er = jmax (0.0, tFor (p.x) - predelay());
                                     Graphs::setValue (P (R::pSize), (float) ((er / 0.08 - 0.35) / 1.65));
                                     Graphs::setValue (P (R::pDiffusion), (1.0f - jlimit (0.0f, 1.0f, (-dbFor (p.y) - 6.0f) / 24.0f)) * 0.9f);
                                 },
                                 [this] { return rev.param (R::pSize)->getCurrentValueAsString() + " / " + rev.param (R::pDiffusion)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pSize), rev.get (R::pSize) + d * 0.1f); } });
            handles.push_back ({ "Decay", Theme::purple, { P (R::pDecay) },
                                 [this] { return juce::Point<float> (xT (predelay() + (double) rev.get (R::pDecay)), yDb (-60.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pDecay), (float) jmax (0.2, tFor (p.x) - predelay())); },
                                 [this] { return rev.param (R::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pDecay), rev.get (R::pDecay) * (d > 0 ? 1.1f : 0.9f)); } });
            handles.push_back ({ "Low cut", Theme::orange, { P (R::pLowCut) },
                                 [this] { return juce::Point<float> (xForFreq (toneArea, rev.get (R::pLowCut)), yTone (toneDb (rev.get (R::pLowCut)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pLowCut), freqForX (toneArea, p.x)); },
                                 [this] { return rev.param (R::pLowCut)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Damping", Theme::yellow, { P (R::pDamping) },
                                 [this] { return juce::Point<float> (xForFreq (toneArea, rev.get (R::pDamping)), yTone (toneDb (rev.get (R::pDamping)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pDamping), freqForX (toneArea, p.x)); },
                                 [this] { return rev.param (R::pDamping)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            toneArea = r.removeFromRight (jmin (360.0f, r.getWidth() * 0.34f)).withTrimmedBottom (14.0f);
            r.removeFromRight (40.0f);
            timeArea = r.withTrimmedLeft (30.0f).withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            if (dragging < 0) span = jmax (1.0, (predelay() + rev.get (R::pDecay)) * 1.2);
            paintTime (g);
            paintTone (g);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "PREDELAY", rev.param (R::pPredelay)->getCurrentValueAsString() },
                                            { "DECAY", rev.get (R::pFreeze) > 0.5f ? String ("Frozen") : rev.param (R::pDecay)->getCurrentValueAsString() },
                                            { "SIZE", rev.param (R::pSize)->getCurrentValueAsString() },
                                            { "DIFFUSION", rev.param (R::pDiffusion)->getCurrentValueAsString() },
                                            { "LOW CUT", rev.param (R::pLowCut)->getCurrentValueAsString() },
                                            { "DAMPING", rev.param (R::pDamping)->getCurrentValueAsString() } });
        }

        double predelay() const { return rev.get (R::pPredelay) * 0.001; }
        double earlyLen() const { return 0.08 * (0.35 + 1.65 * rev.get (R::pSize)); }

    private:
        R& rev;
        Rectangle<float> timeArea, toneArea, readoutArea;
        double span = 3.0;

        float xT (double t) const     { return timeArea.getX() + (float) (t / span) * timeArea.getWidth(); }
        double tFor (float x) const   { return (x - timeArea.getX()) / timeArea.getWidth() * span; }
        float yDb (float db) const    { return timeArea.getY() + jlimit (0.0f, 1.0f, -db / 66.0f) * timeArea.getHeight(); }
        float dbFor (float y) const   { return -(y - timeArea.getY()) / timeArea.getHeight() * 66.0f; }
        float yTone (float db) const  { return toneArea.getY() + 10.0f + jlimit (0.0f, 1.0f, -db / 30.0f) * (toneArea.getHeight() - 10.0f); }

        /** The tail's level at a frequency: the low cut (12 dB/oct) and the damping (one pole, which
            repeats on every trip around the loop, so it bites harder than one filter would). */
        float toneDb (float f) const
        {
            const double lc = rev.get (R::pLowCut), damp = rev.get (R::pDamping);
            const double hp = std::pow (f / lc, 2.0) / std::sqrt (1.0 + std::pow (f / lc, 4.0));
            const double lp = 1.0 / std::sqrt (1.0 + std::pow (f / damp, 2.0));
            return (float) Decibels::gainToDecibels (hp * lp * lp, -60.0);
        }

        void paintTime (Graphics& g)
        {
            auto a = timeArea;
            areaTitle (g, a, "DECAY", "drag the points   |   wheel: decay / size   |   double-click resets");
            for (float db = 0; db >= -60; db -= 12)
            {
                g.setColour (Theme::ink.withAlpha (db == 0 ? 0.1f : 0.04f));
                g.drawHorizontalLine ((int) yDb (db), a.getX(), a.getRight());
                Graphs::label (g, String ((int) db), { a.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            const double step = niceStep (span, 6);
            for (double t = 0; t <= span + 1e-6; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.04f));
                g.drawVerticalLine ((int) xT (t), a.getY(), a.getBottom());
                Graphs::label (g, t < 1.0 ? String ((int) std::round (t * 1000)) + " ms" : String (t, step < 1.0 ? 1 : 0) + " s",
                               { xT (t) - 24.0f, a.getBottom() + 2.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            const bool frozen = rev.get (R::pFreeze) > 0.5f;
            const double pd = predelay(), er = earlyLen(), decay = rev.get (R::pDecay);
            // the dry hit and the predelay gap
            g.setColour (Theme::ink.withAlpha (0.8f));
            g.fillRect (Rectangle<float> (xT (0) - 1.0f, yDb (0), 2.5f, a.getBottom() - yDb (0)));
            if (pd > 0.001)
            {
                g.setColour (Theme::ink.withAlpha (0.04f));
                g.fillRect (Rectangle<float> (xT (0) + 2.0f, a.getY(), xT (pd) - xT (0) - 2.0f, a.getHeight()));
                Graphs::label (g, "predelay", { xT (0) + 4.0f, a.getY() + 4.0f, jmax (40.0f, xT (pd) - xT (0)), 12.0f }, Justification::centredLeft, 0.5f);
            }
            // the tail: straight in dB, from the end of the early part down to -60 at the decay time
            const float startDb = -3.0f - (1.0f - rev.get (R::pDiffusion) / 0.9f) * 6.0f;
            auto tailDb = [&] (double t)
            {
                if (frozen) return startDb;
                return (float) (startDb + (-60.0 - startDb) * jlimit (0.0, 1.5, (t - pd) / jmax (0.05, decay)));
            };
            const float wob = rev.get (R::pMod);
            Path tail;
            tail.startNewSubPath (xT (pd), a.getBottom());
            for (float x = xT (pd); x <= a.getRight(); x += 2.0f)
            {
                const double t = tFor (x);
                const float ripple = wob * 1.5f * std::sin ((float) t * 40.0f) * std::sin ((float) t * 7.0f);
                tail.lineTo (x, yDb (jmax (-66.0f, tailDb (t) + ripple)));
            }
            tail.lineTo (a.getRight(), a.getBottom());
            tail.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::purple.withAlpha (0.45f), 0.0f, a.getY(), Theme::purple.withAlpha (0.04f), 0.0f, a.getBottom(), false));
            g.fillPath (tail);
            Path line;
            for (float x = xT (pd); x <= a.getRight(); x += 2.0f)
            {
                const float y = yDb (jmax (-66.0f, tailDb (tFor (x))));
                if (x == xT (pd)) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            Graphs::glowStroke (g, line, Theme::purple, 1.8f);
            // early reflections: spikes, denser and smoother with more diffusion
            Random rnd (1234);
            const int count = 8 + (int) (rev.get (R::pDiffusion) * 40.0f);
            for (int i = 0; i < count; ++i)
            {
                const double t = pd + er * std::pow (rnd.nextDouble(), 0.8);
                const float db = -2.0f - rnd.nextFloat() * (14.0f - rev.get (R::pDiffusion) * 8.0f) - (float) ((t - pd) / er) * 4.0f;
                g.setColour (Theme::accent.withAlpha (0.35f));
                g.drawLine (xT (t), yDb (-66.0f), xT (t), yDb (db), 1.0f);
            }
            if (frozen)
            {
                g.setColour (Theme::accent);
                g.setFont (FontOptions (13.0f, Font::bold));
                g.drawText ("FROZEN: the tail holds forever", a.removeFromTop (40.0f), Justification::centred);
            }
            // live: the reverb's own output
            const float live = jlimit (-66.0f, 0.0f, rev.wetDb.load());
            auto m = Rectangle<float> (timeArea.getRight() + 14.0f, yDb (0), 8.0f, a.getBottom() - yDb (0));
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (m, 2.0f);
            g.setColour (Theme::purple);
            g.fillRoundedRectangle (m.withTop (yDb (live)), 2.0f);
        }

        void paintTone (Graphics& g)
        {
            auto a = toneArea;
            areaTitle (g, a, "TAIL TONE");
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
            freqGrid (g, a, true);
            for (float db : { 0.0f, -12.0f, -24.0f })
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawHorizontalLine ((int) yTone (db), a.getX(), a.getRight());
            }
            Path p, fill;
            for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
            {
                const float y = yTone (toneDb (freqForX (a, x)));
                if (x == a.getX()) { p.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); }
                else p.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (a.getRight(), a.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.3f), 0.0f, a.getY(), Theme::accent.withAlpha (0.02f), 0.0f, a.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, p, Theme::accent, 1.8f);
        }
    };

    //==============================================================================
    /** Delay: the echoes on a time line (left above, right below the centre in ping-pong), the
        first echo's handle sets the time (snapping to notes when synced), the second the feedback;
        the feedback filter has its own curve. */
    class DelayView : public HandleView
    {
    public:
        using D = pulse::Delay;
        explicit DelayView (D& d) : HandleView (d), del (d)
        {
            handles.push_back ({ "Time", Theme::accent, { P (D::pTime), P (D::pNote) },
                                 [this] { return juce::Point<float> (xT (delayS()), yAmp (0.0f, true)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { setTime (jmax (0.001, tFor (p.x))); },
                                 [this] { return timeText(); },
                                 [this] (float d)
                                 {
                                     if (del.getInt (D::pSync) > 0) Graphs::setValue (P (D::pNote), (float) (del.getInt (D::pNote) + (d > 0 ? 1 : -1)));
                                     else Graphs::setValue (P (D::pTime), del.get (D::pTime) * (d > 0 ? 1.06f : 0.94f));
                                 } });
            handles.push_back ({ "Feedback", Theme::orange, { P (D::pFeedback) },
                                 [this] { return juce::Point<float> (xT (delayS() * 2.0), yAmp (ampDb (1), secondUp())); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     const float h = secondUp() ? (centreY() - p.y) : (p.y - centreY());
                                     const float db = jlimit (0.0f, 1.0f, h / halfH()) * 48.0f - 48.0f;
                                     Graphs::setValue (P (D::pFeedback), Decibels::decibelsToGain (db, -48.0f));
                                 },
                                 [this] { return del.param (D::pFeedback)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (D::pFeedback), del.get (D::pFeedback) + d * 0.05f); } });
            handles.push_back ({ "Low cut", Theme::orange, { P (D::pLowCut) },
                                 [this] { return juce::Point<float> (xForFreq (toneArea, del.get (D::pLowCut)), yTone (toneDb (del.get (D::pLowCut)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (D::pLowCut), freqForX (toneArea, p.x)); },
                                 [this] { return del.param (D::pLowCut)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "High cut", Theme::yellow, { P (D::pHighCut) },
                                 [this] { return juce::Point<float> (xForFreq (toneArea, del.get (D::pHighCut)), yTone (toneDb (del.get (D::pHighCut)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (D::pHighCut), freqForX (toneArea, p.x)); },
                                 [this] { return del.param (D::pHighCut)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            toneArea = r.removeFromRight (jmin (340.0f, r.getWidth() * 0.32f)).withTrimmedBottom (14.0f);
            r.removeFromRight (30.0f);
            timeArea = r.withTrimmedLeft (8.0f).withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            if (dragging < 0) span = jmax (1.0, delayS() * 8.5);
            paintTime (g);
            paintTone (g);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "TIME", timeText() }, { "FEEDBACK", del.param (D::pFeedback)->getCurrentValueAsString() },
                                            { "MODE", del.getInt (D::pPingPong) > 0 ? String ("Ping-pong") : String ("Stereo") },
                                            { "LOW CUT", del.param (D::pLowCut)->getCurrentValueAsString() },
                                            { "HIGH CUT", del.param (D::pHighCut)->getCurrentValueAsString() },
                                            { "DRY/WET", del.param (D::pMix)->getCurrentValueAsString() } });
        }

        double delayS() const { return del.delaySeconds (del.lastBpm.load()); }

        /** Sets the time from seconds: free time in ms, or the nearest note value when synced. */
        void setTime (double seconds)
        {
            if (del.getInt (D::pSync) > 0)
            {
                const double beats = seconds * del.lastBpm.load() / 60.0;
                int best = 0;
                for (int i = 0; i < D::noteNames.size(); ++i)
                    if (std::abs (std::log (D::noteBeats (i) / beats)) < std::abs (std::log (D::noteBeats (best) / beats))) best = i;
                Graphs::setValue (P (D::pNote), (float) best);
            }
            else
                Graphs::setValue (P (D::pTime), (float) (seconds * 1000.0));
        }

    private:
        D& del;
        Rectangle<float> timeArea, toneArea, readoutArea;
        double span = 3.0;

        String timeText() const
        {
            return del.getInt (D::pSync) > 0 ? del.param (D::pNote)->getCurrentValueAsString() + "  (" + String (roundToInt (delayS() * 1000.0)) + " ms)"
                                             : del.param (D::pTime)->getCurrentValueAsString();
        }
        float xT (double t) const   { return timeArea.getX() + (float) (t / span) * timeArea.getWidth(); }
        double tFor (float x) const { return (x - timeArea.getX()) / timeArea.getWidth() * span; }
        float centreY() const       { return timeArea.getCentreY(); }
        float halfH() const         { return timeArea.getHeight() * 0.5f - 6.0f; }
        float yAmp (float db, bool up) const
        {
            const float h = jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) * halfH();
            return up ? centreY() - h : centreY() + h;
        }
        bool ping() const { return del.getInt (D::pPingPong) > 0; }
        bool secondUp() const { return ! ping(); }   // in ping-pong the second echo is on the right (below)
        float ampDb (int k) const { return k == 0 ? 0.0f : Decibels::gainToDecibels (std::pow (jmax (0.0001f, del.get (D::pFeedback)), (float) k), -60.0f); }
        float yTone (float db) const { return toneArea.getY() + 10.0f + jlimit (0.0f, 1.0f, -db / 30.0f) * (toneArea.getHeight() - 10.0f); }

        /** What one trip through the feedback filters does (12 dB/oct low and high cut). */
        float toneDb (float f) const
        {
            double gain = 1.0;
            const double lc = del.get (D::pLowCut), hc = del.get (D::pHighCut);
            if (lc > 20.5) gain *= std::pow (f / lc, 2.0) / std::sqrt (1.0 + std::pow (f / lc, 4.0));
            if (hc < 19500.0) gain *= 1.0 / std::sqrt (1.0 + std::pow (f / hc, 4.0));
            return (float) Decibels::gainToDecibels (gain, -60.0);
        }

        void paintTime (Graphics& g)
        {
            auto a = timeArea;
            areaTitle (g, a, "ECHOES", "drag the first echo: time   |   the second: feedback   |   wheel steps");
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (a.expanded (4.0f), 5.0f);
            // the grid: beats when synced, otherwise tidy times
            const double bpm = del.lastBpm.load();
            const bool sync = del.getInt (D::pSync) > 0;
            const double step = sync ? 60.0 / bpm * (span / (60.0 / bpm) > 24.0 ? 4.0 : 1.0) : niceStep (span, 8);
            int idx = 0;
            for (double t = 0; t <= span + 1e-6; t += step, ++idx)
            {
                const bool bar = sync && (idx % 4 == 0 || step > 60.0 / bpm * 2.0);
                g.setColour (Theme::ink.withAlpha (bar ? 0.09f : 0.04f));
                g.drawVerticalLine ((int) xT (t), a.getY(), a.getBottom());
                const String txt = sync ? (step > 60.0 / bpm * 2.0 ? String (idx + 1) : String (idx / 4 + 1) + "." + String (idx % 4 + 1))
                                        : (t < 1.0 ? String (roundToInt (t * 1000.0)) + " ms" : String (t, step < 1.0 ? 1 : 0) + " s");
                if (! sync || bar || span < 6.0 * 60.0 / bpm) Graphs::label (g, txt, { xT (t) - 22.0f, a.getBottom() + 2.0f, 44.0f, 11.0f }, Justification::centred, 0.55f);
            }
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawHorizontalLine ((int) centreY(), a.getX(), a.getRight());
            Graphs::label (g, ping() ? "L" : "", { a.getX() + 4.0f, a.getY() + 2.0f, 20.0f, 12.0f }, Justification::centredLeft, 0.6f);
            Graphs::label (g, ping() ? "R" : "", { a.getX() + 4.0f, a.getBottom() - 14.0f, 20.0f, 12.0f }, Justification::centredLeft, 0.6f);
            // the dry hit
            g.setColour (Theme::ink.withAlpha (0.8f));
            g.fillRect (Rectangle<float> (xT (0) - 1.0f, yAmp (0, true), 3.0f, yAmp (0, false) - yAmp (0, true)));
            // echoes
            const double d = delayS();
            const float wob = del.get (D::pMod), drive = del.get (D::pDrive);
            const Colour col = Theme::accent.interpolatedWith (Theme::orange, drive);
            const float loss = toneDb (1000.0f);   // broadband loss per trip, roughly
            for (int k = 0; k < 64; ++k)
            {
                const double t = d * (k + 1);
                if (t > span) break;
                const float db = ampDb (k) + loss * (float) k;
                if (db < -48.0f) break;
                const float x = xT (t);
                const float w = 4.0f + wob * 6.0f * (float) std::sqrt ((double) k + 1.0);
                auto bar = [&] (bool up, float alpha)
                {
                    const float y = yAmp (db, up);
                    auto r = up ? Rectangle<float> (x - w * 0.5f, y, w, centreY() - y) : Rectangle<float> (x - w * 0.5f, centreY(), w, y - centreY());
                    g.setGradientFill (ColourGradient (col.withAlpha (alpha), x, y, col.withAlpha (alpha * 0.25f), x, centreY(), false));
                    g.fillRoundedRectangle (r, 1.5f);
                    g.setColour (col);
                    g.fillRect (Rectangle<float> (x - w * 0.5f, y - 1.0f, w, 2.0f));
                };
                if (ping()) bar (k % 2 == 0, 0.8f);   // left first, then crossing over
                else { bar (true, 0.8f); bar (false, 0.5f); }
            }
        }

        void paintTone (Graphics& g)
        {
            auto a = toneArea;
            areaTitle (g, a, "FEEDBACK TONE");
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
            freqGrid (g, a, true);
            Path p, fill;
            for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
            {
                const float y = yTone (toneDb (freqForX (a, x)));
                if (x == a.getX()) { p.startNewSubPath (x, y); fill.startNewSubPath (x, a.getBottom()); }
                else p.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (a.getRight(), a.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.3f), 0.0f, a.getY(), Theme::accent.withAlpha (0.02f), 0.0f, a.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, p, Theme::accent, 1.8f);
        }
    };

    //==============================================================================
    /** Saturator: the transfer curve (drag its handle up for drive, sideways for bias), what it does
        to a sine, and the tone tilt as a curve with its own handle. */
    class SaturatorView : public HandleView
    {
    public:
        using S = pulse::Saturator;
        explicit SaturatorView (S& s) : HandleView (s), sat (s)
        {
            handles.push_back ({ "Drive / bias", Theme::orange, { P (S::pDrive), P (S::pBias) },
                                 [this] { return juce::Point<float> (xC (sat.get (S::pBias) * 0.5f), yC (sat.get (S::pDrive) / 36.0f * 0.9f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (S::pBias), inC (p.x) * 2.0f);
                                     Graphs::setValue (P (S::pDrive), outC (p.y) / 0.9f * 36.0f);
                                 },
                                 [this] { return sat.param (S::pDrive)->getCurrentValueAsString() + ", bias " + sat.param (S::pBias)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pDrive), sat.get (S::pDrive) + d * 3.0f); } });
            handles.push_back ({ "Tone", Theme::yellow, { P (S::pTone) },
                                 [this] { return juce::Point<float> (xForFreq (toneArea, 6000.0f), yTone (toneDb (6000.0f))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     // find the tone that puts 6 kHz at this height
                                     const float want = jlimit (-12.0f, 12.0f, 12.0f - (p.y - toneArea.getY()) / toneArea.getHeight() * 24.0f);
                                     float best = 0.0f, bestErr = 1e9f;
                                     for (float t = -1.0f; t <= 1.0f; t += 0.01f)
                                         if (const float e = std::abs (toneDbFor (t, 6000.0f) - want); e < bestErr) { bestErr = e; best = t; }
                                     Graphs::setValue (P (S::pTone), best);
                                 },
                                 [this] { return sat.param (S::pTone)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pTone), sat.get (S::pTone) + d * 0.1f); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            const float side = jmin (r.getHeight() - 14.0f, r.getWidth() * 0.4f);
            curveArea = r.removeFromLeft (side + 30.0f).withTrimmedLeft (30.0f).withHeight (side);
            r.removeFromLeft (30.0f);
            auto right = r;
            waveArea = right.removeFromTop (right.getHeight() * 0.55f).withTrimmedBottom (30.0f);
            toneArea = right.withTrimmedBottom (14.0f).withTrimmedTop (8.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            paintCurve (g);
            paintWave (g);
            paintTone (g);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "TYPE", sat.param (S::pType)->getCurrentValueAsString() }, { "DRIVE", sat.param (S::pDrive)->getCurrentValueAsString() },
                                            { "BIAS", sat.param (S::pBias)->getCurrentValueAsString() }, { "TONE", sat.param (S::pTone)->getCurrentValueAsString() },
                                            { "OUTPUT", sat.param (S::pOutput)->getCurrentValueAsString() }, { "DRY/WET", sat.param (S::pMix)->getCurrentValueAsString() } });
        }

    private:
        S& sat;
        Rectangle<float> curveArea, waveArea, toneArea, readoutArea;

        // the curve area spans -1..1 both ways
        float xC (float in) const  { return curveArea.getCentreX() + jlimit (-1.0f, 1.0f, in) * curveArea.getWidth() * 0.5f; }
        float yC (float out) const { return curveArea.getCentreY() - jlimit (-1.1f, 1.1f, out) * curveArea.getHeight() * 0.5f; }
        float inC (float x) const  { return (x - curveArea.getCentreX()) / (curveArea.getWidth() * 0.5f); }
        float outC (float y) const { return (curveArea.getCentreY() - y) / (curveArea.getHeight() * 0.5f); }

        float process (float x) const
        {
            const float g = Decibels::decibelsToGain (sat.get (S::pDrive));
            const int type = sat.getInt (S::pType);
            const float bias = sat.get (S::pBias);
            const float y = S::shape (type, x * g, bias) - S::shape (type, 0.0f, bias);
            return jlimit (-1.2f, 1.2f, y);
        }
        static float toneDbFor (float tone, float f)
        {
            const float lowG = 1.0f - jmax (0.0f, tone) * 0.7f, highG = 1.0f + jmin (0.0f, tone) * 0.9f + jmax (0.0f, tone) * 0.8f;
            const std::complex<float> jw (0.0f, f / 1200.0f);
            return Decibels::gainToDecibels (std::abs ((lowG + highG * jw) / (1.0f + jw)), -60.0f);
        }
        float toneDb (float f) const { return toneDbFor (sat.get (S::pTone), f); }
        float yTone (float db) const { return toneArea.getCentreY() - jlimit (-12.0f, 12.0f, db) / 12.0f * toneArea.getHeight() * 0.5f; }

        void paintCurve (Graphics& g)
        {
            auto a = curveArea;
            areaTitle (g, a, "SHAPE", "drag: up drive, sideways bias");
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
            for (float v : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f })
            {
                g.setColour (Theme::ink.withAlpha (v == 0.0f ? 0.1f : 0.04f));
                g.drawVerticalLine ((int) xC (v), a.getY(), a.getBottom());
                g.drawHorizontalLine ((int) yC (v), a.getX(), a.getRight());
            }
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawLine (xC (-1), yC (-1), xC (1), yC (1), 1.0f);
            Path p;
            for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
            {
                const float y = yC (process (inC (x)));
                if (x == a.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Graphs::glowStroke (g, p, Theme::orange, 2.0f);
            // live: the input level as a marker on the curve
            const float in = jlimit (0.0f, 1.0f, sat.inPeak.load());
            if (in > 0.001f)
            {
                g.setColour (Theme::ink.withAlpha (0.6f));
                g.fillEllipse (xC (in) - 3.0f, yC (process (in)) - 3.0f, 6.0f, 6.0f);
                g.fillEllipse (xC (-in) - 3.0f, yC (process (-in)) - 3.0f, 6.0f, 6.0f);
            }
        }

        void paintWave (Graphics& g)
        {
            auto a = waveArea;
            areaTitle (g, a, "WHAT IT DOES TO A SINE");
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
            Path in, out;
            const float mix = sat.get (S::pMix);
            for (float x = a.getX(); x <= a.getRight(); x += 1.0f)
            {
                const float ph = (x - a.getX()) / a.getWidth() * MathConstants<float>::twoPi * 2.0f;
                const float s = 0.8f * std::sin (ph);
                const float y = s + mix * (process (s) - s);
                const float yi = a.getCentreY() - s * a.getHeight() * 0.45f, yo = a.getCentreY() - jlimit (-1.1f, 1.1f, y) * a.getHeight() * 0.45f;
                if (x == a.getX()) { in.startNewSubPath (x, yi); out.startNewSubPath (x, yo); }
                else { in.lineTo (x, yi); out.lineTo (x, yo); }
            }
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.strokePath (in, PathStrokeType (1.2f));
            Graphs::glowStroke (g, out, Theme::orange, 1.8f);
        }

        void paintTone (Graphics& g)
        {
            auto a = toneArea;
            areaTitle (g, a, "TONE", "drag the point up for brighter");
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
            freqGrid (g, a, true);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) yTone (0), a.getX(), a.getRight());
            Path p;
            for (float x = a.getX(); x <= a.getRight(); x += 2.0f)
            {
                const float y = yTone (toneDb (freqForX (a, x)));
                if (x == a.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Graphs::glowStroke (g, p, Theme::yellow, 1.8f);
        }
    };

    //==============================================================================
    class ReverbPanel : public DynamicsUI::Panel
    {
    public:
        using R = pulse::Reverb;
        ReverbPanel (EditViewState& evs, R& r)
        {
            display = std::make_unique<ReverbView> (r);
            addAndMakeVisible (*display);
            auto* ch = strips.add (new Strip ("CHARACTER"));
            ch->add (knob (evs, *r.param (R::pMod), "Chorus"), 64);
            ch->add (knob (evs, *r.param (R::pWidth), "Width"), 64);
            auto* fr = strips.add (new Strip ("TAIL"));
            fr->add (new ParamToggle (*r.param (R::pFreeze), "Freeze", Theme::accent), 80);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *r.param (R::pMix), "Dry/Wet"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class DelayPanel : public DynamicsUI::Panel
    {
    public:
        using D = pulse::Delay;
        DelayPanel (EditViewState& evs, D& d)
        {
            display = std::make_unique<DelayView> (d);
            addAndMakeVisible (*display);
            auto* tm = strips.add (new Strip ("TIME"));
            tm->add (new ParamChoice (*d.param (D::pSync), Theme::accent, false), 150);
            tm->add (new ParamCombo (*d.param (D::pNote), false), 110);
            auto* md = strips.add (new Strip ("STEREO"));
            md->add (new ParamToggle (*d.param (D::pPingPong), "Ping-pong", Theme::accent), 92);
            auto* col = strips.add (new Strip ("COLOUR"));
            col->add (knob (evs, *d.param (D::pMod), "Wobble"), 64);
            col->add (knob (evs, *d.param (D::pDrive), "Drive"), 64);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *d.param (D::pMix), "Dry/Wet"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class SaturatorPanel : public DynamicsUI::Panel
    {
    public:
        using S = pulse::Saturator;
        SaturatorPanel (EditViewState& evs, S& s)
        {
            display = std::make_unique<SaturatorView> (s);
            addAndMakeVisible (*display);
            auto* ty = strips.add (new Strip ("TYPE"));
            ty->add (new ParamChoice (*s.param (S::pType), Theme::orange, false), 560);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *s.param (S::pOutput), "Output"), 64);
            out->add (knob (evs, *s.param (S::pMix), "Dry/Wet"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
