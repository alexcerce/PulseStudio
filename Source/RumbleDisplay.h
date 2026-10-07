#pragma once

#include "Rumble.h"
#include "ColourFxDisplay.h"

/** The window for Rumble. */
namespace RumbleUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Left: the last two seconds: the kick (grey), the rumble behind it (orange) and, from the top, how far the
        rumble is pushed down while the kick sounds (blue). Right: what is left of the reverb: a band of lows between
        Low Cut and Tone. One point there: sideways is the tone, up and down the amount. */
    class RumbleView : public HandleView
    {
    public:
        using R = pulse::Rumble;
        explicit RumbleView (R& r) : HandleView (r), rm (r)
        {
            handles.push_back ({ "Tone / amount", Theme::orange, { P (R::pTone), P (R::pAmount) },
                                 [this] { return juce::Point<float> (xHz (rm.get (R::pTone)), yAmount (rm.get (R::pAmount))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (R::pTone), hzAt (p.x));
                                     Graphs::setValue (P (R::pAmount), 6.0f - (p.y - tone.getY()) / jmax (1.0f, tone.getHeight()) * 42.0f);
                                 },
                                 [this] { return rm.param (R::pTone)->getCurrentValueAsString() + ", " + rm.param (R::pAmount)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pAmount), rm.get (R::pAmount) + (d > 0 ? 1.0f : -1.0f)); } });
            handles.push_back ({ "Low cut", Theme::accent, { P (R::pLowCut) },
                                 [this] { return juce::Point<float> (xHz (rm.get (R::pLowCut)), tone.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pLowCut), hzAt (p.x)); },
                                 [this] { return rm.param (R::pLowCut)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Duck", Theme::accent, { P (R::pDuck), P (R::pRecover) },
                                 [this] { return juce::Point<float> (time.getX() + 14.0f + rm.get (R::pRecover) / 800.0f * time.getWidth() * 0.4f, time.getY() + rm.get (R::pDuck) * time.getHeight() * 0.9f); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (R::pDuck), (p.y - time.getY()) / jmax (1.0f, time.getHeight() * 0.9f));
                                     Graphs::setValue (P (R::pRecover), (p.x - time.getX() - 14.0f) / jmax (1.0f, time.getWidth() * 0.4f) * 800.0f);
                                 },
                                 [this] { return "Duck " + rm.param (R::pDuck)->getCurrentValueAsString() + ", back in " + rm.param (R::pRecover)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            tone = r.removeFromRight (r.getWidth() * 0.36f).withTrimmedBottom (16.0f).withTrimmedLeft (10.0f);
            r.removeFromRight (26.0f);
            time = r.withTrimmedLeft (10.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());

            // --- over time ---
            SpaceUI::areaTitle (g, time, "KICK AND RUMBLE, THE LAST TWO SECONDS", "drag the point: down = duck further, sideways = how soon the rumble comes back");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (time.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.06f));
            for (int k = 1; k < 4; ++k) g.drawVerticalLine ((int) (time.getX() + time.getWidth() * (float) k / 4.0f), time.getY(), time.getBottom());
            const int n = R::historySize, newest = rm.historyAt.load (std::memory_order_relaxed);
            auto yLevel = [this] (float gain) { return time.getBottom() - jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (gain, -48.0f) + 48.0f) / 48.0f) * time.getHeight(); };
            Path kick, rumble, rumbleLine, duck;
            kick.startNewSubPath (time.getX(), time.getBottom());
            rumble.startNewSubPath (time.getX(), time.getBottom());
            for (int i = 0; i < n; ++i)
            {
                const auto idx = (size_t) ((newest + 1 + i) % n);
                const float x = time.getX() + time.getWidth() * (float) i / (float) (n - 1);
                kick.lineTo (x, yLevel (rm.kickHistory[idx].load (std::memory_order_relaxed)));
                const float yr = yLevel (rm.rumbleHistory[idx].load (std::memory_order_relaxed));
                rumble.lineTo (x, yr);
                if (i == 0) rumbleLine.startNewSubPath (x, yr); else rumbleLine.lineTo (x, yr);
                const float yd = time.getY() + (1.0f - rm.duckHistory[idx].load (std::memory_order_relaxed)) * time.getHeight() * 0.9f;
                if (i == 0) duck.startNewSubPath (x, yd); else duck.lineTo (x, yd);
            }
            kick.lineTo (time.getRight(), time.getBottom()); kick.closeSubPath();
            rumble.lineTo (time.getRight(), time.getBottom()); rumble.closeSubPath();
            g.setColour (Theme::ink.withAlpha (0.16f));
            g.fillPath (kick);
            g.setGradientFill (ColourGradient (Theme::orange.withAlpha (0.55f), 0.0f, time.getY(), Theme::orange.withAlpha (0.10f), 0.0f, time.getBottom(), false));
            g.fillPath (rumble);
            Graphs::glowStroke (g, rumbleLine, Theme::orange, 1.6f);
            g.setColour (Theme::accent.withAlpha (0.85f));
            g.strokePath (duck, PathStrokeType (1.4f));
            Graphs::label (g, "KICK", { time.getX() + 6.0f, time.getBottom() - 16.0f, 60.0f, 12.0f }, Justification::centredLeft, 0.5f);

            // --- what is left of the reverb ---
            SpaceUI::areaTitle (g, tone, "WHAT IS LEFT OF THE REVERB", "drag the points");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (tone.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.07f));
            g.setFont (FontOptions (9.0f));
            for (float f : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f })
            {
                g.setColour (Theme::ink.withAlpha (0.07f));
                g.drawVerticalLine ((int) xHz (f), tone.getY(), tone.getBottom());
                g.setColour (Theme::muted.withAlpha (0.7f));
                g.drawText (String ((int) f), (int) xHz (f) - 16, (int) tone.getBottom() + 2, 32, 12, Justification::centred);
            }
            Path fill, line;
            fill.startNewSubPath (tone.getX(), tone.getBottom());
            const float top = yAmount (rm.get (R::pAmount));
            for (float x = tone.getX(); x <= tone.getRight(); x += 2.0f)
            {
                const float y = tone.getBottom() - rm.toneResponse (hzAt (x)) * (tone.getBottom() - top);
                fill.lineTo (x, y);
                if (x == tone.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (tone.getRight(), tone.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::orange.withAlpha (0.45f), 0.0f, tone.getY(), Theme::orange.withAlpha (0.06f), 0.0f, tone.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, Theme::orange, 1.8f);

            drawHandles (g);
            drawReadouts (g, readoutArea, { { "AMOUNT", rm.param (R::pAmount)->getCurrentValueAsString() }, { "DECAY", rm.param (R::pDecay)->getCurrentValueAsString() },
                                            { "TONE", rm.param (R::pLowCut)->getCurrentValueAsString() + " - " + rm.param (R::pTone)->getCurrentValueAsString() },
                                            { "DRIVE", rm.param (R::pDrive)->getCurrentValueAsString() }, { "DUCK", rm.param (R::pDuck)->getCurrentValueAsString() },
                                            { "BACK IN", rm.param (R::pRecover)->getCurrentValueAsString() } });
        }

    private:
        // the tone picture shows 20 Hz to 1 kHz: the rumble lives at the bottom of that
        float xHz (float f) const       { return tone.getX() + std::log (jlimit (20.0f, 1000.0f, f) / 20.0f) / std::log (50.0f) * tone.getWidth(); }
        float hzAt (float x) const      { return 20.0f * std::pow (50.0f, jlimit (0.0f, 1.0f, (x - tone.getX()) / jmax (1.0f, tone.getWidth()))); }
        float yAmount (float db) const   { return tone.getY() + jlimit (0.0f, 1.0f, (6.0f - db) / 42.0f) * tone.getHeight(); }
        R& rm;
        Rectangle<float> time, tone, readoutArea;
    };

    class RumblePanel : public DynamicsUI::Panel
    {
    public:
        using R = pulse::Rumble;
        RumblePanel (EditViewState& evs, R& r)
        {
            display = std::make_unique<RumbleView> (r);
            addAndMakeVisible (*display);
            auto* rb = strips.add (new Strip ("RUMBLE"));
            rb->add (knob (evs, *r.param (R::pAmount), "Amount"), 62);
            rb->add (knob (evs, *r.param (R::pDecay), "Decay"), 60);
            rb->add (knob (evs, *r.param (R::pDrive), "Drive"), 60);
            rb->add (knob (evs, *r.param (R::pWidth), "Width"), 60);
            auto* tn = strips.add (new Strip ("TONE"));
            tn->add (knob (evs, *r.param (R::pLowCut), "Low cut"), 62);
            tn->add (knob (evs, *r.param (R::pTone), "Tone"), 60);
            auto* dk = strips.add (new Strip ("DUCKED BY THE KICK"));
            dk->add (knob (evs, *r.param (R::pDuck), "Duck"), 62);
            dk->add (knob (evs, *r.param (R::pRecover), "Recover"), 62);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (new ParamChoice (*r.param (R::pDry), Theme::orange, false), 230);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
