#pragma once

#include "SpectralFx.h"
#include "LoopFxDisplay.h"

/** The window of Pulse Spectral: the frequencies going in (grey) and coming out (colour), the line that says
    how late each frequency comes out (drag its two ends), and the shift as a point at the top. */
namespace SpectralUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    class SpectralView : public HandleView
    {
    public:
        using S = pulse::Spectral;
        explicit SpectralView (S& s) : HandleView (s), sp (s)
        {
            handles.push_back ({ "Shift", Theme::accent, { P (S::pShift) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (area, 1000.0f * std::pow (2.0f, sp.get (S::pShift) / 12.0f)), area.getY()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     const float st = 12.0f * std::log2 (SpaceUI::freqForX (area, p.x) / 1000.0f);
                                     Graphs::setValue (P (S::pShift), std::abs (st - std::round (st)) < 0.3f ? std::round (st) : st);   // catches on whole semitones
                                 },
                                 [this] { return sp.param (S::pShift)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pShift), std::round (sp.get (S::pShift)) + (d > 0 ? 1.0f : -1.0f)); } });
            auto endHandle = [this] (const char* name, bool high)
            {
                handles.push_back ({ name, Theme::yellow, { P (S::pDelay), P (S::pTilt) },
                                     [this, high] { return juce::Point<float> (high ? area.getRight() : area.getX(), yMs (sp.delayAt (high ? 1.0f : 0.0f))); },
                                     [this, high] (juce::Point<float> p, juce::Point<float>)
                                     {
                                         // the two ends of the line are the delay of the lowest and of the highest frequency
                                         float lo = sp.delayAt (0.0f), hi = sp.delayAt (1.0f);
                                         (high ? hi : lo) = jlimit (0.0f, S::maxDelayMs, msAt (p.y));
                                         const float most = jmax (lo, hi);
                                         Graphs::setValue (P (S::pDelay), most);
                                         Graphs::setValue (P (S::pTilt), most < 0.5f ? 0.0f : hi >= lo ? 1.0f - lo / most : -(1.0f - hi / most));
                                     },
                                     [this, high] { return pulse::fmt::ms (sp.delayAt (high ? 1.0f : 0.0f)); }, {} });
            };
            endHandle ("Delay of the lows", false);
            endHandle ("Delay of the highs", true);
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (32.0f).withTrimmedRight (52.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "FREQUENCIES IN (GREY) AND OUT", "blue point: shift   yellow line: how late each frequency comes out (drag its ends)");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, area, true);
            for (float db = 0.0f; db >= -84.0f; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (0.04f));
                g.drawHorizontalLine ((int) yDb (db), area.getX(), area.getRight());
                Graphs::label (g, String ((int) db), { area.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            const bool held = sp.holding.load();
            auto curve = [&] (const std::array<std::atomic<float>, S::shownPoints>& v, bool closed)
            {
                Path p;
                if (closed) p.startNewSubPath (area.getX(), area.getBottom());
                for (int i = 0; i < S::shownPoints; ++i)
                {
                    const float x = area.getX() + area.getWidth() * ((float) i + 0.5f) / (float) S::shownPoints, y = yDb (v[(size_t) i].load());
                    if (i == 0 && ! closed) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                if (closed) { p.lineTo (area.getRight(), area.getBottom()); p.closeSubPath(); }
                return p;
            };
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.fillPath (curve (sp.shownIn, true));
            const auto col = held ? Theme::yellow : Theme::accent;
            g.setGradientFill (ColourGradient (col.withAlpha (0.35f), 0.0f, area.getY(), col.withAlpha (0.03f), 0.0f, area.getBottom(), false));
            g.fillPath (curve (sp.shownOut, true));
            Graphs::glowStroke (g, curve (sp.shownOut, false), col, 1.6f);

            // how late each frequency comes out (right-hand scale)
            for (float ms = 0.0f; ms <= S::maxDelayMs; ms += 500.0f)
                Graphs::label (g, pulse::fmt::ms (ms), { area.getRight() + 8.0f, yMs (ms) - 6.0f, 44.0f, 12.0f }, Justification::centredLeft, 0.55f);
            Path line;
            for (int i = 0; i <= 40; ++i)
            {
                const float t = (float) i / 40.0f, x = area.getX() + t * area.getWidth(), y = yMs (sp.delayAt (t));
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            g.setColour (Theme::yellow.withAlpha (sp.get (S::pDelay) > 0.5f ? 0.9f : 0.35f));
            g.strokePath (line, PathStrokeType (1.6f));
            // the shift, as an arrow from where 1 kHz was to where it goes
            const float x0 = SpaceUI::xForFreq (area, 1000.0f), x1 = SpaceUI::xForFreq (area, 1000.0f * std::pow (2.0f, sp.get (S::pShift) / 12.0f));
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.drawLine (x0, area.getY(), x1, area.getY(), 2.0f);
            g.drawVerticalLine ((int) x0, area.getY() - 4.0f, area.getY() + 5.0f);
            if (held)
            {
                auto lamp = Rectangle<float> (area.getRight() - 96.0f, area.getY() + 10.0f, 88.0f, 22.0f);
                g.setColour (Theme::yellow.withAlpha (0.2f));
                g.fillRoundedRectangle (lamp, 11.0f);
                g.setColour (Theme::yellow);
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText ("HOLDING", lamp, Justification::centred);
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "FREEZE", sp.getInt (S::pFreeze) > 0 ? (held ? "Holding" : "Waiting for a sound") : "Off" }, { "BLUR", sp.param (S::pBlur)->getCurrentValueAsString() },
                                            { "SHIFT", sp.param (S::pShift)->getCurrentValueAsString() }, { "DELAY", sp.param (S::pDelay)->getCurrentValueAsString() },
                                            { "TILT", sp.param (S::pTilt)->getCurrentValueAsString() }, { "FEEDBACK", sp.param (S::pFeedback)->getCurrentValueAsString() } });
        }

    private:
        float yDb (float db) const  { return area.getY() + jlimit (0.0f, 1.0f, db / -90.0f) * area.getHeight(); }
        float yMs (float ms) const  { return area.getBottom() - jlimit (0.0f, 1.0f, ms / S::maxDelayMs) * area.getHeight(); }
        float msAt (float y) const  { return (area.getBottom() - y) / jmax (1.0f, area.getHeight()) * S::maxDelayMs; }
        S& sp;
        Rectangle<float> area, readoutArea;
    };

    class SpectralPanel : public DynamicsUI::Panel
    {
    public:
        using S = pulse::Spectral;
        SpectralPanel (EditViewState& evs, S& s)
        {
            display = std::make_unique<SpectralView> (s);
            addAndMakeVisible (*display);
            auto* hd = strips.add (new Strip ("HOLD"));
            hd->add (new ParamToggle (*s.param (S::pFreeze), "Freeze", Theme::yellow), 76);
            hd->add (knob (evs, *s.param (S::pBlur), "Blur"), 58);
            auto* pt = strips.add (new Strip ("PITCH"));
            pt->add (knob (evs, *s.param (S::pShift), "Shift"), 60);
            auto* dl = strips.add (new Strip ("SPECTRAL DELAY"));
            dl->add (knob (evs, *s.param (S::pDelay), "Delay"), 58);
            dl->add (knob (evs, *s.param (S::pTilt), "Tilt"), 58);
            dl->add (knob (evs, *s.param (S::pFeedback), "Feedback"), 60);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *s.param (S::pMix), "Dry/Wet"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
