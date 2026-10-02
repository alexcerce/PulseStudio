#pragma once

#include "Graphs.h"
#include "MidiFx.h"
#include "PulseFx2.h"
#include "UtilityDevices.h"
#include "DragDrop.h"

//==============================================================================
/** Larger live views for devices that had none: what a MIDI effect does to a phrase, the
    vocoder's bands and a stereo scope for Utility. */
namespace DeviceViews
{
    //==============================================================================
    /** Plays a short test phrase (a held C major chord, then four single notes) through a copy of
        the MIDI effect and draws input (outlines) against output (filled notes). Updates when a
        setting changes. */
    class MidiFxPreview : public Component, private Timer
    {
    public:
        struct Note { double start, end; int pitch; int vel; };

        explicit MidiFxPreview (pulse::PulseMidiEffect& fx) : holder (&fx), plugin (&fx) { startTimerHz (5); render(); }

        /** The phrase that goes in (beats). */
        static std::vector<Note> inputPhrase()
        {
            return { { 0, 4, 48, 100 }, { 0, 4, 52, 100 }, { 0, 4, 55, 100 },
                     { 4, 5, 50, 100 }, { 5, 6, 54, 90 }, { 6, 7, 57, 100 }, { 7, 8, 61, 90 } };
        }

        /** Runs the phrase through a copy of the effect (message thread). */
        static std::vector<Note> run (pulse::PulseMidiEffect& fx, double bpm)
        {
            std::vector<Note> out;
            auto copy = DragDrop::duplicate (fx);
            auto* p = dynamic_cast<pulse::PulseMidiEffect*> (copy.get());
            if (p == nullptr) return out;
            const double sr = 48000.0;
            const int block = 480;
            const double spb = sr * 60.0 / jmax (20.0, bpm);
            p->initialise ({ te::TimePosition(), sr, block });
            auto in = inputPhrase();
            std::map<int, std::pair<double, int>> open;   // pitch -> (start beat, velocity)
            AudioBuffer<float> audio (2, block);
            const auto src = te::createUniqueMPESourceID();
            const int blocks = (int) std::ceil (spb * 9.0 / block);
            for (int k = 0; k < blocks; ++k)
            {
                const double b0 = k * block / spb, b1 = (k + 1) * block / spb;
                te::MidiMessageArray midi;
                for (auto& n : in)
                {
                    if (n.start >= b0 && n.start < b1) midi.addMidiMessage (MidiMessage::noteOn (1, n.pitch, (uint8) n.vel), (n.start - b0) * spb / sr, src);
                    if (n.end >= b0 && n.end < b1)     midi.addMidiMessage (MidiMessage::noteOff (1, n.pitch), (n.end - b0) * spb / sr, src);
                }
                audio.clear();
                te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0,
                                            { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                            true, false, true, false);
                p->applyToBuffer (fc);
                for (auto& m : midi)
                {
                    const double beat = b0 + m.getTimeStamp() * sr / spb;
                    if (m.isNoteOn())
                    {
                        if (auto it = open.find (m.getNoteNumber()); it != open.end()) out.push_back ({ it->second.first, beat, it->first, it->second.second });
                        open[m.getNoteNumber()] = { beat, m.getVelocity() };
                    }
                    else if (m.isNoteOff())
                        if (auto it = open.find (m.getNoteNumber()); it != open.end())
                        {
                            out.push_back ({ it->second.first, jmax (beat, it->second.first + 0.02), it->first, it->second.second });
                            open.erase (it);
                        }
                }
            }
            for (auto& [pitch, sv] : open) out.push_back ({ sv.first, 9.0, pitch, sv.second });
            p->deinitialise();
            return out;
        }

        const std::vector<Note>& getOutput() const { return output; }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            const bool large = Graphs::isLarge (*this);
            auto area = b.reduced (10.0f, 8.0f).withTrimmedTop (16.0f).withTrimmedLeft (large ? 28.0f : 0.0f).withTrimmedBottom (large ? 14.0f : 0.0f);
            const auto in = inputPhrase();
            int lo = 127, hi = 0;
            for (auto& n : in) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
            for (auto& n : output) { lo = jmin (lo, n.pitch); hi = jmax (hi, n.pitch); }
            lo -= 2; hi += 2;
            const float rowH = area.getHeight() / (float) (hi - lo + 1);
            auto xFor = [&] (double beat) { return area.getX() + (float) (beat / 9.0) * area.getWidth(); };
            auto yFor = [&] (int p) { return area.getBottom() - (float) (p - lo + 1) * rowH; };

            for (int p = lo; p <= hi; ++p)
                if (MidiMessage::isMidiNoteBlack (p))
                {
                    g.setColour (Colours::black.withAlpha (0.18f));
                    g.fillRect (Rectangle<float> (area.getX(), yFor (p), area.getWidth(), rowH));
                }
            for (int beat = 0; beat <= 9; ++beat)
            {
                g.setColour (Colours::white.withAlpha (beat % 4 == 0 ? 0.12f : 0.04f));
                g.drawVerticalLine ((int) xFor (beat), area.getY(), area.getBottom());
                if (large && beat % 4 == 0 && beat < 9)
                    Graphs::label (g, "Bar " + String (beat / 4 + 1), { xFor (beat) + 3.0f, area.getBottom() + 2.0f, 60.0f, 11.0f }, Justification::centredLeft);
            }
            if (large)
                for (int p = lo; p <= hi; ++p)
                    if (p % 12 == 0 || rowH >= 11.0f)
                        Graphs::label (g, MidiMessage::getMidiNoteName (p, true, true, 3), { b.getX() + 2.0f, yFor (p), 28.0f, rowH }, Justification::centredLeft, 0.5f);

            for (auto& n : in)
            {
                g.setColour (Colours::white.withAlpha (0.35f));
                g.drawRoundedRectangle (Rectangle<float> (xFor (n.start), yFor (n.pitch), xFor (n.end) - xFor (n.start), rowH).reduced (0.5f, 1.0f), 2.0f, 1.0f);
            }
            for (auto& n : output)
            {
                auto r = Rectangle<float> (xFor (n.start), yFor (n.pitch), jmax (2.0f, xFor (n.end) - xFor (n.start)), rowH).reduced (0.5f, 1.0f);
                g.setColour (Theme::accent.withAlpha (0.35f + 0.65f * n.vel / 127.0f));
                g.fillRoundedRectangle (r, 2.0f);
                g.setColour (Theme::accent.brighter (0.4f));
                g.drawRoundedRectangle (r, 2.0f, 0.8f);
            }
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.5f, Font::bold));
            g.drawText ("IN: C major chord, then D  F#  A  C#  (outlines)      OUT: filled  -  " + String ((int) output.size()) + " notes",
                        b.reduced (8.0f, 3.0f).removeFromTop (14.0f), Justification::centredLeft, true);
        }

    private:
        void timerCallback() override
        {
            double sig = 0;
            for (int i = 0; i < plugin->numParams(); ++i) sig = sig * 1.37 + plugin->get (i);
            sig += plugin->edit.tempoSequence.getTempo (0)->getBpm() * 0.001;
            if (sig != lastSig) render();
        }
        void render()
        {
            double sig = 0;
            for (int i = 0; i < plugin->numParams(); ++i) sig = sig * 1.37 + plugin->get (i);
            const double bpm = plugin->edit.tempoSequence.getTempo (0)->getBpm();
            lastSig = sig + bpm * 0.001;
            output = run (*plugin, bpm);
            repaint();
        }

        te::Plugin::Ptr holder;
        pulse::PulseMidiEffect* plugin;
        std::vector<Note> output;
        double lastSig = -1;
    };

    //==============================================================================
    /** The vocoder's 16 bands (the voice's spectrum, live), with peak caps. */
    class VocoderBands : public Component, private Timer
    {
    public:
        explicit VocoderBands (pulse::Vocoder& v) : holder (&v), voc (&v) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            const bool large = Graphs::isLarge (*this);
            auto area = b.reduced (10.0f, 8.0f).withTrimmedTop (14.0f).withTrimmedBottom (large ? 14.0f : 0.0f);
            const int n = pulse::Vocoder::bands;
            const float w = area.getWidth() / n;
            for (int i = 0; i < n; ++i)
            {
                auto cell = Rectangle<float> (area.getX() + w * i, area.getY(), w, area.getHeight()).reduced (jmin (3.0f, w * 0.15f), 0.0f);
                const float db = Decibels::gainToDecibels (levels[(size_t) i], -60.0f);
                const float h = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                const auto col = Colour::fromHSV (0.58f - 0.5f * i / n, 0.6f, 0.95f, 1.0f);
                g.setColour (Colours::white.withAlpha (0.03f));
                g.fillRect (cell);
                auto bar = cell.withTop (cell.getBottom() - h * cell.getHeight());
                g.setGradientFill (ColourGradient (col, 0.0f, bar.getY(), col.withAlpha (0.25f), 0.0f, cell.getBottom(), false));
                g.fillRect (bar);
                const float ph = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (peaks[(size_t) i], -60.0f) + 60.0f) / 60.0f);
                g.setColour (Colours::white.withAlpha (0.8f));
                g.fillRect (cell.getX(), cell.getBottom() - ph * cell.getHeight() - 1.0f, cell.getWidth(), 2.0f);
                if (large && (i % 3 == 0 || i == n - 1))
                {
                    const float f = pulse::Vocoder::bandFreq (i);
                    Graphs::label (g, f >= 1000.0f ? String (f / 1000.0f, 1) + "k" : String (roundToInt (f)),
                                   { cell.getX() - 6.0f, area.getBottom() + 2.0f, cell.getWidth() + 12.0f, 11.0f }, Justification::centred);
                }
            }
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.5f, Font::bold));
            g.drawText ("Band levels of the voice (modulator)", b.reduced (8.0f, 3.0f).removeFromTop (12.0f), Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            bool moved = false;
            for (int i = 0; i < pulse::Vocoder::bands; ++i)
            {
                const float v = voc->bandLevel[(size_t) i].load() * 4.0f;
                auto& l = levels[(size_t) i];
                const float nl = jmax (v, l * 0.85f);
                if (std::abs (nl - l) > 1.0e-5f) moved = true;
                l = nl;
                auto& pk = peaks[(size_t) i];
                pk = jmax (l, pk * 0.97f);
            }
            if (moved) repaint();
        }
        te::Plugin::Ptr holder;
        pulse::Vocoder* voc;
        std::array<float, pulse::Vocoder::bands> levels {}, peaks {};
    };

    //==============================================================================
    /** Utility's stereo picture: a scope of the output (mid up, side across), a correlation meter,
        and a width / balance wedge with a draggable node (left / right = balance, up / down = width). */
    class StereoField : public Graphs::GraphBase
    {
    public:
        explicit StereoField (pulse::Utility& u) : holder (&u), util (&u), width (u.param (pulse::Utility::pWidth)), balance (u.param (pulse::Utility::pBalance))
        {
            setTooltip ("Drag the point: up / down = width, left / right = balance (double-click resets)");
        }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.0f);
            Graphs::panel (g, b);
            auto corrBar = b.reduced (10.0f).removeFromBottom (10.0f);
            area = b.reduced (10.0f).withTrimmedBottom (18.0f).withTrimmedTop (12.0f);
            const float r = jmin (area.getWidth() * 0.5f, area.getHeight());
            centre = { area.getCentreX(), area.getBottom() };
            radius = r;

            // half-circle guides
            g.setColour (Colours::white.withAlpha (0.06f));
            for (float k : { 0.33f, 0.66f, 1.0f })
            {
                Path arc;
                arc.addCentredArc (centre.x, centre.y, r * k, r * k, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
                g.strokePath (arc, PathStrokeType (1.0f));
            }
            g.drawLine (centre.x, centre.y, centre.x, centre.y - r, 1.0f);
            g.drawLine (centre.x, centre.y, centre.x - r * 0.707f, centre.y - r * 0.707f, 0.8f);
            g.drawLine (centre.x, centre.y, centre.x + r * 0.707f, centre.y - r * 0.707f, 0.8f);
            Graphs::label (g, "L", { centre.x - r * 0.75f - 12.0f, centre.y - r * 0.75f - 12.0f, 12.0f, 12.0f }, Justification::centred);
            Graphs::label (g, "R", { centre.x + r * 0.75f, centre.y - r * 0.75f - 12.0f, 12.0f, 12.0f }, Justification::centred);
            Graphs::label (g, "M", { centre.x - 6.0f, centre.y - r - 12.0f, 12.0f, 12.0f }, Justification::centred);

            // width wedge
            const float wNorm = jlimit (0.0f, 1.0f, width->getCurrentBaseValue() / 4.0f);
            const float spread = jlimit (0.02f, MathConstants<float>::halfPi, width->getCurrentBaseValue() * MathConstants<float>::pi * 0.25f);
            const float tilt = balance->getCurrentBaseValue() * MathConstants<float>::pi * 0.25f;
            Path wedge;
            wedge.addPieSegment (centre.x - r, centre.y - r, r * 2.0f, r * 2.0f, tilt - spread, tilt + spread, 0.0f);
            g.setColour (Theme::accent.withAlpha (0.12f));
            g.fillPath (wedge);
            g.setColour (Theme::accent.withAlpha (0.5f));
            g.strokePath (wedge, PathStrokeType (1.0f));
            juce::ignoreUnused (wNorm);

            // scope dots
            const int n = pulse::Utility::scopeSize;
            const int w = util->scopeWrite.load();
            float corrNum = 0, corrL = 0, corrR = 0;
            for (int i = 0; i < n; ++i)
            {
                const int idx = (w + i) % n;
                const float L = util->scope[(size_t) (idx * 2)].load(), R = util->scope[(size_t) (idx * 2 + 1)].load();
                corrNum += L * R; corrL += L * L; corrR += R * R;
                const float side = (L - R) * 0.707f, mid = (L + R) * 0.707f;
                const float x = centre.x + jlimit (-1.0f, 1.0f, side) * r, y = centre.y - jlimit (0.0f, 1.0f, std::abs (mid)) * r;
                g.setColour (Theme::orange.withAlpha (0.08f + 0.5f * i / n));
                g.fillRect (x - 0.75f, y - 0.75f, 1.5f, 1.5f);
            }
            const float corr = corrL > 1.0e-9f && corrR > 1.0e-9f ? corrNum / std::sqrt (corrL * corrR) : 1.0f;

            // correlation meter (-1 .. +1)
            g.setColour (Colour (0xff1d2026));
            g.fillRoundedRectangle (corrBar, 3.0f);
            const float cx = corrBar.getX() + (corr + 1.0f) * 0.5f * corrBar.getWidth();
            g.setColour (corr < 0.0f ? Theme::red : Colour (0xff69db7c));
            g.fillRoundedRectangle (Rectangle<float> (jmin (cx, corrBar.getCentreX()), corrBar.getY(), std::abs (cx - corrBar.getCentreX()) + 2.0f, corrBar.getHeight()), 3.0f);
            Graphs::label (g, "-1", corrBar.withWidth (20.0f).translated (-22.0f, 0.0f).expanded (0.0f, 2.0f), Justification::centredRight, 0.5f);
            Graphs::label (g, "+1  correlation " + String (corr, 2), corrBar.withX (corrBar.getRight() - 110.0f).withWidth (108.0f).translated (0.0f, -12.0f),
                           Justification::centredRight, 0.6f);

            Graphs::node (g, nodePos(), Theme::accent, hot >= 0 || dragging >= 0);
            drawReadout (g);
        }

        void mouseMove (const MouseEvent& e) override { const int h = nodePos().getDistanceFrom (e.position) < 10.0f ? 0 : -1; if (h != hot) { hot = h; updateReadout(); repaint(); } }
        void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }
        void mouseDown (const MouseEvent& e) override { dragging = 0; Graphs::begin ({ width, balance }); mouseDrag (e); }
        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging < 0 || radius <= 0) return;
            Graphs::setValue (balance, jlimit (-1.0f, 1.0f, (e.position.x - centre.x) / radius));
            Graphs::setValue (width, jlimit (0.0f, 4.0f, (centre.y - e.position.y) / radius * 4.0f));
            updateReadout();
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (dragging >= 0) Graphs::end ({ width, balance }); dragging = -1; }
        void mouseDoubleClick (const MouseEvent&) override
        {
            width->getEdit().getUndoManager().beginNewTransaction ("Reset");
            Graphs::reset (width); Graphs::reset (balance);
        }

    private:
        juce::Point<float> nodePos() const
        {
            return { centre.x + balance->getCurrentBaseValue() * radius, centre.y - width->getCurrentBaseValue() / 4.0f * radius };
        }
        void updateReadout()
        {
            readoutPos = nodePos();
            readout = "Width " + width->getCurrentValueAsStringWithLabel() + "  Balance " + balance->getCurrentValueAsStringWithLabel();
        }
        double signature() const override
        {
            return width->getCurrentBaseValue() + balance->getCurrentBaseValue() * 3.1 + util->scopeWrite.load() * 1.0e-3;
        }

        te::Plugin::Ptr holder;
        pulse::Utility* util;
        Graphs::Param width, balance;
        Rectangle<float> area;
        juce::Point<float> centre;
        float radius = 0;
    };
}
