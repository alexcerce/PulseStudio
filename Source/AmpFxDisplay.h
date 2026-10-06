#pragma once

#include "AmpFx.h"
#include "ColourFxDisplay.h"

/** The window of Pulse Amp: the tone controls and the cabinet as one curve (the four tone controls are points
    on it), and what the valves do to the signal at the current gain. */
namespace AmpUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    class AmpView : public HandleView
    {
    public:
        using A = pulse::Amp;
        explicit AmpView (A& a) : HandleView (a), amp (a)
        {
            auto toneHandle = [this] (const char* name, int param, std::function<float()> hz, float rangeDb)
            {
                handles.push_back ({ name, Theme::yellow, { P (param) },
                                     [this, param, hz, rangeDb] { return juce::Point<float> (SpaceUI::xForFreq (tone, hz()), yDb ((amp.get (param) - 0.5f) * rangeDb)); },
                                     [this, param, rangeDb] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (param), 0.5f + dbAt (p.y) / rangeDb); },
                                     [this, param] { return amp.param (param)->getCurrentValueAsString(); },
                                     [this, param] (float d) { Graphs::setValue (P (param), amp.get (param) + (d > 0 ? 0.05f : -0.05f)); } });
            };
            toneHandle ("Bass", A::pBass, [] { return 90.0f; }, 24.0f);
            toneHandle ("Middle", A::pMid, [this] { return A::models[jlimit (0, 4, amp.getInt (A::pModel))].toneHz; }, 20.0f);
            toneHandle ("Presence", A::pPresence, [] { return 4800.0f; }, 16.0f);
            toneHandle ("Treble", A::pTreble, [] { return 9000.0f; }, 24.0f);
            handles.push_back ({ "Gain", Theme::orange, { P (A::pGain) },
                                 [this] { return juce::Point<float> (curve.getX() + (0.15f + 0.7f * amp.get (A::pGain)) * curve.getWidth(), curve.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (A::pGain), ((p.x - curve.getX()) / jmax (1.0f, curve.getWidth()) - 0.15f) / 0.7f); },
                                 [this] { return amp.param (A::pGain)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (A::pGain), amp.get (A::pGain) + (d > 0 ? 0.03f : -0.03f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            curve = r.removeFromRight (jmin (r.getHeight() - 16.0f, r.getWidth() * 0.3f)).withTrimmedBottom (16.0f);
            r.removeFromRight (26.0f);
            tone = r.withTrimmedLeft (32.0f).withTrimmedBottom (16.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, tone, "TONE AND CABINET", "drag the points: bass, middle, presence, treble");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (tone.expanded (6.0f), 5.0f);
            SpaceUI::freqGrid (g, tone, true);
            for (float db = 12.0f; db >= -36.0f; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.12f : 0.04f));
                g.drawHorizontalLine ((int) yDb (db), tone.getX(), tone.getRight());
                Graphs::label (g, String ((int) db), { tone.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            Path line, fill;
            fill.startNewSubPath (tone.getX(), tone.getBottom());
            for (float x = tone.getX(); x <= tone.getRight(); x += 2.0f)
            {
                const float y = yDb (Decibels::gainToDecibels (amp.toneResponse (SpaceUI::freqForX (tone, x)), -60.0f));
                fill.lineTo (x, y);
                if (x == tone.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (tone.getRight(), tone.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::yellow.withAlpha (0.22f), 0.0f, tone.getY(), Theme::yellow.withAlpha (0.02f), 0.0f, tone.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, Theme::yellow, 1.8f);

            // the valves: what comes out for what goes in at this gain, and how far the playing reaches into it
            SpaceUI::areaTitle (g, curve, "THE VALVES");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (curve.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) curve.getCentreY(), curve.getX(), curve.getRight());
            g.drawVerticalLine ((int) curve.getCentreX(), curve.getY(), curve.getBottom());
            const auto& m = A::models[jlimit (0, 4, amp.getInt (A::pModel))];
            const float drive = Decibels::decibelsToGain (-6.0f + m.preGain * std::pow (amp.get (A::pGain), 1.3f));
            auto through = [&] (float in)
            {
                float x = in * drive;
                for (int s = 0; s < m.stages; ++s) { x = A::valve (x, m.asym); if (s + 1 < m.stages) x *= 2.2f; }
                return x;
            };
            Path vp;
            for (float x = curve.getX(); x <= curve.getRight(); x += 1.0f)
            {
                const float in = (x - curve.getCentreX()) / (curve.getWidth() * 0.5f) * 0.5f;   // +/- 0.5: a loud guitar
                const float y = curve.getCentreY() - through (in) * curve.getHeight() * 0.46f;
                if (x == curve.getX()) vp.startNewSubPath (x, y); else vp.lineTo (x, y);
            }
            Graphs::glowStroke (g, vp, Theme::orange, 1.8f);
            const float reach = amp.driveNow.load();
            g.setColour (Theme::orange.withAlpha (0.15f + 0.5f * reach));
            g.fillRoundedRectangle (Rectangle<float> (curve.getX(), curve.getBottom() + 4.0f, curve.getWidth() * reach, 3.0f), 1.5f);
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "AMP", amp.param (A::pModel)->getCurrentValueAsString() }, { "GAIN", amp.param (A::pGain)->getCurrentValueAsString() },
                                            { "CABINET", amp.param (A::pCabinet)->getCurrentValueAsString() }, { "MICROPHONE", amp.param (A::pMic)->getCurrentValueAsString() },
                                            { "STAGES", String (m.stages) }, { "LEVEL", amp.param (A::pLevel)->getCurrentValueAsString() } });
        }

    private:
        float yDb (float db) const  { return tone.getY() + jlimit (0.0f, 1.0f, (18.0f - db) / 60.0f) * tone.getHeight(); }
        float dbAt (float y) const  { return 18.0f - (y - tone.getY()) / jmax (1.0f, tone.getHeight()) * 60.0f; }
        A& amp;
        Rectangle<float> tone, curve, readoutArea;
    };

    class AmpPanel : public DynamicsUI::Panel
    {
    public:
        using A = pulse::Amp;
        AmpPanel (EditViewState& evs, A& a)
        {
            display = std::make_unique<AmpView> (a);
            addAndMakeVisible (*display);
            auto* am = strips.add (new Strip ("AMP"));
            am->add (new ParamChoice (*a.param (A::pModel), Theme::orange, false), 300);
            am->add (knob (evs, *a.param (A::pGain), "Gain"), 58);
            auto* tn = strips.add (new Strip ("TONE"));
            tn->add (knob (evs, *a.param (A::pBass), "Bass"), 54);
            tn->add (knob (evs, *a.param (A::pMid), "Middle"), 54);
            tn->add (knob (evs, *a.param (A::pTreble), "Treble"), 54);
            tn->add (knob (evs, *a.param (A::pPresence), "Presence"), 58);
            auto* cb = strips.add (new Strip ("CABINET"));
            cb->add (new ParamCombo (*a.param (A::pCabinet), false), 120);
            cb->add (knob (evs, *a.param (A::pMic), "Mic"), 54);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *a.param (A::pLevel), "Level"), 56);
            out->add (knob (evs, *a.param (A::pMix), "Dry/Wet"), 58);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
