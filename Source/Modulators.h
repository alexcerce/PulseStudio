#pragma once

#include "Common.h"

/** Modulators (Tracktion modifiers) on a track: LFO, step sequencer, envelope follower, random,
    MIDI tracker and breakpoint envelope. Any device knob can be modulated by any of them. */
namespace Modulators
{
    struct Type { const char* name; const char* shortName; Identifier id; };

    inline const std::vector<Type>& types()
    {
        static const std::vector<Type> t {
            { "LFO", "LFO", te::IDs::LFO },
            { "Step sequencer", "Step", te::IDs::STEP },
            { "Envelope follower", "Env", te::IDs::ENVELOPEFOLLOWER },
            { "Random", "Random", te::IDs::RANDOM },
            { "MIDI tracker (note / velocity)", "Tracker", te::IDs::MIDITRACKER },
            { "Breakpoint envelope", "Shape", te::IDs::BREAKPOINTOSCILLATOR },
        };
        return t;
    }

    inline te::ModifierList* listFor (te::Track& t) { return t.getModifierList(); }

    inline juce::ReferenceCountedArray<te::Modifier> all (te::Track& t)
    {
        if (auto l = listFor (t)) return l->getModifiers();
        return {};
    }

    inline te::Modifier::Ptr add (te::Track& t, const Identifier& type)
    {
        if (auto l = listFor (t))
            return l->insertModifier (ValueTree (type), -1, nullptr);
        return {};
    }

    /** "LFO 1", "Step 2"... numbered per type on the track. */
    inline String nameOf (te::Track& t, te::Modifier& m)
    {
        for (auto& ty : types())
            if (m.state.hasType (ty.id))
            {
                int n = 0;
                for (auto other : all (t))
                {
                    if (other->state.hasType (ty.id)) ++n;
                    if (other == &m) return String (ty.shortName) + " " + String (n);
                }
            }
        return m.getName();
    }

    inline te::AutomatableParameter::ModifierAssignment* assignmentFor (te::AutomatableParameter& p, te::Modifier& m)
    {
        for (auto a : p.getAssignments())
            if (a->isForModifierSource (m)) return a;
        return nullptr;
    }

    /** Sets how much `m` moves `p` (-1..1); 0 removes the connection. */
    inline void setAmount (te::AutomatableParameter& p, te::Modifier& m, float amount)
    {
        if (auto a = assignmentFor (p, m))
        {
            if (std::abs (amount) < 1.0e-4f) p.removeModifier (*a);
            else a->value = amount;
            return;
        }
        if (std::abs (amount) >= 1.0e-4f)
            p.addModifier (m, amount);
    }

    inline float amountOf (te::AutomatableParameter& p, te::Modifier& m)
    {
        if (auto a = assignmentFor (p, m)) return a->value.get();
        return 0.0f;
    }

    /** True if any modulator (not a macro) drives the parameter. */
    inline bool isModulated (te::AutomatableParameter& p)
    {
        for (auto a : p.getAssignments())
            if (dynamic_cast<te::MacroParameter::Assignment*> (a) == nullptr) return true;
        return false;
    }

    /** Parameters a modulator drives (for its card). */
    inline StringArray targetsOf (te::Track& t, te::Modifier& m)
    {
        StringArray out;
        if (auto at = dynamic_cast<te::AudioTrack*> (&t))
            for (auto pl : at->pluginList)
                for (auto prm : pl->getAutomatableParameters())
                    if (assignmentFor (*prm, m) != nullptr)
                        out.add (pl->getName() + " > " + prm->getParameterName());
        return out;
    }

    /** The track a parameter belongs to (its plugin's track). */
    inline te::Track* trackOf (te::AutomatableParameter& p)
    {
        if (auto pl = p.getPlugin()) return pl->getOwnerTrack();
        return nullptr;
    }

    /** Adds "Modulate with..." to a knob's context menu. */
    inline void addToMenu (PopupMenu& menu, te::AutomatableParameter& p)
    {
        auto t = trackOf (p);
        if (t == nullptr || listFor (*t) == nullptr) return;

        te::AutomatableParameter::Ptr param (&p);
        te::Track::Ptr track (t);
        PopupMenu sub;
        for (auto m : all (*t))
        {
            te::Modifier::Ptr mod (m);
            const float amt = amountOf (p, *m);
            PopupMenu amounts;
            for (int pct : { 100, 75, 50, 25, 10, -10, -25, -50, -100 })
                amounts.addItem ((pct > 0 ? "+" : "") + String (pct) + "%", true, std::abs (amt * 100.0f - (float) pct) < 0.5f,
                                 [param, mod, pct] { param->getEdit().getUndoManager().beginNewTransaction ("Modulation amount");
                                                     setAmount (*param, *mod, pct / 100.0f); });
            amounts.addSeparator();
            amounts.addItem ("Remove", std::abs (amt) > 0.0f, false, [param, mod] { setAmount (*param, *mod, 0.0f); });
            sub.addSubMenu (nameOf (*t, *m) + (std::abs (amt) > 0.0f ? "  (" + String (roundToInt (amt * 100.0f)) + "%)" : String()), amounts);
        }
        if (sub.getNumItems() > 0) sub.addSeparator();
        for (auto& ty : types())
            sub.addItem ("New " + String (ty.name) + " on this track", [param, track, id = ty.id]
            {
                param->getEdit().getUndoManager().beginNewTransaction ("Add modulator");
                if (auto m = add (*track, id)) setAmount (*param, *m, 0.5f);
            });
        menu.addSubMenu ("Modulate with", sub);
    }
}
