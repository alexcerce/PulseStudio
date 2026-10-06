#pragma once

#include "TimeFx.h"
#include "AmpFxDisplay.h"

/** Windows for Resonators, Shimmer and Grain Delay. */
namespace TimeUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Resonators: the five strings as peaks on a frequency scale (each with its fainter overtones), glowing as
        they ring. The point on the first string: sideways is the note, up and down how long they ring. */
    class ResonatorsView : public HandleView
    {
    public:
        using R = pulse::Resonators;
        explicit ResonatorsView (R& r) : HandleView (r), res (r)
        {
            handles.push_back ({ "Note / decay", Theme::accent, { P (R::pNote), P (R::pDecay) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (area, res.frequency (0)), yDecay (res.get (R::pDecay))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (R::pNote), (float) roundToInt (69.0f + 12.0f * std::log2 (SpaceUI::freqForX (area, p.x) / 440.0f)));
                                     Graphs::setValue (P (R::pDecay), 0.05f * std::pow (240.0f, jlimit (0.0f, 1.0f, (area.getBottom() - p.y) / jmax (1.0f, area.getHeight() - 20.0f))));
                                 },
                                 [this] { return res.param (R::pNote)->getCurrentValueAsString() + ", " + res.param (R::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pNote), res.get (R::pNote) + (d > 0 ? 1.0f : -1.0f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.reduced (20.0f, 0.0f).withTrimmedBottom (30.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "THE FIVE STRINGS", "drag the point: sideways = the note, up = ring longer");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, area, false);
            const Colour cols[] = { Theme::accent, Theme::green, Theme::yellow, Theme::orange, Theme::red };
            const float top = yDecay (res.get (R::pDecay)), bright = res.get (R::pColour);
            // the longer a string rings, the taller and the sharper its peak
            const float sharp = 25.0f + 110.0f * (area.getBottom() - top) / jmax (1.0f, area.getHeight());
            for (int i = R::num; --i >= 0;)
            {
                const float f0 = res.frequency (i), glow = res.ringing[(size_t) i].load();
                Path p;
                p.startNewSubPath (area.getX(), area.getBottom());
                for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
                {
                    const float f = SpaceUI::freqForX (area, x);
                    float v = 0.0f;
                    for (int h = 1; h <= 8; ++h)
                    {
                        const float fh = f0 * (float) h;
                        if (fh > 20000.0f) break;
                        const float oct = std::log2 (f / fh), dull = 1.0f / (1.0f + (fh / bright) * (fh / bright));   // overtones fade with Brightness
                        v = jmax (v, dull / (float) h * (h == 1 ? 1.0f : 0.55f) / (1.0f + sharp * sharp * oct * oct));
                    }
                    p.lineTo (x, area.getBottom() - v * (area.getBottom() - top));
                }
                p.lineTo (area.getRight(), area.getBottom());
                p.closeSubPath();
                g.setColour (cols[i].withAlpha (0.10f + 0.35f * glow));
                g.fillPath (p);
                g.setColour (cols[i].withAlpha (0.55f + 0.45f * glow));
                g.strokePath (p, PathStrokeType (1.3f));
                const float x0 = SpaceUI::xForFreq (area, f0);
                const int note = roundToInt (69.0f + 12.0f * std::log2 (f0 / 440.0f));
                g.setColour (cols[i]);
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText (MidiMessage::getMidiNoteName (note, true, true, 3), Rectangle<float> (x0 - 20.0f, area.getBottom() + 6.0f + (i % 2) * 12.0f, 40.0f, 12.0f), Justification::centred);
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "NOTE", res.param (R::pNote)->getCurrentValueAsString() }, { "CHORD", res.param (R::pChord)->getCurrentValueAsString() },
                                            { "DECAY", res.param (R::pDecay)->getCurrentValueAsString() }, { "BRIGHTNESS", res.param (R::pColour)->getCurrentValueAsString() },
                                            { "DETUNE", res.param (R::pDetune)->getCurrentValueAsString() }, { "DRY/WET", res.param (R::pMix)->getCurrentValueAsString() } });
        }

    private:
        float yDecay (float seconds) const { return area.getBottom() - std::log (jlimit (0.05f, 12.0f, seconds) / 0.05f) / std::log (240.0f) * (area.getHeight() - 20.0f); }
        R& res;
        Rectangle<float> area, readoutArea;
    };

    //==============================================================================
    /** Shimmer: the tail over time as layers, each one the interval higher than the one below and as much quieter
        as Shimmer says. Drag the end of the bottom layer for the decay, the second layer up and down for the shimmer. */
    class ShimmerView : public HandleView
    {
    public:
        using S = pulse::Shimmer;
        explicit ShimmerView (S& s) : HandleView (s), sh (s)
        {
            handles.push_back ({ "Decay", Theme::accent, { P (S::pDecay) },
                                 [this] { return juce::Point<float> (xT (sh.get (S::pDecay)), rowY (0) + rowH() * 0.5f); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (S::pDecay), (p.x - area.getX()) / jmax (1.0f, area.getWidth()) * span); },
                                 [this] { return sh.param (S::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pDecay), sh.get (S::pDecay) * (d > 0 ? 1.08f : 0.92f)); } });
            handles.push_back ({ "Shimmer", Theme::yellow, { P (S::pShimmer) },
                                 [this] { return juce::Point<float> (area.getX() + 30.0f, area.getBottom() - sh.get (S::pShimmer) * area.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (S::pShimmer), (area.getBottom() - p.y) / jmax (1.0f, area.getHeight())); },
                                 [this] { return sh.param (S::pShimmer)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pShimmer), sh.get (S::pShimmer) + (d > 0 ? 0.03f : -0.03f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (70.0f).withTrimmedBottom (18.0f).withTrimmedRight (10.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "THE TAIL, LAYER ON LAYER", "drag: the blue point for the decay, the yellow one for how much climbs");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            const float decay = sh.get (S::pDecay), shimmer = sh.get (S::pShimmer), semis = sh.semitones();
            span = jmax (4.0f, decay * 1.3f);
            const double step = SpaceUI::niceStep (span, 8);
            for (double t = 0.0; t <= span; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xT ((float) t), area.getY(), area.getBottom());
                Graphs::label (g, String (t, 1) + " s", { xT ((float) t) - 24.0f, area.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            const float live = jlimit (0.3f, 1.0f, 1.0f + sh.wetDb.load() / 50.0f);
            for (int k = 0; k < rows; ++k)
            {
                // layer k has been round the pitch shifter k times: it starts later, quieter, and that much higher (or lower)
                const float level = std::pow (shimmer, (float) k), starts = k * 0.12f * decay;
                auto row = Rectangle<float> (area.getX(), rowY (k), area.getWidth(), rowH()).reduced (0.0f, 3.0f);
                const float total = semis * (float) k;
                Graphs::label (g, k == 0 ? String ("as played") : (total > 0 ? "+" : "") + String (roundToInt (total)) + " st", { area.getX() - 70.0f, row.getCentreY() - 6.0f, 62.0f, 12.0f }, Justification::centredRight, 0.7f);
                if (level < 0.003f) continue;
                const auto col = Theme::accent.interpolatedWith (Theme::yellow, (float) k / (float) (rows - 1));
                Path p;
                p.startNewSubPath (xT (starts), row.getBottom());
                for (float x = xT (starts); x <= area.getRight(); x += 2.0f)
                {
                    const float t = (x - area.getX()) / area.getWidth() * span - starts;
                    const float rise = 1.0f - std::exp (-t / (0.05f + 0.1f * k * decay)), fall = std::pow (10.0f, -3.0f * t / jmax (0.1f, decay));
                    p.lineTo (x, row.getBottom() - rise * fall * level * row.getHeight());
                }
                p.lineTo (area.getRight(), row.getBottom());
                p.closeSubPath();
                g.setColour (col.withAlpha (0.55f * live));
                g.fillPath (p);
                g.setColour (col);
                g.strokePath (p, PathStrokeType (1.2f));
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "DECAY", sh.param (S::pDecay)->getCurrentValueAsString() }, { "SHIMMER", sh.param (S::pShimmer)->getCurrentValueAsString() },
                                            { "INTERVAL", sh.param (S::pInterval)->getCurrentValueAsString() }, { "SIZE", sh.param (S::pSize)->getCurrentValueAsString() },
                                            { "DAMPING", sh.param (S::pDamping)->getCurrentValueAsString() }, { "DRY/WET", sh.param (S::pMix)->getCurrentValueAsString() } });
        }

    private:
        static constexpr int rows = 5;
        float rowH() const        { return area.getHeight() / (float) rows; }
        float rowY (int k) const  { return area.getBottom() - rowH() * (float) (k + 1); }
        float xT (float t) const  { return area.getX() + t / span * area.getWidth(); }
        S& sh;
        Rectangle<float> area, readoutArea;
        float span = 8.0f;
    };

    //==============================================================================
    /** Grain Delay: how far back the grains come from (sideways) and at what pitch (up and down). The box is
        where the grains may fall (Spray wide, Random Pitch tall); the two dots are the grains playing now. */
    class GrainView : public HandleView
    {
    public:
        using G = pulse::GrainDelay;
        explicit GrainView (G& gd) : HandleView (gd), gr (gd)
        {
            handles.push_back ({ "Time / pitch", Theme::accent, { P (G::pTime), P (G::pPitch) },
                                 [this] { return juce::Point<float> (xMs (gr.timeMs()), ySemi (gr.get (G::pPitch))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     if (gr.getInt (G::pSync) == 0) Graphs::setValue (P (G::pTime), msAt (p.x));
                                     const float st = semiAt (p.y);
                                     Graphs::setValue (P (G::pPitch), std::abs (st - std::round (st)) < 0.25f ? std::round (st) : st);   // catches on whole semitones
                                 },
                                 [this] { return pulse::fmt::ms (gr.timeMs()) + ", " + gr.param (G::pPitch)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (G::pPitch), std::round (gr.get (G::pPitch)) + (d > 0 ? 1.0f : -1.0f)); } });
            handles.push_back ({ "Spray / random pitch", Theme::yellow, { P (G::pSpray), P (G::pRandomPitch) },
                                 [this] { return juce::Point<float> (xMs (gr.timeMs() + gr.get (G::pSpray)), ySemi (gr.get (G::pPitch) + gr.get (G::pRandomPitch) * 12.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (G::pSpray), jmax (0.0f, msAt (p.x) - gr.timeMs()));
                                     Graphs::setValue (P (G::pRandomPitch), jmax (0.0f, semiAt (p.y) - gr.get (G::pPitch)) / 12.0f);
                                 },
                                 [this] { return "spray " + gr.param (G::pSpray)->getCurrentValueAsString() + ", pitch +/- " + String (gr.get (G::pRandomPitch) * 12.0f, 1) + " st"; }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (40.0f).withTrimmedBottom (18.0f).withTrimmedRight (10.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "WHERE THE GRAINS COME FROM", "sideways = how long ago, up and down = pitch   blue point: time / pitch, yellow: spray / random pitch");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            const float time = gr.timeMs(), spray = gr.get (G::pSpray), pitch = gr.get (G::pPitch), rp = gr.get (G::pRandomPitch) * 12.0f;
            span = jmax (500.0f, (time + spray) * 1.3f);
            const double step = SpaceUI::niceStep (span, 8);
            for (double t = 0.0; t <= span; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xMs ((float) t), area.getY(), area.getBottom());
                Graphs::label (g, pulse::fmt::ms ((float) t), { xMs ((float) t) - 24.0f, area.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            for (int st = -24; st <= 24; st += 12)
            {
                g.setColour (Theme::ink.withAlpha (st == 0 ? 0.14f : 0.05f));
                g.drawHorizontalLine ((int) ySemi ((float) st), area.getX(), area.getRight());
                Graphs::label (g, (st > 0 ? "+" : "") + String (st), { area.getX() - 38.0f, ySemi ((float) st) - 6.0f, 32.0f, 12.0f }, Justification::centredRight, 0.6f);
            }
            // where grains may fall
            auto box = Rectangle<float>::leftTopRightBottom (xMs (time), ySemi (pitch + rp), xMs (time + spray), ySemi (pitch - rp)).expanded (3.0f);
            g.setColour (Theme::yellow.withAlpha (0.14f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (Theme::yellow.withAlpha (0.5f));
            g.drawRoundedRectangle (box, 3.0f, 1.0f);
            // the echoes of the feedback, each fainter and (if re-pitched) further up or down
            const float fbk = gr.get (G::pFeedback);
            for (int k = 2; k <= 6; ++k)
            {
                const float lv = std::pow (fbk, (float) (k - 1));
                if (lv < 0.03f || time * k > span) break;
                g.setColour (Theme::accent.withAlpha (0.5f * lv));
                g.fillEllipse (xMs (time * k) - 4.0f, ySemi (jlimit (-24.0f, 24.0f, pitch * k)) - 4.0f, 8.0f, 8.0f);
            }
            for (auto& s : gr.shown)
            {
                const float lv = s.level.load();
                g.setColour (Theme::ink.withAlpha (0.25f + 0.75f * lv));
                const float rad = 3.0f + 4.0f * lv;
                g.fillEllipse (xMs (s.ms.load()) - rad, ySemi (jlimit (-24.0f, 24.0f, s.semis.load())) - rad, rad * 2.0f, rad * 2.0f);
            }
            drawHandles (g);
            const int sync = gr.getInt (G::pSync);
            drawReadouts (g, readoutArea, { { "TIME", sync > 0 ? G::syncNames[sync] + "  (" + pulse::fmt::ms (time) + ")" : pulse::fmt::ms (time) }, { "PITCH", gr.param (G::pPitch)->getCurrentValueAsString() },
                                            { "GRAIN", gr.param (G::pGrain)->getCurrentValueAsString() }, { "SPRAY", gr.param (G::pSpray)->getCurrentValueAsString() },
                                            { "FEEDBACK", gr.param (G::pFeedback)->getCurrentValueAsString() }, { "DRY/WET", gr.param (G::pMix)->getCurrentValueAsString() } });
        }

    private:
        float xMs (float ms) const    { return area.getX() + jlimit (0.0f, 1.0f, ms / span) * area.getWidth(); }
        float msAt (float x) const    { return (x - area.getX()) / jmax (1.0f, area.getWidth()) * span; }
        float ySemi (float st) const  { return area.getCentreY() - jlimit (-1.0f, 1.0f, st / 26.0f) * area.getHeight() * 0.5f; }
        float semiAt (float y) const  { return (area.getCentreY() - y) / jmax (1.0f, area.getHeight() * 0.5f) * 26.0f; }
        G& gr;
        Rectangle<float> area, readoutArea;
        float span = 500.0f;
    };

    //==============================================================================
    class ResonatorsPanel : public DynamicsUI::Panel
    {
    public:
        using R = pulse::Resonators;
        ResonatorsPanel (EditViewState& evs, R& r)
        {
            display = std::make_unique<ResonatorsView> (r);
            addAndMakeVisible (*display);
            auto* tu = strips.add (new Strip ("TUNING"));
            tu->add (knob (evs, *r.param (R::pNote), "Note"), 58);
            tu->add (new ParamCombo (*r.param (R::pChord), false), 116);
            tu->add (knob (evs, *r.param (R::pDetune), "Detune"), 58);
            auto* rg = strips.add (new Strip ("RING"));
            rg->add (knob (evs, *r.param (R::pDecay), "Decay"), 58);
            rg->add (knob (evs, *r.param (R::pColour), "Brightness"), 66);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *r.param (R::pInput), "Input"), 56);
            out->add (knob (evs, *r.param (R::pWidth), "Width"), 56);
            out->add (knob (evs, *r.param (R::pMix), "Dry/Wet"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class ShimmerPanel : public DynamicsUI::Panel
    {
    public:
        using S = pulse::Shimmer;
        ShimmerPanel (EditViewState& evs, S& s)
        {
            display = std::make_unique<ShimmerView> (s);
            addAndMakeVisible (*display);
            auto* sm = strips.add (new Strip ("SHIMMER"));
            sm->add (knob (evs, *s.param (S::pShimmer), "Shimmer"), 60);
            sm->add (new ParamCombo (*s.param (S::pInterval), false), 140);
            auto* tl = strips.add (new Strip ("TAIL"));
            tl->add (knob (evs, *s.param (S::pDecay), "Decay"), 58);
            tl->add (knob (evs, *s.param (S::pSize), "Size"), 56);
            tl->add (knob (evs, *s.param (S::pDamping), "Damping"), 60);
            tl->add (knob (evs, *s.param (S::pLowCut), "Low cut"), 58);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *s.param (S::pWidth), "Width"), 56);
            out->add (knob (evs, *s.param (S::pMix), "Dry/Wet"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class GrainPanel : public DynamicsUI::Panel
    {
    public:
        using G = pulse::GrainDelay;
        GrainPanel (EditViewState& evs, G& gd)
        {
            display = std::make_unique<GrainView> (gd);
            addAndMakeVisible (*display);
            auto* dl = strips.add (new Strip ("DELAY"));
            dl->add (new ParamCombo (*gd.param (G::pSync), false), 104);
            dl->add (knob (evs, *gd.param (G::pTime), "Time"), 58);
            dl->add (knob (evs, *gd.param (G::pFeedback), "Feedback"), 60);
            auto* gn = strips.add (new Strip ("GRAINS"));
            gn->add (knob (evs, *gd.param (G::pPitch), "Pitch"), 58);
            gn->add (knob (evs, *gd.param (G::pGrain), "Size"), 56);
            gn->add (knob (evs, *gd.param (G::pSpray), "Spray"), 56);
            gn->add (knob (evs, *gd.param (G::pRandomPitch), "Random pitch"), 78);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *gd.param (G::pMix), "Dry/Wet"), 60);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
