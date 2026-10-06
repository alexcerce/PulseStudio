#pragma once

#include "EditViewState.h"
#include "Macros.h"

/** A track's macros on its header: small dials for the macros that control something, shown when the track is
    tall enough to have room for them. The sound of a track can then be played from the arrangement, without
    opening its devices. */
namespace HeaderMacros
{
    /** The macros of a track that are mapped to something (the only ones worth a place on the header). */
    inline ReferenceCountedArray<te::MacroParameter> used (te::AudioTrack& t)
    {
        ReferenceCountedArray<te::MacroParameter> out;
        for (auto m : Macros::get (t))
            if (! Macros::targets (t, *m).isEmpty()) out.add (m);
        return out;
    }

    /** One small dial: drag up and down (Shift = fine), wheel, double-click for its default. */
    class Dial : public Component,
                 public SettableTooltipClient
    {
    public:
        Dial (te::AudioTrack& t, te::MacroParameter& m) : track (t), macro (&m) {}

        te::MacroParameter& getMacro() const { return *macro; }
        float value() const { return macro->getCurrentNormalisedValue(); }

        /** Turns the dial (0..1), as a drag does. */
        void turnTo (float v) { macro->setNormalisedParameter (jlimit (0.0f, 1.0f, v), sendNotification); repaint(); }

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            auto label = r.removeFromBottom (11.0f);
            const float d = jmin (r.getWidth(), r.getHeight()) - 4.0f;
            auto dial = r.withSizeKeepingCentre (d, d);
            const float a0 = MathConstants<float>::pi * 1.25f, a1 = MathConstants<float>::pi * 2.75f, v = value();
            Path back, arc;
            back.addCentredArc (dial.getCentreX(), dial.getCentreY(), d * 0.5f - 1.5f, d * 0.5f - 1.5f, 0.0f, a0, a1, true);
            arc.addCentredArc (dial.getCentreX(), dial.getCentreY(), d * 0.5f - 1.5f, d * 0.5f - 1.5f, 0.0f, a0, a0 + (a1 - a0) * v, true);
            g.setColour (Theme::well);
            g.fillEllipse (dial.reduced (3.5f));
            g.setColour (Theme::ink.withAlpha (0.14f));
            g.strokePath (back, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
            const auto col = track.getColour().isTransparent() ? Theme::accent : track.getColour();
            g.setColour (isMouseOverOrDragging() ? col.brighter (0.3f) : col);
            g.strokePath (arc, PathStrokeType (2.4f, PathStrokeType::curved, PathStrokeType::rounded));
            const float ang = a0 + (a1 - a0) * v;
            g.setColour (Theme::text);
            g.drawLine (Line<float> (dial.getCentre(), dial.getCentre().getPointOnCircumference (d * 0.5f - 5.0f, ang)).withShortenedStart (2.0f), 1.5f);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (9.5f));
            g.drawText (macro->macroName.get(), label.expanded (3.0f, 0.0f), Justification::centred, true);
        }

        void mouseDown (const MouseEvent&) override
        {
            track.edit.getUndoManager().beginNewTransaction ("Macro " + macro->macroName.get());
            macro->parameterChangeGestureBegin();
            atDown = value();
        }
        void mouseDrag (const MouseEvent& e) override { turnTo (atDown - (float) e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 800.0f : 160.0f)); }
        void mouseUp (const MouseEvent&) override     { macro->parameterChangeGestureEnd(); }
        void mouseDoubleClick (const MouseEvent&) override
        {
            track.edit.getUndoManager().beginNewTransaction ("Reset macro");
            if (auto d = macro->getDefaultValue()) macro->setParameter (*d, sendNotification);
            repaint();
        }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override { turnTo (value() + (w.deltaY > 0 ? 0.02f : -0.02f)); }
        void mouseEnter (const MouseEvent&) override { repaint(); }
        void mouseExit (const MouseEvent&) override  { repaint(); }

    private:
        te::AudioTrack& track;
        te::MacroParameter::Ptr macro;
        float atDown = 0.0f;
    };

    /** The row of dials. Empty (and asking for no room) when the track has no mapped macro. */
    class Row : public Component,
                private Timer
    {
    public:
        static constexpr int dialWidth = 38, height = 40;

        explicit Row (te::AudioTrack& t) : track (t) { rebuild(); startTimerHz (5); }

        int numDials() const       { return dials.size(); }
        Dial* dial (int i) const   { return dials[i]; }
        /** Called when the dials have changed (the header lays itself out again). */
        std::function<void()> onChange;

        /** Looks at the track again now (the row does this itself a few times a second). */
        void refresh() { timerCallback(); }

        void resized() override
        {
            auto r = getLocalBounds();
            for (auto* d : dials)
            {
                d->setVisible (r.getWidth() >= dialWidth);   // those there is no room for wait until the header is wider
                d->setBounds (r.removeFromLeft (dialWidth));
            }
        }

    private:
        String signature() const
        {
            String s;
            for (auto m : used (track)) s << m->paramID << "|" << m->macroName.get() << ";";
            return s;
        }

        void rebuild()
        {
            dials.clear();
            for (auto m : used (track))
            {
                auto* d = dials.add (new Dial (track, *m));
                addAndMakeVisible (d);
            }
            built = signature();
            resized();
        }

        void timerCallback() override
        {
            if (signature() != built) { rebuild(); if (onChange) onChange(); }
            for (auto* d : dials)
            {
                d->setTooltip (d->getMacro().macroName.get() + ": " + String (roundToInt (d->value() * 100.0f)) + "%\nControls: " + Macros::targets (track, d->getMacro()).joinIntoString (", "));
                if (std::abs (d->value() - d->getProperties().getWithDefault ("shown", -1.0f).operator float()) > 0.002f)
                {
                    d->getProperties().set ("shown", d->value());
                    d->repaint();   // moved by automation, a controller or the device panel
                }
            }
        }

        te::AudioTrack& track;
        OwnedArray<Dial> dials;
        String built;
    };
}
