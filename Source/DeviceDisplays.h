#pragma once

#include "ParamKnob.h"
#include "PulseSynths.h"
#include "ClipImport.h"

/** Visual panels shown at the left of Pulse device cards. */
namespace DeviceDisplays
{
    inline void paintPanel (Graphics& g, Rectangle<float> r)
    {
        g.setColour (Colour (0xff111317));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
    }

    /** Repaints when any of the plugin's parameters move (polled, cheap). */
    class Polled : public Component, private Timer
    {
    public:
        explicit Polled (pulse::PulsePlugin& p, int hz = 15) : plugin (&p) { startTimerHz (hz); }

    protected:
        virtual bool alwaysRepaint() const { return false; }
        te::Plugin::Ptr plugin;

        pulse::PulsePlugin& pp() const { return *dynamic_cast<pulse::PulsePlugin*> (plugin.get()); }

        void timerCallback() override
        {
            double h = 0;
            for (int i = 0; i < pp().numParams(); ++i) h = h * 1.0003 + pp().get (i) * (i + 1);
            if (h != lastHash || alwaysRepaint()) { lastHash = h; repaint(); }
        }

    private:
        double lastHash = -1;
    };

    //==============================================================================
    class WavetableDisplay : public Polled
    {
    public:
        explicit WavetableDisplay (pulse::WavetableSynth& s) : Polled (s) {}

        void paint (Graphics& g) override
        {
            using W = pulse::WavetableSynth;
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            paintPanel (g, r);
            const int sets[2] = { pp().getInt (W::pSet1), pp().getInt (W::pSet2) };
            const float pos[2] = { pp().get (W::pPos1), pp().get (W::pPos2) };
            const Colour cols[2] = { Theme::accent, Theme::orange };

            for (int osc = 0; osc < 2; ++osc)
            {
                auto area = (osc == 0 ? r.removeFromTop (r.getHeight() / 2.0f) : r).reduced (8.0f, 6.0f);
                g.setColour (Theme::muted);
                g.setFont (FontOptions (11.0f));
                g.drawText ("OSC " + String (osc + 1) + "  " + pulse::WavetableBank::setNames[sets[osc]], area.removeFromTop (12.0f), Justification::centredLeft);

                auto& bank = pulse::WavetableBank::get();
                const float fpos = pos[osc] * (pulse::WavetableBank::numFrames - 1);
                const int f0 = jmin (pulse::WavetableBank::numFrames - 2, (int) fpos);
                const float frac = fpos - (float) f0;
                auto* a = bank.table (sets[osc], f0, 0);
                auto* b = bank.table (sets[osc], f0 + 1, 0);

                Path p;
                const int points = jmax (2, (int) area.getWidth());
                for (int i = 0; i < points; ++i)
                {
                    const int idx = i * pulse::WavetableBank::tableSize / points;
                    const float v = a[idx] + frac * (b[idx] - a[idx]);
                    const float x = area.getX() + (float) i, y = area.getCentreY() - v * area.getHeight() * 0.48f;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.setColour (Theme::line);
                g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
                g.setColour (cols[osc]);
                g.strokePath (p, PathStrokeType (1.6f));
            }
        }
    };

    //==============================================================================
    template <typename Device>
    class GRMeter : public Polled
    {
    public:
        explicit GRMeter (Device& c) : Polled (c, 30), comp (c) {}

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            paintPanel (g, r);
            auto text = r.removeFromBottom (16.0f);
            auto bar = r.reduced (14.0f, 8.0f);
            g.setColour (Colour (0xff1d2026));
            g.fillRect (bar);
            const float gr = jlimit (0.0f, 20.0f, comp.gainReductionDb.load());
            g.setColour (Theme::orange);
            g.fillRect (bar.withHeight (bar.getHeight() * gr / 20.0f));
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f));
            g.drawText ("GR " + String (gr, 1), text, Justification::centred);
            for (int db : { 4, 8, 12, 16 })
                g.drawHorizontalLine ((int) (bar.getY() + bar.getHeight() * db / 20.0f), bar.getX() - 4.0f, bar.getX());
        }

    private:
        bool alwaysRepaint() const override { return true; }
        Device& comp;
    };

    //==============================================================================
    class SaturatorCurve : public Polled
    {
    public:
        explicit SaturatorCurve (pulse::Saturator& s) : Polled (s), sat (s) {}

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            paintPanel (g, r);
            auto a = r.reduced (8.0f);
            g.setColour (Theme::line);
            g.drawHorizontalLine ((int) a.getCentreY(), a.getX(), a.getRight());
            g.drawVerticalLine ((int) a.getCentreX(), a.getY(), a.getBottom());
            g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 0.6f);

            Path p;
            for (int i = 0; i <= 100; ++i)
            {
                const float x = (float) i / 50.0f - 1.0f;
                const float y = jlimit (-1.2f, 1.2f, sat.shaped (x));
                const float px = a.getCentreX() + x * a.getWidth() * 0.5f, py = a.getCentreY() - y * a.getHeight() * 0.5f / 1.2f;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (Theme::orange);
            g.strokePath (p, PathStrokeType (1.8f));
        }

    private:
        pulse::Saturator& sat;
    };

    //==============================================================================
    /** Log-frequency helpers shared by the EQ and filter displays. */
    struct FreqAxis
    {
        Rectangle<float> area;
        float xFor (double f) const { return area.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); }
        double freqAt (float x) const { return 20.0 * std::pow (1000.0, (x - area.getX()) / area.getWidth()); }

        void drawGrid (Graphics& g) const
        {
            g.setColour (Colours::white.withAlpha (0.06f));
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
                g.drawVerticalLine ((int) xFor (f), area.getY(), area.getBottom());
            g.setColour (Theme::muted.withAlpha (0.6f));
            g.setFont (FontOptions (10.0f));
            for (auto [f, t] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
                g.drawText (t, Rectangle<float> (xFor (f) + 2.0f, area.getBottom() - 11.0f, 30.0f, 10.0f), Justification::left);
        }
    };

    class AutoFilterDisplay : public Polled
    {
    public:
        explicit AutoFilterDisplay (pulse::AutoFilter& f) : Polled (f, 30), filter (f) {}

        void paint (Graphics& g) override
        {
            using A = pulse::AutoFilter;
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            paintPanel (g, r);
            FreqAxis axis { r.reduced (6.0f, 8.0f) };
            axis.drawGrid (g);

            // LFO shape preview along the top
            auto lfoArea = axis.area.removeFromTop (axis.area.getHeight() * 0.35f).reduced (0, 4.0f);
            Path lfo;
            for (int i = 0; i <= 60; ++i)
            {
                const float v = A::lfoValue (pp().getInt (A::pLfoShape), i / 30.0, (i / 10) % 2 ? 0.6f : -0.4f);
                const float x = lfoArea.getX() + lfoArea.getWidth() * i / 60.0f, y = lfoArea.getCentreY() - v * lfoArea.getHeight() * 0.45f;
                if (i == 0) lfo.startNewSubPath (x, y); else lfo.lineTo (x, y);
            }
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.strokePath (lfo, PathStrokeType (1.2f));

            // base cutoff and the modulated cutoff
            const float base = axis.xFor (pp().get (A::pCutoff)), now = axis.xFor (filter.currentCutoff.load());
            g.setColour (Theme::muted);
            g.drawVerticalLine ((int) base, axis.area.getY(), axis.area.getBottom());
            g.setColour (Theme::orange);
            g.fillRect (now - 1.5f, axis.area.getY(), 3.0f, axis.area.getHeight());
            g.setFont (FontOptions (11.0f));
            g.drawText (A::typeNames[pp().getInt (A::pType)], r.removeFromBottom (14.0f), Justification::centred);
        }

    private:
        bool alwaysRepaint() const override { return true; }
        pulse::AutoFilter& filter;
    };

    //==============================================================================
    /** EQ curve + analyser with draggable band nodes, and the selected band's controls. */
    class EQDisplay : public Polled
    {
    public:
        EQDisplay (EditViewState& e, pulse::PulseEQ& q) : Polled (q, 30), evs (e), eq (q), fft (11)
        {
            for (int b = 0; b < pulse::PulseEQ::numBands; ++b)
            {
                auto* btn = bandButtons.add (new TextButton (String (b + 1)));
                btn->setClickingTogglesState (false);
                btn->setColour (TextButton::buttonOnColourId, bandColour (b).withAlpha (0.8f));
                btn->onClick = [this, b] { selectBand (b); };
                btn->setTooltip ("Select band " + String (b + 1) + " (the On button turns it on/off)");
                addAndMakeVisible (btn);
            }
            onBtn.setClickingTogglesState (false);
            onBtn.setColour (TextButton::buttonOnColourId, Theme::green);
            onBtn.onClick = [this]
            {
                auto i = pulse::PulseEQ::idx (selected, pulse::PulseEQ::pOn);
                eq.set (i, eq.getInt (i) > 0 ? 0.0f : 1.0f);
            };
            addAndMakeVisible (onBtn);
            selectBand (2);
            spectrum.fill (-120.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            auto bottom = r.removeFromBottom (80);
            graph = r.toFloat();
            auto btns = bottom.removeFromLeft (150).withSizeKeepingCentre (150, 52);
            for (int i = 0; i < bandButtons.size(); ++i)
                bandButtons[i]->setBounds (btns.getX() + (i % 3) * 50, btns.getY() + (i / 3) * 27, 46, 24);
            onBtn.setBounds (bottom.removeFromLeft (44).withSizeKeepingCentre (40, 24));
            for (auto* k : knobs)
                k->setBounds (bottom.removeFromLeft (64));
        }

        void paint (Graphics& g) override
        {
            paintPanel (g, graph);
            FreqAxis axis { graph.reduced (4.0f) };
            axis.drawGrid (g);
            auto dbToY = [this] (double db) { return graph.getCentreY() - (float) (db / 20.0) * graph.getHeight() * 0.5f; };
            g.setColour (Colours::white.withAlpha (0.1f));
            g.drawHorizontalLine ((int) dbToY (0.0), graph.getX(), graph.getRight());

            // analyser (post EQ)
            Path spec;
            for (int i = 0; i < (int) axis.area.getWidth(); i += 2)
            {
                const double f = axis.freqAt (axis.area.getX() + (float) i);
                const int bin = jlimit (1, fftSize / 2 - 1, (int) (f / sampleRateGuess * fftSize));
                const float db = jlimit (-90.0f, 0.0f, spectrum[(size_t) bin]);
                const float y = graph.getBottom() - (db + 90.0f) / 90.0f * graph.getHeight();
                if (i == 0) spec.startNewSubPath (axis.area.getX(), graph.getBottom());
                spec.lineTo (axis.area.getX() + (float) i, y);
            }
            spec.lineTo (axis.area.getRight(), graph.getBottom());
            spec.closeSubPath();
            g.setColour (Colours::white.withAlpha (0.07f));
            g.fillPath (spec);

            // response curve
            Path curve;
            for (int i = 0; i <= (int) axis.area.getWidth(); ++i)
            {
                const float x = axis.area.getX() + (float) i;
                const float y = jlimit (graph.getY(), graph.getBottom(), dbToY (eq.responseDb (axis.freqAt (x))));
                if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
            }
            g.setColour (Theme::accent);
            g.strokePath (curve, PathStrokeType (2.0f));

            // band nodes
            for (int b = 0; b < pulse::PulseEQ::numBands; ++b)
            {
                auto c = nodePos (axis, b);
                const bool on = eq.isBandOn (b);
                g.setColour (bandColour (b).withAlpha (on ? 1.0f : 0.35f));
                g.fillEllipse (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f);
                if (b == selected) { g.setColour (Colours::white); g.drawEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f, 1.5f); }
                g.setColour (Colours::black);
                g.setFont (FontOptions (10.0f, Font::bold));
                g.drawText (String (b + 1), Rectangle<float> (c.x - 6.0f, c.y - 6.0f, 12.0f, 12.0f), Justification::centred);
            }

            for (int b = 0; b < bandButtons.size(); ++b)
                bandButtons[b]->setToggleState (b == selected, dontSendNotification);
            onBtn.setButtonText (eq.isBandOn (selected) ? "On" : "Off");
            onBtn.setToggleState (eq.isBandOn (selected), dontSendNotification);
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (! graph.contains (e.position)) return;
            FreqAxis axis { graph.reduced (4.0f) };
            int best = -1; float bestD = 14.0f;
            for (int b = 0; b < pulse::PulseEQ::numBands; ++b)
                if (auto d = nodePos (axis, b).getDistanceFrom (e.position); d < bestD) { bestD = d; best = b; }
            dragging = best;
            if (best >= 0)
            {
                selectBand (best);
                if (e.mods.isPopupMenu() || e.getNumberOfClicks() > 1)
                {
                    auto i = pulse::PulseEQ::idx (best, pulse::PulseEQ::pOn);
                    eq.set (i, eq.getInt (i) > 0 ? 0.0f : 1.0f);
                    dragging = -1;
                    return;
                }
                if (! eq.isBandOn (best))
                    eq.set (pulse::PulseEQ::idx (best, pulse::PulseEQ::pOn), 1.0f);
                eq.param (pulse::PulseEQ::idx (best, pulse::PulseEQ::pFreq))->parameterChangeGestureBegin();
            }
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0) return;
            FreqAxis axis { graph.reduced (4.0f) };
            using Q = pulse::PulseEQ;
            eq.set (Q::idx (dragging, Q::pFreq), (float) jlimit (20.0, 20000.0, axis.freqAt (e.position.x)));
            if (Q::usesGain (eq.getInt (Q::idx (dragging, Q::pType))))
            {
                const float db = (graph.getCentreY() - e.position.y) / (graph.getHeight() * 0.5f) * 20.0f;
                eq.set (Q::idx (dragging, Q::pGain), jlimit (-18.0f, 18.0f, db));
            }
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dragging >= 0)
                eq.param (pulse::PulseEQ::idx (dragging, pulse::PulseEQ::pFreq))->parameterChangeGestureEnd();
            dragging = -1;
        }

        void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
        {
            FreqAxis axis { graph.reduced (4.0f) };
            for (int b = 0; b < pulse::PulseEQ::numBands; ++b)
                if (nodePos (axis, b).getDistanceFrom (e.position) < 16.0f)
                {
                    auto i = pulse::PulseEQ::idx (b, pulse::PulseEQ::pQ);
                    eq.set (i, jlimit (0.1f, 18.0f, eq.get (i) * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f)));
                    selectBand (b);
                    return;
                }
        }

    private:
        static Colour bandColour (int b)
        {
            const uint32 c[] = { 0xffe0604a, 0xffe0a24a, 0xffd8d04f, 0xff5fcf7a, 0xff4fa8e0, 0xffa77be8 };
            return Colour (c[b % 6]);
        }

        Point<float> nodePos (const FreqAxis& axis, int b) const
        {
            using Q = pulse::PulseEQ;
            const float x = axis.xFor (eq.get (Q::idx (b, Q::pFreq)));
            const float db = Q::usesGain (eq.getInt (Q::idx (b, Q::pType))) ? eq.get (Q::idx (b, Q::pGain)) : 0.0f;
            return { x, graph.getCentreY() - db / 20.0f * graph.getHeight() * 0.5f };
        }

        void selectBand (int b)
        {
            selected = b;
            knobs.clear();
            using Q = pulse::PulseEQ;
            for (int which : { Q::pType, Q::pFreq, Q::pGain, Q::pQ })
                addAndMakeVisible (knobs.add (new ParamKnob (evs, *eq.param (Q::idx (b, which)))));
            for (auto* k : knobs)   // "Band 3 Freq" -> "Freq"
                k->name.setText (k->name.getText().fromLastOccurrenceOf (" ", false, false), dontSendNotification);
            resized();
            repaint();
        }

        void timerCallback() override
        {
            // analyser: newest 2048 samples, Hann window, smoothed magnitude
            std::vector<float> buf ((size_t) fftSize * 2, 0.0f);
            eq.analyser.read (buf.data(), fftSize);
            for (int i = 0; i < fftSize; ++i)
                buf[(size_t) i] *= 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (fftSize - 1));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int i = 0; i < fftSize / 2; ++i)
            {
                const float db = Decibels::gainToDecibels (buf[(size_t) i] * 4.0f / fftSize, -120.0f);
                spectrum[(size_t) i] = db > spectrum[(size_t) i] ? db : spectrum[(size_t) i] * 0.85f + db * 0.15f;
            }
            if (eq.getPluginType().isNotEmpty() && eq.edit.engine.getDeviceManager().getSampleRate() > 0)
                sampleRateGuess = eq.edit.engine.getDeviceManager().getSampleRate();
            repaint();
        }

        static constexpr int fftSize = 2048;
        EditViewState& evs;
        pulse::PulseEQ& eq;
        dsp::FFT fft;
        std::array<float, fftSize / 2> spectrum {};
        double sampleRateGuess = 48000.0;
        Rectangle<float> graph;
        OwnedArray<TextButton> bandButtons;
        TextButton onBtn { "On" };
        OwnedArray<ParamKnob> knobs;
        int selected = 2, dragging = -1;
    };

    //==============================================================================
    /** Waveform with start / end / loop-start handles; load by button, file drop or browser drag. */
    class SimplerDisplay : public Polled,
                           public FileDragAndDropTarget,
                           public DragAndDropTarget
    {
    public:
        explicit SimplerDisplay (pulse::Simpler& s) : Polled (s, 30), simpler (s)
        {
            loadBtn.onClick = [this] { browse(); };
            loadBtn.setTooltip ("Load a sample (or drop one here)");
            addAndMakeVisible (loadBtn);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2);
            loadBtn.setBounds (r.removeFromBottom (24).removeFromRight (70));
        }

        void paint (Graphics& g) override
        {
            using S = pulse::Simpler;
            auto all = getLocalBounds().toFloat().reduced (2.0f);
            auto footer = all.removeFromBottom (26.0f);
            wave = all;
            paintPanel (g, wave);
            auto data = simpler.getSample();

            g.setFont (FontOptions (12.0f));
            g.setColour (Theme::muted);
            g.drawText (data != nullptr ? data->file.getFileNameWithoutExtension() + "  (" + String (data->buffer.getNumSamples() / data->sampleRate, 2) + " s)"
                                        : String ("Drop a sample here"),
                        footer.withTrimmedRight (74.0f).reduced (4.0f, 0.0f), Justification::centredLeft, true);

            if (data == nullptr)
            {
                g.drawText ("No sample", wave, Justification::centred);
                return;
            }

            const float s = xFor (jmin (pp().get (S::pStart), pp().get (S::pEnd)));
            const float e = xFor (jmax (pp().get (S::pStart), pp().get (S::pEnd)));
            g.setColour (Colours::black.withAlpha (0.4f));
            g.fillRect (wave.withRight (s));
            g.fillRect (wave.withLeft (e));

            auto& ov = data->overview;
            g.setColour (Theme::accent.withAlpha (0.85f));
            const float mid = wave.getCentreY(), h = wave.getHeight() * 0.45f;
            for (int x = 0; x < (int) wave.getWidth(); ++x)
            {
                auto [lo, hi] = ov[(size_t) jmin ((int) ov.size() - 1, x * (int) ov.size() / jmax (1, (int) wave.getWidth()))];
                g.drawVerticalLine ((int) wave.getX() + x, mid - hi * h, mid - lo * h + 1.0f);
            }

            auto marker = [&g, this] (float x, Colour c, const String& label)
            {
                g.setColour (c);
                g.fillRect (x - 1.0f, wave.getY(), 2.0f, wave.getHeight());
                g.fillRect (x - 1.0f, wave.getY(), 16.0f, 12.0f);
                g.setColour (Colours::black);
                g.setFont (FontOptions (10.0f, Font::bold));
                g.drawText (label, Rectangle<float> (x + 1.0f, wave.getY(), 14.0f, 12.0f), Justification::centred);
            };
            marker (s, Theme::green, "S");
            marker (e - 15.0f, Theme::red, "E");
            if (pp().getInt (S::pLoop) > 0)
                marker (s + (e - s) * pp().get (S::pLoopStart), Theme::orange, "L");

            g.setColour (Colours::white);
            for (auto& ph : simpler.playheads)
                if (auto v = ph.load(); v >= 0.0f)
                    g.drawVerticalLine ((int) xFor (v), wave.getY(), wave.getBottom());
        }

        void mouseDown (const MouseEvent& e) override
        {
            using S = pulse::Simpler;
            if (simpler.getSample() == nullptr || ! wave.contains (e.position)) { dragging = -1; return; }
            const float s = xFor (pp().get (S::pStart)), en = xFor (pp().get (S::pEnd));
            const float l = s + (en - s) * pp().get (S::pLoopStart);
            const float ds = std::abs (e.position.x - s), de = std::abs (e.position.x - en), dl = std::abs (e.position.x - l);
            dragging = (pp().getInt (S::pLoop) > 0 && dl < ds && dl < de) ? S::pLoopStart : (ds <= de ? S::pStart : S::pEnd);
            pp().param (dragging)->parameterChangeGestureBegin();
            mouseDrag (e);
        }

        void mouseDrag (const MouseEvent& e) override
        {
            using S = pulse::Simpler;
            if (dragging < 0) return;
            const float v = jlimit (0.0f, 1.0f, (e.position.x - wave.getX()) / wave.getWidth());
            if (dragging == S::pLoopStart)
            {
                const float s = pp().get (S::pStart), en = pp().get (S::pEnd);
                pp().set (S::pLoopStart, en > s ? jlimit (0.0f, 1.0f, (v - s) / (en - s)) : 0.0f);
            }
            else
            {
                pp().set (dragging, v);
            }
        }

        void mouseUp (const MouseEvent&) override
        {
            if (dragging >= 0) pp().param (dragging)->parameterChangeGestureEnd();
            dragging = -1;
        }

        bool isInterestedInFileDrag (const StringArray& files) override { return ClipImport::isAudioFile (File (files[0])); }
        void filesDropped (const StringArray& files, int, int) override  { load (File (files[0])); }

        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == "PulseFiles"; }
        void itemDropped (const SourceDetails& d) override
        {
            auto files = ClipImport::filesFromDrag (d);
            if (! files.isEmpty() && ClipImport::isAudioFile (files[0]))
                load (files[0]);
        }

    private:
        bool alwaysRepaint() const override
        {
            if (simpler.sampleVersion.load() != seenVersion) return true;
            for (auto& ph : simpler.playheads) if (ph.load() >= 0.0f) return true;
            return false;
        }

        void timerCallback() override
        {
            Polled::timerCallback();
            seenVersion = simpler.sampleVersion.load();
        }

        float xFor (float v) const { return wave.getX() + v * wave.getWidth(); }

        void load (const File& f)
        {
            simpler.edit.getUndoManager().beginNewTransaction ("Load sample");
            simpler.setSampleFile (f);
        }

        void browse()
        {
            chooser = std::make_shared<FileChooser> ("Load sample", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [this, sp = SafePointer<SimplerDisplay> (this)] (const FileChooser& fc)
                                  {
                                      if (sp != nullptr && fc.getResult().existsAsFile()) load (fc.getResult());
                                  });
        }

        pulse::Simpler& simpler;
        TextButton loadBtn { "Load..." };
        Rectangle<float> wave;
        int dragging = -1, seenVersion = -1;
        std::shared_ptr<FileChooser> chooser;
    };

    //==============================================================================
    inline std::unique_ptr<Component> create (EditViewState& evs, te::Plugin& p)
    {
        if (auto s = dynamic_cast<pulse::WavetableSynth*> (&p)) return std::make_unique<WavetableDisplay> (*s);
        if (auto s = dynamic_cast<pulse::Simpler*> (&p))        return std::make_unique<SimplerDisplay> (*s);
        if (auto s = dynamic_cast<pulse::GlueCompressor*> (&p)) return std::make_unique<GRMeter<pulse::GlueCompressor>> (*s);
        if (auto s = dynamic_cast<pulse::Limiter*> (&p))        return std::make_unique<GRMeter<pulse::Limiter>> (*s);
        if (auto s = dynamic_cast<pulse::Saturator*> (&p))      return std::make_unique<SaturatorCurve> (*s);
        if (auto s = dynamic_cast<pulse::PulseEQ*> (&p))        return std::make_unique<EQDisplay> (evs, *s);
        if (auto s = dynamic_cast<pulse::AutoFilter*> (&p))     return std::make_unique<AutoFilterDisplay> (*s);
        return {};
    }
}
