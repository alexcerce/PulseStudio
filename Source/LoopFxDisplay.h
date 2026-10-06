#pragma once

#include "LoopFx.h"
#include "TimeFxDisplay.h"

/** Windows for Beat Repeat and the Looper. */
namespace LoopUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** Beat Repeat: one interval as a strip of time. The shaded part is where it repeats (the gate), cut into
        slices that get quieter and lower as Decay and Pitch say. Drag the edge of the first slice for the grid,
        the end of the shaded part for the gate, the top of the second slice for the decay. */
    class BeatRepeatView : public HandleView
    {
    public:
        using B = pulse::BeatRepeat;
        explicit BeatRepeatView (B& b) : HandleView (b), br (b)
        {
            auto nearest = [] (const double* table, int count, double beats)
            {
                int best = 0;
                for (int i = 1; i < count; ++i) if (std::abs (std::log (table[i] / beats)) < std::abs (std::log (table[best] / beats))) best = i;
                return (float) best;
            };
            handles.push_back ({ "Grid", Theme::accent, { P (B::pGrid) },
                                 [this] { return juce::Point<float> (xBeat (grid()), area.getBottom()); },
                                 [this, nearest] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (B::pGrid), nearest (B::gridBeats, 7, jmax (0.05, beatAt (p.x)))); },
                                 [this] { return br.param (B::pGrid)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Gate", Theme::yellow, { P (B::pGate) },
                                 [this] { return juce::Point<float> (xBeat (gate()), area.getY()); },
                                 [this, nearest] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (B::pGate), nearest (B::gateBeats, 5, jmax (0.1, beatAt (p.x)))); },
                                 [this] { return br.param (B::pGate)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Decay", Theme::orange, { P (B::pDecay) },
                                 [this] { return juce::Point<float> (xBeat (grid() * 1.5), blockTop (1)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (B::pDecay), (1.0f - (area.getBottom() - p.y) / jmax (1.0f, area.getHeight() * 0.8f)) / 0.6f); },
                                 [this] { return br.param (B::pDecay)->getCurrentValueAsString(); }, {} });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            area = r.reduced (20.0f, 0.0f).withTrimmedBottom (20.0f).withTrimmedTop (8.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area.translated (0.0f, -8.0f), "ONE INTERVAL", "drag: the first slice's edge = grid, the end of the shaded part = gate, the second slice's top = decay");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            const double interval = B::intervalBeats[jlimit (0, 4, br.getInt (B::pInterval))];
            span = jmax (interval, gate());
            for (double b = 0.0; b <= span + 0.001; b += span > 8.0 ? 4.0 : 1.0)
            {
                g.setColour (Theme::ink.withAlpha (std::fmod (b, 4.0) < 0.01 ? 0.14f : 0.06f));
                g.drawVerticalLine ((int) xBeat (b), area.getY(), area.getBottom());
                Graphs::label (g, String (b + 1.0, 0), { xBeat (b) + 3.0f, area.getBottom() + 3.0f, 30.0f, 11.0f }, Justification::centredLeft, 0.55f);
            }
            const bool live = br.repeating.load();
            const int current = br.repeatNumber.load();
            g.setColour (Theme::yellow.withAlpha (live ? 0.14f : 0.07f));
            g.fillRect (Rectangle<float> (area.getX(), area.getY(), xBeat (gate()) - area.getX(), area.getHeight()));
            const float pitch = br.get (B::pPitch), pitchDecay = br.get (B::pPitchDecay);
            int k = 0;
            for (double b = 0.0; b < gate() - 0.001 && k < 128; b += grid(), ++k)
            {
                // each slice a little quieter (Decay) and drawn lower (Pitch)
                const float semis = pitch * (1.0f - pitchDecay) + pitch * pitchDecay * jmin (1.0f, k / 4.0f);
                auto block = Rectangle<float>::leftTopRightBottom (xBeat (b) + 1.5f, blockTop (k), xBeat (jmin (gate(), b + grid())) - 1.5f, area.getBottom()).translated (0.0f, 0.0f);
                const auto col = (k == 0 ? Theme::ink : Theme::accent).interpolatedWith (Theme::orange, jlimit (0.0f, 1.0f, -semis / 24.0f));
                g.setColour (col.withAlpha (live && k == current ? 0.9f : k == 0 ? 0.25f : 0.5f));
                g.fillRoundedRectangle (block, 3.0f);
            }
            Graphs::label (g, "heard as played", { area.getX() + 4.0f, area.getBottom() - 16.0f, 120.0f, 12.0f }, Justification::centredLeft, 0.7f);
            g.setColour (Theme::ink.withAlpha (0.8f));
            g.drawVerticalLine ((int) xBeat (br.intervalPhase.load() * interval), area.getY(), area.getBottom());
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "EVERY", br.param (B::pInterval)->getCurrentValueAsString() }, { "GRID", br.param (B::pGrid)->getCurrentValueAsString() },
                                            { "GATE", br.param (B::pGate)->getCurrentValueAsString() }, { "CHANCE", br.param (B::pChance)->getCurrentValueAsString() },
                                            { "MODE", br.param (B::pMode)->getCurrentValueAsString() }, { "NOW", live ? "repeat " + String (current + 1) : String ("-") } });
        }

    private:
        double grid() const             { return B::gridBeats[jlimit (0, 6, br.getInt (B::pGrid))]; }
        double gate() const             { return B::gateBeats[jlimit (0, 4, br.getInt (B::pGate))]; }
        float xBeat (double b) const    { return area.getX() + (float) (b / span) * area.getWidth(); }
        double beatAt (float x) const   { return (double) ((x - area.getX()) / jmax (1.0f, area.getWidth())) * span; }
        float blockTop (int k) const    { return area.getBottom() - area.getHeight() * 0.8f * std::pow (1.0f - br.get (B::pDecay) * 0.6f, (float) k); }
        B& br;
        Rectangle<float> area, readoutArea;
        double span = 4.0;
    };

    //==============================================================================
    /** Looper: the loop as a waveform with the playing position, and what it is doing in large letters. */
    class LooperView : public HandleView
    {
    public:
        using L = pulse::Looper;
        explicit LooperView (L& l) : HandleView (l), lp (l) {}

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
            const int state = lp.getInt (L::pState);
            const bool rec = lp.isRecordingNow();
            const double secs = lp.loopSeconds();
            const auto col = rec ? Theme::red : state == L::overdubbing ? Theme::orange : state == L::stopped ? Theme::muted : Theme::green;
            SpaceUI::areaTitle (g, area, "THE LOOP", secs > 0.0 ? String (secs, 2) + " s" : String());
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
            if (secs <= 0.0)
            {
                g.setColour (Theme::muted);
                g.setFont (FontOptions (15.0f));
                g.drawText ("Nothing recorded yet. Choose Record, play, then choose Play (or Overdub to add to it).", area, Justification::centred);
            }
            else
            {
                const int bins = jmax (16, (int) (area.getWidth() / 3.0f));
                // while recording into a free-length loop, the picture fills from the left
                for (int b = 0; b < bins; ++b)
                {
                    const float pk = jlimit (0.0f, 1.0f, lp.peakAt (b, bins)), h = jmax (1.0f, pk * area.getHeight() * 0.48f);
                    g.setColour (col.withAlpha (0.75f));
                    g.fillRect (Rectangle<float> (area.getX() + area.getWidth() * (float) b / (float) bins, area.getCentreY() - h, jmax (1.0f, area.getWidth() / (float) bins - 1.0f), h * 2.0f));
                }
                const float x = area.getX() + (rec ? 1.0f : lp.position01()) * area.getWidth();
                g.setColour (Theme::ink);
                g.fillRect (Rectangle<float> (x - 1.0f, area.getY(), 2.0f, area.getHeight()));
            }
            // what it is doing
            auto lamp = Rectangle<float> (area.getRight() - 150.0f, area.getY() + 8.0f, 142.0f, 26.0f);
            g.setColour (col.withAlpha (0.2f));
            g.fillRoundedRectangle (lamp, 13.0f);
            g.setColour (col);
            g.fillEllipse (lamp.getX() + 9.0f, lamp.getCentreY() - 5.0f, 10.0f, 10.0f);
            g.setFont (FontOptions (13.0f, Font::bold));
            const String doing = rec ? "RECORDING" : state == L::recording ? "PLAYING" : state == L::overdubbing ? "OVERDUBBING" : state == L::playing ? "PLAYING" : "STOPPED";
            g.drawText (doing, lamp.withTrimmedLeft (22.0f), Justification::centred);
            drawReadouts (g, readoutArea, { { "LENGTH", secs > 0.0 ? String (secs, 2) + " s" : String ("-") }, { "SET LENGTH", lp.param (L::pLength)->getCurrentValueAsString() },
                                            { "SPEED", lp.param (L::pSpeed)->getCurrentValueAsString() + (lp.getInt (L::pReverse) > 0 ? " reversed" : "") },
                                            { "FEEDBACK", lp.param (L::pFeedback)->getCurrentValueAsString() }, { "MONITOR", lp.param (L::pMonitor)->getCurrentValueAsString() },
                                            { "LOOP LEVEL", lp.param (L::pLevel)->getCurrentValueAsString() } });
        }

    private:
        L& lp;
        Rectangle<float> area, readoutArea;
    };

    //==============================================================================
    class BeatRepeatPanel : public DynamicsUI::Panel
    {
    public:
        using B = pulse::BeatRepeat;
        BeatRepeatPanel (EditViewState& evs, B& b)
        {
            display = std::make_unique<BeatRepeatView> (b);
            addAndMakeVisible (*display);
            auto* wh = strips.add (new Strip ("WHEN"));
            wh->add (new ParamCombo (*b.param (B::pInterval), false), 84);
            wh->add (knob (evs, *b.param (B::pChance), "Chance"), 56);
            wh->add (new ParamCombo (*b.param (B::pGate), false), 84);
            wh->add (new ParamToggle (*b.param (B::pHold), "Repeat", Theme::yellow), 70);
            auto* sl = strips.add (new Strip ("SLICES"));
            sl->add (new ParamCombo (*b.param (B::pGrid), false), 84);
            sl->add (knob (evs, *b.param (B::pVariation), "Variation"), 60);
            auto* chg = strips.add (new Strip ("CHANGE"));
            chg->add (knob (evs, *b.param (B::pDecay), "Decay"), 56);
            chg->add (knob (evs, *b.param (B::pPitch), "Pitch"), 56);
            chg->add (knob (evs, *b.param (B::pPitchDecay), "Pitch decay"), 70);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (new ParamChoice (*b.param (B::pMode), Theme::accent, false), 190);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    class LooperPanel : public DynamicsUI::Panel
    {
    public:
        using L = pulse::Looper;
        LooperPanel (EditViewState& evs, L& l) : looper (l), holder (&l)
        {
            display = std::make_unique<LooperView> (l);
            addAndMakeVisible (*display);
            auto* st0 = strips.add (new Strip ("LOOP"));
            st0->add (new ParamChoice (*l.param (L::pState), Theme::green, false), 290);
            auto* clr = st0->add (new TextButton ("Clear"), 60);
            clr->setTooltip ("Throw the loop away");
            clr->onClick = [this] { looper.clear(); };
            st0->add (new ParamCombo (*l.param (L::pLength), false), 86);
            auto* pb = strips.add (new Strip ("PLAYBACK"));
            pb->add (knob (evs, *l.param (L::pSpeed), "Speed"), 56);
            pb->add (new ParamToggle (*l.param (L::pReverse), "Reverse", Theme::accent), 74);
            pb->add (knob (evs, *l.param (L::pLevel), "Level"), 56);
            auto* in = strips.add (new Strip ("INPUT"));
            in->add (knob (evs, *l.param (L::pFeedback), "Feedback"), 60);
            in->add (new ParamToggle (*l.param (L::pMonitor), "Monitor", Theme::accent), 74);
            for (auto* st : strips) addAndMakeVisible (st);
        }

    private:
        L& looper;
        te::Plugin::Ptr holder;
    };
}
