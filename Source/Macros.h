#pragma once

#include "Common.h"

/** Eight macro knobs per track (Tracktion MacroParameters used as modifiers). */
namespace Macros
{
    constexpr int count = 8;

    inline ReferenceCountedArray<te::MacroParameter> get (te::AudioTrack& t)
    {
        if (auto l = t.getMacroParameterList())
            return l->getMacroParameters();
        return {};
    }

    inline void ensure (te::AudioTrack& t)
    {
        auto& list = t.getMacroParameterListForWriting();
        while (list.getMacroParameters().size() < count)
            if (auto m = list.createMacroParameter())
                m->macroName = "Macro " + String (list.getMacroParameters().size());
    }

    inline bool isMapped (te::AutomatableParameter& p, te::MacroParameter& m)
    {
        for (auto mod : p.getModifiers())
            if (mod == &m)
                return true;
        return false;
    }

    inline void toggleMapping (te::AutomatableParameter& p, te::MacroParameter& m)
    {
        if (isMapped (p, m)) p.removeModifier (m);
        else                 p.addModifier (m, 1.0f, 0.0f, 0.5f);
    }

    /** Names of the parameters a macro controls on its track. */
    inline StringArray targets (te::AudioTrack& t, te::MacroParameter& m)
    {
        StringArray names;
        for (auto plugin : t.pluginList)
            for (auto p : plugin->getAutomatableParameters())
                if (isMapped (*p, m))
                    names.add (plugin->getName() + ": " + p->getParameterName());
        return names;
    }

    /** Adds "Map to Macro" items to a parameter's context menu. */
    inline void addToMenu (PopupMenu& menu, te::AutomatableParameter& p)
    {
        auto track = dynamic_cast<te::AudioTrack*> (p.getPlugin() != nullptr ? p.getPlugin()->getOwnerTrack() : nullptr);
        if (track == nullptr) return;

        PopupMenu sub;
        auto macros = get (*track);
        for (int i = 0; i < count; ++i)
        {
            const bool mapped = i < macros.size() && isMapped (p, *macros[i]);
            const String label = i < macros.size() ? macros[i]->getParameterName() : "Macro " + String (i + 1);
            sub.addItem (label, true, mapped, [t = te::AudioTrack::Ptr (track), ptr = te::AutomatableParameter::Ptr (&p), i]
            {
                ensure (*t);
                if (auto m = get (*t)[i])
                    toggleMapping (*ptr, *m);
            });
        }
        menu.addSubMenu ("Map to Macro", sub);
    }
}
