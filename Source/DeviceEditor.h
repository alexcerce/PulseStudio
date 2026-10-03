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
            const float depthX = a.getWidth() * 0.2f, depthY = a.getHeight() * 0.46f;
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
            for (int f = frames; --f >= 0;)   // back to front
            {
                if (f == current) continue;
                Path p; float by, x0, x1;
                frame (f, p, by, x0, x1);
                g.setColour (colour.withMultipliedSaturation (0.5f).withAlpha (0.10f + 0.20f * f / (float) frames));
                g.strokePath (p, PathStrokeType (1.0f));
            }
            {
                Path p; float by, x0, x1;
                frame (current, p, by, x0, x1);
                Path fill (p);
                fill.lineTo (x1, by); fill.lineTo (x0, by); fill.closeSubPath();
                g.setColour (colour.withAlpha (0.10f));
                g.fillPath (fill);
                Graphs::glowStroke (g, p, colour, 2.0f);
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
    /** The Wavetable's full panel: oscillators with their wavetables in 3D, filters, envelopes,
        LFOs, the modulation matrix, effects, and the play settings with a live scope, on dark cards. */
    class WavetablePanel : public Component
    {
    public:
        using W = pulse::WavetableSynth;

        /** A dark rounded card with a small title; its header row can hold menus or tabs, and it can
            draw captioned wells behind groups of knobs. */
        struct Card : public Component
        {
            struct Well { Rectangle<int> area; String name; };
            String title;
            Colour accent;
            std::vector<Well> wells;
            static constexpr int headerH = 30;

            Card (const String& t, Colour a) : title (t), accent (a) {}
            Rectangle<int> content() const { return getLocalBounds().reduced (10, 0).withTrimmedTop (headerH + 2).withTrimmedBottom (8); }
            /** The header to the right of the title. */
            Rectangle<int> header() const
            {
                const int tw = GlyphArrangement::getStringWidthInt (FontOptions (12.0f, Font::bold), title) + 34;
                return getLocalBounds().removeFromTop (headerH).withTrimmedLeft (tw).withTrimmedRight (8).reduced (0, 4);
            }
            void paint (Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setGradientFill (ColourGradient (Pro::cardTop(), 0.0f, r.getY(), Pro::cardBot(), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 8.0f);
                // header band
                {
                    Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (getLocalBounds().removeFromTop (headerH));
                    g.setColour (Theme::ink.withAlpha (0.035f));
                    g.fillRoundedRectangle (r.withHeight ((float) headerH + 10.0f), 8.0f);
                }
                g.setColour (Pro::shade (0.35f));
                g.drawHorizontalLine (headerH, r.getX() + 1.0f, r.getRight() - 1.0f);
                g.setColour (Theme::ink.withAlpha (0.08f));
                g.drawRoundedRectangle (r, 8.0f, 1.0f);
                g.setColour (accent);
                g.fillRoundedRectangle (Rectangle<float> (r.getX() + 11.0f, r.getY() + 9.0f, 3.0f, 12.0f), 1.5f);
                g.setColour (Theme::ink.withAlpha (0.9f));
                g.setFont (FontOptions (12.0f, Font::bold));
                g.drawText (title, Rectangle<float> (r.getX() + 20.0f, r.getY(), r.getWidth() - 30.0f, (float) headerH), Justification::centredLeft);
                for (auto& w : wells)
                {
                    auto a = w.area.toFloat();
                    g.setColour (Pro::shade (0.22f));
                    g.fillRoundedRectangle (a, 6.0f);
                    g.setColour (Theme::ink.withAlpha (0.05f));
                    g.drawRoundedRectangle (a, 6.0f, 1.0f);
                    g.setColour (accent.withAlpha (0.9f));
                    g.setFont (FontOptions (9.5f, Font::bold));
                    g.drawText (w.name, a.reduced (8.0f, 3.0f).removeFromTop (11.0f), Justification::centredLeft);
                }
            }
        };

        WavetablePanel (EditViewState& e, W& w) : evs (e), synth (w), holder (&w), scope (w)
        {
            const auto oscA = Colour (0xff4dabf7), oscB = Colour (0xffffa94d), filt = Colour (0xffff6b6b), env = Colour (0xff69db7c),
                       lfo = Colour (0xffda77f2), fxc = Colour (0xff38d9a9), voice = Colour (0xff748ffc);
            auto card = [this] (const String& t, Colour c) { auto* k = cards.add (new Card (t, c)); addAndMakeVisible (k); return k; };
            cOscA = card ("OSC A", oscA); cOscB = card ("OSC B", oscB); cVoice = card ("VOICE", voice);
            cF1 = card ("FILTER 1", filt); cF2 = card ("FILTER 2", filt.withRotatedHue (0.05f)); cEnv = card ("ENVELOPES", env);
            cLfo = card ("LFO", lfo); cMod = card ("MODULATION", lfo.withRotatedHue (0.1f)); cFx = card ("EFFECTS", fxc);

            using L = std::initializer_list<std::pair<int, const char*>>;
            framesA = std::make_unique<FramesView> (w, 0); framesA->flat = true; framesA->colour = oscA; cOscA->addAndMakeVisible (*framesA);
            framesB = std::make_unique<FramesView> (w, 1); framesB->flat = true; framesB->colour = oscB; cOscB->addAndMakeVisible (*framesB);
            tableA = combo (*cOscA, W::pSet1); warpA = combo (*cOscA, W::pWarp1);
            tableB = combo (*cOscB, W::pSet2); warpB = combo (*cOscB, W::pWarp2);
            for (auto [pi, l] : L { { W::pPos1, "Position" }, { W::pWarpAmt1, "Warp" }, { W::pSemi1, "Semi" }, { W::pFine1, "Fine" } }) knobsA.add (knob (*cOscA, pi, l, oscA));
            for (auto [pi, l] : L { { W::pPos2, "Position" }, { W::pWarpAmt2, "Warp" }, { W::pSemi2, "Semi" }, { W::pFine2, "Fine" } }) knobsB.add (knob (*cOscB, pi, l, oscB));
            for (auto [pi, l] : L { { W::pMix, "A / B" }, { W::pSub, "Sub" }, { W::pVoices, "Unison" }, { W::pDetune, "Detune" } }) knobsVoice.add (knob (*cVoice, pi, l, voice));

            graphF1 = std::make_unique<Graphs::FilterGraph> (w.param (W::pCutoff), w.param (W::pReso), w.param (W::pFilterType),
                                                             [] (int t) { const int m[] = { 1, 5, 2, 3 }; return m[jlimit (0, 3, t)]; }, filt, w.param (W::pFilterEnv));
            cF1->addAndMakeVisible (*graphF1);
            typeF1 = combo (*cF1, W::pFilterType);
            for (auto [pi, l] : L { { W::pCutoff, "Cutoff" }, { W::pReso, "Resonance" }, { W::pFilterEnv, "Envelope" } }) knobsF1.add (knob (*cF1, pi, l, filt));
            graphF2 = std::make_unique<Graphs::FilterGraph> (w.param (W::pF2Cutoff), w.param (W::pF2Reso), w.param (W::pF2Type),
                                                             [] (int t) { const int m[] = { 0, 1, 5, 2, 3, 4 }; return m[jlimit (0, 5, t)]; }, filt.withRotatedHue (0.05f), w.param (W::pF2Env));
            cF2->addAndMakeVisible (*graphF2);
            typeF2 = combo (*cF2, W::pF2Type); routing = combo (*cF2, W::pRouting);
            for (auto [pi, l] : L { { W::pF2Cutoff, "Cutoff" }, { W::pF2Reso, "Resonance" }, { W::pF2Env, "Envelope" } }) knobsF2.add (knob (*cF2, pi, l, filt.withRotatedHue (0.05f)));

            using EG = Graphs::EnvelopeGraph;
            envGraph = std::make_unique<EG> (std::vector<EG::Env> { { w.param (W::pA), w.param (W::pD), w.param (W::pS), w.param (W::pR), env, "AMP" },
                                                                   { w.param (W::pFA), w.param (W::pFD), w.param (W::pFS), w.param (W::pFR), filt, "FILTER" },
                                                                   { w.param (W::pMA), w.param (W::pMD), w.param (W::pMS), w.param (W::pMR), lfo, "MOD" } });
            cEnv->addAndMakeVisible (*envGraph);
            const char* envNames[] = { "Amp", "Filter", "Mod" };
            for (int i = 0; i < 3; ++i)
            {
                auto* b = envTabs.add (new TextButton (envNames[i]));
                b->setColour (TextButton::buttonOnColourId, (i == 0 ? env : i == 1 ? filt : lfo).withAlpha (0.8f));
                b->setColour (TextButton::textColourOnId, Colour (0xff111317));
                b->setConnectedEdges ((i > 0 ? Button::ConnectedOnLeft : 0) | (i < 2 ? Button::ConnectedOnRight : 0));
                b->onClick = [this, i] { showEnv (i); };
                cEnv->addAndMakeVisible (b);
            }

            lfoView = std::make_unique<WavetableUI::LfoView> (w);
            lfoView->onSelect = [this] (int l) { showLfo (l); };
            cLfo->addAndMakeVisible (*lfoView);
            matrix = std::make_unique<WavetableUI::MatrixView> (w);
            cMod->addAndMakeVisible (*matrix);

            for (auto [pi, l] : L { { W::pDrive, "Amount" }, { W::pChorusMix, "Mix" }, { W::pChorusRate, "Rate" }, { W::pChorusDepth, "Depth" },
                                    { W::pDelayMix, "Mix" }, { W::pDelayFb, "Feedback" }, { W::pReverbMix, "Mix" }, { W::pReverbSize, "Size" } })
                knobsFx.add (knob (*cFx, pi, l, fxc));
            delayTime = combo (*cFx, W::pDelayTime);

            for (auto [pi, l] : L { { W::pGlide, "Glide" }, { W::pBendRange, "Bend" }, { W::pPressure, "Pressure" }, { W::pSlide, "Slide" }, { W::pLevel, "Level" } })
            {
                auto* k = new ParamKnob (evs, *w.param (pi));
                k->setLabel (l);
                k->setCompact (voice);
                barKnobs.add (k);
                addAndMakeVisible (k);
            }
            addAndMakeVisible (scope);
            showEnv (0);
            showLfo (0);
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Pro::bg());
            auto bar = getLocalBounds().removeFromBottom (barH).toFloat();
            g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, bar.getY(), Pro::barBot(), 0.0f, bar.getBottom(), false));
            g.fillRect (bar);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawHorizontalLine ((int) bar.getY(), 0.0f, (float) getWidth());
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ("PLAY", bar.reduced (16.0f, 0.0f), Justification::centredLeft);
            g.drawText ("OUTPUT", scope.getBounds().toFloat().translated (-64.0f, 0.0f).withWidth (58.0f), Justification::centredRight);
        }

        /** Knobs in a grid, all the same size, centred in their cells. */
        template <typename K>
        static void placeKnobs (Rectangle<int> area, const Array<K*>& ks, int cols = 0, int maxW = 78, int maxH = 82)
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
            auto r = getLocalBounds();
            auto bar = r.removeFromBottom (barH).reduced (0, 5);
            bar.removeFromLeft (64);
            scope.setBounds (bar.removeFromRight (jmin (300, bar.getWidth() / 3)).withTrimmedRight (12));
            bar.removeFromRight (70);
            for (auto* k : barKnobs) { k->setBounds (bar.removeFromLeft (74)); bar.removeFromLeft (6); }

            r = r.reduced (8);
            const int gap = 8;
            auto top = r.removeFromTop (r.getHeight() * 35 / 100); r.removeFromTop (gap);
            auto mid = r.removeFromTop (r.getHeight() * 45 / 100); r.removeFromTop (gap);
            auto bottom = r;

            // oscillators + voice
            auto voiceArea = top.removeFromRight (jlimit (150, 190, top.getWidth() / 6));
            top.removeFromRight (gap);
            cOscA->setBounds (top.removeFromLeft ((top.getWidth() - gap) / 2)); top.removeFromLeft (gap);
            cOscB->setBounds (top);
            cVoice->setBounds (voiceArea);
            layoutOsc (*cOscA, *framesA, *tableA, *warpA, knobsA);
            layoutOsc (*cOscB, *framesB, *tableB, *warpB, knobsB);
            placeKnobs (cVoice->content(), knobsVoice, 2);

            // filters + envelopes
            auto envArea = mid.removeFromRight (mid.getWidth() * 36 / 100); mid.removeFromRight (gap);
            cF1->setBounds (mid.removeFromLeft ((mid.getWidth() - gap) / 2)); mid.removeFromLeft (gap);
            cF2->setBounds (mid);
            cEnv->setBounds (envArea);
            auto layoutFilter = [] (Card& card, Component& graph, Component& type, Component* extra, Array<ParamKnob*>& ks)
            {
                auto head = card.header();
                if (extra != nullptr) { extra->setBounds (head.removeFromRight (jmin (130, head.getWidth() / 2 - 3))); head.removeFromRight (6); }
                type.setBounds (head.removeFromRight (jmin (130, head.getWidth())));
                auto c = card.content();
                placeKnobs (c.removeFromBottom (jmin (76, c.getHeight() / 2)), ks);
                c.removeFromBottom (4);
                graph.setBounds (c);
            };
            layoutFilter (*cF1, *graphF1, *typeF1, nullptr, knobsF1);
            layoutFilter (*cF2, *graphF2, *typeF2, routing, knobsF2);
            {
                auto tabs = cEnv->header();
                tabs = tabs.removeFromRight (jmin (210, tabs.getWidth()));
                const int tw = tabs.getWidth() / 3;
                for (auto* t : envTabs) t->setBounds (tabs.removeFromLeft (tw));
                auto c = cEnv->content();
                placeKnobs (c.removeFromBottom (jmin (76, c.getHeight() / 2)), envKnobs);
                c.removeFromBottom (4);
                envGraph->setBounds (c);
            }

            // LFOs, modulation, effects
            cLfo->setBounds (bottom.removeFromLeft (bottom.getWidth() * 28 / 100)); bottom.removeFromLeft (gap);
            cFx->setBounds (bottom.removeFromRight (bottom.getWidth() * 57 / 100)); bottom.removeFromRight (gap);
            cMod->setBounds (bottom);
            {
                auto c = cLfo->content();
                auto side = c.removeFromRight (92);
                c.removeFromRight (6);
                lfoView->setBounds (c);
                if (lfoKnobs.size() == 3)
                {
                    lfoKnobs[2]->setBounds (side.removeFromBottom (26));
                    side.removeFromBottom (6);
                    lfoKnobs[1]->setBounds (side.removeFromBottom (42));
                    side.removeFromBottom (2);
                    lfoKnobs[0]->setBounds (side.withSizeKeepingCentre (jmin (78, side.getWidth()), jmin (82, side.getHeight())));
                }
            }
            matrix->setBounds (cMod->content());
            {
                // four captioned groups in two rows: drive + chorus, delay + reverb
                auto c = cFx->content();
                const int rowH = (c.getHeight() - 6) / 2;
                auto r1 = c.removeFromTop (rowH); c.removeFromTop (6);
                auto r2 = c;
                cFx->wells.clear();
                auto group = [this] (Rectangle<int> area, const String& name, std::initializer_list<Component*> items)
                {
                    cFx->wells.push_back ({ area, name });
                    auto in = area.reduced (4, 0).withTrimmedTop (14).withTrimmedBottom (2);
                    const int cw = in.getWidth() / (int) items.size();
                    for (auto* it : items)
                    {
                        auto cell = in.removeFromLeft (cw);
                        if (dynamic_cast<ParamKnob*> (it) != nullptr) it->setBounds (cell.withSizeKeepingCentre (jmin (cw, 78), jmin (cell.getHeight(), 82)));
                        else it->setBounds (cell.withSizeKeepingCentre (jmin (cw - 2, 110), jmin (cell.getHeight(), 28)));
                    }
                };
                const int u1 = (r1.getWidth() - 6) / 4;
                group (r1.removeFromLeft (u1), "DRIVE", { knobsFx[0] });
                r1.removeFromLeft (6);
                group (r1, "CHORUS", { knobsFx[1], knobsFx[2], knobsFx[3] });
                const int u2 = (r2.getWidth() - 6) / 5;
                group (r2.removeFromLeft (u2 * 3), "DELAY", { knobsFx[4], delayTime, knobsFx[5] });
                r2.removeFromLeft (6);
                group (r2, "REVERB", { knobsFx[6], knobsFx[7] });
                cFx->repaint();
            }
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
        ParamCombo* combo (Card& c, int pi)
        {
            auto* cb = new ParamCombo (*synth.param (pi), false);
            owned.add (cb);
            c.addAndMakeVisible (cb);
            return cb;
        }
        /** The table and warp menus in the header, the wavetable on the left, a 2 x 2 of knobs on the right. */
        void layoutOsc (Card& c, Component& frames, Component& table, Component& warp, Array<ParamKnob*>& ks)
        {
            auto head = c.header();
            warp.setBounds (head.removeFromRight (jmin (120, head.getWidth() / 2 - 3)));
            head.removeFromRight (6);
            table.setBounds (head.removeFromRight (jmin (160, head.getWidth())));
            auto r = c.content();
            placeKnobs (r.removeFromRight (jlimit (120, 164, r.getWidth() * 36 / 100)), ks, 2);
            r.removeFromRight (6);
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
            sync->setLabel ("Sync");
            owned.add (sync); cLfo->addAndMakeVisible (sync); lfoKnobs.add (sync);
            auto* retrig = new ParamToggle (*synth.param (synth.lfoParam (l, 3)), "Per note", col);
            owned.add (retrig); cLfo->addAndMakeVisible (retrig); lfoKnobs.add (retrig);
            resized();
        }

        static constexpr int barH = 84;
        EditViewState& evs;
        W& synth;
        te::Plugin::Ptr holder;
        SynthScope scope;
        OwnedArray<Card> cards;
        OwnedArray<Component> owned;
        Card *cOscA = nullptr, *cOscB = nullptr, *cVoice = nullptr, *cF1 = nullptr, *cF2 = nullptr, *cEnv = nullptr, *cLfo = nullptr, *cMod = nullptr, *cFx = nullptr;
        std::unique_ptr<FramesView> framesA, framesB;
        ParamCombo *tableA = nullptr, *warpA = nullptr, *tableB = nullptr, *warpB = nullptr, *typeF2 = nullptr, *routing = nullptr, *delayTime = nullptr;
        ParamCombo* typeF1 = nullptr;
        std::unique_ptr<Graphs::FilterGraph> graphF1, graphF2;
        std::unique_ptr<Graphs::EnvelopeGraph> envGraph;
        std::unique_ptr<WavetableUI::LfoView> lfoView;
        std::unique_ptr<WavetableUI::MatrixView> matrix;
        Array<ParamKnob*> knobsA, knobsB, knobsVoice, knobsF1, knobsF2, knobsFx, envKnobs;
        Array<Component*> lfoKnobs;
        OwnedArray<TextButton> envTabs;
        OwnedArray<ParamKnob> barKnobs;
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
        {
            using S = pulse::Simpler;
            add ("Sample", std::make_unique<DeviceDisplays::SimplerDisplay> (*s));
            add ("Filter", std::make_unique<Graphs::FilterGraph> (s->param (S::pCutoff), s->param (S::pReso), s->param (S::pFilterType),
                                                                  [] (int t) { return jlimit (0, 3, t); }, Theme::orange, s->param (S::pFEnv)));
            add ("Envelopes", env ({ { s->param (S::pA), s->param (S::pD), s->param (S::pS), s->param (S::pR), Theme::accent, "AMP" },
                                     { s->param (S::pFA), s->param (S::pFD), s->param (S::pFS), s->param (S::pFR), Theme::orange, "FILTER" } }));
        }
        else if (auto s = dynamic_cast<pulse::FMSynth*> (&p))
        {
            using F = pulse::FMSynth;
            add ("Algorithm", std::make_unique<DeviceDisplays::FMDisplay> (*s));
            std::vector<EG::Env> ops;
            const Colour cols[] = { Theme::orange, Theme::accent, Colour (0xff69db7c), Theme::purple };
            for (int op = 0; op < F::numOps; ++op)
                ops.push_back ({ s->param (F::opParam (op, F::kA)), s->param (F::opParam (op, F::kD)), s->param (F::opParam (op, F::kS)),
                                 s->param (F::opParam (op, F::kR)), cols[op], "OP " + String (op + 1) });
            add ("Operator envelopes", env (ops));
            add ("Filter", std::make_unique<Graphs::FilterGraph> (s->param (F::pCutoff), s->param (F::pReso), nullptr, nullptr, Theme::orange, s->param (F::pFEnv)));
            add ("Filter envelope", env ({ { s->param (F::pFA), s->param (F::pFD), s->param (F::pFS), s->param (F::pFR), Theme::orange, "FILTER" } }));
        }
        else if (auto s = dynamic_cast<pulse::DrumSynth*> (&p))
            add ("Hits", std::make_unique<DrumHitsView> (*s));
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
            || dynamic_cast<pulse::Reverb*> (&p) != nullptr || dynamic_cast<pulse::Delay*> (&p) != nullptr || dynamic_cast<pulse::Saturator*> (&p) != nullptr
            || dynamic_cast<pulse::MultibandDynamics*> (&p) != nullptr || dynamic_cast<pulse::Limiter*> (&p) != nullptr;
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
                for (auto& pr : FactoryPresets::forType (plugin->getPluginType()))
                    m.addItem (pr.name, [this, name = String (pr.name)]
                    {
                        plugin->edit.getUndoManager().beginNewTransaction ("Factory preset");
                        FactoryPresets::apply (*plugin, name);
                    });
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
            setSize (1100, 760);
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
            for (auto* b : { &resetBtn, &randomBtn, &presetBtn }) { b->setBounds (top.removeFromRight (110)); top.removeFromRight (6); }
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
                setResizeLimits (560, 420, 3000, 2000);
                centreWithSize (getWidth(), getHeight());
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
