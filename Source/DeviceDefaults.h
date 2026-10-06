#pragma once

#include "PulseDevices.h"

/** Your own starting points. A device can be given a default: the settings it has now become the settings every
    new one of its kind starts with (a compressor that opens the way you always set it). The default is kept in the
    settings, so it holds in every project. It is applied when a device is made new from a menu or the browser,
    never to a device that comes from a preset, a copy or a project. */
namespace DeviceDefaults
{
    inline PropertiesFile& store (te::Engine& e)        { return e.getPropertyStorage().getPropertiesFile(); }
    inline String keyFor (const String& type)           { return "deviceDefault_" + type; }
    inline bool has (te::Engine& e, const String& type) { return store (e).getValue (keyFor (type)).isNotEmpty(); }
    inline void forget (te::Engine& e, const String& type) { store (e).removeValue (keyFor (type)); }

    /** The device's settings as text: one "name=value" for each parameter. */
    inline String capture (pulse::PulsePlugin& p)
    {
        StringArray parts;
        for (int i = 0; i < p.numParams(); ++i) parts.add (p.param (i)->paramID + "=" + String (p.get (i), 6));
        return parts.joinIntoString ("|");
    }

    /** Makes the device's settings as they are now the default for its kind. */
    inline void save (pulse::PulsePlugin& p) { store (p.engine).setValue (keyFor (p.getPluginType()), capture (p)); }

    /** Gives a new device the default of its kind, when there is one. Returns true when it did. Parameters the default
        does not name (added in a later version) keep their own starting values. */
    inline bool applyTo (te::Plugin* plugin)
    {
        auto* p = dynamic_cast<pulse::PulsePlugin*> (plugin);
        if (p == nullptr) return false;
        const auto text = store (p->engine).getValue (keyFor (p->getPluginType()));
        if (text.isEmpty()) return false;
        StringPairArray values;
        for (auto& part : StringArray::fromTokens (text, "|", {}))
            values.set (part.upToFirstOccurrenceOf ("=", false, false), part.fromFirstOccurrenceOf ("=", false, false));
        for (int i = 0; i < p->numParams(); ++i)
            if (auto* prm = p->param (i); values.containsKey (prm->paramID))
            {
                const float v = prm->valueRange.snapToLegalValue (jlimit (prm->valueRange.start, prm->valueRange.end, values[prm->paramID].getFloatValue()));
                if (std::abs (p->get (i) - v) > 1.0e-7f) p->set (i, v);
            }
        return true;
    }

    /** The two entries for a device's preset menu. */
    inline void addToMenu (PopupMenu& m, te::Plugin::Ptr plugin)
    {
        auto* p = dynamic_cast<pulse::PulsePlugin*> (plugin.get());
        if (p == nullptr) return;
        m.addItem ("Make these settings the default for every new " + p->getName(), [plugin] { if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (plugin.get())) save (*pp); });
        if (has (p->engine, p->getPluginType()))
            m.addItem ("Forget the default (new ones start as they came)", [plugin] { forget (plugin->engine, plugin->getPluginType()); });
    }
}
