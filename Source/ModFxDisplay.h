#pragma once

#include "ModFx.h"
#include "SpaceDisplay.h"

/** Windows for Pulse Chorus, the Phaser-Flanger and Auto Pan: what the movement does, drawn live, with the
    main settings as points to drag. */
namespace ModUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Changes a rate by dragging sideways: the synced note value in steps, or the free rate smoothly. */
    inline void dragRate (Graphs::Param rate, Graphs::Param sync, float startRate, int startSync, float dx)
    {
        if (sync != nullptr && roundToInt (sync->getCurrentValue()) > 0)
            Graphs::setValue (sync, (float) jlimit (1, pulse::ModEffect::syncNames.size() - 1, startSync - roundToInt (dx / 40.0f)));
        else
            Graphs::setValue (rate, startRate * std::pow (2.0f, -dx / 80.0f));
    }

    //==============================================================================
    /** Chorus: how late each moving copy is, over time. Left side solid, right side fainter. */
    class ChorusView : public HandleView
    {
    public:
        using C = pulse::Chorus;
        explicit ChorusView (C& c) : HandleView (c), ch (c)
        {
            handles.push_back ({ "Delay", Theme::ink, { P (C::pDelay) },
                                 [this] { return juce::Point<float> (area.getX() + 14.0f, yMs (ch.get (C::pDelay))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (C::pDelay), msAt (p.y)); },
                                 [this] { return ch.param (C::pDelay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (C::pDelay), ch.get (C::pDelay) + (d > 0 ? 0.5f : -0.5f)); } });
            handles.push_back ({ "Amount / rate", Theme::accent, { P (C::pAmount), P (C::pRate) },
                                 [this] { return juce::Point<float> (xT (0.25 / ch.get (C::pRate)), yMs (ch.get (C::pDelay) + ch.swingMs())); },
                                 [this] (juce::Point<float> p, juce::Point<float> d)
                                 {
                                     const float most = jmin (ch.get (C::pDelay) * 0.85f, 6.0f);
                                     Graphs::setValue (P (C::pAmount), (msAt (p.y) - ch.get (C::pDelay)) / jmax (0.1f, most));
                                     dragRate (P (C::pRate), nullptr, startRate, 0, d.x);
                                 },
                                 [this] { return ch.param (C::pAmount)->getCurrentValueAsString() + "  " + ch.param (C::pRate)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (C::pRate), ch.get (C::pRate) * (d > 0 ? 1.1f : 0.9f)); } });
        }

        void mouseDown (const MouseEvent& e) override { startRate = ch.get (C::pRate); HandleView::mouseDown (e); }
        void dragAmountRate (float dx, float dy) { startRate = ch.get (C::pRate); dragHandle (1, dx, dy); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (36.0f).withTrimmedBottom (18.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "HOW LATE EACH COPY IS", "left solid, right faint   drag: delay, amount (up / down), rate (sideways)");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            const float rate = ch.get (C::pRate), delay = ch.get (C::pDelay), swing = ch.swingMs(), width = ch.get (C::pWidth);
            span = jlimit (0.4, 8.0, 2.0 / rate);
            top = jmax (10.0f, (delay + jmin (delay * 0.85f, 6.0f)) * 1.15f);
            for (float ms = 0.0f; ms <= top; ms += top > 20.0f ? 10.0f : 5.0f)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawHorizontalLine ((int) yMs (ms), area.getX(), area.getRight());
                Graphs::label (g, String ((int) ms) + " ms", { area.getX() - 36.0f, yMs (ms) - 6.0f, 32.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            const double step = SpaceUI::niceStep (span, 8);
            for (double t = 0.0; t <= span; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xT (t), area.getY(), area.getBottom());
                Graphs::label (g, String (t, 2) + " s", { xT (t) - 24.0f, area.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            g.setColour (Theme::ink.withAlpha (0.2f));
            const float dash[] = { 4.0f, 4.0f };
            g.drawDashedLine (Line<float> (area.getX(), yMs (delay), area.getRight(), yMs (delay)), dash, 2, 1.0f);
            const Colour cols[] = { Theme::accent, Theme::green, Theme::orange };
            const double speed[] = { 1.0, 1.11, 0.93 };
            const double now = ch.lfoPhase.load();
            for (int side = 1; side >= 0; --side)
                for (int v = ch.voices(); --v >= 0;)
                {
                    Path p;
                    for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
                    {
                        const double t = (x - area.getX()) / area.getWidth() * span;
                        const float y = yMs (delay + swing * (float) std::sin ((t * rate * speed[v] + v / 3.0 + (side == 1 ? 0.25 * width : 0.0)) * MathConstants<double>::twoPi));
                        if (x == area.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    }
                    if (side == 0) Graphs::glowStroke (g, p, cols[v], 1.8f);
                    else { g.setColour (cols[v].withAlpha (0.35f)); g.strokePath (p, PathStrokeType (1.2f)); }
                }
            // where the first copy is right now
            g.setColour (Theme::ink);
            g.fillEllipse (area.getRight() + 2.0f, yMs (delay + swing * (float) std::sin (now * MathConstants<double>::twoPi)) - 3.5f, 7.0f, 7.0f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "MODE", ch.param (C::pMode)->getCurrentValueAsString() }, { "RATE", ch.param (C::pRate)->getCurrentValueAsString() },
                                            { "AMOUNT", ch.param (C::pAmount)->getCurrentValueAsString() }, { "DELAY", ch.param (C::pDelay)->getCurrentValueAsString() },
                                            { "SWING", "+/- " + String (swing, 1) + " ms" }, { "DRY/WET", ch.getInt (C::pMode) == 2 ? String ("100%") : ch.param (C::pMix)->getCurrentValueAsString() } });
        }

    private:
        float xT (double t) const   { return area.getX() + (float) (t / span) * area.getWidth(); }
        float yMs (float ms) const  { return area.getBottom() - jlimit (0.0f, 1.0f, ms / top) * area.getHeight(); }
        float msAt (float y) const  { return (area.getBottom() - y) / jmax (1.0f, area.getHeight()) * top; }
        C& ch;
        Rectangle<float> area, readoutArea;
        double span = 2.0;
        float top = 20.0f, startRate = 1.0f;
    };

    //==============================================================================
    /** Phaser-Flanger: what it does to each frequency right now (the notches move as it sweeps), with the
        range of the sweep as a band. */
    class PhaserView : public HandleView
    {
    public:
        using F = pulse::PhaserFlanger;
        explicit PhaserView (F& f) : HandleView (f), ph (f)
        {
            handles.push_back ({ "Centre / feedback", Theme::accent, { P (F::pCentre), P (F::pFeedback) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (area, ph.get (F::pCentre)), area.getCentreY() - ph.get (F::pFeedback) / 0.95f * area.getHeight() * 0.42f); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (F::pCentre), SpaceUI::freqForX (area, p.x));
                                     Graphs::setValue (P (F::pFeedback), (area.getCentreY() - p.y) / (area.getHeight() * 0.42f) * 0.95f);
                                 },
                                 [this] { return ph.param (F::pCentre)->getCurrentValueAsString() + ", feedback " + ph.param (F::pFeedback)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (F::pFeedback), ph.get (F::pFeedback) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Amount", Theme::yellow, { P (F::pAmount) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (area, topOfSweep()), area.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 { Graphs::setValue (P (F::pAmount), std::log2 (jmax (1.0f, SpaceUI::freqForX (area, p.x) / ph.get (F::pCentre))) / F::sweepOctaves); },
                                 [this] { return ph.param (F::pAmount)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (F::pAmount), ph.get (F::pAmount) + (d > 0 ? 0.05f : -0.05f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (32.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "WHAT IT DOES TO EACH FREQUENCY", "drag: centre (sideways), feedback (up / down), amount (the point on the edge)");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, area, true);
            for (float db = 12.0f; db >= -36.0f; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.12f : 0.04f));
                g.drawHorizontalLine ((int) yDb (db), area.getX(), area.getRight());
                Graphs::label (g, String ((int) db), { area.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            // the band the sweep covers
            const float centre = ph.get (F::pCentre), lowEdge = centre * centre / topOfSweep();
            g.setColour (Theme::yellow.withAlpha (0.06f));
            g.fillRect (Rectangle<float> (SpaceUI::xForFreq (area, lowEdge), area.getY(), SpaceUI::xForFreq (area, topOfSweep()) - SpaceUI::xForFreq (area, lowEdge), area.getHeight()));
            g.setColour (Theme::yellow.withAlpha (0.5f));
            g.drawHorizontalLine ((int) area.getBottom() - 1, SpaceUI::xForFreq (area, lowEdge), SpaceUI::xForFreq (area, topOfSweep()));
            auto curve = [&] (double swept)
            {
                Path p;
                for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
                {
                    const float y = yDb (Decibels::gainToDecibels (ph.responseAt (SpaceUI::freqForX (area, x), swept), -60.0f));
                    if (x == area.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                return p;
            };
            g.setColour (Theme::orange.withAlpha (0.35f));
            g.strokePath (curve (ph.sweptRight.load()), PathStrokeType (1.2f));
            Graphs::glowStroke (g, curve (ph.sweptLeft.load()), Theme::accent, 1.8f);
            drawHandles (g);
            const int sync = ph.getInt (F::pSync);
            drawReadouts (g, readoutArea, { { "MODE", ph.param (F::pMode)->getCurrentValueAsString() }, { "RATE", sync > 0 ? F::syncNames[sync] : ph.param (F::pRate)->getCurrentValueAsString() },
                                            { "AMOUNT", ph.param (F::pAmount)->getCurrentValueAsString() }, { "CENTRE", ph.param (F::pCentre)->getCurrentValueAsString() },
                                            { "FEEDBACK", ph.param (F::pFeedback)->getCurrentValueAsString() }, { "NOW AT", pulse::fmt::hz (ph.sweptLeft.load()) } });
        }

    private:
        float topOfSweep() const   { return ph.get (F::pCentre) * std::pow (2.0f, ph.get (F::pAmount) * F::sweepOctaves); }
        float yDb (float db) const { return area.getY() + jlimit (0.0f, 1.0f, (18.0f - db) / 60.0f) * area.getHeight(); }
        F& ph;
        Rectangle<float> area, readoutArea;
    };

    //==============================================================================
    /** Auto Pan: how loud the left (blue) and the right (orange) side are over two cycles, and where it is now. */
    class AutoPanView : public HandleView
    {
    public:
        using A = pulse::AutoPan;
        explicit AutoPanView (A& a) : HandleView (a), pan (a)
        {
            handles.push_back ({ "Amount / rate", Theme::accent, { P (A::pAmount), P (A::pRate), P (A::pSync) },
                                 [this] { return juce::Point<float> (xC (0.75), yG (1.0f - pan.get (A::pAmount))); },
                                 [this] (juce::Point<float> p, juce::Point<float> d)
                                 {
                                     Graphs::setValue (P (A::pAmount), 1.0f - (area.getBottom() - p.y) / jmax (1.0f, area.getHeight()));
                                     dragRate (P (A::pRate), P (A::pSync), startRate, startSync, d.x);
                                 },
                                 [this] { return pan.param (A::pAmount)->getCurrentValueAsString() + "  " + rateText(); },
                                 [this] (float d)
                                 {
                                     if (pan.getInt (A::pSync) > 0) Graphs::setValue (P (A::pSync), (float) jlimit (1, A::syncNames.size() - 1, pan.getInt (A::pSync) + (d > 0 ? -1 : 1)));
                                     else Graphs::setValue (P (A::pRate), pan.get (A::pRate) * (d > 0 ? 1.1f : 0.9f));
                                 } });
            handles.push_back ({ "Stereo phase", Theme::orange, { P (A::pPhase) },
                                 [this] { return juce::Point<float> (xC (1.0 + pan.get (A::pPhase) / 360.0 * 0.999), area.getY()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (A::pPhase), (float) jlimit (0.0, 360.0, (cAt (p.x) - 1.0) * 360.0)); },
                                 [this] { return pan.param (A::pPhase)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (A::pPhase), pan.get (A::pPhase) + (d > 0 ? 15.0f : -15.0f)); } });
        }

        void mouseDown (const MouseEvent& e) override { startRate = pan.get (A::pRate); startSync = pan.getInt (A::pSync); HandleView::mouseDown (e); }
        void dragAmountRate (float dx, float dy) { startRate = pan.get (A::pRate); startSync = pan.getInt (A::pSync); dragHandle (0, dx, dy); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (36.0f).withTrimmedBottom (16.0f).withTrimmedTop (6.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area.translated (0.0f, -6.0f), "HOW LOUD EACH SIDE IS", "left blue, right orange   drag: amount (up / down), rate (sideways), the right side's phase (top edge)");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            for (int q = 0; q <= 8; ++q)
            {
                g.setColour (Theme::ink.withAlpha (q % 4 == 0 ? 0.10f : 0.04f));
                g.drawVerticalLine ((int) xC (q * 0.25), area.getY(), area.getBottom());
            }
            for (float v : { 0.0f, 0.5f, 1.0f })
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawHorizontalLine ((int) yG (v), area.getX(), area.getRight());
                Graphs::label (g, String (roundToInt (v * 100.0f)) + "%", { area.getX() - 36.0f, yG (v) - 6.0f, 32.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            const int shape = pan.getInt (A::pShape);
            const float amount = pan.get (A::pAmount);
            const double offset = pan.get (A::pPhase) / 360.0;
            auto curve = [&] (double off)
            {
                Path p;
                for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
                {
                    const double c = cAt (x) + off;
                    // "random" is drawn as steps that stay the same from frame to frame
                    const float held = (float) std::sin (std::floor (c) * 12.9898 + off * 7.0) ;
                    const float y = yG (A::gainFor (A::lfoValue (shape, c, held), amount));
                    if (x == area.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                return p;
            };
            auto right = curve (offset), left = curve (0.0);
            Graphs::glowStroke (g, right, Theme::orange, 1.6f);
            Graphs::glowStroke (g, left, Theme::accent, 1.8f);
            const float nowX = xC (pan.lfoPhase.load());
            g.setColour (Theme::ink.withAlpha (0.5f));
            g.drawVerticalLine ((int) nowX, area.getY(), area.getBottom());
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "AMOUNT", pan.param (A::pAmount)->getCurrentValueAsString() }, { "RATE", rateText() },
                                            { "SHAPE", pan.param (A::pShape)->getCurrentValueAsString() }, { "STEREO PHASE", pan.param (A::pPhase)->getCurrentValueAsString() },
                                            { "WORKS AS", pan.get (A::pPhase) < 1.0f || pan.get (A::pPhase) > 359.0f ? String ("Tremolo") : String ("Auto-pan") },
                                            { "SMOOTH", pan.param (A::pSmooth)->getCurrentValueAsString() } });
        }

    private:
        String rateText() const { const int s = pan.getInt (A::pSync); return s > 0 ? A::syncNames[s] : pan.param (A::pRate)->getCurrentValueAsString(); }
        float xC (double cycles) const { return area.getX() + (float) (cycles / 2.0) * area.getWidth(); }
        double cAt (float x) const     { return (double) ((x - area.getX()) / jmax (1.0f, area.getWidth())) * 2.0; }
        float yG (float gain) const    { return area.getBottom() - jlimit (0.0f, 1.0f, gain) * area.getHeight(); }
        A& pan;
        Rectangle<float> area, readoutArea;
        float startRate = 2.0f;
        int startSync = 0;
    };

    //==============================================================================
    class ChorusPanel : public DynamicsUI::Panel
    {
    public:
        using C = pulse::Chorus;
        ChorusPanel (EditViewState& evs, C& c)
        {
            display = std::make_unique<ChorusView> (c);
            addAndMakeVisible (*display);
            auto* md = strips.add (new Strip ("MODE"));
            md->add (new ParamChoice (*c.param (C::pMode), Theme::accent, false), 250);
            auto* mv = strips.add (new Strip ("MOVEMENT"));
            mv->add (knob (evs, *c.param (C::pRate), "Rate"), 60);
            mv->add (knob (evs, *c.param (C::pAmount), "Amount"), 60);
            mv->add (knob (evs, *c.param (C::pDelay), "Delay"), 60);
            auto* col = strips.add (new Strip ("COLOUR"));
            col->add (knob (evs, *c.param (C::pFeedback), "Feedback"), 62);
            col->add (knob (evs, *c.param (C::pLowCut), "Low cut"), 60);
            col->add (knob (evs, *c.param (C::pHighCut), "High cut"), 60);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *c.param (C::pWidth), "Width"), 60);
            out->add (knob (evs, *c.param (C::pMix), "Dry/Wet"), 62);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class PhaserPanel : public DynamicsUI::Panel
    {
    public:
        using F = pulse::PhaserFlanger;
        PhaserPanel (EditViewState& evs, F& f)
        {
            display = std::make_unique<PhaserView> (f);
            addAndMakeVisible (*display);
            auto* md = strips.add (new Strip ("MODE"));
            md->add (new ParamChoice (*f.param (F::pMode), Theme::accent, false), 160);
            auto* sw = strips.add (new Strip ("SWEEP"));
            sw->add (new ParamCombo (*f.param (F::pSync), false), 104);
            sw->add (knob (evs, *f.param (F::pRate), "Rate"), 58);
            sw->add (knob (evs, *f.param (F::pAmount), "Amount"), 58);
            sw->add (knob (evs, *f.param (F::pCentre), "Centre"), 58);
            auto* col = strips.add (new Strip ("COLOUR"));
            col->add (knob (evs, *f.param (F::pFeedback), "Feedback"), 62);
            col->add (new ParamChoice (*f.param (F::pStages), Theme::accent, false), 190);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *f.param (F::pSpread), "Stereo"), 58);
            out->add (knob (evs, *f.param (F::pMix), "Dry/Wet"), 62);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class AutoPanPanel : public DynamicsUI::Panel
    {
    public:
        using A = pulse::AutoPan;
        AutoPanPanel (EditViewState& evs, A& a)
        {
            display = std::make_unique<AutoPanView> (a);
            addAndMakeVisible (*display);
            auto* mv = strips.add (new Strip ("MOVEMENT"));
            mv->add (knob (evs, *a.param (A::pAmount), "Amount"), 60);
            mv->add (new ParamCombo (*a.param (A::pSync), false), 104);
            mv->add (knob (evs, *a.param (A::pRate), "Rate"), 60);
            auto* sh = strips.add (new Strip ("SHAPE"));
            sh->add (new ParamChoice (*a.param (A::pShape), Theme::accent, false), 420);
            auto* st2 = strips.add (new Strip ("STEREO"));
            st2->add (knob (evs, *a.param (A::pPhase), "Phase"), 60);
            st2->add (knob (evs, *a.param (A::pSmooth), "Smooth"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
