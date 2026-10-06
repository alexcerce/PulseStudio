#pragma once

#include "ColourFx.h"
#include "ModFxDisplay.h"

/** Windows for the Frequency Shifter, Redux and the Exciter. */
namespace ColourUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Frequency Shifter: a scale of Hz with the shift as a point (and the LFO's reach as a band); under it, the
        harmonics of a note before (grey) and after (colour), which shows why shifted sounds turn bell-like. */
    class ShifterView : public HandleView
    {
    public:
        using S = pulse::FreqShifter;
        explicit ShifterView (S& s) : HandleView (s), sh (s)
        {
            handles.push_back ({ "Shift", Theme::accent, { P (S::pShift), P (S::pFine) },
                                 [this] { return juce::Point<float> (xHz (sh.shiftHz()), scale.getCentreY()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (S::pShift), hzAt (p.x) - sh.get (S::pFine)); },
                                 [this] { return sh.param (S::pShift)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (S::pFine), sh.get (S::pFine) + (d > 0 ? 0.5f : -0.5f)); } });
            handles.push_back ({ "LFO amount", Theme::yellow, { P (S::pLfoAmount) },
                                 [this] { return juce::Point<float> (xHz (sh.shiftHz() + sh.get (S::pLfoAmount)), scale.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (S::pLfoAmount), jmax (0.0f, hzAt (p.x) - sh.shiftHz())); },
                                 [this] { return sh.param (S::pLfoAmount)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            scale = r.removeFromTop (r.getHeight() * 0.3f).reduced (20.0f, 0.0f).withTrimmedBottom (18.0f);
            r.removeFromTop (30.0f);
            lines = r.reduced (20.0f, 0.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, scale, "SHIFT", "drag the point; scroll over it for fine steps; the yellow point is how far the LFO moves it");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (scale.expanded (6.0f), 5.0f);
            for (int hz = -2000; hz <= 2000; hz += 500)
            {
                g.setColour (Theme::ink.withAlpha (hz == 0 ? 0.25f : 0.07f));
                g.drawVerticalLine ((int) xHz ((float) hz), scale.getY(), scale.getBottom());
                Graphs::label (g, (hz > 0 ? "+" : "") + String (hz), { xHz ((float) hz) - 24.0f, scale.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            const float shift = sh.shiftHz(), lfo = sh.get (S::pLfoAmount), now = sh.shiftNow.load();
            if (lfo > 0.05f)
            {
                g.setColour (Theme::yellow.withAlpha (0.12f));
                g.fillRect (Rectangle<float> (xHz (shift - lfo), scale.getY(), xHz (shift + lfo) - xHz (shift - lfo), scale.getHeight()));
            }
            g.setColour (Theme::accent.withAlpha (0.25f));
            g.fillRect (Rectangle<float> (jmin (xHz (0.0f), xHz (shift)), scale.getCentreY() - 3.0f, std::abs (xHz (shift) - xHz (0.0f)), 6.0f));
            g.setColour (Theme::ink);
            g.fillEllipse (xHz (now) - 3.0f, scale.getY() + 4.0f, 6.0f, 6.0f);

            SpaceUI::areaTitle (g, lines, "THE HARMONICS OF A NOTE (A, 110 Hz)", "before grey, after in colour");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (lines.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, lines, true);
            const bool ring = sh.getInt (S::pMode) == 1;
            for (int h = 1; h <= 16; ++h)
            {
                const float f = 110.0f * (float) h, tall = lines.getHeight() * (0.9f / std::sqrt ((float) h));
                g.setColour (Theme::ink.withAlpha (0.22f));
                g.fillRect (Rectangle<float> (SpaceUI::xForFreq (lines, f) - 1.0f, lines.getBottom() - tall, 2.0f, tall));
                for (int side = 0; side < (ring ? 2 : 1); ++side)
                {
                    const float to = std::abs (f + (side == 0 ? now : -now));   // below zero it folds back up
                    if (to < 20.0f || to > 20000.0f) continue;
                    g.setColour ((side == 0 ? Theme::accent : Theme::orange).withAlpha (0.9f));
                    g.fillRect (Rectangle<float> (SpaceUI::xForFreq (lines, to) - 1.5f, lines.getBottom() - tall * (ring ? 0.7f : 1.0f), 3.0f, tall * (ring ? 0.7f : 1.0f)));
                }
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "MODE", sh.param (S::pMode)->getCurrentValueAsString() }, { "SHIFT", sh.param (S::pShift)->getCurrentValueAsString() },
                                            { "FINE", sh.param (S::pFine)->getCurrentValueAsString() }, { "NOW", (now > 0.05f ? "+" : "") + String (now, 1) + " Hz" },
                                            { "FEEDBACK", sh.param (S::pFeedback)->getCurrentValueAsString() }, { "DRY/WET", sh.param (S::pMix)->getCurrentValueAsString() } });
        }

    private:
        float xHz (float hz) const { return scale.getCentreX() + jlimit (-1.0f, 1.0f, hz / 2000.0f) * scale.getWidth() * 0.5f; }
        float hzAt (float x) const { return (x - scale.getCentreX()) / jmax (1.0f, scale.getWidth() * 0.5f) * 2000.0f; }
        S& sh;
        Rectangle<float> scale, lines, readoutArea;
    };

    //==============================================================================
    /** Redux: a sine before (grey) and after (the staircase). One point: sideways is the sample rate, up and down the bits. */
    class ReduxView : public HandleView
    {
    public:
        using R = pulse::Redux;
        explicit ReduxView (R& r) : HandleView (r), rx (r)
        {
            handles.push_back ({ "Sample rate / bits", Theme::orange, { P (R::pRate), P (R::pBits) },
                                 [this] { return juce::Point<float> (area.getX() + rateTo01 (rx.get (R::pRate)) * area.getWidth(), area.getBottom() - (rx.get (R::pBits) - 1.0f) / 15.0f * area.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (R::pRate), 200.0f * std::pow (240.0f, jlimit (0.0f, 1.0f, (p.x - area.getX()) / jmax (1.0f, area.getWidth()))));
                                     Graphs::setValue (P (R::pBits), 1.0f + (area.getBottom() - p.y) / jmax (1.0f, area.getHeight()) * 15.0f);
                                 },
                                 [this] { return rx.param (R::pRate)->getCurrentValueAsString() + ", " + rx.param (R::pBits)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pBits), rx.get (R::pBits) + (d > 0 ? 0.5f : -0.5f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.reduced (20.0f, 0.0f).withTrimmedBottom (8.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "WHAT IT DOES TO A WAVE (5 ms of a 400 Hz sine)", "drag the point: left = lower sample rate, down = fewer bits");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
            const float rate = rx.get (R::pRate), bits = rx.get (R::pBits), mix = rx.get (R::pMix);
            const double window = 0.005, every = 1.0 / rate;
            auto wave = [] (double t) { return 0.85f * (float) std::sin (t * 400.0 * MathConstants<double>::twoPi); };
            auto yFor = [&] (float v) { return area.getCentreY() - v * area.getHeight() * 0.48f; };
            Path before, after;
            for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
            {
                const double t = (x - area.getX()) / area.getWidth() * window;
                const float in = wave (t), held = pulse::Redux::quantise (wave (std::floor (t / every) * every), bits);
                const float y0 = yFor (in), y1 = yFor (in + (held - in) * mix);
                if (x == area.getX()) { before.startNewSubPath (x, y0); after.startNewSubPath (x, y1); }
                else { before.lineTo (x, y0); after.lineTo (x, y1); }
            }
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.strokePath (before, PathStrokeType (1.2f));
            Graphs::glowStroke (g, after, Theme::orange, 1.8f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "SAMPLE RATE", rx.param (R::pRate)->getCurrentValueAsString() }, { "BITS", rx.param (R::pBits)->getCurrentValueAsString() },
                                            { "STEPS OF LOUDNESS", String (roundToInt (std::pow (2.0f, bits))) }, { "JITTER", rx.param (R::pJitter)->getCurrentValueAsString() },
                                            { "TONE", rx.param (R::pTone)->getCurrentValueAsString() }, { "DRY/WET", rx.param (R::pMix)->getCurrentValueAsString() } });
        }

    private:
        static float rateTo01 (float rate) { return std::log (jlimit (200.0f, 48000.0f, rate) / 200.0f) / std::log (240.0f); }
        R& rx;
        Rectangle<float> area, readoutArea;
    };

    //==============================================================================
    /** Exciter: the sound as a level line, and above it what is added: nothing below Frequency, harmonics above.
        One point: sideways is the frequency, up and down the amount. */
    class ExciterView : public HandleView
    {
    public:
        using E = pulse::Exciter;
        explicit ExciterView (E& e) : HandleView (e), ex (e)
        {
            handles.push_back ({ "Frequency / amount", Theme::yellow, { P (E::pFreq), P (E::pAmount) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (area, ex.get (E::pFreq)), yAmount (ex.get (E::pAmount))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (E::pFreq), SpaceUI::freqForX (area, p.x));
                                     Graphs::setValue (P (E::pAmount), (base() - p.y) / jmax (1.0f, base() - area.getY() - 10.0f));
                                 },
                                 [this] { return ex.param (E::pFreq)->getCurrentValueAsString() + ", " + ex.param (E::pAmount)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (E::pAmount), ex.get (E::pAmount) + (d > 0 ? 0.03f : -0.03f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            shapeArea = r.removeFromRight (jmin (r.getHeight() - 16.0f, r.getWidth() * 0.26f)).withTrimmedBottom (16.0f);
            r.removeFromRight (26.0f);
            area = r.withTrimmedLeft (10.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "WHAT IS ADDED", "drag the point: sideways = from which frequency up, up and down = how much");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, area, true);
            const float freq = ex.get (E::pFreq), amount = ex.get (E::pAmount);
            // the sound itself
            g.setColour (Theme::ink.withAlpha (0.10f));
            g.fillRect (Rectangle<float> (area.getX(), base(), area.getWidth(), area.getBottom() - base()));
            g.setColour (Theme::ink.withAlpha (0.35f));
            g.drawHorizontalLine ((int) base(), area.getX(), area.getRight());
            // what is added: harmonics start an octave above what they are made from
            pulse::Biquad hp;
            hp.set (pulse::Biquad::Type::lowCut, 96000.0, freq * 1.6, 0.707, 0.0);
            const float glow = jlimit (0.25f, 1.0f, 1.0f + ex.addedDb.load() / 50.0f);
            Path fill, line;
            fill.startNewSubPath (area.getX(), base());
            for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
            {
                const float y = base() - (float) hp.magnitude (SpaceUI::freqForX (area, x), 96000.0) * amount * (base() - area.getY() - 10.0f);
                fill.lineTo (x, y);
                if (x == area.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (area.getRight(), base());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::yellow.withAlpha (0.5f * glow), 0.0f, area.getY(), Theme::yellow.withAlpha (0.08f), 0.0f, base(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, Theme::yellow, 1.8f);
            g.setColour (Theme::yellow.withAlpha (0.4f));
            g.drawVerticalLine ((int) SpaceUI::xForFreq (area, freq), base(), area.getBottom());

            // how the harmonics are made
            SpaceUI::areaTitle (g, shapeArea, "HOW THEY ARE MADE");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (shapeArea.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) shapeArea.getCentreY(), shapeArea.getX(), shapeArea.getRight());
            g.drawVerticalLine ((int) shapeArea.getCentreX(), shapeArea.getY(), shapeArea.getBottom());
            const float drive = Decibels::decibelsToGain (ex.get (E::pDrive) * 0.5f), colour = ex.get (E::pColour);
            Path sp;
            for (float x = shapeArea.getX(); x <= shapeArea.getRight(); x += 1.0f)
            {
                const float in = (x - shapeArea.getCentreX()) / (shapeArea.getWidth() * 0.5f);
                const float y = shapeArea.getCentreY() - pulse::Exciter::shape (in * drive, colour) * shapeArea.getHeight() * 0.46f;
                if (x == shapeArea.getX()) sp.startNewSubPath (x, y); else sp.lineTo (x, y);
            }
            Graphs::glowStroke (g, sp, Theme::orange, 1.8f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "FREQUENCY", ex.param (E::pFreq)->getCurrentValueAsString() }, { "AMOUNT", ex.param (E::pAmount)->getCurrentValueAsString() },
                                            { "DRIVE", ex.param (E::pDrive)->getCurrentValueAsString() }, { "HARMONICS", ex.param (E::pColour)->getCurrentValueAsString() },
                                            { "ADDING", ex.addedDb.load() < -90.0f ? String ("-") : String (ex.addedDb.load(), 1) + " dB" }, { "OUTPUT", ex.param (E::pOutput)->getCurrentValueAsString() } });
        }

    private:
        float base() const               { return area.getY() + area.getHeight() * 0.62f; }
        float yAmount (float a) const     { return base() - a * (base() - area.getY() - 10.0f); }
        E& ex;
        Rectangle<float> area, shapeArea, readoutArea;
    };

    //==============================================================================
    class ShifterPanel : public DynamicsUI::Panel
    {
    public:
        using S = pulse::FreqShifter;
        ShifterPanel (EditViewState& evs, S& s)
        {
            display = std::make_unique<ShifterView> (s);
            addAndMakeVisible (*display);
            auto* md = strips.add (new Strip ("MODE"));
            md->add (new ParamChoice (*s.param (S::pMode), Theme::accent, false), 170);
            auto* sf = strips.add (new Strip ("SHIFT"));
            sf->add (knob (evs, *s.param (S::pShift), "Shift"), 62);
            sf->add (knob (evs, *s.param (S::pFine), "Fine"), 60);
            auto* mv = strips.add (new Strip ("MOVEMENT"));
            mv->add (knob (evs, *s.param (S::pLfoAmount), "LFO"), 60);
            mv->add (knob (evs, *s.param (S::pLfoRate), "Rate"), 60);
            mv->add (knob (evs, *s.param (S::pSpread), "Stereo"), 60);
            auto* sp = strips.add (new Strip ("SPIRAL"));
            sp->add (knob (evs, *s.param (S::pFeedback), "Feedback"), 62);
            sp->add (knob (evs, *s.param (S::pDelay), "Delay"), 60);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *s.param (S::pMix), "Dry/Wet"), 62);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class ReduxPanel : public DynamicsUI::Panel
    {
    public:
        using R = pulse::Redux;
        ReduxPanel (EditViewState& evs, R& r)
        {
            display = std::make_unique<ReduxView> (r);
            addAndMakeVisible (*display);
            auto* sr = strips.add (new Strip ("SAMPLE RATE"));
            sr->add (knob (evs, *r.param (R::pRate), "Rate"), 62);
            sr->add (knob (evs, *r.param (R::pJitter), "Jitter"), 60);
            sr->add (new ParamToggle (*r.param (R::pSoft), "Soft", Theme::orange), 60);
            auto* bt = strips.add (new Strip ("BITS"));
            bt->add (knob (evs, *r.param (R::pBits), "Bits"), 62);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *r.param (R::pTone), "Tone"), 60);
            out->add (knob (evs, *r.param (R::pMix), "Dry/Wet"), 62);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class ExciterPanel : public DynamicsUI::Panel
    {
    public:
        using E = pulse::Exciter;
        ExciterPanel (EditViewState& evs, E& e)
        {
            display = std::make_unique<ExciterView> (e);
            addAndMakeVisible (*display);
            auto* hm = strips.add (new Strip ("HARMONICS"));
            hm->add (knob (evs, *e.param (E::pFreq), "Frequency"), 66);
            hm->add (knob (evs, *e.param (E::pDrive), "Drive"), 60);
            hm->add (knob (evs, *e.param (E::pColour), "Odd / even"), 70);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *e.param (E::pAmount), "Amount"), 62);
            out->add (knob (evs, *e.param (E::pOutput), "Output"), 62);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
