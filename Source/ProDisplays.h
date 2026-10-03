#pragma once

#include "SpaceDisplay.h"
#include "WavetableModUI.h"

/** Big displays for the device windows: spectrum analyser, tuner, pitch correction, the glue
    compressor's needle and the auto filter's LFO. They sit on the window's dark display area. */
namespace ProUI
{
    using SpaceUI::xForFreq;
    using SpaceUI::freqForX;

    inline void well (Graphics& g, Rectangle<float> r)
    {
        g.setColour (Pro::well());
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (Theme::ink.withAlpha (0.06f));
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    inline void readoutBox (Graphics& g, Rectangle<float> box, const String& text, Colour c)
    {
        g.setColour (Colour (0xee0b0d11));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (c.withAlpha (0.8f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (Colours::white);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (text, box, Justification::centred);
    }

    inline String hzText (double f) { return f >= 1000.0 ? String (f / 1000.0, 2) + " kHz" : String (roundToInt (f)) + " Hz"; }

    //==============================================================================
    /** Spectrum: a smooth log-frequency curve (bins are blended per pixel so the low end has no
        steps), peak hold, dB and frequency axes, the loudest peak, and a crosshair readout. */
    class SpectrumPro : public Component, private Timer
    {
    public:
        using S = pulse::Spectrum;
        explicit SpectrumPro (S& s) : spec (s), holder (&s) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            well (g, b);
            area = b.reduced (10.0f).withTrimmedLeft (30.0f).withTrimmedBottom (16.0f).withTrimmedTop (6.0f);
            update();
            const float floorDb = spec.get (S::pFloor);
            auto yFor = [&] (float db) { return area.getBottom() - area.getHeight() * jlimit (0.0f, 1.0f, (db - floorDb) / (0.0f - floorDb)); };
            SpaceUI::freqGrid (g, area, true);
            const float step = floorDb < -70.0f ? 12.0f : 6.0f;
            for (float db = 0.0f; db > floorDb; db -= step)
            {
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.1f : 0.045f));
                g.drawHorizontalLine ((int) yFor (db), area.getX(), area.getRight());
                Graphs::label (g, String ((int) db), { area.getX() - 32.0f, yFor (db) - 6.0f, 28.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            const int bins = (int) smooth.size();
            if (bins < 4) return;
            const double sr = spec.getSampleRate() > 0 ? spec.getSampleRate() : 48000.0;
            const double binHz = sr * 0.5 / bins;
            // one value per 2 px: the loudest bin in the pixel's band, or a blend of two bins when the band is narrower than a bin
            auto valueAt = [&] (const std::vector<float>& m, float x)
            {
                const double f0 = freqForX (area, x - 1.0f), f1 = freqForX (area, x + 1.0f);
                const double p0 = f0 / binHz, p1 = f1 / binHz;
                if (p1 - p0 < 1.0)
                {
                    const double p = jlimit (1.0, bins - 2.0, 0.5 * (p0 + p1));
                    const int i = (int) p;
                    return m[(size_t) i] + (m[(size_t) i + 1] - m[(size_t) i]) * (float) (p - i);
                }
                float best = -200.0f;
                for (int i = jmax (1, (int) p0); i <= jmin (bins - 1, (int) p1); ++i) best = jmax (best, m[(size_t) i]);
                return best;
            };
            auto pathFor = [&] (const std::vector<float>& m, bool closed)
            {
                Path p;
                std::vector<float> ys;
                for (float x = area.getX(); x <= area.getRight(); x += 2.0f) ys.push_back (yFor (valueAt (m, x)));
                for (size_t i = 1; i + 1 < ys.size(); ++i) ys[i] = 0.25f * ys[i - 1] + 0.5f * ys[i] + 0.25f * ys[i + 1];   // soften
                for (size_t i = 0; i < ys.size(); ++i)
                {
                    const float x = area.getX() + 2.0f * (float) i;
                    if (i == 0) { p.startNewSubPath (x, closed ? area.getBottom() : ys[i]); if (closed) p.lineTo (x, ys[i]); }
                    else p.lineTo (x, ys[i]);
                }
                if (closed) { p.lineTo (area.getRight(), area.getBottom()); p.closeSubPath(); }
                return p;
            };
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.5f), 0.0f, area.getY(), Theme::accent.withAlpha (0.03f), 0.0f, area.getBottom(), false));
            g.fillPath (pathFor (smooth, true));
            Graphs::glowStroke (g, pathFor (smooth, false), Theme::accent, 1.4f);
            if (spec.getInt (S::pHold) > 0)
            {
                g.setColour (Theme::orange.withAlpha (0.75f));
                g.strokePath (pathFor (peaks, false), PathStrokeType (1.0f));
            }
            // the loudest peak
            int top = 1;
            for (int i = 2; i < bins; ++i) if (smooth[(size_t) i] > smooth[(size_t) top]) top = i;
            if (smooth[(size_t) top] > floorDb + 6.0f)
            {
                const double f = top * binHz;
                const float x = xForFreq (area, (float) f), y = yFor (smooth[(size_t) top]);
                g.setColour (Colours::white);
                g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
                const String t = "Peak  " + hzText (f) + "  " + MidiMessage::getMidiNoteName (roundToInt (pulse::Tuner::noteOf (f)), true, true, 3) + "  " + String (smooth[(size_t) top], 1) + " dB";
                Graphs::label (g, t, area.withHeight (14.0f), Justification::centredRight, 0.9f);
            }
            if (hover.x >= area.getX() && area.contains (hover))
            {
                const double f = freqForX (area, hover.x);
                const float db = floorDb + (area.getBottom() - hover.y) / area.getHeight() * -floorDb;
                g.setColour (Theme::ink.withAlpha (0.25f));
                g.drawVerticalLine ((int) hover.x, area.getY(), area.getBottom());
                g.drawHorizontalLine ((int) hover.y, area.getX(), area.getRight());
                const String t = hzText (f) + "  " + MidiMessage::getMidiNoteName (roundToInt (pulse::Tuner::noteOf (f)), true, true, 3) + "  " + String (db, 1) + " dB";
                const float w = GlyphArrangement::getStringWidth (FontOptions (11.0f, Font::bold), t) + 16.0f;
                readoutBox (g, Rectangle<float> (hover.x + 10.0f, hover.y - 26.0f, w, 20.0f).constrainedWithin (area), t, Theme::accent);
            }
        }

        void mouseMove (const MouseEvent& e) override { hover = e.position; }
        void mouseExit (const MouseEvent&) override   { hover = { -1.0f, -1.0f }; }
        void mouseDoubleClick (const MouseEvent&) override { std::fill (peaks.begin(), peaks.end(), -200.0f); }

    private:
        void update()
        {
            const int order = spec.fftOrder(), size = 1 << order;
            if (fft == nullptr || fft->getSize() != size)
            {
                fft = std::make_unique<dsp::FFT> (order);
                smooth.assign ((size_t) size / 2, -200.0f);
                peaks.assign ((size_t) size / 2, -200.0f);
            }
            std::vector<float> buf ((size_t) size * 2, 0.0f);
            spec.fifo.read (buf.data(), size);
            for (int i = 0; i < size; ++i)
                buf[(size_t) i] *= 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * i / (size - 1));
            fft->performFrequencyOnlyForwardTransform (buf.data());
            const float avg = spec.get (S::pAverage);
            for (int i = 0; i < size / 2; ++i)
            {
                const float db = Decibels::gainToDecibels (buf[(size_t) i] * 4.0f / (float) size, -200.0f);
                smooth[(size_t) i] = db > smooth[(size_t) i] ? db : smooth[(size_t) i] * avg + db * (1.0f - avg);
                peaks[(size_t) i] = jmax (peaks[(size_t) i] - 0.15f, smooth[(size_t) i]);
            }
        }
        void timerCallback() override { repaint(); }

        S& spec;
        te::Plugin::Ptr holder;
        std::unique_ptr<dsp::FFT> fft;
        std::vector<float> smooth, peaks;
        Rectangle<float> area;
        juce::Point<float> hover { -1.0f, -1.0f };
    };

    //==============================================================================
    /** Tuner: a wide arc from -50 to +50 cents with a needle, the note in the middle (green when in
        tune), the neighbouring notes either side, and the frequency. */
    class TunerPro : public Component, private Timer
    {
    public:
        using T = pulse::Tuner;
        explicit TunerPro (T& t) : tuner (t), holder (&t) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            well (g, b);
            const double sr = tuner.getSampleRate() > 0 ? tuner.getSampleRate() : 48000.0;
            std::vector<float> buf (4096);
            tuner.fifo.read (buf.data(), (int) buf.size());
            const double hz = T::detectPitch (buf.data(), (int) buf.size(), sr);
            if (hz > 0) { shownHz = shownHz > 0 ? shownHz * 0.7 + hz * 0.3 : hz; silent = 0; }
            else if (++silent > 15) shownHz = 0;

            const bool active = shownHz > 0;
            const double note = active ? T::noteOf (shownHz, tuner.get (T::pReference)) : 69.0;
            const int nearest = roundToInt (note);
            const float cents = active ? (float) ((note - nearest) * 100.0) : 0.0f;
            needle += (cents - needle) * 0.35f;
            const bool inTune = active && std::abs (cents) < 3.0f;
            const Colour col = ! active ? Theme::muted : inTune ? Theme::green : std::abs (cents) < 15.0f ? Theme::yellow : Theme::orange;

            // the arc
            auto a = b.reduced (30.0f, 24.0f);
            const float radius = jmin (a.getWidth() * 0.5f, a.getHeight() * 0.95f);
            const juce::Point<float> c (a.getCentreX(), a.getY() + radius + 6.0f);
            const float span = 1.05f;   // radians either side
            auto pt = [&] (float ct, float r) { const float ang = ct / 50.0f * span; return juce::Point<float> (c.x + std::sin (ang) * r, c.y - std::cos (ang) * r); };
            Path arc;
            arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, -span, span, true);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.strokePath (arc, PathStrokeType (10.0f, PathStrokeType::curved, PathStrokeType::rounded));
            Path zone;
            zone.addCentredArc (c.x, c.y, radius, radius, 0.0f, -span * 3.0f / 50.0f, span * 3.0f / 50.0f, true);
            g.setColour (Theme::green.withAlpha (inTune ? 0.9f : 0.35f));
            g.strokePath (zone, PathStrokeType (10.0f));
            for (int ct = -50; ct <= 50; ct += 5)
            {
                const bool major = ct % 10 == 0;
                g.setColour (Theme::ink.withAlpha (major ? 0.45f : 0.18f));
                g.drawLine (Line<float> (pt ((float) ct, radius - (major ? 22.0f : 14.0f)), pt ((float) ct, radius - 8.0f)), major ? 1.5f : 1.0f);
                if (ct % 25 == 0)
                {
                    const auto p = pt ((float) ct, radius - 36.0f);
                    Graphs::label (g, (ct > 0 ? "+" : "") + String (ct), { p.x - 16.0f, p.y - 6.0f, 32.0f, 12.0f }, Justification::centred, 0.7f);
                }
            }
            if (active)
            {
                const auto tip = pt (jlimit (-50.0f, 50.0f, needle), radius - 4.0f), base = pt (jlimit (-50.0f, 50.0f, needle), radius * 0.45f);
                g.setColour (col.withAlpha (0.25f));
                g.drawLine (Line<float> (base, tip), 7.0f);
                g.setColour (col);
                g.drawLine (Line<float> (base, tip), 2.5f);
                g.fillEllipse (tip.x - 5.0f, tip.y - 5.0f, 10.0f, 10.0f);
            }
            // the note
            auto text = Rectangle<float> (c.x - 220.0f, c.y - radius * 0.5f, 440.0f, radius * 0.5f);
            g.setColour (col);
            g.setFont (FontOptions (jlimit (40.0f, 110.0f, radius * 0.32f), Font::bold));
            g.drawText (active ? MidiMessage::getMidiNoteName (nearest, true, true, 3) : String ("--"), text, Justification::centred);
            if (active)
            {
                g.setColour (Theme::muted.withAlpha (0.5f));
                g.setFont (FontOptions (jlimit (18.0f, 36.0f, radius * 0.1f), Font::bold));
                g.drawText (MidiMessage::getMidiNoteName (nearest - 1, true, true, 3), text.translated (-radius * 0.55f, 0.0f), Justification::centred);
                g.drawText (MidiMessage::getMidiNoteName (nearest + 1, true, true, 3), text.translated (radius * 0.55f, 0.0f), Justification::centred);
            }
            g.setColour (Theme::text.withAlpha (0.85f));
            g.setFont (FontOptions (14.0f, Font::bold));
            g.drawText (active ? String (shownHz, 1) + " Hz     " + (cents >= 0 ? "+" : "") + String (cents, 1) + " cents" + (inTune ? "     in tune" : cents < 0 ? "     flat" : "     sharp")
                               : String ("Play a single note"),
                        Rectangle<float> (b.getX(), jmin (b.getBottom() - 34.0f, c.y + 8.0f), b.getWidth(), 22.0f), Justification::centred);
        }

    private:
        void timerCallback() override { repaint(); }
        T& tuner;
        te::Plugin::Ptr holder;
        double shownHz = 0;
        int silent = 0;
        float needle = 0;
    };

    //==============================================================================
    /** Pitch correction: what comes in (orange) and where it is pulled to (blue) over the last few
        seconds, on rows of notes with the notes of the key lit; the current note and its error on the right. */
    class PitchPro : public Component, private Timer
    {
    public:
        using P = pulse::PitchCorrect;
        explicit PitchPro (P& p) : pc (p), holder (&p) { startTimerHz (30); }

        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            well (g, b);
            auto r = b.reduced (10.0f);
            auto side = r.removeFromRight (jmin (220.0f, r.getWidth() * 0.26f));
            r.removeFromRight (10.0f);
            auto a = r.withTrimmedLeft (34.0f);
            const int root = pc.getInt (P::pRoot), scale = pc.getInt (P::pScale);
            auto inKey = [&] (int n) { return scale == 0 || Scales::contains (scale - 1, root, n); };
            // rows: 15 semitones around the centre
            const int rows = 15;
            const float rh = a.getHeight() / rows;
            const int lowest = roundToInt (centre) - rows / 2;
            auto yFor = [&] (float note) { return a.getBottom() - (note - (float) lowest + 0.5f) * rh; };
            for (int i = 0; i < rows; ++i)
            {
                const int n = lowest + i;
                auto row = Rectangle<float> (a.getX(), a.getBottom() - (i + 1) * rh, a.getWidth(), rh);
                const bool black = MidiMessage::isMidiNoteBlack (n);
                g.setColour (inKey (n) ? Theme::accent.withAlpha (black ? 0.07f : 0.11f) : Theme::ink.withAlpha (0.015f));
                g.fillRect (row.reduced (0.0f, 0.5f));
                g.setColour (inKey (n) ? Theme::text.withAlpha (0.8f) : Theme::muted.withAlpha (0.4f));
                g.setFont (FontOptions (10.0f, inKey (n) ? Font::bold : Font::plain));
                g.drawText (MidiMessage::getMidiNoteName (n, true, true, 3), Rectangle<float> (r.getX(), row.getY(), 30.0f, rh), Justification::centredRight);
            }
            auto line = [&] (const std::vector<float>& h, Colour c, float width)
            {
                Path p;
                bool pen = false;
                for (int i = 0; i < size; ++i)
                {
                    const float v = h[(size_t) ((head + i) % size)];
                    if (v < 0) { pen = false; continue; }
                    const float x = a.getX() + a.getWidth() * i / (float) (size - 1), y = jlimit (a.getY(), a.getBottom(), yFor (v));
                    if (! pen) { p.startNewSubPath (x, y); pen = true; } else p.lineTo (x, y);
                }
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (a.toNearestInt());
                Graphs::glowStroke (g, p, c, width);
            };
            line (target, Theme::accent, 2.2f);
            line (detected, Theme::orange, 1.4f);
            Graphs::label (g, "IN (orange)    CORRECTED TO (blue)", a.withHeight (12.0f).translated (4.0f, 2.0f), Justification::centredLeft, 0.7f);

            // the readout
            well (g, side);
            const float det = pc.detectedNote.load(), tgt = pc.targetNote.load();
            const bool active = det >= 0;
            auto s = side.reduced (12.0f);
            Graphs::label (g, "CORRECTING TO", s.removeFromTop (14.0f), Justification::centred, 0.7f);
            g.setColour (active ? Theme::accent : Theme::muted);
            g.setFont (FontOptions (jlimit (30.0f, 64.0f, side.getWidth() * 0.3f), Font::bold));
            g.drawText (active ? MidiMessage::getMidiNoteName (roundToInt (tgt), true, true, 3) : String ("--"), s.removeFromTop (s.getHeight() * 0.42f), Justification::centred);
            const float cents = active ? jlimit (-100.0f, 100.0f, (det - tgt) * 100.0f) : 0.0f;
            g.setColour (Theme::text.withAlpha (0.85f));
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText (active ? String ("in: ") + (cents >= 0 ? "+" : "") + String (roundToInt (cents)) + " cents" : String ("Sing or play a single line"), s.removeFromTop (20.0f), Justification::centred);
            s.removeFromTop (10.0f);
            auto bar = s.removeFromTop (10.0f).reduced (4.0f, 0.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Theme::ink.withAlpha (0.4f));
            g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
            if (active)
            {
                const float x = bar.getCentreX() + cents / 100.0f * bar.getWidth() * 0.5f;
                g.setColour (std::abs (cents) < 8.0f ? Theme::green : Theme::orange);
                g.fillRoundedRectangle (Rectangle<float> (jmin (x, bar.getCentreX()), bar.getY(), std::abs (x - bar.getCentreX()), bar.getHeight()), 3.0f);
            }
            s.removeFromTop (16.0f);
            Graphs::label (g, "KEY", s.removeFromTop (12.0f), Justification::centred, 0.7f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (15.0f, Font::bold));
            g.drawText (scale == 0 ? String ("Chromatic") : pc.param (P::pRoot)->getCurrentValueAsString() + " " + pc.param (P::pScale)->getCurrentValueAsString(),
                        s.removeFromTop (22.0f), Justification::centred);
        }

    private:
        void timerCallback() override
        {
            const float det = pc.detectedNote.load(), tgt = pc.targetNote.load();
            detected[(size_t) head] = det;
            target[(size_t) head] = det >= 0 ? tgt : -1.0f;
            head = (head + 1) % size;
            if (det >= 0) centre += (tgt - centre) * (std::abs (tgt - centre) > 6.0f ? 0.2f : 0.02f);   // follow the singer slowly
            repaint();
        }
        static constexpr int size = 240;   // 8 seconds
        P& pc;
        te::Plugin::Ptr holder;
        std::vector<float> detected = std::vector<float> (size, -1.0f), target = std::vector<float> (size, -1.0f);
        int head = 0;
        float centre = 60.0f;
    };

    //==============================================================================
    /** The glue compressor's gain reduction on a needle, like a bus compressor's meter. */
    class GlueNeedle : public Component, private Timer
    {
    public:
        explicit GlueNeedle (pulse::GlueCompressor& c) : comp (c), holder (&c) { startTimerHz (30); }
        void paint (Graphics& g) override
        {
            auto b = getLocalBounds().toFloat();
            well (g, b);
            auto face = b.reduced (16.0f);
            const float radius = jmin (face.getWidth() * 0.62f, face.getHeight() * 0.8f);
            const juce::Point<float> c (face.getCentreX(), face.getCentreY() + radius * 0.5f);
            const float span = 0.85f;
            // 0 dB at the right, 20 dB at the left (the needle swings left as it compresses)
            auto angleFor = [&] (float db) { return span - jlimit (0.0f, 20.0f, db) / 20.0f * 2.0f * span; };
            auto pt = [&] (float db, float r) { const float ang = angleFor (db); return juce::Point<float> (c.x + std::sin (ang) * r, c.y - std::cos (ang) * r); };
            Path arc;
            arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, -span, span, true);
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.strokePath (arc, PathStrokeType (1.5f));
            for (int db = 0; db <= 20; db += 1)
            {
                const bool major = db % 4 == 0;
                g.setColour (Theme::ink.withAlpha (major ? 0.6f : 0.2f));
                g.drawLine (Line<float> (pt ((float) db, radius), pt ((float) db, radius + (major ? 12.0f : 6.0f))), major ? 1.6f : 1.0f);
                if (major)
                {
                    const auto p = pt ((float) db, radius + 26.0f);
                    g.setColour (Theme::text.withAlpha (0.8f));
                    g.setFont (FontOptions (12.0f, Font::bold));
                    g.drawText (String (db), Rectangle<float> (p.x - 14.0f, p.y - 8.0f, 28.0f, 16.0f), Justification::centred);
                }
            }
            shown += (comp.gainReductionDb.load() - shown) * 0.3f;
            const auto tip = pt (shown, radius - 6.0f);
            g.setColour (Theme::orange.withAlpha (0.25f));
            g.drawLine (Line<float> (c, tip), 6.0f);
            g.setColour (Theme::orange);
            g.drawLine (Line<float> (c, tip), 2.0f);
            // the hub hides the needle's foot
            {
                Graphics::ScopedSaveState ss (g);
                g.setColour (Pro::cardBot());
                g.fillEllipse (c.x - radius * 0.22f, c.y - radius * 0.22f, radius * 0.44f, radius * 0.44f);
                g.setColour (Theme::ink.withAlpha (0.08f));
                g.drawEllipse (c.x - radius * 0.22f, c.y - radius * 0.22f, radius * 0.44f, radius * 0.44f, 1.0f);
            }
            Graphs::label (g, "dB COMPRESSION", Rectangle<float> (b.getX(), c.y - radius * 0.55f, b.getWidth(), 12.0f), Justification::centred, 0.7f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (20.0f, Font::bold));
            g.drawText ("-" + String (shown, 1) + " dB", Rectangle<float> (b.getX(), b.getBottom() - 40.0f, b.getWidth(), 26.0f), Justification::centred);
        }
    private:
        void timerCallback() override { repaint(); }
        pulse::GlueCompressor& comp;
        te::Plugin::Ptr holder;
        float shown = 0;
    };

    //==============================================================================
    /** The auto filter's LFO: two cycles of the shape at its depth. Drag the handle up / down for the
        amount and sideways for the rate (it steps through the sync values when synced); pick the shape
        from the pictures; the strip on the right shows where the cutoff is right now. */
    class AutoFilterLfo : public SpaceUI::HandleView
    {
    public:
        using A = pulse::AutoFilter;
        explicit AutoFilterLfo (A& f) : HandleView (f), filter (f)
        {
            handles.push_back ({ "Amount / rate", Theme::accent, { P (A::pLfoAmount), P (A::pLfoRate), P (A::pLfoSync) },
                                 [this] { return juce::Point<float> (area.getX() + area.getWidth() * 0.125f, area.getCentreY() - filter.get (A::pLfoAmount) * area.getHeight() * 0.46f); },
                                 [this] (juce::Point<float> p, juce::Point<float> d)
                                 {
                                     Graphs::setValue (P (A::pLfoAmount), (area.getCentreY() - p.y) / (area.getHeight() * 0.46f));
                                     if (filter.getInt (A::pLfoSync) > 0) Graphs::setValue (P (A::pLfoSync), (float) jlimit (1, A::syncNames.size() - 1, startSync - roundToInt (d.x / 40.0f)));
                                     else Graphs::setValue (P (A::pLfoRate), startRate * std::pow (2.0f, -d.x / 80.0f));
                                 },
                                 [this] { return filter.param (A::pLfoAmount)->getCurrentValueAsString() + "  " + rateText(); },
                                 [this] (float d)
                                 {
                                     if (filter.getInt (A::pLfoSync) > 0) Graphs::setValue (P (A::pLfoSync), (float) jlimit (1, A::syncNames.size() - 1, filter.getInt (A::pLfoSync) + (d > 0 ? -1 : 1)));
                                     else Graphs::setValue (P (A::pLfoRate), filter.get (A::pLfoRate) * (d > 0 ? 1.1f : 0.9f));
                                 } });
            for (int s = 0; s < A::shapeNames.size(); ++s)
            {
                auto* b = shapes.add (new WavetableUI::ShapeButton (s, A::shapeNames[s]));
                b->onClick = [this, s]
                {
                    filter.edit.getUndoManager().beginNewTransaction ("LFO shape");
                    Graphs::setValue (P (A::pLfoShape), (float) s);
                };
                addAndMakeVisible (b);
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            startRate = filter.get (A::pLfoRate);
            startSync = filter.getInt (A::pLfoSync);
            HandleView::mouseDown (e);
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (10.0f);
            auto row = r.removeFromBottom (30.0f);
            r.removeFromBottom (8.0f);
            strip = r.removeFromRight (26.0f);
            r.removeFromRight (10.0f);
            area = r.withTrimmedTop (16.0f);
            const float bw = jmin (54.0f, row.getWidth() / jmax (1, shapes.size()));
            for (auto* s : shapes) s->setBounds (row.removeFromLeft (bw).reduced (2.0f, 0.0f).toNearestInt());
        }

        void paint (Graphics& g) override
        {
            ProUI::well (g, getLocalBounds().toFloat());
            const int shape = filter.getInt (A::pLfoShape);
            for (int s = 0; s < shapes.size(); ++s) shapes[s]->setToggleState (s == shape, dontSendNotification);
            g.setColour (Theme::ink.withAlpha (0.1f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
            g.setColour (Theme::ink.withAlpha (0.05f));
            for (int q = 1; q < 4; ++q) g.drawVerticalLine ((int) (area.getX() + area.getWidth() * q / 4.0f), area.getY(), area.getBottom());
            const float amt = filter.get (A::pLfoAmount);
            Path p, fill;
            fill.startNewSubPath (area.getX(), area.getCentreY());
            for (int i = 0; i <= (int) area.getWidth(); i += 2)
            {
                const double ph = i / area.getWidth() * 2.0;
                const float v = A::lfoValue (shape, ph, ((int) (ph * 4.0) % 3 - 1) * 0.7f) * amt;
                const float x = area.getX() + (float) i, y = area.getCentreY() - v * area.getHeight() * 0.46f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (area.getRight(), area.getCentreY());
            fill.closeSubPath();
            g.setColour (Theme::accent.withAlpha (0.15f));
            g.fillPath (fill);
            Graphs::glowStroke (g, p, Theme::accent, 2.0f);
            Graphs::label (g, rateText() + "   depth " + filter.param (A::pLfoAmount)->getCurrentValueAsString()
                              + (filter.get (A::pPhase) > 0.5f ? "   stereo " + filter.param (A::pPhase)->getCurrentValueAsString() : String()),
                           area.withHeight (12.0f).translated (0.0f, -15.0f), Justification::centredLeft, 0.85f);
            // where the cutoff is now (20 Hz at the bottom, 20 kHz at the top)
            g.setColour (Pro::shade (0.4f));
            g.fillRoundedRectangle (strip, 4.0f);
            auto yF = [&] (float f) { return strip.getBottom() - std::log (jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f) * strip.getHeight(); };
            g.setColour (Theme::ink.withAlpha (0.35f));
            g.fillRect (Rectangle<float> (strip.getX() + 3.0f, yF (filter.get (A::pCutoff)) - 0.5f, strip.getWidth() - 6.0f, 1.0f));
            g.setColour (Theme::orange);
            g.fillRoundedRectangle (Rectangle<float> (strip.getX() + 3.0f, yF (filter.currentCutoff.load()) - 2.5f, strip.getWidth() - 6.0f, 5.0f), 2.0f);
            drawHandles (g);
        }

    private:
        String rateText() const
        {
            return filter.getInt (A::pLfoSync) > 0 ? filter.param (A::pLfoSync)->getCurrentValueAsString() : filter.param (A::pLfoRate)->getCurrentValueAsString();
        }
        A& filter;
        Rectangle<float> area, strip;
        OwnedArray<WavetableUI::ShapeButton> shapes;
        float startRate = 1.0f;
        int startSync = 0;
    };
}
