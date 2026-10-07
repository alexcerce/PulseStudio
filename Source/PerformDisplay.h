#pragma once

#include "Perform.h"
#include "RumbleDisplay.h"
#include "Routing.h"
#include "MidiLearn.h"

/** The window for Perform, and the Performance window: mutes, fills and the sweep on one page. */
namespace PerformUI
{
    using SpaceUI::HandleView;
    using DynamicsUI::Strip;
    using DynamicsUI::knob;

    /** What the sweep leaves of the sound, from 20 Hz to 20 kHz. One point: sideways is the sweep (the middle is
        off), up and down the resonance. Along the bottom, which fill is held and how far through its slice it is. */
    class PerformView : public HandleView
    {
    public:
        using F = pulse::Perform;
        explicit PerformView (F& f) : HandleView (f), pf (f)
        {
            handles.push_back ({ "Sweep / resonance", Theme::accent, { P (F::pSweep), P (F::pResonance) },
                                 [this] { return juce::Point<float> (xSweep (pf.get (F::pSweep)), yReso (pf.get (F::pResonance))); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     Graphs::setValue (P (F::pSweep), (p.x - area.getX()) / jmax (1.0f, area.getWidth()) * 2.0f - 1.0f);
                                     Graphs::setValue (P (F::pResonance), 1.0f - (p.y - area.getY()) / jmax (1.0f, area.getHeight()));
                                 },
                                 [this] { return pf.param (F::pSweep)->getCurrentValueAsString() + ", resonance " + pf.param (F::pResonance)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (F::pSweep), pf.get (F::pSweep) + (d > 0 ? 0.02f : -0.02f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (10.0f);
            fills = r.removeFromBottom (18.0f);
            r.removeFromBottom (12.0f);
            r.removeFromTop (22.0f);
            area = r.reduced (10.0f, 0.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            SpaceUI::areaTitle (g, area, "SWEEP", "drag the point: left closes a low-pass, right opens a high-pass, the middle is off; up is more resonance. Double-click: off");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (area.expanded (6.0f), 5.0f);
            g.setFont (FontOptions (9.0f));
            for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            {
                g.setColour (Theme::ink.withAlpha (0.07f));
                g.drawVerticalLine ((int) xHz (f), area.getY(), area.getBottom());
                g.setColour (Theme::muted.withAlpha (0.7f));
                g.drawText (f >= 1000.0f ? String ((int) (f / 1000.0f)) + "k" : String ((int) f), (int) xHz (f) - 16, (int) area.getBottom() - 13, 32, 12, Justification::centred);
            }
            // the middle of the sweep: where it is off
            g.setColour (Theme::ink.withAlpha (0.14f));
            g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
            // (the point moves along the sweep, not along the frequencies: say which way is which)
            Graphs::label (g, "< LOW-PASS CLOSES", { area.getX() + 6.0f, area.getY() + 4.0f, 160.0f, 12.0f }, Justification::centredLeft, 0.5f);
            Graphs::label (g, "OFF", { area.getCentreX() - 30.0f, area.getY() + 4.0f, 60.0f, 12.0f }, Justification::centred, 0.5f);
            Graphs::label (g, "HIGH-PASS OPENS >", { area.getRight() - 166.0f, area.getY() + 4.0f, 160.0f, 12.0f }, Justification::centredRight, 0.5f);

            // 0 dB sits two thirds of the way up, so that a resonance has room above it
            auto yGain = [this] (float gain) { return area.getY() + jlimit (0.0f, 1.0f, (18.0f - Decibels::gainToDecibels (gain, -48.0f)) / 66.0f) * area.getHeight(); };
            Path fill, line;
            fill.startNewSubPath (area.getX(), area.getBottom());
            for (float x = area.getX(); x <= area.getRight(); x += 2.0f)
            {
                const float y = yGain (pf.response (hzAt (x)));
                fill.lineTo (x, y);
                if (x == area.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (area.getRight(), area.getBottom());
            fill.closeSubPath();
            const bool on = F::sweepOn (pf.get (F::pSweep));
            const auto colour = on ? Theme::accent : Theme::muted;
            g.setGradientFill (ColourGradient (colour.withAlpha (0.40f), 0.0f, area.getY(), colour.withAlpha (0.05f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, colour, 1.8f);

            // the fills
            const int held = pf.getInt (F::pFill);
            const bool repeating = pf.repeating.load();
            for (int i = 1; i <= F::numFills; ++i)
            {
                auto r = fills.withWidth (fills.getWidth() / (float) F::numFills).translated (fills.getWidth() / (float) F::numFills * (float) (i - 1), 0.0f).reduced (3.0f, 0.0f);
                g.setColour (i == held ? Theme::orange.withAlpha (0.30f) : Pro::shade (0.35f));
                g.fillRoundedRectangle (r, 3.0f);
                if (i == held && repeating)
                {
                    g.setColour (Theme::orange);
                    g.fillRoundedRectangle (r.withWidth (r.getWidth() * jlimit (0.0f, 1.0f, pf.slicePhase.load())), 3.0f);
                }
                g.setColour (i == held ? Theme::text : Theme::muted);
                g.setFont (FontOptions (10.0f, Font::bold));
                g.drawText ("FILL " + F::fillNames[i], r, Justification::centred);
            }

            drawHandles (g);
            drawReadouts (g, readoutArea, { { "SWEEP", pf.param (F::pSweep)->getCurrentValueAsString() }, { "RESONANCE", pf.param (F::pResonance)->getCurrentValueAsString() },
                                            { "FILL", pf.param (F::pFill)->getCurrentValueAsString() } });
        }

    private:
        float xSweep (float v) const { return area.getX() + (jlimit (-1.0f, 1.0f, v) + 1.0f) * 0.5f * area.getWidth(); }
        float yReso (float v) const  { return area.getY() + (1.0f - jlimit (0.0f, 1.0f, v)) * area.getHeight(); }
        float xHz (float f) const    { return area.getX() + std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f) * area.getWidth(); }
        float hzAt (float x) const   { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, (x - area.getX()) / jmax (1.0f, area.getWidth()))); }
        F& pf;
        Rectangle<float> area, fills, readoutArea;
    };

    /** The device's own window: the picture, and a control for each setting. */
    class PerformPanel : public DynamicsUI::Panel
    {
    public:
        using F = pulse::Perform;
        PerformPanel (EditViewState& evs, F& f)
        {
            display = std::make_unique<PerformView> (f);
            addAndMakeVisible (*display);
            auto* sw = strips.add (new Strip ("SWEEP"));
            sw->add (knob (evs, *f.param (F::pSweep), "Sweep"), 70);
            sw->add (knob (evs, *f.param (F::pResonance), "Resonance"), 70);
            auto* fl = strips.add (new Strip ("FILL (REPEATS WHILE IT IS ON)"));
            fl->add (new ParamChoice (*f.param (F::pFill), Theme::orange, false), 300);
            for (auto* st : strips) addAndMakeVisible (st);
        }
    };

    //==============================================================================
    /** The project's Perform device: the first one on the master. With `create`, one is put there (first in the
        chain, so that whatever limits the master also holds a resonant sweep) if there is none. */
    inline pulse::Perform* device (te::Edit& e, bool create)
    {
        auto& list = e.getMasterPluginList();
        if (auto p = list.findFirstPluginOfType<pulse::Perform>()) return p;
        if (! create) return nullptr;
        auto p = e.getPluginCache().createNewPlugin (pulse::Perform::xmlTypeName, {});
        if (p == nullptr) return nullptr;
        e.getUndoManager().beginNewTransaction ("Perform on the master");
        list.insertPlugin (p, 0, nullptr);
        return dynamic_cast<pulse::Perform*> (p.get());
    }

    /** The tracks that get a mute pad: every audio track that is not a return. */
    inline Array<te::AudioTrack*> padTracks (te::Edit& e)
    {
        Array<te::AudioTrack*> out;
        for (auto* t : te::getAudioTracks (e)) if (! Routing::isReturnTrack (*t)) out.add (t);
        return out;
    }

    /** The Performance page: a mute pad for every track, the fills to hold, and the sweep. Everything on it can
        be learned to a controller (right-click). */
    class Stage : public Component,
                  public TooltipClient,
                  private Timer
    {
    public:
        using F = pulse::Perform;
        Stage (EditViewState& e, F& f) : evs (e), pf (f), holder (&f), view (f)
        {
            addAndMakeVisible (view);
            startTimerHz (15);
        }

        int numMutePads() const { return padTracks (evs.edit).size(); }
        /** A click on a mute pad. */
        void pressMute (int index)
        {
            const auto tracks = padTracks (evs.edit);
            if (isPositiveAndBelow (index, tracks.size())) EngineHelpers::toggleMute (*tracks[index]);
            repaint();
        }
        /** A fill pad held (1..4) or let go (0). */
        void holdFill (int fill)
        {
            latched = 0;
            pf.param (F::pFill)->setParameter ((float) jlimit (0, F::numFills, fill), sendNotificationSync);
            repaint();
        }
        /** A fill that stays on until it (or another) is pressed. */
        void latchFill (int fill)
        {
            latched = latched == fill ? 0 : jlimit (0, F::numFills, fill);
            pf.param (F::pFill)->setParameter ((float) latched, sendNotificationSync);
            repaint();
        }
        /** False once the device has been taken off the master (the window then closes). */
        bool deviceStillThere() const { return pf.state.getParent().isValid(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            mutes = r.removeFromTop (jlimit (90, 260, roundToInt ((float) r.getHeight() * 0.36f)));
            r.removeFromTop (10);
            fills = r.removeFromRight (jlimit (150, 260, r.getWidth() / 4));
            r.removeFromRight (10);
            view.setBounds (r);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto caption = [&g] (const String& text, Rectangle<int> r)
            {
                g.setColour (Theme::muted);
                g.setFont (FontOptions (10.0f, Font::bold));
                g.drawText (text, r, Justification::centredLeft);
            };
            caption ("MUTES   (click: mute, right-click: solo and controller)", mutes.withHeight (14));
            const auto tracks = padTracks (evs.edit);
            const bool anySolo = evs.edit.areAnyTracksSolo();
            for (int i = 0; i < tracks.size(); ++i)
            {
                auto* t = tracks[i];
                const auto r = mutePad (i, tracks.size()).toFloat();
                const bool muted = t->isMuted (false), solo = t->isSolo (false), heard = ! muted && (! anySolo || solo);
                auto colour = t->getColour();
                if (colour.isTransparent()) colour = Theme::trackColour (i);
                g.setColour (heard ? colour.withAlpha (0.85f) : Pro::deep());
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (heard ? Colours::white.withAlpha (0.25f) : colour.withAlpha (0.55f));
                g.drawRoundedRectangle (r.reduced (0.75f), 6.0f, 1.5f);
                g.setColour (heard ? Colours::black.withAlpha (0.85f) : Theme::muted);
                g.setFont (FontOptions (jlimit (10.0f, 14.0f, r.getHeight() * 0.22f), Font::bold));
                g.drawFittedText (t->getName(), r.reduced (6.0f, 4.0f).toNearestInt(), Justification::centred, 2);
                g.setFont (FontOptions (9.0f, Font::bold));
                if (muted) g.drawText ("MUTED", r.reduced (6.0f, 3.0f), Justification::centredBottom);
                if (solo) { g.setColour (Theme::orange); g.drawText ("SOLO", r.reduced (6.0f, 3.0f), Justification::centredTop); }
                if (evs.midiLearn != nullptr && evs.midiLearn->isMuteMapped (*t))
                {
                    g.setColour (heard ? Colours::black.withAlpha (0.6f) : Theme::purple);
                    g.fillEllipse (r.getRight() - 11.0f, r.getY() + 5.0f, 5.0f, 5.0f);
                }
            }
            if (tracks.isEmpty()) { g.setColour (Theme::muted); g.setFont (FontOptions (12.0f)); g.drawText ("No tracks yet", mutes, Justification::centred); }

            caption ("FILLS   (hold; Shift-click stays on)", fills.withHeight (14));
            const int held = pf.getInt (F::pFill);
            for (int i = 1; i <= F::numFills; ++i)
            {
                const auto r = fillPad (i).toFloat();
                const bool on = i == held;
                g.setColour (on ? Theme::orange : Pro::cardTop());
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (on ? Colours::white.withAlpha (0.4f) : Theme::ink.withAlpha (0.18f));
                g.drawRoundedRectangle (r.reduced (0.75f), 6.0f, 1.5f);
                g.setColour (on ? Colours::black.withAlpha (0.85f) : Theme::text);
                g.setFont (FontOptions (jlimit (13.0f, 22.0f, r.getHeight() * 0.42f), Font::bold));
                g.drawText (F::fillNames[i], r, Justification::centred);
                if (on && latched == i) { g.setFont (FontOptions (9.0f, Font::bold)); g.drawText ("ON", r.reduced (6.0f, 3.0f), Justification::centredBottom); }
            }
        }

        String getTooltip() override
        {
            const auto pos = getMouseXYRelative();
            const auto tracks = padTracks (evs.edit);
            for (int i = 0; i < tracks.size(); ++i)
                if (mutePad (i, tracks.size()).contains (pos))
                    return "Mutes and unmutes " + tracks[i]->getName() + ". Right-click: solo, or learn a button of your controller for this mute";
            for (int i = 1; i <= F::numFills; ++i)
                if (fillPad (i).contains (pos))
                    return "Fill: while this is held, the last " + F::fillNames[i] + " note of the whole mix repeats in time with the song. Shift-click keeps it on. Right-click: learn a control of your controller (a knob chooses between the fills, a button gives the shortest)";
            return "Performance: mutes for every track, fills to hold, and a sweep filter over the whole mix. The fills and the sweep are the Perform device on the master";
        }

        void mouseDown (const MouseEvent& e) override
        {
            const auto tracks = padTracks (evs.edit);
            for (int i = 0; i < tracks.size(); ++i)
                if (mutePad (i, tracks.size()).contains (e.getPosition()))
                {
                    if (e.mods.isPopupMenu()) showMuteMenu (*tracks[i]); else pressMute (i);
                    return;
                }
            for (int i = 1; i <= F::numFills; ++i)
                if (fillPad (i).contains (e.getPosition()))
                {
                    if (e.mods.isPopupMenu())
                    {
                        PopupMenu m;
                        m.addSectionHeader ("Fill");
                        if (evs.midiLearn != nullptr) evs.midiLearn->addToMenu (m, *pf.param (F::pFill));
                        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
                    }
                    else if (e.mods.isShiftDown()) latchFill (i);
                    else { holdFill (i); holding = true; }
                    return;
                }
        }
        void mouseUp (const MouseEvent&) override { if (holding) { holding = false; holdFill (0); } }

        Rectangle<int> mutePad (int index, int count) const
        {
            auto a = mutes.withTrimmedTop (18);
            const int rows = count > 16 ? 3 : count > 8 ? 2 : 1, cols = jmax (1, (count + rows - 1) / rows);
            const int w = jmin (190, a.getWidth() / cols), h = a.getHeight() / rows;
            return Rectangle<int> (a.getX() + (index % cols) * w, a.getY() + (index / cols) * h, w, h).reduced (3);
        }
        Rectangle<int> fillPad (int fill) const
        {
            auto a = fills.withTrimmedTop (18);
            const int w = a.getWidth() / 2, h = a.getHeight() / 2;
            return Rectangle<int> (a.getX() + ((fill - 1) % 2) * w, a.getY() + ((fill - 1) / 2) * h, w, h).reduced (3);
        }

    private:
        void showMuteMenu (te::AudioTrack& t)
        {
            PopupMenu m;
            m.addSectionHeader (t.getName());
            te::Track::Ptr tp (&t);
            m.addItem (t.isSolo (false) ? "Unsolo" : "Solo", [this, tp] { EngineHelpers::toggleSolo (*tp); repaint(); });
            if (auto* ml = evs.midiLearn)
            {
                m.addSeparator();
                m.addItem (ml->getPage() > 0 ? "Learn a controller button for this mute (on " + ml->pageName (ml->getPage()) + ")..." : String ("Learn a controller button for this mute..."),
                           [ml, tp] { ml->startLearningMute (*tp); });
                if (ml->isMuteMapped (t)) m.addItem ("Forget the button (" + ml->muteMappingText (t) + ")", [this, ml, tp] { ml->removeMuteMapping (*tp); repaint(); });
                if (ml->onShowMappings) m.addItem ("Controller mappings and pages...", [ml] { if (ml->onShowMappings) ml->onShowMappings(); });
            }
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
        }

        void timerCallback() override
        {
            if (! isShowing()) return;
            // drawn again only when what it shows has changed: the tracks, their mutes and solos, the fill
            double sig = pf.getInt (F::pFill) * 3.0 + latched;
            int i = 0;
            for (auto* t : padTracks (evs.edit))
            {
                sig += (double) t->getName().hashCode() * (++i) + (t->isMuted (false) ? 7.0 : 0.0) * i + (t->isSolo (false) ? 13.0 : 0.0) * i + (double) t->getColour().getARGB();
                if (evs.midiLearn != nullptr && evs.midiLearn->isMuteMapped (*t)) sig += 0.5 * i;
            }
            if (sig != lastSig) { lastSig = sig; repaint(); }
        }

        EditViewState& evs;
        F& pf;
        te::Plugin::Ptr holder;
        PerformView view;
        Rectangle<int> mutes, fills;
        int latched = 0;
        bool holding = false;
        double lastSig = -1.0;
    };

    //==============================================================================
    struct Window : public DocumentWindow, private Timer
    {
        Window (EditViewState& evs, pulse::Perform& f) : DocumentWindow ("Performance", Pro::bg(), DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            auto* s = new Stage (evs, f);
            s->setSize (1040, 600);
            setContentOwned (s, true);
            setResizable (true, false);
            setResizeLimits (640, 380, 2600, 1800);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            startTimer (500);
        }
        Stage& stage() { return *dynamic_cast<Stage*> (getContentComponent()); }
        void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }
        void timerCallback() override { if (! stage().deviceStillThere()) closeButtonPressed(); }   // the device was taken off the master (an undo, say)
    };

    inline Component::SafePointer<Window>& openWindow()
    {
        static Component::SafePointer<Window> w;
        return w;
    }

    /** Opens the Performance window (putting a Perform device on the master if it has none). */
    inline void show (EditViewState& evs)
    {
        auto& w = openWindow();
        if (w == nullptr)
            if (auto* f = device (evs.edit, true)) w = new Window (evs, *f);
        if (w != nullptr) w->toFront (true);
    }

    /** Closes the window (before the project goes away). */
    inline void close() { delete openWindow().getComponent(); }
}
