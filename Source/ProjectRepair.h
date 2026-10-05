#pragma once

/** Repairs for things older versions saved wrongly (used for projects and for presets). */
namespace ProjectRepair
{
    /** Versions before 0.92 kept the Saturator's and the Auto Filter's "Type" setting in the same place
        as the record of what kind of device it is. Once that setting had been changed and the project
        saved, the device could not be loaded again. This puts such devices right in the project file
        (a copy of the file as it was is kept next to it) before the project is opened.
        Returns the number of devices repaired. */
    inline int repairDeviceTypes (XmlElement& xml)
    {
        int fixed = 0;
        for (auto* e : xml.getChildIterator())
        {
            if (e->hasTagName ("PLUGIN"))
            {
                const auto type = e->getStringAttribute ("type");
                if (type.isNotEmpty() && type.containsOnly ("0123456789.-"))
                {
                    // which device it was: by the settings only that device has
                    const char* kind = e->hasAttribute ("bias") || e->hasAttribute ("tone") ? "pulseSaturator"
                                     : e->hasAttribute ("lfoamount") || e->hasAttribute ("lforate") || e->hasAttribute ("envamount") ? "pulseAutoFilter" : nullptr;
                    if (kind != nullptr)
                    {
                        e->setAttribute ("pulse_type", type);
                        e->setAttribute ("type", kind);
                        ++fixed;
                    }
                }
            }
            fixed += repairDeviceTypes (*e);
        }
        return fixed;
    }

    /** The same for one saved device or chain (a preset file): returns the repaired copy. */
    inline std::unique_ptr<XmlElement> repaired (const XmlElement& preset)
    {
        XmlElement holder ("HOLDER");
        holder.addChildElement (new XmlElement (preset));
        repairDeviceTypes (holder);
        return std::make_unique<XmlElement> (*holder.getFirstChildElement());
    }
}
