#pragma once

#include "MatchEQ.h"
#include "SpaceDisplay.h"
#include "DynamicsDisplay.h"
#include "About.h"

/** The window of Match EQ: the two sounds it has learned, one over the other, and under them the curve that
    takes this track towards the reference (drag its ends and its height). */
namespace MatchUI
{
    using namespace SpaceUI;

    class MatchView : public HandleView
    {
    public:
        using M = pulse::MatchEQ;
        explicit MatchView (M& m) : HandleView (m), eq (m)
        {
            handles.push_back ({ "From", Theme::yellow, { P (M::pLow) },
                                 [this] { return juce::Point<float> (xForFreq (curveArea, eq.get (M::pLow)), yCurve (0.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (M::pLow), freqForX (curveArea, p.x)); },
                                 [this] { return eq.param (M::pLow)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "To", Theme::yellow, { P (M::pHigh) },
                                 [this] { return juce::Point<float> (xForFreq (curveArea, eq.get (M::pHigh)), yCurve (0.0f)); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (M::pHigh), freqForX (curveArea, p.x)); },
                                 [this] { return eq.param (M::pHigh)->getCurrentValueAsString(); }, {} });
            handles.push_back ({ "Amount", Theme::accent, { P (M::pAmount) },
                                 [this] { return amountPoint(); },
                                 [this] (juce::Point<float>, juce::Point<float> delta)
                                 {
                                     // up is always more: the point sits on the curve's biggest move, which may be a cut
                                     Graphs::setValue (P (M::pAmount), amountAtDown - delta.y / jmax (1.0f, curveArea.getHeight()) * 3.0f);
                                 },
                                 [this] { return eq.param (M::pAmount)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (M::pAmount), eq.get (M::pAmount) + (d > 0 ? 0.05f : -0.05f)); } });
        }

        void mouseDown (const MouseEvent& e) override { amountAtDown = eq.get (M::pAmount); HandleView::mouseDown (e); }
        /** For the self-test: drags the Amount point as the mouse would. */
        void dragAmount (float dy) { amountAtDown = eq.get (M::pAmount); dragHandle (2, 0.0f, dy); }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            r.removeFromTop (22.0f);
            auto top = r.removeFromTop (r.getHeight() * 0.5f);
            soundArea = top.withTrimmedLeft (32.0f).withTrimmedBottom (18.0f);
            r.removeFromTop (26.0f);
            curveArea = r.withTrimmedLeft (32.0f).withTrimmedBottom (14.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            M::Points src, ref, curve;
            const bool gotSrc = eq.getSpectrum (false, src), gotRef = eq.getSpectrum (true, ref);
            eq.getCurve (curve);
            const auto learning = eq.isLearning();

            areaTitle (g, soundArea, "THE TWO SOUNDS", gotSrc && gotRef ? String ("this track, and the reference it is taken towards") : String());
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (soundArea.expanded (6.0f), 5.0f);
            freqGrid (g, soundArea, false);
            // each is drawn around its own middle, so their shapes can be compared whatever their loudness
            auto drawSound = [&] (const M::Points& p, Colour c, bool filled)
            {
                double mean = 0.0; int n = 0;
                for (int i = 0; i < M::numPoints; ++i) { const double f = pulse::SpectrumLearner::hzOf (i); if (f >= 100.0 && f <= 8000.0) { mean += p[(size_t) i]; ++n; } }
                mean /= jmax (1, n);
                Path line, fill;
                for (int i = 0; i < M::numPoints; ++i)
                {
                    const float x = xForFreq (soundArea, (float) pulse::SpectrumLearner::hzOf (i)), y = ySound (p[(size_t) i] - (float) mean);
                    if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, soundArea.getBottom()); } else line.lineTo (x, y);
                    fill.lineTo (x, y);
                }
                fill.lineTo (soundArea.getRight(), soundArea.getBottom());
                fill.closeSubPath();
                if (filled)
                {
                    g.setGradientFill (ColourGradient (c.withAlpha (0.35f), 0.0f, soundArea.getY(), c.withAlpha (0.03f), 0.0f, soundArea.getBottom(), false));
                    g.fillPath (fill);
                    g.setColour (c.withAlpha (0.8f));
                    g.strokePath (line, PathStrokeType (1.2f));
                }
                else
                    Graphs::glowStroke (g, line, c, 1.6f);
            };
            {
                Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (soundArea.expanded (1.0f).toNearestInt());
                if (gotSrc) drawSound (src, Theme::muted.brighter (0.3f), true);
                if (gotRef) drawSound (ref, Theme::orange, false);
            }
            // what is what, and what to do next
            auto key = soundArea.reduced (10.0f, 8.0f).removeFromTop (14.0f);
            auto legend = [&] (Colour c, const String& t)
            {
                g.setColour (c);
                g.fillRoundedRectangle (key.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 3.0f), 1.5f);
                key.removeFromLeft (5.0f);
                const float w = GlyphArrangement::getStringWidth (FontOptions (11.0f), t) + 14.0f;
                Graphs::label (g, t, key.removeFromLeft (w), Justification::centredLeft, 0.8f);
            };
            if (gotSrc) legend (Theme::muted.brighter (0.3f), "This track" + (eq.sourceName().isNotEmpty() && eq.sourceName() != "this track" ? " (" + eq.sourceName() + ")" : String()));
            if (gotRef) legend (Theme::orange, "Reference" + (eq.referenceName().isNotEmpty() ? " (" + eq.referenceName() + ")" : String()));
            String note;
            if (learning == M::learningSource)         note = "Listening to this track... " + String (eq.learnedSeconds.load(), 1) + " s  (play the loudest part of the song, then press Stop)";
            else if (learning == M::learningReference) note = eq.waitingForKeyInput() ? String ("Nothing comes in at the key input: choose the reference's track as this device's key input, and play")
                                                                                       : "Listening to the key input... " + String (eq.learnedSeconds.load(), 1) + " s";
            else if (! gotSrc && ! gotRef)             note = "1. Play the song and press \"Learn this track\".    2. Give it a reference: an audio file, or another track through the key input.";
            else if (! gotSrc)                         note = "Now play the song and press \"Learn this track\".";
            else if (! gotRef)                         note = "Now give it a reference: an audio file, or another track through the key input.";
            if (note.isNotEmpty())
            {
                g.setColour (learning != M::notLearning ? Theme::green : Theme::text.withAlpha (0.75f));
                g.setFont (FontOptions (13.0f));
                g.drawFittedText (note, soundArea.reduced (30.0f, 0.0f).toNearestInt(), Justification::centred, 2);
            }

            areaTitle (g, curveArea, "WHAT THE EQ DOES", "drag the ends of the range, and the point for the amount");
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (curveArea.expanded (6.0f), 5.0f);
            freqGrid (g, curveArea, true);
            for (float db = -scaleDb; db <= scaleDb; db += 6.0f)
            {
                g.setColour (Theme::ink.withAlpha (db == 0.0f ? 0.16f : 0.04f));
                g.drawHorizontalLine ((int) yCurve (db), curveArea.getX(), curveArea.getRight());
                Graphs::label (g, (db > 0 ? "+" : "") + String ((int) db), { curveArea.getX() - 30.0f, yCurve (db) - 6.0f, 26.0f, 12.0f }, Justification::centredRight, 0.55f);
            }
            // outside From and To nothing is changed
            g.setColour (Colours::black.withAlpha (0.25f));
            g.fillRect (curveArea.withRight (xForFreq (curveArea, eq.get (M::pLow))));
            g.fillRect (curveArea.withLeft (xForFreq (curveArea, eq.get (M::pHigh))));
            // the most it may change
            const float most = eq.get (M::pRange);
            g.setColour (Theme::yellow.withAlpha (0.25f));
            for (float s : { -1.0f, 1.0f })
                if (most < scaleDb) g.drawHorizontalLine ((int) yCurve (s * most), curveArea.getX(), curveArea.getRight());
            Path line, fill;
            fill.startNewSubPath (curveArea.getX(), yCurve (0.0f));
            for (float x = curveArea.getX(); x <= curveArea.getRight(); x += 2.0f)
            {
                const float y = yCurve (M::curveAt (curve, freqForX (curveArea, x)));
                if (x == curveArea.getX()) line.startNewSubPath (x, y); else line.lineTo (x, y);
                fill.lineTo (x, y);
            }
            fill.lineTo (curveArea.getRight(), yCurve (0.0f));
            fill.closeSubPath();
            g.setColour (Theme::accent.withAlpha (0.14f));
            g.fillPath (fill);
            Graphs::glowStroke (g, line, Theme::accent, 2.0f);

            drawHandles (g);
            drawReadouts (g, readoutArea, { { "THIS TRACK", gotSrc ? String ("learned") : String ("-") }, { "REFERENCE", gotRef ? (eq.referenceName().isNotEmpty() ? eq.referenceName() : String ("learned")) : String ("-") },
                                            { "AMOUNT", eq.param (M::pAmount)->getCurrentValueAsString() }, { "SMOOTHING", eq.param (M::pSmooth)->getCurrentValueAsString() },
                                            { "MOST CHANGE", eq.param (M::pRange)->getCurrentValueAsString() },
                                            { "RANGE", eq.param (M::pLow)->getCurrentValueAsString() + " - " + eq.param (M::pHigh)->getCurrentValueAsString() } });
        }

    private:
        static constexpr float scaleDb = 18.0f;
        float ySound (float db) const { return soundArea.getCentreY() - jlimit (-1.0f, 1.0f, db / 36.0f) * soundArea.getHeight() * 0.5f; }
        float yCurve (float db) const { return curveArea.getCentreY() - jlimit (-1.0f, 1.0f, db / scaleDb) * curveArea.getHeight() * 0.5f; }

        /** Where the Amount point sits: on the curve's biggest move (in the middle when there is no curve yet). */
        juce::Point<float> amountPoint() const
        {
            M::Points c;
            eq.getCurve (c);
            int best = M::numPoints / 2;
            float top = 0.0f;
            for (int i = 0; i < M::numPoints; ++i) if (std::abs (c[(size_t) i]) > top + 0.05f) { top = std::abs (c[(size_t) i]); best = i; }
            return { xForFreq (curveArea, (float) pulse::SpectrumLearner::hzOf (best)), yCurve (c[(size_t) best]) };
        }

        M& eq;
        float amountAtDown = 1.0f;
        Rectangle<float> soundArea, curveArea, readoutArea;
    };

    //==============================================================================
    class MatchPanel : public DynamicsUI::Panel, private Timer
    {
    public:
        using M = pulse::MatchEQ;
        MatchPanel (EditViewState& evs, M& m) : eq (m), holder (&m)
        {
            display = std::make_unique<MatchView> (m);
            addAndMakeVisible (*display);
            auto* ln = strips.add (new Strip ("LEARN"));
            learnSrc = ln->add (new TextButton ("Learn this track"), 150);
            learnSrc->setTooltip ("Listens to this track while it plays; press again to stop and keep what was heard");
            learnSrc->onClick = [this] { toggle (M::learningSource); };
            learnRef = ln->add (new TextButton ("Learn key input"), 130);
            learnRef->setTooltip ("Listens to the track chosen as this device's key input (the reference); press again to stop");
            learnRef->onClick = [this] { toggle (M::learningReference); };
            auto* file = ln->add (new TextButton ("Reference file..."), 116);
            file->setTooltip ("Use an audio file as the reference: a finished song whose tone you like");
            file->onClick = [this] { chooseFile(); };
            auto* mt = strips.add (new Strip ("MATCH"));
            mt->add (knob (evs, *m.param (M::pAmount), "Amount"), 62);
            mt->add (knob (evs, *m.param (M::pSmooth), "Smoothing"), 66);
            mt->add (knob (evs, *m.param (M::pRange), "Most change"), 74);
            auto* rg = strips.add (new Strip ("RANGE"));
            rg->add (knob (evs, *m.param (M::pLow), "From"), 62);
            rg->add (knob (evs, *m.param (M::pHigh), "To"), 62);
            auto* out = strips.add (new Strip ("OUTPUT"));
            out->add (knob (evs, *m.param (M::pOutput), "Output"), 64);
            for (auto* st : strips) addAndMakeVisible (st);
            startTimerHz (8);
            timerCallback();
        }

        /** Loads a reference file as the button does (for the self-test). */
        bool loadReference (const File& f)
        {
            // the file is listened to on a thread of its own, with a progress window: a long song takes a few seconds
            struct Listener : public ThreadWithProgressWindow
            {
                Listener (te::Engine& e, const File& file) : ThreadWithProgressWindow ("Listening to " + file.getFileName(), true, true), engine (e), f (file) {}
                void run() override { ok = pulse::MatchEQ::listenToFile (engine, f, points, [this] (float p) { setProgress (p); return ! threadShouldExit(); }); }
                te::Engine& engine;
                File f;
                pulse::MatchEQ::Points points {};
                bool ok = false;
            };
            Listener listener (eq.engine, f);
            if (About::isCapturing()) listener.run(); else if (! listener.runThread()) return true;   // (cancelled: nothing changes, and nothing is wrong)
            if (! listener.ok) return false;
            eq.edit.getUndoManager().beginNewTransaction ("Match EQ reference");
            eq.keep (true, listener.points, f.getFileName());
            return true;
        }

    private:
        void toggle (M::Learning which)
        {
            if (eq.isLearning() == which)
            {
                eq.edit.getUndoManager().beginNewTransaction ("Match EQ learn");
                eq.stopLearning();
            }
            else
            {
                if (eq.isLearning() != M::notLearning) { eq.edit.getUndoManager().beginNewTransaction ("Match EQ learn"); eq.stopLearning(); }
                eq.startLearning (which);
            }
            timerCallback();
        }

        void chooseFile()
        {
            chooser = std::make_shared<FileChooser> ("Choose a reference", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", ! About::isCapturing());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<MatchPanel> (this)] (const FileChooser& fc)
                                  {
                                      if (sp == nullptr || fc.getResult() == File()) return;
                                      if (! sp->loadReference (fc.getResult()) && ! About::isCapturing())
                                          AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Match EQ", "That file could not be read, or there is no sound in it.");
                                  });
        }

        void timerCallback() override
        {
            const auto l = eq.isLearning();
            auto show = [] (TextButton& b, bool on, const String& idle)
            {
                const String t = on ? String ("Stop and keep") : idle;
                if (b.getButtonText() != t) b.setButtonText (t);
                b.setColour (TextButton::buttonOnColourId, Theme::green);
                b.setColour (TextButton::textColourOnId, Colour (0xff111317));
                if (b.getToggleState() != on) b.setToggleState (on, dontSendNotification);
            };
            show (*learnSrc, l == M::learningSource, eq.hasSource() ? "Learn this track again" : "Learn this track");
            show (*learnRef, l == M::learningReference, "Learn key input");
        }

        M& eq;
        te::Plugin::Ptr holder;
        TextButton* learnSrc = nullptr;
        TextButton* learnRef = nullptr;
        std::shared_ptr<FileChooser> chooser;
    };
}
