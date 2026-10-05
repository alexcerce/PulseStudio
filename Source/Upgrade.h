#pragma once

#include "Common.h"
#include "ProjectRepair.h"
#include "Routing.h"
#include "PulseFx2.h"
#include "ClipImport.h"

//==============================================================================
/** Brings projects from older versions up to date when they open. */
namespace Upgrade
{
    using ProjectRepair::repairDeviceTypes;

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
