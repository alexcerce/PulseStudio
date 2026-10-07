#pragma once

#include "Kick.h"
#include "Synths2Display.h"

/** The window for Pulse Kick. */
namespace KickUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** The kick itself, as it will sound (the wave, drawn from the same code that plays it), with what shapes it
        drawn over it: the pitch falling (orange), the loudness dying away (green) and the click (yellow). The
        points are the settings: drag them. */
    class KickView : public HandleView
    {
    public:
        using K = pulse::KickSynth;
        explicit KickView (K& k) : HandleView (k), ks (k)
        {
            handles.push_back ({ "Pitch fall", Theme::orange, { P (K::pPitch) },
                                 [this] { return juce::Point<float> (area.getX(), yPitch (ks.get (K::pPitch))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (K::pPitch), semisAt (p.y)); },
                                 [this] { return "starts " + ks.param (K::pPitch)->getCurrentValueAsString() + " above"; }, {} });
            handles.push_back ({ "Pitch time / curve", Theme::orange, { P (K::pPitchTime), P (K::pPitchCurve) },
                                 [this] { return juce::Point<float> (xTime (ks.get (K::pPitchTime) * 0.001), yPitch (0.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (K::pPitchTime), (float) timeAt (p.x) * 1000.0f); },
                                 [this] { return "down in " + ks.param (K::pPitchTime)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (K::pPitchCurve), ks.get (K::pPitchCurve) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Hold", Theme::green, { P (K::pHold) },
                                 [this] { return juce::Point<float> (xTime (ks.get (K::pHold) * 0.001), area.getY()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (K::pHold), (float) timeAt (p.x) * 1000.0f); },
                                 [this] { return "held " + ks.param (K::pHold)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Decay / curve", Theme::green, { P (K::pDecay), P (K::pAmpCurve) },
                                 [this] { return juce::Point<float> (xTime ((ks.get (K::pHold) + ks.get (K::pDecay)) * 0.001), area.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (K::pDecay), (float) timeAt (p.x) * 1000.0f - ks.get (K::pHold)); },
                                 [this] { return "gone after " + ks.param (K::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (K::pAmpCurve), ks.get (K::pAmpCurve) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Click", Theme::yellow, { P (K::pClick), P (K::pClickTime) },
                                 [this] { return juce::Point<float> (xTime (ks.get (K::pClickTime) * 0.001 * 2.0), area.getBottom() - ks.get (K::pClick) * area.getHeight() * 0.9f); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (K::pClick), (area.getBottom() - p.y) / jmax (1.0f, area.getHeight() * 0.9f));
                                     Graphs::setValue (P (K::pClickTime), (float) timeAt (p.x) * 500.0f);
                                 },
                                 [this] { return "click " + ks.param (K::pClick)->getCurrentValueAsString() + ", " + ks.param (K::pClickTime)->getCurrentValueAsString(); }, {} });
        }

        /** How many seconds the picture is wide (for the self-test). */
        double shownSeconds() const { return span(); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (18.0f);
            r.removeFromTop (22.0f);
            area = r.withTrimmedLeft (14.0f).withTrimmedRight (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "THE KICK", "drag the points: orange = pitch, green = loudness, yellow = click (wheel on a point: its curve)");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            const auto shape = ks.shapeNow();
            const double seconds = span();

            // time marks
            g.setFont (FontOptions (9.0f));
            const double stepSec = SpaceUI::niceStep (seconds, 8);
            for (double t = 0.0; t <= seconds + 1.0e-6; t += stepSec)
            {
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.drawVerticalLine ((int) xTime (t), area.getY(), area.getBottom());
                g.setColour (Theme::muted.withAlpha (0.7f));
                g.drawText (String (roundToInt (t * 1000.0)) + " ms", (int) xTime (t) - 30, (int) area.getBottom() + 3, 60, 12, Justification::centred);
            }
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

            // the wave: made by the same code that plays it, at a rate that suits the picture
            {
                const double rate = 16000.0;
                pulse::KickMaker maker;
                maker.start (shape, rate, 1.0f, 12345u);
                const int total = (int) (seconds * rate), width = jmax (1, (int) area.getWidth());
                Path wave;
                std::vector<float> lo ((size_t) width, 0.0f), hi ((size_t) width, 0.0f);
                for (int i = 0; i < total; ++i)
                {
                    const float v = maker.next();
                    const auto x = (size_t) jmin (width - 1, (int) ((double) i / total * width));
                    lo[x] = jmin (lo[x], v); hi[x] = jmax (hi[x], v);
                }
                const float half = area.getHeight() * 0.46f, mid = area.getCentreY();
                wave.startNewSubPath (area.getX(), mid);
                for (int x = 0; x < width; ++x) wave.lineTo (area.getX() + (float) x, mid - hi[(size_t) x] * half);
                for (int x = width; --x >= 0;) wave.lineTo (area.getX() + (float) x, mid - lo[(size_t) x] * half);
                wave.closeSubPath();
                const float flash = jlimit (0.0f, 1.0f, glow);
                g.setColour (Theme::ink.withAlpha (0.22f + 0.25f * flash));
                g.fillPath (wave);
            }

            // loudness (green), pitch (orange)
            Path amp, pitch;
            for (float x = area.getX(); x <= area.getRight(); x += 1.5f)
            {
                const double t = timeAt (x);
                const float ya = area.getBottom() - shape.ampAt (jmax (0.0011, t)) * area.getHeight();
                const float yp = yPitch (shape.pitchSemis * pulse::KickShape::fall (t, shape.pitchMs * 0.001, shape.pitchCurve));
                if (x == area.getX()) { amp.startNewSubPath (x, ya); pitch.startNewSubPath (x, yp); } else { amp.lineTo (x, ya); pitch.lineTo (x, yp); }
            }
            Graphs::glowStroke (g, amp, Theme::green, 1.6f);
            Graphs::glowStroke (g, pitch, Theme::orange, 1.8f);
            // the click: how long and how loud
            if (shape.click > 0.001f)
            {
                Path click;
                click.startNewSubPath (area.getX(), area.getBottom());
                for (float x = area.getX(); x <= xTime (shape.clickMs * 0.004); x += 1.0f)
                {
                    const double t = timeAt (x);
                    click.lineTo (x, area.getBottom() - shape.click * (float) std::exp (-t / (shape.clickMs * 0.001)) * area.getHeight() * 0.9f);
                }
                click.lineTo (xTime (shape.clickMs * 0.004), area.getBottom());
                click.closeSubPath();
                g.setColour (Theme::yellow.withAlpha (0.22f));
                g.fillPath (click);
            }
            Graphs::label (g, "PITCH", { area.getX() + 10.0f, yPitch (ks.get (K::pPitch)) - 16.0f, 60.0f, 12.0f }, Justification::centredLeft, 0.7f);

            drawHandles (g);
            const float hz = ks.get (K::pTune);
            const int nearest = roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
            drawReadouts (g, readoutArea, { { "TUNE", String (hz, 1) + " Hz  (" + MidiMessage::getMidiNoteName (nearest, true, true, 3) + ")" }, { "STARTS AT", String (roundToInt (shape.pitchAt (0.0))) + " Hz" },
                                            { "PITCH DOWN IN", ks.param (K::pPitchTime)->getCurrentValueAsString() }, { "LENGTH", String (roundToInt (shape.length() * 1000.0)) + " ms" },
                                            { "CLICK", ks.param (K::pClick)->getCurrentValueAsString() }, { "DRIVE", ks.param (K::pDrive)->getCurrentValueAsString() } });

            // a kick has just been played: the wave lights up for a moment
            if (const int h = ks.hits.load(); h != lastHits) { lastHits = h; glow = 1.0f; }
            glow *= 0.85f;
        }

    private:
        double span() const              { return jmax (0.12, (double) (ks.get (K::pHold) + ks.get (K::pDecay)) * 0.001 * 1.08); }
        float xTime (double t) const     { return area.getX() + (float) (t / span()) * area.getWidth(); }
        double timeAt (float x) const    { return jlimit (0.0, 1.0, (double) (x - area.getX()) / jmax (1.0f, area.getWidth())) * span(); }
        // pitch: the Tune at three quarters down the picture, six octaves above it at the top
        float yPitch (float semis) const { return area.getY() + (1.0f - semis / 72.0f) * area.getHeight() * 0.75f; }
        float semisAt (float y) const    { return (1.0f - (y - area.getY()) / jmax (1.0f, area.getHeight() * 0.75f)) * 72.0f; }
        K& ks;
        Rectangle<float> area, readoutArea;
        int lastHits = 0;
        float glow = 0.0f;
    };

    class KickPanel : public DynamicsUI::Panel
    {
    public:
        using K = pulse::KickSynth;
        KickPanel (EditViewState& evs, K& k)
        {
            display = std::make_unique<KickView> (k);
            addAndMakeVisible (*display);
            auto* bd = strips.add (new Strip ("BODY"));
            bd->add (knob (evs, *k.param (K::pTune), "Tune"), 52);
            { auto* keys = new ParamToggle (*k.param (K::pKeys), "Keys", Theme::accent);
              keys->setTooltip ("Keys: the kick follows the notes that are played (C1 is the Tune itself). Off: every note plays the same kick");
              bd->add (keys, 50); }
            bd->add (knob (evs, *k.param (K::pShape), "Shape"), 50);
            bd->add (knob (evs, *k.param (K::pPhase), "Phase"), 50);
            auto* pt = strips.add (new Strip ("PITCH"));
            pt->add (knob (evs, *k.param (K::pPitch), "Fall"), 50);
            pt->add (knob (evs, *k.param (K::pPitchTime), "Time"), 50);
            pt->add (knob (evs, *k.param (K::pPitchCurve), "Curve"), 50);
            auto* am = strips.add (new Strip ("LOUDNESS"));
            am->add (knob (evs, *k.param (K::pHold), "Hold"), 50);
            am->add (knob (evs, *k.param (K::pDecay), "Decay"), 50);
            am->add (knob (evs, *k.param (K::pAmpCurve), "Curve"), 50);
            auto* cl = strips.add (new Strip ("CLICK"));
            cl->add (knob (evs, *k.param (K::pClick), "Level"), 50);
            cl->add (knob (evs, *k.param (K::pClickTime), "Time"), 50);
            cl->add (knob (evs, *k.param (K::pClickTone), "Tone"), 50);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *k.param (K::pDrive), "Drive"), 50);
            out->add (knob (evs, *k.param (K::pVelocity), "Velocity"), 52);
            out->add (knob (evs, *k.param (K::pLevel), "Level"), 50);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };
}
