#pragma once

#include "DeviceDisplays.h"
#include "FactoryPresets.h"
#include "DeviceViews.h"
#include "Presets.h"
#include "WavetableModUI.h"

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
        FramesView (pulse::WavetableSynth& s, int oscIndex) : holder (&s), synth (&s), osc (oscIndex) { startTimerHz (15); }

        void paint (Graphics& g) override
        {
            using W = pulse::WavetableSynth;
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            const int set = synth->getInt (osc == 0 ? W::pSet1 : W::pSet2);
            const float pos = synth->get (osc == 0 ? W::pPos1 : W::pPos2);
            const auto user = synth->getUserTable();
            auto a = b.reduced (12.0f, 22.0f);
            const int frames = pulse::WavetableBank::numFrames;
            const float depthX = a.getWidth() * 0.18f, depthY = a.getHeight() * 0.45f;
            const float w = a.getWidth() - depthX, h = a.getHeight() - depthY;
            const int current = roundToInt (pos * (frames - 1));
            for (int f = frames; --f >= 0;)   // back to front
            {
                const float ox = a.getX() + depthX * (1.0f - f / (float) (frames - 1)), oy = a.getY() + depthY * (1.0f - f / (float) (frames - 1));
                const float* t = synth->tableFor (set, f, 0, user.get());
                Path p;
                for (int i = 0; i <= 96; ++i)
                {
                    const float v = t[i * pulse::WavetableBank::tableSize / 96];
                    const float x = ox + w * i / 96.0f, y = oy + h * 0.5f - v * h * 0.45f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                const bool hot = f == current;
                g.setColour (hot ? (osc == 0 ? Theme::accent : Theme::orange) : Theme::ink.withAlpha (0.08f + 0.12f * f / frames));
                g.strokePath (p, PathStrokeType (hot ? 2.2f : 1.0f));
            }
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ("OSC " + String (osc + 1) + "  " + pulse::WavetableBank::setNames[set] + "   position " + String (roundToInt (pos * 100)) + "%",
                        b.reduced (8.0f, 4.0f).removeFromTop (14.0f), Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            using W = pulse::WavetableSynth;
            const double sig = synth->get (W::pSet1) * 3 + synth->get (W::pPos1) * 7 + synth->get (W::pSet2) * 11 + synth->get (W::pPos2) * 13
                               + (double) (pointer_sized_int) synth->getUserTable().get();
            if (sig != last) { last = sig; repaint(); }
        }
        te::Plugin::Ptr holder;
        pulse::WavetableSynth* synth;
        int osc;
        double last = -1;
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
    using Graph = std::pair<String, std::unique_ptr<Component>>;

    /** Every view of a device, big. */
    inline std::vector<Graph> graphsFor (EditViewState& evs, te::Plugin& p)
    {
        using EG = Graphs::EnvelopeGraph;
        std::vector<Graph> v;
        auto add = [&v] (String t, std::unique_ptr<Component> c) { v.push_back ({ std::move (t), std::move (c) }); };
        auto env = [] (std::vector<EG::Env> e) { return std::make_unique<EG> (std::move (e)); };

        if (auto s = dynamic_cast<pulse::WavetableSynth*> (&p))
        {
            using W = pulse::WavetableSynth;
            add ("Oscillator 1", std::make_unique<FramesView> (*s, 0));
            add ("Oscillator 2", std::make_unique<FramesView> (*s, 1));
            add ("Filter", std::make_unique<Graphs::FilterGraph> (s->param (W::pCutoff), s->param (W::pReso), s->param (W::pFilterType),
                                                                  [] (int t) { const int m[] = { 1, 5, 2, 3 }; return m[jlimit (0, 3, t)]; }, Theme::orange, s->param (W::pFilterEnv)));
            add ("Envelopes", env ({ { s->param (W::pA), s->param (W::pD), s->param (W::pS), s->param (W::pR), Theme::accent, "AMP" },
                                     { s->param (W::pFA), s->param (W::pFD), s->param (W::pFS), s->param (W::pFR), Theme::orange, "FILTER" },
                                     { s->param (W::pMA), s->param (W::pMD), s->param (W::pMS), s->param (W::pMR), Theme::purple, "MOD" } }));
            add ("Filter 2", std::make_unique<Graphs::FilterGraph> (s->param (W::pF2Cutoff), s->param (W::pF2Reso), s->param (W::pF2Type),
                                                                    [] (int t) { const int m[] = { 0, 1, 5, 2, 3, 4 }; return m[jlimit (0, 5, t)]; }, Theme::purple, s->param (W::pF2Env)));
            add ("LFOs", std::make_unique<WavetableUI::LfoView> (*s));
            add ("Modulation (drag a source onto any knob)", std::make_unique<WavetableUI::MatrixView> (*s));
        }
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
            add ("Gain reduction", std::make_unique<HistoryGraph> ([s] { return s->gainReductionDb.load(); }, 24.0f, "dB", Theme::orange));
        }
        else if (auto s = dynamic_cast<pulse::Limiter*> (&p))
            add ("Limiter", std::make_unique<DynamicsUI::LimiterPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::ProCompressor*> (&p))
            add ("Compressor", std::make_unique<DynamicsUI::CompressorPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::MultibandDynamics*> (&p))
            add ("Multiband", std::make_unique<DynamicsUI::MultibandPanel> (evs, *s));
        else if (auto s = dynamic_cast<pulse::AutoFilter*> (&p))
        {
            using A = pulse::AutoFilter;
            auto modes = [] (int t) { const int m[] = { 1, 2, 3, 4, 5 }; return m[jlimit (0, 4, t)]; };
            add ("Filter", std::make_unique<Graphs::FilterGraph> (s->param (A::pCutoff), s->param (A::pReso), s->param (A::pType), modes, Theme::accent, s->param (A::pEnvAmount)));
            add ("LFO", std::make_unique<DeviceDisplays::AutoFilterDisplay> (*s));
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
                grp->header.setButtonText (sec.name);
                grp->header.setTooltip ("Click to fold or unfold this section");
                grp->header.onClick = [this, grp] { grp->collapsed = ! grp->collapsed; layoutControls(); };
                controls.addAndMakeVisible (grp->header);
                for (auto idx : sec.params)
                {
                    auto& prm = *p.param (idx);
                    auto shortName = prm.getParameterName();
                    if (shortName.startsWithIgnoreCase (sec.name + " ")) shortName = shortName.substring (sec.name.length() + 1);
                    // choices are buttons, on / off parameters a toggle, everything else a knob
                    Component* c = nullptr;
                    int span = 1;
                    if (ParamButtons::isToggle (prm))
                        c = new ParamToggle (prm, shortName, accent);
                    else if (ParamButtons::numOptions (prm) > 0 && ParamButtons::numOptions (prm) <= 4)
                    {
                        auto* ch = new ParamChoice (prm, accent);
                        span = jlimit (2, 4, (ch->idealWidth() + 69) / 70);
                        c = ch;
                    }
                    else if (ParamButtons::numOptions (prm) > 4)
                    {
                        auto* cb = new ParamCombo (prm);
                        cb->setLabel (shortName);
                        span = 2;
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
            for (auto* b : { &randomBtn, &resetBtn, &foldBtn, &abBtn }) addAndMakeVisible (b);
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
            foldBtn.onClick = [this]
            {
                bool any = false;
                for (auto* g : groups) any = any || ! g->collapsed;
                for (auto* g : groups) g->collapsed = any;
                layoutControls();
            };
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
            g.fillAll (Theme::bg);
            g.setColour (Theme::panel);
            g.fillRect (getLocalBounds().removeFromTop (44));
            g.setColour (plugin->isSynth() ? Theme::orange : Theme::accent);
            g.fillRect (0, 42, getWidth(), 2);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (44).reduced (12, 8);
            for (auto* b : { &foldBtn, &resetBtn, &randomBtn, &presetBtn }) { b->setBounds (top.removeFromRight (110)); top.removeFromRight (6); }
            abBtn.setBounds (top.removeFromRight (44)); top.removeFromRight (6);
            if (windowBtn.isVisible()) { windowBtn.setBounds (top.removeFromRight (130)); top.removeFromRight (6); }
            title.setBounds (top);
            r.reduce (12, 8);

            // graphs: a grid of up to 3 columns (2 x 2 for four), about half the window - more when
            // the controls need less room
            if (hasFullEditor (*plugin) && ! graphs.empty())   // its own full editor
            {
                for (auto* t : graphTitles) t->setVisible (false);
                viewport.setVisible (false);
                graphs[0]->setBounds (r);
                return;
            }
            if (! graphs.empty())
            {
                const int n = (int) graphs.size();
                const int cols = jlimit (1, n == 4 ? 2 : 3, jmin (n, r.getWidth() / 330));
                const int rows = (n + cols - 1) / cols;
                viewport.setBounds (r);
                layoutControls();
                const int spare = r.getHeight() - controls.getHeight() - 14;
                const int h = jmax (jmin (rows * 250, (int) (r.getHeight() * 0.58f)), jmin (spare, rows * 380));
                auto area = r.removeFromTop (h);
                r.removeFromTop (10);
                const int cw = area.getWidth() / cols, rh = area.getHeight() / rows;
                for (int i = 0; i < (int) graphs.size(); ++i)
                {
                    auto cell = Rectangle<int> (area.getX() + (i % cols) * cw, area.getY() + (i / cols) * rh, cw, rh).reduced (5);
                    graphTitles[i]->setBounds (cell.removeFromTop (16));
                    graphs[(size_t) i]->setBounds (cell);
                }
            }
            viewport.setBounds (r);
            layoutControls();
        }

    private:
        struct Group
        {
            TextButton header;
            String name;
            OwnedArray<Component> knobs;   // knobs, toggles and option buttons
            Array<int> spans;              // cells each control takes
            bool collapsed = false;
        };

        void layoutControls()
        {
            const int w = jmax (200, viewport.getWidth() - viewport.getScrollBarThickness() - 4);
            constexpr int kw = 70, kh = 80;
            int x = 0, y = 0, rowH = 0;
            for (auto* g : groups)
            {
                // a group is a block: header across its controls; blocks flow left to right.
                // Controls take cells (a knob or toggle one, option buttons two or more).
                int cells = 0;
                for (auto sp : g->spans) cells += sp;
                const int cols = jlimit (1, 8, jmax (1, cells));
                const int bw = g->collapsed ? 130 : jmax (130, jmin (cols, jmax (1, (w - 8) / kw)) * kw);
                const int perRow = jmax (1, bw / kw);
                if (x > 0 && x + bw > w) { x = 0; y += rowH; rowH = 0; }
                int cx = 0, row = 0;
                for (int i = 0; i < g->knobs.size(); ++i)
                {
                    const int sp = jmin (perRow, g->spans[i]);
                    if (cx > 0 && cx + sp > perRow) { cx = 0; ++row; }
                    auto* k = g->knobs[i];
                    k->setVisible (! g->collapsed);
                    auto cell = Rectangle<int> (x + cx * kw, y + 26 + row * kh, sp * kw, kh);
                    if (dynamic_cast<ParamToggle*> (k) != nullptr) cell = cell.withSizeKeepingCentre (kw - 8, 28);
                    else if (dynamic_cast<ParamChoice*> (k) != nullptr || dynamic_cast<ParamCombo*> (k) != nullptr) cell = cell.reduced (4, 14);
                    k->setBounds (cell);
                    cx += sp;
                }
                const int bh = 26 + (g->collapsed ? 0 : (row + 1) * kh) + 10;
                g->header.setBounds (x, y, bw - 8, 22);
                g->header.setButtonText (String (g->collapsed ? CharPointer_UTF8 ("\xe2\x96\xb8  ") : CharPointer_UTF8 ("\xe2\x96\xbe  "))
                                         + g->name);
                x += bw + 12;
                rowH = jmax (rowH, bh);
            }
            controls.setSize (w, y + rowH + 8);
        }

        EditViewState& evs;
        te::Plugin::Ptr holder;
        pulse::PulsePlugin* plugin;
        std::vector<std::unique_ptr<Component>> graphs;
        OwnedArray<Label> graphTitles;
        Viewport viewport;
        Component controls;
        OwnedArray<Group> groups;
        Label title;
        TextButton presetBtn { "Presets" }, randomBtn { "Randomise" }, resetBtn { "Reset" }, foldBtn { "Fold all" }, abBtn { "A" }, windowBtn { "Plugin window" };
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
