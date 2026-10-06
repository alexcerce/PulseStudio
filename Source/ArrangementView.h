#pragma once

#include "EditViewState.h"
#include "Sections.h"
#include "Chords.h"
#include "BusLanes.h"
#include "PluginMenu.h"
#include "AutomationLane.h"
#include "ClipProperties.h"
#include "ClipImport.h"
#include "EditingOps.h"
#include "Presets.h"
#include "Workflow.h"
#include "AudioToMidi.h"
#include "Stems.h"
#include "Timeline.h"
#include "SampleEditor.h"
#include "Meters.h"
#include "DragDrop.h"
#include "Notes.h"

//==============================================================================
/** Draws a bar/beat grid between x positions for the given view. */
inline void drawBeatGrid (Graphics& g, const EditViewState& evs, Rectangle<int> area)
{
    auto& edit = const_cast<te::Edit&> (evs.edit);
    const int bpb = EngineHelpers::beatsPerBar (edit);
    double step = 0.25;
    for (double st : { 0.25, 0.5, 1.0 })
        if (st * evs.pixelsPerBeat >= 14.0) { step = st; break; }
    const bool showBeats = step * evs.pixelsPerBeat >= 14.0 && evs.pixelsPerBeat >= 10.0;

    const auto b0 = evs.xToBeat (0.0f), b1 = evs.xToBeat ((float) area.getRight());
    if (showBeats)
        for (double b = std::floor (b0 / step) * step; b <= b1; b += step)
        {
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (Theme::ink.withAlpha (beat ? 0.06f : 0.03f));
            g.drawVerticalLine ((int) evs.beatToX (b), (float) area.getY(), (float) area.getBottom());
        }

    int barStep = 1;
    while (barStep * bpb * evs.pixelsPerBeat < 14.0) barStep *= 2;
    Timeline::forEachBar (edit, b0, b1, barStep, [&] (int, double b)
    {
        g.setColour (Theme::ink.withAlpha (0.13f));
        g.drawVerticalLine ((int) evs.beatToX (b), (float) area.getY(), (float) area.getBottom());
    });
}

//==============================================================================
class ClipComponent : public Component,
                      public SettableTooltipClient
{
public:
    ClipComponent (EditViewState& e, te::Clip::Ptr c, std::function<te::AudioTrack* (int screenY)> trackFinder)
        : evs (e), clip (c), findTrackAt (std::move (trackFinder))
    {
        if (auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        {
            te::AudioFile af (wac->getAudioFile());

            if (af.isValid())
                thumbnail = std::make_unique<te::SmartThumbnail> (evs.edit.engine, wac->getPlaybackFile(), *this, &evs.edit);
        }
    }

    te::Clip& getClip() { return *clip; }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto col = clip->getTrack() != nullptr ? clip->getTrack()->getColour() : Theme::accent;
        if (col == Colour()) col = Theme::accent;
        const bool selected = evs.selectionManager.isSelected (clip.get());

        // body: tinted, slightly darker towards the bottom
        g.setGradientFill (ColourGradient (col.withAlpha (0.32f), 0.0f, r.getY(), col.withAlpha (0.18f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);

        auto header = r.removeFromTop (15.0f);
        g.setGradientFill (ColourGradient (col.brighter (0.15f), 0.0f, header.getY(), col.darker (0.1f), 0.0f, header.getBottom(), false));
        g.fillRoundedRectangle (header.withHeight (19.0f), 5.0f);
        g.setColour (col.withAlpha (0.28f));
        g.fillRect (header.withTop (header.getBottom()).withHeight (4.0f));
        g.setColour (col.getPerceivedBrightness() > 0.55f ? Colours::black.withAlpha (0.85f) : Theme::ink.withAlpha (0.92f));
        g.setFont (FontOptions (12.0f, Font::bold));
        g.drawText (clip->getName(), header.reduced (6.0f, 0.0f), Justification::centredLeft, true);


        auto body = getLocalBounds().withTrimmedTop (17).reduced (0, 2);

        if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
            drawMidi (g, *mc, body, col);
        else if (thumbnail != nullptr)
            drawWave (g, body, col);

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
            drawFadesAndGain (g, *ac, body, col, header);

        drawLoopBoundaries (g, body);

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
            if (ac->getWarpTime())
                drawWarpMarkers (g, *ac, body);

        if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            StringArray tags;
            if (mc->getGrooveTemplate().isNotEmpty()) tags.add ("GROOVE");
            if (mc->isLooping()) tags.add ("LOOP");
            g.setColour (Colours::black.withAlpha (0.8f));
            g.setFont (FontOptions (11.5f));
            g.drawText (tags.joinIntoString ("  "), header.reduced (5.0f, 0.0f), Justification::centredRight, false);
        }

        if (selected)
        {
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 5.0f, 3.0f);
        }
        g.setColour (selected ? Colours::white : col.brighter (0.1f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, selected ? 1.4f : 1.0f);
    }

    void mouseMove (const MouseEvent& e) override
    {
        auto m = modeAt (e.position);
        setMouseCursor (m == Mode::move ? MouseCursor::DraggingHandCursor : MouseCursor::LeftRightResizeCursor);
        setTooltip (m == Mode::warp ? "Warp marker: drag to move (Alt = no snap), right-click to delete"
                                    : dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr && static_cast<te::AudioClipBase*> (clip.get())->getWarpTime() && e.position.y >= (float) getHeight() - markerStripHeight
                                        ? "Drag a hit (a small tick) to pin it to a beat; double-click elsewhere for a marker" : String());
    }

    void mouseDown (const MouseEvent& e) override
    {
        auto& sm = evs.selectionManager;

        if (e.mods.isCtrlDown())
        {
            if (sm.isSelected (clip.get())) sm.deselect (clip.get());
            else                            sm.addToSelection (clip.get());
            mode = Mode::none;
            return;
        }

        if (e.mods.isShiftDown())                   sm.addToSelection (clip.get());
        else if (! sm.isSelected (clip.get()))      sm.selectOnly (clip.get());

        if (e.mods.isPopupMenu())
        {
            if (modeAt (e.position) == Mode::warp)
                return showWarpMarkerMenu();
            return showContextMenu();
        }

        auto pos = clip->getPosition();
        dragStartBeat = EngineHelpers::timeToBeat (evs.edit, pos.getStart());
        dragEndBeat = EngineHelpers::timeToBeat (evs.edit, pos.getEnd());
        mode = modeAt (e.position);

        // Grabbing a hit in the warp strip where there is no marker yet: one is put exactly on the hit, and the drag moves it.
        if (mode != Mode::warp)
            if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()); ac != nullptr && ac->getWarpTime() && e.position.y >= (float) getHeight() - markerStripHeight)
            {
                const double beat = evs.xToBeat (evs.beatToX (ac->getStartBeat().inBeats()) + e.position.x);
                if (auto hit = Workflow::transientNearBeat (*ac, beat, 6.0 / jmax (1.0, (double) evs.pixelsPerBeat)))
                {
                    evs.edit.getUndoManager().beginNewTransaction ("Warp marker on a hit");
                    warpIndex = Workflow::addWarpMarkerAtTransient (*ac, *hit);
                    if (warpIndex >= 0) mode = Mode::warp;
                }
            }

        // remember every selected clip's start so they can move together
        groupStarts.clear();
        for (auto c : sm.getItemsOfType<te::Clip>())
            groupStarts.push_back ({ c, EngineHelpers::timeToBeat (evs.edit, c->getPosition().getStart()) });

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            dragFadeIn = ac->getFadeIn();
            dragFadeOut = ac->getFadeOut();
        }

        if (mode == Mode::move && e.mods.isAltDown() && dynamic_cast<te::AudioClipBase*> (clip.get()) != nullptr)
        {
            mode = Mode::slip;   // Alt-drag: move the audio inside the clip
            dragOffset = clip->getPosition().getOffset();
        }
        const char* names[] = { "Move clip", "Resize clip", "Clip fade in", "Clip fade out", "Move warp marker", "", "Slip clip content" };
        evs.edit.getUndoManager().beginNewTransaction (names[(int) mode]);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || mode == Mode::none)
            return;

        const double delta = e.getDistanceFromDragStartX() / evs.pixelsPerBeat;
        const auto secondsPerPixel = [this] { return 60.0 / (evs.edit.tempoSequence.getTempo (0)->getBpm() * evs.pixelsPerBeat); };

        switch (mode)
        {
            case Mode::resize:
            {
                const double minLen = evs.snapBeats > 0.0 ? evs.snapBeats : 0.25;
                auto newEnd = jmax (dragStartBeat + minLen, evs.snap (dragEndBeat + delta));
                clip->setEnd (EngineHelpers::beatToTime (evs.edit, newEnd), true);
                break;
            }
            case Mode::slip:
                clip->setOffset (te::TimeDuration::fromSeconds (jmax (0.0, dragOffset.inSeconds() - e.getDistanceFromDragStartX() * secondsPerPixel())));
                repaint();
                break;
            case Mode::fadeIn:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                    ac->setFadeIn (te::TimeDuration::fromSeconds (jmax (0.0, dragFadeIn.inSeconds() + e.getDistanceFromDragStartX() * secondsPerPixel())));
                repaint();
                break;
            case Mode::fadeOut:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                    ac->setFadeOut (te::TimeDuration::fromSeconds (jmax (0.0, dragFadeOut.inSeconds() - e.getDistanceFromDragStartX() * secondsPerPixel())));
                repaint();
                break;
            case Mode::move:
            {
                // snap the grabbed clip, move the rest of the selection by the same amount
                auto shift = jmax (0.0, evs.snap (dragStartBeat + delta)) - dragStartBeat;
                for (auto& [c, start] : groupStarts)
                    shift = jmax (shift, -start);

                for (auto& [c, start] : groupStarts)
                    c->setStart (EngineHelpers::beatToTime (evs.edit, start + shift), false, true);
                break;
            }
            case Mode::warp:
                if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                {
                    auto beat = evs.xToBeat (evs.beatToX (ac->getStartBeat().inBeats()) + e.position.x);
                    if (! e.mods.isAltDown()) beat = evs.snap (beat);
                    ac->getWarpTimeManager().moveMarker (warpIndex, Workflow::editBeatToWarpTime (*ac, beat));
                    repaint();
                }
                break;
            case Mode::none:
                break;
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        auto* sourceTrack = dynamic_cast<te::AudioTrack*> (clip->getTrack());
        auto updateFades = [c = clip, sourceTrack]
        {
            if (sourceTrack != nullptr) Timeline::updateCrossfades (*sourceTrack);
            if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()); t != nullptr && t != sourceTrack) Timeline::updateCrossfades (*t);
        };

        if (mode != Mode::move || e.mods.isPopupMenu() || findTrackAt == nullptr || groupStarts.size() > 1)
        {
            if (mode == Mode::move || mode == Mode::resize) updateFades();
            return;
        }

        // Moving to another track happens on release (it rebuilds the clip components).
        if (auto target = findTrackAt (e.getScreenY()))
            if (target != clip->getTrack())
                clip->moveTo (*target);
        updateFades();
    }

    /** Edits the clip's name in place (double-click the name bar). */
    void startRename()
    {
        auto* ed = new TextEditor();
        renameEditor.reset (ed);
        ed->setFont (FontOptions (12.5f));
        ed->setText (clip->getName(), false);
        ed->selectAll();
        ed->setColour (TextEditor::backgroundColourId, Theme::well);
        ed->setColour (TextEditor::outlineColourId, Theme::accent);
        ed->setBounds (0, 0, jmax (120, jmin (getWidth(), 260)), 17);
        auto finish = [this] (bool commit)
        {
            if (renameEditor == nullptr) return;
            const auto n = renameEditor->getText().trim();
            if (commit && n.isNotEmpty() && n != clip->getName())
            {
                evs.edit.getUndoManager().beginNewTransaction ("Rename clip");
                clip->setName (n);
            }
            MessageManager::callAsync ([sp = SafePointer<Component> (this)] { if (auto* c = dynamic_cast<ClipComponent*> (sp.getComponent())) c->renameEditor.reset(); });
        };
        ed->onReturnKey = [finish] { finish (true); };
        ed->onEscapeKey = [finish] { finish (false); };
        ed->onFocusLost = [finish] { finish (true); };
        addAndMakeVisible (ed);
        ed->grabKeyboardFocus();
    }

    std::unique_ptr<TextEditor> renameEditor;

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (e.position.y < 15.0f) { startRename(); return; }

        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
            if (ac->getWarpTime() && e.position.y >= (float) getHeight() - markerStripHeight)
            {
                auto beat = evs.snap (evs.xToBeat (evs.beatToX (ac->getStartBeat().inBeats()) + e.position.x));
                evs.edit.getUndoManager().beginNewTransaction ("Add warp marker");
                Workflow::addWarpMarkerAtBeat (*ac, beat);
                repaint();
                return;
            }

        if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            if (evs.openPianoRoll)
                evs.openPianoRoll (mc);
        }
        else if (auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        {
            if (wac->isUsingARA()) te::showARAWindow (*wac);
            else                   showAudioClipPanel (*wac, *this);
        }
    }

private:
    enum class Mode { move, resize, fadeIn, fadeOut, warp, none, slip };
    std::vector<std::pair<te::Clip::Ptr, double>> groupStarts;
    static constexpr float markerStripHeight = 12.0f;
    int warpIndex = -1;

    /** Index of the warp marker under x (in the bottom strip), or -1. */
    int warpMarkerAt (te::AudioClipBase& ac, Point<float> p) const
    {
        if (! ac.getWarpTime() || p.y < (float) getHeight() - markerStripHeight)
            return -1;

        auto& markers = ac.getWarpTimeManager().getMarkers();
        const auto x0 = evs.beatToX (ac.getStartBeat().inBeats());
        int best = -1;
        float bestDist = 6.0f;
        for (int i = 0; i < markers.size(); ++i)
        {
            auto x = evs.beatToX (Workflow::warpTimeToEditBeat (ac, markers[i]->warpTime)) - x0;
            if (std::abs (x - p.x) < bestDist) { bestDist = std::abs (x - p.x); best = i; }
        }
        return best;
    }

    // Fade handles sit just under the clip header; the right edge resizes; warp markers sit at the bottom.
    Mode modeAt (Point<float> p)
    {
        if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            warpIndex = warpMarkerAt (*ac, p);
            if (warpIndex >= 0)
                return Mode::warp;

            auto [inX, outX] = fadeXs (*ac);
            if (p.y >= 15.0f && p.y <= 30.0f)
            {
                if (std::abs (p.x - inX) <= 6.0f)  return Mode::fadeIn;
                if (std::abs (p.x - outX) <= 6.0f) return Mode::fadeOut;
            }
        }

        return p.x > (float) getWidth() - 7.0f ? Mode::resize : Mode::move;
    }

    std::pair<float, float> fadeXs (te::AudioClipBase& ac) const
    {
        const auto start = ac.getPosition().getStart();
        const auto x0 = evs.timeToX (start);
        auto inX = evs.timeToX (start + ac.getFadeIn()) - x0;
        auto outX = evs.timeToX (ac.getPosition().getEnd() - ac.getFadeOut()) - x0;
        return { jlimit (4.0f, (float) getWidth() - 4.0f, inX), jlimit (4.0f, (float) getWidth() - 4.0f, outX) };
    }

    void drawFadesAndGain (Graphics& g, te::AudioClipBase& ac, Rectangle<int> body, Colour col, Rectangle<float> header)
    {
        auto [inX, outX] = fadeXs (ac);
        const auto top = (float) body.getY(), bottom = (float) body.getBottom();

        g.setColour (Colours::black.withAlpha (0.35f));
        if (ac.getFadeIn() > te::TimeDuration())
        {
            Path p; p.addTriangle (0.0f, top, inX, top, 0.0f, bottom);
            g.fillPath (p);
            g.setColour (col.brighter (0.4f));
            g.drawLine (0.0f, bottom, inX, top, 1.2f);
            g.setColour (Colours::black.withAlpha (0.35f));
        }
        if (ac.getFadeOut() > te::TimeDuration())
        {
            const auto w = (float) getWidth();
            Path p; p.addTriangle (outX, top, w, top, w, bottom);
            g.fillPath (p);
            g.setColour (col.brighter (0.4f));
            g.drawLine (outX, top, w, bottom, 1.2f);
        }

        // handles
        g.setColour (Theme::ink.withAlpha (0.85f));
        g.fillRect (inX - 3.0f, top - 1.0f, 6.0f, 6.0f);
        g.fillRect (outX - 3.0f, top - 1.0f, 6.0f, 6.0f);

        // gain readout in the header
        if (std::abs (ac.getGainDB()) > 0.05f || ac.getIsReversed() || ac.getAutoTempo() || ac.isUsingARA() || std::abs (ac.getPitchChange()) > 0.01f)
        {
            String tags;
            if (std::abs (ac.getGainDB()) > 0.05f) tags << String (ac.getGainDB(), 1) << " dB  ";
            if (std::abs (ac.getPitchChange()) > 0.01f) tags << String (ac.getPitchChange(), 1) << " st  ";
            if (ac.getIsReversed()) tags << "REV  ";
            if (ac.getAutoTempo()) tags << "WARP  ";
            if (ac.isUsingARA()) tags << "ARA";
            g.setColour (Colours::black.withAlpha (0.8f));
            g.setFont (FontOptions (11.5f));
            g.drawText (tags.trim(), header.reduced (5.0f, 0.0f), Justification::centredRight, false);
        }
    }

    void drawMidi (Graphics& g, te::MidiClip& mc, Rectangle<int> area, Colour col)
    {
        auto& notes = mc.getSequence().getNotes();
        if (notes.isEmpty())
            return;

        int lo = 127, hi = 0;
        for (auto n : notes) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
        if (hi - lo < 12) { auto pad = (12 - (hi - lo)) / 2; lo -= pad; hi = lo + 12; }
        const float rowH = (float) area.getHeight() / (float) (hi - lo + 1);

        Workflow::MidiPlacement place (mc);
        const float clipX = evs.beatToX (place.clipStart);
        const auto ppb = (float) evs.pixelsPerBeat;

        g.setColour (col.brighter (0.2f));
        for (auto n : notes)
        {
            auto y = (float) area.getY() + (float) (hi - n->getNoteNumber()) * rowH;
            place.forEach (n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), [&] (double s, double len)
            {
                auto x1 = evs.beatToX (s) - clipX;
                g.fillRect (x1, y, jmax (1.5f, (float) len * ppb - 1.0f), jmax (1.5f, rowH - 0.5f));
            });
        }
    }

    /** A piece of the clip that plays a linear stretch of the source file. */
    struct WaveSegment { float x0, x1; double s0, s1; };

    /** Splits the clip into linear pieces (at loop wraps and warp markers) for drawing. */
    std::vector<WaveSegment> waveSegments (te::AudioClipBase& ac, std::vector<float>* loopXs = nullptr)
    {
        std::vector<WaveSegment> out;
        struct Piece { double p, c, len; };   // clip-relative pos, content pos, length
        std::vector<Piece> pieces;

        auto split = [&pieces, loopXs] (double off, double clipLen, bool looped, double loopStart, double loopLen,
                                        const std::function<float (double)>& toX)
        {
            if (! looped || loopLen <= 0.0)
            {
                pieces.push_back ({ 0.0, off, clipLen });
                return;
            }
            double p = 0.0, c = loopStart + std::fmod (jmax (0.0, off), loopLen);
            while (p < clipLen - 1.0e-9 && pieces.size() < 512)
            {
                auto len = jmin (loopStart + loopLen - c, clipLen - p);
                pieces.push_back ({ p, c, len });
                p += len;
                c = loopStart;
                if (loopXs != nullptr && p < clipLen) loopXs->push_back (toX (p));
            }
        };

        if (ac.getAutoTempo())
        {
            const double clipStart = ac.getStartBeat().inBeats();
            const auto x0 = evs.beatToX (clipStart);
            auto toX = [this, clipStart, x0] (double p) { return evs.beatToX (clipStart + p) - x0; };
            split (ac.getOffsetInBeats().inBeats(), ac.getLengthInBeats().inBeats(), ac.isLooping(),
                   ac.getLoopStartBeats().inBeats(), ac.getLoopLengthBeats().inBeats(), toX);

            const double spb = Workflow::sourceSecondsPerBeat (ac);
            std::vector<double> cuts;   // warp marker positions in content beats
            if (ac.getWarpTime())
                for (auto m : ac.getWarpTimeManager().getMarkers())
                    cuts.push_back (m->warpTime.inSeconds() / spb);

            auto toSource = [&ac, spb] (double c)
            {
                auto w = te::TimePosition::fromSeconds (c * spb);
                return ac.getWarpTime() ? ac.getWarpTimeManager().warpTimeToSourceTime (w).inSeconds() : w.inSeconds();
            };

            for (auto& pc : pieces)
            {
                std::vector<double> edges { pc.c };
                for (auto cut : cuts)
                    if (cut > pc.c && cut < pc.c + pc.len) edges.push_back (cut);
                edges.push_back (pc.c + pc.len);
                std::sort (edges.begin(), edges.end());

                for (size_t i = 0; i + 1 < edges.size(); ++i)
                {
                    auto a = edges[i], b = edges[i + 1];
                    out.push_back ({ toX (pc.p + a - pc.c), toX (pc.p + b - pc.c), toSource (a), toSource (b) });
                }
            }
        }
        else
        {
            auto pos = ac.getPosition();
            const auto start = pos.getStart();
            const auto x0 = evs.timeToX (start);
            auto toX = [this, start, x0] (double p) { return evs.timeToX (start + te::TimeDuration::fromSeconds (p)) - x0; };
            split (pos.getOffset().inSeconds(), pos.getLength().inSeconds(), ac.isLooping(),
                   ac.getLoopStart().inSeconds(), ac.getLoopLength().inSeconds(), toX);

            const double speed = ac.getSpeedRatio();
            for (auto& pc : pieces)
                out.push_back ({ toX (pc.p), toX (pc.p + pc.len), pc.c * speed, (pc.c + pc.len) * speed });
        }
        return out;
    }

    void drawWave (Graphics& g, Rectangle<int> area, Colour col)
    {
        auto* wac = dynamic_cast<te::WaveAudioClip*> (clip.get());
        if (wac == nullptr)
            return;

        if (thumbnail->isGeneratingProxy())
        {
            g.setColour (Theme::muted);
            g.drawText ("Building waveform...", area, Justification::centred);
            return;
        }

        g.setColour (col.brighter (0.3f));
        for (auto& seg : waveSegments (*wac))
        {
            auto x0 = (int) std::floor (seg.x0), x1 = (int) std::ceil (seg.x1);
            if (x1 <= 0 || x0 >= getWidth() || x1 <= x0 || seg.s1 <= seg.s0)
                continue;
            thumbnail->drawChannels (g, area.withX (x0).withWidth (x1 - x0),
                                     { te::TimePosition::fromSeconds (seg.s0), te::TimePosition::fromSeconds (seg.s1) }, wac->getGain());
        }
    }

    /** Dashed lines where a looped clip wraps around. */
    void drawLoopBoundaries (Graphics& g, Rectangle<int> body)
    {
        if (! clip->isLooping())
            return;

        std::vector<float> xs;
        if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            Workflow::MidiPlacement place (*mc);
            if (place.loopLength > 0.0)
            {
                double p = place.loopLength - std::fmod (place.offset, place.loopLength);
                for (; p < place.clipLength - 1.0e-6 && xs.size() < 512; p += place.loopLength)
                    xs.push_back (evs.beatToX (place.clipStart + p) - evs.beatToX (place.clipStart));
            }
        }
        else if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
        {
            waveSegments (*ac, &xs);
        }

        g.setColour (Theme::ink.withAlpha (0.45f));
        const float dash[] = { 3.0f, 3.0f };
        for (auto x : xs)
            g.drawDashedLine ({ x, (float) body.getY(), x, (float) body.getBottom() }, dash, 2, 1.0f);
    }

    void drawWarpMarkers (Graphics& g, te::AudioClipBase& ac, Rectangle<int> body)
    {
        const auto x0 = evs.beatToX (ac.getStartBeat().inBeats());
        const float bottom = (float) getHeight();
        g.setColour (Colours::black.withAlpha (0.3f));
        g.fillRect (0.0f, bottom - markerStripHeight, (float) getWidth(), markerStripHeight);

        // the hits in the audio, as small ticks to grab (for the selected clip: working them out reads its file)
        if (evs.selectionManager.isSelected (clip.get()))
        {
            auto& wm = ac.getWarpTimeManager();
            g.setColour (Colours::white.withAlpha (0.45f));
            for (auto t : Workflow::sourceTransients (ac))
            {
                const float x = evs.beatToX (Workflow::warpTimeToEditBeat (ac, wm.sourceTimeToWarpTime (t))) - x0;
                if (x >= 0.0f && x <= (float) getWidth()) g.fillRect (x - 0.5f, bottom - markerStripHeight + 2.0f, 1.5f, markerStripHeight - 4.0f);
            }
        }

        auto& markers = ac.getWarpTimeManager().getMarkers();
        for (int i = 0; i < markers.size(); ++i)
        {
            auto x = evs.beatToX (Workflow::warpTimeToEditBeat (ac, markers[i]->warpTime)) - x0;
            if (x < -6.0f || x > (float) getWidth() + 6.0f)
                continue;
            const bool active = (mode == Mode::warp && i == warpIndex && isMouseButtonDown());
            g.setColour (active ? Colours::white : Colour (0xfff5c542));
            g.drawVerticalLine ((int) x, (float) body.getY(), bottom - markerStripHeight);
            Path tri;
            tri.addTriangle (x - 5.0f, bottom - 1.0f, x + 5.0f, bottom - 1.0f, x, bottom - markerStripHeight + 1.0f);
            g.fillPath (tri);
        }
    }

    void showWarpMarkerMenu()
    {
        auto ac = dynamic_cast<te::AudioClipBase*> (clip.get());
        if (ac == nullptr || warpIndex < 0)
            return;

        PopupMenu m;
        m.addItem ("Delete warp marker", [c = clip, idx = warpIndex, safe = Component::SafePointer<Component> (this)]
        {
            if (auto a = dynamic_cast<te::AudioClipBase*> (c.get()))
            {
                a->edit.getUndoManager().beginNewTransaction ("Delete warp marker");
                a->getWarpTimeManager().removeMarker (idx);
                if (safe != nullptr) safe->repaint();
            }
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

    void addLoopItems (PopupMenu& m)
    {
        if (! Workflow::canLoop (*clip))
            return;

        auto c = clip;
        auto* um = &evs.edit.getUndoManager();
        const bool looping = clip->isLooping();
        m.addSeparator();
        m.addItem ("Loop clip", true, looping, [c, um]
        {
            um->beginNewTransaction ("Loop clip");
            Workflow::setLooping (*c, ! c->isLooping());
        });

        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        const double loopLen = Workflow::loopLengthBeats (*clip);
        PopupMenu loopLenMenu, clipLenMenu;
        for (auto beats : { 1.0, 2.0, (double) bpb, bpb * 2.0, bpb * 4.0, bpb * 8.0 })
        {
            auto name = beats < bpb ? String ((int) beats) + (beats == 1.0 ? " beat" : " beats")
                                    : String ((int) (beats / bpb)) + (beats == bpb ? " bar" : " bars");
            loopLenMenu.addItem (name, true, looping && std::abs (loopLen - beats) < 1.0e-6, [c, beats, um]
            {
                um->beginNewTransaction ("Loop length");
                Workflow::setLoopLengthBeats (*c, beats);
            });
        }
        for (int bars : { 1, 2, 4, 8, 16, 32 })
            clipLenMenu.addItem (String (bars) + (bars == 1 ? " bar" : " bars"), [c, beats = (double) bars * bpb, um]
            {
                um->beginNewTransaction ("Clip length");
                Workflow::setLengthBeats (*c, beats);
            });
        m.addSubMenu ("Loop length", loopLenMenu);
        m.addSubMenu ("Clip length (repeats the loop)", clipLenMenu);
    }

    /** Convert to MIDI, stem separation and ARA (Melodyne etc). */
    void addAudioToolItems (PopupMenu& m, te::WaveAudioClip& wac)
    {
        te::Clip::Ptr c = &wac;
        auto* sm = &evs.selectionManager;
        m.addSeparator();

        PopupMenu toMidi;
        const std::pair<AudioToMidi::Mode, const char*> modes[] = { { AudioToMidi::Mode::drums, "Drums (kick / snare / hi-hat)" },
                                                                    { AudioToMidi::Mode::melody, "Melody (monophonic)" },
                                                                    { AudioToMidi::Mode::harmony, "Harmony (chords)" } };
        for (auto [mode, name] : modes)
            toMidi.addItem (name, [c, mode, sm]
            {
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                {
                    w->edit.getUndoManager().beginNewTransaction ("Convert to MIDI");
                    int count = 0;
                    if (auto t = AudioToMidi::convertClip (*w, mode, &count))
                        sm->selectOnly (t);
                    if (count == 0)
                        AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Convert to MIDI", "No notes were detected in this clip.");
                }
            });
        m.addSubMenu ("Convert to new MIDI track", toMidi);

        m.addItem ("Edit audio (sample editor)...", [c]
        {
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get())) showSampleEditor (*w);
        });
        m.addItem ("Slice to Drum Rack (at transients)", [c, sm]
        {
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
            {
                w->edit.getUndoManager().beginNewTransaction ("Slice to Drum Rack");
                auto r = SampleEdit::sliceToDrumRack (*w);
                if (r.track != nullptr) sm->selectOnly (r.track);
                else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Slice", "Couldn't slice this clip.");
            }
        });

        PopupMenu warp;
        const auto current = SampleEdit::currentWarpMode (wac);
        for (auto& mode : SampleEdit::warpModes (wac.edit.engine))
            warp.addItem (mode, true, mode == current, [c, mode]
            {
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                {
                    w->edit.getUndoManager().beginNewTransaction ("Warp mode");
                    SampleEdit::setWarpMode (*w, mode);
                }
            });
        m.addSubMenu ("Warp mode (" + current + ")", warp);

        m.addItem (String ("Separate stems (Demucs)") + (Stems::isInstalled() ? "..." : " - set up..."), [c]
        {
            if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                Stems::separateClipInteractive (*w);
        });

        // ARA: Melodyne and other ARA plugins edit the clip's audio directly
        PopupMenu ara;
        auto araPlugins = wac.edit.engine.getPluginManager().getARACompatiblePlugDescriptions();
        const bool using_ = wac.isUsingARA();
        for (auto& d : araPlugins)
            ara.addItem ("Edit with " + d.name, true, using_ && wac.araPluginDescription.get().name == d.name, [c, d]
            {
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                {
                    w->edit.getUndoManager().beginNewTransaction ("Use ARA plugin");
                    w->araPluginDescription = d;
                    w->setTimeStretchMode (te::TimeStretcher::ara);
                    if (w->setupARA (false))
                        te::showARAWindow (*w);
                }
            });
        if (araPlugins.isEmpty())
            ara.addItem ("No ARA plugins found - install one (e.g. Melodyne), then rescan in Plugins...", false, false, nullptr);
        if (using_)
        {
            ara.addSeparator();
            ara.addItem ("Open " + wac.araPluginDescription.get().name + " editor", [c]
            {
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get())) te::showARAWindow (*w);
            });
            ara.addItem ("Stop using ARA", [c]
            {
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                {
                    w->edit.getUndoManager().beginNewTransaction ("Stop using ARA");
                    te::hideARAWindow (*w);
                    w->setTimeStretchMode (te::TimeStretcher::defaultMode);
                }
            });
        }
        m.addSubMenu (using_ ? "ARA (" + wac.araPluginDescription.get().name + ")" : String ("ARA (Melodyne...)"), ara);
    }

    void addWarpItems (PopupMenu& m, te::AudioClipBase& ac)
    {
        te::Clip::Ptr c = &ac;
        auto* um = &evs.edit.getUndoManager();
        Component::SafePointer<Component> safe (this);
        const bool on = ac.getWarpTime();
        auto withClip = [c] (auto fn) { return [c, fn] { if (auto a = dynamic_cast<te::AudioClipBase*> (c.get())) fn (*a); }; };
        auto repaintSelf = [safe] { if (safe != nullptr) safe->repaint(); };

        m.addItem ("Warp markers", true, on, withClip ([um, on] (te::AudioClipBase& a)
        {
            um->beginNewTransaction ("Warp markers");
            Workflow::enableWarpMarkers (a, ! on);
        }));

        if (! on)
            return;

        m.addItem ("Add warp marker at playhead", withClip ([um, repaintSelf] (te::AudioClipBase& a)
        {
            um->beginNewTransaction ("Add warp marker");
            Workflow::addWarpMarkerAtBeat (a, EngineHelpers::timeToBeat (a.edit, a.edit.getTransport().getPosition()));
            repaintSelf();
        }));
        m.addItem ("Add warp markers at transients", withClip ([um, repaintSelf] (te::AudioClipBase& a)
        {
            um->beginNewTransaction ("Warp markers at transients");
            if (Workflow::addWarpMarkersAtTransients (a) < 0)
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Warp markers",
                                                  "Transients are still being analysed. Try again in a moment.");
            repaintSelf();
        }));
        m.addItem ("Clear warp markers", withClip ([um, repaintSelf] (te::AudioClipBase& a)
        {
            um->beginNewTransaction ("Clear warp markers");
            a.getWarpTimeManager().removeAllMarkers();
            repaintSelf();
        }));

        PopupMenu q;
        for (auto [label, grid, strength] : { std::tuple<const char*, double, double> { "1/16, 100%", 0.25, 1.0 }, { "1/16, 50% (keeps some feel)", 0.25, 0.5 },
                                              { "1/8, 100%", 0.5, 1.0 }, { "1/4, 100%", 1.0, 1.0 } })
            q.addItem (label, withClip ([um, repaintSelf, grid = grid, strength = strength] (te::AudioClipBase& a)
            {
                um->beginNewTransaction ("Quantize audio");
                if (Workflow::quantizeAudio (a, grid, strength) < 0)
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Quantize audio",
                                                      "Transients are still being analysed. Try again in a moment.");
                repaintSelf();
            }));
        m.addSubMenu ("Quantize audio (transients to grid)", q);
    }

    void showContextMenu()
    {
        PopupMenu m;
        auto* sm = &evs.selectionManager;
        auto& edit = evs.edit;
        m.addItem (PopupMenu::Item ("Cut").setAction ([sm, &edit] { edit.getUndoManager().beginNewTransaction ("Cut"); EditingOps::cut (*sm); }));
        m.addItem (PopupMenu::Item ("Copy").setAction ([sm] { EditingOps::copy (*sm); }));
        m.addItem (PopupMenu::Item ("Duplicate").setAction ([sm, &edit] { edit.getUndoManager().beginNewTransaction ("Duplicate"); EditingOps::duplicate (*sm); }));
        m.addItem (PopupMenu::Item ("Split at playhead").setAction ([sm, &edit] { edit.getUndoManager().beginNewTransaction ("Split"); EditingOps::splitAtPlayhead (edit, *sm); }));
        m.addItem (PopupMenu::Item ("Bounce selection to new track").setAction ([sm]
        {
            if (auto t = EngineHelpers::getSelectedTrack (*sm))
                EditingOps::bounceToNewTrack (*t, EditingOps::rangeOf (EditingOps::selectedClips (*sm)));
        }));
        {
            const auto sel = EditingOps::selectedClips (*sm);
            const auto loop = edit.getTransport().getLoopRange();
            bool reaches = false;
            for (auto c : sel) reaches = reaches || c->getEditTimeRange().overlaps (loop);
            m.addItem ("Crop to the loop range", ! loop.isEmpty() && reaches, false, [sm, &edit, loop]
            {
                edit.getUndoManager().beginNewTransaction ("Crop clips");
                EditingOps::cropToRange (EditingOps::selectedClips (*sm), loop);
            });
            m.addItem ("Consolidate into one clip", EditingOps::canConsolidate (sel), false, [sm, &edit]
            {
                edit.getUndoManager().beginNewTransaction ("Consolidate clips");
                EditingOps::consolidate (EditingOps::selectedClips (*sm), sm);
            });
        }
        m.addItem (PopupMenu::Item ("Delete").setAction ([sm, &edit] { edit.getUndoManager().beginNewTransaction ("Delete clips"); EditingOps::deleteSelected (*sm); }));

        // takes (loop-recorded MIDI takes, or audio takes combined from several passes)
        if (const int n = Timeline::numTakes (*clip); n > 0)
        {
            PopupMenu takes;
            for (int i = 0; i < n; ++i)
                takes.addItem ("Take " + String (i + 1), true, Timeline::currentTake (*clip) == i, [c = clip, i]
                {
                    c->edit.getUndoManager().beginNewTransaction ("Choose take");
                    Timeline::setTake (*c, i);
                });
            takes.addSeparator();
            takes.addItem ("Show take lanes for comping", [this]
            {
                if (auto t = clip->getTrack()) { evs.takeLanes.insert (t->itemID); evs.layoutChanged.sendChangeMessage(); }
            });
            m.addSubMenu ("Takes", takes);
        }
        {
            int audioSelected = 0;
            for (auto c : EditingOps::selectedClips (*sm)) audioSelected += dynamic_cast<te::WaveAudioClip*> (c) != nullptr;
            if (audioSelected >= 2)
                m.addItem ("Combine " + String (audioSelected) + " selected audio clips into takes", [sm, &edit]
                {
                    edit.getUndoManager().beginNewTransaction ("Combine into takes");
                    auto clips = EditingOps::selectedClips (*sm);
                    sm->deselectAll();
                    if (auto t = Timeline::combineIntoTakes (clips))
                        sm->selectOnly (t);
                });
        }
        if (auto mc = dynamic_cast<te::MidiClip*> (clip.get()))
        {
            m.addItem ("Open in Piano Roll", [this, mc] { if (evs.openPianoRoll) evs.openPianoRoll (mc); });
            Groove::addToMenu (m, *mc);
        }
        m.addItem ("Extract groove (to the groove list)", [c = clip]
        {
            const auto name = Groove::extractGroove (*c);
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Extract groove",
                                              name.isNotEmpty() ? "Added the groove \"" + name + "\". Apply it to a MIDI clip from its Groove menu."
                                                                : String ("Not enough hits in this clip to find a groove."));
        });

        addLoopItems (m);

        if (auto wac = dynamic_cast<te::WaveAudioClip*> (clip.get()))
        {
            m.addSeparator();
            m.addItem ("Clip properties...", [this, wac] { showAudioClipPanel (*wac, *this); });
            addWarpItems (m, *wac);
            addAudioToolItems (m, *wac);
            m.addItem ("Reverse", true, wac->getIsReversed(), [wac] { wac->setIsReversed (! wac->getIsReversed()); });
            m.addItem ("Remove fades", [wac] { wac->setFadeIn ({}); wac->setFadeOut ({}); });
            m.addItem ("Reset gain", [wac] { wac->setGainDB (0.0f); });
        }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

    EditViewState& evs;
    te::Clip::Ptr clip;
    std::function<te::AudioTrack* (int)> findTrackAt;
    std::unique_ptr<te::SmartThumbnail> thumbnail;
    double dragStartBeat = 0, dragEndBeat = 0;
    te::TimeDuration dragFadeIn, dragFadeOut, dragOffset;
    Mode mode = Mode::move;
};

//==============================================================================
class TrackLane : public Component,
                  public FileDragAndDropTarget,
                  public DragAndDropTarget,
                  private te::ValueTreeAllEventListener,
                  private FlaggedAsyncUpdater
{
public:
    TrackLane (EditViewState& e, te::AudioTrack::Ptr t, std::function<te::AudioTrack* (int)> finder)
        : evs (e), track (t), findTrackAt (std::move (finder))
    {
        track->state.addListener (this);
        markAndUpdate (updateClips);
    }

    ~TrackLane() override { track->state.removeListener (this); }

    te::AudioTrack& getTrack() { return *track; }
    const OwnedArray<ClipComponent>& getClipComponents() const { return clips; }

    /** Called while dragging on empty lane space: (rectangle in this lane's coords, finished). */
    std::function<void (const MouseEvent&, bool)> onRubberBand;

    void paint (Graphics& g) override
    {
        const bool selected = evs.selectionManager.isSelected (track.get());
        g.fillAll (selected ? Theme::panel2 : Theme::header);
        if (track->isFrozen (te::Track::anyFreeze))
            g.fillAll (Colour (0x183d8bfd));
        drawBeatGrid (g, evs, getLocalBounds());

        auto& tc = evs.edit.getTransport();
        if (tc.looping)
        {
            auto lr = tc.getLoopRange();
            auto x1 = evs.timeToX (lr.getStart()), x2 = evs.timeToX (lr.getEnd());
            g.setColour (Theme::orange.withAlpha (0.05f));
            g.fillRect (x1, 0.0f, x2 - x1, (float) getHeight());
        }

        g.setColour (Theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override
    {
        const float clipH = (float) (getHeight() - evs.takeRows (*track) * EditViewState::takeRowHeight);
        for (auto* cc : clips)
        {
            auto pos = cc->getClip().getPosition();
            auto x1 = evs.timeToX (pos.getStart()), x2 = evs.timeToX (pos.getEnd());
            cc->setBounds (Rectangle<float> (x1, 3.0f, jmax (4.0f, x2 - x1), clipH - 6.0f).toNearestInt());
        }
    }

    // Take lanes: one row per take under the clips. Drag across a take to use it for that range.
    int takeRowAt (int y) const
    {
        const int rows = evs.takeRows (*track);
        const int top = getHeight() - rows * EditViewState::takeRowHeight;
        return (rows > 0 && y >= top) ? (y - top) / EditViewState::takeRowHeight : -1;
    }

    void paintOverChildren (Graphics& g) override
    {
        paintDropHighlight (g);
        const int rows = evs.takeRows (*track);
        if (rows == 0) return;

        const int top = getHeight() - rows * EditViewState::takeRowHeight;
        auto col = track->getColour();
        for (int i = 0; i < rows; ++i)
        {
            auto rowArea = Rectangle<int> (0, top + i * EditViewState::takeRowHeight, getWidth(), EditViewState::takeRowHeight);
            g.setColour (i % 2 ? Theme::header : Theme::raised);
            g.fillRect (rowArea);
            for (auto c : track->getClips())
            {
                if (Timeline::numTakes (*c) <= i) continue;
                auto r = c->getEditTimeRange();
                auto x1 = evs.timeToX (r.getStart()), x2 = evs.timeToX (r.getEnd());
                const bool active = Timeline::currentTake (*c) == i;
                g.setColour (col.withAlpha (active ? 0.85f : 0.25f));
                g.fillRoundedRectangle (Rectangle<float> (x1 + 1, (float) rowArea.getY() + 2, x2 - x1 - 2, (float) rowArea.getHeight() - 4), 3.0f);
                g.setColour (active ? Colours::black : Theme::muted);
                g.setFont (FontOptions (11.0f));
                g.drawText ("Take " + String (i + 1), Rectangle<float> (x1 + 4, (float) rowArea.getY(), x2 - x1 - 6, (float) rowArea.getHeight()),
                            Justification::centredLeft, true);
            }
        }
        if (compRow >= 0)
        {
            const float a = (float) jmin (compX0, compX1), b = (float) jmax (compX0, compX1);
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.fillRect (a, (float) (top + compRow * EditViewState::takeRowHeight), b - a, (float) EditViewState::takeRowHeight);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isMiddleButtonDown())   // middle-drag: pan the view
        {
            panning = true;
            panStartBeat = evs.viewStartBeat;
            setMouseCursor (MouseCursor::DraggingHandCursor);
            return;
        }
        evs.selectionManager.selectOnly (track.get());

        compRow = e.mods.isPopupMenu() ? -1 : takeRowAt (e.y);
        if (compRow >= 0)
        {
            compX0 = compX1 = e.x;
            return;
        }

        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem ("Insert MIDI clip here", [this, x = e.x] { createMidiClipAt ((float) x); });
            m.addItem ("Import audio here...", [this, x = e.x] { browseForAudio ((float) x); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }
    }

    // Dragging on empty space draws a selection rectangle across lanes.
    bool panning = false;
    double panStartBeat = 0;

    void mouseDrag (const MouseEvent& e) override
    {
        if (panning)
        {
            evs.viewStartBeat = jmax (0.0, panStartBeat - e.getDistanceFromDragStartX() / evs.pixelsPerBeat);
            evs.viewChanged.sendChangeMessage();
            return;
        }
        if (compRow >= 0) { compX1 = e.x; repaint(); return; }
        if (! e.mods.isPopupMenu() && e.getDistanceFromDragStart() > 4 && onRubberBand)
            onRubberBand (e, false);
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (panning) { panning = false; setMouseCursor (MouseCursor::NormalCursor); return; }
        if (compRow >= 0)
        {
            // a click picks the take for the whole clip, a drag picks it for that range (snapped)
            auto t0 = evs.xToTime ((float) jmin (compX0, compX1)), t1 = evs.xToTime ((float) jmax (compX0, compX1));
            if (std::abs (compX1 - compX0) < 4)
            {
                if (auto c = Timeline::clipAt (*track, t0)) { t0 = c->getEditTimeRange().getStart(); t1 = c->getEditTimeRange().getEnd(); }
            }
            else
            {
                t0 = EngineHelpers::beatToTime (evs.edit, evs.snap (EngineHelpers::timeToBeat (evs.edit, t0)));
                t1 = EngineHelpers::beatToTime (evs.edit, evs.snap (EngineHelpers::timeToBeat (evs.edit, t1)));
            }
            if (t1 > t0)
            {
                evs.edit.getUndoManager().beginNewTransaction ("Comp take");
                Timeline::compTake (*track, { t0, t1 }, compRow);
            }
            compRow = -1;
            repaint();
            return;
        }
        if (e.getDistanceFromDragStart() > 4 && onRubberBand)
            onRubberBand (e, true);
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (EngineHelpers::trackHasInstrument (*track))
            createMidiClipAt ((float) e.x);
        else
            browseForAudio ((float) e.x);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
            evs.zoom (std::exp (w.deltaY * 1.5), (float) e.x);
        else if (e.mods.isShiftDown() || e.mods.isAltDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
            evs.scrollBy (-((e.mods.isShiftDown() || e.mods.isAltDown()) ? w.deltaY : w.deltaX) * 400.0 / evs.pixelsPerBeat);
        else
            Component::mouseWheelMove (e, w);
    }

    // File drag & drop from Explorer --------------------------------------------
    bool isInterestedInFileDrag (const StringArray& files) override
    {
        for (auto& f : files)
            if (ClipImport::isImportable (File (f)))
                return true;

        return false;
    }

    void filesDropped (const StringArray& files, int x, int) override
    {
        insertAudioFiles (files, (float) x);
        setDropHighlight (false);
    }

    void fileDragEnter (const StringArray&, int, int) override { setDropHighlight (true); }
    void fileDragExit (const StringArray&) override              { setDropHighlight (false); }

    // Drag & drop from the browser (files and devices) -------------------------------
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return ClipImport::isPluginDescription (d.description) || d.description == "PulseFiles";
    }

    void itemDragEnter (const SourceDetails&) override { setDropHighlight (true); }
    void itemDragExit (const SourceDetails&) override  { setDropHighlight (false); }

    void itemDropped (const SourceDetails& d) override
    {
        setDropHighlight (false);

        if (Workflow::isTrackPreset (d.description.toString()))
        {
            evs.edit.getUndoManager().beginNewTransaction ("Load track preset");
            if (auto t = Workflow::loadTrackPreset (evs.edit, Workflow::trackPresetFile (d.description.toString())))
                evs.selectionManager.selectOnly (t);
            return;
        }

        if (ClipImport::isPluginDescription (d.description))
        {
            if (Presets::applyDescription (track.get(), track->pluginList, d.description.toString()))
                evs.selectionManager.selectOnly (track.get());
            return;
        }

        StringArray paths;
        for (auto& f : ClipImport::filesFromDrag (d)) paths.add (f.getFullPathName());
        insertAudioFiles (paths, (float) d.localPosition.x);
    }

    void paintDropHighlight (Graphics& g)
    {
        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.12f));
            g.fillAll();
            g.setColour (Theme::accent);
            g.drawRect (getLocalBounds(), 2);
        }
    }

private:
    void createMidiClipAt (float x)
    {
        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        auto startBeat = std::floor (evs.xToBeat (x) / bpb) * bpb;
        auto range = te::TimeRange (EngineHelpers::beatToTime (evs.edit, startBeat),
                                    EngineHelpers::beatToTime (evs.edit, startBeat + bpb));

        if (auto mc = track->insertMIDIClip (track->getName(), range, &evs.selectionManager))
            if (evs.openPianoRoll)
                evs.openPianoRoll (mc.get());
    }

    void browseForAudio (float x)
    {
        auto beat = jmax (0.0, evs.snapFloor (evs.xToBeat (x)));
        chooser = std::make_shared<FileChooser> ("Import audio", File(), evs.edit.engine.getAudioFileFormatManager().readFormatManager.getWildcardForAllFormats(), ! About::isCapturing());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this, beat, sp = SafePointer<TrackLane> (this)] (const FileChooser& fc)
                              {
                                  if (sp == nullptr) return;
                                  StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  insertAudioFiles (paths, evs.beatToX (beat));
                              });
    }

    void insertAudioFiles (const StringArray& paths, float x)
    {
        auto start = EngineHelpers::beatToTime (evs.edit, jmax (0.0, evs.snapFloor (evs.xToBeat (x))));
        Array<File> files;
        for (auto& p : paths) files.add (File (p));
        evs.edit.getUndoManager().beginNewTransaction ("Import files");
        // audio on an instrument track would never be heard: it gets its own audio track
        Array<File> here, ownTrack;
        for (auto& f : files) (ClipImport::needsOwnAudioTrack (*track, f) ? ownTrack : here).add (f);
        if (! here.isEmpty()) ClipImport::importFiles (*track, evs.edit, here, start);
        for (auto& f : ownTrack)
            if (auto t = ClipImport::createContentTrack (evs.edit, f.getFileNameWithoutExtension(), false))
            {
                ClipImport::importFile (*t, evs.edit, f, start);
                evs.selectionManager.selectOnly (t);
            }
    }

    void setDropHighlight (bool b)
    {
        if (dropHighlight != b) { dropHighlight = b; repaint(); }
    }

    bool dropHighlight = false;
    int compRow = -1, compX0 = 0, compX1 = 0, lastTakeRows = 0;

    void valueTreeChanged() override {}

    void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
    {
        if (te::Clip::isClipState (v) && (id == te::IDs::start || id == te::IDs::length || id == te::IDs::offset))
            markAndUpdate (updatePositions);
        else
            markAndUpdate (repaintClips); // e.g. note edits inside a clip, colour, name

        if (id == te::IDs::colour)
            repaint();
    }

    void valueTreeChildAdded (ValueTree&, ValueTree& c) override
    {
        markAndUpdate (te::Clip::isClipState (c) ? updateClips : repaintClips);
    }

    void valueTreeChildRemoved (ValueTree&, ValueTree& c, int) override
    {
        markAndUpdate (te::Clip::isClipState (c) ? updateClips : repaintClips);
    }

    void valueTreeChildOrderChanged (ValueTree&, int, int) override { markAndUpdate (updatePositions); }

    void handleAsyncUpdate() override
    {
        if (compareAndReset (updateClips))
        {
            buildClips();
            if (const int rows = evs.takeRows (*track); rows != lastTakeRows)
            {
                lastTakeRows = rows;
                evs.layoutChanged.sendChangeMessage();
            }
        }

        if (compareAndReset (updatePositions))
        {
            resized();
            repaintClips = true;
        }

        if (compareAndReset (repaintClips))
            for (auto* c : clips) c->repaint();
    }

    void buildClips()
    {
        clips.clear();

        for (auto c : track->getClips())
        {
            auto* cc = clips.add (new ClipComponent (evs, c, findTrackAt));
            addAndMakeVisible (cc);
        }

        resized();
    }

    EditViewState& evs;
    te::AudioTrack::Ptr track;
    std::function<te::AudioTrack* (int)> findTrackAt;
    OwnedArray<ClipComponent> clips;
    std::shared_ptr<FileChooser> chooser;
    bool updateClips = false, updatePositions = false, repaintClips = false;
};

//==============================================================================
class TrackHeader : public Component,
                    public DragAndDropTarget,
                    public TooltipClient,
                    private Timer
{
public:
    TrackHeader (EditViewState& e, te::AudioTrack::Ptr t) : evs (e), track (t)
    {
        name.setText (track->getName(), dontSendNotification);
        name.setEditable (false, true);
        name.setFont (FontOptions (14.0f, Font::bold));
        name.onTextChange = [this] { track->setName (name.getText()); track->state.removeProperty (ClipImport::autoNameID, nullptr); };
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        type.setFont (FontOptions (11.5f));
        type.setColour (Label::textColourId, Theme::muted);
        type.setInterceptsMouseClicks (false, false);   // the header handles clicks on it (opens the instrument)
        type.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (type);

        for (auto* b : { &muteBtn, &soloBtn, &armBtn, &inBtn, &autoBtn })
        {
            b->setClickingTogglesState (false);
            addAndMakeVisible (b);
        }
        autoBtn.setColour (TextButton::buttonOnColourId, Theme::orange);
        autoBtn.setTooltip ("Show / hide the automation lane");
        autoBtn.onClick = [this] { evs.setAutomationVisible (*track, ! evs.isAutomationVisible (*track)); };
        muteBtn.setColour (TextButton::buttonOnColourId, Theme::yellow);
        soloBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
        armBtn.setColour (TextButton::buttonOnColourId, Theme::red);
        armBtn.setColour (TextButton::textColourOffId, Theme::red);
        muteBtn.setTooltip ("Mute");
        soloBtn.setTooltip ("Solo");
        armBtn.setTooltip ("Arm for recording");
        inBtn.setTooltip ("Input routing / monitoring");

        muteBtn.onClick = [this] { EngineHelpers::toggleMute (*track); };
        soloBtn.onClick = [this] { EngineHelpers::toggleSolo (*track); };
        armBtn.onClick = [this] { toggleArm(); };
        inBtn.onClick = [this] { showInputMenu(); };

        volume.setSliderStyle (Slider::LinearHorizontal);
        volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTooltip ("Volume (double-click: 0 dB)");
        if (auto lm = track->getLevelMeterPlugin())
        {
            meter = std::make_unique<LevelMeter> (*lm, true);
            addAndMakeVisible (*meter);
        }
        volume.onValueChange = [this]
        {
            if (auto vp = track->getVolumePlugin())
                vp->setVolumeDb ((float) volume.getValue());
        };
        addAndMakeVisible (volume);

        refresh();
        startTimerHz (6);
    }

    void paint (Graphics& g) override
    {
        const bool selected = evs.selectionManager.isSelected (track.get());
        const auto col = track->getColour();
        g.fillAll (selected ? Theme::line : Theme::panel);
        g.setGradientFill (ColourGradient (col.withAlpha (selected ? 0.22f : 0.12f), 0.0f, 0.0f,
                                          col.withAlpha (0.0f), (float) getWidth() * 0.7f, 0.0f, false));
        g.fillRect (getLocalBounds());
        // group indentation: one coloured bar per enclosing folder
        int x = 0;
        for (auto f = track->getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
        {
            g.setColour (f->getColour().withAlpha (0.5f));
            g.fillRect (x, 0, 3, getHeight());
            x += indentPerLevel;
        }
        g.setColour (col);
        g.fillRect (x, 0, 6, getHeight());
        // track number badge
        const int number = EngineHelpers::getUserTracks (evs.edit).indexOf (track.get()) + 1;
        auto badge = numberArea.toFloat();
        g.setColour (col);
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (TrackColours::textOn (col));
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (String (number), badge, Justification::centred);
        if (Notes::has (*track))   // a folded corner: this track has notes
        {
            Path corner;
            corner.addTriangle (0.0f, 0.0f, 11.0f, 0.0f, 0.0f, 11.0f);
            g.setColour (Theme::yellow);
            g.fillPath (corner);
        }
        g.setColour (Theme::well);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    int depth() const
    {
        int d = 0;
        for (auto f = track->getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack()) ++d;
        return d;
    }

    static constexpr int indentPerLevel = 12;

    void resized() override
    {
        const bool compact = getHeight() < 58;
        auto r = getLocalBounds().withTrimmedLeft (10 + depth() * indentPerLevel).reduced (4, compact ? 3 : 6);
        if (meter != nullptr)
        {
            meter->setBounds (r.removeFromRight (8));
            r.removeFromRight (6);
        }
        for (auto* c : { (Component*) &type, (Component*) &armBtn, (Component*) &inBtn, (Component*) &autoBtn, (Component*) &volume })
            c->setVisible (! compact);
        if (compact)
        {
            auto top = r.removeFromTop (jmin (22, r.getHeight()));
            numberArea = top.removeFromLeft (20).reduced (0, 1);
            top.removeFromLeft (5);
            soloBtn.setBounds (top.removeFromRight (22).reduced (0, 1));
            top.removeFromRight (3);
            muteBtn.setBounds (top.removeFromRight (22).reduced (0, 1));
            top.removeFromRight (4);
            name.setBounds (top);
            return;
        }
        auto top = r.removeFromTop (20);
        numberArea = top.removeFromLeft (20).reduced (0, 1);
        top.removeFromLeft (5);
        name.setBounds (top);
        type.setBounds (r.removeFromTop (16));
        r.removeFromTop (4);
        auto row = r.removeFromTop (20);
        for (auto* b : { &muteBtn, &soloBtn, &armBtn, &inBtn, &autoBtn })
        {
            b->setBounds (row.removeFromLeft (22));
            row.removeFromLeft (3);
        }
        volume.setBounds (row.reduced (2, 0));
    }

    bool onResizeEdge (int y) const { return y >= getHeight() - 5; }

    te::Plugin* instrument() const
    {
        for (auto p : track->pluginList)
            if (p->isSynth() || dynamic_cast<te::RackInstance*> (p) != nullptr) return p;
        return nullptr;
    }

    bool overInstrumentLabel (juce::Point<int> p) const { return type.isVisible() && type.getBounds().contains (p) && instrument() != nullptr; }

    /** The corner mark of a track that has notes: hovering shows them, a click opens them. */
    bool overNoteMark (juce::Point<int> p) const { return Notes::has (*track) && p.x < 16 && p.y < 16; }
    String getTooltip() override { return overNoteMark (getMouseXYRelative()) ? "Notes:\n" + Notes::summary (Notes::of (*track)) : String(); }

    void mouseMove (const MouseEvent& e) override
    {
        setMouseCursor (onResizeEdge (e.y) ? MouseCursor::UpDownResizeCursor
                                           : overInstrumentLabel (e.getPosition()) || overNoteMark (e.getPosition()) ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
        type.setColour (Label::textColourId, overInstrumentLabel (e.getPosition()) ? Theme::accent : Theme::muted);
    }
    void mouseExit (const MouseEvent&) override { type.setColour (Label::textColourId, Theme::muted); }

    void mouseDown (const MouseEvent& e) override
    {
        auto& sm = evs.selectionManager;
        if (! e.mods.isPopupMenu() && overNoteMark (e.getPosition())) { Notes::show (evs.edit, track.get()); return; }
        resizing = ! e.mods.isPopupMenu() && onResizeEdge (e.y);
        if (resizing) { resizeStart = evs.baseHeight (*track); return; }
        if (! e.mods.isPopupMenu() && overInstrumentLabel (e.getPosition()))   // click the instrument name: open it
        {
            sm.selectOnly (track.get());
            if (auto inst = instrument())
            {
                if (dynamic_cast<te::RackInstance*> (inst) != nullptr) { if (evs.showDevices) evs.showDevices (track.get()); }
                else if (evs.openDeviceEditor) evs.openDeviceEditor (*inst);
            }
            return;
        }

        // click the colour strip or the number: colour palette (for every selected track)
        if (! e.mods.isPopupMenu() && (e.x < 10 + depth() * indentPerLevel || numberArea.contains (e.getPosition())))
        {
            if (! sm.isSelected (track.get())) sm.selectOnly (track.get());
            showColours();
            return;
        }

        // Ctrl-click: multi-select tracks (e.g. to group them)
        if (e.mods.isCtrlDown() && ! e.mods.isPopupMenu())
        {
            if (sm.isSelected (track.get())) sm.deselect (track.get());
            else                             sm.addToSelection (track.get());
            return;
        }

        if (! (e.mods.isPopupMenu() && sm.isSelected (track.get())))
            sm.selectOnly (track.get());
        EngineHelpers::routeMidiInputsTo (*track);

        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem ("Rename", [this] { name.showEditor(); });
            {
                PopupMenu duck;
                for (auto other : EngineHelpers::getUserTracks (evs.edit))
                    if (other != track.get())
                        duck.addItem (other->getName(), [t = track, src = te::Track::Ptr (other)]
                        {
                            t->edit.getUndoManager().beginNewTransaction ("Sidechain ducking");
                            auto p = t->edit.getPluginCache().createNewPlugin (pulse::GlueCompressor::xmlTypeName, {});
                            PluginMenu::insertPlugin (t.get(), t->pluginList, p, nullptr);
                            if (auto g = dynamic_cast<pulse::GlueCompressor*> (p.get()))
                            {
                                g->set (pulse::GlueCompressor::pThreshold, -28.0f);
                                g->set (pulse::GlueCompressor::pRatio, 2.0f);       // 10:1
                                g->set (pulse::GlueCompressor::pAttack, 1.0f);      // 0.1 ms
                                g->set (pulse::GlueCompressor::pRelease, 1.0f);     // 0.2 s
                                g->set (pulse::GlueCompressor::pScFilter, 120.0f);
                                g->setSidechainSourceID (src->itemID);
                                g->guessSidechainRouting();   // wires the key track into the sidechain inputs
                            }
                        });
                m.addSubMenu ("Duck this track from (sidechain)", duck);
            }
            m.addItem ("Add plugin...", [this] { PluginMenu::show (evs.edit, this, [t = track, sm = &evs.selectionManager] (te::Plugin::Ptr p)
                                                  { PluginMenu::insertPlugin (t.get(), t->pluginList, p, sm); }); });
            m.addItem ("Colour...", [this] { showColours(); });
            m.addSeparator();

            auto selectedTracks = sm.getItemsOfType<te::AudioTrack>();
            m.addItem ("Group " + String (selectedTracks.size()) + " selected track(s)  (Ctrl+G)", [this, selectedTracks]
            {
                evs.edit.getUndoManager().beginNewTransaction ("Group tracks");
                EditingOps::groupTracks (evs.edit, selectedTracks, "Group");
            });
            if (auto folder = track->getParentFolderTrack())
                m.addItem ("Ungroup \"" + folder->getName() + "\"", [folder] { EditingOps::ungroup (*folder); });

            m.addItem (Notes::has (*track) ? "Notes for this track..." : "Write a note for this track...", [this] { Notes::show (evs.edit, track.get()); });
            m.addSeparator();
            const bool frozen = track->isFrozen (te::Track::individualFreeze);
            m.addItem (frozen ? "Unfreeze track" : "Freeze track", [t = track] { EditingOps::toggleFreeze (*t); });
            m.addItem ("Flatten (replace with audio)", [t = track]
            {
                t->edit.getUndoManager().beginNewTransaction ("Flatten track");
                EditingOps::flatten (*t);
            });
            m.addItem ("Bounce to new audio track", [t = track] { EditingOps::bounceToNewTrack (*t); });
            {
                bool anyTakes = false;
                for (auto c : track->getClips()) anyTakes = anyTakes || Timeline::numTakes (*c) > 0;
                const bool shown = evs.takeLanes.count (track->itemID) > 0;
                m.addItem ("Show take lanes (comping)", anyTakes || shown, shown, [this]
                {
                    if (evs.takeLanes.count (track->itemID)) evs.takeLanes.erase (track->itemID);
                    else                                      evs.takeLanes.insert (track->itemID);
                    evs.layoutChanged.sendChangeMessage();
                });
            }
            m.addSeparator();
            m.addItem ("Save track preset...", [t = track]
            {
                Presets::askForName ("Save track preset", t->getName(), [t] (const String& name)
                {
                    if (auto at = dynamic_cast<te::AudioTrack*> (t.get()))
                        if (! Workflow::saveTrackPreset (*at, name))
                            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Track preset", "Couldn't save the preset.");
                });
            });
            m.addItem ("Export MIDI clips...", [t = track]
            {
                if (auto at = dynamic_cast<te::AudioTrack*> (t.get()))
                {
                    Array<te::MidiClip*> clips;
                    for (auto c : at->getClips())
                        if (auto mc = dynamic_cast<te::MidiClip*> (c)) clips.add (mc);
                    Workflow::chooseAndExportMidi (at->edit, clips, at->getName());
                }
            });
            m.addSeparator();
            m.addItem ("Delete track", [this] { evs.edit.deleteTrack (track.get()); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }
    }

    void mouseDoubleClick (const MouseEvent& e) override   // double-click the bottom edge: back to the default height
    {
        if (onResizeEdge (e.y)) { track->state.removeProperty (EditViewState::laneHeightID, nullptr); evs.heightsChanged.sendChangeMessage(); return; }
        name.showEditor();
    }

    // drag the header to reorder tracks
    void mouseDrag (const MouseEvent& e) override
    {
        if (resizing)
        {
            evs.setBaseHeight (*track, resizeStart + e.getDistanceFromDragStartY());
            return;
        }
        if (dragStarted || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8) return;
        dragStarted = true;
        DragDrop::start (*this, DragDrop::trackPrefix + track->itemID.toString(), track->getName(), track->getColour());
    }
    void mouseUp (const MouseEvent&) override { dragStarted = false; resizing = false; }


    // devices dropped on the header go to the end of this track's chain
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return DragDrop::is (d.description, DragDrop::devicePrefix)
               || (ClipImport::isPluginDescription (d.description) && ! Workflow::isTrackPreset (d.description.toString()));
    }
    void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        dropHover = false;
        repaint();
        if (DragDrop::is (d.description, DragDrop::devicePrefix))
        {
            if (auto p = DragDrop::pluginFrom (evs.edit, d.description))
                DragDrop::placeDevice (*p, track.get(), track->pluginList, nullptr, ModifierKeys::currentModifiers.isCtrlDown());
        }
        else
        {
            evs.edit.getUndoManager().beginNewTransaction ("Add device");
            Presets::applyDescription (track.get(), track->pluginList, d.description.toString());
        }
        evs.selectionManager.selectOnly (track.get());
    }

    void paintOverChildren (Graphics& g) override
    {
        if (inputClipped)
        {
            g.setColour (Theme::red);
            g.drawRect (getLocalBounds(), 2);
            g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText ("INPUT CLIPPING", getLocalBounds().reduced (6, 3).removeFromBottom (12), Justification::centredRight);
        }
        if (! dropHover) return;
        g.setColour (Theme::accent.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (Theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }

    te::AudioTrack* getTrack() const { return track.get(); }

    /** While the track is armed, the meter shows what comes in (with a clip warning). */
    void updateInputMeter()
    {
        auto& dm = track->edit.engine.getDeviceManager();
        auto exists = [&dm] (te::InputDevice* d)
        {
            for (int i = 0; i < dm.getNumWaveInDevices(); ++i) if (dm.getWaveInDevice (i) == d) return true;
            return false;
        };
        if (inputDevice != nullptr && ! exists (inputDevice))   // the device went away (audio settings changed)
        {
            if (inputMeter != nullptr) inputMeter->stillValid = [] { return false; };
            inputMeter.reset();
            inputDevice = nullptr;
        }
        te::InputDevice* dev = nullptr;
        if (EngineHelpers::isTrackArmed (*track))
            for (auto in : track->edit.getAllInputDevices())
                if (te::isOnTargetTrack (*in, *track, 0) && in->owner.getDeviceType() == te::InputDevice::waveDevice) { dev = &in->owner; break; }
        if (dev != inputDevice)
        {
            inputMeter.reset();
            inputDevice = dev;
            if (dev != nullptr)
            {
                inputMeter = std::make_unique<LevelMeter> (dev->levelMeasurer, true);
                inputMeter->stillValid = [&dm, dev]
                {
                    for (int i = 0; i < dm.getNumWaveInDevices(); ++i) if (dm.getWaveInDevice (i) == dev) return true;
                    return false;
                };
                inputMeter->setTooltip ("Input level of " + dev->getName() + " (the top light turns red if the input clips: turn the input gain down)");
                addAndMakeVisible (*inputMeter);
            }
            if (meter != nullptr) meter->setVisible (inputMeter == nullptr);
            resized();
        }
        if (inputMeter != nullptr)
        {
            if (meter != nullptr) inputMeter->setBounds (meter->getBounds());
            const bool clip = inputMeter->hasClipped();
            if (clip != inputClipped) { inputClipped = clip; repaint(); }
        }
    }

    std::unique_ptr<LevelMeter> inputMeter;
    te::InputDevice* inputDevice = nullptr;
    bool inputClipped = false;
    bool resizing = false;
    int resizeStart = 0;

    /** The palette, applied to every selected track (and their clips follow the track colour). */
    void showColours()
    {
        TrackColours::show (*this, track->getColour(), [&e = evs, t = track] (Colour c)
        {
            e.edit.getUndoManager().beginNewTransaction ("Track colour");
            auto sel = e.selectionManager.getItemsOfType<te::Track>();
            if (! sel.contains (t.get())) sel = { t.get() };
            for (auto tr : sel) tr->setColour (c);
        });
    }

private:
    bool hadNote = false;
    void timerCallback() override { refresh(); }

    void refresh()
    {
        if (const bool noted = Notes::has (*track); noted != hadNote) { hadNote = noted; repaint(); }
        muteBtn.setToggleState (track->isMuted (false), dontSendNotification);
        soloBtn.setToggleState (track->isSolo (false), dontSendNotification);
        armBtn.setToggleState (EngineHelpers::isTrackArmed (*track), dontSendNotification);
        updateInputMeter();
        autoBtn.setToggleState (evs.isAutomationVisible (*track), dontSendNotification);

        if (! volume.isMouseButtonDown())
            if (auto vp = track->getVolumePlugin())
                volume.setValue (vp->getVolumeDb(), dontSendNotification);

        String desc = "AUDIO";
        te::Plugin* inst = nullptr;
        for (auto p : track->pluginList)
            if (p->isSynth() || dynamic_cast<te::RackInstance*> (p) != nullptr) { inst = p; desc = "INST  " + (dynamic_cast<te::RackInstance*> (p) != nullptr ? String ("Drum Rack") : p->getName()); break; }

        // tracks Pulse named follow their instrument (until the user renames them)
        if (inst != nullptr && track->state.getProperty (ClipImport::autoNameID) && ! name.isBeingEdited())
        {
            const auto id = inst->itemID.toString();
            if (track->state.getProperty (ClipImport::autoInstID).toString() != id)
            {
                track->state.setProperty (ClipImport::autoInstID, id, nullptr);
                const auto want = ClipImport::instrumentTrackName (*inst);
                if (want.isNotEmpty() && track->getName() != want) track->setName (want);
            }
        }

        if (track->isFrozen (te::Track::anyFreeze))
            desc = "FROZEN  " + desc;

        type.setText (desc, dontSendNotification);

        if (! name.isBeingEdited())
            name.setText (track->getName(), dontSendNotification);

        repaint();
    }

    /** Arms the track, auto-assigning an input if it has none yet
        (MIDI input for instrument tracks, the first audio input otherwise). */
    void toggleArm()
    {
        const bool arm = ! EngineHelpers::isTrackArmed (*track);

        if (arm && ! EngineHelpers::trackHasInput (*track))
        {
            const bool wantMidi = EngineHelpers::trackHasInstrument (*track);

            for (auto instance : evs.edit.getAllInputDevices())
            {
                auto type = instance->getInputDevice().getDeviceType();
                const bool isMidi = type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice;

                if ((wantMidi && isMidi) || (! wantMidi && type == te::InputDevice::waveDevice))
                {
                    [[maybe_unused]] auto res = instance->setTarget (track->itemID, true, &evs.edit.getUndoManager(), 0);
                    break;
                }
            }
        }

        if (arm && ! EngineHelpers::trackHasInput (*track))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "No input available",
                                              "Enable an input on your audio interface (Audio Settings) or connect a MIDI device.");
            return;
        }

        EngineHelpers::armTrack (*track, arm);
        refresh();
    }

    void showInputMenu()
    {
        PopupMenu m;
        int id = 1;
        Array<te::InputDeviceInstance*> instances;

        m.addSectionHeader ("Audio inputs");
        for (auto instance : evs.edit.getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::waveDevice)
            {
                m.addItem (id++, instance->getInputDevice().getName(), true, instance->getTargets().contains (track->itemID));
                instances.add (instance);
            }

        m.addSectionHeader ("MIDI inputs");
        for (auto instance : evs.edit.getAllInputDevices())
        {
            auto t = instance->getInputDevice().getDeviceType();
            if (t == te::InputDevice::physicalMidiDevice || t == te::InputDevice::virtualMidiDevice)
            {
                m.addItem (id++, instance->getInputDevice().getName(), true, instance->getTargets().contains (track->itemID));
                instances.add (instance);
            }
        }

        if (instances.isEmpty())
            m.addItem ("No inputs - check Audio Settings", false, false, nullptr);

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&inBtn), [this, instances] (int res)
        {
            if (res >= 1 && res <= instances.size())
            {
                auto* instance = instances[res - 1];
                instance->getInputDevice().setMonitorMode (te::InputDevice::MonitorMode::automatic);
                [[maybe_unused]] auto r = instance->setTarget (track->itemID, true, &evs.edit.getUndoManager(), 0);
            }
        });
    }

    EditViewState& evs;
    te::AudioTrack::Ptr track;
    Label name, type;
    TextButton muteBtn { "M" }, soloBtn { "S" }, armBtn { "R" }, inBtn { "I" }, autoBtn { "A" };
    Slider volume;
    std::unique_ptr<LevelMeter> meter;
    Rectangle<int> numberArea;
    bool dragStarted = false, dropHover = false;
};

//==============================================================================
class Ruler : public Component,
              private Timer,
              private ChangeListener
{
public:
    explicit Ruler (EditViewState& e) : evs (e)
    {
        evs.viewChanged.addChangeListener (this);
        startTimerHz (30);
    }

    ~Ruler() override { evs.viewChanged.removeChangeListener (this); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        auto& tc = evs.edit.getTransport();
        auto lr = tc.getLoopRange();
        auto lx1 = evs.timeToX (lr.getStart()), lx2 = evs.timeToX (lr.getEnd());
        const bool punch = evs.edit.recordingPunchInOut.get();
        g.setColour (punch ? Theme::red.withAlpha (0.75f) : tc.looping ? Theme::orange.withAlpha (0.6f) : Theme::ink.withAlpha (0.12f));
        g.fillRect (lx1, 0.0f, lx2 - lx1, 7.0f);
        if (punch)
        {
            g.setColour (Theme::red);
            g.fillRect (lx1 - 1.0f, 0.0f, 2.0f, (float) getHeight());
            g.fillRect (lx2 - 1.0f, 0.0f, 2.0f, (float) getHeight());
        }

        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        int barStep = 1;
        while (barStep * bpb * evs.pixelsPerBeat < 36.0) barStep *= 2;

        g.setFont (FontOptions (12.0f));
        Timeline::forEachBar (evs.edit, evs.xToBeat (0), evs.xToBeat ((float) getWidth()), barStep, [&] (int bar, double beat)
        {
            auto x = (int) evs.beatToX (beat);
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.drawVerticalLine (x, 11.0f, (float) getHeight());
            g.setColour (Theme::muted.brighter (0.3f));
            g.drawText (String (bar + 1), x + 4, 9, 40, 16, Justification::centredLeft);
        });

        auto px = evs.timeToX (tc.getPosition());
        g.setColour (Theme::accent);
        Path tri;
        tri.addTriangle (px - 5.0f, (float) getHeight() - 8.0f, px + 5.0f, (float) getHeight() - 8.0f, px, (float) getHeight());
        g.fillPath (tri);
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = false;

        if (e.mods.isPopupMenu())
        {
            auto& edit = evs.edit;
            auto* sm = &evs.selectionManager;
            const bool hasRange = ! edit.getTransport().getLoopRange().isEmpty();
            PopupMenu m;
            m.addSectionHeader ("Loop range");
            m.addItem ("Duplicate loop region", hasRange, false, [&edit]
            {
                edit.getUndoManager().beginNewTransaction ("Duplicate region");
                EditingOps::duplicateLoopRegion (edit);
            });
            m.addItem ("Insert silence (loop length)", hasRange, false, [&edit]
            {
                edit.getUndoManager().beginNewTransaction ("Insert time");
                EditingOps::insertTime (edit);
            });
            m.addItem ("Delete time (close gap)", hasRange, false, [&edit, sm]
            {
                edit.getUndoManager().beginNewTransaction ("Delete time");
                EditingOps::deleteTime (edit, *sm);
            });
            m.addItem ("Bounce loop range of selected track", hasRange, false, [&edit, sm]
            {
                if (auto t = EngineHelpers::getSelectedTrack (*sm))
                    EditingOps::bounceToNewTrack (*t, edit.getTransport().getLoopRange());
            });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
            dragging = true;   // don't move the playhead on release
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (std::abs (e.getDistanceFromDragStartX()) < 4 && ! dragging)
            return;

        dragging = true;
        auto b1 = jmax (0.0, evs.snap (evs.xToBeat ((float) jmin (e.getMouseDownX(), e.x))));
        auto b2 = evs.snap (evs.xToBeat ((float) jmax (e.getMouseDownX(), e.x)));

        if (b2 > b1)
        {
            auto& tc = evs.edit.getTransport();
            tc.setLoopRange ({ EngineHelpers::beatToTime (evs.edit, b1), EngineHelpers::beatToTime (evs.edit, b2) });
            tc.looping = true;
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (! dragging)
            evs.edit.getTransport().setPosition (EngineHelpers::beatToTime (evs.edit, jmax (0.0, evs.snap (evs.xToBeat ((float) e.x)))));
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (std::abs (w.deltaX) > std::abs (w.deltaY))
            evs.scrollBy (-w.deltaX * 400.0 / evs.pixelsPerBeat);
        else
            evs.zoom (std::exp (w.deltaY * 1.5), (float) e.x);
    }

private:
    void timerCallback() override
    {
        auto& tc = evs.edit.getTransport();
        const auto lr = tc.getLoopRange();
        if (evs.changedSince (lastSig, lr.getStart().inSeconds() * 1.7 + lr.getEnd().inSeconds() * 2.9 + (tc.looping ? 11.0 : 0.0) + (evs.edit.recordingPunchInOut.get() ? 23.0 : 0.0) + getWidth()))
            repaint();
    }
    double lastSig = -1.0;
    void changeListenerCallback (ChangeBroadcaster*) override { repaint(); }

    EditViewState& evs;
    bool dragging = false;
};

//==============================================================================
/** Playhead line drawn over the lanes; also auto-scrolls while playing. */
class PlayheadOverlay : public Component,
                        private Timer
{
public:
    explicit PlayheadOverlay (EditViewState& e) : evs (e)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (40);
    }

    void paint (Graphics& g) override
    {
        auto& tc = evs.edit.getTransport();
        g.setColour (tc.isRecording() ? Theme::red : Colours::white);
        g.fillRect ((float) x, 0.0f, 1.5f, (float) getHeight());
    }

private:
    void timerCallback() override
    {
        auto& tc = evs.edit.getTransport();
        auto newX = (int) evs.timeToX (tc.getPosition());

        if (tc.isPlaying() && newX > getWidth() * 9 / 10)
        {
            evs.scrollBy ((double) getWidth() * 0.5 / evs.pixelsPerBeat);
            newX = (int) evs.timeToX (tc.getPosition());
        }

        if (newX != x)
        {
            repaint (jmin (x, newX) - 2, 0, std::abs (newX - x) + 5, getHeight());
            x = newX;
        }
    }

    EditViewState& evs;
    int x = -10;
};

//==============================================================================
/** Header row for a group (submix folder) track. */
class FolderHeader : public Component,
                     private Timer
{
public:
    FolderHeader (EditViewState& e, te::FolderTrack::Ptr f) : evs (e), folder (f)
    {
        name.setText (folder->getName(), dontSendNotification);
        name.setEditable (false, true);
        name.setFont (FontOptions (14.0f, Font::bold));
        name.onTextChange = [this] { folder->setName (name.getText()); };
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        collapseBtn.onClick = [this]
        {
            auto id = folder->itemID;
            if (evs.collapsed.count (id)) evs.collapsed.erase (id); else evs.collapsed.insert (id);
            evs.layoutChanged.sendChangeMessage();
        };
        collapseBtn.setFlat (true);
        muteBtn.setColour (TextButton::buttonOnColourId, Theme::yellow);
        soloBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
        muteBtn.onClick = [this] { EngineHelpers::toggleMute (*folder); };
        soloBtn.onClick = [this] { EngineHelpers::toggleSolo (*folder); };
        for (auto* b : std::initializer_list<Component*> { &collapseBtn, &muteBtn, &soloBtn }) addAndMakeVisible (b);

        volume.setSliderStyle (Slider::LinearHorizontal);
        volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setTooltip ("Group (submix) volume");
        volume.onValueChange = [this] { if (auto vp = folder->getVolumePlugin()) vp->setVolumeDb ((float) volume.getValue()); };
        addAndMakeVisible (volume);
        refresh();
        startTimerHz (6);
    }

    void paint (Graphics& g) override
    {
        const bool sel = evs.selectionManager.isSelected (folder.get());
        g.fillAll (sel ? Theme::line : Theme::panel2);
        g.setColour (folder->getColour());
        g.fillRect (0, 0, getWidth(), 3);
        g.fillRect (0, 0, 5, getHeight());
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("GROUP  " + String (folder->getAllAudioSubTracks (true).size()) + " tracks", 34, 24, 140, 14, Justification::left);
        g.setColour (Theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (8).reduced (4, 6);
        auto top = r.removeFromTop (20);
        collapseBtn.setBounds (top.removeFromLeft (20));
        top.removeFromLeft (4);
        name.setBounds (top);
        r.removeFromTop (18);
        auto row = r.removeFromTop (20);
        muteBtn.setBounds (row.removeFromLeft (22)); row.removeFromLeft (3);
        soloBtn.setBounds (row.removeFromLeft (22)); row.removeFromLeft (6);
        volume.setBounds (row);
    }

    void mouseDown (const MouseEvent& e) override
    {
        evs.selectionManager.selectOnly (folder.get());
        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            m.addItem ("Rename", [this] { name.showEditor(); });
            m.addItem ("Ungroup", [f = folder] { f->edit.getUndoManager().beginNewTransaction ("Ungroup"); EditingOps::ungroup (*f); });
            m.addSeparator();
            const bool frozen = EditingOps::isGroupFrozen (*folder);
            m.addItem (frozen ? "Unfreeze group" : "Freeze group (render to audio, saves CPU)", [f = folder, frozen]
            {
                f->edit.getUndoManager().beginNewTransaction (frozen ? "Unfreeze group" : "Freeze group");
                if (frozen) EditingOps::unfreezeGroup (*f); else EditingOps::freezeGroup (*f);
            });
            m.addItem ("Flatten group (replace with audio)", [f = folder]
            {
                f->edit.getUndoManager().beginNewTransaction ("Flatten group");
                EditingOps::flattenGroup (*f);
            });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }
    }

    void mouseDoubleClick (const MouseEvent&) override { name.showEditor(); }

private:
    void timerCallback() override { refresh(); }

    void refresh()
    {
        collapseBtn.setIcon (evs.collapsed.count (folder->itemID) ? Icons::Type::chevronRight : Icons::Type::chevronDown);
        muteBtn.setToggleState (folder->isMuted (false), dontSendNotification);
        soloBtn.setToggleState (folder->isSolo (false), dontSendNotification);
        if (! volume.isMouseButtonDown())
            if (auto vp = folder->getVolumePlugin())
                volume.setValue (vp->getVolumeDb(), dontSendNotification);
        if (! name.isBeingEdited())
            name.setText (folder->getName(), dontSendNotification);
        repaint();
    }

    EditViewState& evs;
    te::FolderTrack::Ptr folder;
    Label name;
    IconButton collapseBtn { Icons::Type::chevronDown, "Show / hide the tracks in this group" };
    TextButton muteBtn { "M" }, soloBtn { "S" };
    Slider volume;
};

/** Lane for a group row: a compact overview of all clips inside the group. */
class FolderLane : public Component,
                   private Timer
{
public:
    FolderLane (EditViewState& e, te::FolderTrack::Ptr f) : evs (e), folder (f) { startTimerHz (4); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::raised);
        drawBeatGrid (g, evs, getLocalBounds());
        auto subs = folder->getAllAudioSubTracks (true);
        const float rowH = jmax (2.0f, (float) (getHeight() - 8) / (float) jmax (1, subs.size()));
        for (int i = 0; i < subs.size(); ++i)
            for (auto c : subs[i]->getClips())
            {
                auto x1 = evs.timeToX (c->getPosition().getStart()), x2 = evs.timeToX (c->getPosition().getEnd());
                g.setColour (subs[i]->getColour().withAlpha (0.7f));
                g.fillRect (x1, 4.0f + (float) i * rowH, jmax (1.0f, x2 - x1), jmax (1.5f, rowH - 1.0f));
            }
        g.setColour (Theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void mouseDown (const MouseEvent&) override { evs.selectionManager.selectOnly (folder.get()); }

private:
    void timerCallback() override { repaint(); }
    EditViewState& evs;
    te::FolderTrack::Ptr folder;
};

//==============================================================================
/** The whole song in miniature (every track's clips) with the visible part as a box: drag the box
    to scroll, drag its edges to zoom, click elsewhere to jump there, scroll the wheel to move. */
class Overview : public Component,
                 private Timer
{
public:
    Overview (EditViewState& e, std::function<int()> laneWidth) : evs (e), visibleWidth (std::move (laneWidth))
    {
        setTooltip ("Overview: drag the box to scroll, its edges to zoom, click to jump (wheel scrolls too)");
        startTimerHz (15);
    }

    void setTooltip (const String& t) { tip = t; }
    String getTooltip() { return tip; }

    double totalBeats() const
    {
        const double song = EngineHelpers::timeToBeat (evs.edit, te::TimePosition() + evs.edit.getLength());
        return jmax (song + 16.0, viewEnd() + 8.0, 64.0);
    }
    double viewBeats() const { return visibleWidth() / jmax (0.01, evs.pixelsPerBeat); }
    double viewEnd() const   { return evs.viewStartBeat + viewBeats(); }
    float xFor (double beat) const { return (float) (beat / totalBeats()) * (float) getWidth(); }
    double beatAt (float x) const  { return x / (float) jmax (1, getWidth()) * totalBeats(); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::well);
        auto tracks = EngineHelpers::getUserTracks (evs.edit);
        const float rowH = jmax (1.0f, (float) (getHeight() - 2) / (float) jmax (1, tracks.size()));
        for (int i = 0; i < tracks.size(); ++i)
            for (auto c : tracks[i]->getClips())
            {
                const double b0 = c->getStartBeat().inBeats(), b1 = c->getEndBeat().inBeats();
                g.setColour (tracks[i]->getColour().withAlpha (0.8f));
                g.fillRect (Rectangle<float> (xFor (b0), 1.0f + rowH * i, jmax (1.0f, xFor (b1) - xFor (b0)), jmax (1.0f, rowH - 1.0f)));
            }
        // playhead
        const double ph = EngineHelpers::timeToBeat (evs.edit, evs.edit.getTransport().getPosition());
        g.setColour (Theme::ink.withAlpha (0.7f));
        g.drawVerticalLine ((int) xFor (ph), 0.0f, (float) getHeight());
        // the visible part
        auto box = Rectangle<float> (xFor (evs.viewStartBeat), 0.5f, jmax (6.0f, xFor (viewEnd()) - xFor (evs.viewStartBeat)), (float) getHeight() - 1.0f);
        g.setColour (Theme::accent.withAlpha (0.14f));
        g.fillRect (box);
        g.setColour (Theme::accent);
        g.drawRect (box, 1.5f);
        g.fillRect (box.withWidth (3.0f));
        g.fillRect (box.withX (box.getRight() - 3.0f).withWidth (3.0f));
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int h = hit (e.position.x);
        setMouseCursor (h == 1 || h == 2 ? MouseCursor::LeftRightResizeCursor : h == 0 ? MouseCursor::DraggingHandCursor : MouseCursor::PointingHandCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        mode = hit (e.position.x);
        if (mode < 0)   // jump: centre the view there
        {
            evs.viewStartBeat = jmax (0.0, beatAt (e.position.x) - viewBeats() * 0.5);
            evs.viewChanged.sendChangeMessage();
            mode = 0;
        }
        startView = evs.viewStartBeat;
        startEnd = viewEnd();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const double d = beatAt ((float) e.getDistanceFromDragStartX()) ;
        if (mode == 0) evs.viewStartBeat = jmax (0.0, startView + d);
        else
        {
            double v0 = startView, v1 = startEnd;
            if (mode == 1) v0 = jlimit (0.0, v1 - 1.0, startView + d);
            else           v1 = jmax (v0 + 1.0, startEnd + d);
            evs.pixelsPerBeat = jlimit (2.0, 600.0, visibleWidth() / (v1 - v0));
            evs.viewStartBeat = v0;
        }
        evs.viewChanged.sendChangeMessage();
    }

    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        evs.scrollBy (-(std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * viewBeats() * 0.5);
    }

private:
    int hit (float x) const   // 1 left edge, 2 right edge, 0 inside, -1 outside
    {
        const float l = xFor (evs.viewStartBeat), r = jmax (l + 6.0f, xFor (viewEnd()));
        if (std::abs (x - l) < 5.0f) return 1;
        if (std::abs (x - r) < 5.0f) return 2;
        return x > l && x < r ? 0 : -1;
    }
    void timerCallback() override
    {
        // the playhead (to the pixel), the song's length and the size; the view and the clips are in the signature
        const double ph = EngineHelpers::timeToBeat (evs.edit, evs.edit.getTransport().getPosition());
        if (evs.changedSince (lastSig, std::floor (xFor (ph)) * 13.0 + evs.edit.getLength().inSeconds() + getWidth() * 0.01)) repaint();
    }
    double lastSig = -1.0;

    EditViewState& evs;
    std::function<int()> visibleWidth;
    String tip;
    int mode = 0;
    double startView = 0, startEnd = 0;
};

//==============================================================================
/** Song sections: coloured blocks above the ruler. Click to loop and select a section, drag one
    to move it (with everything in it) before another, drag its right edge to change its length,
    double-click empty space to add one; right-click for rename, colour, duplicate and delete. */
class SectionStrip : public Component,
                     private Timer
{
public:
    explicit SectionStrip (EditViewState& e) : evs (e) { startTimerHz (10); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        const auto list = Sections::all (evs.edit);
        g.setFont (FontOptions (12.0f, Font::bold));
        for (auto& sec : list)
        {
            auto r = rectOf (sec);
            if (r.getRight() < 0 || r.getX() > getWidth()) continue;
            const bool sel = sec.index == selected, dragging = sec.index == dragIndex && moving;
            g.setColour (sec.colour.withAlpha (dragging ? 0.35f : (sel ? 0.95f : 0.75f)));
            g.fillRoundedRectangle (r.reduced (1.0f, 2.0f), 4.0f);
            if (sel) { g.setColour (Colours::white); g.drawRoundedRectangle (r.reduced (1.0f, 2.0f), 4.0f, 1.5f); }
            g.setColour (sec.colour.getPerceivedBrightness() > 0.6f ? Colour (0xff111317) : Colours::white);
            g.drawText (sec.name, r.reduced (7.0f, 0.0f), Justification::centredLeft, true);
        }
        if (moving && dropX >= 0.0f)
        {
            g.setColour (Theme::ink);
            g.fillRect (dropX - 1.5f, 0.0f, 3.0f, (float) getHeight());
        }
        if (list.empty())
        {
            g.setColour (Theme::muted.withAlpha (0.6f));
            g.setFont (FontOptions (11.0f));
            g.drawText ("Double-click to add a section (Intro, Verse, Chorus...)", getLocalBounds().reduced (8, 0), Justification::centredLeft);
        }
    }

    Rectangle<float> rectOf (const Sections::Section& sec) const
    {
        const float x0 = evs.beatToX (sec.start), x1 = evs.beatToX (sec.end());
        return { x0, 0.0f, jmax (6.0f, x1 - x0), (float) getHeight() };
    }

    int sectionAt (float x) const
    {
        for (auto& sec : Sections::all (evs.edit))
            if (auto r = rectOf (sec); x >= r.getX() && x < r.getRight()) return sec.index;
        return -1;
    }

    bool onRightEdge (float x, int index) const
    {
        for (auto& sec : Sections::all (evs.edit))
            if (sec.index == index) return std::abs (x - rectOf (sec).getRight()) < 6.0f;
        return false;
    }

    /** The section boundary nearest to x (for drops). */
    double boundaryNear (float x, float& outX) const
    {
        double best = 0; float bestD = 1e9f;
        for (auto& sec : Sections::all (evs.edit))
            for (double b : { sec.start, sec.end() })
                if (auto d = std::abs (evs.beatToX (b) - x); d < bestD) { bestD = d; best = b; }
        outX = evs.beatToX (best);
        return best;
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int i = sectionAt (e.position.x - 3.0f);
        setMouseCursor (i >= 0 && onRightEdge (e.position.x, i) ? MouseCursor::LeftRightResizeCursor
                                                                  : sectionAt (e.position.x) >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = sectionAt (e.position.x - 3.0f);
        resizing = i >= 0 && onRightEdge (e.position.x, i);
        dragIndex = resizing ? i : sectionAt (e.position.x);
        moving = false;
        if (e.mods.isPopupMenu()) { showMenu (dragIndex, e.position.x); dragIndex = -1; return; }
        if (dragIndex >= 0 && ! resizing) select (dragIndex);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragIndex < 0) return;
        if (resizing)
        {
            for (auto& sec : Sections::all (evs.edit))
                if (sec.index == dragIndex)
                {
                    const double end = jmax (sec.start + 1.0, evs.snap (evs.xToBeat (e.position.x)));
                    if (! startedEdit) { evs.edit.getUndoManager().beginNewTransaction ("Section length"); startedEdit = true; }
                    Sections::setLength (evs.edit, dragIndex, end - sec.start);
                }
            repaint();
            return;
        }
        if (e.getDistanceFromDragStart() > 6) moving = true;
        if (moving) { boundaryNear (e.position.x, dropX); repaint(); }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (moving && dragIndex >= 0)
        {
            float x = 0;
            const double target = boundaryNear (e.position.x, x);
            evs.edit.getUndoManager().beginNewTransaction ("Move section");
            Sections::move (evs.edit, dragIndex, target, &evs.selectionManager);
            select (-1);
        }
        moving = resizing = startedEdit = false;
        dragIndex = -1;
        dropX = -1.0f;
        repaint();
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int i = sectionAt (e.position.x);
        if (i >= 0) { rename (i); return; }
        const double bar = EngineHelpers::beatsPerBar (evs.edit);
        const double start = std::floor (evs.xToBeat (e.position.x) / bar) * bar;
        evs.edit.getUndoManager().beginNewTransaction ("Add section");
        const char* names[] = { "Intro", "Verse", "Chorus", "Verse 2", "Chorus 2", "Bridge", "Drop", "Outro" };
        const int n = Sections::add (evs.edit, names[jmin (7, (int) Sections::all (evs.edit).size())], jmax (0.0, start), bar * 8);
        if (n >= 0) select (n);
    }

    /** Selects a section: the loop range covers it and its clips are selected. */
    void select (int index)
    {
        selected = index;
        for (auto& sec : Sections::all (evs.edit))
            if (sec.index == index)
            {
                const auto r = Sections::timeRange (evs.edit, sec);
                evs.edit.getTransport().setLoopRange (r);
                te::SelectableList clips;
                for (auto t : EngineHelpers::getUserTracks (evs.edit))
                    for (auto c : t->getClips())
                        if (c->getEditTimeRange().overlaps (r)) clips.add (c);
                evs.selectionManager.select (clips);
            }
        repaint();
    }

private:
    void rename (int index)
    {
        auto* w = new AlertWindow ("Rename section", {}, MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", Sections::node (evs.edit, index)[Sections::nameID].toString());
        w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w, index] (int r)
        {
            const auto n = w->getTextEditorContents ("name").trim();
            if (r == 1 && n.isNotEmpty()) { evs.edit.getUndoManager().beginNewTransaction ("Rename section"); Sections::rename (evs.edit, index, n); }
        }), true);
    }

    void showMenu (int index, float x)
    {
        PopupMenu m;
        auto& edit = evs.edit;
        if (index >= 0)
        {
            m.addItem ("Rename...", [this, index] { rename (index); });
            PopupMenu cols;
            for (int c = 0; c < 8; ++c)
                cols.addColouredItem (c + 1, "Colour " + String (c + 1), Sections::defaultColour (c), true, false, nullptr);
            m.addSubMenu ("Colour", cols);
            m.addItem ("Loop and select it", [this, index] { select (index); });
            m.addSeparator();
            m.addItem ("Duplicate (with its content)", [&edit, index] { edit.getUndoManager().beginNewTransaction ("Duplicate section"); Sections::duplicate (edit, index); });
            m.addItem ("Delete (with its content, close the gap)", [this, &edit, index] { edit.getUndoManager().beginNewTransaction ("Delete section"); Sections::remove (edit, index, &evs.selectionManager); });
            m.addItem ("Remove the section only (keep the content)", [&edit, index] { edit.getUndoManager().beginNewTransaction ("Remove section"); Sections::removeMarker (edit, index); });
            m.showMenuAsync ({}, [&edit, index] (int r)
            {
                if (r >= 1 && r <= 8) { edit.getUndoManager().beginNewTransaction ("Section colour"); Sections::setColour (edit, index, Sections::defaultColour (r - 1)); }
            });
            return;
        }
        m.addItem ("Add a section here", [this, x]
        {
            const double bar = EngineHelpers::beatsPerBar (evs.edit);
            evs.edit.getUndoManager().beginNewTransaction ("Add section");
            Sections::add (evs.edit, "Section", jmax (0.0, std::floor (evs.xToBeat (x) / bar) * bar), bar * 8);
        });
        const auto loop = edit.getTransport().getLoopRange();
        m.addItem ("Add a section for the loop range", ! loop.isEmpty(), false, [this, loop]
        {
            evs.edit.getUndoManager().beginNewTransaction ("Add section");
            const double b0 = EngineHelpers::timeToBeat (evs.edit, loop.getStart()), b1 = EngineHelpers::timeToBeat (evs.edit, loop.getEnd());
            Sections::add (evs.edit, "Section", b0, b1 - b0);
        });
        m.showMenuAsync ({});
    }

    void timerCallback() override
    {
        String sig;
        for (auto& sec : Sections::all (evs.edit)) sig << sec.name << sec.start << sec.length << sec.colour.toString();
        sig << evs.viewStartBeat << evs.pixelsPerBeat;
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    EditViewState& evs;
    int selected = -1, dragIndex = -1;
    bool moving = false, resizing = false, startedEdit = false;
    float dropX = -1.0f;
    String lastSig;
};

//==============================================================================
/** The chord track: the song's chords as blocks above the ruler. Double-click empty space to add one (then choose
    it), click a chord to change it, drag its right edge for its length; right-click for progressions in any key,
    writing the chords out as notes, and clearing. */
class ChordStrip : public Component,
                   private Timer
{
public:
    explicit ChordStrip (EditViewState& e) : evs (e) { startTimerHz (10); }

    static Colour colourFor (const Chords::Chord& c)
    {
        return Colour::fromHSV ((float) (((c.root * 7) % 12) / 12.0), c.kind == Chords::minor ? 0.5f : 0.4f, 0.72f, 1.0f);   // neighbours on the circle of fifths get neighbouring colours
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        const auto list = Chords::all (evs.edit);
        g.setFont (FontOptions (12.0f, Font::bold));
        for (auto& c : list)
        {
            auto r = rectOf (c);
            if (r.getRight() < 0 || r.getX() > getWidth()) continue;
            const auto col = colourFor (c);
            g.setColour (col.withAlpha (0.8f));
            g.fillRoundedRectangle (r.reduced (1.0f, 2.0f), 4.0f);
            g.setColour (col.getPerceivedBrightness() > 0.6f ? Colour (0xff111317) : Colours::white);
            g.drawText (c.name(), r.reduced (6.0f, 0.0f), Justification::centredLeft, true);
        }
        if (list.empty())
        {
            g.setColour (Theme::muted.withAlpha (0.6f));
            g.setFont (FontOptions (11.0f));
            g.drawText ("Double-click to add a chord; right-click for whole progressions in any key", getLocalBounds().reduced (8, 0), Justification::centredLeft);
        }
    }

    Rectangle<float> rectOf (const Chords::Chord& c) const
    {
        const float x0 = evs.beatToX (c.start), x1 = evs.beatToX (c.end());
        return { x0, 0.0f, jmax (6.0f, x1 - x0), (float) getHeight() };
    }

    int chordAt (float x) const
    {
        for (auto& c : Chords::all (evs.edit))
            if (auto r = rectOf (c); x >= r.getX() && x < r.getRight()) return c.index;
        return -1;
    }

    /** Adds a chord one bar long at the bar under x: the same chord as the one before it, or C major. Returns its index. */
    int addAt (float x)
    {
        const double bar = EngineHelpers::beatsPerBar (evs.edit);
        const double start = jmax (0.0, std::floor (evs.xToBeat (x) / bar) * bar);
        int root = 0, kind = Chords::major;
        for (auto& c : Chords::all (evs.edit)) if (c.start < start) { root = c.root; kind = c.kind; }
        evs.edit.getUndoManager().beginNewTransaction ("Add chord");
        return Chords::add (evs.edit, start, bar, root, kind);
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int i = chordAt (e.position.x - 3.0f);
        setMouseCursor (i >= 0 && onRightEdge (e.position.x, i) ? MouseCursor::LeftRightResizeCursor : chordAt (e.position.x) >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        const int i = chordAt (e.position.x - 3.0f);
        resizing = i >= 0 && onRightEdge (e.position.x, i);
        dragIndex = resizing ? i : chordAt (e.position.x);
        startedEdit = false;
        if (e.mods.isPopupMenu()) { showMenu (dragIndex, e.position.x); dragIndex = -1; }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragIndex < 0 || ! resizing) return;
        for (auto& c : Chords::all (evs.edit))
            if (c.index == dragIndex)
            {
                const double end = jmax (c.start + 0.25, evs.snap (evs.xToBeat (e.position.x)));
                if (! startedEdit) { evs.edit.getUndoManager().beginNewTransaction ("Chord length"); startedEdit = true; }
                Chords::setLength (evs.edit, dragIndex, end - c.start);
            }
        repaint();
    }

    void mouseUp (const MouseEvent& e) override
    {
        // a plain click on a chord opens the list to change it
        if (dragIndex >= 0 && ! resizing && ! e.mouseWasDraggedSinceMouseDown() && ! e.mods.isPopupMenu()) showPicker (dragIndex);
        dragIndex = -1;
        resizing = startedEdit = false;
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (chordAt (e.position.x) >= 0) return;
        if (const int n = addAt (e.position.x); n >= 0) showPicker (n);
    }

private:
    bool onRightEdge (float x, int index) const
    {
        for (auto& c : Chords::all (evs.edit))
            if (c.index == index) return std::abs (x - rectOf (c).getRight()) < 6.0f;
        return false;
    }

    /** Every chord, by root note and kind, as a menu. */
    PopupMenu chordMenu (std::function<void (int root, int kind)> chosen, int currentRoot = -1, int currentKind = -1) const
    {
        PopupMenu m;
        const auto roots = Scales::rootNames();
        for (int r = 0; r < 12; ++r)
        {
            PopupMenu sub;
            for (int k = 0; k < (int) Chords::kinds().size(); ++k)
                sub.addItem (roots[r] + Chords::kinds()[(size_t) k].suffix + "   " + Chords::kinds()[(size_t) k].name, true, r == currentRoot && k == currentKind, [chosen, r, k] { chosen (r, k); });
            m.addSubMenu (roots[r], sub, true, nullptr, r == currentRoot);
        }
        return m;
    }

    void showPicker (int index)
    {
        auto v = Chords::node (evs.edit, index);
        if (! v.isValid()) return;
        auto& edit = evs.edit;
        auto m = chordMenu ([&edit, index] (int r, int k) { edit.getUndoManager().beginNewTransaction ("Change chord"); Chords::change (edit, index, r, k); }, (int) v[Chords::rootID], (int) v[Chords::kindID]);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showMenu (int index, float x)
    {
        PopupMenu m;
        auto& edit = evs.edit;
        const double bar = EngineHelpers::beatsPerBar (edit);
        const double barStart = jmax (0.0, std::floor (evs.xToBeat (x) / bar) * bar);
        if (index >= 0)
        {
            auto v = Chords::node (edit, index);
            m.addSubMenu ("Change this chord", chordMenu ([&edit, index] (int r, int k) { edit.getUndoManager().beginNewTransaction ("Change chord"); Chords::change (edit, index, r, k); },
                                                          (int) v[Chords::rootID], (int) v[Chords::kindID]));
            m.addItem ("Remove this chord", [&edit, index] { edit.getUndoManager().beginNewTransaction ("Remove chord"); Chords::remove (edit, index); });
            m.addSeparator();
        }
        else
            m.addSubMenu ("Add a chord here", chordMenu ([&edit, barStart, bar] (int r, int k) { edit.getUndoManager().beginNewTransaction ("Add chord"); Chords::add (edit, barStart, bar, r, k); }));

        // whole progressions, a bar a chord, in any key
        PopupMenu prog;
        const auto roots = Scales::rootNames();
        for (int minorKey = 0; minorKey < 2; ++minorKey)
        {
            PopupMenu keys;
            for (int r = 0; r < 12; ++r)
            {
                PopupMenu list;
                for (int p = 0; p < (int) Chords::progressions().size(); ++p)
                {
                    const String name = Chords::progressions()[(size_t) p].name;
                    const bool forMinor = name.startsWith ("i "), either = name.startsWith ("Twelve");
                    if (! either && forMinor != (minorKey == 1)) continue;
                    list.addItem (name, [&edit, p, r, minorKey, barStart, bar]
                    {
                        edit.getUndoManager().beginNewTransaction ("Add progression");
                        Chords::addProgression (edit, p, r, minorKey == 1, barStart, bar);
                    });
                }
                keys.addSubMenu (roots[r] + (minorKey == 1 ? " minor" : " major"), list);
            }
            prog.addSubMenu (minorKey == 1 ? "In a minor key" : "In a major key", keys);
        }
        m.addSubMenu ("Add a progression from this bar", prog);
        m.addSeparator();

        const bool any = ! Chords::all (edit).empty();
        auto* selClip = evs.selectionManager.getFirstItemOfType<te::MidiClip>();
        m.addItem ("Write the chords into the selected MIDI clip", any && selClip != nullptr, false, [&edit, clip = te::MidiClip::Ptr (selClip)]
        {
            if (clip == nullptr) return;
            edit.getUndoManager().beginNewTransaction ("Write chords");
            Chords::writeToClip (*clip);
        });
        auto* selTrack = evs.selectionManager.getFirstItemOfType<te::AudioTrack>();
        m.addItem ("Write the chords as a new clip on the selected track", any && selTrack != nullptr, false, [&edit, track = te::AudioTrack::Ptr (selTrack)]
        {
            if (track == nullptr) return;
            const auto chords = Chords::all (edit);
            if (chords.empty()) return;
            edit.getUndoManager().beginNewTransaction ("Write chords");
            if (auto clip = te::insertMIDIClip (*track, "Chords", { EngineHelpers::beatToTime (edit, chords.front().start), EngineHelpers::beatToTime (edit, chords.back().end()) }))
                Chords::writeToClip (*clip);
        });
        m.addItem ("Remove all chords", any, false, [&edit] { edit.getUndoManager().beginNewTransaction ("Remove chords"); Chords::clear (edit); });
        m.showMenuAsync ({});
    }

    void timerCallback() override
    {
        const String sig = Chords::signature (evs.edit) + String (evs.viewStartBeat) + String (evs.pixelsPerBeat);
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    EditViewState& evs;
    int dragIndex = -1;
    bool resizing = false, startedEdit = false;
    String lastSig;
};

//==============================================================================
/** Strip above the ruler: song markers (click to jump, drag to move, double-click to rename)
    and time-signature changes. */
class MarkerStrip : public Component,
                    private Timer
{
public:
    explicit MarkerStrip (EditViewState& e) : evs (e) { startTimerHz (10); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        auto& ts = evs.edit.tempoSequence;

        // time signature changes (the first one is shown in the transport bar)
        g.setFont (FontOptions (11.0f, Font::bold));
        for (int i = 1; i < ts.getNumTimeSigs(); ++i)
        {
            auto* sig = ts.getTimeSig (i);
            const float x = evs.beatToX (sig->getStartBeat().inBeats());
            if (x < -40 || x > getWidth()) continue;
            auto r = Rectangle<float> (x, 2.0f, 30.0f, (float) getHeight() - 4.0f);
            g.setColour (Theme::line);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Theme::text);
            g.drawText (String (sig->numerator.get()) + "/" + String (sig->denominator.get()), r, Justification::centred);
        }

        // markers
        g.setFont (FontOptions (12.0f, Font::bold));
        for (auto m : Timeline::markers (evs.edit))
        {
            const float x = evs.timeToX (m->getPosition().getStart());
            if (x > getWidth()) continue;
            const float w = jmax (40.0f, GlyphArrangement::getStringWidth (g.getCurrentFont(), m->getName()) + 12.0f);
            auto r = Rectangle<float> (x, 1.0f, w, (float) getHeight() - 2.0f);
            g.setColour (m == dragged ? Theme::orange : Colour (0xffd8a24a));
            Path flag;
            flag.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), 3.0f, 3.0f, false, true, false, true);
            g.fillPath (flag);
            g.setColour (Colours::black.withAlpha (0.85f));
            g.drawText (m->getName(), r.withTrimmedLeft (5.0f), Justification::centredLeft, true);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragged = markerAt (e.position.x);
        const int sig = timeSigAt (e.position.x);

        if (e.mods.isPopupMenu())
        {
            PopupMenu m;
            const double beat = evs.snap (evs.xToBeat (e.position.x));
            auto& edit = evs.edit;
            if (auto mk = dragged)
            {
                te::Clip::Ptr hold (mk);
                m.addItem ("Rename marker...", [this, hold] { rename (dynamic_cast<te::MarkerClip*> (hold.get())); });
                m.addItem ("Delete marker", [hold, &edit] { edit.getUndoManager().beginNewTransaction ("Delete marker"); hold->removeFromParent(); });
            }
            else if (sig > 0)
            {
                m.addItem ("Delete time signature change", [sig, &edit]
                {
                    edit.getUndoManager().beginNewTransaction ("Delete time signature");
                    edit.tempoSequence.removeTimeSig (sig);
                });
            }
            else
            {
                m.addItem ("Add marker here", [beat, &edit]
                {
                    edit.getUndoManager().beginNewTransaction ("Add marker");
                    Timeline::addMarker (edit, EngineHelpers::beatToTime (edit, beat));
                });
                m.addItem ("Add tempo change here...", [this, beat] { askTempo (beat); });
                m.addItem ("Add time signature change here...", [this, beat] { askTimeSig (beat); });
            }
            dragged = nullptr;
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
            return;
        }

        if (dragged != nullptr)
        {
            dragStart = EngineHelpers::timeToBeat (evs.edit, dragged->getPosition().getStart());
            evs.edit.getUndoManager().beginNewTransaction ("Move marker");
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragged == nullptr || e.getDistanceFromDragStartX() == 0) return;
        moved = true;
        const double b = jmax (0.0, evs.snap (dragStart + e.getDistanceFromDragStartX() / evs.pixelsPerBeat));
        dragged->setStart (EngineHelpers::beatToTime (evs.edit, b), false, true);
        repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        if (dragged != nullptr && ! moved)
            evs.edit.getTransport().setPosition (dragged->getPosition().getStart());
        dragged = nullptr;
        moved = false;
        repaint();
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (auto m = markerAt (e.position.x)) { rename (m); return; }
        evs.edit.getUndoManager().beginNewTransaction ("Add marker");
        Timeline::addMarker (evs.edit, EngineHelpers::beatToTime (evs.edit, evs.snap (evs.xToBeat (e.position.x))));
    }

private:
    te::MarkerClip* markerAt (float x)
    {
        te::MarkerClip* found = nullptr;
        for (auto m : Timeline::markers (evs.edit))
        {
            const float mx = evs.timeToX (m->getPosition().getStart());
            if (x >= mx - 2 && x <= mx + 60) found = m;   // later markers win (drawn on top)
        }
        return found;
    }

    int timeSigAt (float x)
    {
        auto& ts = evs.edit.tempoSequence;
        for (int i = 1; i < ts.getNumTimeSigs(); ++i)
        {
            const float sx = evs.beatToX (ts.getTimeSig (i)->getStartBeat().inBeats());
            if (x >= sx && x <= sx + 30) return i;
        }
        return -1;
    }

    static void askText (const String& title, const String& prompt, const String& initial, std::function<void (const String&)> onOk)
    {
        auto* w = new AlertWindow (title, prompt, MessageBoxIconType::NoIcon);
        w->addTextEditor ("t", initial);
        w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([w, onOk] (int r)
        {
            if (r == 1) onOk (w->getTextEditorContents ("t").trim());
        }), true);
    }

    void rename (te::MarkerClip* m)
    {
        if (m == nullptr) return;
        te::Clip::Ptr hold (m);
        askText ("Rename marker", "Name:", m->getName(), [hold] (const String& name)
        {
            if (name.isNotEmpty()) { hold->edit.getUndoManager().beginNewTransaction ("Rename marker"); hold->setName (name); }
        });
    }

    void askTempo (double beat)
    {
        auto& edit = evs.edit;
        const double now = edit.tempoSequence.getTempoAt (te::BeatPosition::fromBeats (beat)).getBpm();
        askText ("Tempo change", "New tempo (BPM) from bar " + String (Timeline::barAtBeat (edit, beat) + 1) + ":", String (now, 1),
                 [&edit, beat] (const String& t)
        {
            const double bpm = t.getDoubleValue();
            if (bpm < 20.0 || bpm > 300.0) return;
            edit.getUndoManager().beginNewTransaction ("Tempo change");
            Timeline::addTempo (edit, beat, bpm);
        });
    }

    void askTimeSig (double beat)
    {
        auto& edit = evs.edit;
        askText ("Time signature change", "Time signature from bar " + String (Timeline::barAtBeat (edit, beat) + 1) + " (e.g. 3/4, 7/8):", "3/4",
                 [&edit, beat] (const String& t)
        {
            int num = 4, den = 4;
            if (! Timeline::parseTimeSig (t, num, den)) return;
            edit.getUndoManager().beginNewTransaction ("Time signature change");
            Timeline::addTimeSig (edit, beat, num, den);
        });
    }

    void timerCallback() override { if (evs.changedSince (lastSig, getWidth())) repaint(); }
    double lastSig = -1.0;

    EditViewState& evs;
    te::MarkerClip* dragged = nullptr;
    double dragStart = 0;
    bool moved = false;
};

//==============================================================================
/** Tempo automation lane: tempo points (drag up/down for BPM, sideways to move),
    double-click to add, right-click to delete. */
class TempoLane : public Component,
                  private Timer
{
public:
    explicit TempoLane (EditViewState& e) : evs (e) { startTimerHz (10); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        drawBeatGrid (g, evs, getLocalBounds());
        auto& ts = evs.edit.tempoSequence;
        updateRange();

        Path line;
        const int n = ts.getNumTempos();
        for (int i = 0; i < n; ++i)
        {
            auto* t = ts.getTempo (i);
            const float x = evs.beatToX (t->getStartBeat().inBeats()), y = yFor (t->getBpm());
            const float nextX = i + 1 < n ? evs.beatToX (ts.getTempo (i + 1)->getStartBeat().inBeats()) : (float) getWidth();
            if (i == 0) line.startNewSubPath (jmin (0.0f, x), y); else line.lineTo (x, y);
            line.lineTo (nextX, y);   // tempo holds until the next point
        }
        g.setColour (Theme::accent);
        g.strokePath (line, PathStrokeType (1.6f));

        g.setFont (FontOptions (11.0f));
        for (int i = 0; i < n; ++i)
        {
            auto* t = ts.getTempo (i);
            const float x = evs.beatToX (t->getStartBeat().inBeats()), y = yFor (t->getBpm());
            g.setColour (i == dragIndex ? Colours::white : Theme::accent);
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.setColour (Theme::muted);
            g.drawText (String (t->getBpm(), 1), Rectangle<float> (x + 6.0f, y - 14.0f, 50.0f, 12.0f), Justification::left);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragIndex = pointAt (e.position);
        auto& edit = evs.edit;
        if (e.mods.isPopupMenu())
        {
            if (dragIndex > 0)
            {
                PopupMenu m;
                m.addItem ("Delete tempo change", [&edit, i = dragIndex]
                {
                    edit.getUndoManager().beginNewTransaction ("Delete tempo change");
                    edit.tempoSequence.removeTempo (i, true);
                });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
            }
            dragIndex = -1;
            return;
        }
        if (dragIndex >= 0)
        {
            auto* t = edit.tempoSequence.getTempo (dragIndex);
            startBpm = t->getBpm();
            startBeat = t->getStartBeat().inBeats();
            edit.getUndoManager().beginNewTransaction ("Change tempo");
        }
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragIndex < 0) return;
        auto& ts = evs.edit.tempoSequence;
        auto* t = ts.getTempo (dragIndex);
        if (t == nullptr) return;

        const double perPixel = (hiBpm - loBpm) / jmax (1, getHeight() - 8);
        const double bpm = jlimit (20.0, 300.0, std::round ((startBpm - e.getDistanceFromDragStartY() * perPixel) * 10.0) / 10.0);
        double beat = startBeat;
        if (dragIndex > 0)   // the first tempo stays at the start
        {
            const double prev = ts.getTempo (dragIndex - 1)->getStartBeat().inBeats() + 0.25;
            const double next = dragIndex + 1 < ts.getNumTempos() ? ts.getTempo (dragIndex + 1)->getStartBeat().inBeats() - 0.25 : 1.0e9;
            beat = jlimit (prev, next, evs.snap (startBeat + e.getDistanceFromDragStartX() / evs.pixelsPerBeat));
        }
        t->set (te::BeatPosition::fromBeats (beat), bpm, t->getCurve(), true);
        repaint();
    }

    void mouseUp (const MouseEvent&) override { dragIndex = -1; repaint(); }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (pointAt (e.position) >= 0) return;
        const double beat = evs.snap (evs.xToBeat (e.position.x));
        auto& edit = evs.edit;
        edit.getUndoManager().beginNewTransaction ("Add tempo change");
        Timeline::addTempo (edit, beat, bpmForY (e.position.y));
    }

private:
    void updateRange()
    {
        if (dragIndex >= 0) return;   // keep the scale steady while dragging
        auto& ts = evs.edit.tempoSequence;
        double lo = 1.0e9, hi = 0;
        for (int i = 0; i < ts.getNumTempos(); ++i) { lo = jmin (lo, ts.getTempo (i)->getBpm()); hi = jmax (hi, ts.getTempo (i)->getBpm()); }
        loBpm = jmax (20.0, lo - 20.0);
        hiBpm = jmin (300.0, hi + 20.0);
    }

    float yFor (double bpm) const     { return 4.0f + (float) ((hiBpm - bpm) / (hiBpm - loBpm)) * (float) (getHeight() - 8); }
    double bpmForY (float y) const    { return jlimit (20.0, 300.0, std::round ((hiBpm - (y - 4.0f) / (float) (getHeight() - 8) * (hiBpm - loBpm)) * 10.0) / 10.0); }

    int pointAt (Point<float> p)
    {
        auto& ts = evs.edit.tempoSequence;
        for (int i = 0; i < ts.getNumTempos(); ++i)
        {
            auto* t = ts.getTempo (i);
            if (Point<float> (evs.beatToX (t->getStartBeat().inBeats()), yFor (t->getBpm())).getDistanceFrom (p) < 8.0f)
                return i;
        }
        return -1;
    }

    void timerCallback() override { if (dragIndex < 0 && evs.changedSince (lastSig, getWidth())) repaint(); }
    double lastSig = -1.0;

    EditViewState& evs;
    int dragIndex = -1;
    double startBpm = 120, startBeat = 0, loBpm = 60, hiBpm = 180;
};

//==============================================================================
class ArrangementView : public Component,
                        private te::ValueTreeAllEventListener,
                        private FlaggedAsyncUpdater,
                        private ChangeListener
{
public:
    static constexpr int headerWidth = 230;

    explicit ArrangementView (EditViewState& e) : evs (e), ruler (e), markerStrip (e), sectionStrip (e), chordStrip (e), tempoLane (e), content (e), playhead (e), buses (e, headerWidth),
                                                   overview (e, [this] { return jmax (100, viewport.getWidth() - headerWidth - viewport.getScrollBarThickness()); })
    {
        addAndMakeVisible (overview);
        addAndMakeVisible (sectionStrip);
        addAndMakeVisible (chordStrip);
        addAndMakeVisible (buses);
        addAndMakeVisible (ruler);
        addAndMakeVisible (markerStrip);
        addChildComponent (tempoLane);

        tempoBtn.setClickingTogglesState (true);
        tempoBtn.setTooltip ("Show the tempo lane (tempo changes and ramps). Right-click the marker strip for time signatures.");
        tempoBtn.onClick = [this] { tempoLane.setVisible (tempoBtn.getToggleState()); resized(); };
        xfadeBtn.setClickingTogglesState (true);
        xfadeBtn.setToggleState (Timeline::autoCrossfadesEnabled (evs.edit), dontSendNotification);
        xfadeBtn.setTooltip ("Auto-crossfade overlapping audio clips");
        xfadeBtn.onClick = [this] { Timeline::setAutoCrossfades (evs.edit, xfadeBtn.getToggleState()); };
        pencilBtn.setClickingTogglesState (true);
        pencilBtn.setTooltip ("Pencil (D): drag in automation lanes to draw");
        pencilBtn.onClick = [this] { evs.drawMode = pencilBtn.getToggleState(); };
        addAndMakeVisible (pencilBtn);
        for (auto* b : { &tempoBtn, &xfadeBtn })
        {
            b->setColour (TextButton::buttonOnColourId, Theme::accentDim);
            addAndMakeVisible (b);
        }
        addAndMakeVisible (viewport);
        addAndMakeVisible (playhead);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);

        snapBox.addItemList ({ "Bar", "1/4", "1/8", "1/16", "Off" }, 1);
        snapBox.setSelectedId (2, dontSendNotification);
        snapBox.onChange = [this]
        {
            const double vals[] = { (double) EngineHelpers::beatsPerBar (evs.edit), 1.0, 0.5, 0.25, 0.0 };
            evs.snapBeats = vals[snapBox.getSelectedId() - 1];
        };
        addAndMakeVisible (snapBox);
        zoomIn.onClick  = [this] { evs.zoom (1.4, 0); };
        zoomOut.onClick = [this] { evs.zoom (1.0 / 1.4, 0); };
        addAndMakeVisible (zoomIn);
        addAndMakeVisible (zoomOut);

        evs.edit.state.addListener (this);
        evs.viewChanged.addChangeListener (this);
        evs.layoutChanged.addChangeListener (this);
        evs.heightsChanged.addChangeListener (this);
        evs.selectionManager.addChangeListener (this);
        markAndUpdate (updateTracks);
    }

    ~ArrangementView() override
    {
        evs.layoutChanged.removeChangeListener (this);
        evs.heightsChanged.removeChangeListener (this);
        evs.selectionManager.removeChangeListener (this);
        evs.viewChanged.removeChangeListener (this);
        evs.edit.state.removeListener (this);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        g.setColour (Theme::bg2);
        g.fillRect (0, 0, headerWidth, topHeight());
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText ("MARKERS", 10, 0, headerWidth - 20, markerStripHeight, Justification::centredLeft);
        g.drawText ("SECTIONS", 10, markerStripHeight, headerWidth - 20, sectionStripHeight, Justification::centredLeft);
        g.drawText ("CHORDS", 10, markerStripHeight + sectionStripHeight, headerWidth - 20, chordStripHeight, Justification::centredLeft);
        if (tempoLane.isVisible())
            g.drawText ("TEMPO", 10, markerStripHeight + sectionStripHeight + chordStripHeight + rulerHeight, headerWidth - 20, tempoLaneHeight, Justification::centredLeft);
        g.setColour (Theme::line);
        g.drawHorizontalLine (topHeight() - 1, 0.0f, (float) getWidth());
        if (! overviewLabel.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ("OVERVIEW", overviewLabel.reduced (10, 0), Justification::centredLeft);
            g.setFont (FontOptions (10.0f));
            g.drawText ("drag / wheel / middle-drag", overviewLabel.reduced (10, 0), Justification::centredRight);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto strip = r.removeFromTop (markerStripHeight);
        markerStrip.setBounds (strip.withTrimmedLeft (headerWidth));
        auto stripTools = strip.removeFromLeft (headerWidth).reduced (4, 1);
        xfadeBtn.setBounds (stripTools.removeFromRight (52));
        stripTools.removeFromRight (3);
        pencilBtn.setBounds (stripTools.removeFromRight (26));
        stripTools.removeFromRight (3);
        tempoBtn.setBounds (stripTools.removeFromRight (52));
        sectionStrip.setBounds (r.removeFromTop (sectionStripHeight).withTrimmedLeft (headerWidth));
        chordStrip.setBounds (r.removeFromTop (chordStripHeight).withTrimmedLeft (headerWidth));
        auto top = r.removeFromTop (rulerHeight);
        if (tempoLane.isVisible())
            tempoLane.setBounds (r.removeFromTop (tempoLaneHeight).withTrimmedLeft (headerWidth));
        auto tools = top.removeFromLeft (headerWidth).reduced (6, 4);
        zoomIn.setBounds (tools.removeFromRight (24));
        tools.removeFromRight (3);
        zoomOut.setBounds (tools.removeFromRight (24));
        tools.removeFromRight (8);
        snapBox.setBounds (tools.removeFromRight (70));
        ruler.setBounds (top);
        buses.setBounds (r.removeFromBottom (jmin (buses.idealHeight(), r.getHeight() / 2)));
        overview.setBounds (r.removeFromBottom (overviewHeight).withTrimmedLeft (headerWidth));
        overviewLabel = r.withTop (overview.getY()).withHeight (overviewHeight).withWidth (headerWidth);
        viewport.setBounds (r);
        layoutContent();
        playhead.setBounds (r.withTrimmedLeft (headerWidth).withTrimmedRight (viewport.getScrollBarThickness()));
    }

    int getSnapIndex() const        { return snapBox.getSelectedId(); }
    void setSnapIndex (int id)      { snapBox.setSelectedId (jlimit (1, 5, id), sendNotificationSync); }

    /** Zooms and scrolls so the beat range fills the visible lanes. */
    void zoomToBeats (double b0, double b1)
    {
        const double w = jmax (100, ruler.getWidth() - 12);
        b1 = jmax (b1, b0 + 1.0);
        const double pad = (b1 - b0) * 0.03;
        evs.pixelsPerBeat = jlimit (2.0, 600.0, w / (b1 - b0 + 2.0 * pad));
        evs.viewStartBeat = jmax (0.0, b0 - pad);
        evs.viewChanged.sendChangeMessage();
    }

    /** Zooms to the selected clips, or the whole song when nothing is selected. */
    void zoomToFit (bool wholeSong)
    {
        auto clips = EditingOps::selectedClips (evs.selectionManager);
        te::TimeRange r;
        if (! wholeSong && ! clips.isEmpty())
            r = EditingOps::rangeOf (clips);
        else
            r = { te::TimePosition(), te::TimePosition() + evs.edit.getLength() };

        if (r.getLength() <= te::TimeDuration())
            r = { te::TimePosition(), EngineHelpers::beatToTime (evs.edit, 16.0) };

        zoomToBeats (EngineHelpers::timeToBeat (evs.edit, r.getStart()), EngineHelpers::timeToBeat (evs.edit, r.getEnd()));
    }

private:
    static constexpr int rulerHeight = 30, markerStripHeight = 20, sectionStripHeight = 22, chordStripHeight = 22, tempoLaneHeight = 64, overviewHeight = 30;
    Rectangle<int> overviewLabel;
    int topHeight() const { return markerStripHeight + sectionStripHeight + chordStripHeight + rulerHeight + (tempoLane.isVisible() ? tempoLaneHeight : 0); }

    /** Holds the rows; drops on the empty area below the tracks create a new track. */
    struct Content : public Component,
                     public DragAndDropTarget,
                     public FileDragAndDropTarget
    {
        explicit Content (EditViewState& e) : evs (e) {}

        void paint (Graphics& g) override
        {
            g.fillAll (Theme::bg2);
            if (! highlight && getHeight() - rowsBottom() > 60)
            {
                g.setColour (Theme::muted.withAlpha (0.45f));
                g.setFont (FontOptions (12.5f));
                g.drawText ("Drop samples, MIDI files or devices here to create a track", getLocalBounds().withTop (rowsBottom() + 18).withHeight (20),
                            Justification::centred);
            }
            if (highlight)
            {
                g.setColour (Theme::accent.withAlpha (0.1f));
                g.fillRect (getLocalBounds().withTop (rowsBottom()));
                g.setColour (Theme::accent);
                g.drawText ("Drop to create a new track", getLocalBounds().withTop (rowsBottom()).withHeight (40), Justification::centred);
            }
        }

        int rowsBottom() const
        {
            int b = 0;
            for (auto* c : getChildren()) b = jmax (b, c->getBottom());
            return b;
        }

        te::TimePosition timeAt (int x) const
        {
            return EngineHelpers::beatToTime (evs.edit, jmax (0.0, evs.snapFloor (evs.xToBeat ((float) (x - headerWidth)))));
        }

        void dropFiles (const Array<File>& files, int x)
        {
            if (files.isEmpty()) return;
            evs.edit.getUndoManager().beginNewTransaction ("Import files");

            for (auto& f : files)   // one new track per file
                if (auto t = ClipImport::createContentTrack (evs.edit, f.getFileNameWithoutExtension(), ClipImport::isMidiFile (f)))
                    ClipImport::importFile (*t, evs.edit, f, timeAt (x));
        }

        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return ClipImport::isPluginDescription (d.description) || d.description == "PulseFiles" || DragDrop::is (d.description, DragDrop::trackPrefix);
        }

        void itemDragMove (const SourceDetails& d) override
        {
            if (DragDrop::is (d.description, DragDrop::trackPrefix))
            {
                const int y = insertionFor (d.localPosition.y).second;
                if (y != insertY) { insertY = y; repaint(); }
                return;
            }
            setHighlight (d.localPosition.y > rowsBottom());
        }
        void itemDragExit (const SourceDetails&) override    { setHighlight (false); insertY = -1; repaint(); }

        /** The track a dropped track goes before (nullptr = the end) and the y of the insertion line. */
        std::pair<te::Track*, int> insertionFor (int y)
        {
            Array<TrackHeader*> headers;
            for (auto* c : getChildren())
                if (auto* h = dynamic_cast<TrackHeader*> (c)) headers.add (h);
            std::sort (headers.begin(), headers.end(), [] (auto* a, auto* b) { return a->getY() < b->getY(); });
            for (auto* h : headers)
                if (y < h->getBounds().getCentreY())
                    return { h->getTrack(), h->getY() };
            return { nullptr, headers.isEmpty() ? 0 : headers.getLast()->getBottom() };
        }

        void itemDropped (const SourceDetails& d) override
        {
            setHighlight (false);
            if (DragDrop::is (d.description, DragDrop::trackPrefix))
            {
                insertY = -1;
                repaint();
                if (auto t = DragDrop::trackFrom (evs.edit, d.description))
                    DragDrop::moveTrack (*t, insertionFor (d.localPosition.y).first);
                return;
            }

            if (Workflow::isTrackPreset (d.description.toString()))
            {
                evs.edit.getUndoManager().beginNewTransaction ("Load track preset");
                if (auto t = Workflow::loadTrackPreset (evs.edit, Workflow::trackPresetFile (d.description.toString())))
                    evs.selectionManager.selectOnly (t);
                return;
            }

            if (ClipImport::isPluginDescription (d.description))
            {
                auto desc = d.description.toString();
                auto name = ClipImport::trackNameFor (evs.edit.engine, desc);
                if (auto t = ClipImport::createContentTrack (evs.edit, name, false))
                {
                    t->state.setProperty (ClipImport::autoNameID, true, nullptr);
                    Presets::applyDescription (t, t->pluginList, desc);
                    ClipImport::noteInstrument (*t);
                    evs.selectionManager.selectOnly (t);
                }
                return;
            }

            dropFiles (ClipImport::filesFromDrag (d), d.localPosition.x);
        }

        bool isInterestedInFileDrag (const StringArray& files) override
        {
            for (auto& f : files) if (ClipImport::isImportable (File (f))) return true;
            return false;
        }

        void fileDragMove (const StringArray&, int, int y) override { setHighlight (y > rowsBottom()); }
        void fileDragExit (const StringArray&) override               { setHighlight (false); }

        void filesDropped (const StringArray& paths, int x, int) override
        {
            setHighlight (false);
            Array<File> files;
            for (auto& p : paths) if (ClipImport::isImportable (File (p))) files.add (File (p));
            dropFiles (files, x);
        }

        void setHighlight (bool b) { if (b != highlight) { highlight = b; repaint(); } }

        void paintOverChildren (Graphics& g) override
        {
            if (insertY >= 0)
            {
                g.setColour (Theme::accent);
                g.fillRect (0, insertY - 1, getWidth(), 3);
                g.fillEllipse (2.0f, (float) insertY - 4.0f, 8.0f, 8.0f);
            }
            if (! rubber.isEmpty())
            {
                g.setColour (Theme::accent.withAlpha (0.12f));
                g.fillRect (rubber);
                g.setColour (Theme::accent);
                g.drawRect (rubber, 1);
            }
        }

        EditViewState& evs;
        bool highlight = false;
        Rectangle<int> rubber;
        int insertY = -1;
    };

    /** One arrangement row: a track (with optional automation lane) or a group. */
    struct Row
    {
        Component* header = nullptr;
        Component* lane = nullptr;
        AutomationHeader* autoHeader = nullptr;
        AutomationLane* autoLane = nullptr;
        int height = 0;
    };

    void layoutContent()
    {
        const int w = viewport.getMaximumVisibleWidth();
        int total = 0;
        for (auto& r : rows) total += r.height;
        content.setSize (w, jmax (viewport.getMaximumVisibleHeight(), total + 80));

        int y = 0;
        for (auto& r : rows)
        {
            const int h = r.autoLane != nullptr ? r.height - EditViewState::automationHeight : r.height;
            r.header->setBounds (0, y, headerWidth, h);
            r.lane->setBounds (headerWidth, y, w - headerWidth, h);
            y += h;

            if (r.autoHeader != nullptr)
            {
                r.autoHeader->setBounds (0, y, headerWidth, EditViewState::automationHeight);
                r.autoLane->setBounds (headerWidth, y, w - headerWidth, EditViewState::automationHeight);
                y += EditViewState::automationHeight;
            }
        }
    }

    te::AudioTrack* trackAtScreenY (int screenY)
    {
        for (auto* lane : trackLanes)
        {
            auto b = lane->getScreenBounds();
            if (screenY >= b.getY() && screenY < b.getBottom())
                return &lane->getTrack();
        }
        return nullptr;
    }

    bool isHiddenByCollapsedGroup (te::Track& t) const
    {
        for (auto f = t.getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
            if (evs.collapsed.count (f->itemID))
                return true;
        return false;
    }

    void buildTracks()
    {
        rows.clear();
        trackLanes.clear();
        autoLanes.clear();
        autoHeaders.clear();
        lanes.clear();
        headers.clear();

        for (auto t : te::getAllTracks (evs.edit))
        {
            if (isHiddenByCollapsedGroup (*t))
                continue;

            if (auto folder = dynamic_cast<te::FolderTrack*> (t))
            {
                Row r;
                r.header = headers.add (new FolderHeader (evs, folder));
                r.lane = lanes.add (new FolderLane (evs, folder));
                r.height = 70;
                content.addAndMakeVisible (r.header);
                content.addAndMakeVisible (r.lane);
                rows.push_back (r);
                continue;
            }

            auto at = dynamic_cast<te::AudioTrack*> (t);
            if (at == nullptr || dynamic_cast<te::AuxReturnPlugin*> (at->pluginList[0]) != nullptr)
                continue;   // returns live in the mixer

            Row r;
            r.header = headers.add (new TrackHeader (evs, at));
            auto* lane = new TrackLane (evs, at, [this] (int y) { return trackAtScreenY (y); });
            lane->onRubberBand = [this, lane] (const MouseEvent& e, bool finished) { rubberBand (*lane, e, finished); };
            r.lane = lanes.add (lane);
            trackLanes.add (lane);
            r.height = evs.heightFor (*at);
            content.addAndMakeVisible (r.header);
            content.addAndMakeVisible (r.lane);

            if (evs.isAutomationVisible (*at))
            {
                r.autoHeader = autoHeaders.add (new AutomationHeader (evs, at));
                r.autoLane = autoLanes.add (new AutomationLane (evs, at));
                content.addAndMakeVisible (r.autoHeader);
                content.addAndMakeVisible (r.autoLane);
            }
            rows.push_back (r);
        }

        buses.rebuild();
        resized();
    }

    /** Rubber-band selection across all lanes (Shift adds to the current selection). */
    void rubberBand (TrackLane& source, const MouseEvent& e, bool finished)
    {
        auto start = content.getLocalPoint (&source, e.getMouseDownPosition());
        auto end   = content.getLocalPoint (&source, e.getPosition());
        content.rubber = finished ? Rectangle<int>() : Rectangle<int> (start, end);
        content.repaint();

        auto area = Rectangle<int> (start, end);
        te::SelectableList sel;
        if (e.mods.isShiftDown())
            sel = evs.selectionManager.getSelectedObjects();

        for (auto* lane : trackLanes)
            for (auto* cc : lane->getClipComponents())
                if (content.getLocalArea (lane, cc->getBounds()).intersects (area))
                    sel.addIfNotAlreadyThere (&cc->getClip());

        evs.selectionManager.select (sel);
    }

    void valueTreeChanged() override {}

    void valueTreeChildAdded (ValueTree&, ValueTree& c) override
    {
        if (te::TrackList::isTrack (c)) markAndUpdate (updateTracks);
    }

    void valueTreeChildRemoved (ValueTree&, ValueTree& c, int) override
    {
        if (te::TrackList::isTrack (c)) markAndUpdate (updateTracks);
    }

    void valueTreeChildOrderChanged (ValueTree& v, int a, int b) override
    {
        if (te::TrackList::isTrack (v.getChild (a)) || te::TrackList::isTrack (v.getChild (b)))
            markAndUpdate (updateTracks);
    }

    void handleAsyncUpdate() override
    {
        if (compareAndReset (updateTracks))
            buildTracks();
    }

    void changeListenerCallback (ChangeBroadcaster* source) override
    {
        if (source == &evs.layoutChanged)
            return markAndUpdate (updateTracks);
        if (source == &evs.heightsChanged)   // a lane was resized: just lay the rows out again
        {
            for (auto& r : rows)
                if (auto* h = dynamic_cast<TrackHeader*> (r.header))
                    r.height = evs.heightFor (*h->getTrack());
            layoutContent();
            return;
        }

        pencilBtn.setToggleState (evs.drawMode, dontSendNotification);

        for (auto* l : lanes) { l->resized(); l->repaint(); }
        for (auto* h : headers) h->repaint();
        for (auto* a : autoLanes) a->repaint();
        buses.repaintLanes();
    }

    EditViewState& evs;
    Ruler ruler;
    MarkerStrip markerStrip;
    SectionStrip sectionStrip;
    ChordStrip chordStrip;
    TempoLane tempoLane;
    TextButton tempoBtn { "Tempo" }, xfadeBtn { "X-fade" };
    IconButton pencilBtn { Icons::Type::pencil, "Pencil (D)" };
    Viewport viewport;
    Content content;
    PlayheadOverlay playhead;
    BusLanes::Area buses;
    Overview overview;
    std::vector<Row> rows;
    OwnedArray<Component> headers, lanes;
    Array<TrackLane*> trackLanes;
    OwnedArray<AutomationHeader> autoHeaders;
    OwnedArray<AutomationLane> autoLanes;
    ComboBox snapBox;
    TextButton zoomIn { "+" }, zoomOut { "-" };
    bool updateTracks = false;
};
