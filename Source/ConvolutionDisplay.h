#pragma once

#include "Convolution.h"
#include "SpaceDisplay.h"
#include "About.h"

/** The window of Pulse Space: the response of the space over time (drag the predelay, the length and the
    decay on it) and the tone of the reverb (low and high cut as points), with the rest in the bottom bar. */
namespace SpaceUI
{
    class ConvolutionView : public HandleView
    {
    public:
        using C = pulse::ConvolutionReverb;
        explicit ConvolutionView (C& c) : HandleView (c), rev (c)
        {
            handles.push_back ({ "Predelay", Theme::ink, { P (C::pPredelay) },
                                 [this] { return juce::Point<float> (xT (predelay()), time.getY() + 10.0f); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (C::pPredelay), (float) (tFor (p.x) * 1000.0)); },
                                 [this] { return rev.param (C::pPredelay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (C::pPredelay), rev.get (C::pPredelay) + (d > 0 ? 2.0f : -2.0f)); } });
            handles.push_back ({ "Size", Theme::accent, { P (C::pSize) },
                                 [this] { return juce::Point<float> (xT (predelay() + seconds), time.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>)
                                 {
                                     // the end of the response follows the mouse: its length grows with the size
                                     const double want = jmax (0.02, tFor (p.x) - predelay());
                                     Graphs::setValue (P (C::pSize), rev.get (C::pSize) * (float) (want / jmax (0.02, seconds)));
                                 },
                                 [this] { return rev.param (C::pSize)->getCurrentValueAsString() + "  (" + String (seconds, 2) + " s)"; },
                                 [this] (float d) { Graphs::setValue (P (C::pSize), rev.get (C::pSize) + (d > 0 ? 0.03f : -0.03f)); } });
            handles.push_back ({ "Decay", Theme::orange, { P (C::pDecay) },
                                 [this] { return juce::Point<float> (xT (predelay() + seconds * 0.4), yDb (shapeAt (0.4f))); },
                                 [this] (juce::Point<float>, juce::Point<float> delta) { Graphs::setValue (P (C::pDecay), decayAtDown - delta.y / jmax (1.0f, time.getHeight()) * 1.6f); },
                                 [this] { return rev.param (C::pDecay)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (C::pDecay), rev.get (C::pDecay) + (d > 0 ? 0.03f : -0.03f)); } });
            handles.push_back ({ "Low cut", Theme::yellow, { P (C::pLowCut) },
                                 [this] { return juce::Point<float> (xForFreq (tone, rev.get (C::pLowCut)), yTone (-3.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (C::pLowCut), freqForX (tone, p.x)); },
                                 [this] { return rev.param (C::pLowCut)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "High cut", Theme::yellow, { P (C::pHighCut) },
                                 [this] { return juce::Point<float> (xForFreq (tone, rev.get (C::pHighCut)), yTone (-3.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (C::pHighCut), freqForX (tone, p.x)); },
                                 [this] { return rev.param (C::pHighCut)->getCurrentValueAsString(); }, {} });
            refresh();
        }

        void mouseDown (const MouseEvent& e) override { decayAtDown = rev.get (C::pDecay); HandleView::mouseDown (e); }

        /** For the self-test: drags the Decay point as the mouse would (it works from where the drag began). */
        void dragDecay (float dy) { decayAtDown = rev.get (C::pDecay); dragHandle (2, 0.0f, dy); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            auto top = r.removeFromTop (r.getHeight() * 0.62f);
            time = top.withTrimmedLeft (32.0f).withTrimmedBottom (18.0f);
            r.removeFromTop (26.0f);
            meter = r.removeFromRight (20.0f).withTrimmedBottom (14.0f);
            r.removeFromRight (16.0f);
            tone = r.withTrimmedLeft (32.0f).withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            refresh();
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            areaTitle (g, time, "THE SPACE OVER TIME", rev.usingFile() ? rev.getFile().getFileName() : String ("drag: predelay, length, decay"));
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (time.expanded (6.0f), 5.0f);
            span = jmax (0.25, (predelay() + seconds) * 1.12);
            const double step = niceStep (span, 8);
            for (double t = 0.0; t <= span; t += step)
            {
                g.setColour (Theme::ink.withAlpha (0.05f));
                g.drawVerticalLine ((int) xT (t), time.getY(), time.getBottom());
                Graphs::label (g, t < 1.0 ? String (roundToInt (t * 1000.0)) + " ms" : String (t, 1) + " s", { xT (t) - 24.0f, time.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
            }
            for (float db = 0.0f; db >= floorDb; db -= 12.0f)
            {
                g.setColour (Theme::ink.withAlpha (0.04f));
                g.drawHorizontalLine ((int) yDb (db), time.getX(), time.getRight());
                Graphs::label (g, String ((int) db), { time.getX() - 30.0f, yDb (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            // the dry sound, then the response
            g.setColour (Theme::ink.withAlpha (0.7f));
            g.fillRect (Rectangle<float> (xT (0.0) - 1.0f, yDb (0.0f), 2.0f, time.getBottom() - yDb (0.0f)));
            Path fill, line;
            const float x0 = xT (predelay()), x1 = xT (predelay() + seconds);
            fill.startNewSubPath (x0, time.getBottom());
            for (int i = 0; i < C::shapeSize; ++i)
            {
                const float x = x0 + (x1 - x0) * (float) i / (float) (C::shapeSize - 1), y = yDb (shape[(size_t) i]);
                fill.lineTo (x, y);
                if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            fill.lineTo (x1, time.getBottom());
            fill.closeSubPath();
            g.setGradientFill (ColourGradient (Theme::accent.withAlpha (0.5f), 0.0f, time.getY(), Theme::accent.withAlpha (0.04f), 0.0f, time.getBottom(), false));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, Theme::accent, 1.5f);
            if (predelay() > 0.0005)
            {
                g.setColour (Theme::ink.withAlpha (0.35f));
                g.drawHorizontalLine ((int) time.getY() + 10, xT (0.0), x0);
            }

            areaTitle (g, tone, "TONE OF THE REVERB", "drag the points: low cut, high cut");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (tone.expanded (6.0f), 5.0f);
            freqGrid (g, tone, true);
            pulse::Biquad lo, hi;
            lo.set (pulse::Biquad::Type::lowCut, 96000.0, rev.get (C::pLowCut), 0.707, 0.0);
            hi.set (pulse::Biquad::Type::highCut, 96000.0, rev.get (C::pHighCut), 0.707, 0.0);
            Path tp, tf;
            tf.startNewSubPath (tone.getX(), tone.getBottom());
            for (float x = tone.getX(); x <= tone.getRight(); x += 2.0f)
            {
                const double f = freqForX (tone, x);
                const float y = yTone (Decibels::gainToDecibels ((float) (lo.magnitude (f, 96000.0) * hi.magnitude (f, 96000.0)), -60.0f));
                if (x == tone.getX()) tp.startNewSubPath (x, y); else tp.lineTo (x, y);
                tf.lineTo (x, y);
            }
            tf.lineTo (tone.getRight(), tone.getBottom());
            tf.closeSubPath();
            g.setColour (Theme::yellow.withAlpha (0.10f));
            g.fillPath (tf);
            Graphs::glowStroke (g, tp, Theme::yellow, 1.8f);

            // how loud the reverb is right now
            g.setColour (Pro::shade (0.4f));
            g.fillRoundedRectangle (meter, 4.0f);
            const float lvl = jlimit (0.0f, 1.0f, 1.0f + rev.wetDb.load() / 60.0f);
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (meter.withTop (meter.getBottom() - lvl * meter.getHeight()).reduced (4.0f, 0.0f), 3.0f);

            drawHandles (g);
            drawReadouts (g, readoutArea, { { "SPACE", rev.usingFile() ? rev.getFile().getFileNameWithoutExtension() : rev.param (C::pSpace)->getCurrentValueAsString() },
                                            { "LENGTH", String (seconds, 2) + " s" }, { "SIZE", rev.param (C::pSize)->getCurrentValueAsString() },
                                            { "DECAY", rev.param (C::pDecay)->getCurrentValueAsString() }, { "PREDELAY", rev.param (C::pPredelay)->getCurrentValueAsString() },
                                            { "DRY/WET", rev.param (C::pMix)->getCurrentValueAsString() } });
        }

    private:
        static constexpr float floorDb = -72.0f;
        void refresh() { rev.getShape (shape, seconds); seconds = jmax (0.02, seconds); span = jmax (0.25, (predelay() + seconds) * 1.12); }
        double predelay() const        { return rev.get (C::pPredelay) * 0.001; }
        float xT (double t) const      { return time.getX() + (float) (t / span) * time.getWidth(); }
        double tFor (float x) const    { return jlimit (0.0, span * 4.0, (double) ((x - time.getX()) / jmax (1.0f, time.getWidth())) * span); }
        float yDb (float db) const     { return time.getY() + jlimit (0.0f, 1.0f, db / floorDb) * time.getHeight(); }
        float yTone (float db) const   { return tone.getY() + jlimit (0.0f, 1.0f, (6.0f - db) / 36.0f) * tone.getHeight(); }
        float shapeAt (float frac) const
        {
            // the loudest of the slices around this point, so the handle rides the outline
            float best = -100.0f;
            const int at = (int) (frac * (C::shapeSize - 1));
            for (int i = jmax (0, at - 6); i <= jmin (C::shapeSize - 1, at + 6); ++i) best = jmax (best, shape[(size_t) i]);
            return best;
        }

        C& rev;
        std::array<float, C::shapeSize> shape {};
        double seconds = 1.0, span = 1.0;
        float decayAtDown = 1.0f;
        Rectangle<float> time, tone, meter, readoutArea;
    };

    //==============================================================================
    class ConvolutionPanel : public DynamicsUI::Panel
    {
    public:
        using C = pulse::ConvolutionReverb;
        ConvolutionPanel (EditViewState& evs, C& c) : rev (c), holder (&c)
        {
            display = std::make_unique<ConvolutionView> (c);
            addAndMakeVisible (*display);
            auto* sp = strips.add (new Strip ("SPACE"));
            sp->add (new ParamCombo (*c.param (C::pSpace), false), 150);
            auto* load = sp->add (new TextButton ("Load file..."), 92);
            load->setTooltip ("Use any audio file as the space: a recorded impulse response, or any sound at all");
            load->onClick = [this] { chooseFile(); };
            sp->add (new ParamToggle (*c.param (C::pReverse), "Reverse", Theme::accent), 76);
            auto* sh = strips.add (new Strip ("SHAPE"));
            sh->add (knob (evs, *c.param (C::pSize), "Size"), 62);
            sh->add (knob (evs, *c.param (C::pDecay), "Decay"), 62);
            sh->add (knob (evs, *c.param (C::pPredelay), "Predelay"), 62);
            auto* tn = strips.add (new Strip ("TONE"));
            tn->add (knob (evs, *c.param (C::pLowCut), "Low cut"), 62);
            tn->add (knob (evs, *c.param (C::pHighCut), "High cut"), 62);
            tn->add (knob (evs, *c.param (C::pWidth), "Width"), 62);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *c.param (C::pMix), "Dry/Wet"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }

    private:
        void chooseFile()
        {
            auto start = rev.getFile().existsAsFile() ? rev.getFile() : File::getSpecialLocation (File::userMusicDirectory);
            chooser = std::make_shared<FileChooser> ("Load a space", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", ! About::isCapturing());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<ConvolutionPanel> (this)] (const FileChooser& fc)
                                  {
                                      if (sp == nullptr || fc.getResult() == File()) return;
                                      sp->rev.edit.getUndoManager().beginNewTransaction ("Load a space");
                                      sp->rev.setFile (fc.getResult());
                                  });
        }

        C& rev;
        te::Plugin::Ptr holder;
        std::shared_ptr<FileChooser> chooser;
    };
}
