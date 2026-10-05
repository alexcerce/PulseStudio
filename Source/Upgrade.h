#pragma once

#include "Common.h"
#include "Routing.h"
#include "PulseFx2.h"
#include "ClipImport.h"

//==============================================================================
/** Brings projects from older versions up to date when they open. */
namespace Upgrade
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

    inline int repairProjectFile (const File& file)
    {
        auto xml = parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("EDIT")) return 0;
        const int fixed = repairDeviceTypes (*xml);
        if (fixed > 0)
        {
            file.copyFileTo (file.getSiblingFile (file.getFileNameWithoutExtension() + " (before repair).tracktionedit"));
            xml->writeTo (file);
        }
        return fixed;
    }

    /** Swaps the engine's basic reverb / delay on return tracks for Pulse Reverb / Delay (every
        setting editable), carrying the old settings over as closely as possible. */
    inline int upgradeReturns (te::Edit& edit)
    {
        int swapped = 0;
        for (auto t : Routing::getReturnTracks (edit))
        {
            for (int i = 0; i < t->pluginList.size(); ++i)
            {
                auto old = t->pluginList[i];
                te::Plugin::Ptr np;
                if (auto r = dynamic_cast<te::ReverbPlugin*> (old))
                {
                    np = edit.getPluginCache().createNewPlugin (pulse::Reverb::xmlTypeName, {});
                    if (auto pr = dynamic_cast<pulse::Reverb*> (np.get()))
                    {
                        const float room = jlimit (0.0f, 1.0f, r->getRoomSize());
                        pr->set (pulse::Reverb::pSize, jlimit (0.1f, 1.0f, 0.2f + room * 0.8f));
                        pr->set (pulse::Reverb::pDecay, 0.6f + room * room * 5.0f);
                        pr->set (pulse::Reverb::pDamping, jmap (jlimit (0.0f, 1.0f, r->getDamp()), 16000.0f, 2500.0f));
                        pr->set (pulse::Reverb::pWidth, jlimit (0.0f, 1.0f, r->getWidth()));
                        const float wet = r->getWet(), dry = r->getDry();
                        pr->set (pulse::Reverb::pMix, wet + dry > 0.0f ? jlimit (0.0f, 1.0f, wet / jmax (0.001f, wet + dry) * 1.0f) : 1.0f);
                    }
                }
                else if (auto d = dynamic_cast<te::DelayPlugin*> (old))
                {
                    np = edit.getPluginCache().createNewPlugin (pulse::Delay::xmlTypeName, {});
                    if (auto pd = dynamic_cast<pulse::Delay*> (np.get()))
                    {
                        pd->set (pulse::Delay::pSync, 0.0f);
                        pd->set (pulse::Delay::pTime, (float) jlimit (1, 2000, d->lengthMs.get()));
                        const float fbDb = d->feedbackValue.get();
                        pd->set (pulse::Delay::pFeedback, fbDb <= te::DelayPlugin::getMinDelayFeedbackDb() ? 0.0f
                                                                                                         : jlimit (0.0f, 0.95f, Decibels::decibelsToGain (fbDb)));
                        pd->set (pulse::Delay::pMix, jlimit (0.0f, 1.0f, d->mixValue.get()));
                    }
                }
                if (np == nullptr) continue;
                np->setEnabled (old->isEnabled());
                t->pluginList.insertPlugin (np, i, nullptr);
                old->deleteFromParent();
                ++swapped;
            }
        }
        return swapped;
    }

    /** Everything that runs when a project opens. */
    inline void run (te::Edit& edit)
    {
        upgradeReturns (edit);
        ClipImport::fixRawTrackNames (edit);
    }
}
