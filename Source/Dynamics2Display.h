#pragma once

#include "Dynamics2.h"
#include "SpaceDisplay.h"

/** Windows for Pulse Gate, the Transient Shaper and the De-esser: the level history with the
    settings as lines and points to drag, and only the leftovers as knobs and buttons below. */
namespace DynamicsUI
{
    /** Input (grey) and output (colour) from the bottom, on a dB scale down to `floorDb`. */
    inline void drawLevels (Graphics& g, Rectangle<float> area, const pulse::LevelHistory& h, float floorDb, Colour outCol)
    {
        auto yFor = [&] (float db) { return area.getY() + jlimit (0.0f, 1.0f, db / floorDb) * area.getHeight(); };
        for (float db = 0; db >= floorDb; db -= 12.0f)
        {
            g.setColour (Theme::ink.withAlpha (db == 0 ? 0.12f : 0.04f));
            g.drawHorizontalLine ((int) yFor (db), area.getX(), area.getRight());
            Graphs::label (g, String ((int) db), { area.getX() - 30.0f, yFor (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
        }
        const int n = pulse::LevelHistory::size, w = h.write.load();
        const float step = area.getWidth() / (float) (n - 1);
        Path inP, outP;
        inP.startNewSubPath (area.getX(), area.getBottom());
        outP.startNewSubPath (area.getX(), area.getBottom());
        for (int i = 0; i < n; ++i)
        {
            const int idx = (w + i) % n;
            const float x = area.getX() + step * i;
            inP.lineTo (x, yFor (jmax (floorDb, h.in[(size_t) idx].load())));
            outP.lineTo (x, yFor (jmax (floorDb, h.out[(size_t) idx].load())));
        }
        inP.lineTo (area.getRight(), area.getBottom()); inP.closeSubPath();
        outP.lineTo (area.getRight(), area.getBottom()); outP.closeSubPath();
        g.setColour (Theme::ink.withAlpha (0.10f));
        g.fillPath (inP);
        g.setGradientFill (ColourGradient (outCol.withAlpha (0.45f), 0.0f, area.getY(), outCol.withAlpha (0.06f), 0.0f, area.getBottom(), false));
        g.fillPath (outP);
    }

    /** The gain over time as a line around a centre: up for a boost, down for a cut (`scaleDb` at the edges). */
    inline void drawGainLine (Graphics& g, Rectangle<float> area, const pulse::LevelHistory& h, float scaleDb, Colour boost, Colour cut)
    {
        const int n = pulse::LevelHistory::size, w = h.write.load();
        const float step = area.getWidth() / (float) (n - 1), cy = area.getCentreY();
        g.setColour (Theme::ink.withAlpha (0.14f));
        g.drawHorizontalLine ((int) cy, area.getX(), area.getRight());
        Path up, down, line;
        up.startNewSubPath (area.getX(), cy);
        down.startNewSubPath (area.getX(), cy);
        for (int i = 0; i < n; ++i)
        {
            const float gain = -h.gr[(size_t) ((w + i) % n)].load();   // the history holds reduction; gain is its opposite
            const float x = area.getX() + step * i, y = cy - jlimit (-1.0f, 1.0f, gain / scaleDb) * area.getHeight() * 0.5f;
            up.lineTo (x, jmin (cy, y));
            down.lineTo (x, jmax (cy, y));
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        up.lineTo (area.getRight(), cy); up.closeSubPath();
        down.lineTo (area.getRight(), cy); down.closeSubPath();
        g.setColour (boost.withAlpha (0.3f));
        g.fillPath (up);
        g.setColour (cut.withAlpha (0.3f));
        g.fillPath (down);
        g.setColour (Theme::text.withAlpha (0.8f));
        g.strokePath (line, PathStrokeType (1.3f));
    }

    //==============================================================================
    /** Gate: the level history with the threshold, the hysteresis line under it and the range to drag,
        the curve of what comes out for what goes in, and a light that shows when it is open. */
    class GateView : public SpaceUI::HandleView
    {
    public:
        using G = pulse::Gate;
        explicit GateView (G& gt) : HandleView (gt), gate (gt)
        {
            handles.push_back ({ "Threshold", Theme::yellow, { P (G::pThreshold) },
                                 [this] { return juce::Point<float> (hist.getRight() - 40.0f, yDb (gate.get (G::pThreshold))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (G::pThreshold), dbAt (p.y)); },
                                 [this] { return gate.param (G::pThreshold)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (G::pThreshold), gate.get (G::pThreshold) + (d > 0 ? 1.0f : -1.0f)); } });
            handles.push_back ({ "Hysteresis", Theme::orange, { P (G::pHysteresis) },
                                 [this] { return juce::Point<float> (hist.getRight() - 110.0f, yDb (gate.get (G::pThreshold) - gate.get (G::pHysteresis))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (G::pHysteresis), gate.get (G::pThreshold) - dbAt (p.y)); },
                                 [this] { return gate.param (G::pHysteresis)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Range", Theme::red, { P (G::pRange) },
                                 [this] { return juce::Point<float> (hist.getX() + 60.0f, yReduction (gate.get (G::pRange))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (G::pRange), (p.y - hist.getY()) / (hist.getHeight() * 0.5f) * 80.0f); },
                                 [this] { return gate.param (G::pRange)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (12.0f);
            r.removeFromTop (20.0f);
            curve = r.removeFromRight (jmin (r.getHeight(), r.getWidth() * 0.32f)).reduced (20.0f, 4.0f).withTrimmedBottom (14.0f);
            r.removeFromRight (16.0f);
            hist = r.withTrimmedLeft (32.0f).withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, hist, "LEVEL", "input (grey)   output (green)   turned down (red, from the top)");
            drawLevels (g, hist, gate.history, floorDb, Theme::green);
            // what is being taken away, from the top (the range is the most it can take)
            {
                const int n = pulse::LevelHistory::size, w = gate.history.write.load();
                Path p;
                p.startNewSubPath (hist.getX(), hist.getY());
                for (int i = 0; i < n; ++i)
                    p.lineTo (hist.getX() + hist.getWidth() * i / (float) (n - 1), yReduction (jmax (0.0f, gate.history.gr[(size_t) ((w + i) % n)].load())));
                p.lineTo (hist.getRight(), hist.getY());
                p.closeSubPath();
                g.setColour (Theme::red.withAlpha (0.28f));
                g.fillPath (p);
            }
            const float th = gate.get (G::pThreshold), hy = gate.get (G::pHysteresis);
            const float dash[] = { 5.0f, 3.0f };
            if (gate.getInt (G::pMode) != 1)
            {
                g.setColour (Theme::orange.withAlpha (0.10f));
                g.fillRect (Rectangle<float> (hist.getX(), yDb (th), hist.getWidth(), yDb (th - hy) - yDb (th)));
                g.setColour (Theme::orange.withAlpha (0.7f));
                g.drawDashedLine (Line<float> (hist.getX(), yDb (th - hy), hist.getRight(), yDb (th - hy)), dash, 2, 1.0f);
            }
            g.setColour (Theme::yellow);
            g.drawDashedLine (Line<float> (hist.getX(), yDb (th), hist.getRight(), yDb (th)), dash, 2, 1.5f);
            g.setColour (Theme::red.withAlpha (0.8f));
            g.drawHorizontalLine ((int) yReduction (gate.get (G::pRange)), hist.getX(), hist.getX() + 130.0f);
            // open / shut
            const bool open = gate.isOpen.load();
            auto lamp = Rectangle<float> (hist.getRight() - 86.0f, hist.getY() + 6.0f, 80.0f, 20.0f);
            g.setColour ((open ? Theme::green : Theme::red).withAlpha (0.2f));
            g.fillRoundedRectangle (lamp, 10.0f);
            g.setColour (open ? Theme::green : Theme::red);
            g.fillEllipse (lamp.getX() + 6.0f, lamp.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText (open ? "OPEN" : "SHUT", lamp.withTrimmedLeft (18.0f), Justification::centred);

            // what comes out for what goes in
            SpaceUI::areaTitle (g, curve, "IN > OUT");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (curve.expanded (6.0f), 5.0f);
            auto cx = [&] (float db) { return curve.getX() + (1.0f - db / floorDb) * curve.getWidth(); };
            auto cy = [&] (float db) { return curve.getY() + jlimit (0.0f, 1.0f, db / floorDb) * curve.getHeight(); };
            for (float db = 0; db >= floorDb; db -= 20.0f)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) cx (db), curve.getY(), curve.getBottom());
                g.drawHorizontalLine ((int) cy (db), curve.getX(), curve.getRight());
            }
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawLine (cx (floorDb), cy (floorDb), cx (0), cy (0), 1.0f);
            Path c;
            for (float x = curve.getX(); x <= curve.getRight(); x += 1.0f)
            {
                const float in = floorDb * (1.0f - (x - curve.getX()) / curve.getWidth());
                const float y = cy (jmax (floorDb, in + gate.staticGainDb (in)));
                if (x == curve.getX()) c.startNewSubPath (x, y); else c.lineTo (x, y);
            }
            Graphs::glowStroke (g, c, Theme::green, 2.0f);
            const float key = gate.keyDb.load();
            if (key > floorDb)
            {
                g.setColour (Theme::ink);
                g.fillEllipse (cx (key) - 3.5f, cy (jmax (floorDb, key + gate.staticGainDb (key))) - 3.5f, 7.0f, 7.0f);
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "THRESHOLD", gate.param (G::pThreshold)->getCurrentValueAsString() }, { "RANGE", gate.param (G::pRange)->getCurrentValueAsString() },
                                            { "HYSTERESIS", gate.param (G::pHysteresis)->getCurrentValueAsString() }, { "TURNED DOWN", String (-gate.gainReductionDb.load(), 1) + " dB" },
                                            { "KEY", sidechainName() }, { "MODE", gate.param (G::pMode)->getCurrentValueAsString() } });
        }

    private:
        String sidechainName() const
        {
            auto s = gate.getSidechainSourceName();
            return s.isEmpty() || s.equalsIgnoreCase ("none") || s.equalsIgnoreCase ("<none>") ? String ("This track") : s;
        }
        static constexpr float floorDb = -80.0f;
        float yDb (float db) const        { return hist.getY() + jlimit (0.0f, 1.0f, db / floorDb) * hist.getHeight(); }
        float dbAt (float y) const        { return (y - hist.getY()) / hist.getHeight() * floorDb; }
        float yReduction (float db) const { return hist.getY() + jlimit (0.0f, 1.0f, db / 80.0f) * hist.getHeight() * 0.5f; }

        G& gate;
        Rectangle<float> hist, curve, readoutArea;
    };

    //==============================================================================
    /** Transient shaper: a picture of a hit and its tail. Drag the top of the hit up or down for the
        attack, the tail for the sustain, and the point on the time line for how long a hit lasts. Next
        to it, what it is doing to the sound right now. */
    class TransientView : public SpaceUI::HandleView
    {
    public:
        using T = pulse::TransientShaper;
        explicit TransientView (T& t) : HandleView (t), ts (t)
        {
            handles.push_back ({ "Attack", Theme::orange, { P (T::pAttack) },
                                 [this] { return juce::Point<float> (xAt (hitTime()), yAmp (hitPeak (ts.get (T::pAttack)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (T::pAttack), amountFor ((shape.getBottom() - p.y) / shape.getHeight() / basePeak)); },
                                 [this] { return ts.param (T::pAttack)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (T::pAttack), ts.get (T::pAttack) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Sustain", Theme::accent, { P (T::pSustain) },
                                 [this] { return juce::Point<float> (xAt (0.55f), yAmp (tailAt (0.55f, ts.get (T::pSustain)))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (T::pSustain), amountFor ((shape.getBottom() - p.y) / shape.getHeight() / tailAt (0.55f, 0.0f))); },
                                 [this] { return ts.param (T::pSustain)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (T::pSustain), ts.get (T::pSustain) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Hit length", Theme::yellow, { P (T::pLength) },
                                 [this] { return juce::Point<float> (xAt (hitEnd()), shape.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (T::pLength), 2.0f + jlimit (0.0f, 1.0f, ((p.x - shape.getX()) / shape.getWidth() - 0.06f) / 0.3f) * 78.0f); },
                                 [this] { return ts.param (T::pLength)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (12.0f);
            r.removeFromTop (20.0f);
            shape = r.removeFromLeft (r.getWidth() * 0.44f).withTrimmedBottom (10.0f);
            r.removeFromLeft (46.0f);
            live = r.withTrimmedBottom (10.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, shape, "SHAPE", "drag the hit, the tail and the length");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (shape.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.1f));
            g.drawHorizontalLine ((int) shape.getBottom(), shape.getX(), shape.getRight());
            const float att = ts.get (T::pAttack), sus = ts.get (T::pSustain);
            auto envelope = [&] (float a, float s)
            {
                Path p;
                p.startNewSubPath (shape.getX(), shape.getBottom());
                for (float t = 0.0f; t <= 1.0f; t += 0.004f)
                {
                    float v;
                    if (t < hitTime()) v = hitPeak (a) * t / hitTime();
                    else if (t < hitEnd()) { const float u = (t - hitTime()) / (hitEnd() - hitTime()); v = hitPeak (a) + (tailAt (hitEnd(), s) - hitPeak (a)) * u * u * (3.0f - 2.0f * u); }
                    else v = tailAt (t, s);
                    p.lineTo (xAt (t), yAmp (v));
                }
                p.lineTo (shape.getRight(), shape.getBottom());
                p.closeSubPath();
                return p;
            };
            g.setColour (Theme::ink.withAlpha (0.10f));
            g.fillPath (envelope (0.0f, 0.0f));   // as it came in
            auto shaped = envelope (att, sus);
            g.setGradientFill (ColourGradient (Theme::orange.withAlpha (0.45f), shape.getX(), 0.0f, Theme::accent.withAlpha (0.35f), shape.getRight(), 0.0f, false));
            g.fillPath (shaped);
            g.setColour (Theme::text.withAlpha (0.85f));
            g.strokePath (shaped, PathStrokeType (1.5f));
            g.setColour (Theme::yellow.withAlpha (0.5f));
            g.drawVerticalLine ((int) xAt (hitEnd()), shape.getY(), shape.getBottom());
            Graphs::label (g, "hit", { shape.getX() + 4.0f, shape.getBottom() + 2.0f, 60.0f, 12.0f }, Justification::centredLeft, 0.6f);
            Graphs::label (g, "tail", { xAt (hitEnd()) + 6.0f, shape.getBottom() + 2.0f, 60.0f, 12.0f }, Justification::centredLeft, 0.6f);

            SpaceUI::areaTitle (g, live, "RIGHT NOW", "level (grey in, colour out) and the gain: up = louder, down = quieter");
            auto levels = live.withTrimmedLeft (32.0f);
            auto gainArea = levels.removeFromTop (levels.getHeight() * 0.42f).reduced (0.0f, 6.0f);
            drawGainLine (g, gainArea, ts.history, T::maxChangeDb, Theme::green, Theme::red);
            Graphs::label (g, "+" + String ((int) T::maxChangeDb), { gainArea.getX() - 32.0f, gainArea.getY() - 2.0f, 28.0f, 12.0f }, Justification::centredRight, 0.55f);
            Graphs::label (g, "-" + String ((int) T::maxChangeDb), { gainArea.getX() - 32.0f, gainArea.getBottom() - 10.0f, 28.0f, 12.0f }, Justification::centredRight, 0.55f);
            drawLevels (g, levels, ts.history, -48.0f, Theme::accent);
            drawHandles (g);
            const float now = ts.gainNowDb.load();
            drawReadouts (g, readoutArea, { { "ATTACK", ts.param (T::pAttack)->getCurrentValueAsString() }, { "SUSTAIN", ts.param (T::pSustain)->getCurrentValueAsString() },
                                            { "HIT LENGTH", ts.param (T::pLength)->getCurrentValueAsString() }, { "GAIN NOW", (now > 0.05f ? "+" : "") + String (now, 1) + " dB" },
                                            { "OUTPUT", ts.param (T::pOutput)->getCurrentValueAsString() }, { "DRY/WET", ts.param (T::pMix)->getCurrentValueAsString() } });
        }

    private:
        static constexpr float basePeak = 0.55f;   // the hit as it came in, as a fraction of the height
        float hitTime() const { return 0.06f; }
        float hitEnd() const  { return 0.06f + 0.04f + (ts.get (T::pLength) - 2.0f) / 78.0f * 0.3f; }
        // an amount of -1..1 scales the picture between a quarter and four times (about -12 .. +12 dB)
        static float factor (float amount)      { return std::pow (2.0f, amount * 0.8f); }
        static float amountFor (float f)        { return jlimit (-1.0f, 1.0f, std::log2 (jmax (0.05f, f)) / 0.8f); }
        float hitPeak (float attack) const      { return jmin (0.97f, basePeak * factor (attack)); }
        float tailAt (float t, float sustain) const { return jmin (0.9f, 0.34f * std::exp (-(t - 0.1f) * 2.4f) * factor (sustain)); }
        float xAt (float t) const               { return shape.getX() + t * shape.getWidth(); }
        float yAmp (float v) const              { return shape.getBottom() - v * shape.getHeight(); }

        T& ts;
        Rectangle<float> shape, live, readoutArea;
    };

    //==============================================================================
    /** De-esser: the band it listens to on a frequency scale, with one point for where (sideways) and
        how loud is too loud (up and down); the level in that band right now; and how much it is taking off. */
    class DeEsserView : public SpaceUI::HandleView
    {
    public:
        using D = pulse::DeEsser;
        explicit DeEsserView (D& d) : HandleView (d), de (d)
        {
            handles.push_back ({ "Frequency / threshold", Theme::yellow, { P (D::pFreq), P (D::pThreshold) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (band, de.get (D::pFreq)), yDb (de.get (D::pThreshold))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (D::pFreq), SpaceUI::freqForX (band, p.x));
                                     Graphs::setValue (P (D::pThreshold), (p.y - band.getY()) / band.getHeight() * -60.0f);
                                 },
                                 [this] { return de.param (D::pFreq)->getCurrentValueAsString() + ", " + de.param (D::pThreshold)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Range", Theme::red, { P (D::pRange) },
                                 [this] { return juce::Point<float> (meter.getCentreX(), meter.getY() + de.get (D::pRange) / 24.0f * meter.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (D::pRange), (p.y - meter.getY()) / meter.getHeight() * 24.0f); },
                                 [this] { return de.param (D::pRange)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (12.0f);
            r.removeFromTop (20.0f);
            meter = r.removeFromRight (34.0f).withTrimmedBottom (14.0f);
            r.removeFromRight (30.0f);
            auto left = r.withTrimmedLeft (32.0f);
            history = left.removeFromBottom (left.getHeight() * 0.3f).withTrimmedTop (26.0f).withTrimmedBottom (4.0f);
            band = left.withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, band, "WHAT IT LISTENS TO", "drag the point: sideways for where, up and down for how loud is too loud");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (band.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, band, true);
            for (float db = 0; db >= -60.0f; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (0.04f));
                g.drawHorizontalLine ((int) yDb (db), band.getX(), band.getRight());
                Graphs::label (g, String ((int) db), { band.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            // the detection band, as tall as the level in it right now
            const float level = jlimit (-60.0f, 0.0f, de.bandDb.load());
            const float th = de.get (D::pThreshold);
            const bool over = level > th;
            Path shapePath, fill;
            fill.startNewSubPath (band.getX(), band.getBottom());
            for (float x = band.getX(); x <= band.getRight(); x += 2.0f)
            {
                const float y = band.getBottom() - de.detectorResponse (SpaceUI::freqForX (band, x)) * (band.getBottom() - yDb (level));
                if (x == band.getX()) shapePath.startNewSubPath (x, y); else shapePath.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (band.getRight(), band.getBottom());
            fill.closeSubPath();
            const auto col = over ? Theme::red : Theme::accent;
            g.setGradientFill (ColourGradient (col.withAlpha (0.45f), 0.0f, band.getY(), col.withAlpha (0.05f), 0.0f, band.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, shapePath, col, 1.8f);
            const float dash[] = { 5.0f, 3.0f };
            g.setColour (Theme::yellow);
            g.drawDashedLine (Line<float> (band.getX(), yDb (th), band.getRight(), yDb (th)), dash, 2, 1.5f);
            Graphs::label (g, "too loud above this line", { band.getX() + 6.0f, yDb (th) - 14.0f, 200.0f, 12.0f }, Justification::centredLeft, 0.8f);

            // how much it has been taking off
            SpaceUI::areaTitle (g, history, "TAKEN OFF");
            {
                const int n = pulse::LevelHistory::size, w = de.history.write.load();
                Path p;
                p.startNewSubPath (history.getX(), history.getY());
                for (int i = 0; i < n; ++i)
                    p.lineTo (history.getX() + history.getWidth() * i / (float) (n - 1),
                              history.getY() + jlimit (0.0f, 1.0f, jmax (0.0f, de.history.gr[(size_t) ((w + i) % n)].load()) / 24.0f) * history.getHeight());
                p.lineTo (history.getRight(), history.getY());
                p.closeSubPath();
                g.setColour (Theme::ink.withAlpha (0.08f));
                g.drawHorizontalLine ((int) history.getY(), history.getX(), history.getRight());
                g.setColour (Theme::red.withAlpha (0.5f));
                g.fillPath (p);
            }
            // the meter, with the most it may take off as a point to drag
            g.setColour (Pro::shade (0.4f));
            g.fillRoundedRectangle (meter, 4.0f);
            const float red = jlimit (0.0f, 24.0f, de.reductionDb.load());
            g.setColour (Theme::red);
            g.fillRoundedRectangle (meter.withHeight (red / 24.0f * meter.getHeight()).reduced (5.0f, 0.0f), 3.0f);
            Graphs::label (g, "0", { meter.getX() - 20.0f, meter.getY() - 6.0f, 18.0f, 12.0f }, Justification::centredRight, 0.55f);
            Graphs::label (g, "-24", { meter.getX() - 26.0f, meter.getBottom() - 6.0f, 24.0f, 12.0f }, Justification::centredRight, 0.55f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "FREQUENCY", de.param (D::pFreq)->getCurrentValueAsString() }, { "THRESHOLD", de.param (D::pThreshold)->getCurrentValueAsString() },
                                            { "RANGE", de.param (D::pRange)->getCurrentValueAsString() }, { "TAKING OFF", String (-red, 1) + " dB" },
                                            { "BAND LEVEL", String (level, 1) + " dB" }, { "MODE", de.param (D::pMode)->getCurrentValueAsString() } });
        }

    private:
        float yDb (float db) const { return band.getY() + jlimit (0.0f, 1.0f, db / -60.0f) * band.getHeight(); }
        D& de;
        Rectangle<float> band, history, meter, readoutArea;
    };

    //==============================================================================
    class GatePanel : public Panel
    {
    public:
        using G = pulse::Gate;
        GatePanel (EditViewState& evs, G& gt)
        {
            display = std::make_unique<GateView> (gt);
            addAndMakeVisible (*display);
            auto* md = strips.add (new Strip ("MODE"));
            md->add (new ParamChoice (*gt.param (G::pMode), Theme::green, false), 230);
            md->add (knob (evs, *gt.param (G::pRatio), "Ratio"), 60);
            auto* lv = strips.add (new Strip ("LEVEL"));
            lv->add (knob (evs, *gt.param (G::pThreshold), "Threshold"), 64);
            lv->add (knob (evs, *gt.param (G::pHysteresis), "Hysteresis"), 64);
            lv->add (knob (evs, *gt.param (G::pRange), "Range"), 60);
            auto* tm = strips.add (new Strip ("TIMING"));
            tm->add (knob (evs, *gt.param (G::pAttack), "Attack"), 62);
            tm->add (knob (evs, *gt.param (G::pHold), "Hold"), 62);
            tm->add (knob (evs, *gt.param (G::pRelease), "Release"), 62);
            tm->add (knob (evs, *gt.param (G::pLookahead), "Lookahead"), 66);
            auto* key = strips.add (new Strip ("KEY"));
            key->add (knob (evs, *gt.param (G::pScHpf), "Low cut"), 60);
            key->add (knob (evs, *gt.param (G::pScLpf), "High cut"), 60);
            key->add (new ParamToggle (*gt.param (G::pScListen), "Listen", Theme::yellow), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class TransientPanel : public Panel
    {
    public:
        using T = pulse::TransientShaper;
        TransientPanel (EditViewState& evs, T& t)
        {
            display = std::make_unique<TransientView> (t);
            addAndMakeVisible (*display);
            auto* sh = strips.add (new Strip ("SHAPE"));
            sh->add (knob (evs, *t.param (T::pAttack), "Attack"), 66);
            sh->add (knob (evs, *t.param (T::pSustain), "Sustain"), 66);
            sh->add (knob (evs, *t.param (T::pLength), "Hit length"), 70);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *t.param (T::pOutput), "Output"), 64);
            out->add (new ParamToggle (*t.param (T::pClip), "Soft clip", Theme::orange), 80);
            out->add (knob (evs, *t.param (T::pMix), "Dry/Wet"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class DeEsserPanel : public Panel
    {
    public:
        using D = pulse::DeEsser;
        DeEsserPanel (EditViewState& evs, D& d)
        {
            display = std::make_unique<DeEsserView> (d);
            addAndMakeVisible (*display);
            auto* md = strips.add (new Strip ("REDUCE"));
            md->add (new ParamChoice (*d.param (D::pMode), Theme::accent, false), 200);
            md->add (knob (evs, *d.param (D::pRange), "Range"), 64);
            md->add (knob (evs, *d.param (D::pRelease), "Release"), 64);
            auto* det = strips.add (new Strip ("DETECT"));
            det->add (knob (evs, *d.param (D::pFreq), "Frequency"), 70);
            det->add (knob (evs, *d.param (D::pThreshold), "Threshold"), 70);
            det->add (new ParamToggle (*d.param (D::pListen), "Listen", Theme::yellow), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
