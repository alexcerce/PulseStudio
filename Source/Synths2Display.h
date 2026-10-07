#pragma once

#include "Synths2.h"
#include "KeysDisplay.h"

/** Windows for Pulse Analog and Pulse String. */
namespace Synths2UI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Pulse Analog: the wave the two oscillators make together, and the filter as a curve with the cutoff and
        resonance as its point (the marker is where the envelope and LFO have the cutoff right now). */
    class AnalogView : public HandleView
    {
    public:
        using A = pulse::Analog;
        explicit AnalogView (A& a) : HandleView (a), an (a)
        {
            handles.push_back ({ "Mix / pulse width", Theme::accent, { P (A::pMix), P (A::pPw) },
                                 [this] { return juce::Point<float> (wave.getX() + an.get (A::pMix) * wave.getWidth(), wave.getBottom() - (an.get (A::pPw) - 0.05f) / 0.9f * wave.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (A::pMix), (p.x - wave.getX()) / jmax (1.0f, wave.getWidth()));
                                     Graphs::setValue (P (A::pPw), 0.05f + (wave.getBottom() - p.y) / jmax (1.0f, wave.getHeight()) * 0.9f);
                                 },
                                 [this] { return "mix " + an.param (A::pMix)->getCurrentValueAsString() + ", width " + an.param (A::pPw)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Cutoff / resonance", Theme::orange, { P (A::pCutoff), P (A::pReso) },
                                 [this] { return juce::Point<float> (SpaceUI::xForFreq (filt, an.get (A::pCutoff)), yDb (-3.0f + an.get (A::pReso) * 21.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (A::pCutoff), SpaceUI::freqForX (filt, p.x));
                                     Graphs::setValue (P (A::pReso), (dbAt (p.y) + 3.0f) / 21.0f);
                                 },
                                 [this] { return an.param (A::pCutoff)->getCurrentValueAsString() + ", resonance " + an.param (A::pReso)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (A::pReso), an.get (A::pReso) + (d > 0 ? 0.04f : -0.04f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (14.0f);
            r.removeFromTop (20.0f);
            wave = r.removeFromLeft (r.getWidth() * 0.42f).reduced (8.0f, 0.0f).withTrimmedBottom (6.0f);
            r.removeFromLeft (20.0f);
            filt = r.withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, wave, "THE TWO OSCILLATORS TOGETHER", "drag: sideways = mix, up = pulse width");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (wave.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) wave.getCentreY(), wave.getX(), wave.getRight());
            const int w1 = an.getInt (A::pWave1), w2 = an.getInt (A::pWave2);
            const float pw = an.get (A::pPw), mix = an.get (A::pMix);
            const double r1 = std::pow (2.0, an.getInt (A::pOct1)), r2 = std::pow (2.0, an.getInt (A::pOct2) + an.getInt (A::pSemi2) / 12.0);
            const bool sync = an.getInt (A::pSync) > 0;
            Path p;
            for (float x = wave.getX(); x <= wave.getRight(); x += 1.0f)
            {
                const double t = (x - wave.getX()) / wave.getWidth() * 2.0;   // two cycles of the note
                const double ph1 = t * r1, ph2 = sync ? (ph1 - std::floor (ph1)) * r2 / r1 : t * r2;
                const float v = A::shape (w1, ph1, pw) * (1.0f - mix) + A::shape (w2, ph2, pw) * mix;
                const float y = wave.getCentreY() - v * wave.getHeight() * 0.42f;
                if (x == wave.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Graphs::glowStroke (g, p, Theme::accent, 1.8f);

            SpaceUI::areaTitle (g, filt, "FILTER", "drag the point: cutoff and resonance   the line marks where it is right now");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (filt.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, filt, true);
            for (float db = 12.0f; db >= -48.0f; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.12f : 0.04f));
                g.drawHorizontalLine ((int) yDb (db), filt.getX(), filt.getRight());
                Graphs::label (g, String ((int) db), { filt.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            auto curve = [&] (double fc, bool closed)
            {
                Path c;
                if (closed) c.startNewSubPath (filt.getX(), filt.getBottom());
                for (float x = filt.getX(); x <= filt.getRight(); x += 2.0f)
                {
                    const float y = yDb (Decibels::gainToDecibels (an.filterResponse (SpaceUI::freqForX (filt, x), fc), -80.0f));
                    if (x == filt.getX() && ! closed) c.startNewSubPath (x, y); else c.lineTo (x, y);
                }
                if (closed) { c.lineTo (filt.getRight(), filt.getBottom()); c.closeSubPath(); }
                return c;
            };
            const float cutoff = an.get (A::pCutoff), env = an.get (A::pEnvAmt);
            // how far the envelope will carry the cutoff
            const float swept = jlimit (20.0f, 20000.0f, cutoff * std::pow (2.0f, env * 7.0f));
            g.setColour (Theme::orange.withAlpha (0.25f));
            g.strokePath (curve (swept, false), PathStrokeType (1.2f));
            g.setGradientFill (ColourGradient (Theme::orange.withAlpha (0.3f), 0.0f, filt.getY(), Theme::orange.withAlpha (0.03f), 0.0f, filt.getBottom(), false));
            g.fillPath (curve (cutoff, true));
            Graphs::glowStroke (g, curve (cutoff, false), Theme::orange, 1.8f);
            g.setColour (Theme::ink.withAlpha (0.6f));
            g.drawVerticalLine ((int) SpaceUI::xForFreq (filt, an.cutoffNow.load()), filt.getY(), filt.getBottom());
            drawHandles (g);
        }

    private:
        float yDb (float db) const  { return filt.getY() + jlimit (0.0f, 1.0f, (24.0f - db) / 78.0f) * filt.getHeight(); }
        float dbAt (float y) const  { return 24.0f - (y - filt.getY()) / jmax (1.0f, filt.getHeight()) * 78.0f; }
        A& an;
        Rectangle<float> wave, filt;
    };

    /** Pulse Analog's window: the picture and the two envelopes on top, every control in two rows underneath. */
    class AnalogPanel : public Component
    {
    public:
        using A = pulse::Analog;
        AnalogPanel (EditViewState& evs, A& a) : view (a)
        {
            addAndMakeVisible (view);
            using EG = Graphs::EnvelopeGraph;
            envs = std::make_unique<EG> (std::vector<EG::Env> { { a.param (A::pA), a.param (A::pD), a.param (A::pS), a.param (A::pR), Theme::green, "AMP" },
                                                                { a.param (A::pFA), a.param (A::pFD), a.param (A::pFS), a.param (A::pFR), Theme::orange, "FILTER" } });
            addAndMakeVisible (*envs);
            auto strip = [this] (int row, const char* name) { auto* s = (row == 0 ? row1 : row2).add (new Strip (name)); addAndMakeVisible (s); return s; };
            auto* o1 = strip (0, "OSC 1");
            o1->add (new ParamCombo (*a.param (A::pWave1), false), 86);
            o1->add (knob (evs, *a.param (A::pOct1), "Octave"), 47);
            o1->add (knob (evs, *a.param (A::pPw), "Width"), 47);
            auto* o2 = strip (0, "OSC 2");
            o2->add (new ParamCombo (*a.param (A::pWave2), false), 86);
            o2->add (knob (evs, *a.param (A::pOct2), "Octave"), 47);
            o2->add (knob (evs, *a.param (A::pSemi2), "Semi"), 46);
            o2->add (knob (evs, *a.param (A::pFine2), "Fine"), 46);
            o2->add (new ParamToggle (*a.param (A::pSync), "Sync", Theme::accent), 48);
            auto* mx = strip (0, "MIX");
            mx->add (knob (evs, *a.param (A::pMix), "1 / 2"), 46);
            mx->add (knob (evs, *a.param (A::pSub), "Sub"), 45);
            mx->add (knob (evs, *a.param (A::pNoise), "Noise"), 45);
            mx->add (knob (evs, *a.param (A::pLevel), "Level"), 45);
            auto* fl = strip (0, "FILTER");
            fl->add (knob (evs, *a.param (A::pCutoff), "Cutoff"), 48);
            fl->add (knob (evs, *a.param (A::pReso), "Reso"), 46);
            fl->add (knob (evs, *a.param (A::pEnvAmt), "Env"), 45);
            fl->add (knob (evs, *a.param (A::pKeyTrack), "Key"), 45);
            fl->add (knob (evs, *a.param (A::pFilterDrive), "Drive"), 45);
            auto* fe = strip (1, "FILTER ENVELOPE");
            fe->add (knob (evs, *a.param (A::pFA), "Attack"), 44); fe->add (knob (evs, *a.param (A::pFD), "Decay"), 44);
            fe->add (knob (evs, *a.param (A::pFS), "Sustain"), 44); fe->add (knob (evs, *a.param (A::pFR), "Release"), 44);
            auto* ae = strip (1, "AMP ENVELOPE");
            ae->add (knob (evs, *a.param (A::pA), "Attack"), 44); ae->add (knob (evs, *a.param (A::pD), "Decay"), 44);
            ae->add (knob (evs, *a.param (A::pS), "Sustain"), 44); ae->add (knob (evs, *a.param (A::pR), "Release"), 44);
            auto* lf = strip (1, "LFO");
            lf->add (knob (evs, *a.param (A::pLfoRate), "Rate"), 46); lf->add (knob (evs, *a.param (A::pLfoPitch), "Pitch"), 46);
            lf->add (knob (evs, *a.param (A::pLfoCutoff), "Cutoff"), 46); lf->add (knob (evs, *a.param (A::pLfoPw), "Width"), 46);
            auto* vc = strip (1, "VOICE");
            { auto* mono = new ParamToggle (*a.param (A::pMode), "Mono", Theme::accent);
              mono->setTooltip ("Mono: one note at a time (a bass line, a lead). Off: chords");
              vc->add (mono, 52); }
            vc->add (knob (evs, *a.param (A::pGlide), "Glide"), 45);
            { auto* tied = new ParamToggle (*a.param (A::pGlideMode), "Tied", Theme::accent);
              tied->setTooltip ("Tied notes: in mono mode a note that begins before the last one has ended is not struck again, the pitch slides to it; a note on its own does not glide. Off: every note glides");
              vc->add (tied, 50); }
            vc->add (knob (evs, *a.param (A::pAccent), "Accent"), 46);
            vc->add (knob (evs, *a.param (A::pUnison), "Unison"), 46);
            vc->add (knob (evs, *a.param (A::pDrift), "Drift"), 45);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto bar = getLocalBounds().removeFromBottom (rowsHeight()).toFloat();
            g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, bar.getY(), Pro::barBot(), 0.0f, bar.getBottom(), false));
            g.fillRect (bar);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawHorizontalLine ((int) bar.getY(), 0.0f, (float) getWidth());
            g.drawHorizontalLine ((int) bar.getCentreY(), 10.0f, (float) getWidth() - 10.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bottom = r.removeFromBottom (rowsHeight()).reduced (10, 4);
            auto placeRow = [] (Rectangle<int> area, OwnedArray<Strip>& row)
            {
                int total = 0;
                for (auto* s : row) total += s->idealWidth() + 8;
                const float scale = total > area.getWidth() ? (float) area.getWidth() / (float) total : 1.0f;
                for (auto* s : row) { s->setBounds (area.removeFromLeft ((int) (s->idealWidth() * scale))); area.removeFromLeft (8); }
            };
            placeRow (bottom.removeFromTop (bottom.getHeight() / 2), row1);
            placeRow (bottom, row2);
            envs->setBounds (r.removeFromRight (jlimit (240, 360, r.getWidth() * 30 / 100)).reduced (8, 14));
            view.setBounds (r);
        }

    private:
        int rowsHeight() const { return 216; }
        AnalogView view;
        std::unique_ptr<Graphs::EnvelopeGraph> envs;
        OwnedArray<Strip> row1, row2;
    };

    //==============================================================================
    /** Pulse String: the string with the place it is set going (drag it along the string, and up for a brighter
        start), and its harmonics: how loud each one starts and how long each one rings. */
    class StringView : public HandleView
    {
    public:
        using S = pulse::StringModel;
        explicit StringView (S& s) : HandleView (s), sm (s)
        {
            handles.push_back ({ "Position / brightness", Theme::orange, { P (S::pPosition), P (S::pBrightness) },
                                 [this] { return juce::Point<float> (line.getX() + sm.get (S::pPosition) * line.getWidth(), line.getCentreY() - (0.15f + 0.8f * sm.get (S::pBrightness)) * line.getHeight() * 0.5f); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (S::pPosition), (p.x - line.getX()) / jmax (1.0f, line.getWidth()));
                                     Graphs::setValue (P (S::pBrightness), ((line.getCentreY() - p.y) / jmax (1.0f, line.getHeight() * 0.5f) - 0.15f) / 0.8f);
                                 },
                                 [this] { return sm.param (S::pPosition)->getCurrentValueAsString() + ", brightness " + sm.param (S::pBrightness)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Decay / damping", Theme::accent, { P (S::pDecay), P (S::pDamping) },
                                 [this] { return juce::Point<float> (ring.getX() + sm.get (S::pDamping) * ring.getWidth(), ring.getBottom() - decay01 (sm.get (S::pDecay)) * ring.getHeight()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (S::pDamping), (p.x - ring.getX()) / jmax (1.0f, ring.getWidth()));
                                     Graphs::setValue (P (S::pDecay), 0.2f * std::pow (150.0f, jlimit (0.0f, 1.0f, (ring.getBottom() - p.y) / jmax (1.0f, ring.getHeight()))));
                                 },
                                 [this] { return sm.param (S::pDecay)->getCurrentValueAsString() + ", damping " + sm.param (S::pDamping)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            line = r.removeFromTop (r.getHeight() * 0.36f).reduced (30.0f, 6.0f);
            r.removeFromTop (30.0f);
            start = r.removeFromLeft ((r.getWidth() - 20.0f) * 0.5f).reduced (10.0f, 0.0f).withTrimmedBottom (16.0f);
            r.removeFromLeft (20.0f);
            ring = r.reduced (10.0f, 0.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, line, "THE STRING", "drag the point along the string; up for a brighter start");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (line.expanded (20.0f, 8.0f), 5.0f);
            const float pos = sm.get (S::pPosition), bright = sm.get (S::pBrightness);
            const auto tip = juce::Point<float> (line.getX() + pos * line.getWidth(), line.getCentreY() - (0.15f + 0.8f * bright) * line.getHeight() * 0.5f);
            g.setColour (Theme::ink.withAlpha (0.15f));
            g.drawLine (line.getX(), line.getCentreY(), line.getRight(), line.getCentreY(), 1.0f);
            Path st;
            st.startNewSubPath (line.getX(), line.getCentreY());
            // a brighter start is a sharper corner where the string is pulled
            st.quadraticTo (tip.x - (tip.x - line.getX()) * (1.0f - bright) * 0.6f, tip.y, tip.x, tip.y);
            st.quadraticTo (tip.x + (line.getRight() - tip.x) * (1.0f - bright) * 0.3f, tip.y, line.getRight(), line.getCentreY());
            Graphs::glowStroke (g, st, Theme::orange, 2.0f);
            g.setColour (Theme::text.withAlpha (0.7f));
            g.fillRoundedRectangle (line.getX() - 5.0f, line.getCentreY() - 14.0f, 5.0f, 28.0f, 2.0f);
            g.fillRoundedRectangle (line.getRight(), line.getCentreY() - 14.0f, 5.0f, 28.0f, 2.0f);
            Graphs::label (g, sm.param (S::pExciter)->getCurrentValueAsString().toUpperCase(), { tip.x - 40.0f, tip.y - 26.0f, 80.0f, 12.0f }, Justification::centred, 0.8f);

            auto barsIn = [&] (Rectangle<float> a, const String& title, const String& hint, std::function<float (int)> value, float most, Colour col, std::function<String (float)> text)
            {
                SpaceUI::areaTitle (g, a, title, hint);
                g.setColour (Pro::shade (0.3f));
                g.fillRoundedRectangle (a.expanded (6.0f), 5.0f);
                const float bw = a.getWidth() / (float) numHarmonics;
                for (int n = 1; n <= numHarmonics; ++n)
                {
                    const float v = jlimit (0.0f, 1.0f, value (n) / most), h = jmax (1.5f, v * a.getHeight());
                    g.setColour (col.withAlpha (0.35f + 0.55f * v));
                    g.fillRoundedRectangle (Rectangle<float> (a.getX() + bw * (float) (n - 1) + 2.0f, a.getBottom() - h, bw - 4.0f, h), 2.0f);
                    if (n == 1 || n % 4 == 0) Graphs::label (g, String (n), { a.getX() + bw * (float) (n - 1), a.getBottom() + 3.0f, bw, 11.0f }, Justification::centred, 0.55f);
                }
                Graphs::label (g, text (value (1)), { a.getX() + 4.0f, a.getY() + 2.0f, 120.0f, 12.0f }, Justification::centredLeft, 0.7f);
            };
            float most = 1.0e-6f;
            for (int n = 1; n <= numHarmonics; ++n) most = jmax (most, sm.harmonicStart (n));
            barsIn (start, "HOW LOUD EACH HARMONIC STARTS", "the place on the string decides which are missing",
                    [this] (int n) { return sm.harmonicStart (n); }, most, Theme::orange, [] (float) { return String(); });
            barsIn (ring, "HOW LONG EACH HARMONIC RINGS (A3)", "drag the point: up = longer, right = more damping",
                    [this] (int n) { return decay01 (sm.harmonicDecay (n, 220.0f)); }, 1.0f, Theme::accent, [this] (float) { return String (sm.harmonicDecay (1, 220.0f), 1) + " s"; });
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "EXCITER", sm.param (S::pExciter)->getCurrentValueAsString() }, { "POSITION", sm.param (S::pPosition)->getCurrentValueAsString() },
                                            { "DECAY", sm.param (S::pDecay)->getCurrentValueAsString() }, { "DAMPING", sm.param (S::pDamping)->getCurrentValueAsString() },
                                            { "STIFFNESS", sm.param (S::pStiffness)->getCurrentValueAsString() }, { "BODY", sm.param (S::pBody)->getCurrentValueAsString() } });
        }

    private:
        static constexpr int numHarmonics = 20;
        static float decay01 (float seconds) { return std::log (jlimit (0.2f, 30.0f, seconds) / 0.2f) / std::log (150.0f); }
        S& sm;
        Rectangle<float> line, start, ring, readoutArea;
    };

    class StringPanel : public DynamicsUI::Panel
    {
    public:
        using S = pulse::StringModel;
        StringPanel (EditViewState& evs, S& s)
        {
            display = std::make_unique<StringView> (s);
            addAndMakeVisible (*display);
            auto* ex = strips.add (new Strip ("EXCITER"));
            ex->add (new ParamChoice (*s.param (S::pExciter), Theme::orange, false), 250);
            ex->add (knob (evs, *s.param (S::pPosition), "Position"), 58);
            ex->add (knob (evs, *s.param (S::pBrightness), "Brightness"), 62);
            ex->add (knob (evs, *s.param (S::pHardness), "Velocity"), 56);
            auto* st1 = strips.add (new Strip ("STRING"));
            st1->add (knob (evs, *s.param (S::pDecay), "Decay"), 54);
            st1->add (knob (evs, *s.param (S::pDamping), "Damping"), 58);
            st1->add (knob (evs, *s.param (S::pStiffness), "Stiffness"), 58);
            st1->add (knob (evs, *s.param (S::pDetune), "2nd string"), 62);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *s.param (S::pBody), "Body"), 52);
            out->add (knob (evs, *s.param (S::pRelease), "Release"), 56);
            out->add (knob (evs, *s.param (S::pLevel), "Level"), 52);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
