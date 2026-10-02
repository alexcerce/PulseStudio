#pragma once

#include "Common.h"

/** Push 2 encoder assignments per track: any device knob can be put on encoders 1-8, which then
    control it in Device mode (before the device's own parameters). Stored in the project. */
namespace PushMap
{
    inline const Identifier listID { "PULSEPUSHMAP" }, entryID { "ENC" };

    inline te::AutomatableParameter* find (te::Track& t, int encoder)
    {
        for (auto v : t.state.getChildWithName (listID))
            if ((int) v["enc"] == encoder)
                if (auto p = t.edit.getPluginCache().getPluginFor (te::EditItemID::fromVar (v["plugin"])))
                    for (auto prm : p->getAutomatableParameters())
                        if (prm->paramID == v["param"].toString())
                            return prm;
        return nullptr;
    }

    inline void assign (te::Track& t, int encoder, te::AutomatableParameter& prm)
    {
        auto* um = &t.edit.getUndoManager();
        auto list = t.state.getOrCreateChildWithName (listID, um);
        for (int i = list.getNumChildren(); --i >= 0;)
            if ((int) list.getChild (i)["enc"] == encoder) list.removeChild (i, um);
        if (auto* plugin = prm.getPlugin())
        {
            ValueTree v (entryID);
            v.setProperty ("enc", encoder, nullptr);
            v.setProperty ("plugin", plugin->itemID.toVar(), nullptr);
            v.setProperty ("param", prm.paramID, nullptr);
            list.appendChild (v, um);
        }
    }

    inline void clear (te::Track& t, int encoder)
    {
        auto list = t.state.getChildWithName (listID);
        for (int i = list.getNumChildren(); --i >= 0;)
            if ((int) list.getChild (i)["enc"] == encoder) list.removeChild (i, &t.edit.getUndoManager());
    }

    /** Adds "Assign to Push encoder" to a knob's context menu. */
    inline void addToMenu (PopupMenu& menu, te::AutomatableParameter& p)
    {
        auto track = p.getPlugin() != nullptr ? p.getPlugin()->getOwnerTrack() : nullptr;
        if (track == nullptr) return;
        PopupMenu sub;
        for (int e = 0; e < 8; ++e)
        {
            auto* cur = find (*track, e);
            const String label = "Encoder " + String (e + 1) + (cur != nullptr ? "  (" + cur->getParameterName() + ")" : String());
            sub.addItem (label, true, cur == &p, [t = te::Track::Ptr (track), prm = te::AutomatableParameter::Ptr (&p), e]
            {
                t->edit.getUndoManager().beginNewTransaction ("Assign Push encoder");
                if (find (*t, e) == prm.get()) clear (*t, e);
                else                           assign (*t, e, *prm);
            });
        }
        menu.addSubMenu ("Assign to Push encoder", sub);
    }
}
