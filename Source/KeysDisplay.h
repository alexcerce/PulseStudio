#pragma once

#include "Keys.h"
#include "SpectralFxDisplay.h"

/** Windows for the Tonewheel Organ and the Electric Piano. */
namespace KeysUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** The organ: its nine drawbars to pull out (down) and push in, as on the instrument, and the speaker turning. */
    class OrganView : public HandleView
    {
    public:
        using O = pulse::Organ;
        explicit OrganView (O& o) : HandleView (o), org (o)
        {
            for (int b = 0; b < O::numBars; ++b)
                handles.push_back ({ "Drawbar " + O::barNames[b], barColour (b), { P (O::pBar1 + b) },
                                     [this, b] { return juce::Point<float> (barX (b), barTip (org.get (O::pBar1 + b))); },
                                     [this, b] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (O::pBar1 + b), (float) roundToInt ((p.y - bars.getY()) / jmax (1.0f, bars.getHeight()) * 8.0f)); },
                                     [this, b] { return String (roundToInt (org.get (O::pBar1 + b))); },
                                     [this, b] (float d) { Graphs::setValue (P (O::pBar1 + b), org.get (O::pBar1 + b) + (d > 0 ? -1.0f : 1.0f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            speaker = r.removeFromRight (jmin (r.getHeight(), r.getWidth() * 0.3f)).reduced (10.0f);
            r.removeFromRight (20.0f);
            bars = r.reduced (20.0f, 0.0f).withTrimmedTop (26.0f).withTrimmedBottom (26.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, bars.translated (0.0f, -26.0f), "DRAWBARS", "pull a bar down for more of that pitch (8 = full), push it up for none");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (bars.expanded (14.0f, 30.0f), 6.0f);
            const float w = jmin (46.0f, bars.getWidth() / (float) O::numBars - 10.0f);
            for (int b = 0; b < O::numBars; ++b)
            {
                const float x = barX (b), v = org.get (O::pBar1 + b), tip = barTip (v);
                const auto col = barColour (b);
                // the slot, then the part of the bar that is out, with its numbers
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (Rectangle<float> (x - w * 0.5f, bars.getY(), w, bars.getHeight()), 4.0f);
                if (v > 0.05f)
                {
                    auto out = Rectangle<float> (x - w * 0.5f + 2.0f, bars.getY(), w - 4.0f, tip - bars.getY());
                    g.setGradientFill (ColourGradient (col.darker (0.25f), out.getX(), 0.0f, col, out.getCentreX(), 0.0f, false));
                    g.fillRoundedRectangle (out, 3.0f);
                    g.setColour ((col.getBrightness() > 0.6f ? Colours::black : Colours::white).withAlpha (0.55f));
                    g.setFont (FontOptions (10.0f, Font::bold));
                    for (int n = 1; n <= roundToInt (v); ++n)
                        g.drawText (String (n), Rectangle<float> (out.getX(), bars.getY() + bars.getHeight() * ((float) n - 0.75f) / 8.0f, out.getWidth(), 12.0f), Justification::centred);
                }
                // the knob on the end
                g.setColour (col.brighter (0.15f));
                g.fillRoundedRectangle (Rectangle<float> (x - w * 0.5f - 3.0f, tip - 5.0f, w + 6.0f, 16.0f), 4.0f);
                g.setColour (Theme::text.withAlpha (0.85f));
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText (O::barNames[b], Rectangle<float> (x - 34.0f, bars.getY() - 24.0f, 68.0f, 14.0f), Justification::centred);
                Graphs::label (g, String (roundToInt (v)), { x - 20.0f, bars.getBottom() + 10.0f, 40.0f, 12.0f }, Justification::centred, 0.8f);
            }

            // the rotary speaker seen from above: the horn turning inside its cabinet
            SpaceUI::areaTitle (g, speaker, "SPEAKER");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (speaker.expanded (6.0f), 5.0f);
            const auto c = speaker.getCentre();
            const float rad = jmin (speaker.getWidth(), speaker.getHeight()) * 0.38f;
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, 1.5f);
            g.drawEllipse (c.x - rad * 0.55f, c.y - rad * 0.55f, rad * 1.1f, rad * 1.1f, 1.0f);
            const float ha = org.hornTurn.load() * MathConstants<float>::twoPi, da = org.drumTurn.load() * MathConstants<float>::twoPi;
            Path horn;
            horn.addPieSegment (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f, ha - 0.35f, ha + 0.35f, 0.25f);
            g.setColour (Theme::orange.withAlpha (0.85f));
            g.fillPath (horn);
            Path drum;
            drum.addPieSegment (c.x - rad * 0.55f, c.y - rad * 0.55f, rad * 1.1f, rad * 1.1f, da - 0.5f, da + 0.5f, 0.3f);
            g.setColour (Theme::accent.withAlpha (0.7f));
            g.fillPath (drum);
            Graphs::label (g, org.getInt (O::pRotary) == 0 && org.hornHz.load() < 0.05f ? String ("stopped") : String (org.hornHz.load(), 1) + " turns a second",
                           { speaker.getX(), speaker.getBottom() - 14.0f, speaker.getWidth(), 12.0f }, Justification::centred, 0.8f);
            drawHandles (g);
            String reg;
            for (int b = 0; b < O::numBars; ++b) reg << roundToInt (org.get (O::pBar1 + b)) << (b == 1 || b == 5 ? " " : "");
            drawReadouts (g, readoutArea, { { "REGISTRATION", reg }, { "PERCUSSION", org.param (O::pPerc)->getCurrentValueAsString() },
                                            { "VIBRATO / CHORUS", org.param (O::pVibrato)->getCurrentValueAsString() }, { "DRIVE", org.param (O::pDrive)->getCurrentValueAsString() },
                                            { "SPEAKER", org.param (O::pRotary)->getCurrentValueAsString() }, { "LEVEL", org.param (O::pLevel)->getCurrentValueAsString() } });
        }

    private:
        static Colour barColour (int b)
        {
            return b < 2 ? Colour (0xff8b5a3c) : (b == 4 || b == 6 || b == 7) ? Colour (0xff2b2d33) : Colour (0xffe9e6dc);   // brown, black, white: as on the instrument
        }
        float barX (int b) const       { return bars.getX() + bars.getWidth() * ((float) b + 0.5f) / (float) O::numBars; }
        float barTip (float v) const   { return bars.getY() + bars.getHeight() * jlimit (0.0f, 8.0f, v) / 8.0f; }
        O& org;
        Rectangle<float> bars, speaker, readoutArea;
    };

    //==============================================================================
    /** The electric piano: one cycle of the note played softly, medium and hard (how much it barks), and how the
        note dies away, with the tremolo riding on it. */
    class EPianoView : public HandleView
    {
    public:
        using E = pulse::EPiano;
        explicit EPianoView (E& e) : HandleView (e), ep (e)
        {
            handles.push_back ({ "Bark / bell", Theme::orange, { P (E::pBark), P (E::pBell) },
                                 [this] { return juce::Point<float> (waves.getX() + ep.get (E::pBell) * waves.getWidth(), waves.getBottom() - ep.get (E::pBark) * waves.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (E::pBell), (p.x - waves.getX()) / jmax (1.0f, waves.getWidth()));
                                     Graphs::setValue (P (E::pBark), (waves.getBottom() - p.y) / jmax (1.0f, waves.getHeight()));
                                 },
                                 [this] { return "bark " + ep.param (E::pBark)->getCurrentValueAsString() + ", bell " + ep.param (E::pBell)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Decay", Theme::accent, { P (E::pDecay) },
                                 [this] { return juce::Point<float> (xT (ep.get (E::pDecay)), fade.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (E::pDecay), (p.x - fade.getX()) / jmax (1.0f, fade.getWidth()) * span); },
                                 [this] { return ep.param (E::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (E::pDecay), ep.get (E::pDecay) * (d > 0 ? 1.08f : 0.92f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            waves = r.removeFromLeft (r.getWidth() * 0.5f).reduced (10.0f, 0.0f).withTrimmedBottom (16.0f);
            r.removeFromLeft (16.0f);
            fade = r.reduced (10.0f, 0.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, waves, "ONE CYCLE: SOFT, MEDIUM, HARD", "drag the point: up = more bark, right = more bell");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (waves.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) waves.getCentreY(), waves.getX(), waves.getRight());
            const float vels[] = { 0.3f, 0.65f, 1.0f };
            const Colour cols[] = { Theme::accent, Theme::yellow, Theme::orange };
            for (int k = 0; k < 3; ++k)
            {
                Path p;
                for (float x = waves.getX(); x <= waves.getRight(); x += 1.5f)
                {
                    const float y = waves.getCentreY() - ep.wave ((x - waves.getX()) / waves.getWidth(), 0.01f, vels[k], 220.0f) * waves.getHeight() * 0.3f * (0.5f + 0.5f * vels[k]);
                    if (x == waves.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                if (k == 2) Graphs::glowStroke (g, p, cols[k], 1.8f);
                else { g.setColour (cols[k].withAlpha (0.75f)); g.strokePath (p, PathStrokeType (1.3f)); }
            }

            SpaceUI::areaTitle (g, fade, "HOW A NOTE DIES AWAY", "middle C, with the tremolo; drag the point for the decay");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (fade.expanded (6.0f), 5.0f);
            const float decay = ep.get (E::pDecay), trem = ep.get (E::pTremolo), rate = ep.get (E::pTremRate);
            span = jmax (4.0f, decay * 1.25f);
            const double step = SpaceUI::niceStep (span, 6);
            for (double t = 0.0; t <= span; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xT ((float) t), fade.getY(), fade.getBottom());
                Graphs::label (g, String (t, 1) + " s", { xT ((float) t) - 24.0f, fade.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            Path env, top;
            env.startNewSubPath (fade.getX(), fade.getBottom());
            for (float x = fade.getX(); x <= fade.getRight(); x += 1.0f)
            {
                const float t = (x - fade.getX()) / fade.getWidth() * span;
                const float lvl = std::exp (-6.9f * t / decay) * (1.0f - trem * 0.5f * (1.0f + std::sin (t * rate * MathConstants<float>::twoPi)));
                const float y = fade.getBottom() - lvl * fade.getHeight() * 0.92f;
                env.lineTo (x, y);
                if (x == fade.getX()) top.startNewSubPath (x, y); else top.lineTo (x, y);
            }
            env.lineTo (fade.getRight(), fade.getBottom());
            env.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.4f), 0.0f, fade.getY(), Theme::accent.withAlpha (0.04f), 0.0f, fade.getBottom(), false));
            g.fillPath (env);
            Graphs::glowStroke (g, top, Theme::accent, 1.5f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "MODEL", ep.param (E::pModel)->getCurrentValueAsString() }, { "BARK", ep.param (E::pBark)->getCurrentValueAsString() },
                                            { "BELL", ep.param (E::pBell)->getCurrentValueAsString() }, { "HARDNESS", ep.param (E::pHardness)->getCurrentValueAsString() },
                                            { "DECAY", ep.param (E::pDecay)->getCurrentValueAsString() }, { "TREMOLO", ep.param (E::pTremolo)->getCurrentValueAsString() } });
        }

    private:
        float xT (float t) const { return fade.getX() + t / span * fade.getWidth(); }
        E& ep;
        Rectangle<float> waves, fade, readoutArea;
        float span = 5.0f;
    };

    //==============================================================================
    class OrganPanel : public DynamicsUI::Panel
    {
    public:
        using O = pulse::Organ;
        OrganPanel (EditViewState& evs, O& o)
        {
            display = std::make_unique<OrganView> (o);
            addAndMakeVisible (*display);
            // (the nine drawbars are the picture itself: they are dragged there, as on the instrument)
            auto* pc = strips.add (new Strip ("PERCUSSION"));
            pc->add (new ParamChoice (*o.param (O::pPerc), Theme::orange, false), 150);
            pc->add (new ParamChoice (*o.param (O::pPercDecay), Theme::orange, false), 110);
            pc->add (knob (evs, *o.param (O::pPercLevel), "Level"), 56);
            pc->add (knob (evs, *o.param (O::pClick), "Key click"), 60);
            auto* tn = strips.add (new Strip ("TONE"));
            tn->add (new ParamCombo (*o.param (O::pVibrato), false), 84);
            tn->add (knob (evs, *o.param (O::pDrive), "Drive"), 56);
            auto* sp = strips.add (new Strip ("SPEAKER"));
            sp->add (new ParamChoice (*o.param (O::pRotary), Theme::accent, false), 170);
            sp->add (knob (evs, *o.param (O::pRotaryMix), "Amount"), 58);
            sp->add (knob (evs, *o.param (O::pLevel), "Level"), 56);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class EPianoPanel : public DynamicsUI::Panel
    {
    public:
        using E = pulse::EPiano;
        EPianoPanel (EditViewState& evs, E& e)
        {
            display = std::make_unique<EPianoView> (e);
            addAndMakeVisible (*display);
            auto* sd = strips.add (new Strip ("SOUND"));
            sd->add (new ParamChoice (*e.param (E::pModel), Theme::orange, false), 120);
            sd->add (knob (evs, *e.param (E::pBell), "Bell"), 52);
            sd->add (knob (evs, *e.param (E::pBark), "Bark"), 52);
            sd->add (knob (evs, *e.param (E::pHardness), "Hardness"), 60);
            sd->add (knob (evs, *e.param (E::pTone), "Tone"), 52);
            auto* tm = strips.add (new Strip ("TIME"));
            tm->add (knob (evs, *e.param (E::pDecay), "Decay"), 54);
            tm->add (knob (evs, *e.param (E::pRelease), "Release"), 56);
            auto* tr = strips.add (new Strip ("TREMOLO"));
            tr->add (knob (evs, *e.param (E::pTremolo), "Depth"), 52);
            tr->add (knob (evs, *e.param (E::pTremRate), "Rate"), 52);
            tr->add (knob (evs, *e.param (E::pTremStereo), "Stereo"), 54);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *e.param (E::pDrive), "Drive"), 52);
            out->add (knob (evs, *e.param (E::pLevel), "Level"), 52);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
