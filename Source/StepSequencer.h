#pragma once

#include "DrumRack.h"
#include "EditViewState.h"

/** A step grid for a MIDI clip, as on a drum machine: a row for each drum pad (or each pitch the clip uses), a
    column for each step. Click to put a hit in or take it out, drag to paint, Shift-click or the wheel for how
    hard it hits; right-click a row's name to fill, shift or clear it.

    A row can be more than a line of notes. Give a step a chance of playing, or repeats inside the step (a
    ratchet), or give the row a length of its own (seven steps under a kick of sixteen: polymetre), and the row
    becomes a pattern row: its steps are kept with the clip, and its notes are written out from them over the
    whole length of the clip, every repetition with its own throw of the dice. The notes are ordinary notes (they
    play, export and show in the piano roll like any other); the pattern is what the grid edits. */
class StepGrid : public Component,
                 public SettableTooltipClient,
                 private Timer
{
public:
    explicit StepGrid (EditViewState& e) : evs (e) { setTooltip (helpText()); startTimerHz (20); }

    void setClip (te::MidiClip* c) { clip = c; scroll = 0; lastNumSteps = numSteps(); repaint(); }
    /** The length of one step in beats (1/16 = 0.25). */
    void setStep (double beats) { step = beats > 0.0 ? beats : 0.25; lastNumSteps = numSteps(); repaint(); }
    double stepBeats() const { return step; }

    //==============================================================================
    struct Row { int note; String name; };

    /** The rows: the pads of the track's Drum Rack that have a sound, top row the highest; without a Drum Rack, the
        pitches the clip uses (or the sixteen from C1 if it is empty). */
    std::vector<Row> rows() const { return rowsFor (clip.get()); }
    static std::vector<Row> rowsFor (te::MidiClip* clip)
    {
        std::vector<Row> out;
        if (clip == nullptr) return out;
        auto* t = dynamic_cast<te::AudioTrack*> (clip->getTrack());
        if (auto* rack = t != nullptr ? DrumRack::rackOf (DrumRack::findOnTrack (*t)) : nullptr)
        {
            for (int slot : DrumRack::usedPads (*rack))
            {
                auto n = DrumRack::padName (*rack, slot);
                out.push_back ({ DrumRack::noteOfPad (*rack, slot), n.isNotEmpty() ? n.upToFirstOccurrenceOf (" (synth)", false, false) : DrumRack::defaultPadLabel (slot) });
            }
        }
        std::set<int> have;
        for (auto& r : out) have.insert (r.note);
        for (auto* n : clip->getSequence().getNotes())   // notes with no pad of their own still get a row, so nothing is hidden
            if (have.insert (n->getNoteNumber()).second) out.push_back ({ n->getNoteNumber(), MidiMessage::getMidiNoteName (n->getNoteNumber(), true, true, 3) });
        if (out.empty())
            for (int n = 36; n < 52; ++n) out.push_back ({ n, MidiMessage::getMidiNoteName (n, true, true, 3) });
        std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.note > b.note; });
        return out;
    }

    int numSteps() const { return clip == nullptr ? 0 : jmax (1, roundToInt (clipLengthBeats() / step)); }

    /** The note that starts in a step of a row, if there is one. */
    te::MidiNote* noteAt (int note, int stepIndex) const
    {
        if (clip == nullptr) return nullptr;
        const double from = contentStart() + stepIndex * step, to = from + step;
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == note && n->getStartBeat().inBeats() >= from - 1.0e-4 && n->getStartBeat().inBeats() < to - 1.0e-4) return n;
        return nullptr;
    }

    //==============================================================================
    // Pattern rows
    struct PStep { int vel = 0, chance = 100, ratchet = 1; };
    struct Pattern
    {
        std::vector<PStep> steps;
        bool valid = false, whole = true;   // whole: as long as the clip (no length of its own)
        int length() const { return (int) steps.size(); }
    };

    /** The pattern of a row, if it is a pattern row (and was made on the grid's present step size). */
    Pattern pattern (int note) const
    {
        Pattern p;
        if (clip == nullptr) return p;
        auto r = clip->state.getChildWithName (rowsID).getChildWithProperty (noteID, note);
        if (! r.isValid() || std::abs ((double) r[stepID] - step) > 1.0e-6) return p;
        for (auto& tok : StringArray::fromTokens (r[stepsID].toString(), ",", ""))
        {
            const auto v = StringArray::fromTokens (tok, ":", "");
            p.steps.push_back ({ jlimit (0, 127, v[0].getIntValue()), v.size() > 1 ? jlimit (0, 100, v[1].getIntValue()) : 100, v.size() > 2 ? jlimit (1, maxRatchet, v[2].getIntValue()) : 1 });
        }
        p.whole = (bool) r.getProperty (wholeID, true);
        if (p.whole) p.steps.resize ((size_t) numSteps());
        p.valid = ! p.steps.empty();
        return p;
    }
    bool isPatternRow (int note) const { return pattern (note).valid; }
    /** How many steps of a row are its own (the rest of the clip repeats them). */
    int rowLength (int note) const { const auto p = pattern (note); return p.valid ? p.length() : numSteps(); }

    /** How hard the step of a row hits (0: no hit). For a pattern row this is what the pattern says, whether or
        not the dice let it play this time round. */
    int velocityAt (int note, int stepIndex) const
    {
        if (const auto p = pattern (note); p.valid) return p.steps[(size_t) (stepIndex % p.length())].vel;
        if (auto* n = noteAt (note, stepIndex)) return n->getVelocity();
        return 0;
    }
    int chanceAt (int note, int stepIndex) const  { const auto p = pattern (note); return p.valid ? p.steps[(size_t) (stepIndex % p.length())].chance : 100; }
    int ratchetAt (int note, int stepIndex) const { const auto p = pattern (note); return p.valid ? p.steps[(size_t) (stepIndex % p.length())].ratchet : 1; }

    /** Puts a hit into a step (or changes how hard the one there hits). */
    void setHit (int note, int stepIndex, int velocity)
    {
        if (clip == nullptr || stepIndex < 0 || stepIndex >= numSteps()) return;
        velocity = jlimit (1, 127, velocity);
        if (auto p = pattern (note); p.valid) { p.steps[(size_t) (stepIndex % p.length())].vel = velocity; store (note, p); return; }
        forgetPattern (note);   // (one made on another step size: the notes are the truth again)
        if (auto* n = noteAt (note, stepIndex)) { n->setVelocity (velocity, &um()); return; }
        clip->getSequence().addNote (note, te::BeatPosition::fromBeats (contentStart() + stepIndex * step), te::BeatDuration::fromBeats (step * 0.9), velocity, 0, &um());
    }
    void clearHit (int note, int stepIndex)
    {
        if (clip == nullptr || stepIndex < 0 || stepIndex >= numSteps()) return;
        if (auto p = pattern (note); p.valid) { p.steps[(size_t) (stepIndex % p.length())].vel = 0; store (note, p); return; }
        forgetPattern (note);
        while (auto* n = noteAt (note, stepIndex)) clip->getSequence().removeNote (*n, &um());
    }
    /** Click: a hit goes in where there is none, and comes out where there is one. */
    void toggle (int note, int stepIndex)
    {
        um().beginNewTransaction ("Step");
        if (velocityAt (note, stepIndex) > 0) clearHit (note, stepIndex); else setHit (note, stepIndex, lastVelocity);
    }

    /** How likely a step is to play, in percent. Each time the row comes round, the dice are thrown again. */
    void setChance (int note, int stepIndex, int percent)
    {
        if (clip == nullptr || stepIndex < 0 || stepIndex >= numSteps()) return;
        um().beginNewTransaction ("Step chance");
        auto p = patternOrCapture (note);
        p.steps[(size_t) (stepIndex % p.length())].chance = jlimit (0, 100, percent);
        store (note, p);
    }
    /** How many times a step plays inside its own length (1: once, as usual). */
    void setRatchet (int note, int stepIndex, int times)
    {
        if (clip == nullptr || stepIndex < 0 || stepIndex >= numSteps()) return;
        um().beginNewTransaction ("Step repeats");
        auto p = patternOrCapture (note);
        p.steps[(size_t) (stepIndex % p.length())].ratchet = jlimit (1, maxRatchet, times);
        store (note, p);
    }
    /** Gives a row a length of its own, in steps: those steps repeat over the whole clip, against the other rows.
        0 (or the clip's own number of steps) makes it as long as the clip again, keeping what was repeated. */
    void setRowLength (int note, int steps)
    {
        if (clip == nullptr) return;
        um().beginNewTransaction ("Row length");
        auto p = patternOrCapture (note);
        const auto old = p.steps;
        p.whole = steps <= 0 || steps >= numSteps();
        p.steps.resize ((size_t) (p.whole ? numSteps() : steps));
        for (size_t i = old.size(); i < p.steps.size(); ++i) p.steps[i] = old[i % old.size()];   // growing: what was heard stays
        store (note, p);
    }
    /** Throws the dice again for every step that has a chance. */
    void rollAgain()
    {
        if (clip == nullptr) return;
        um().beginNewTransaction ("Roll again");
        auto all = clip->state.getChildWithName (rowsID);
        if (! all.isValid()) return;
        all.setProperty (seedID, (int) all.getProperty (seedID, 1) + 1, &um());
        renderAll();
    }
    /** A pattern row becomes a plain line of notes again (the notes stay as they are). */
    void makePlain (int note)
    {
        um().beginNewTransaction ("Plain row");
        forgetPattern (note);
    }

    void clearRow (int note)
    {
        um().beginNewTransaction ("Clear row");
        setRowVelocities (note, std::vector<int> ((size_t) rowLength (note), 0));
    }
    /** A hit on every `every`th step, starting at `offset`. */
    void fillRow (int note, int every, int offset = 0)
    {
        um().beginNewTransaction ("Fill row");
        std::vector<int> vel ((size_t) rowLength (note), 0);
        for (int s = 0; s < (int) vel.size(); ++s) if (every > 0 && (s - offset) % every == 0 && s >= offset) vel[(size_t) s] = lastVelocity;
        setRowVelocities (note, vel);
    }
    /** Moves a row's hits one step later (+1) or earlier (-1); what falls off one end comes back at the other. */
    void shiftRow (int note, int by)
    {
        um().beginNewTransaction ("Shift row");
        const int n = rowLength (note);
        if (auto p = pattern (note); p.valid)
        {
            auto moved = p.steps;
            for (int s = 0; s < n; ++s) moved[(size_t) (((s + by) % n + n) % n)] = p.steps[(size_t) s];   // chance and repeats travel with their hit
            p.steps = moved;
            store (note, p);
            return;
        }
        std::vector<int> vel ((size_t) n, 0), moved ((size_t) n, 0);
        for (int s = 0; s < n; ++s) vel[(size_t) s] = velocityAt (note, s);
        for (int s = 0; s < n; ++s) moved[(size_t) (((s + by) % n + n) % n)] = vel[(size_t) s];
        setRowVelocities (note, moved);
    }
    void randomiseRow (int note, float density)
    {
        um().beginNewTransaction ("Random row");
        Random r;
        std::vector<int> vel ((size_t) rowLength (note), 0);
        for (auto& v : vel) if (r.nextFloat() < density) v = 70 + r.nextInt (50);
        setRowVelocities (note, vel);
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg);
        if (clip == nullptr)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (13.0f));
            g.drawText ("No clip - double-click a MIDI clip in the arrangement", getLocalBounds(), Justification::centred);
            return;
        }
        const auto rs = rows();
        const int n = numSteps();
        const float rh = rowHeight ((int) rs.size());
        const int perBeat = jmax (1, roundToInt (1.0 / step)), perBar = perBeat * jmax (1, roundToInt (EngineHelpers::beatsPerBar (clip->edit)));
        const int playing = playingStep();
        for (int ri = 0; ri < (int) rs.size(); ++ri)
        {
            const float y = top + (float) ri * rh;
            const int note = rs[(size_t) ri].note;
            const auto pat = pattern (note);
            const int own = pat.valid ? pat.length() : n;
            g.setColour (ri % 2 == 0 ? Theme::panel : Theme::bg2);
            g.fillRect (0.0f, y, (float) getWidth(), rh);
            g.setFont (FontOptions (jmin (12.0f, rh * 0.6f)));
            g.setColour (ri == hoverRow ? Theme::text : Theme::muted);
            g.drawText (rs[(size_t) ri].name, 8, (int) y, labelW - (own < n ? 40 : 12), (int) rh, Justification::centredLeft, true);
            if (own < n)
            {
                // a row with a length of its own says so
                g.setColour (Theme::accent);
                g.setFont (FontOptions (jmin (11.0f, rh * 0.55f), Font::bold));
                g.drawText (String (own), labelW - 38, (int) y, 30, (int) rh, Justification::centredRight);
            }
            for (int s = firstStep(); s < n; ++s)
            {
                auto cell = cellRect (ri, s, rh);
                if (cell.getX() > (float) getWidth()) break;
                const bool beat = s % perBeat == 0, repeat = s >= own;
                const float dim = repeat ? 0.55f : 1.0f;
                g.setColour (Theme::ink.withAlpha (s == playing ? 0.16f : (s / perBeat) % 2 == 0 ? 0.07f : 0.04f));
                g.fillRoundedRectangle (cell, 3.0f);
                const PStep st = pat.valid ? pat.steps[(size_t) (s % own)] : PStep { 0, 100, 1 };
                auto* sounding = noteAt (note, s);
                const int velocity = pat.valid ? st.vel : (sounding != nullptr ? sounding->getVelocity() : 0);
                if (velocity > 0)
                {
                    const float v = (float) velocity / 127.0f;
                    const auto colour = s == playing ? Colours::white : Theme::trackColour (ri);
                    if (sounding != nullptr)
                    {
                        g.setColour (colour.withAlpha ((0.35f + 0.65f * v) * dim));
                        g.fillRoundedRectangle (cell.reduced (1.0f), 3.0f);
                        // how hard it hits, as a bar inside the cell
                        g.setColour (Colours::black.withAlpha (0.25f));
                        g.fillRect (cell.reduced (3.0f).withHeight ((cell.getHeight() - 6.0f) * (1.0f - v)));   // the softer the hit, the more of the cell is shaded from the top
                    }
                    else
                    {
                        // in the pattern, but the dice kept it silent this time round
                        g.setColour (colour.withAlpha (0.55f * dim));
                        g.drawRoundedRectangle (cell.reduced (1.5f), 3.0f, 1.0f);
                    }
                    if (st.ratchet > 1)
                    {
                        // repeats: the cell cut into as many parts
                        g.setColour (Theme::bg.withAlpha (0.85f));
                        for (int k = 1; k < st.ratchet; ++k)
                            g.fillRect (cell.getX() + cell.getWidth() * (float) k / (float) st.ratchet - 0.75f, cell.getY() + 2.0f, 1.5f, cell.getHeight() - 4.0f);
                    }
                    if (st.chance < 100 && ! repeat)
                    {
                        g.setColour ((sounding != nullptr ? Colours::black : Theme::text).withAlpha (0.8f));
                        g.setFont (FontOptions (jmin (9.5f, rh * 0.5f), Font::bold));
                        g.drawText (String (st.chance), cell.toNearestInt(), Justification::centred);
                    }
                }
                else if (beat)
                {
                    g.setColour (Theme::ink.withAlpha (0.10f));
                    g.fillEllipse (cell.getCentreX() - 1.5f, cell.getCentreY() - 1.5f, 3.0f, 3.0f);
                }
            }
            if (own < n && own > firstStep())
            {
                // where the row's own steps end and the repeats begin
                const auto last = cellRect (ri, own - 1, rh);
                g.setColour (Theme::accent);
                g.fillRect (last.getRight() + 0.5f, last.getY() - 1.0f, 2.0f, last.getHeight() + 2.0f);
            }
        }
        // bar numbers along the top
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        for (int s = firstStep(); s < n; s += 1)
            if (s % perBar == 0) g.drawText (String (s / perBar + 1), (int) cellRect (0, s, rh).getX(), 2, 40, top - 4, Justification::centredLeft);
        g.setColour (Theme::bg);
        g.fillRect (0, 0, labelW, top);
        g.setColour (Theme::muted);
        g.drawText (String (n) + " steps", 8, 2, labelW - 12, top - 4, Justification::centredLeft);
    }

    void mouseMove (const MouseEvent& e) override { const int r = rowAt (e.position.y); if (r != hoverRow) { hoverRow = r; repaint(); } }
    void mouseExit (const MouseEvent&) override { hoverRow = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        const auto rs = rows();
        const int ri = rowAt (e.position.y);
        if (clip == nullptr || ri < 0 || ri >= (int) rs.size()) return;
        const int note = rs[(size_t) ri].note;
        const int s = e.position.x < (float) labelW ? -1 : stepAt (e.position.x);
        if (e.mods.isPopupMenu() && s >= 0 && velocityAt (note, s) > 0) { showStepMenu (note, s, rs[(size_t) ri].name); return; }
        if (e.position.x < (float) labelW || e.mods.isPopupMenu()) { showRowMenu (note, rs[(size_t) ri].name); return; }
        if (s < 0) return;
        um().beginNewTransaction ("Steps");
        if (const int v = velocityAt (note, s); v > 0 && e.mods.isShiftDown())
        {
            // Shift-click goes round: normal, accent, ghost
            lastVelocity = v < 80 ? 100 : v < 115 ? 127 : 60;
            setHit (note, s, lastVelocity);
            painting = 0;
        }
        else
        {
            painting = velocityAt (note, s) > 0 ? -1 : 1;   // the first cell decides whether the drag puts hits in or takes them out
            if (painting > 0) setHit (note, s, lastVelocity); else clearHit (note, s);
        }
        dragRow = ri;
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const auto rs = rows();
        if (clip == nullptr || painting == 0 || dragRow < 0 || dragRow >= (int) rs.size()) return;
        const int s = stepAt (e.position.x);
        if (s < 0) return;
        const int note = rs[(size_t) dragRow].note;
        if (painting > 0) { if (velocityAt (note, s) == 0) setHit (note, s, lastVelocity); }
        else if (velocityAt (note, s) > 0) clearHit (note, s);
        repaint();
    }

    void mouseUp (const MouseEvent&) override { painting = 0; dragRow = -1; }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const auto rs = rows();
        const int ri = rowAt (e.position.y), s = stepAt (e.position.x);
        if (clip != nullptr && ri >= 0 && ri < (int) rs.size() && s >= 0)
            if (const int note = rs[(size_t) ri].note; velocityAt (note, s) > 0)
            {
                const int up = w.deltaY > 0 ? 1 : -1;
                // over a hit: how hard it hits; with Alt how likely it is to play, with Ctrl how often it repeats
                if (e.mods.isAltDown())          setChance (note, s, chanceAt (note, s) + up * 10);
                else if (e.mods.isCommandDown()) setRatchet (note, s, ratchetAt (note, s) + up);
                else
                {
                    um().beginNewTransaction ("Step velocity");
                    lastVelocity = jlimit (1, 127, velocityAt (note, s) + up * 8);
                    setHit (note, s, lastVelocity);
                }
                repaint();
                return;
            }
        // otherwise: along the steps
        scroll = jlimit (0, jmax (0, numSteps() - 8), scroll + (w.deltaY > 0 || w.deltaX > 0 ? -4 : 4));
        repaint();
    }

    /** Takes every pattern row out of a clip (its notes stay): for whatever else writes the clip's notes itself. */
    static void forgetAllPatterns (te::MidiClip& c, juce::UndoManager* um)
    {
        if (auto all = c.state.getChildWithName (rowsID); all.isValid()) c.state.removeChild (all, um);
    }

    /** (for the help line) */
    static String helpText()
    {
        return "Step grid: click puts a hit in or takes it out, drag paints. Wheel on a hit: how hard. Alt + wheel: its chance of playing. "
               "Ctrl + wheel: repeats inside the step. Right-click a hit for the same; right-click a row's name to fill it or give it a length of its own";
    }

private:
    static constexpr int labelW = 150, top = 18, maxRatchet = 8;
    static inline const Identifier rowsID { "STEPROWS" }, rowID { "ROW" }, noteID { "note" }, stepsID { "steps" }, stepID { "step" }, wholeID { "whole" }, seedID { "seed" };

    juce::UndoManager& um() const { return evs.edit.getUndoManager(); }
    double contentStart() const    { return clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats(); }
    double clipLengthBeats() const { return clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats() - contentStart(); }
    float rowHeight (int numRows) const { return jlimit (16.0f, 34.0f, (float) (getHeight() - top) / (float) jmax (1, numRows)); }
    float cellWidth() const { return jlimit (18.0f, 44.0f, (float) (getWidth() - labelW) / (float) jmax (1, jmin (numSteps(), 32))); }
    int firstStep() const { return scroll; }
    Rectangle<float> cellRect (int row, int s, float rh) const
    {
        const int perBeat = jmax (1, roundToInt (1.0 / step));
        // a little more room between beats, so the grid reads in fours
        return Rectangle<float> ((float) labelW + (float) (s - scroll) * cellWidth() + (float) ((s / perBeat) - (scroll / perBeat)) * 4.0f, (float) top + (float) row * rh, cellWidth(), rh).reduced (1.5f, 2.0f);
    }
    int rowAt (float y) const { const auto n = (int) rows().size(); const int r = (int) ((y - (float) top) / rowHeight (n)); return y >= (float) top && r < n ? r : -1; }
    int stepAt (float x) const
    {
        const float rh = rowHeight ((int) rows().size());
        for (int s = scroll; s < numSteps(); ++s)
        {
            auto c = cellRect (0, s, rh).expanded (1.5f, 0.0f);
            if (x >= c.getX() && x < c.getRight()) return s;
            if (c.getX() > x) break;
        }
        return -1;
    }
    /** The step the song is on right now (-1 when it is not playing inside the clip). */
    int playingStep() const
    {
        auto& tr = evs.edit.getTransport();
        if (clip == nullptr || ! tr.isPlaying()) return -1;
        const auto now = tr.getPosition();
        if (now < clip->getPosition().getStart() || now >= clip->getPosition().getEnd()) return -1;
        return (int) ((clip->getContentBeatAtTime (now).inBeats() - contentStart()) / step);
    }

    //==============================================================================
    /** The row's pattern; a plain row is turned into one from the notes it has now. */
    Pattern patternOrCapture (int note)
    {
        if (auto p = pattern (note); p.valid) return p;
        Pattern p;
        p.steps.resize ((size_t) numSteps());
        for (int s = 0; s < numSteps(); ++s) if (auto* n = noteAt (note, s)) p.steps[(size_t) s].vel = n->getVelocity();
        p.valid = p.whole = true;
        return p;
    }

    /** Keeps a row's pattern with the clip and writes its notes out again. */
    void store (int note, const Pattern& p)
    {
        auto all = clip->state.getChildWithName (rowsID);
        if (! all.isValid()) { all = ValueTree (rowsID); all.setProperty (seedID, 1, nullptr); clip->state.addChild (all, -1, &um()); }
        auto r = all.getChildWithProperty (noteID, note);
        if (! r.isValid()) { r = ValueTree (rowID); r.setProperty (noteID, note, nullptr); all.addChild (r, -1, &um()); }
        StringArray toks;
        for (auto& st : p.steps) toks.add (String (st.vel) + ":" + String (st.chance) + ":" + String (st.ratchet));
        r.setProperty (stepsID, toks.joinIntoString (","), &um());
        r.setProperty (stepID, step, &um());
        r.setProperty (wholeID, p.whole, &um());
        render (note, p);
    }

    void forgetPattern (int note)
    {
        if (clip == nullptr) return;
        auto all = clip->state.getChildWithName (rowsID);
        if (auto r = all.getChildWithProperty (noteID, note); r.isValid()) all.removeChild (r, &um());
    }

    /** Whether a step plays this time round: the same answer every time it is asked, until the dice are thrown again. */
    static bool plays (int seed, int note, int stepIndex, int chance)
    {
        if (chance >= 100) return true;
        if (chance <= 0) return false;
        auto h = (uint32) seed * 2654435761u ^ (uint32) (note + 1) * 40503u ^ (uint32) (stepIndex + 1) * 2246822519u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        return (int) (h % 100u) < chance;
    }

    /** Writes a pattern row's notes over the whole clip: its steps again and again, each repetition with its own
        throw of the dice, each hit as many times as it repeats. */
    void render (int note, const Pattern& p)
    {
        auto& seq = clip->getSequence();
        const int n = numSteps(), own = p.length();
        const double from = contentStart(), to = from + n * step;
        Array<te::MidiNote*> old;
        for (auto* m : seq.getNotes())
            if (m->getNoteNumber() == note && m->getStartBeat().inBeats() >= from - 1.0e-4 && m->getStartBeat().inBeats() < to - 1.0e-4) old.add (m);
        for (auto* m : old) seq.removeNote (*m, &um());
        if (own <= 0) return;
        const int seed = clip->state.getChildWithName (rowsID).getProperty (seedID, 1);
        for (int s = 0; s < n; ++s)
        {
            const auto& st = p.steps[(size_t) (s % own)];
            if (st.vel <= 0 || ! plays (seed, note, s, st.chance)) continue;
            const int times = jlimit (1, maxRatchet, st.ratchet);
            for (int k = 0; k < times; ++k)
                seq.addNote (note, te::BeatPosition::fromBeats (from + ((double) s + (double) k / (double) times) * step), te::BeatDuration::fromBeats (step / (double) times * 0.9), st.vel, 0, &um());
        }
    }

    void renderAll()
    {
        auto all = clip->state.getChildWithName (rowsID);
        for (int i = 0; i < all.getNumChildren(); ++i)
        {
            const int note = all.getChild (i)[noteID];
            if (const auto p = pattern (note); p.valid) render (note, p);
        }
    }

    /** Sets which steps of a row have a hit (for a pattern row its own steps, for a plain one all of them). */
    void setRowVelocities (int note, const std::vector<int>& vel)
    {
        if (clip == nullptr) return;
        if (auto p = pattern (note); p.valid)
        {
            for (int s = 0; s < p.length() && s < (int) vel.size(); ++s) p.steps[(size_t) s].vel = vel[(size_t) s];
            store (note, p);
            return;
        }
        forgetPattern (note);
        for (int s = 0; s < numSteps() && s < (int) vel.size(); ++s)
        {
            while (auto* n = noteAt (note, s)) clip->getSequence().removeNote (*n, &um());
            if (vel[(size_t) s] > 0) clip->getSequence().addNote (note, te::BeatPosition::fromBeats (contentStart() + s * step), te::BeatDuration::fromBeats (step * 0.9), jlimit (1, 127, vel[(size_t) s]), 0, &um());
        }
    }

    //==============================================================================
    PopupMenu rowMenu (int note, const String& name)
    {
        PopupMenu m;
        m.addSectionHeader (name);
        const int perBeat = jmax (1, roundToInt (1.0 / step));
        m.addItem ("A hit on every step", [this, note] { fillRow (note, 1); repaint(); });
        m.addItem ("On every second step", [this, note] { fillRow (note, 2); repaint(); });
        m.addItem ("On every beat", [this, note, perBeat] { fillRow (note, perBeat); repaint(); });
        m.addItem ("On the off-beats", [this, note, perBeat] { fillRow (note, perBeat, perBeat / 2); repaint(); });
        m.addItem ("On beats 2 and 4", [this, note, perBeat] { fillRow (note, perBeat * 2, perBeat); repaint(); });
        m.addSeparator();
        m.addItem ("Move one step later", [this, note] { shiftRow (note, 1); repaint(); });
        m.addItem ("Move one step earlier", [this, note] { shiftRow (note, -1); repaint(); });
        m.addItem ("Scatter hits at random", [this, note] { randomiseRow (note, 0.3f); repaint(); });
        m.addSeparator();
        // a length of its own: the row goes round against the others
        PopupMenu len;
        const int own = rowLength (note), n = numSteps();
        len.addItem ("As long as the clip (" + String (n) + " steps)", true, own >= n, [this, note] { setRowLength (note, 0); repaint(); });
        len.addSeparator();
        for (int steps : { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 28, 32, 48 })
            if (steps < n) len.addItem (String (steps) + " steps", true, own == steps, [this, note, steps] { setRowLength (note, steps); repaint(); });
        m.addSubMenu ("Row length", len);
        m.addItem ("Throw the dice again (every step with a chance)", clip != nullptr && clip->state.getChildWithName (rowsID).isValid(), false, [this] { rollAgain(); repaint(); });
        m.addItem ("Back to plain notes (forget chances, repeats and length)", isPatternRow (note), false, [this, note] { makePlain (note); repaint(); });
        m.addSeparator();
        m.addItem ("Clear this row", [this, note] { clearRow (note); repaint(); });
        return m;
    }

    void showRowMenu (int note, const String& name)
    {
        auto m = rowMenu (note, name);
        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    /** Right-click on a hit: its chance and its repeats. */
    void showStepMenu (int note, int s, const String& name)
    {
        PopupMenu m;
        m.addSectionHeader (name + ", step " + String (s % rowLength (note) + 1));
        PopupMenu chance, repeats;
        for (int pc : { 100, 90, 75, 66, 50, 33, 25, 10 })
            chance.addItem (pc == 100 ? String ("Always") : String (pc) + "%", true, chanceAt (note, s) == pc, [this, note, s, pc] { setChance (note, s, pc); repaint(); });
        for (int r : { 1, 2, 3, 4, 6, 8 })
            repeats.addItem (r == 1 ? String ("Once") : String (r) + " times", true, ratchetAt (note, s) == r, [this, note, s, r] { setRatchet (note, s, r); repaint(); });
        m.addSubMenu ("Chance of playing: " + (chanceAt (note, s) >= 100 ? String ("always") : String (chanceAt (note, s)) + "%"), chance);
        m.addSubMenu ("Repeats inside the step: " + String (ratchetAt (note, s)), repeats);
        m.addSeparator();
        m.addItem ("Accent", [this, note, s] { um().beginNewTransaction ("Step velocity"); setHit (note, s, 127); repaint(); });
        m.addItem ("Normal", [this, note, s] { um().beginNewTransaction ("Step velocity"); setHit (note, s, 100); repaint(); });
        m.addItem ("Ghost", [this, note, s] { um().beginNewTransaction ("Step velocity"); setHit (note, s, 60); repaint(); });
        m.addItem ("Take the hit out", [this, note, s] { um().beginNewTransaction ("Step"); clearHit (note, s); repaint(); });
        m.addSeparator();
        m.addSubMenu ("The row", rowMenu (note, name));
        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void timerCallback() override
    {
        if (clip != nullptr && (clip->getTrack() == nullptr || ! clip->state.getParent().isValid())) clip = nullptr;   // taken out of the project behind our back
        if (! isShowing()) return;
        // a clip made longer or shorter: the pattern rows are written out over its new length
        if (clip != nullptr)
        {
            if (const int n = numSteps(); n != lastNumSteps)
            {
                if (lastNumSteps > 0 && clip->state.getChildWithName (rowsID).getNumChildren() > 0) renderAll();
                lastNumSteps = n;
            }
        }
        // redrawn only when something it shows has changed: the step being played, the notes (an undo, an edit elsewhere) or the patterns
        double sig = playingStep() * 7919.0 + scroll;
        if (clip != nullptr)
        {
            for (auto* n : clip->getSequence().getNotes()) sig += n->getNoteNumber() * 31.0 + n->getStartBeat().inBeats() * 101.0 + n->getVelocity();
            for (auto r : clip->state.getChildWithName (rowsID)) sig += (double) r[stepsID].toString().hashCode();
        }
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    EditViewState& evs;
    te::MidiClip::Ptr clip;
    double step = 0.25;
    int scroll = 0, hoverRow = -1, dragRow = -1, painting = 0, lastVelocity = 100, lastNumSteps = 0;
    double lastSig = -1.0;
};
