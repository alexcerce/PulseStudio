#pragma once

#include "Common.h"
#include "Scales.h"

/** Note-editing tools for the piano roll (all undoable). Each works on the given notes, or on every
    note of the clip when none are given. */
namespace MidiTools
{
    using Notes = Array<te::MidiNote*>;

    inline Notes target (te::MidiClip& c, const Notes& sel)
    {
        if (! sel.isEmpty()) return sel;
        Notes all;
        for (auto n : c.getSequence().getNotes()) all.add (n);
        return all;
    }

    inline Notes byStart (Notes n)
    {
        std::stable_sort (n.begin(), n.end(), [] (auto* a, auto* b)
        {
            return a->getStartBeat() != b->getStartBeat() ? a->getStartBeat() < b->getStartBeat() : a->getNoteNumber() < b->getNoteNumber();
        });
        return n;
    }

    /** Random timing (± beats) and velocity (± amount) changes. */
    inline void humanize (te::MidiClip& c, const Notes& sel, double timingBeats, int velocityAmount, Random& rng)
    {
        auto* um = &c.edit.getUndoManager();
        for (auto n : target (c, sel))
        {
            const double shift = (rng.nextDouble() * 2.0 - 1.0) * timingBeats;
            n->setStartAndLength (te::BeatPosition::fromBeats (jmax (0.0, n->getStartBeat().inBeats() + shift)), n->getLengthBeats(), um);
            n->setVelocity (jlimit (1, 127, n->getVelocity() + rng.nextInt ({ -velocityAmount, velocityAmount + 1 })), um);
        }
    }

    /** Each note lasts until the next note starts (the last one keeps its length). */
    inline void legato (te::MidiClip& c, const Notes& sel)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = byStart (target (c, sel));
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto start = notes[i]->getStartBeat();
            for (int j = i + 1; j < notes.size(); ++j)
                if (notes[j]->getStartBeat().inBeats() > start.inBeats() + 1.0e-6)
                {
                    notes[i]->setStartAndLength (start, notes[j]->getStartBeat() - start, um);
                    break;
                }
        }
    }

    /** Spreads each chord's notes in time (low to high when `up`), like strumming a guitar. */
    inline void strum (te::MidiClip& c, const Notes& sel, double stepBeats, bool up)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = byStart (target (c, sel));
        for (int i = 0; i < notes.size();)
        {
            Notes chord;
            const double t0 = notes[i]->getStartBeat().inBeats();
            while (i < notes.size() && notes[i]->getStartBeat().inBeats() - t0 < 1.0 / 64.0) chord.add (notes[i++]);
            std::sort (chord.begin(), chord.end(), [up] (auto* a, auto* b) { return up ? a->getNoteNumber() < b->getNoteNumber() : a->getNoteNumber() > b->getNoteNumber(); });
            for (int k = 0; k < chord.size(); ++k)
            {
                auto* n = chord[k];
                const double shift = k * stepBeats;
                const double end = n->getEndBeat().inBeats();
                const double start = t0 + shift;
                n->setStartAndLength (te::BeatPosition::fromBeats (start), te::BeatDuration::fromBeats (jmax (0.01, end - start)), um);
            }
        }
    }

    /** Moves notes outside the scale to the nearest note in it (down on a tie). Returns how many moved. */
    inline int fitToScale (te::MidiClip& c, const Notes& sel, int scale, int root)
    {
        if (scale < 0) return 0;
        auto* um = &c.edit.getUndoManager();
        int moved = 0;
        for (auto n : target (c, sel))
        {
            const int p = n->getNoteNumber();
            if (Scales::contains (scale, root, p)) continue;
            for (int d = 1; d < 12; ++d)
            {
                if (p - d >= 0 && Scales::contains (scale, root, p - d))   { n->setNoteNumber (p - d, um); ++moved; break; }
                if (p + d <= 127 && Scales::contains (scale, root, p + d)) { n->setNoteNumber (p + d, um); ++moved; break; }
            }
        }
        return moved;
    }

    enum class Velocity { louder, softer, compress, fixed, rampUp, rampDown, randomise };

    inline void velocities (te::MidiClip& c, const Notes& sel, Velocity mode, Random& rng)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = byStart (target (c, sel));
        if (notes.isEmpty()) return;
        double mean = 0;
        for (auto n : notes) mean += n->getVelocity();
        mean /= notes.size();
        for (int i = 0; i < notes.size(); ++i)
        {
            auto* n = notes[i];
            const double v = n->getVelocity();
            const double t = notes.size() > 1 ? (double) i / (notes.size() - 1) : 1.0;
            double nv = v;
            switch (mode)
            {
                case Velocity::louder:    nv = v * 1.1 + 1; break;
                case Velocity::softer:    nv = v * 0.9; break;
                case Velocity::compress:  nv = mean + (v - mean) * 0.5; break;
                case Velocity::fixed:     nv = 100; break;
                case Velocity::rampUp:    nv = 40 + t * 87; break;
                case Velocity::rampDown:  nv = 127 - t * 87; break;
                case Velocity::randomise: nv = v + rng.nextInt ({ -12, 13 }); break;
            }
            n->setVelocity (jlimit (1, 127, roundToInt (nv)), um);
        }
    }

    /** Plays the notes backwards in time (within their own span). */
    inline void reverse (te::MidiClip& c, const Notes& sel)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = target (c, sel);
        if (notes.isEmpty()) return;
        double lo = 1.0e9, hi = -1.0e9;
        for (auto n : notes) { lo = jmin (lo, n->getStartBeat().inBeats()); hi = jmax (hi, n->getEndBeat().inBeats()); }
        for (auto n : notes)
            n->setStartAndLength (te::BeatPosition::fromBeats (lo + hi - n->getEndBeat().inBeats()), n->getLengthBeats(), um);
    }

    /** Mirrors pitches around the middle of their range (high becomes low). */
    inline void invert (te::MidiClip& c, const Notes& sel)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = target (c, sel);
        if (notes.isEmpty()) return;
        int lo = 127, hi = 0;
        for (auto n : notes) { lo = jmin (lo, n->getNoteNumber()); hi = jmax (hi, n->getNoteNumber()); }
        for (auto n : notes) n->setNoteNumber (jlimit (0, 127, lo + hi - n->getNoteNumber()), um);
    }

    /** Stretches timing around the first note (2 = half speed, 0.5 = double speed). */
    inline void stretch (te::MidiClip& c, const Notes& sel, double factor)
    {
        auto* um = &c.edit.getUndoManager();
        auto notes = target (c, sel);
        if (notes.isEmpty()) return;
        double lo = 1.0e9;
        for (auto n : notes) lo = jmin (lo, n->getStartBeat().inBeats());
        for (auto n : notes)
            n->setStartAndLength (te::BeatPosition::fromBeats (lo + (n->getStartBeat().inBeats() - lo) * factor),
                                  te::BeatDuration::fromBeats (jmax (0.01, n->getLengthBeats().inBeats() * factor)), um);
    }
}
