#pragma once

#include "MidiFx2.h"
#include "SpaceDisplay.h"

/** The picture for the Velocity effect: how hard a note comes out for how hard it was played. */
namespace MidiFxUI
{
    class VelocityView : public SpaceUI::HandleView
    {
    public:
        using V = pulse::VelocityFx;
        explicit VelocityView (V& v) : HandleView (v), vel (v)
        {
            handles.push_back ({ "Lowest", Theme::accent, { P (V::pOutLow) },
                                 [this] { return juce::Point<float> (area.getX(), yVel (vel.get (V::pOutLow))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (V::pOutLow), velAt (p.y)); },
                                 [this] { return vel.param (V::pOutLow)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Curve", Theme::orange, { P (V::pCurve) },
                                 [this] { return juce::Point<float> (area.getCentreX(), yVel ((float) vel.map (64))); },
                                 [this] (juce::Point<float>, juce::Point<float> d) { Graphs::setValue (P (V::pCurve), curveAtDown - d.y / jmax (1.0f, area.getHeight()) * 2.5f); },
                                 [this] { return vel.param (V::pCurve)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (V::pCurve), vel.get (V::pCurve) + (d > 0 ? 0.05f : -0.05f)); } });
            handles.push_back ({ "Highest", Theme::accent, { P (V::pOutHigh) },
                                 [this] { return juce::Point<float> (area.getRight(), yVel (vel.get (V::pOutHigh))); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (V::pOutHigh), velAt (p.y)); },
                                 [this] { return vel.param (V::pOutHigh)->getCurrentValueAsString(); }, {} });
        }

        void mouseDown (const MouseEvent& e) override { curveAtDown = vel.get (V::pCurve); HandleView::mouseDown (e); }
        void dragCurve (float dy) { curveAtDown = vel.get (V::pCurve); dragHandle (1, 0.0f, dy); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            r.removeFromTop (22.0f);
            const float side = jmin (r.getHeight() - 20.0f, r.getWidth() - 60.0f);
            area = Rectangle<float> (side, side).withCentre (r.getCentre().translated (10.0f, -6.0f));
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "HOW HARD IN  >  HOW HARD OUT", "drag the ends for the range, the middle for the curve");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            for (int v = 0; v <= 127; v += 32)
            {
                const float x = area.getX() + area.getWidth() * (float) v / 127.0f, y = yVel ((float) jmax (1, v));
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
                g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
                Graphs::label (g, String (jmax (1, v)), { x - 14.0f, area.getBottom() + 3.0f, 28.0f, 11.0f }, Justification::centred, 0.55f);
                Graphs::label (g, String (jmax (1, v)), { area.getX() - 30.0f, y - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            g.setColour (Theme::ink.withAlpha (0.14f));
            g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);   // unchanged
            const float rnd = vel.get (V::pRandom) * 64.0f;
            Path line, band;
            for (int v = 1; v <= 127; ++v)
            {
                const float x = area.getX() + area.getWidth() * (float) (v - 1) / 126.0f, y = yVel ((float) vel.map (v));
                if (v == 1) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            if (rnd > 0.5f)   // where the randomness can put a note
            {
                for (int v = 1; v <= 127; ++v) { const float x = area.getX() + area.getWidth() * (float) (v - 1) / 126.0f, y = yVel (jmin (127.0f, (float) vel.map (v) + rnd)); if (v == 1) band.startNewSubPath (x, y); else band.lineTo (x, y); }
                for (int v = 127; v >= 1; --v) band.lineTo (area.getX() + area.getWidth() * (float) (v - 1) / 126.0f, yVel (jmax (1.0f, (float) vel.map (v) - rnd)));
                band.closeSubPath();
                g.setColour (Theme::orange.withAlpha (0.15f));
                g.fillPath (band);
            }
            Graphs::glowStroke (g, line, Theme::orange, 2.0f);
            drawHandles (g);
        }

    private:
        float yVel (float v) const   { return area.getBottom() - jlimit (0.0f, 1.0f, (v - 1.0f) / 126.0f) * area.getHeight(); }
        float velAt (float y) const  { return 1.0f + jlimit (0.0f, 1.0f, (area.getBottom() - y) / jmax (1.0f, area.getHeight())) * 126.0f; }
        V& vel;
        Rectangle<float> area;
        float curveAtDown = 0.0f;
    };
}
