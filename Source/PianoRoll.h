#pragma once

#include "Meters.h"
#include "DrumRack.h"
#include "MidiTools.h"

#include "EditViewState.h"
#include "Scales.h"
#include "Chords.h"
#include "StepSequencer.h"
#include "ClipEnvelopes.h"

/** Piano roll editor for a te::MidiClip. X positions are in the clip's content beats. */
class PianoRoll : public Component,
                  private Timer,
                  private te::ValueTreeAllEventListener
{
public:
    explicit PianoRoll (EditViewState& e) : evs (e), steps (e)
    {
        setWantsKeyboardFocus (true);

        // the step grid: the same clip as a drum machine would show it (the Steps button swaps it in over the notes)
        stepsBtn.setClickingTogglesState (true);
        stepsBtn.setTooltip ("Step grid: a row for each drum pad, a column for each step. Click to put a hit in or take it out");
        stepsBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        stepsBtn.onClick = [this] { showSteps (stepsBtn.getToggleState()); };
        addAndMakeVisible (stepsBtn);
        addChildComponent (steps);

        gridBox.addItemList ({ "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "Off" }, 1);
        gridBox.setSelectedId (3, dontSendNotification);
        gridBox.onChange = [this]
        {
            const double v[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
            snap = v[gridBox.getSelectedId() - 1];
            if (snap > 0) noteLen = snap;
            steps.setStep (snap > 0 ? snap : 0.25);
        };
        addAndMakeVisible (gridBox);

        paintBtn.setClickingTogglesState (true);
        paintBtn.setTooltip ("Draw tool (B): drag across the grid to paint a note on every step");
        paintBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        addAndMakeVisible (paintBtn);
        ghostBtn.setClickingTogglesState (true);
        ghostBtn.setToggleState (true, dontSendNotification);
        ghostBtn.setTooltip ("Show other clips' notes faintly (ghost notes) to write parts that fit together");
        ghostBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        ghostBtn.onClick = [this] { repaint(); };
        addAndMakeVisible (ghostBtn);

        quantizeBtn.onClick = [this] { quantize(); };
        addAndMakeVisible (quantizeBtn);
        toolsBtn.setTooltip ("MIDI tools for the selected notes (or all notes): humanize, legato, strum, fit to scale, velocity...");
        toolsBtn.onClick = [this] { showTools(); };
        addAndMakeVisible (toolsBtn);

        laneBox.addItemList ({ "Velocity", "Pitch Bend", "Mod Wheel (CC1)", "Volume (CC7)", "Pan (CC10)", "Expression (CC11)", "Sustain (CC64)",
                               "Device envelope..." }, 1);
        laneBox.setSelectedId (1, dontSendNotification);
        laneBox.setTooltip ("What the lane under the notes edits");
        laneBox.onChange = [this]
        {
            if (laneBox.getSelectedId() == envelopeItem) chooseEnvelopeTarget();
            repaint();
        };
        addAndMakeVisible (laneBox);

        scaleBox.addItem ("No scale", 1);
        scaleBox.addItemList (Scales::names(), 2);
        scaleBox.setSelectedId (1, dontSendNotification);
        scaleBox.setTooltip ("Highlight a scale (out-of-key rows are shaded)");
        scaleBox.onChange = [this] { rowsKey.clear(); centreOnNotes(); };
        rootBox.addItemList (Scales::rootNames(), 1);
        rootBox.setSelectedId (1, dontSendNotification);
        rootBox.setTooltip ("Scale root");
        rootBox.onChange = [this] { rowsKey.clear(); centreOnNotes(); };
        foldBtn.setClickingTogglesState (true);
        foldBtn.setTooltip ("Fold: show only rows in the scale (or, with no scale, only rows that have notes)");
        foldBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        foldBtn.onClick = [this] { rowsKey.clear(); centreOnNotes(); };
        for (auto* c : std::initializer_list<Component*> { &scaleBox, &rootBox, &foldBtn })
            addAndMakeVisible (c);

        title.setFont (FontOptions (13.5f, Font::bold));
        addAndMakeVisible (title);
        hint.setText ("Click: add | drag: move | edge: resize | right-click: delete | Shift-drag: select | arrows: move | Q: quantize",
                      dontSendNotification);
        hint.setColour (Label::textColourId, Theme::muted);
        hint.setFont (FontOptions (12.0f));
        addAndMakeVisible (hint);

        startTimerHz (30);
    }

    ~PianoRoll() override { setClip (nullptr); }

    /** Grid as in the grid box: 1 = 1/4, 2 = 1/8, 3 = 1/16, 4 = 1/32, 5 = 1/8T, 6 = 1/16T, 7 = off. */
    void setGrid (int id) { gridBox.setSelectedId (id, sendNotificationSync); }

    void setDrawTool (bool on) { paintBtn.setToggleState (on, dontSendNotification); }
    int numGhostNotes() const   { return (int) ghostNotes().size(); }

    /** Where a beat (from the clip start) and pitch are drawn: the middle of the note row. */
    juce::Point<float> positionOf (double beat, int pitch) const { return { beatToX (beat), pitchToY (pitch) + rowH * 0.5f }; }

    void setClip (te::MidiClip* mc)
    {
        if (clip != nullptr)
            clip->state.removeListener (this);

        clip = mc;
        steps.setClip (mc);
        selected.clear();
        scrollX = 0.0;

        if (clip != nullptr)
        {
            clip->state.addListener (this);
            title.setText (clip->getTrack()->getName() + "  >  " + clip->getName(), dontSendNotification);

            centreOnNotes();
        }
        else
        {
            title.setText ("No clip - double-click a MIDI clip (or an empty instrument lane) in the arrangement", dontSendNotification);
        }

        repaint();
    }

    te::MidiClip* getClip() const { return clip.get(); }

    /** Shows the step grid in place of the notes (or the notes again). */
    void showSteps (bool on)
    {
        stepsBtn.setToggleState (on, dontSendNotification);
        steps.setVisible (on);
        if (on) steps.toFront (false);
        resized();
    }
    StepGrid& stepGrid() { return steps; }

    /** The clip on show can be taken out of the project behind the piano roll's back (undo, deleting its
        track, cutting the section it is in). Everything that draws or edits asks this first. */
    void dropClipIfGone()
    {
        if (clip != nullptr && (clip->getTrack() == nullptr || ! clip->state.getParent().isValid()))
            setClip (nullptr);
    }

    /** Scrolls so the clip's notes (or middle C) sit in the middle of the view. */
    void centreOnNotes()
    {
        int centre = 66;
        if (clip != nullptr && ! clip->getSequence().getNotes().isEmpty())
        {
            int lo = 127, hi = 0;
            for (auto n : clip->getSequence().getNotes()) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
            centre = (lo + hi) / 2;
        }
        scrollY = jmax (0.0f, (float) rowIndexOf (centre) * rowH - (float) gridHeight() * 0.5f);
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (toolbarH).reduced (6, 4);
        title.setBounds (r.removeFromLeft (200));
        scaleBox.setBounds (r.removeFromLeft (130));
        r.removeFromLeft (4);
        rootBox.setBounds (r.removeFromLeft (56));
        r.removeFromLeft (4);
        foldBtn.setBounds (r.removeFromLeft (44));
        r.removeFromLeft (8);
        gridBox.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (6);
        quantizeBtn.setBounds (r.removeFromLeft (80));
        r.removeFromLeft (4);
        toolsBtn.setBounds (r.removeFromLeft (64));
        r.removeFromLeft (4);
        paintBtn.setBounds (r.removeFromLeft (54));
        r.removeFromLeft (4);
        ghostBtn.setBounds (r.removeFromLeft (60));
        r.removeFromLeft (4);
        stepsBtn.setBounds (r.removeFromLeft (56));
        steps.setBounds (getLocalBounds().withTrimmedTop (toolbarH));
        r.removeFromLeft (6);
        laneBox.setBounds (r.removeFromLeft (140));
        r.removeFromLeft (10);
        hint.setBounds (r);
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        dropClipIfGone();
        g.fillAll (Theme::bg2);
        auto area = getLocalBounds().withTrimmedTop (toolbarH);
        const int gh = gridHeight();
        const int top = area.getY();
        updateRows();
        scrollY = jlimit (0.0f, jmax (0.0f, (float) rows.size() * rowH - (float) gh), scrollY);
        const int scale = scaleBox.getSelectedId() - 2, root = rootBox.getSelectedId() - 1;

        g.saveState();
        g.reduceClipRegion (area);

        // note rows (shaded by scale when one is chosen)
        for (int p : rows)
        {
            auto y = pitchToY (p);
            if (y > top + gh || y + rowH < top) continue;
            if (scale >= 0)
            {
                const bool in = Scales::contains (scale, root, p), isRoot = ((p - root) % 12 + 12) % 12 == 0;
                g.setColour (isRoot ? Theme::accentDim : in ? Theme::panel2 : Theme::bg2);
            }
            else g.setColour (isBlack (p) ? Theme::panel : Theme::raised);
            g.fillRect ((float) keyW, y, (float) getWidth(), rowH);
            if (p % 12 == 0 || p % 12 == 5)
            {
                g.setColour (Colours::black.withAlpha (0.35f));
                g.fillRect ((float) keyW, y + rowH - 1.0f, (float) getWidth(), 1.0f);
            }
        }

        // grid
        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        double step = (snap > 0 && snap * ppb >= 8) ? snap : (ppb >= 40 ? 0.25 : 1.0);
        for (double b = std::floor (xToBeat ((float) keyW) / step) * step; beatToX (b) < getWidth(); b += step)
        {
            auto x = beatToX (b);
            if (x < keyW) continue;
            const bool bar = std::abs (std::fmod (b, (double) bpb)) < 1.0e-6;
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (Theme::ink.withAlpha (bar ? 0.2f : beat ? 0.08f : 0.035f));
            g.drawVerticalLine ((int) x, (float) top, (float) getHeight());
            if (bar)
            {
                g.setColour (Theme::muted);
                g.setFont (FontOptions (11.0f));
                g.drawText (String ((int) std::round (b / bpb) + 1), (int) x + 3, top, 30, 12, Justification::left);
            }
        }

        if (clip != nullptr)
        {
            const auto colour = clip->getTrack()->getColour();
            const double len = clipLengthBeats();
            auto ex = beatToX (len);
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRect (jmax ((float) keyW, ex), (float) top, (float) getWidth(), (float) getHeight());
            g.setColour (Theme::orange);
            g.drawVerticalLine ((int) ex, (float) top, (float) getHeight());

            if (ghostBtn.getToggleState())
                for (auto& gn : ghostNotes())
                {
                    auto x = beatToX (gn.start);
                    auto w = jmax (3.0f, (float) ((gn.end - gn.start) * ppb) - 1.0f);
                    auto y = pitchToY (gn.pitch);
                    if (x > getWidth() || x + w < keyW || y > top + gh || y + rowH < top) continue;
                    auto nr = Rectangle<float> (x, y + 1.0f, w, rowH - 2.0f);
                    g.setColour (gn.colour.withAlpha (0.13f));
                    g.fillRoundedRectangle (nr, 2.0f);
                    g.setColour (gn.colour.withAlpha (0.35f));
                    g.drawRoundedRectangle (nr, 2.0f, 0.8f);
                }

            for (auto n : clip->getSequence().getNotes())
            {
                auto x = beatToX (n->getStartBeat().inBeats() - contentStart());
                auto w = jmax (3.0f, (float) (n->getLengthBeats().inBeats() * ppb) - 1.0f);
                auto y = pitchToY (n->getNoteNumber());
                if (x > getWidth() || x + w < keyW || y > top + gh || y + rowH < top) continue;
                const bool sel = selected.contains (n);
                const float vel = (float) n->getVelocity() / 127.0f;
                auto nr = Rectangle<float> (x, y + 1.0f, w, rowH - 2.0f);
                const auto body = sel ? colour.brighter (0.6f) : colour.darker (0.55f - 0.55f * vel).withMultipliedSaturation (0.7f + 0.3f * vel);
                g.setColour (body);
                g.fillRoundedRectangle (nr, 2.0f);
                g.setColour (Theme::ink.withAlpha (sel ? 0.5f : 0.18f));   // top highlight
                g.fillRect (nr.withHeight (jmin (2.0f, nr.getHeight() * 0.3f)).reduced (1.0f, 0.0f));
                g.setColour (sel ? Colours::white : Colours::black.withAlpha (0.55f));
                g.drawRoundedRectangle (nr, 2.0f, sel ? 1.5f : 1.0f);
                if (w > 8.0f && rowH >= 8.0f)   // velocity: a bar along the bottom of the note
                {
                    g.setColour (Colour::fromHSV (0.33f - 0.33f * vel, 0.8f, 1.0f, 0.9f));
                    g.fillRect (Rectangle<float> (nr.getX() + 1.5f, nr.getBottom() - 2.5f, (w - 3.0f) * vel, 1.5f));
                }
                if (w > 30.0f && rowH >= 11.0f)
                {
                    g.setColour (TrackColours::textOn (body).withAlpha (0.85f));
                    g.setFont (FontOptions (jmin (11.0f, rowH - 2.0f)));
                    g.drawText (MidiMessage::getMidiNoteName (n->getNoteNumber(), true, true, 3), nr.reduced (3.0f, 0.0f), Justification::centredLeft, false);
                }
            }

            if (rubber)
            {
                g.setColour (Theme::accent.withAlpha (0.15f));
                g.fillRect (*rubber);
                g.setColour (Theme::accent);
                g.drawRect (*rubber, 1.0f);
            }

            // velocity lane
            auto vy = (float) (top + gh);
            g.setColour (Theme::bg);
            g.fillRect ((float) keyW, vy, (float) getWidth(), (float) velH);
            g.setColour (Theme::line);
            g.drawHorizontalLine ((int) vy, (float) keyW, (float) getWidth());

            if (laneType() == velocityLane)
            {
                for (auto n : clip->getSequence().getNotes())
                {
                    auto x = beatToX (n->getStartBeat().inBeats() - contentStart());
                    if (x < keyW || x > getWidth()) continue;
                    auto h = (float) n->getVelocity() / 127.0f * (float) (velH - 12);
                    g.setColour ((selected.contains (n) ? Colours::white : colour).withAlpha (0.8f));
                    g.fillRect (x + 1.0f, vy + (float) velH - h, 2.0f, h);
                    g.setColour (selected.contains (n) ? Colours::white : colour);
                    g.fillEllipse (x - 1.5f, vy + (float) velH - h - 3.5f, 7.0f, 7.0f);
                }
            }
            else
            {
                drawControllerLane (g, vy, colour);
            }

            // playhead
            auto& tc = evs.edit.getTransport();
            if (tc.isPlaying())
            {
                auto beat = clip->getContentBeatAtTime (tc.getPosition()).inBeats() - contentStart();
                if (beat >= 0 && beat <= len)
                {
                    g.setColour (Colours::white);
                    g.drawVerticalLine ((int) beatToX (beat), (float) top, (float) getHeight());
                }
            }
        }

        // keyboard
        for (int p : rows)
        {
            auto y = pitchToY (p);
            if (y > top + gh || y + rowH < top) continue;
            g.setColour (pressedKey == p ? Theme::accent : isBlack (p) ? Colour (0xff26292f) : Colour (0xffd9dde3));
            g.fillRect (0.0f, y, (float) keyW, rowH);
            g.setColour (Colours::black.withAlpha (0.3f));
            g.fillRect (0.0f, y + rowH - 1.0f, (float) keyW, 1.0f);
            if (auto padName = drumNameFor (p); padName.isNotEmpty() && rowH >= 9.0f)
            {
                g.setColour (isBlack (p) ? Colour (0xffc8ccd2) : Colour (0xff2b3038));
                g.setFont (FontOptions (jmin (10.5f, rowH - 1.0f)));
                g.drawText (padName, 3, (int) y, keyW - 6, (int) rowH, Justification::centredLeft, true);
            }
            else if (p % 12 == 0 || foldBtn.getToggleState())
            {
                g.setColour (Colour (0xff495057));
                g.setFont (FontOptions (10.0f, p % 12 == 0 ? Font::bold : Font::plain));
                g.drawText (MidiMessage::getMidiNoteName (p, true, true, 3), keyW - 30, (int) y, 28, (int) rowH, Justification::centredRight);
            }
        }
        g.setColour (Theme::bg);
        g.fillRect (0, top + gh, keyW, velH);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f));
        g.drawText (laneType() == velocityLane ? "Velocity" : laneShortName(), 6, top + gh + 6, keyW, 14, Justification::left);

        g.restoreState();
    }

    //==============================================================================
    void mouseDown (const MouseEvent& e) override
    {
        dropClipIfGone();
        grabKeyboardFocus();
        const auto x = (float) e.x, y = (float) e.y;

        if (x < keyW && y < toolbarH + gridHeight())
        {
            pressedKey = yToPitch (y);
            preview (pressedKey);
            mode = Mode::key;
            return;
        }

        if (clip == nullptr || y < toolbarH)
            return;

        um().beginNewTransaction ("Edit notes");
        auto& seq = clip->getSequence();

        if (y >= toolbarH + gridHeight())
        {
            if (laneType() == envelopeLane)
            {
                if (! envTarget.isValid()) { chooseEnvelopeTarget(); return; }
                const double beat = jmax (0.0, snapFloor (xToBeat (x)));
                const double tol = jmax (0.02, (snap > 0 ? snap : 0.25) * 0.5);
                if (e.mods.isPopupMenu()) ClipEnvelopes::removeNear (*clip, envTarget, beat, tol, &um());
                else                      { ClipEnvelopes::setPoint (*clip, envTarget, beat, envValueAt (y), tol, &um()); mode = Mode::controller; }
                repaint();
                return;
            }

            if (laneType() != velocityLane)
            {
                if (e.mods.isPopupMenu())
                {
                    if (auto* ev = controllerNear (x))
                        seq.removeControllerEvent (*ev, &um());
                    mode = Mode::none;
                }
                else
                {
                    mode = Mode::controller;
                    drawControllerAt (x, y);
                }
                repaint();
                return;
            }

            mode = Mode::velocity;
            setVelocityAt (x, y);
            return;
        }

        auto* n = hitNote (x, y);

        if (e.mods.isPopupMenu())
        {
            if (n != nullptr)
            {
                if (selected.contains (n))
                {
                    for (auto* s : selected) seq.removeNote (*s, &um());
                    selected.clear();
                }
                else
                {
                    seq.removeNote (*n, &um());
                }
            }
            mode = Mode::erase;
            repaint();
            return;
        }

        if (n != nullptr)
        {
            if (! selected.contains (n))
            {
                if (! e.mods.isShiftDown()) selected.clear();
                selected.add (n);
            }
            anchor = n;
            mode = edgeAt (*n, x);
            if (mode == Mode::move && e.mods.isAltDown())   // Alt-drag: copies of the selection move, the originals stay
            {
                Array<te::MidiNote*> copies;
                te::MidiNote* anchorCopy = nullptr;
                for (auto* sn : selected)
                {
                    auto* c = seq.addNote (sn->getNoteNumber(), sn->getStartBeat(), sn->getLengthBeats(), sn->getVelocity(), sn->getColour(), &um());
                    copies.add (c);
                    if (sn == n) anchorCopy = c;
                }
                selected = copies;
                anchor = anchorCopy != nullptr ? anchorCopy : copies.getFirst();
            }
            captureOriginals();
            if (mode == Mode::move) preview (n->getNoteNumber(), n->getVelocity());
            repaint();
            return;
        }

        if (e.mods.isShiftDown())
        {
            mode = Mode::rubber;
            rubber = Rectangle<float> (x, y, 0, 0);
            return;
        }

        // create a note one grid step long; dragging right draws it longer
        auto beat = jmax (0.0, snapFloor (xToBeat (x)));
        auto pitch = jlimit (0, 127, yToPitch (y));
        const double len = snap > 0 ? snap : 0.25;
        auto* note = seq.addNote (pitch, te::BeatPosition::fromBeats (beat + contentStart()),
                                  te::BeatDuration::fromBeats (len), velocity, 0, &um());
        selected.clear();
        selected.add (note);
        anchor = note;
        mode = paintBtn.getToggleState() ? Mode::paint : Mode::draw;
        paintPitch = pitch;
        captureOriginals();
        preview (pitch, velocity);
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        dropClipIfGone();
        const auto x = (float) e.x, y = (float) e.y;

        if (mode == Mode::key)
        {
            auto p = yToPitch (y);
            if (p != pressedKey && p >= 0 && p <= 127) { pressedKey = p; preview (p); }
            return;
        }

        if (clip == nullptr) return;
        auto& seq = clip->getSequence();

        switch (mode)
        {
            case Mode::velocity: setVelocityAt (x, y); break;
            case Mode::controller:
                if (laneType() == envelopeLane)
                    ClipEnvelopes::setPoint (*clip, envTarget, jmax (0.0, snapFloor (xToBeat (x))), envValueAt (y),
                                             jmax (0.02, (snap > 0 ? snap : 0.25) * 0.5), &um());
                else
                    drawControllerAt (x, y);
                break;

            case Mode::erase:
                if (auto* n = hitNote (x, y)) { selected.removeFirstMatchingValue (n); seq.removeNote (*n, &um()); }
                break;

            case Mode::rubber:
            {
                rubber = Rectangle<float> (Point<float> ((float) e.getMouseDownX(), (float) e.getMouseDownY()), Point<float> (x, y));
                selected.clear();
                for (auto n : seq.getNotes())
                {
                    Rectangle<float> nr (beatToX (n->getStartBeat().inBeats() - contentStart()), pitchToY (n->getNoteNumber()),
                                         (float) (n->getLengthBeats().inBeats() * ppb), rowH);
                    if (nr.intersects (*rubber)) selected.add (n);
                }
                break;
            }

            case Mode::move:
            {
                const auto& o = originals[anchor];
                const double dBeat = e.getDistanceFromDragStartX() / ppb;
                double newStart = snap > 0 ? std::round ((o.start + dBeat) / snap) * snap : o.start + dBeat;
                double db = newStart - o.start;
                double minStart = 1.0e9;
                for (auto& [n, v] : originals) minStart = jmin (minStart, v.start);
                db = jmax (db, -minStart);
                const int dp = (int) std::round (-e.getDistanceFromDragStartY() / rowH);
                const int prevPitch = anchor->getNoteNumber();

                for (auto& [n, v] : originals)
                {
                    n->setStartAndLength (te::BeatPosition::fromBeats (v.start + db + contentStart()), te::BeatDuration::fromBeats (v.len), &um());
                    n->setNoteNumber (pitchByRows (v.pitch, dp), &um());
                }

                if (anchor->getNoteNumber() != prevPitch)
                    preview (anchor->getNoteNumber(), anchor->getVelocity());
                break;
            }

            case Mode::paint:   // draw tool: a note on every grid step the mouse crosses (at the mouse's pitch)
            {
                const double step = snap > 0 ? snap : 0.25;
                const double beat = jmax (0.0, std::floor (xToBeat (x) / step + 1.0e-9) * step);
                const int pitch = jlimit (0, 127, yToPitch (y));
                bool exists = false;
                for (auto n : seq.getNotes())
                    if (n->getNoteNumber() == pitch && std::abs (n->getStartBeat().inBeats() - contentStart() - beat) < 1.0e-6) { exists = true; break; }
                if (! exists && beat < clipLengthBeats())
                {
                    selected.add (seq.addNote (pitch, te::BeatPosition::fromBeats (beat + contentStart()), te::BeatDuration::fromBeats (step), velocity, 0, &um()));
                    if (pitch != paintPitch) { preview (pitch, velocity); paintPitch = pitch; }
                }
                break;
            }

            case Mode::draw:   // a new note: its end follows the mouse, at least one grid step long
            {
                const double step = snap > 0 ? snap : 0.0625;
                const double start = anchor->getStartBeat().inBeats();
                double end = xToBeat (x) + contentStart();
                end = std::ceil ((end - start) / step - 1.0e-6) * step + start;
                anchor->setStartAndLength (anchor->getStartBeat(), te::BeatDuration::fromBeats (jmax (step, end - start)), &um());
                break;
            }

            case Mode::resizeStart:   // the left edge moves the start, keeping the end
            {
                const auto& o = originals[anchor];
                const double dBeat = e.getDistanceFromDragStartX() / ppb;
                double start = o.start + dBeat;
                if (snap > 0) start = std::round (start / snap) * snap;
                const double ds = start - o.start;
                const double minLen = snap > 0 ? snap : 0.05;
                for (auto& [n, v] : originals)
                {
                    const double ns = jlimit (0.0, v.start + v.len - minLen, v.start + ds);
                    n->setStartAndLength (te::BeatPosition::fromBeats (ns + contentStart()), te::BeatDuration::fromBeats (v.start + v.len - ns), &um());
                }
                break;
            }

            case Mode::resize:
            {
                const auto& o = originals[anchor];
                const double dBeat = e.getDistanceFromDragStartX() / ppb;
                double end = o.start + o.len + dBeat;
                if (snap > 0) end = std::round (end / snap) * snap;
                const double dd = end - (o.start + o.len);

                for (auto& [n, v] : originals)
                    n->setStartAndLength (te::BeatPosition::fromBeats (v.start + contentStart()),
                                          te::BeatDuration::fromBeats (jmax (snap > 0 ? snap : 0.05, v.len + dd)), &um());

                noteLen = anchor->getLengthBeats().inBeats();
                break;
            }

            case Mode::none: case Mode::key: break;
        }

        repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        dropClipIfGone();
        if (mode == Mode::key)
            if (auto at = audioTrack()) at->turnOffGuideNotes();

        pressedKey = -1;
        mode = Mode::none;
        rubber.reset();
        repaint();
    }

    void mouseMove (const MouseEvent& e) override
    {
        dropClipIfGone();
        auto* n = (e.y > toolbarH && e.y < toolbarH + gridHeight()) ? hitNote ((float) e.x, (float) e.y) : nullptr;
        if (n != nullptr)
            setMouseCursor (edgeAt (*n, (float) e.x) != Mode::move ? MouseCursor::LeftRightResizeCursor : MouseCursor::DraggingHandCursor);
        else
            setMouseCursor (e.x < keyW ? MouseCursor::PointingHandCursor : MouseCursor::CrosshairCursor);
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        dropClipIfGone();
        if (e.mods.isCtrlDown())
        {
            auto b = xToBeat ((float) e.x);
            ppb = jlimit (10.0, 600.0, ppb * std::exp (w.deltaY * 1.5));
            scrollX = jmax (0.0, b - (e.x - keyW) / ppb);
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            scrollX = jmax (0.0, scrollX - (e.mods.isShiftDown() ? w.deltaY : w.deltaX) * 400.0 / ppb);
        }
        else
        {
            scrollY -= w.deltaY * 400.0f;
        }
        repaint();
    }

    bool keyPressed (const KeyPress& k) override
    {
        dropClipIfGone();
        if (clip == nullptr) return false;
        auto& seq = clip->getSequence();

        if (k.getModifiers().isCommandDown() && ! isCtrlKey (k, 'a'))
            return false; // let Ctrl+Z / Ctrl+S etc. reach the main window

        um().beginNewTransaction ("Edit notes");

        if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey)
        {
            for (auto* n : selected) seq.removeNote (*n, &um());
            selected.clear();
        }
        else if (isCtrlKey (k, 'a'))
        {
            selected = seq.getNotes();
        }
        else if (k.getKeyCode() == KeyPress::upKey || k.getKeyCode() == KeyPress::downKey)
        {
            const int d = (k.getKeyCode() == KeyPress::upKey ? 1 : -1) * (k.getModifiers().isShiftDown() ? 12 : 1);
            for (auto* n : selected) n->setNoteNumber (jlimit (0, 127, n->getNoteNumber() + d), &um());
            if (! selected.isEmpty()) preview (selected.getFirst()->getNoteNumber());
        }
        else if (k.getKeyCode() == KeyPress::leftKey || k.getKeyCode() == KeyPress::rightKey)
        {
            const double d = (k.getKeyCode() == KeyPress::rightKey ? 1.0 : -1.0) * (snap > 0 ? snap : 0.25);
            for (auto* n : selected)
                n->setStartAndLength (te::BeatPosition::fromBeats (jmax (contentStart(), n->getStartBeat().inBeats() + d)), n->getLengthBeats(), &um());
        }
        else if (k == KeyPress ('q'))
        {
            quantize();
        }
        else if (k == KeyPress ('b'))
        {
            paintBtn.setToggleState (! paintBtn.getToggleState(), dontSendNotification);
        }
        else
        {
            return false;
        }

        repaint();
        return true;
    }

private:
    enum class Mode { none, key, move, resize, rubber, erase, velocity, controller, draw, resizeStart, paint };
    struct Orig { double start, len; int pitch; };

    //==============================================================================
    // Controller lane (pitch bend / CCs). Tracktion stores every controller value as 14-bit (0..16383).
    static constexpr int velocityLane = -1, envelopeLane = -2, envelopeItem = 8;

    int laneType() const
    {
        switch (laneBox.getSelectedId())
        {
            case 2: return te::MidiControllerEvent::pitchWheelType;
            case 3: return 1;
            case 4: return 7;
            case 5: return 10;
            case 6: return 11;
            case 7: return 64;
            case envelopeItem: return envelopeLane;
            default: return velocityLane;
        }
    }

    String laneShortName() const
    {
        switch (laneBox.getSelectedId())
        {
            case 2: return "Bend";
            case 3: return "CC1";
            case 4: return "CC7";
            case 5: return "CC10";
            case 6: return "CC11";
            case 7: return "CC64";
            case envelopeItem: return envTarget.isValid() ? envTarget.name : String ("Envelope");
            default: return "Velocity";
        }
    }

    float laneTop() const { return (float) (toolbarH + gridHeight()); }

    float controllerToY (int value) const
    {
        return laneTop() + (float) velH - 4.0f - (float) value / 16383.0f * (float) (velH - 8);
    }

    int yToController (float y) const
    {
        auto n = (laneTop() + (float) velH - 4.0f - y) / (float) (velH - 8);
        return jlimit (0, 16383, roundToInt (n * 16383.0f));
    }

    te::MidiControllerEvent* controllerNear (float x)
    {
        for (auto* ev : clip->getSequence().getControllerEvents())
            if (ev->getType() == laneType()
                && std::abs (beatToX (ev->getBeatPosition().inBeats() - contentStart()) - x) <= 5.0f)
                return ev;
        return nullptr;
    }

    /** Pencil: sets (or creates) the event at the snapped beat under the mouse. */
    void drawControllerAt (float x, float y)
    {
        auto beat = jmax (0.0, snapFloor (xToBeat (x)));
        auto value = yToController (y);
        auto absBeat = beat + contentStart();

        for (auto* ev : clip->getSequence().getControllerEvents())
            if (ev->getType() == laneType() && std::abs (ev->getBeatPosition().inBeats() - absBeat) < 1.0e-4)
            {
                ev->setControllerValue (value, &um());
                return;
            }

        clip->getSequence().addControllerEvent (te::BeatPosition::fromBeats (absBeat), laneType(), value, &um());
    }

    float envValueAt (float y) const
    {
        return jlimit (0.0f, 1.0f, (laneTop() + (float) velH - 4.0f - y) / (float) (velH - 8));
    }

    float envToY (float v) const { return laneTop() + (float) velH - 4.0f - v * (float) (velH - 8); }

    /** Pick the device parameter the envelope lane edits. */
    void chooseEnvelopeTarget()
    {
        auto at = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
        if (at == nullptr) return;
        PopupMenu m;
        for (auto pl : at->pluginList)
        {
            auto params = pl->getAutomatableParameters();
            if (params.isEmpty() || dynamic_cast<te::LevelMeterPlugin*> (pl) != nullptr) continue;
            PopupMenu sub;
            for (auto p : params)
            {
                ClipEnvelopes::Target t { pl->itemID.toString(), p->paramID, pl->getName() + " > " + p->getParameterName() };
                const bool has = ClipEnvelopes::findEnv (*clip, t).isValid();
                sub.addItem (p->getParameterName() + (has ? "  *" : ""), true, envTarget.paramID == t.paramID && envTarget.pluginID == t.pluginID,
                             [this, t] { envTarget = t; repaint(); });
            }
            m.addSubMenu (pl->getName(), sub);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&laneBox));
    }

    /** On a Drum Rack track: the name of the pad that plays this note (empty otherwise). */
    String drumNameFor (int note)
    {
        if (clip == nullptr) return {};
        auto* t = dynamic_cast<te::AudioTrack*> (clip->getTrack());
        auto* rack = t != nullptr ? DrumRack::rackOf (DrumRack::findOnTrack (*t)) : nullptr;
        if (rack == nullptr) return {};
        const int slot = DrumRack::padForNote (*rack, note);
        if (slot < 0) return {};
        auto n = DrumRack::padName (*rack, slot);
        return n.isNotEmpty() ? n.upToFirstOccurrenceOf (" (synth)", false, false) : DrumRack::defaultPadLabel (slot);
    }

    void drawEnvelopeLane (Graphics& g, float laneY, Colour colour)
    {
        if (! envTarget.isValid())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.0f));
            g.drawText ("Choose a device parameter (lane menu), then click / drag to draw its envelope", keyW + 8, (int) laneY + 4, 600, 14, Justification::left);
            return;
        }
        auto env = ClipEnvelopes::findEnv (*clip, envTarget);
        auto pts = ClipEnvelopes::points (env);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f));
        g.drawText (envTarget.name + (pts.empty() ? "  - click to draw" : String()), keyW + 8, (int) laneY + 2, 500, 13, Justification::left);
        if (pts.empty()) return;

        Path p;
        p.startNewSubPath ((float) keyW, envToY (pts.front().second));
        for (auto& [b, v] : pts) p.lineTo (beatToX (b), envToY (v));
        p.lineTo ((float) getWidth(), envToY (pts.back().second));
        g.setColour (Theme::purple);
        g.strokePath (p, PathStrokeType (1.6f));
        for (auto& [b, v] : pts) g.fillEllipse (beatToX (b) - 3.0f, envToY (v) - 3.0f, 6.0f, 6.0f);
        ignoreUnused (colour);
    }

    void drawControllerLane (Graphics& g, float laneY, Colour colour)
    {
        if (laneType() == envelopeLane) { drawEnvelopeLane (g, laneY, colour); return; }

        // centre line for pitch bend
        if (laneType() == te::MidiControllerEvent::pitchWheelType)
        {
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawHorizontalLine ((int) controllerToY (8192), (float) keyW, (float) getWidth());
        }

        Array<te::MidiControllerEvent*> events;
        for (auto* ev : clip->getSequence().getControllerEvents())
            if (ev->getType() == laneType())
                events.add (ev);

        std::sort (events.begin(), events.end(), [] (auto* a, auto* b) { return a->getBeatPosition() < b->getBeatPosition(); });

        if (events.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.0f));
            g.drawText ("Click / drag to draw " + laneBox.getText(), keyW + 8, (int) laneY + 4, 300, 14, Justification::left);
            return;
        }

        // step line (controllers hold their value until the next event)
        Path p;
        for (int i = 0; i < events.size(); ++i)
        {
            auto x = beatToX (events[i]->getBeatPosition().inBeats() - contentStart());
            auto y = controllerToY (events[i]->getControllerValue());
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, p.getCurrentPosition().y), p.lineTo (x, y);
        }
        p.lineTo ((float) getWidth(), p.getCurrentPosition().y);
        g.setColour (colour);
        g.strokePath (p, PathStrokeType (1.5f));

        for (auto* ev : events)
        {
            auto x = beatToX (ev->getBeatPosition().inBeats() - contentStart());
            g.fillEllipse (x - 3.0f, controllerToY (ev->getControllerValue()) - 3.0f, 6.0f, 6.0f);
        }
    }

    static bool isBlack (int p) { auto n = p % 12; return n == 1 || n == 3 || n == 6 || n == 8 || n == 10; }

    juce::UndoManager& um() { return evs.edit.getUndoManager(); }
    int gridHeight() const { return jmax (40, getHeight() - toolbarH - velH); }
    double contentStart() const { return clip != nullptr ? clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats() : 0.0; }
    double clipLengthBeats() const
    {
        auto pos = clip->getPosition();
        return clip->getContentBeatAtTime (pos.getEnd()).inBeats() - contentStart();
    }

    float beatToX (double b) const  { return (float) (keyW + (b - scrollX) * ppb); }
    double xToBeat (float x) const  { return scrollX + (x - keyW) / ppb; }
    // Visible rows, top to bottom: all 128 pitches, or folded to the scale / used pitches.
    void updateRows() const
    {
        const int scale = scaleBox.getSelectedId() - 2, root = rootBox.getSelectedId() - 1;
        const bool fold = foldBtn.getToggleState();
        String key; key << (int) fold << ":" << scale << ":" << root;
        if (fold && clip != nullptr)
            for (auto n : clip->getSequence().getNotes()) key << "," << n->getNoteNumber();
        if (key == rowsKey && ! rows.empty()) return;
        rowsKey = key;

        std::set<int> used;
        if (clip != nullptr)
            for (auto n : clip->getSequence().getNotes()) used.insert (n->getNoteNumber());

        rows.clear();
        for (int p = 127; p >= 0; --p)
        {
            const bool keep = ! fold || used.count (p) > 0
                              || (scale >= 0 ? Scales::contains (scale, root, p) : used.empty());
            if (keep) rows.push_back (p);
        }
        if (rows.empty()) for (int p = 127; p >= 0; --p) rows.push_back (p);
        rowOf.fill (-1);
        for (size_t i = 0; i < rows.size(); ++i) rowOf[(size_t) rows[i]] = (int) i;
    }

    int rowIndexOf (int p) const
    {
        updateRows();
        p = jlimit (0, 127, p);
        if (rowOf[(size_t) p] >= 0) return rowOf[(size_t) p];
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i] <= p) return (int) i;   // nearest row below
        return (int) rows.size() - 1;
    }

    /** Moves a pitch up/down by visible rows (so a folded roll stays in key). */
    int pitchByRows (int pitch, int rowsUp) const
    {
        const int idx = jlimit (0, (int) rows.size() - 1, rowIndexOf (pitch) - rowsUp);
        return rows[(size_t) idx];
    }

    float pitchToY (int p) const
    {
        updateRows();
        const int idx = rowOf[(size_t) jlimit (0, 127, p)];
        return idx < 0 ? -100000.0f : (float) toolbarH + (float) idx * rowH - scrollY;
    }

    int yToPitch (float y) const
    {
        updateRows();
        const int idx = (int) std::floor ((y - (float) toolbarH + scrollY) / rowH);
        return rows[(size_t) jlimit (0, (int) rows.size() - 1, idx)];
    }
    double snapFloor (double b) const { return snap > 0 ? std::floor (b / snap + 1.0e-9) * snap : b; }

    te::AudioTrack* audioTrack() const { return clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr; }

    void preview (int pitch, int vel = 100)
    {
        if (auto at = audioTrack())
            at->playGuideNote (pitch, te::MidiChannel (1), vel, true, true, mode != Mode::key);
    }

    /** Which part of a note is under x: its start edge, end edge (resize) or body (move). The edges
        grow with the note but stay grabbable on short notes. */
    Mode edgeAt (te::MidiNote& n, float x) const
    {
        const float x0 = beatToX (n.getStartBeat().inBeats() - contentStart()), x1 = beatToX (n.getEndBeat().inBeats() - contentStart());
        const float edge = jlimit (4.0f, 10.0f, (x1 - x0) * 0.3f);
        if (x >= x1 - edge) return Mode::resize;
        if (x <= x0 + edge && x1 - x0 > 16.0f) return Mode::resizeStart;
        return Mode::move;
    }

    te::MidiNote* hitNote (float x, float y)
    {
        if (clip == nullptr) return nullptr;
        auto beat = xToBeat (x) + contentStart();
        auto pitch = yToPitch (y);
        auto& notes = clip->getSequence().getNotes();

        for (int i = notes.size(); --i >= 0;)
        {
            auto* n = notes.getUnchecked (i);
            const double slack = 3.0 / ppb;
            if (n->getNoteNumber() == pitch && beat >= n->getStartBeat().inBeats() - slack && beat <= n->getEndBeat().inBeats() + slack)
                return n;
        }
        return nullptr;
    }

    struct GhostNote { double start, end; int pitch; Colour colour; };

    /** Notes of other MIDI clips that overlap this clip in time (in this clip's beats). */
    std::vector<GhostNote> ghostNotes() const
    {
        std::vector<GhostNote> out;
        if (clip == nullptr) return out;
        const double s0 = clip->getStartBeat().inBeats(), cs = contentStart(), len = clipLengthBeats();
        for (auto t : te::getAudioTracks (clip->edit))
            for (auto c : t->getClips())
                if (auto mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc != clip.get())
                {
                    const double o0 = mc->getStartBeat().inBeats(), o1 = mc->getEndBeat().inBeats();
                    if (o1 <= s0 || o0 >= s0 + len) continue;
                    const double ocs = mc->getContentBeatAtTime (mc->getPosition().getStart()).inBeats();
                    for (auto n : mc->getSequence().getNotes())
                    {
                        const double eb = o0 + n->getStartBeat().inBeats() - ocs;   // edit beat
                        const double ee = eb + n->getLengthBeats().inBeats();
                        if (ee <= o0 || eb >= o1) continue;
                        out.push_back ({ eb - s0, jmin (ee, o1) - s0, n->getNoteNumber(), t->getColour() });
                    }
                    if (out.size() > 4000) return out;
                }
        juce::ignoreUnused (cs);
        return out;
    }

    void captureOriginals()
    {
        originals.clear();
        for (auto* n : selected)
            originals[n] = { n->getStartBeat().inBeats() - contentStart(), n->getLengthBeats().inBeats(), n->getNoteNumber() };
    }

    void setVelocityAt (float x, float y)
    {
        const int vel = jlimit (1, 127, (int) std::round (((float) (toolbarH + gridHeight() + velH) - y) / (float) (velH - 8) * 127.0f));
        Array<te::MidiNote*> hits;
        for (auto n : clip->getSequence().getNotes())
            if (std::abs (beatToX (n->getStartBeat().inBeats() - contentStart()) - x) <= 5.0f)
                hits.add (n);

        bool anySelected = false;
        for (auto* n : hits) anySelected |= selected.contains (n);

        for (auto* n : hits)
            if (! anySelected || selected.contains (n))
                n->setVelocity (vel, &um());

        velocity = vel;
    }

    void quantize()
    {
        if (clip == nullptr || snap <= 0) return;
        um().beginNewTransaction ("Quantize");
        auto notes = selected.isEmpty() ? clip->getSequence().getNotes() : selected;
        for (auto* n : notes)
        {
            auto rel = n->getStartBeat().inBeats() - contentStart();
            n->setStartAndLength (te::BeatPosition::fromBeats (contentStart() + jmax (0.0, std::round (rel / snap) * snap)), n->getLengthBeats(), &um());
        }
        repaint();
    }

public:
    /** Runs a MIDI tool by id (keyboard shortcuts): humanize, legato, fit, strum. */
    void applyTool (const String& id)
    {
        if (clip == nullptr) return;
        um().beginNewTransaction ("MIDI tool");
        Random r;
        if (id == "humanize")    MidiTools::humanize (*clip, selected, 0.025, 12, r);
        else if (id == "legato") MidiTools::legato (*clip, selected);
        else if (id == "strum")  MidiTools::strum (*clip, selected, 1.0 / 16.0, true);
        else if (id == "fit")    MidiTools::fitToScale (*clip, selected, scaleBox.getSelectedId() - 2, rootBox.getSelectedId() - 1);
        else if (id == "fitchords") Chords::fitNotes (*clip, selected);
        repaint();
    }

private:
    void showTools()
    {
        if (clip == nullptr) return;
        using MT = MidiTools::Velocity;
        auto run = [this] (const String& name, std::function<void (te::MidiClip&, const MidiTools::Notes&)> fn)
        {
            return [this, name, fn]
            {
                if (clip == nullptr) return;
                um().beginNewTransaction (name);
                fn (*clip, selected);
                repaint();
            };
        };
        const int scale = scaleBox.getSelectedId() - 2, root = rootBox.getSelectedId() - 1;
        PopupMenu m;
        m.addSectionHeader (selected.isEmpty() ? "All notes" : String (selected.size()) + " selected notes");
        PopupMenu hum;
        hum.addItem ("Light", run ("Humanize", [] (auto& c, auto& n) { Random r; MidiTools::humanize (c, n, 0.01, 6, r); }));
        hum.addItem ("Medium", run ("Humanize", [] (auto& c, auto& n) { Random r; MidiTools::humanize (c, n, 0.025, 12, r); }));
        hum.addItem ("Heavy", run ("Humanize", [] (auto& c, auto& n) { Random r; MidiTools::humanize (c, n, 0.05, 20, r); }));
        m.addSubMenu ("Humanize", hum);
        m.addItem ("Legato", run ("Legato", [] (auto& c, auto& n) { MidiTools::legato (c, n); }));
        PopupMenu strum;
        strum.addItem ("Up (1/64)", run ("Strum", [] (auto& c, auto& n) { MidiTools::strum (c, n, 1.0 / 16.0, true); }));
        strum.addItem ("Up (1/32)", run ("Strum", [] (auto& c, auto& n) { MidiTools::strum (c, n, 1.0 / 8.0, true); }));
        strum.addItem ("Down (1/64)", run ("Strum", [] (auto& c, auto& n) { MidiTools::strum (c, n, 1.0 / 16.0, false); }));
        strum.addItem ("Down (1/32)", run ("Strum", [] (auto& c, auto& n) { MidiTools::strum (c, n, 1.0 / 8.0, false); }));
        m.addSubMenu ("Strum chords", strum);
        m.addItem ("Fit to scale" + String (scale < 0 ? "  (choose a scale first)" : ""), scale >= 0, false,
                   run ("Fit to scale", [scale, root] (auto& c, auto& n) { MidiTools::fitToScale (c, n, scale, root); }));
        const bool anyChords = ! Chords::all (clip->edit).empty();
        m.addItem ("Fit to the chord track" + String (anyChords ? "" : "  (add chords above the arrangement first)"), anyChords, false,
                   run ("Fit to chords", [] (auto& c, auto& n) { Chords::fitNotes (c, n); }));
        PopupMenu vel;
        vel.addItem ("Louder (+10%)", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::louder, r); }));
        vel.addItem ("Softer (-10%)", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::softer, r); }));
        vel.addItem ("Compress (towards the average)", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::compress, r); }));
        vel.addItem ("All 100", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::fixed, r); }));
        vel.addItem ("Crescendo", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::rampUp, r); }));
        vel.addItem ("Decrescendo", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::rampDown, r); }));
        vel.addItem ("Randomise", run ("Velocity", [] (auto& c, auto& n) { Random r; MidiTools::velocities (c, n, MT::randomise, r); }));
        m.addSubMenu ("Velocity", vel);
        m.addSeparator();
        m.addItem ("Reverse", run ("Reverse", [] (auto& c, auto& n) { MidiTools::reverse (c, n); }));
        m.addItem ("Invert (flip pitches)", run ("Invert", [] (auto& c, auto& n) { MidiTools::invert (c, n); }));
        m.addItem ("Double speed", run ("Double speed", [] (auto& c, auto& n) { MidiTools::stretch (c, n, 0.5); }));
        m.addItem ("Half speed", run ("Half speed", [] (auto& c, auto& n) { MidiTools::stretch (c, n, 2.0); }));
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&toolsBtn));
    }

    void timerCallback() override
    {
        dropClipIfGone();
        if (clip != nullptr && evs.edit.getTransport().isPlaying())
            repaint();
    }

    void valueTreeChanged() override { repaint(); }

    void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override
    {
        // Notes can disappear via undo; drop stale selection pointers.
        if (clip == nullptr) return;
        auto& notes = clip->getSequence().getNotes();
        selected.removeIf ([&notes] (te::MidiNote* n) { return ! notes.contains (n); });
        repaint();
    }

    static constexpr int keyW = 64, velH = 64, toolbarH = 32;
    static constexpr float rowH = 13.0f;

    EditViewState& evs;
    te::MidiClip::Ptr clip;
    Array<te::MidiNote*> selected;
    std::map<te::MidiNote*, Orig> originals;
    te::MidiNote* anchor = nullptr;
    std::optional<Rectangle<float>> rubber;
    Mode mode = Mode::none;
    double ppb = 80.0, scrollX = 0.0, snap = 0.25, noteLen = 0.25;
    int paintPitch = -1;
    float scrollY = 0.0f;
    int velocity = 100, pressedKey = -1;

    ComboBox gridBox, laneBox, scaleBox, rootBox;
    ClipEnvelopes::Target envTarget;
    TextButton foldBtn { "Fold" };
    mutable std::vector<int> rows;
    mutable std::array<int, 128> rowOf {};
    mutable String rowsKey;
    TextButton quantizeBtn { "Quantize" }, toolsBtn { "Tools" }, paintBtn { "Draw" }, ghostBtn { "Ghosts" }, stepsBtn { "Steps" };
    StepGrid steps;
    Label title, hint;
};
