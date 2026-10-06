#pragma once

#include "DeviceDisplays.h"
#include "FactoryPresets.h"
#include "DeviceViews.h"
#include "Presets.h"
#include "WavetableModUI.h"
#include "ProDisplays.h"

/** Big device editor windows for Pulse devices: large, detailed graphs (all of a device's views at
    once) above every control, in sections you can fold. Resizable; scrolls when there's a lot. */
namespace DeviceEditors
{
    //==============================================================================
    /** Scrolling history of a value (e.g. gain reduction), newest on the right. */
    class HistoryGraph : public Component, private Timer
    {
    public:
        HistoryGraph (std::function<float()> v, float maxValue, String unitLabel, Colour c)
            : value (std::move (v)), maxV (maxValue), unit (std::move (unitLabel)), colour (c)
        {
            history.assign (240, 0.0f);
            startTimerHz (30);
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            auto a = b.reduced (34.0f, 10.0f).withTrimmedLeft (-26.0f);
            g.setFont (FontOptions (9.5f));
            for (int i = 0; i <= 4; ++i)
            {
                const float y = a.getY() + a.getHeight() * i / 4.0f;
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.drawHorizontalLine ((int) y, a.getX(), a.getRight());
                g.setColour (Theme::muted.withAlpha (0.7f));
                g.drawText ("-" + String (maxV * i / 4.0f, 0) + " " + unit, Rectangle<float> (b.getX() + 2.0f, y - 6.0f, 34.0f, 12.0f), Justification::centredLeft);
            }
            Path p, fill;
            const int n = (int) history.size();
            for (int i = 0; i < n; ++i)
            {
                const float v = history[(size_t) ((head + i) % n)];
                const float x = a.getX() + a.getWidth() * i / (float) (n - 1), y = a.getY() + a.getHeight() * jlimit (0.0f, 1.0f, v / maxV);
                if (i == 0) { p.startNewSubPath (x, y); fill.startNewSubPath (x, a.getY()); fill.lineTo (x, y); }
                else { p.lineTo (x, y); fill.lineTo (x, y); }
            }
            fill.lineTo (a.getRight(), a.getY());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.25f));
            g.fillPath (fill);
            g.setColour (colour);
            g.strokePath (p, PathStrokeType (1.6f));
            g.setColour (Theme::text);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText (String (history[(size_t) ((head + n - 1) % n)], 1) + " " + unit, a.withHeight (16.0f), Justification::topRight);
        }

    private:
        void timerCallback() override
        {
            history[(size_t) head] = value ? value() : 0.0f;
            head = (head + 1) % (int) history.size();
            repaint();
        }
        std::function<float()> value;
        float maxV;
        String unit;
        Colour colour;
        std::vector<float> history;
        int head = 0;
    };

    //==============================================================================
    /** All 16 frames of an oscillator's wavetable stacked in perspective; the frame Position plays
        is highlighted. */
    class FramesView : public Component, private Timer
    {
    public:
        using W = pulse::WavetableSynth;
        FramesView (W& s, int oscIndex) : holder (&s), synth (&s), osc (oscIndex), colour (oscIndex == 0 ? Theme::accent : Theme::orange)
        {
            setMouseCursor (MouseCursor::UpDownResizeCursor);
            startTimerHz (15);
        }
        Colour colour;
        bool flat = false;   // true: the deep look of the Wavetable panel

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            if (flat)
            {
                g.setColour (Pro::deep());
                g.fillRoundedRectangle (b, 6.0f);
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.drawRoundedRectangle (b, 6.0f, 1.0f);
            }
            else Graphs::panel (g, b);
            const int set = synth->getInt (osc == 0 ? W::pSet1 : W::pSet2);
            const float pos = posParam()->getCurrentValue();
            const auto user = synth->getUserTable();
            auto a = b.reduced (14.0f, flat ? 14.0f : 22.0f);
            const int frames = pulse::WavetableBank::numFrames;
            const float depthX = a.getWidth() * 0.22f, depthY = a.getHeight() * 0.52f;
            const float w = a.getWidth() - depthX, h = a.getHeight() - depthY;
            const int current = roundToInt (pos * (frames - 1));
            auto frame = [&] (int f, Path& p, float& baseY, float& x0, float& x1)
            {
                const float t01 = f / (float) (frames - 1);
                const float ox = a.getX() + depthX * (1.0f - t01), oy = a.getY() + depthY * (1.0f - t01);
                const float* t = synth->tableFor (set, f, 0, user.get());
                for (int i = 0; i <= 96; ++i)
                {
                    const float v = t[jmin (pulse::WavetableBank::tableSize - 1, i * pulse::WavetableBank::tableSize / 96)];
                    const float x = ox + w * i / 96.0f, y = oy + h * 0.5f - v * h * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                baseY = oy + h * 0.5f; x0 = ox; x1 = ox + w;
            };
            // back to front, each frame a solid ridge: nearer frames hide the ones behind them
            const Colour ground = flat ? Pro::deep() : Theme::well;
            for (int f = frames; --f >= 0;)
            {
                Path p; float by, x0, x1;
                frame (f, p, by, x0, x1);
                Path solid (p);
                solid.lineTo (x1, by + h * 0.5f); solid.lineTo (x0, by + h * 0.5f); solid.closeSubPath();
                const bool hot = f == current;
                const float near = 1.0f - f / (float) (frames - 1);   // 1 at the front
                g.setColour (hot ? ground.interpolatedWith (colour, 0.28f) : ground.interpolatedWith (colour, 0.03f + 0.07f * near));
                g.fillPath (solid);
                if (hot) Graphs::glowStroke (g, p, colour, 2.0f);
                else
                {
                    g.setColour (colour.withMultipliedSaturation (0.7f).withAlpha (0.18f + 0.34f * near));
                    g.strokePath (p, PathStrokeType (1.0f));
                }
            }
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.5f, Font::bold));
            const String info = "Frame " + String (current + 1) + " / " + String (frames);
            if (flat) g.drawText (info, b.reduced (8.0f, 5.0f).removeFromBottom (12.0f), Justification::centredRight);
            else g.drawText ("OSC " + String (osc + 1) + "  " + pulse::WavetableBank::setNames[set] + "   position " + String (roundToInt (pos * 100)) + "%",
                             b.reduced (8.0f, 4.0f).removeFromTop (14.0f), Justification::centredLeft);
        }

        // drag up / down (or the wheel): the position
        void mouseDown (const MouseEvent&) override
        {
            synth->edit.getUndoManager().beginNewTransaction ("Wavetable position");
            start = posParam()->getCurrentBaseValue();
            Graphs::begin ({ posParam() });
        }
        void mouseDrag (const MouseEvent& e) override { Graphs::setValue (posParam(), start - e.getDistanceFromDragStartY() / (float) jmax (60, getHeight()) * (e.mods.isShiftDown() ? 0.25f : 1.0f)); }
        void mouseUp (const MouseEvent&) override     { Graphs::end ({ posParam() }); }
        void mouseDoubleClick (const MouseEvent&) override { Graphs::reset (posParam()); }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wd) override
        {
            Graphs::setValue (posParam(), posParam()->getCurrentBaseValue() + (wd.deltaY > 0 ? 1.0f : -1.0f) / (float) (pulse::WavetableBank::numFrames - 1));
        }

    private:
        Graphs::Param posParam() const { return synth->param (osc == 0 ? W::pPos1 : W::pPos2); }
        void timerCallback() override
        {
            const double sig = synth->get (W::pSet1) * 3 + synth->param (W::pPos1)->getCurrentValue() * 7 + synth->get (W::pSet2) * 11 + synth->param (W::pPos2)->getCurrentValue() * 13
                               + (double) (pointer_sized_int) synth->getUserTable().get();
            if (sig != last) { last = sig; repaint(); }
        }
        te::Plugin::Ptr holder;
        W* synth;
        int osc;
        double last = -1;
        float start = 0;
    };

    /** The Wavetable's output: a scope that holds still on the waveform, and a stereo meter. */
    class SynthScope : public Component, private Timer
    {
    public:
        using W = pulse::WavetableSynth;
        explicit SynthScope (W& s) : holder (&s), synth (s) { startTimerHz (30); }
        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Pro::deep());
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            auto meters = b.removeFromRight (22.0f).reduced (4.0f, 6.0f);
            for (int c = 0; c < 2; ++c)
            {
                auto m = meters.removeFromLeft (6.0f);
                meters.removeFromLeft (2.0f);
                const float db = Decibels::gainToDecibels (synth.outPeak[(size_t) c].load(), -60.0f);
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.fillRoundedRectangle (m, 2.0f);
                g.setColour (db > -0.5f ? Theme::red : db > -9.0f ? Theme::yellow : Theme::green);
                g.fillRoundedRectangle (m.withTop (m.getBottom() - jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * m.getHeight()), 2.0f);
            }
            auto a = b.reduced (8.0f, 6.0f);
            g.setColour (Theme::ink.withAlpha (0.07f));
            g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
            const int n = W::scopeSize, pos = synth.scopePos.load();
            float buf[W::scopeSize];
            for (int i = 0; i < n; ++i) buf[i] = synth.scope[(size_t) ((pos + i) % n)].load();
            int from = 0;
            for (int i = 1; i < n / 2; ++i)   // a rising zero crossing keeps the picture still
                if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) { from = i; break; }
            const int len = n / 2;
            Path p;
            for (int i = 0; i < len; ++i)
            {
                const float x = a.getX() + a.getWidth() * i / (float) (len - 1), y = a.getCentreY() - jlimit (-1.0f, 1.0f, buf[from + i]) * a.getHeight() * 0.48f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            Graphs::glowStroke (g, p, Colour (0xff748ffc), 1.4f);
        }
    private:
        void timerCallback() override { repaint(); }
        te::Plugin::Ptr holder;
        W& synth;
    };

    //==============================================================================
    /** Waveforms of a few drum hits with the current settings (kick, snare, hat, clap). */
    class DrumHitsView : public Component, private Timer
    {
    public:
        explicit DrumHitsView (pulse::PulsePlugin& p) : holder (&p), plugin (&p) { startTimerHz (8); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            const int types[] = { pulse::DrumVoice::kick, pulse::DrumVoice::snare, pulse::DrumVoice::closedHat, pulse::DrumVoice::clap };
            const Colour cols[] = { Theme::orange, Theme::accent, Colour (0xff69db7c), Theme::purple };
            auto a = b.reduced (8.0f);
            const float cw = a.getWidth() / 4.0f;
            for (int k = 0; k < 4; ++k)
            {
                auto cell = a.removeFromLeft (cw).reduced (4.0f, 0.0f);
                g.setColour (Theme::muted);
                g.setFont (FontOptions (10.5f, Font::bold));
                g.drawText (pulse::DrumVoice::typeNames()[types[k]].toUpperCase(), cell.removeFromTop (14.0f), Justification::centredLeft);
                pulse::DrumVoice v;
                v.start (48000.0, types[k], plugin->get (1), plugin->get (2), plugin->get (3), plugin->get (4), plugin->get (5), 1234u);
                const int n = 48000 / 2;
                Path p;
                const int cols2 = (int) cell.getWidth();
                std::vector<std::pair<float, float>> mm ((size_t) jmax (1, cols2), { 0.0f, 0.0f });
                for (int i = 0; i < n; ++i)
                {
                    const float x = v.next();
                    auto& m = mm[(size_t) jmin (cols2 - 1, i * cols2 / n)];
                    m.first = jmin (m.first, x); m.second = jmax (m.second, x);
                }
                g.setColour (cols[k]);
                for (int x = 0; x < cols2; ++x)
                    g.drawVerticalLine ((int) cell.getX() + x, cell.getCentreY() - mm[(size_t) x].second * cell.getHeight() * 0.45f,
                                        cell.getCentreY() - mm[(size_t) x].first * cell.getHeight() * 0.45f + 1.0f);
            }
        }

    private:
        void timerCallback() override
        {
            double sig = 0;
            for (int i = 0; i < plugin->numParams(); ++i) sig = sig * 1.3 + plugin->get (i);
            if (sig != last) { last = sig; repaint(); }
        }
        te::Plugin::Ptr holder;
        pulse::PulsePlugin* plugin;
        double last = -1;
    };

    //==============================================================================
    /** Every parameter as a bar with a draggable top (drag = turn the knob, double-click = default).
        The overview for devices without a dedicated graph. */
    class ParamBars : public Graphs::GraphBase
    {
    public:
        explicit ParamBars (pulse::PulsePlugin& p) : holder (&p)
        {
            for (int i = 0; i < p.numParams(); ++i)
                if (auto ap = p.param (i)) params.push_back (ap);
            setTooltip ("Drag a bar to set the parameter (double-click resets it)");
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            area = b.reduced (10.0f, 8.0f).withTrimmedBottom (Graphs::isLarge (*this) ? 30.0f : 4.0f).withTrimmedTop (6.0f);
            g.setColour (Theme::ink.withAlpha (0.05f));
            for (int i = 0; i <= 4; ++i) g.drawHorizontalLine ((int) (area.getY() + area.getHeight() * i / 4.0f), area.getX(), area.getRight());
            const int n = (int) params.size();
            for (int i = 0; i < n; ++i)
            {
                auto cell = column (i);
                const float v = Graphs::norm (params[(size_t) i]);
                const auto col = Theme::trackColour (i * 3);
                auto bar = cell.withSizeKeepingCentre (jmin (56.0f, cell.getWidth() - jmin (6.0f, cell.getWidth() * 0.2f) * 2.0f), cell.getHeight());
                auto fill = bar.withTop (area.getBottom() - v * area.getHeight());
                g.setGradientFill (ColourGradient (col.withAlpha (0.7f), 0.0f, fill.getY(), col.withAlpha (0.15f), 0.0f, area.getBottom(), false));
                g.fillRoundedRectangle (fill, 2.0f);
                Graphs::node (g, { bar.getCentreX(), fill.getY() }, col, hot == i || dragging == i);
                if (Graphs::isLarge (*this))
                {
                    auto name = params[(size_t) i]->getParameterShortName (jmax (4, (int) (cell.getWidth() / 6.0f)));
                    Graphs::label (g, name, { cell.getX() - 4.0f, area.getBottom() + 4.0f, cell.getWidth() + 8.0f, 12.0f }, Justification::centred);
                    Graphs::label (g, params[(size_t) i]->getCurrentValueAsString(), { cell.getX() - 4.0f, area.getBottom() + 16.0f, cell.getWidth() + 8.0f, 12.0f },
                                   Justification::centred, 0.5f);
                }
            }
            drawReadout (g);
        }

        void mouseMove (const MouseEvent& e) override { const int h = hit (e.position); if (h != hot) { hot = h; updateReadout(); repaint(); } }
        void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override
        {
            dragging = hit (e.position);
            if (dragging < 0) return;
            Graphs::begin ({ params[(size_t) dragging] });
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            Graphs::setNorm (params[(size_t) dragging], (area.getBottom() - e.position.y) / area.getHeight());
            updateReadout();
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (dragging >= 0) Graphs::end ({ params[(size_t) dragging] }); dragging = -1; }
        void mouseDoubleClick (const MouseEvent& e) override
        {
            const int h = hit (e.position);
            if (h < 0) return;
            params[(size_t) h]->getEdit().getUndoManager().beginNewTransaction ("Reset");
            Graphs::reset (params[(size_t) h]);
        }

    private:
        Rectangle<float> column (int i) const
        {
            const float w = area.getWidth() / (float) jmax (1, (int) params.size());
            return { area.getX() + w * i, area.getY(), w, area.getHeight() };
        }
        int hit (juce::Point<float> p) const
        {
            for (int i = 0; i < (int) params.size(); ++i)
                if (column (i).expanded (0.0f, 8.0f).contains (p)) return i;
            return -1;
        }
        void updateReadout()
        {
            const int h = dragging >= 0 ? dragging : hot;
            if (h < 0) { readout = {}; return; }
            readout = params[(size_t) h]->getParameterName() + "  " + params[(size_t) h]->getCurrentValueAsStringWithLabel();
            readoutPos = { column (h).getCentreX(), area.getBottom() - Graphs::norm (params[(size_t) h]) * area.getHeight() };
        }
        double signature() const override
        {
            double sig = 0;
            for (auto& ap : params) sig = sig * 1.31 + ap->getCurrentBaseValue();
            return sig;
        }

        te::Plugin::Ptr holder;
        std::vector<Graphs::Param> params;
        Rectangle<float> area;
    };

    //==============================================================================
    /** A value in a small box (octave, semitones, cents): drag up / down, wheel, double-click resets. */
    class ParamValueBox : public Component, public SettableTooltipClient, private Timer
    {
    public:
        te::AutomatableParameter& getParameter() const { return *param; }
        ParamValueBox (te::AutomatableParameter& p, const String& l, Colour c) : param (&p), label (l), colour (c)
        {
            setTooltip (p.getParameterName() + ": drag up or down (Shift for fine steps), or use the wheel; double-click to reset");
            setMouseCursor (MouseCursor::UpDownResizeCursor);
            startTimerHz (10);
        }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Pro::deep());
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (dragging ? colour : Theme::ink.withAlpha (0.1f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            auto top = r.removeFromLeft (r.getWidth() * 0.4f);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (9.5f, Font::bold));
            g.drawText (label.toUpperCase(), top, Justification::centred);
            g.setColour (colour.brighter (0.2f));
            g.setFont (FontOptions (12.0f, Font::bold));
            g.drawText (param->getCurrentValueAsString(), r, Justification::centred);
        }
        void mouseDown (const MouseEvent&) override
        {
            param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName());
            start = Graphs::norm (param);
            dragging = true;
            Graphs::begin ({ param });
        }
        void mouseDrag (const MouseEvent& e) override { Graphs::setNorm (param, start - e.getDistanceFromDragStartY() / (e.mods.isShiftDown() ? 600.0f : 160.0f)); repaint(); }
        void mouseUp (const MouseEvent&) override     { Graphs::end ({ param }); dragging = false; repaint(); }
        void mouseDoubleClick (const MouseEvent&) override { Graphs::reset (param); }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            const float step = param->valueRange.interval > 0.0f ? param->valueRange.interval : (param->valueRange.end - param->valueRange.start) / 100.0f;
            Graphs::setValue (param, param->getCurrentBaseValue() + (w.deltaY > 0 ? step : -step));
        }
    private:
        void timerCallback() override { repaint(); }
        te::AutomatableParameter::Ptr param;
        String label;
        Colour colour;
        float start = 0;
        bool dragging = false;
    };

    /** A picture of what one of the Wavetable's effects is doing. */
    class FxDisplay : public Component, private Timer
    {
    public:
        using W = pulse::WavetableSynth;
        enum Kind { distortion, phaser, chorus, delay, reverb, eq };
        FxDisplay (W& s, Kind k, Colour c) : holder (&s), synth (s), kind (k), colour (c) { startTimerHz (24); }

        bool active() const
        {
            switch (kind)
            {
                case distortion: return synth.get (W::pDrive) > 0.001f;
                case phaser:     return synth.get (W::pPhaserMix) > 0.001f;
                case chorus:     return synth.get (W::pChorusMix) > 0.001f;
                case delay:      return synth.get (W::pDelayMix) > 0.001f;
                case reverb:     return synth.get (W::pReverbMix) > 0.001f;
                default:         return std::abs (synth.get (W::pEqLow)) > 0.05f || std::abs (synth.get (W::pEqHigh)) > 0.05f;
            }
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (Pro::deep());
            g.fillRoundedRectangle (b, 6.0f);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawRoundedRectangle (b, 6.0f, 1.0f);
            const bool on = active();
            const Colour c = on ? colour : Theme::muted.withAlpha (0.5f);
            auto a = b.reduced (12.0f, 10.0f);
            g.setColour (on ? colour : Theme::muted.withAlpha (0.4f));
            g.fillEllipse (b.getRight() - 14.0f, b.getY() + 7.0f, 7.0f, 7.0f);
            Graphs::label (g, on ? "ON" : kind == eq ? "FLAT" : kind == distortion ? "OFF (turn up the drive)" : "OFF (turn up its mix)", Rectangle<float> (b.getRight() - 150.0f, b.getY() + 4.0f, 132.0f, 12.0f), Justification::centredRight, 0.7f);
            const double now = Time::getMillisecondCounterHiRes() * 0.001;
            Path p;
            auto plot = [&] (std::function<float (float)> f, int steps = 160)   // f: 0..1 -> -1..1
            {
                p.clear();
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = i / (float) steps, y = a.getCentreY() - jlimit (-1.0f, 1.0f, f (x)) * a.getHeight() * 0.5f;
                    if (i == 0) p.startNewSubPath (a.getX(), y); else p.lineTo (a.getX() + x * a.getWidth(), y);
                }
            };
            g.setColour (Theme::ink.withAlpha (0.07f));
            g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
            switch (kind)
            {
                case distortion:
                {
                    const float drive = synth.get (W::pDrive), dg = 1.0f + drive * 12.0f, comp = 1.0f / std::tanh (dg);
                    const int type = synth.getInt (W::pDriveType);
                    g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());
                    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
                    plot ([&] (float x)
                    {
                        const float in = x * 2.0f - 1.0f;
                        switch (type)
                        {
                            case 1:  return jlimit (-1.0f, 1.0f, in * dg) * (0.6f + 0.4f * comp);
                            case 2:  return std::sin (in * dg * MathConstants<float>::halfPi);
                            case 3:  { const float steps = std::pow (2.0f, 7.0f - drive * 5.5f); return std::round (std::tanh (in * (1.0f + drive * 3.0f)) * steps) / steps; }
                            default: return std::tanh (in * dg) * comp;
                        }
                    }, 300);
                    Graphs::glowStroke (g, p, c, 2.0f);
                    break;
                }
                case phaser:
                {
                    const float depth = synth.get (W::pPhaserDepth), mix = synth.get (W::pPhaserMix);
                    const float m = 0.5f + 0.5f * (float) std::sin (now * synth.get (W::pPhaserRate) * MathConstants<double>::twoPi);
                    const float fc = 200.0f * std::pow (2.0f, (0.5f + (m - 0.5f) * depth) * 5.0f);
                    plot ([&] (float x)
                    {
                        const float f = 20.0f * std::pow (1000.0f, x);
                        const float phi = -2.0f * std::atan (f / fc) * 6.0f;
                        const float mag = std::sqrt (square (1.0f + mix * (std::cos (phi) - 1.0f) * 0.5f) + square (mix * std::sin (phi) * 0.5f));
                        return jlimit (-1.0f, 1.0f, Decibels::gainToDecibels (mag, -30.0f) / 18.0f + 0.5f);
                    }, 300);
                    Graphs::glowStroke (g, p, c, 1.8f);
                    break;
                }
                case chorus:
                {
                    const float depth = synth.get (W::pChorusDepth), rate = synth.get (W::pChorusRate);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        plot ([&] (float x) { return (ch == 0 ? 1.0f : -1.0f) * depth * 0.8f * (float) std::sin ((x * 2.0 + now * rate) * MathConstants<double>::twoPi); });
                        Graphs::glowStroke (g, p, ch == 0 ? c : c.withRotatedHue (0.08f), 1.6f);
                    }
                    break;
                }
                case delay:
                {
                    const float fb = synth.get (W::pDelayFb);
                    const double beats = W::delayBeats[jlimit (0, 5, synth.getInt (W::pDelayTime))];
                    const float gap = a.getWidth() * (float) (beats / 5.0);
                    g.setColour (Theme::ink.withAlpha (0.8f));
                    g.fillRect (Rectangle<float> (a.getX(), a.getY(), 2.5f, a.getHeight()));
                    float amp = 1.0f;
                    for (int k = 1; k < 40 && gap * k < a.getWidth() && amp > 0.02f; ++k, amp *= jmax (0.05f, fb))
                    {
                        const float h = amp * a.getHeight() * 0.5f, x = a.getX() + gap * k;
                        g.setColour (c.withAlpha (0.85f));
                        g.fillRoundedRectangle (k % 2 ? Rectangle<float> (x - 2.5f, a.getCentreY() - h, 5.0f, h) : Rectangle<float> (x - 2.5f, a.getCentreY(), 5.0f, h), 1.5f);
                    }
                    break;
                }
                case reverb:
                {
                    const float size = synth.get (W::pReverbSize);
                    Path fill;
                    fill.startNewSubPath (a.getX(), a.getBottom());
                    for (int i = 0; i <= 120; ++i)
                    {
                        const float x = i / 120.0f;
                        const float env = std::exp (-x * (7.0f - size * 6.2f));
                        fill.lineTo (a.getX() + x * a.getWidth(), a.getBottom() - env * a.getHeight() * (0.75f + 0.25f * std::sin (x * 90.0f + i)));
                    }
                    fill.lineTo (a.getRight(), a.getBottom());
                    fill.closeSubPath();
                    g.setGradientFill (ColourGradient (c.withAlpha (0.6f), 0.0f, a.getY(), c.withAlpha (0.05f), 0.0f, a.getBottom(), false));
                    g.fillPath (fill);
                    break;
                }
                default:
                {
                    const float gLo = Decibels::decibelsToGain (synth.get (W::pEqLow)) - 1.0f, gHi = Decibels::decibelsToGain (synth.get (W::pEqHigh)) - 1.0f;
                    plot ([&] (float x)
                    {
                        const float f = 20.0f * std::pow (1000.0f, x);
                        const std::complex<float> lp = 1.0f / std::complex<float> (1.0f, f / 220.0f), jw (0.0f, f / 4500.0f);
                        const std::complex<float> hp = jw / (1.0f + jw);
                        return Decibels::gainToDecibels (std::abs (1.0f + gLo * lp + gHi * hp), -30.0f) / 14.0f;
                    }, 200);
                    Graphs::glowStroke (g, p, c, 2.0f);
                    break;
                }
            }
        }
    private:
        void timerCallback() override { repaint(); }
        te::Plugin::Ptr holder;
        W& synth;
        Kind kind;
        Colour colour;
    };

    /** Where the modulation sources are right now (LFOs and macros), as moving bars. */
    class SourceMeters : public Component, private Timer
    {
    public:
        using W = pulse::WavetableSynth;
        explicit SourceMeters (W& s) : holder (&s), synth (s) { startTimerHz (30); }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const int n = W::numLfos + W::numMacros;
            const float rowH = jmin (34.0f, r.getHeight() / (float) n);
            for (int i = 0; i < n; ++i)
            {
                auto row = r.removeFromTop (rowH).reduced (0.0f, 4.0f);
                const bool isLfo = i < W::numLfos;
                const int source = isLfo ? i + 1 : 14 + (i - W::numLfos);
                const float v = isLfo ? synth.lfoShape (i, synth.lfoClockPhase (i), 0.5f) : synth.get (W::pMacro1 + (i - W::numLfos)) ;
                const auto c = WavetableUI::sourceColour (source);
                g.setColour (Theme::text.withAlpha (0.85f));
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText (W::sourceNames[source], row.removeFromLeft (66.0f), Justification::centredLeft);
                g.setColour (Pro::deep());
                g.fillRoundedRectangle (row, 3.0f);
                const float cx = isLfo ? row.getCentreX() : row.getX(), w = isLfo ? v * row.getWidth() * 0.5f : v * row.getWidth();
                g.setColour (c.withAlpha (0.8f));
                g.fillRoundedRectangle (Rectangle<float> (jmin (cx, cx + w), row.getY() + 2.0f, std::abs (w), row.getHeight() - 4.0f), 2.0f);
                if (isLfo) { g.setColour (Theme::ink.withAlpha (0.25f)); g.drawVerticalLine ((int) cx, row.getY(), row.getBottom()); }
            }
        }
    private:
        void timerCallback() override { repaint(); }
        te::Plugin::Ptr holder;
        W& synth;
    };

    /** A text tab with a line under it when it is the open one. */
    struct LineTab : public Button
    {
        Colour colour { Theme::orange };
        explicit LineTab (const String& t) : Button (t) {}
        void paintButton (Graphics& g, bool over, bool) override
        {
            const bool on = getToggleState();
            auto r = getLocalBounds().toFloat();
            g.setColour (on ? Theme::text : Theme::muted.withAlpha (over ? 1.0f : 0.75f));
            g.setFont (FontOptions (12.5f, Font::bold));
            g.drawText (getName(), r, Justification::centred);
            if (on)
            {
                g.setColour (colour);
                g.fillRoundedRectangle (r.removeFromBottom (2.5f).reduced (14.0f, 0.0f), 1.2f);
            }
        }
    };

    //==============================================================================
    /** The Wavetable's window. Main tab: oscillators, filters, envelopes and LFOs in three columns;
        then the effects rack, the modulation matrix and the wave drawing tab. Macros, the modulation
        sources (drag one onto any knob) and the output stay along the bottom. */
    class WavetablePanel : public Component, private Timer
    {
    public:
        using W = pulse::WavetableSynth;
        static inline int startTab = 0;   // the tab a new panel opens on

        /** A quiet panel with a small title; its header row can hold menus or tabs, and it can draw
            captioned wells behind groups of controls. */
        struct Card : public Component
        {
            struct Well { Rectangle<int> area; String name; };
            String title;
            Colour accent;
            std::vector<Well> wells;
            static constexpr int headerH = 30;

            Card (const String& t, Colour a) : title (t), accent (a) {}
            Rectangle<int> content() const { return getLocalBounds().reduced (10, 0).withTrimmedTop (headerH + 2).withTrimmedBottom (8); }
            Rectangle<int> header() const
            {
                const int tw = GlyphArrangement::getStringWidthInt (FontOptions (11.5f, Font::bold), title) + 44;
                return getLocalBounds().removeFromTop (headerH).withTrimmedLeft (tw).withTrimmedRight (8).reduced (0, 4);
            }
            void paint (Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setGradientFill (ColourGradient (Pro::cardTop(), 0.0f, r.getY(), Pro::cardBot(), 0.0f, r.getY() + 60.0f, false));
                g.fillRoundedRectangle (r, 9.0f);
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawRoundedRectangle (r.reduced (0.5f), 9.0f, 1.0f);
                g.setColour (accent);
                g.fillEllipse (r.getX() + 12.0f, r.getY() + 11.5f, 7.0f, 7.0f);
                g.setColour (accent.withAlpha (0.25f));
                g.fillEllipse (r.getX() + 9.5f, r.getY() + 9.0f, 12.0f, 12.0f);
                g.setColour (Theme::text.withAlpha (0.92f));
                g.setFont (FontOptions (11.5f, Font::bold).withKerningFactor (0.08f));
                g.drawText (title, Rectangle<float> (r.getX() + 27.0f, r.getY(), r.getWidth() - 30.0f, (float) headerH), Justification::centredLeft);
                for (auto& w : wells)
                {
                    auto a = w.area.toFloat();
                    g.setColour (Pro::shade (0.2f));
                    g.fillRoundedRectangle (a, 7.0f);
                    g.setColour (Theme::muted);
                    g.setFont (FontOptions (9.5f, Font::bold).withKerningFactor (0.1f));
                    g.drawText (w.name, a.reduced (9.0f, 4.0f).removeFromTop (11.0f), Justification::centredLeft);
                }
            }
        };

        WavetablePanel (EditViewState& e, W& w) : evs (e), synth (w), holder (&w), scope (w)
        {
            const auto oscA = Colour (0xff4dabf7), oscB = Colour (0xffffa94d), filt = Colour (0xffff6b6b), env = Colour (0xff69db7c),
                       lfo = Colour (0xffda77f2), voice = Colour (0xff748ffc), sub = Colour (0xff63e6be);
            using L = std::initializer_list<std::pair<int, const char*>>;

            const char* tabNames[] = { "SYNTH", "EFFECTS", "MATRIX", "DRAW WAVE" };
            for (int i = 0; i < 4; ++i)
            {
                auto* b = tabs.add (new LineTab (tabNames[i]));
                b->onClick = [this, i] { showTab (i); };
                addAndMakeVisible (b);
                addChildComponent (pages.add (new Component()));
            }
            auto card = [this] (int page, const String& t, Colour c) { auto* k = cards.add (new Card (t, c)); pages[page]->addAndMakeVisible (k); return k; };

            // ---- synth page
            cOscA = card (0, "OSC A", oscA); cOscB = card (0, "OSC B", oscB); cMix = card (0, "SUB  /  NOISE  /  UNISON", sub);
            cF1 = card (0, "FILTER 1", filt); cF2 = card (0, "FILTER 2", filt.withRotatedHue (0.05f)); cVoice = card (0, "VOICE", voice);
            cEnv = card (0, "ENVELOPES", env); cLfo = card (0, "LFO", lfo);

            framesA = std::make_unique<FramesView> (w, 0); framesA->flat = true; framesA->colour = oscA; cOscA->addAndMakeVisible (*framesA);
            framesB = std::make_unique<FramesView> (w, 1); framesB->flat = true; framesB->colour = oscB; cOscB->addAndMakeVisible (*framesB);
            tableA = combo (*cOscA, W::pSet1); warpA = combo (*cOscA, W::pWarp1);
            tableB = combo (*cOscB, W::pSet2); warpB = combo (*cOscB, W::pWarp2);
            for (auto [pi, l] : L { { W::pPos1, "Position" }, { W::pWarpAmt1, "Warp" }, { W::pLevel1, "Level" }, { W::pPan1, "Pan" } }) knobsA.add (knob (*cOscA, pi, l, oscA));
            for (auto [pi, l] : L { { W::pPos2, "Position" }, { W::pWarpAmt2, "Warp" }, { W::pLevel2, "Level" }, { W::pPan2, "Pan" } }) knobsB.add (knob (*cOscB, pi, l, oscB));
            for (auto [pi, l] : L { { W::pOct1, "Oct" }, { W::pSemi1, "Semi" }, { W::pFine1, "Fine" } }) boxesA.add (box (*cOscA, pi, l, oscA));
            for (auto [pi, l] : L { { W::pOct2, "Oct" }, { W::pSemi2, "Semi" }, { W::pFine2, "Fine" } }) boxesB.add (box (*cOscB, pi, l, oscB));

            subKnob = knob (*cMix, W::pSub, "Level", sub);
            subShape = combo (*cMix, W::pSubShape);
            subOct = new ParamChoice (*w.param (W::pSubOct), sub, false); owned.add (subOct); cMix->addAndMakeVisible (subOct);
            for (auto [pi, l] : L { { W::pNoise, "Level" }, { W::pNoiseColour, "Colour" } }) knobsNoise.add (knob (*cMix, pi, l, sub));
            for (auto [pi, l] : L { { W::pMix, "A / B" }, { W::pVoices, "Voices" }, { W::pDetune, "Detune" }, { W::pSpread, "Width" } }) knobsUni.add (knob (*cMix, pi, l, voice));

            graphF1 = std::make_unique<Graphs::FilterGraph> (w.param (W::pCutoff), w.param (W::pReso), w.param (W::pFilterType),
                                                             [] (int t) { const int m[] = { 1, 5, 2, 3, 4 }; return m[jlimit (0, 4, t)]; }, filt, w.param (W::pFilterEnv));
            cF1->addAndMakeVisible (*graphF1);
            typeF1 = combo (*cF1, W::pFilterType);
            for (auto [pi, l] : L { { W::pCutoff, "Cutoff" }, { W::pReso, "Reso" }, { W::pFilterEnv, "Env" }, { W::pFDrive, "Drive" }, { W::pKeyTrack, "Key" } })
                knobsF1.add (knob (*cF1, pi, l, filt));
            graphF2 = std::make_unique<Graphs::FilterGraph> (w.param (W::pF2Cutoff), w.param (W::pF2Reso), w.param (W::pF2Type),
                                                             [] (int t) { const int m[] = { 0, 1, 5, 2, 3, 4 }; return m[jlimit (0, 5, t)]; }, filt.withRotatedHue (0.05f), w.param (W::pF2Env));
            cF2->addAndMakeVisible (*graphF2);
            typeF2 = combo (*cF2, W::pF2Type); routing = combo (*cF2, W::pRouting);
            for (auto [pi, l] : L { { W::pF2Cutoff, "Cutoff" }, { W::pF2Reso, "Reso" }, { W::pF2Env, "Env" } }) knobsF2.add (knob (*cF2, pi, l, filt.withRotatedHue (0.05f)));

            voiceMode = new ParamChoice (*w.param (W::pMode), voice, false); owned.add (voiceMode); cVoice->addAndMakeVisible (voiceMode);
            for (auto [pi, l] : L { { W::pGlide, "Glide" }, { W::pBendRange, "Bend" }, { W::pPressure, "Pressure" }, { W::pSlide, "Slide" } }) knobsVoice.add (knob (*cVoice, pi, l, voice));

            using EG = Graphs::EnvelopeGraph;
            envGraph = std::make_unique<EG> (std::vector<EG::Env> { { w.param (W::pA), w.param (W::pD), w.param (W::pS), w.param (W::pR), env, "AMP" },
                                                                   { w.param (W::pFA), w.param (W::pFD), w.param (W::pFS), w.param (W::pFR), filt, "FILTER" },
                                                                   { w.param (W::pMA), w.param (W::pMD), w.param (W::pMS), w.param (W::pMR), lfo, "MOD" } });
            cEnv->addAndMakeVisible (*envGraph);
            const char* envNames[] = { "AMP", "FILTER", "MOD" };
            for (int i = 0; i < 3; ++i)
            {
                auto* b = envTabs.add (new LineTab (envNames[i]));
                b->colour = i == 0 ? env : i == 1 ? filt : lfo;
                b->onClick = [this, i] { showEnv (i); };
                cEnv->addAndMakeVisible (b);
            }
            lfoView = std::make_unique<WavetableUI::LfoView> (w);
            lfoView->onSelect = [this] (int l) { showLfo (l); };
            cLfo->addAndMakeVisible (*lfoView);
            for (auto [pi, l] : L { { W::pLfoPos, "LFO 1 > Position" }, { W::pLfoCut, "LFO 1 > Cutoff" } }) knobsLfo1.add (knob (*cLfo, pi, l, WavetableUI::sourceColour (1)));

            // ---- effects page
            struct FxDef { const char* name; FxDisplay::Kind kind; Colour c; std::vector<std::pair<int, const char*>> knobs; int comboParam; int choiceParam; };
            const FxDef defs[] = {
                { "DISTORTION", FxDisplay::distortion, Colour (0xffff922b), { { W::pDrive, "Drive" } }, -1, W::pDriveType },
                { "PHASER", FxDisplay::phaser, Colour (0xffda77f2), { { W::pPhaserMix, "Mix" }, { W::pPhaserRate, "Rate" }, { W::pPhaserDepth, "Depth" } }, -1, -1 },
                { "CHORUS", FxDisplay::chorus, Colour (0xff4dabf7), { { W::pChorusMix, "Mix" }, { W::pChorusRate, "Rate" }, { W::pChorusDepth, "Depth" } }, -1, -1 },
                { "DELAY", FxDisplay::delay, Colour (0xff38d9a9), { { W::pDelayMix, "Mix" }, { W::pDelayFb, "Feedback" } }, W::pDelayTime, -1 },
                { "REVERB", FxDisplay::reverb, Colour (0xffb197fc), { { W::pReverbMix, "Mix" }, { W::pReverbSize, "Size" } }, -1, -1 },
                { "EQ", FxDisplay::eq, Colour (0xffffd43b), { { W::pEqLow, "Low" }, { W::pEqHigh, "High" } }, -1, -1 } };
            for (auto& d : defs)
            {
                auto* fc = fx.add (new FxSlot());
                fc->card = card (1, d.name, d.c);
                fc->display = std::make_unique<FxDisplay> (w, d.kind, d.c);
                fc->card->addAndMakeVisible (*fc->display);
                if (d.choiceParam >= 0) { fc->top = new ParamChoice (*w.param (d.choiceParam), d.c, false); owned.add (fc->top); fc->card->addAndMakeVisible (fc->top); }
                if (d.comboParam >= 0) { fc->top = combo (*fc->card, d.comboParam); }
                for (auto [pi, l] : d.knobs) fc->knobs.add (knob (*fc->card, pi, l, d.c));
            }

            // ---- matrix page
            cMatrix = card (2, "MODULATION MATRIX", lfo.withRotatedHue (0.1f));
            matrix = std::make_unique<WavetableUI::MatrixView> (w, false);
            cMatrix->addAndMakeVisible (*matrix);
            cSources = card (2, "SOURCES NOW", lfo);
            meters = std::make_unique<SourceMeters> (w);
            cSources->addAndMakeVisible (*meters);

            // ---- draw page
            cUser = card (3, "YOUR OWN WAVETABLE", oscA);
            drawEditor = std::make_unique<DeviceDisplays::WaveDrawEditor> (w);
            cUser->addAndMakeVisible (*drawEditor);

            // ---- the bottom bar
            for (int m = 0; m < W::numMacros; ++m)
            {
                auto* k = new ParamKnob (evs, *w.param (W::pMacro1 + m), false);
                k->setCompact (WavetableUI::sourceColour (14 + m));
                macroKnobs.add (k);
                addAndMakeVisible (k);
                addAndMakeVisible (macroChips.add (new WavetableUI::SourceChip (14 + m)));
            }
            for (int s = 1; s < 14; ++s) addAndMakeVisible (chips.add (new WavetableUI::SourceChip (s)));
            levelKnob = std::make_unique<ParamKnob> (evs, *w.param (W::pLevel));
            levelKnob->setLabel ("Level");
            levelKnob->setCompact (voice);
            levelKnob->setTooltip ("Level of the voices, before the effects (it sets how hard the distortion is driven)");
            addAndMakeVisible (*levelKnob);
            outputKnob = std::make_unique<ParamKnob> (evs, *w.param (W::pOutput));
            outputKnob->setLabel ("Output");
            outputKnob->setCompact (voice);
            outputKnob->setTooltip ("Output volume, after the effects");
            addAndMakeVisible (*outputKnob);
            addAndMakeVisible (scope);
            showEnv (0);
            showLfo (0);
            showTab (startTab);
            startTimerHz (8);
        }

        int currentTab() const { return tab; }
        int numTabs() const { return tabs.size(); }
        void showTab (int t)
        {
            tab = jlimit (0, pages.size() - 1, t);
            for (int i = 0; i < pages.size(); ++i) { pages[i]->setVisible (i == tab); tabs[i]->setToggleState (i == tab, dontSendNotification); }
            resized();
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto bar = getLocalBounds().removeFromBottom (barH).toFloat();
            g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, bar.getY(), Pro::barBot(), 0.0f, bar.getBottom(), false));
            g.fillRect (bar);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawHorizontalLine ((int) bar.getY(), 0.0f, (float) getWidth());
            g.drawHorizontalLine (37, 8.0f, (float) getWidth() - 8.0f);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (9.5f, Font::bold).withKerningFactor (0.1f));
            auto cap = [&] (const String& t, int x) { g.drawText (t, Rectangle<float> ((float) x, bar.getY() + 5.0f, 400.0f, 11.0f), Justification::centredLeft); };
            cap ("MACROS", 14);
            if (! chips.isEmpty()) cap ("MODULATION SOURCES  -  DRAG ONE ONTO ANY KNOB", chips[0]->getX() + 2);
            cap ("OUTPUT    " + String (synth.activeVoices.load()) + (synth.activeVoices.load() == 1 ? " VOICE" : " VOICES"), levelKnob->getX() + 4);
        }

        /** Controls in a grid, all the same size, centred in their cells. */
        template <typename K>
        static void placeKnobs (Rectangle<int> area, const Array<K*>& ks, int cols = 0, int maxW = 84, int maxH = 86)
        {
            const int n = ks.size();
            if (n == 0) return;
            cols = cols > 0 ? cols : n;
            const int rows = (n + cols - 1) / cols;
            const int cw = area.getWidth() / cols, ch = area.getHeight() / rows;
            for (int i = 0; i < n; ++i)
                ks[i]->setBounds (Rectangle<int> (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * ch, cw, ch)
                                      .withSizeKeepingCentre (jmin (cw, maxW), jmin (ch, maxH)));
        }

        void resized() override
        {
            if (pages.isEmpty() || levelKnob == nullptr) return;
            auto r = getLocalBounds();
            // the bottom bar: macros, sources, level, scope
            auto bar = r.removeFromBottom (barH).withTrimmedTop (18).reduced (12, 5);
            for (int m = 0; m < macroKnobs.size(); ++m)
            {
                auto cell = bar.removeFromLeft (70);
                macroChips[m]->setBounds (cell.removeFromTop (20).reduced (1, 1));
                macroKnobs[m]->setBounds (cell);
                bar.removeFromLeft (4);
            }
            bar.removeFromLeft (14);
            scope.setBounds (bar.removeFromRight (jlimit (150, 300, bar.getWidth() / 4)));
            bar.removeFromRight (6);
            outputKnob->setBounds (bar.removeFromRight (70));
            levelKnob->setBounds (bar.removeFromRight (70));
            bar.removeFromRight (14);
            {
                const int perRow = (chips.size() + 1) / 2;
                auto grid = bar.withSizeKeepingCentre (jmin (bar.getWidth(), perRow * 92), jmin (bar.getHeight(), 60)).withX (bar.getX());
                const int cw = grid.getWidth() / perRow, ch = grid.getHeight() / 2;
                for (int i = 0; i < chips.size(); ++i)
                    chips[i]->setBounds (Rectangle<int> (grid.getX() + (i % perRow) * cw, grid.getY() + (i / perRow) * ch, cw, ch).reduced (2, 3));
            }

            // tabs
            auto top = r.removeFromTop (38).reduced (10, 2);
            for (auto* t : tabs) t->setBounds (top.removeFromLeft (jmin (130, top.getWidth() / 4)));
            r.removeFromTop (6);
            for (auto* p : pages) p->setBounds (r);
            const int gap = 8;
            auto area = Rectangle<int> (r.getWidth(), r.getHeight()).reduced (8, 0).withTrimmedBottom (8);

            if (tab == 0)
            {
                auto a = area;
                auto col1 = a.removeFromLeft (a.getWidth() * 46 / 100); a.removeFromLeft (gap);
                auto col2 = a.removeFromLeft ((a.getWidth() - gap) / 2); a.removeFromLeft (gap);
                auto col3 = a;
                const int stripH = jlimit (120, 150, area.getHeight() * 22 / 100);
                const int bigH = (area.getHeight() - stripH - 2 * gap) / 2;

                cOscA->setBounds (col1.removeFromTop (bigH)); col1.removeFromTop (gap);
                cOscB->setBounds (col1.removeFromTop (bigH)); col1.removeFromTop (gap);
                cMix->setBounds (col1);
                layoutOsc (*cOscA, *framesA, *tableA, *warpA, knobsA, boxesA);
                layoutOsc (*cOscB, *framesB, *tableB, *warpB, knobsB, boxesB);
                {
                    auto c = cMix->content();
                    cMix->wells.clear();
                    const int unit = (c.getWidth() - 12) / 8;
                    auto subArea = c.removeFromLeft (unit * 2); c.removeFromLeft (6);
                    auto noiseArea = c.removeFromLeft (unit * 2); c.removeFromLeft (6);
                    cMix->wells.push_back ({ subArea, "SUB" });
                    cMix->wells.push_back ({ noiseArea, "NOISE" });
                    cMix->wells.push_back ({ c, "UNISON / MIX" });
                    auto si = subArea.reduced (4, 0).withTrimmedTop (14).withTrimmedBottom (2);
                    auto opts = si.removeFromRight (si.getWidth() / 2);
                    subKnob->setBounds (si.withSizeKeepingCentre (jmin (84, si.getWidth()), jmin (86, si.getHeight())));
                    opts = opts.withSizeKeepingCentre (opts.getWidth(), jmin (62, opts.getHeight()));
                    subShape->setBounds (opts.removeFromTop (opts.getHeight() / 2).reduced (0, 2));
                    subOct->setBounds (opts.reduced (0, 2));
                    placeKnobs (noiseArea.reduced (4, 0).withTrimmedTop (14).withTrimmedBottom (2), knobsNoise);
                    placeKnobs (c.reduced (4, 0).withTrimmedTop (14).withTrimmedBottom (2), knobsUni);
                }

                auto layoutFilter = [] (Card& cd, Component& graph, Component& type, Component* extra, Array<ParamKnob*>& ks)
                {
                    auto head = cd.header();
                    if (extra != nullptr) { extra->setBounds (head.removeFromRight (jmin (128, head.getWidth() / 2 - 3))); head.removeFromRight (6); }
                    type.setBounds (head.removeFromRight (jmin (128, head.getWidth())));
                    auto c = cd.content();
                    placeKnobs (c.removeFromBottom (jmin (86, c.getHeight() * 45 / 100)), ks);
                    c.removeFromBottom (4);
                    graph.setBounds (c);
                };
                cF1->setBounds (col2.removeFromTop (bigH)); col2.removeFromTop (gap);
                cF2->setBounds (col2.removeFromTop (bigH)); col2.removeFromTop (gap);
                cVoice->setBounds (col2);
                layoutFilter (*cF1, *graphF1, *typeF1, nullptr, knobsF1);
                layoutFilter (*cF2, *graphF2, *typeF2, routing, knobsF2);
                {
                    auto head = cVoice->header();
                    voiceMode->setBounds (head.removeFromRight (jmin (150, head.getWidth())));
                    placeKnobs (cVoice->content(), knobsVoice);
                }

                cEnv->setBounds (col3.removeFromTop (bigH)); col3.removeFromTop (gap);
                cLfo->setBounds (col3);
                {
                    auto tabsArea = cEnv->header();
                    tabsArea = tabsArea.removeFromRight (jmin (200, tabsArea.getWidth()));
                    const int ew = tabsArea.getWidth() / 3;
                    for (auto* t : envTabs) t->setBounds (tabsArea.removeFromLeft (ew));
                    auto c = cEnv->content();
                    placeKnobs (c.removeFromBottom (jmin (86, c.getHeight() * 45 / 100)), envKnobs);
                    c.removeFromBottom (4);
                    envGraph->setBounds (c);
                }
                {
                    auto c = cLfo->content();
                    auto opts = c.removeFromBottom (48);
                    c.removeFromBottom (2);
                    auto row = c.removeFromBottom (86);
                    c.removeFromBottom (4);
                    lfoView->setBounds (c);
                    // the rate, and LFO 1's built-in depths (kept from earlier versions)
                    Array<ParamKnob*> rowKnobs;
                    if (lfoKnobs.size() == 3) rowKnobs.add (dynamic_cast<ParamKnob*> (lfoKnobs[0]));
                    rowKnobs.addArray (knobsLfo1);
                    rowKnobs.removeAllInstancesOf (nullptr);
                    placeKnobs (row, rowKnobs, 0, 104, 86);
                    if (lfoKnobs.size() == 3)
                    {
                        lfoKnobs[1]->setBounds (opts.removeFromLeft (opts.getWidth() / 2 - 3));
                        opts.removeFromLeft (6);
                        lfoKnobs[2]->setBounds (opts.withTrimmedTop (16).withHeight (28));
                    }
                }
            }
            else if (tab == 1)
            {
                // a rack: two columns of three, each with its picture on the left and its controls on the right
                const int cols = 2, rows = 3;
                const int cw = (area.getWidth() - gap * (cols - 1)) / cols, ch = (area.getHeight() - gap * (rows - 1)) / rows;
                for (int i = 0; i < fx.size(); ++i)
                {
                    auto* f = fx[i];
                    f->card->setBounds (area.getX() + (i % cols) * (cw + gap), area.getY() + (i / cols) * (ch + gap), cw, ch);
                    auto c = f->card->content();
                    auto right = c.removeFromRight (jmin (290, c.getWidth() * 48 / 100));
                    c.removeFromRight (8);
                    f->display->setBounds (c);
                    if (f->top != nullptr)
                    {
                        f->top->setBounds (right.removeFromTop (34).withSizeKeepingCentre (jmin (right.getWidth(), 250), 28));
                        right.removeFromTop (4);
                    }
                    placeKnobs (right, f->knobs, 0, 92, 96);
                }
            }
            else if (tab == 2)
            {
                auto a = area;
                cSources->setBounds (a.removeFromRight (jlimit (240, 340, a.getWidth() * 30 / 100))); a.removeFromRight (gap);
                cMatrix->setBounds (a);
                matrix->setBounds (cMatrix->content());
                meters->setBounds (cSources->content());
            }
            else
            {
                cUser->setBounds (area);
                drawEditor->setBounds (cUser->content());
            }
        }

    private:
        struct FxSlot
        {
            Card* card = nullptr;
            std::unique_ptr<FxDisplay> display;
            Component* top = nullptr;
            Array<ParamKnob*> knobs;
        };

        void timerCallback() override { repaint (getLocalBounds().removeFromBottom (barH).removeFromTop (18)); }

        ParamKnob* knob (Card& c, int pi, const char* label, Colour col)
        {
            auto* k = new ParamKnob (evs, *synth.param (pi));
            k->setLabel (label);
            k->setCompact (col);
            owned.add (k);
            c.addAndMakeVisible (k);
            return k;
        }
        ParamValueBox* box (Card& c, int pi, const char* label, Colour col)
        {
            auto* b = new ParamValueBox (*synth.param (pi), label, col);
            owned.add (b);
            c.addAndMakeVisible (b);
            return b;
        }
        ParamCombo* combo (Card& c, int pi)
        {
            auto* cb = new ParamCombo (*synth.param (pi), false);
            owned.add (cb);
            c.addAndMakeVisible (cb);
            return cb;
        }
        /** Menus in the header; the wavetable with the pitch boxes under it on the left; a 2 x 2 of knobs on the right. */
        void layoutOsc (Card& c, Component& frames, Component& table, Component& warp, Array<ParamKnob*>& ks, Array<ParamValueBox*>& boxes)
        {
            auto head = c.header();
            warp.setBounds (head.removeFromRight (jmin (130, head.getWidth() / 2 - 3)));
            head.removeFromRight (6);
            table.setBounds (head.removeFromRight (jmin (170, head.getWidth())));
            auto r = c.content();
            placeKnobs (r.removeFromRight (jlimit (130, 180, r.getWidth() * 32 / 100)), ks, 2);
            r.removeFromRight (6);
            auto row = r.removeFromBottom (26);
            r.removeFromBottom (5);
            const int bw = (row.getWidth() - 8) / 3;
            for (auto* b : boxes) { b->setBounds (row.removeFromLeft (bw)); row.removeFromLeft (4); }
            frames.setBounds (r);
        }
        void showEnv (int i)
        {
            for (int k = 0; k < envTabs.size(); ++k) envTabs[k]->setToggleState (k == i, dontSendNotification);
            for (auto* k : envKnobs) owned.removeObject (k);
            envKnobs.clear();
            const int sets[3][4] = { { W::pA, W::pD, W::pS, W::pR }, { W::pFA, W::pFD, W::pFS, W::pFR }, { W::pMA, W::pMD, W::pMS, W::pMR } };
            const Colour cols[] = { Colour (0xff69db7c), Colour (0xffff6b6b), Colour (0xffda77f2) };
            const char* names[] = { "Attack", "Decay", "Sustain", "Release" };
            for (int k = 0; k < 4; ++k) envKnobs.add (knob (*cEnv, sets[i][k], names[k], cols[i]));
            resized();
        }
        void showLfo (int l)
        {
            for (auto* k : lfoKnobs) owned.removeObject (k);
            lfoKnobs.clear();
            const auto col = WavetableUI::sourceColour (l + 1);
            cLfo->title = "LFO " + String (l + 1);
            cLfo->accent = col;
            cLfo->repaint();
            lfoKnobs.add (knob (*cLfo, synth.lfoParam (l, 0), "Rate", col));
            auto* sync = new ParamCombo (*synth.param (synth.lfoParam (l, 1)), true);
            sync->setLabel ("Sync to tempo");
            owned.add (sync); cLfo->addAndMakeVisible (sync); lfoKnobs.add (sync);
            auto* retrig = new ParamToggle (*synth.param (synth.lfoParam (l, 3)), "Restart on each note", col);
            owned.add (retrig); cLfo->addAndMakeVisible (retrig); lfoKnobs.add (retrig);
            resized();
        }

        static constexpr int barH = 112;
        EditViewState& evs;
        W& synth;
        te::Plugin::Ptr holder;
        SynthScope scope;
        OwnedArray<LineTab> tabs;
        OwnedArray<Component> pages;
        OwnedArray<Card> cards;
        OwnedArray<Component> owned;
        OwnedArray<WavetableUI::SourceChip> chips, macroChips;
        OwnedArray<ParamKnob> macroKnobs;
        std::unique_ptr<ParamKnob> levelKnob, outputKnob;
        OwnedArray<FxSlot> fx;
        Card *cOscA = nullptr, *cOscB = nullptr, *cMix = nullptr, *cF1 = nullptr, *cF2 = nullptr, *cEnv = nullptr, *cLfo = nullptr,
             *cMatrix = nullptr, *cSources = nullptr, *cVoice = nullptr, *cUser = nullptr;
        std::unique_ptr<FramesView> framesA, framesB;
        ParamCombo *tableA = nullptr, *warpA = nullptr, *tableB = nullptr, *warpB = nullptr, *typeF1 = nullptr, *typeF2 = nullptr, *routing = nullptr, *subShape = nullptr;
        ParamChoice *subOct = nullptr, *voiceMode = nullptr;
        ParamKnob* subKnob = nullptr;
        std::unique_ptr<Graphs::FilterGraph> graphF1, graphF2;
        std::unique_ptr<Graphs::EnvelopeGraph> envGraph;
        std::unique_ptr<WavetableUI::LfoView> lfoView;
        std::unique_ptr<WavetableUI::MatrixView> matrix;
        std::unique_ptr<SourceMeters> meters;
        std::unique_ptr<DeviceDisplays::WaveDrawEditor> drawEditor;
        Array<ParamKnob*> knobsA, knobsB, knobsNoise, knobsUni, knobsF1, knobsF2, envKnobs, knobsVoice, knobsLfo1;
        Array<ParamValueBox*> boxesA, boxesB;
        Array<Component*> lfoKnobs;
        OwnedArray<LineTab> envTabs;
        int tab = 0;
    };

    //==============================================================================
    /** The FM synth's window: the algorithm as a diagram, the wave it makes, the four operators
        (each with its envelope to drag), the filter with its envelope, and the output. */
    class FMPanel : public Component
    {
    public:
        using F = pulse::FMSynth;
        using Card = WavetablePanel::Card;
        static Colour opColour (int op)
        {
            const Colour c[] = { Colour (0xffffa94d), Colour (0xff4dabf7), Colour (0xff69db7c), Colour (0xffda77f2) };
            return c[jlimit (0, 3, op)];
        }

        /** The algorithm: who modulates whom (carriers, which you hear, sit at the bottom). */
        class AlgoView : public Component, private Timer
        {
        public:
            explicit AlgoView (F& f) : holder (&f), fm (f) { startTimerHz (10); }
            void paint (Graphics& g) override
            {
                auto b = getLocalBounds().toFloat().reduced (1.0f);
                g.setColour (Pro::deep());
                g.fillRoundedRectangle (b, 6.0f);
                const auto& alg = F::algorithms()[(size_t) jlimit (0, (int) F::algorithms().size() - 1, fm.getInt (F::pAlgorithm))];
                int depth[4] = { 0, 0, 0, 0 };
                for (int pass = 0; pass < 4; ++pass)
                    for (auto [from, to] : alg.mods) depth[from] = jmax (depth[from], depth[to] + 1);
                int perRow[4] = { 0, 0, 0, 0 }, idx[4], maxDepth = 0;
                for (int i = 0; i < 4; ++i) { idx[i] = perRow[depth[i]]++; maxDepth = jmax (maxDepth, depth[i]); }
                auto a = b.reduced (16.0f, 26.0f);
                const float bw = jmin (96.0f, a.getWidth() / 4.2f), bh = jmin (46.0f, a.getHeight() / 5.0f);
                auto pos = [&] (int op)
                {
                    const float rowY = a.getBottom() - bh * 0.5f - (maxDepth > 0 ? depth[op] * (a.getHeight() - bh) / (float) jmax (1, maxDepth) : (a.getHeight() - bh) * 0.5f);
                    const float x = a.getX() + a.getWidth() * (idx[op] + 1.0f) / (perRow[depth[op]] + 1.0f);
                    return juce::Point<float> (x, rowY);
                };
                for (auto [from, to] : alg.mods)
                {
                    const auto p0 = pos (from), p1 = pos (to);
                    Path line;
                    line.startNewSubPath (p0.x, p0.y + bh * 0.5f);
                    line.cubicTo (p0.x, (p0.y + p1.y) * 0.5f, p1.x, (p0.y + p1.y) * 0.5f, p1.x, p1.y - bh * 0.5f);
                    g.setColour (opColour (from).withAlpha (0.25f + 0.6f * fm.get (F::opParam (from, F::kLevel))));
                    g.strokePath (line, PathStrokeType (2.0f));
                }
                if (fm.get (F::pFeedback) > 0.001f)   // operator 4 feeds itself
                {
                    const auto c = pos (3);
                    Path loop;
                    loop.addCentredArc (c.x + bw * 0.5f, c.y - bh * 0.2f, 12.0f, 12.0f, 0.0f, -2.2f, 2.2f, true);
                    g.setColour (opColour (3).withAlpha (0.3f + 0.7f * fm.get (F::pFeedback)));
                    g.strokePath (loop, PathStrokeType (2.0f));
                }
                for (int op = 0; op < 4; ++op)
                {
                    const auto c = pos (op);
                    const bool carrier = std::find (alg.outs.begin(), alg.outs.end(), op) != alg.outs.end();
                    auto box = Rectangle<float> (bw, bh).withCentre (c);
                    const auto col = opColour (op);
                    g.setColour (Pro::cardTop());
                    g.fillRoundedRectangle (box, 7.0f);
                    g.setColour (col.withAlpha (0.12f + 0.3f * fm.get (F::opParam (op, F::kLevel))));
                    g.fillRoundedRectangle (box, 7.0f);
                    g.setColour (col);
                    g.drawRoundedRectangle (box, 7.0f, carrier ? 2.0f : 1.2f);
                    g.setColour (Theme::text);
                    g.setFont (FontOptions (12.0f, Font::bold));
                    g.drawText ("OP " + String (op + 1), box.withTrimmedBottom (bh * 0.42f), Justification::centred);
                    g.setColour (Theme::muted);
                    g.setFont (FontOptions (10.0f));
                    g.drawText (fm.param (F::opParam (op, F::kRatio))->getCurrentValueAsString() + (carrier ? "  out" : ""), box.withTrimmedTop (bh * 0.5f), Justification::centred);
                    if (carrier)
                    {
                        g.setColour (col.withAlpha (0.6f));
                        g.drawLine (c.x, box.getBottom(), c.x, b.getBottom() - 8.0f, 2.0f);
                    }
                }
                Graphs::label (g, "MODULATORS ABOVE, CARRIERS (HEARD) BELOW", b.reduced (10.0f, 6.0f).removeFromTop (12.0f), Justification::centredLeft, 0.6f);
            }
        private:
            void timerCallback() override { repaint(); }
            te::Plugin::Ptr holder;
            F& fm;
        };

        /** Two cycles of the wave the current settings make at the top of the attack. */
        class WaveView : public Component, private Timer
        {
        public:
            explicit WaveView (F& f) : holder (&f), fm (f) { startTimerHz (10); }
            void paint (Graphics& g) override
            {
                auto b = getLocalBounds().toFloat().reduced (1.0f);
                g.setColour (Pro::deep());
                g.fillRoundedRectangle (b, 6.0f);
                auto a = b.reduced (12.0f, 14.0f);
                g.setColour (Theme::ink.withAlpha (0.08f));
                g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
                const auto& alg = F::algorithms()[(size_t) jlimit (0, (int) F::algorithms().size() - 1, fm.getInt (F::pAlgorithm))];
                constexpr int perCycle = 256, cycles = 6;
                double phase[4] = { 0, 0, 0, 0 }, inc[4];
                float level[4], out[4] = { 0, 0, 0, 0 }, fbState = 0.0f;
                for (int i = 0; i < 4; ++i)
                {
                    inc[i] = fm.get (F::opParam (i, F::kRatio)) * std::pow (2.0, fm.get (F::opParam (i, F::kFine)) / 1200.0) / perCycle;
                    level[i] = fm.get (F::opParam (i, F::kLevel));
                }
                const float fb = fm.get (F::pFeedback);
                std::vector<float> ys;
                float peak = 0.01f;
                for (int n = 0; n < perCycle * cycles; ++n)
                {
                    float mod[4] = { 0, 0, 0, fbState * fb * 3.0f };
                    for (int i = 4; --i >= 0;)
                    {
                        for (auto [from, to] : alg.mods) if (to == i) mod[i] += out[from] * 4.0f;
                        out[i] = std::sin ((float) (MathConstants<double>::twoPi * phase[i]) + mod[i]) * level[i];
                        phase[i] += inc[i];
                    }
                    fbState = 0.5f * (fbState + out[3]);
                    float y = 0.0f;
                    for (int c : alg.outs) y += out[c];
                    if (n >= perCycle * (cycles - 2)) { ys.push_back (y); peak = jmax (peak, std::abs (y)); }
                }
                Path p, fill;
                fill.startNewSubPath (a.getX(), a.getCentreY());
                for (size_t i = 0; i < ys.size(); ++i)
                {
                    const float x = a.getX() + a.getWidth() * (float) i / (float) (ys.size() - 1), y = a.getCentreY() - ys[i] / peak * a.getHeight() * 0.46f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                    fill.lineTo (x, y);
                }
                fill.lineTo (a.getRight(), a.getCentreY());
                fill.closeSubPath();
                g.setColour (Theme::orange.withAlpha (0.12f));
                g.fillPath (fill);
                Graphs::glowStroke (g, p, Theme::orange, 1.8f);
            }
        private:
            void timerCallback() override { repaint(); }
            te::Plugin::Ptr holder;
            F& fm;
        };

        FMPanel (EditViewState& e, F& f) : evs (e), fm (f), holder (&f)
        {
            using EG = Graphs::EnvelopeGraph;
            using L = std::initializer_list<std::pair<int, const char*>>;
            auto card = [this] (const String& t, Colour c) { auto* k = cards.add (new Card (t, c)); addAndMakeVisible (k); return k; };
            const auto filt = Colour (0xffff6b6b), out = Colour (0xff748ffc);
            cAlgo = card ("ALGORITHM", Theme::orange);
            algo = std::make_unique<AlgoView> (f); cAlgo->addAndMakeVisible (*algo);
            algoBox = combo (*cAlgo, F::pAlgorithm);
            for (auto* b : { &prevAlgo, &nextAlgo })
            {
                cAlgo->addAndMakeVisible (b);
                b->onClick = [this, b]
                {
                    fm.edit.getUndoManager().beginNewTransaction ("Algorithm");
                    const int n = (int) F::algorithms().size();
                    Graphs::setValue (fm.param (F::pAlgorithm), (float) ((fm.getInt (F::pAlgorithm) + (b == &nextAlgo ? 1 : n - 1)) % n));
                };
            }
            feedback = knob (*cAlgo, F::pFeedback, "Feedback (op 4)", opColour (3));
            cWave = card ("THE WAVE IT MAKES", Theme::orange);
            wave = std::make_unique<WaveView> (f); cWave->addAndMakeVisible (*wave);
            for (int op = 0; op < F::numOps; ++op)
            {
                auto* o = ops.add (new Op());
                o->card = card ("OPERATOR " + String (op + 1), opColour (op));
                o->env = std::make_unique<EG> (std::vector<EG::Env> { { f.param (F::opParam (op, F::kA)), f.param (F::opParam (op, F::kD)), f.param (F::opParam (op, F::kS)),
                                                                        f.param (F::opParam (op, F::kR)), opColour (op), "OP " + String (op + 1) } });
                o->card->addAndMakeVisible (*o->env);
                for (auto [k, l] : L { { F::kRatio, "Ratio" }, { F::kFine, "Fine" }, { F::kLevel, "Level" }, { F::kA, "Attack" }, { F::kD, "Decay" }, { F::kS, "Sustain" }, { F::kR, "Release" } }) o->knobs.add (knob (*o->card, F::opParam (op, k), l, opColour (op)));
            }
            cFilter = card ("FILTER", filt);
            filterGraph = std::make_unique<Graphs::FilterGraph> (f.param (F::pCutoff), f.param (F::pReso), nullptr, nullptr, filt, f.param (F::pFEnv));
            cFilter->addAndMakeVisible (*filterGraph);
            for (auto [pi, l] : L { { F::pCutoff, "Cutoff" }, { F::pReso, "Resonance" }, { F::pFEnv, "Envelope" } }) knobsFilter.add (knob (*cFilter, pi, l, filt));
            cFEnv = card ("FILTER ENVELOPE", filt);
            fenvGraph = std::make_unique<EG> (std::vector<EG::Env> { { f.param (F::pFA), f.param (F::pFD), f.param (F::pFS), f.param (F::pFR), filt, "FILTER" } });
            cFEnv->addAndMakeVisible (*fenvGraph);
            for (auto [pi, l] : L { { F::pFA, "Attack" }, { F::pFD, "Decay" }, { F::pFS, "Sustain" }, { F::pFR, "Release" } }) knobsFEnv.add (knob (*cFEnv, pi, l, filt));
            cOut = card ("PLAY", out);
            for (auto [pi, l] : L { { F::pVelSens, "Velocity" }, { F::pGlide, "Glide" }, { F::pVolume, "Volume" } }) knobsOut.add (knob (*cOut, pi, l, out));
        }

        int numOperatorCards() const { return ops.size(); }

        void paint (Graphics& g) override { g.fillAll (Pro::bg()); }

        void resized() override
        {
            const int gap = 8;
            auto a = getLocalBounds().reduced (8);
            auto col1 = a.removeFromLeft (a.getWidth() * 30 / 100); a.removeFromLeft (gap);
            auto col3 = a.removeFromRight (a.getWidth() * 40 / 100); a.removeFromRight (gap);
            auto col2 = a;

            cAlgo->setBounds (col1.removeFromTop (col1.getHeight() * 58 / 100)); col1.removeFromTop (gap);
            cWave->setBounds (col1);
            {
                auto head = cAlgo->header();
                nextAlgo.setBounds (head.removeFromRight (28));
                head.removeFromRight (4);
                prevAlgo.setBounds (head.removeFromRight (28));
                head.removeFromRight (6);
                algoBox->setBounds (head.removeFromRight (jmin (150, head.getWidth())));
                auto c = cAlgo->content();
                feedback->setBounds (c.removeFromBottom (90).withSizeKeepingCentre (110, 88));
                c.removeFromBottom (4);
                algo->setBounds (c);
            }
            wave->setBounds (cWave->content());

            const int oh = (col2.getHeight() - gap * (ops.size() - 1)) / jmax (1, ops.size());
            for (auto* o : ops)
            {
                o->card->setBounds (col2.removeFromTop (oh)); col2.removeFromTop (gap);
                auto c = o->card->content();
                // ratio, fine and level in a column of the grid; attack, decay, sustain, release as knobs too (the graph drags them as well)
                WavetablePanel::placeKnobs (c.removeFromRight (jmin (300, c.getWidth() * 60 / 100)), o->knobs, 4, 76, 76);
                c.removeFromRight (6);
                o->env->setBounds (c);
            }

            auto graphAndKnobs = [] (Card& cd, Component& graph, Array<ParamKnob*>& ks)
            {
                auto c = cd.content();
                WavetablePanel::placeKnobs (c.removeFromBottom (jmin (88, c.getHeight() * 45 / 100)), ks);
                c.removeFromBottom (4);
                graph.setBounds (c);
            };
            const int big = (col3.getHeight() - 2 * gap) * 38 / 100;
            cFilter->setBounds (col3.removeFromTop (big)); col3.removeFromTop (gap);
            cFEnv->setBounds (col3.removeFromTop (big)); col3.removeFromTop (gap);
            cOut->setBounds (col3);
            graphAndKnobs (*cFilter, *filterGraph, knobsFilter);
            graphAndKnobs (*cFEnv, *fenvGraph, knobsFEnv);
            WavetablePanel::placeKnobs (cOut->content(), knobsOut, 0, 96, 100);
        }

    private:
        struct Op
        {
            Card* card = nullptr;
            std::unique_ptr<Graphs::EnvelopeGraph> env;
            Array<ParamKnob*> knobs;
        };
        ParamKnob* knob (Card& c, int pi, const char* label, Colour col)
        {
            auto* k = new ParamKnob (evs, *fm.param (pi));
            k->setLabel (label);
            k->setCompact (col);
            owned.add (k);
            c.addAndMakeVisible (k);
            return k;
        }
        ParamCombo* combo (Card& c, int pi)
        {
            auto* cb = new ParamCombo (*fm.param (pi), false);
            owned.add (cb);
            c.addAndMakeVisible (cb);
            return cb;
        }

        EditViewState& evs;
        F& fm;
        te::Plugin::Ptr holder;
        OwnedArray<Card> cards;
        OwnedArray<Component> owned;
        OwnedArray<Op> ops;
        Card *cAlgo = nullptr, *cWave = nullptr, *cFilter = nullptr, *cFEnv = nullptr, *cOut = nullptr;
        std::unique_ptr<AlgoView> algo;
        std::unique_ptr<WaveView> wave;
        std::unique_ptr<Graphs::FilterGraph> filterGraph;
        std::unique_ptr<Graphs::EnvelopeGraph> fenvGraph;
        ParamCombo* algoBox = nullptr;
        ParamKnob* feedback = nullptr;
        TextButton prevAlgo { "<" }, nextAlgo { ">" };
        Array<ParamKnob*> knobsFilter, knobsFEnv, knobsOut;
    };

    //==============================================================================
    /** Simpler's window: the sample across the top (region handles on the waveform, region and pitch
        knobs under it), then the filter, both envelopes with all their knobs, granular and output. */
    class SimplerPanel : public Component
    {
    public:
        using S = pulse::Simpler;
        using Card = WavetablePanel::Card;

        SimplerPanel (EditViewState& e, S& sm) : evs (e), simpler (sm), holder (&sm)
        {
            using EG = Graphs::EnvelopeGraph;
            using L = std::initializer_list<std::pair<int, const char*>>;
            auto card = [this] (const String& t, Colour c) { auto* k = cards.add (new Card (t, c)); addAndMakeVisible (k); return k; };
            const auto smp = Theme::orange, filt = Colour (0xffff6b6b), amp = Colour (0xff69db7c), gran = Colour (0xffda77f2), out = Colour (0xff748ffc);

            cSample = card ("SAMPLE", smp);
            display = std::make_unique<DeviceDisplays::SimplerDisplay> (sm);
            cSample->addAndMakeVisible (*display);
            mode = new ParamChoice (*sm.param (S::pMode), smp, false); owned.add (mode); cSample->addAndMakeVisible (mode);
            for (auto [pi, l] : L { { S::pStart, "Start" }, { S::pEnd, "End" }, { S::pLoopStart, "Loop start" }, { S::pXfade, "Crossfade" } }) knobsRegion.add (knob (*cSample, pi, l, smp));
            loop = new ParamToggle (*sm.param (S::pLoop), "Loop", smp); owned.add (loop); cSample->addAndMakeVisible (loop);
            for (auto [pi, l] : L { { S::pRoot, "Root note" }, { S::pTranspose, "Transpose" }, { S::pFine, "Detune" } }) knobsPitch.add (knob (*cSample, pi, l, smp));

            cFilter = card ("FILTER", filt);
            filterGraph = std::make_unique<Graphs::FilterGraph> (sm.param (S::pCutoff), sm.param (S::pReso), sm.param (S::pFilterType),
                                                                 [] (int t) { return jlimit (0, 3, t); }, filt, sm.param (S::pFEnv));
            cFilter->addAndMakeVisible (*filterGraph);
            filterType = new ParamCombo (*sm.param (S::pFilterType), false); owned.add (filterType); cFilter->addAndMakeVisible (filterType);
            for (auto [pi, l] : L { { S::pCutoff, "Cutoff" }, { S::pReso, "Resonance" }, { S::pFEnv, "Envelope" } }) knobsFilter.add (knob (*cFilter, pi, l, filt));

            cAmp = card ("AMP ENVELOPE", amp);
            ampGraph = std::make_unique<EG> (std::vector<EG::Env> { { sm.param (S::pA), sm.param (S::pD), sm.param (S::pS), sm.param (S::pR), amp, "AMP" } });
            cAmp->addAndMakeVisible (*ampGraph);
            for (auto [pi, l] : L { { S::pA, "Attack" }, { S::pD, "Decay" }, { S::pS, "Sustain" }, { S::pR, "Release" } }) knobsAmp.add (knob (*cAmp, pi, l, amp));

            cFEnv = card ("FILTER ENVELOPE", filt);
            fenvGraph = std::make_unique<EG> (std::vector<EG::Env> { { sm.param (S::pFA), sm.param (S::pFD), sm.param (S::pFS), sm.param (S::pFR), filt, "FILTER" } });
            cFEnv->addAndMakeVisible (*fenvGraph);
            for (auto [pi, l] : L { { S::pFA, "Attack" }, { S::pFD, "Decay" }, { S::pFS, "Sustain" }, { S::pFR, "Release" } }) knobsFEnv.add (knob (*cFEnv, pi, l, filt));

            cGrain = card ("GRANULAR  (when the mode is Granular)", gran);
            for (auto [pi, l] : L { { S::pGrainSize, "Grain size" }, { S::pDensity, "Density" }, { S::pSpray, "Spray" }, { S::pGrainPos, "Scan" } }) knobsGrain.add (knob (*cGrain, pi, l, gran));
            cOut = card ("OUTPUT", out);
            for (auto [pi, l] : L { { S::pVelSens, "Velocity" }, { S::pGain, "Gain" }, { S::pPan, "Pan" } }) knobsOut.add (knob (*cOut, pi, l, out));
        }

        void paint (Graphics& g) override { g.fillAll (Pro::bg()); }

        void resized() override
        {
            const int gap = 8;
            auto a = getLocalBounds().reduced (8);
            cSample->setBounds (a.removeFromTop (a.getHeight() * 44 / 100)); a.removeFromTop (gap);
            {
                auto head = cSample->header();
                mode->setBounds (head.removeFromRight (jmin (340, head.getWidth())));
                auto c = cSample->content();
                auto row = c.removeFromBottom (jmin (100, c.getHeight() * 40 / 100));
                c.removeFromBottom (6);
                display->setBounds (c);
                cSample->wells.clear();
                auto region = row.removeFromLeft (row.getWidth() * 60 / 100); row.removeFromLeft (8);
                cSample->wells.push_back ({ region, "REGION" });
                cSample->wells.push_back ({ row, "PITCH" });
                auto ri = region.reduced (6, 0).withTrimmedTop (12);
                loop->setBounds (ri.removeFromRight (90).withSizeKeepingCentre (80, 28));
                WavetablePanel::placeKnobs (ri, knobsRegion);
                WavetablePanel::placeKnobs (row.reduced (6, 0).withTrimmedTop (12), knobsPitch);
            }
            auto graphAndKnobs = [] (Card& cd, Component& graph, Array<ParamKnob*>& ks)
            {
                auto c = cd.content();
                WavetablePanel::placeKnobs (c.removeFromBottom (jmin (90, c.getHeight() * 40 / 100)), ks);
                c.removeFromBottom (4);
                graph.setBounds (c);
            };
            auto right = a.removeFromRight (a.getWidth() * 24 / 100); a.removeFromRight (gap);
            const int cw = (a.getWidth() - 2 * gap) / 3;
            cFilter->setBounds (a.removeFromLeft (cw)); a.removeFromLeft (gap);
            cAmp->setBounds (a.removeFromLeft (cw)); a.removeFromLeft (gap);
            cFEnv->setBounds (a);
            {
                auto head = cFilter->header();
                filterType->setBounds (head.removeFromRight (jmin (140, head.getWidth())));
            }
            graphAndKnobs (*cFilter, *filterGraph, knobsFilter);
            graphAndKnobs (*cAmp, *ampGraph, knobsAmp);
            graphAndKnobs (*cFEnv, *fenvGraph, knobsFEnv);
            cGrain->setBounds (right.removeFromTop (right.getHeight() * 58 / 100)); right.removeFromTop (gap);
            cOut->setBounds (right);
            WavetablePanel::placeKnobs (cGrain->content(), knobsGrain, 2, 96, 96);
            WavetablePanel::placeKnobs (cOut->content(), knobsOut, 0, 96, 96);
        }

    private:
        ParamKnob* knob (Card& c, int pi, const char* label, Colour col)
        {
            auto* k = new ParamKnob (evs, *simpler.param (pi));
            k->setLabel (label);
            k->setCompact (col);
            owned.add (k);
            c.addAndMakeVisible (k);
            return k;
        }

        EditViewState& evs;
        S& simpler;
        te::Plugin::Ptr holder;
        OwnedArray<Card> cards;
        OwnedArray<Component> owned;
        Card *cSample = nullptr, *cFilter = nullptr, *cAmp = nullptr, *cFEnv = nullptr, *cGrain = nullptr, *cOut = nullptr;
        std::unique_ptr<DeviceDisplays::SimplerDisplay> display;
        std::unique_ptr<Graphs::FilterGraph> filterGraph;
        std::unique_ptr<Graphs::EnvelopeGraph> ampGraph, fenvGraph;
        ParamChoice* mode = nullptr;
        ParamCombo* filterType = nullptr;
        ParamToggle* loop = nullptr;
        Array<ParamKnob*> knobsRegion, knobsPitch, knobsFilter, knobsAmp, knobsFEnv, knobsGrain, knobsOut;
    };

    //==============================================================================
    /** The Drum Synth's window: the hits it makes drawn large, the sound controls under them, and how it plays. */
    class DrumSynthPanel : public Component
    {
    public:
        using D = pulse::DrumSynth;
        using Card = WavetablePanel::Card;

        DrumSynthPanel (EditViewState& e, D& d) : evs (e), synth (d), holder (&d)
        {
            using L = std::initializer_list<std::pair<int, const char*>>;
            auto card = [this] (const String& t, Colour c) { auto* k = cards.add (new Card (t, c)); addAndMakeVisible (k); return k; };
            const auto hit = Theme::orange, sound = Colour (0xff4dabf7), play = Colour (0xff748ffc);
            cHits = card ("THE HITS IT MAKES  (kick, snare, hat, clap with these settings)", hit);
            hits = std::make_unique<DrumHitsView> (d);
            cHits->addAndMakeVisible (*hits);
            cSound = card ("SOUND", sound);
            for (auto [pi, l] : L { { D::pTune, "Tune" }, { D::pDecay, "Decay" }, { D::pTone, "Tone" }, { D::pSnap, "Snap" }, { D::pDrive, "Drive" } }) knobsSound.add (knob (*cSound, pi, l, sound));
            cPlay = card ("PLAY", play);
            map = new ParamCombo (*d.param (D::pMap), true);
            map->setLabel ("Keys play");
            owned.add (map); cPlay->addAndMakeVisible (map);
            for (auto [pi, l] : L { { D::pVelSens, "Velocity" }, { D::pGain, "Gain" } }) knobsPlay.add (knob (*cPlay, pi, l, play));
        }

        void paint (Graphics& g) override { g.fillAll (Pro::bg()); }

        void resized() override
        {
            const int gap = 8;
            auto a = getLocalBounds().reduced (8);
            auto bottom = a.removeFromBottom (jlimit (130, 190, a.getHeight() * 30 / 100));
            a.removeFromBottom (gap);
            cHits->setBounds (a);
            hits->setBounds (cHits->content());
            cPlay->setBounds (bottom.removeFromRight (jlimit (300, 460, bottom.getWidth() * 38 / 100)));
            bottom.removeFromRight (gap);
            cSound->setBounds (bottom);
            WavetablePanel::placeKnobs (cSound->content(), knobsSound, 0, 100, 100);
            auto c = cPlay->content();
            map->setBounds (c.removeFromLeft (jmin (190, c.getWidth() / 2)).withSizeKeepingCentre (jmin (180, c.getWidth() / 2 - 6), 48));
            WavetablePanel::placeKnobs (c, knobsPlay, 0, 100, 100);
        }

    private:
        ParamKnob* knob (Card& c, int pi, const char* label, Colour col)
        {
            auto* k = new ParamKnob (evs, *synth.param (pi));
            k->setLabel (label);
            k->setCompact (col);
            owned.add (k);
            c.addAndMakeVisible (k);
            return k;
        }
        EditViewState& evs;
        D& synth;
        te::Plugin::Ptr holder;
        OwnedArray<Card> cards;
        OwnedArray<Component> owned;
        Card *cHits = nullptr, *cSound = nullptr, *cPlay = nullptr;
        std::unique_ptr<DrumHitsView> hits;
        ParamCombo* map = nullptr;
        Array<ParamKnob*> knobsSound, knobsPlay;
    };

    //==============================================================================
    using Graph = std::pair<String, std::unique_ptr<Component>>;

    /** Every view of a device, big. */
    inline std::vector<Graph> graphsFor (EditViewState& evs, te::Plugin& p)
    {
        using EG = Graphs::EnvelopeGraph;
        std::vector<Graph> v;
        auto add = [&v] (String t, std::unique_ptr<Component> c) { v.push_back ({ std::move (t), std::move (c) }); };
        auto env = [] (std::vector<EG::Env> e) { return std::make_unique<EG> (std::move (e)); };

        if (auto s = dynamic_cast<pulse::WavetableSynth*> (&p))
            add ("Wavetable", std::make_unique<WavetablePanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::Simpler*> (&p))
            add ("Simpler", std::make_unique<SimplerPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::FMSynth*> (&p))
            add ("FM Synth", std::make_unique<FMPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::DrumSynth*> (&p))
            add ("Drum Synth", std::make_unique<DrumSynthPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::GlueCompressor*> (&p))
        {
            add ("Transfer curve", DeviceDisplays::create (evs, p));
            add ("Compression", std::make_unique<ProUI::GlueNeedle> (*s));
            add ("Gain reduction over time", std::make_unique<HistoryGraph> ([s] { return s->gainReductionDb.load(); }, 24.0f, "dB", Theme::orange));
        }
        else if (auto s = dynamic_cast<pulse::Limiter*> (&p))
            add ("Limiter", std::make_unique<DynamicsUI::LimiterPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::ProCompressor*> (&p))
            add ("Compressor", std::make_unique<DynamicsUI::CompressorPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::MultibandDynamics*> (&p))
            add ("Multiband", std::make_unique<DynamicsUI::MultibandPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::Gate*> (&p))
            add ("Gate", std::make_unique<DynamicsUI::GatePanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::TransientShaper*> (&p))
            add ("Transient Shaper", std::make_unique<DynamicsUI::TransientPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::DeEsser*> (&p))
            add ("De-esser", std::make_unique<DynamicsUI::DeEsserPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::Reverb*> (&p))
            add ("Reverb", std::make_unique<SpaceUI::ReverbPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::Delay*> (&p))
            add ("Delay", std::make_unique<SpaceUI::DelayPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::Saturator*> (&p))
            add ("Saturator", std::make_unique<SpaceUI::SaturatorPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::AutoFilter*> (&p))
        {
            using A = pulse::AutoFilter;
            auto modes = [] (int t) { const int m[] = { 1, 2, 3, 4, 5 }; return m[jlimit (0, 4, t)]; };
            add ("Filter", std::make_unique<Graphs::FilterGraph> (s->param (A::pCutoff), s->param (A::pReso), s->param (A::pType), modes, Theme::accent, s->param (A::pEnvAmount)));
            add ("LFO", std::make_unique<ProUI::AutoFilterLfo> (*s));
            add ("Envelope follower", env ({ { s->param (A::pAttack), nullptr, nullptr, s->param (A::pRelease), Theme::accent, "FOLLOWER" } }));
        }
        else if (auto q = dynamic_cast<pulse::PrismEQ*> (&p))
            add ("Prism EQ", std::make_unique<PrismUI::View> (evs, *q, true));
        else if (auto fx = dynamic_cast<pulse::PulseMidiEffect*> (&p))
            add ("What it does to a phrase", std::make_unique<DeviceViews::MidiFxPreview> (*fx));
        else if (auto vc = dynamic_cast<pulse::Vocoder*> (&p))
        {
            add ("Bands", std::make_unique<DeviceViews::VocoderBands> (*vc));
            if (auto c = DeviceDisplays::create (evs, p)) add ("Vocoder", std::move (c));
        }
        else if (auto sp = dynamic_cast<pulse::Spectrum*> (&p))
            add ("Spectrum", std::make_unique<ProUI::SpectrumPro> (*sp));
        else if (auto tn = dynamic_cast<pulse::Tuner*> (&p))
            add ("Tuner", std::make_unique<ProUI::TunerPro> (*tn));
        else if (auto pc = dynamic_cast<pulse::PitchCorrect*> (&p))
            add ("Pitch", std::make_unique<ProUI::PitchPro> (*pc));
        else if (auto u = dynamic_cast<pulse::Utility*> (&p))
            add ("Stereo field", std::make_unique<DeviceViews::StereoField> (*u));
        else if (auto c = DeviceDisplays::create (evs, p))
            add (p.getName(), std::move (c));
        if (auto pp = dynamic_cast<pulse::PulsePlugin*> (&p); pp != nullptr && v.empty() && pp->numParams() > 0 && pp->numParams() <= 48)
            add ("All parameters", std::make_unique<ParamBars> (*pp));
        return v;
    }

    struct Window;
    Window* show (EditViewState&, pulse::PulsePlugin&);

    /** Devices whose editor is one complete panel (graph-first, few knobs). */
    inline bool hasFullEditor (pulse::PulsePlugin& p)
    {
        return dynamic_cast<pulse::PrismEQ*> (&p) != nullptr || dynamic_cast<pulse::ProCompressor*> (&p) != nullptr
            || dynamic_cast<pulse::WavetableSynth*> (&p) != nullptr
            || dynamic_cast<pulse::FMSynth*> (&p) != nullptr
            || dynamic_cast<pulse::Simpler*> (&p) != nullptr
            || dynamic_cast<pulse::DrumSynth*> (&p) != nullptr
            || dynamic_cast<pulse::Reverb*> (&p) != nullptr || dynamic_cast<pulse::Delay*> (&p) != nullptr || dynamic_cast<pulse::Saturator*> (&p) != nullptr
            || dynamic_cast<pulse::MultibandDynamics*> (&p) != nullptr || dynamic_cast<pulse::Limiter*> (&p) != nullptr
            || dynamic_cast<pulse::Gate*> (&p) != nullptr || dynamic_cast<pulse::TransientShaper*> (&p) != nullptr || dynamic_cast<pulse::DeEsser*> (&p) != nullptr;
    }

    //==============================================================================
    class Editor : public Component,
                   public DragAndDropContainer   // modulation sources are dragged onto knobs
    {
    public:
        int getNumGraphs() const { return (int) graphs.size(); }
        int getNumGroups() const { return groups.size(); }
        bool hasPluginWindowButton() const { return windowBtn.isVisible(); }
        void clickAB() { abBtn.onClick(); }

        Editor (EditViewState& e, pulse::PulsePlugin& p) : evs (e), holder (&p), plugin (&p)
        {
            for (auto& [title, comp] : graphsFor (evs, p))
            {
                auto* lbl = graphTitles.add (new Label (String(), title.toUpperCase()));
                lbl->setFont (FontOptions (11.0f, Font::bold));
                lbl->setColour (Label::textColourId, Theme::muted);
                addAndMakeVisible (lbl);
                addAndMakeVisible (*comp);
                graphs.push_back (std::move (comp));
            }

            viewport.setViewedComponent (&controls, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);
            const auto accent = p.isSynth() ? Theme::orange : Theme::accent;
            for (auto& sec : p.getSections())
            {
                auto* grp = groups.add (new Group());
                grp->name = sec.name;
                grp->header.setText (sec.name.toUpperCase(), dontSendNotification);
                grp->header.setFont (FontOptions (11.0f, Font::bold));
                grp->header.setColour (Label::textColourId, Theme::muted);
                grp->header.setMinimumHorizontalScale (0.7f);
                controls.addAndMakeVisible (grp->header);
                for (auto idx : sec.params)
                {
                    auto& prm = *p.param (idx);
                    auto shortName = prm.getParameterName();
                    if (shortName.startsWithIgnoreCase (sec.name + " ")) shortName = shortName.substring (sec.name.length() + 1);
                    // choices are buttons, on / off parameters a toggle, everything else a knob
                    Component* c = nullptr;
                    int span = 66;   // width in the bar
                    if (ParamButtons::isToggle (prm))
                    {
                        c = new ParamToggle (prm, shortName, accent);
                        span = jmax (66, GlyphArrangement::getStringWidthInt (FontOptions (13.0f), shortName) + 26);
                    }
                    else if (ParamButtons::numOptions (prm) > 0 && ParamButtons::numOptions (prm) <= 5)
                    {
                        auto* ch = new ParamChoice (prm, accent);
                        ch->setLabel (shortName);
                        span = jmax (96, ch->idealWidth() + 14);
                        c = ch;
                    }
                    else if (ParamButtons::numOptions (prm) > 5)
                    {
                        auto* cb = new ParamCombo (prm);
                        cb->setLabel (shortName);
                        span = 126;
                        c = cb;
                    }
                    else
                    {
                        auto* k = new ParamKnob (evs, prm);
                        k->setCompact (accent);
                        k->name.setText (shortName, dontSendNotification);
                        c = k;
                    }
                    grp->knobs.add (c);
                    grp->spans.add (span);
                    controls.addAndMakeVisible (c);
                }
            }

            title.setText (p.getName(), dontSendNotification);
            title.setFont (FontOptions (16.0f, Font::bold));
            addAndMakeVisible (title);
            for (auto* b : { &randomBtn, &resetBtn, &abBtn }) addAndMakeVisible (b);
            abBtn.setTooltip ("Compare two versions of the settings (click to switch between A and B)");
            abBtn.onClick = [this]
            {
                auto& mine = abIsB ? slotB : slotA;
                mine = DeviceTools::capture (*plugin);
                auto& other = abIsB ? slotA : slotB;
                plugin->edit.getUndoManager().beginNewTransaction ("A/B");
                if (! other.empty()) DeviceTools::apply (*plugin, other);
                abIsB = ! abIsB;
                abBtn.setButtonText (abIsB ? "B" : "A");
            };
            if (auto sb = dynamic_cast<pulse::SandboxedPlugin*> (plugin))
            {
                windowBtn.setTooltip ("Open the plugin's own window (it runs in its own process)");
                windowBtn.onClick = [sb] { sb->showEditor(); };
                addAndMakeVisible (windowBtn);
            }
            randomBtn.onClick = [this] { plugin->edit.getUndoManager().beginNewTransaction ("Randomise"); Random r; DeviceTools::randomise (*plugin, r, 0.3f); };
            resetBtn.onClick = [this] { plugin->edit.getUndoManager().beginNewTransaction ("Reset device"); DeviceTools::resetToDefaults (*plugin); };
            presetBtn.onClick = [this]
            {
                PopupMenu m;
                FactoryPresets::addToMenu (m, plugin->getPluginType(), FactoryPresets::current (*plugin), [this] (const String& name) { choosePreset (name); });
                auto mine = Presets::devicePresets (*plugin);
                if (! mine.isEmpty())
                {
                    m.addSectionHeader ("Your presets");
                    for (auto& f : mine)
                        m.addItem (f.getFileNameWithoutExtension(), [this, f, &evs = evs]
                        {
                            plugin->edit.getUndoManager().beginNewTransaction ("Load preset");
                            auto np = Presets::loadDevice (*plugin, f);   // replaces the device: reopen the editor on the new one
                            if (auto pp = dynamic_cast<pulse::PulsePlugin*> (np.get()))
                                MessageManager::callAsync ([&evs, ref = te::Plugin::Ptr (np), pp] { juce::ignoreUnused (ref); show (evs, *pp); });
                        });
                }
                m.addSeparator();
                m.addItem ("Save preset...", [this]
                {
                    Presets::askForName ("Save preset", plugin->getName(), [p = te::Plugin::Ptr (plugin)] (const String& n) { Presets::saveDevice (*p, n); });
                });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn));
            };
            addAndMakeVisible (presetBtn);
            // previous / next factory preset, and the name of the current one on the button
            const bool hasPresets = ! FactoryPresets::forType (plugin->getPluginType()).empty();
            for (auto* b : { &prevPreset, &nextPreset })
            {
                b->setVisible (hasPresets);
                addChildComponent (b);
                b->setVisible (hasPresets);
                b->onClick = [this, b] { stepPreset (b == &nextPreset ? 1 : -1); };
            }
            prevPreset.setTooltip ("Previous preset");
            nextPreset.setTooltip ("Next preset");
            presetBtn.setTooltip ("Presets: the built-in ones (by kind of sound), your own, and Save preset");
            updatePresetName();
            if (dynamic_cast<pulse::WavetableSynth*> (&p) != nullptr || dynamic_cast<pulse::FMSynth*> (&p) != nullptr || dynamic_cast<pulse::Simpler*> (&p) != nullptr) setSize (1280, 860); else setSize (1100, 760);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto head = getLocalBounds().removeFromTop (44).toFloat();
            g.setGradientFill (ColourGradient (Pro::cardTop(), 0.0f, 0.0f, Pro::cardBot(), 0.0f, head.getBottom(), false));
            g.fillRect (head);
            g.setColour (plugin->isSynth() ? Theme::orange : Theme::accent);
            g.fillRect (0, 42, getWidth(), 2);
            if (! displayArea.isEmpty()) Graphs::proDisplay (g, displayArea.toFloat());
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (44).reduced (12, 8);
            for (auto* b : { &resetBtn, &randomBtn }) { b->setBounds (top.removeFromRight (100)); top.removeFromRight (6); }
            if (nextPreset.isVisible()) { nextPreset.setBounds (top.removeFromRight (26)); top.removeFromRight (2); }
            presetBtn.setBounds (top.removeFromRight (jlimit (110, 230, top.getWidth() / 3))); top.removeFromRight (nextPreset.isVisible() ? 2 : 6);
            if (prevPreset.isVisible()) { prevPreset.setBounds (top.removeFromRight (26)); top.removeFromRight (6); }
            abBtn.setBounds (top.removeFromRight (44)); top.removeFromRight (6);
            if (windowBtn.isVisible()) { windowBtn.setBounds (top.removeFromRight (130)); top.removeFromRight (6); }
            title.setBounds (top);
            displayArea = {};

            if (hasFullEditor (*plugin) && ! graphs.empty())   // its own full editor
            {
                for (auto* t : graphTitles) t->setVisible (false);
                viewport.setVisible (false);
                graphs[0]->setBounds (r.reduced (12, 8));
                return;
            }
            // the displays fill the window; the controls sit in a dark bar along the bottom
            viewport.setBounds (r);
            layoutControls();
            const int barH = graphs.empty() ? r.getHeight() : jmin (controls.getHeight(), r.getHeight() * 55 / 100);
            viewport.setBounds (r.removeFromBottom (barH));
            layoutControls();
            if (! graphs.empty())
            {
                displayArea = r;
                auto area = r.reduced (10, 8);
                const int n = (int) graphs.size();
                const int cols = n <= 3 ? n : n == 4 ? 2 : 3;
                const int rows = (n + cols - 1) / cols;
                const int cw = area.getWidth() / cols, rh = area.getHeight() / rows;
                for (int i = 0; i < n; ++i)
                {
                    auto cell = Rectangle<int> (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * rh, cw, rh).reduced (5);
                    graphTitles[i]->setBounds (cell.removeFromTop (18));
                    graphs[(size_t) i]->setBounds (cell);
                }
            }
        }

        /** Applies a factory preset (undoable) and shows its name. */
        void choosePreset (const String& name)
        {
            plugin->edit.getUndoManager().beginNewTransaction ("Factory preset");
            FactoryPresets::apply (*plugin, name);
            updatePresetName();
        }
        /** Steps through the factory presets in menu order (wrapping round). */
        void stepPreset (int delta)
        {
            const auto names = FactoryPresets::ordered (plugin->getPluginType());
            if (names.isEmpty()) return;
            const int at = names.indexOf (FactoryPresets::current (*plugin));
            choosePreset (names[(at < 0 ? (delta > 0 ? 0 : names.size() - 1) : (at + delta + names.size()) % names.size())]);
        }
        String presetName() const { return FactoryPresets::current (*plugin); }
        void updatePresetName()
        {
            const auto n = FactoryPresets::current (*plugin);
            presetBtn.setButtonText (n.isNotEmpty() ? n : String ("Presets"));
        }

    private:
        struct Group
        {
            Label header;
            String name;
            OwnedArray<Component> knobs;   // knobs, toggles and option buttons
            Array<int> spans;              // the width each control takes
        };

        /** The bottom bar: a dark gradient with a hairline between the groups. */
        struct Bar : public Component
        {
            std::vector<Rectangle<int>> separators;
            void paint (Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, 0.0f, Pro::barBot(), 0.0f, r.getBottom(), false));
                g.fillRect (r);
                g.setColour (Theme::ink.withAlpha (0.06f));
                g.drawHorizontalLine (0, 0.0f, r.getRight());
                for (auto& s : separators) g.fillRect (s);
            }
        };

        void layoutControls()
        {
            // groups flow left to right as strips (title above a row of controls), wrapping to new rows
            const int w = jmax (200, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
            constexpr int rowH = 78, titleH = 18, pad = 14, gapX = 6;
            int x = pad, y = 8, lineH = 0;
            controls.separators.clear();
            for (auto* g : groups)
            {
                int natural = 0;
                for (auto sp : g->spans) natural += sp + gapX;
                const int gw = jlimit (80, w - 2 * pad, natural);
                if (x > pad && x + gw > w - pad) { x = pad; y += lineH + 8; lineH = 0; }
                int cx = 0, row = 0;
                for (int i = 0; i < g->knobs.size(); ++i)
                {
                    const int cw = jmin (gw, g->spans[i]);
                    if (cx > 0 && cx + cw > gw) { cx = 0; ++row; }
                    auto* k = g->knobs[i];
                    auto cell = Rectangle<int> (x + cx, y + titleH + row * rowH, cw, rowH);
                    if (dynamic_cast<ParamToggle*> (k) != nullptr) cell = cell.withSizeKeepingCentre (cw - 4, 28);
                    else if (dynamic_cast<ParamChoice*> (k) != nullptr) cell = cell.withSizeKeepingCentre (cw - 4, 46).translated (0, -4);
                    else if (dynamic_cast<ParamCombo*> (k) != nullptr) cell = cell.withSizeKeepingCentre (cw - 4, 48).translated (0, -4);
                    k->setBounds (cell);
                    cx += cw + gapX;
                }
                const int gh = titleH + (row + 1) * rowH;
                g->header.setBounds (x, y, gw, titleH);
                controls.separators.push_back ({ x + gw + 8, y + 6, 1, gh - 10 });
                x += gw + 20;
                lineH = jmax (lineH, gh);
            }
            controls.setSize (w, y + lineH + 8);
            controls.repaint();
        }

        EditViewState& evs;
        te::Plugin::Ptr holder;
        pulse::PulsePlugin* plugin;
        std::vector<std::unique_ptr<Component>> graphs;
        OwnedArray<Label> graphTitles;
        Viewport viewport;
        Bar controls;
        Rectangle<int> displayArea;
        OwnedArray<Group> groups;
        Label title;
        TextButton prevPreset { "<" }, nextPreset { ">" };
        TextButton presetBtn { "Presets" }, randomBtn { "Randomise" }, resetBtn { "Reset" }, abBtn { "A" }, windowBtn { "Plugin window" };
        DeviceTools::Values slotA, slotB;
        bool abIsB = false;
    };

    //==============================================================================
    /** One window per device (opening it again brings it to the front). */
    struct Window : public DocumentWindow, private Timer
    {
        Window (EditViewState& e, pulse::PulsePlugin& pl) : DocumentWindow (pl.getName(), Theme::bg, DocumentWindow::allButtons), plugin (&pl)
        {
                setUsingNativeTitleBar (true);
                setContentOwned (new Editor (e, pl), true);
                setResizable (true, false);
                // the big instrument windows need room; every window has to fit the screen it opens on
                const bool big = getWidth() > 1200;
                auto screen = Rectangle<int> (1600, 900);
                if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) screen = d->userArea;
                const int maxW = jmax (560, screen.getWidth() - 40), maxH = jmax (420, screen.getHeight() - 60);
                setResizeLimits (jmin (maxW, big ? 1000 : 560), jmin (maxH, big ? 680 : 420), 3000, 2000);
                centreWithSize (jmin (getWidth(), maxW), jmin (getHeight(), maxH));
                setVisible (true);
                startTimerHz (4);
            }
            void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }

        private:
            void timerCallback() override   // the device was deleted: close
            {
                if (plugin->getOwnerTrack() == nullptr && plugin->getOwnerRackType() == nullptr) { stopTimer(); closeButtonPressed(); }
            }
            te::Plugin::Ptr plugin;
    };

    inline std::map<String, Component::SafePointer<Window>>& openWindows()
    {
        static std::map<String, Component::SafePointer<Window>> open;
        return open;
    }

    /** Closes every editor window (before the edit goes away). */
    inline void closeAll()
    {
        for (auto& [k, w] : openWindows()) delete w.getComponent();
        openWindows().clear();
    }

    inline Window* show (EditViewState& evs, pulse::PulsePlugin& p)
    {
        auto key = p.itemID.toString();
        if (auto w = openWindows()[key].getComponent()) { w->toFront (true); return w; }
        auto* w = new Window (evs, p);   // deletes itself when closed
        openWindows()[key] = w;
        return w;
    }
}
