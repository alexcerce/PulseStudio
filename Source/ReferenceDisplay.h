#pragma once

#include "PulseSynths.h"
#include "SpaceDisplay.h"
#include "DynamicsDisplay.h"
#include "About.h"

/** The window of the Reference device: which of the two you are hearing, how loud each is, and the song itself
    with the place it is playing from (drag the point to choose where in the song the project's start falls). */
namespace ReferenceUI
{
    using namespace SpaceUI;

    class ReferenceView : public HandleView
    {
    public:
        using R = pulse::Reference;
        explicit ReferenceView (R& r) : HandleView (r), ref (r)
        {
            handles.push_back ({ "Start at", Theme::yellow, { P (R::pOffset) },
                                 [this] { return juce::Point<float> (xT (ref.get (R::pOffset)), wave.getBottom()); },
                                 [this] (juce::Point<float> p, juce::Point<float>) { Graphs::setValue (P (R::pOffset), (float) tFor (p.x)); },
                                 [this] { return ref.param (R::pOffset)->getCurrentValueAsString(); },
                                 [this] (float d) { Graphs::setValue (P (R::pOffset), ref.get (R::pOffset) + (d > 0 ? 1.0f : -1.0f)); } });
        }

        void resized() override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f);
            readoutArea = r.removeFromBottom (34.0f);
            r.removeFromBottom (14.0f);
            tiles = r.removeFromTop (jmin (120.0f, r.getHeight() * 0.32f));
            r.removeFromTop (34.0f);
            wave = r.withTrimmedBottom (18.0f).reduced (6.0f, 0.0f);
        }

        void paint (Graphics& g) override
        {
            Graphs::proDisplay (g, getLocalBounds().toFloat());
            const bool listening = ref.getInt (R::pListen) > 0, has = ref.hasSong();
            const double seconds = ref.songSeconds(), songLufs = ref.songLufs();
            const float mix = ref.mixLufs.load();
            span = jmax (1.0, seconds);

            // three tiles: what you hear, and how loud the two are
            auto t = tiles;
            const float gap = 12.0f, w = (t.getWidth() - gap * 2.0f) / 3.0f;
            auto tile = [&] (const String& title, const String& big, const String& small, Colour c, bool lit)
            {
                auto b = t.removeFromLeft (w);
                t.removeFromLeft (gap);
                g.setColour (lit ? c.withAlpha (0.16f) : Pro::shade (0.3f));
                g.fillRoundedRectangle (b, 7.0f);
                g.setColour (lit ? c.withAlpha (0.9f) : Theme::ink.withAlpha (0.08f));
                g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, lit ? 1.5f : 1.0f);
                Graphs::label (g, title, b.reduced (14.0f, 10.0f).removeFromTop (12.0f), Justification::centredLeft, 0.6f);
                g.setColour (lit ? c : Theme::text);
                g.setFont (FontOptions (jmin (30.0f, b.getHeight() * 0.3f), Font::bold));
                g.drawText (big, b.reduced (14.0f, 0.0f), Justification::centredLeft, true);
                Graphs::label (g, small, b.reduced (14.0f, 10.0f).removeFromBottom (12.0f), Justification::centredLeft, 0.6f);
            };
            auto lufs = [] (double v) { return v < -70.0 ? String ("-") : String (v, 1) + " LUFS"; };
            tile ("YOU HEAR", listening ? "The reference" : "The mix", listening ? "the song from the file, at the same place in time" : "your project, as it is",
                  listening ? Theme::orange : Theme::accent, true);
            tile ("THE MIX", lufs (mix), mix < -70.0f ? "play the project: it is measured as it plays" : "measured while it plays", Theme::accent, false);
            const float turned = ref.matchDb.load();
            tile ("THE REFERENCE", lufs (songLufs), ! has ? String ("no song yet") : ref.getInt (R::pMatch) > 0 ? "turned " + String (turned > 0 ? "up " : "down ") + String (std::abs (turned), 1) + " dB to match the mix"
                                                                                                             : String ("played at its own loudness"), Theme::orange, false);

            areaTitle (g, wave, "THE SONG", has ? ref.getFile().getFileName() : String());
            g.setColour (Pro::shade (0.3f));
            g.fillRoundedRectangle (wave.expanded (6.0f), 5.0f);
            if (! has)
            {
                g.setColour (Theme::text.withAlpha (0.75f));
                g.setFont (FontOptions (13.0f));
                g.drawFittedText (ref.isLoading() ? String ("Reading the song...")
                                                  : ref.getFile() != File() ? "The song could not be read: " + ref.getFile().getFileName()
                                                                            : String ("Load a finished song whose sound you like (\"Load song...\"), play your project, and switch between the two."),
                                  wave.reduced (30.0f, 0.0f).toNearestInt(), Justification::centred, 2);
            }
            else
            {
                const double step = jmax (1.0, niceStep (span, 10));
                for (double s = 0.0; s <= span; s += step)
                {
                    g.setColour (Theme::ink.withAlpha (0.05f));
                    g.drawVerticalLine ((int) xT (s), wave.getY(), wave.getBottom());
                    Graphs::label (g, String ((int) s / 60) + ":" + String ((int) s % 60).paddedLeft ('0', 2), { xT (s) - 24.0f, wave.getBottom() + 3.0f, 48.0f, 11.0f }, Justification::centred, 0.55f);
                }
                std::array<float, R::shapeSize> shape;
                ref.getShape (shape);
                Path p;
                const float mid = wave.getCentreY(), half = wave.getHeight() * 0.46f;
                p.startNewSubPath (wave.getX(), mid);
                for (int i = 0; i < R::shapeSize; ++i) p.lineTo (wave.getX() + wave.getWidth() * (float) i / (float) (R::shapeSize - 1), mid - std::sqrt (shape[(size_t) i]) * half);
                for (int i = R::shapeSize; --i >= 0;) p.lineTo (wave.getX() + wave.getWidth() * (float) i / (float) (R::shapeSize - 1), mid + std::sqrt (shape[(size_t) i]) * half);
                p.closeSubPath();
                const auto c = listening ? Theme::orange : Theme::muted.brighter (0.2f);
                g.setGradientFill (ColourGradient (c.withAlpha (0.85f), 0.0f, mid, c.withAlpha (0.35f), 0.0f, wave.getY(), false));
                g.fillPath (p);
                // before the place the project starts at, nothing of the song is used
                g.setColour (Colours::black.withAlpha (0.35f));
                g.fillRect (wave.withRight (xT (ref.get (R::pOffset))));
                if (const double at = ref.songPosition.load(); at >= 0.0)
                {
                    g.setColour (Colours::white.withAlpha (0.9f));
                    g.fillRect (Rectangle<float> (xT (at) - 0.75f, wave.getY(), 1.5f, wave.getHeight()));
                }
            }
            drawHandles (g);
            drawReadouts (g, readoutArea, { { "SONG", has ? ref.getFile().getFileNameWithoutExtension() : String ("-") },
                                            { "LENGTH", has ? String ((int) seconds / 60) + ":" + String ((int) seconds % 60).paddedLeft ('0', 2) : String ("-") },
                                            { "STARTS AT", ref.param (R::pOffset)->getCurrentValueAsString() }, { "MATCH", ref.param (R::pMatch)->getCurrentValueAsString() },
                                            { "TRIM", ref.param (R::pTrim)->getCurrentValueAsString() }, { "LOOP", ref.param (R::pLoop)->getCurrentValueAsString() } });
        }

    private:
        float xT (double seconds) const  { return wave.getX() + (float) jlimit (0.0, 1.0, seconds / span) * wave.getWidth(); }
        double tFor (float x) const      { return jlimit (0.0, span, (double) ((x - wave.getX()) / jmax (1.0f, wave.getWidth())) * span); }

        R& ref;
        double span = 1.0;
        Rectangle<float> tiles, wave, readoutArea;
    };

    //==============================================================================
    class ReferencePanel : public DynamicsUI::Panel
    {
    public:
        using R = pulse::Reference;
        ReferencePanel (EditViewState& evs, R& r) : ref (r), holder (&r)
        {
            display = std::make_unique<ReferenceView> (r);
            addAndMakeVisible (*display);
            auto* ls = strips.add (new Strip ("LISTEN TO"));
            ls->add (new ParamChoice (*r.param (R::pListen), Theme::orange, false), 190);
            auto* fl = strips.add (new Strip ("SONG"));
            auto* load = fl->add (new TextButton ("Load song..."), 100);
            load->setTooltip ("Choose the finished song to compare the mix with");
            load->onClick = [this] { chooseFile(); };
            auto* ld = strips.add (new Strip ("LOUDNESS"));
            ld->add (new ParamToggle (*r.param (R::pMatch), "Match loudness", Theme::accent), 118);
            ld->add (knob (evs, *r.param (R::pTrim), "Trim"), 60);
            auto* again = ld->add (new TextButton ("Measure again"), 104);
            again->setTooltip ("Forget how loud the mix has been so far and measure it afresh (after changing the mix)");
            again->onClick = [this] { ref.measureAgain(); };
            auto* ps = strips.add (new Strip ("POSITION"));
            ps->add (knob (evs, *r.param (R::pOffset), "Start at"), 64);
            ps->add (new ParamToggle (*r.param (R::pLoop), "Loop", Theme::accent), 64);
            for (auto* st : strips) addAndMakeVisible (st);
        }

    private:
        void chooseFile()
        {
            auto start = ref.getFile().existsAsFile() ? ref.getFile() : File::getSpecialLocation (File::userMusicDirectory);
            chooser = std::make_shared<FileChooser> ("Choose a reference song", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", ! About::isCapturing());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<ReferencePanel> (this)] (const FileChooser& fc)
                                  {
                                      if (sp == nullptr || fc.getResult() == File()) return;
                                      sp->ref.edit.getUndoManager().beginNewTransaction ("Reference song");
                                      sp->ref.setFile (fc.getResult());
                                  });
        }

        R& ref;
        te::Plugin::Ptr holder;
        std::shared_ptr<FileChooser> chooser;
    };
}
