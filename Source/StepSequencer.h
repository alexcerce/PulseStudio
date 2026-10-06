#pragma once

#include "DrumRack.h"
#include "EditViewState.h"

/** A step grid for a MIDI clip, as on a drum machine: a row for each drum pad (or each pitch the clip uses), a
    column for each step. Click to put a hit in or take it out, drag to paint, Shift-click or the wheel for how
    hard it hits; right-click a row's name to fill, shift or clear it. */
class StepGrid : public Component,
                 private Timer
{
public:
    explicit StepGrid (EditViewState& e) : evs (e) { startTimerHz (20); }

    void setClip (te::MidiClip* c) { clip = c; scroll = 0; repaint(); }
    /** The length of one step in beats (1/16 = 0.25). */
    void setStep (double beats) { step = beats > 0.0 ? beats : 0.25; repaint(); }
    double stepBeats() const { return step; }

    //==============================================================================
    struct Row { int note; String name; };

    /** The rows: the pads of the track's Drum Rack that have a sound, top row the highest; without a Drum Rack, the
        pitches the clip uses (or the sixteen from C1 if it is empty). */
    std::vector<Row> rows() const
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

    /** The hit that starts in a step of a row, if there is one. */
    te::MidiNote* noteAt (int note, int stepIndex) const
    {
        if (clip == nullptr) return nullptr;
        const double from = contentStart() + stepIndex * step, to = from + step;
        for (auto* n : clip->getSequence().getNotes())
            if (n->getNoteNumber() == note && n->getStartBeat().inBeats() >= from - 1.0e-4 && n->getStartBeat().inBeats() < to - 1.0e-4) return n;
        return nullptr;
    }

    /** Puts a hit into a step (or changes how hard the one there hits). */
    void setHit (int note, int stepIndex, int velocity)
    {
        if (clip == nullptr || stepIndex < 0 || stepIndex >= numSteps()) return;
        if (auto* n = noteAt (note, stepIndex)) { n->setVelocity (jlimit (1, 127, velocity), &um()); return; }
        clip->getSequence().addNote (note, te::BeatPosition::fromBeats (contentStart() + stepIndex * step), te::BeatDuration::fromBeats (step * 0.9), jlimit (1, 127, velocity), 0, &um());
    }
    void clearHit (int note, int stepIndex)
    {
        while (auto* n = noteAt (note, stepIndex)) clip->getSequence().removeNote (*n, &um());
    }
    /** Click: a hit goes in where there is none, and comes out where there is one. */
    void toggle (int note, int stepIndex)
    {
        um().beginNewTransaction ("Step");
        if (noteAt (note, stepIndex) != nullptr) clearHit (note, stepIndex); else setHit (note, stepIndex, lastVelocity);
    }

    void clearRow (int note)
    {
        um().beginNewTransaction ("Clear row");
        for (int s = 0; s < numSteps(); ++s) clearHit (note, s);
    }
    /** A hit on every `every`th step, starting at `offset`. */
    void fillRow (int note, int every, int offset = 0)
    {
        um().beginNewTransaction ("Fill row");
        for (int s = 0; s < numSteps(); ++s) { clearHit (note, s); if (every > 0 && (s - offset) % every == 0 && s >= offset) setHit (note, s, lastVelocity); }
    }
    /** Moves a row's hits one step later (+1) or earlier (-1); what falls off one end comes back at the other. */
    void shiftRow (int note, int by)
    {
        um().beginNewTransaction ("Shift row");
        const int n = numSteps();
        std::vector<int> vel ((size_t) n, 0);
        for (int s = 0; s < n; ++s) if (auto* h = noteAt (note, s)) vel[(size_t) s] = h->getVelocity();
        for (int s = 0; s < n; ++s) clearHit (note, s);
        for (int s = 0; s < n; ++s) if (vel[(size_t) s] > 0) setHit (note, ((s + by) % n + n) % n, vel[(size_t) s]);
    }
    void randomiseRow (int note, float density)
    {
        um().beginNewTransaction ("Random row");
        Random r;
        for (int s = 0; s < numSteps(); ++s) { clearHit (note, s); if (r.nextFloat() < density) setHit (note, s, 70 + r.nextInt (50)); }
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
        g.setFont (FontOptions (jmin (12.0f, rh * 0.6f)));
        for (int ri = 0; ri < (int) rs.size(); ++ri)
        {
            const float y = top + (float) ri * rh;
            g.setColour (ri % 2 == 0 ? Theme::panel : Theme::bg2);
            g.fillRect (0.0f, y, (float) getWidth(), rh);
            g.setColour (ri == hoverRow ? Theme::text : Theme::muted);
            g.drawText (rs[(size_t) ri].name, 8, (int) y, labelW - 12, (int) rh, Justification::centredLeft, true);
            for (int s = firstStep(); s < n; ++s)
            {
                auto cell = cellRect (ri, s, rh);
                if (cell.getX() > (float) getWidth()) break;
                const bool beat = s % perBeat == 0;
                g.setColour (Theme::ink.withAlpha (s == playing ? 0.16f : (s / perBeat) % 2 == 0 ? 0.07f : 0.04f));
                g.fillRoundedRectangle (cell, 3.0f);
                if (auto* hit = noteAt (rs[(size_t) ri].note, s))
                {
                    const float v = (float) hit->getVelocity() / 127.0f;
                    g.setColour ((s == playing ? Colours::white : Theme::trackColour (ri)).withAlpha (0.35f + 0.65f * v));
                    g.fillRoundedRectangle (cell.reduced (1.0f), 3.0f);
                    // how hard it hits, as a bar inside the cell
                    g.setColour (Colours::black.withAlpha (0.25f));
                    g.fillRect (cell.reduced (3.0f).withHeight ((cell.getHeight() - 6.0f) * (1.0f - v)));   // the softer the hit, the more of the cell is shaded from the top
                }
                else if (beat)
                {
                    g.setColour (Theme::ink.withAlpha (0.10f));
                    g.fillEllipse (cell.getCentreX() - 1.5f, cell.getCentreY() - 1.5f, 3.0f, 3.0f);
                }
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
        if (e.position.x < (float) labelW || e.mods.isPopupMenu()) { if (e.mods.isPopupMenu() || e.position.x < (float) labelW) showRowMenu (note, rs[(size_t) ri].name); return; }
        const int s = stepAt (e.position.x);
        if (s < 0) return;
        um().beginNewTransaction ("Steps");
        if (auto* hit = noteAt (note, s); hit != nullptr && e.mods.isShiftDown())
        {
            // Shift-click goes round: normal, accent, ghost
            const int v = hit->getVelocity();
            lastVelocity = v < 80 ? 100 : v < 115 ? 127 : 60;
            hit->setVelocity (lastVelocity, &um());
            painting = 0;
        }
        else
        {
            painting = noteAt (note, s) != nullptr ? -1 : 1;   // the first cell decides whether the drag puts hits in or takes them out
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
        if (painting > 0) { if (noteAt (rs[(size_t) dragRow].note, s) == nullptr) setHit (rs[(size_t) dragRow].note, s, lastVelocity); }
        else clearHit (rs[(size_t) dragRow].note, s);
        repaint();
    }

    void mouseUp (const MouseEvent&) override { painting = 0; dragRow = -1; }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        const auto rs = rows();
        const int ri = rowAt (e.position.y), s = stepAt (e.position.x);
        if (clip != nullptr && ri >= 0 && ri < (int) rs.size() && s >= 0)
            if (auto* hit = noteAt (rs[(size_t) ri].note, s))
            {
                // over a hit: how hard it hits
                um().beginNewTransaction ("Step velocity");
                lastVelocity = jlimit (1, 127, hit->getVelocity() + (w.deltaY > 0 ? 8 : -8));
                hit->setVelocity (lastVelocity, &um());
                repaint();
                return;
            }
        // otherwise: along the steps
        scroll = jlimit (0, jmax (0, numSteps() - 8), scroll + (w.deltaY > 0 || w.deltaX > 0 ? -4 : 4));
        repaint();
    }

private:
    static constexpr int labelW = 150, top = 18;
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

    void showRowMenu (int note, const String& name)
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
        m.addItem ("Clear this row", [this, note] { clearRow (note); repaint(); });
        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void timerCallback() override
    {
        if (clip != nullptr && (clip->getTrack() == nullptr || ! clip->state.getParent().isValid())) clip = nullptr;   // taken out of the project behind our back
        if (! isShowing()) return;
        // redrawn only when something it shows has changed: the step being played, or the notes (an undo, an edit elsewhere)
        double sig = playingStep() * 7919.0 + scroll;
        if (clip != nullptr)
            for (auto* n : clip->getSequence().getNotes()) sig += n->getNoteNumber() * 31.0 + n->getStartBeat().inBeats() * 101.0 + n->getVelocity();
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    EditViewState& evs;
    te::MidiClip::Ptr clip;
    double step = 0.25;
    int scroll = 0, hoverRow = -1, dragRow = -1, painting = 0, lastVelocity = 100;
    double lastSig = -1.0;
};
