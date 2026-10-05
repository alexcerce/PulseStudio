#pragma once

#include "Common.h"
#include "ProjectRepair.h"
#include "PluginMenu.h"
#include "ClipImport.h"

/** Device presets (one plugin's full state) and chain presets (a whole device chain).
    Stored as XML in Documents/Pulse Studio/Presets. */
namespace Presets
{
    inline File root()
    {
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio").getChildFile ("Presets");
        d.createDirectory();
        return d;
    }

    inline File deviceFolder (te::Plugin& p)
    {
        auto d = root().getChildFile ("Devices").getChildFile (File::createLegalFileName (p.getName()));
        d.createDirectory();
        return d;
    }

    inline File chainFolder()
    {
        auto d = root().getChildFile ("Chains");
        d.createDirectory();
        return d;
    }

    inline Array<File> devicePresets (te::Plugin& p)
    {
        auto files = deviceFolder (p).findChildFiles (File::findFiles, false, "*.pulsepreset");
        files.sort();
        return files;
    }

    inline Array<File> chainPresets()
    {
        auto files = chainFolder().findChildFiles (File::findFiles, false, "*.pulsechain");
        files.sort();
        return files;
    }

    inline ValueTree snapshot (te::Plugin& p)
    {
        p.flushPluginStateToValueTree();
        return p.state.createCopy();
    }

    /** Plugins that make up a track's user chain (no fader / meter / sends / return). */
    inline Array<te::Plugin*> userChain (te::PluginList& list, te::Track* track)
    {
        Array<te::Plugin*> out;
        auto at = dynamic_cast<te::AudioTrack*> (track);
        for (auto p : list)
        {
            if (dynamic_cast<te::LevelMeterPlugin*> (p) || dynamic_cast<te::AuxSendPlugin*> (p)
                || dynamic_cast<te::AuxReturnPlugin*> (p) || dynamic_cast<te::RackInstance*> (p)
                || (at != nullptr && p == at->getVolumePlugin()))
                continue;
            out.add (p);
        }
        return out;
    }

    //==============================================================================
    inline bool saveDevice (te::Plugin& p, const String& name)
    {
        auto f = deviceFolder (p).getChildFile (File::createLegalFileName (name) + ".pulsepreset");
        if (auto xml = snapshot (p).createXml())
            return xml->writeTo (f);
        return false;
    }

    /** Replaces the plugin with one created from the preset (same position in the list). */
    inline te::Plugin::Ptr loadDevice (te::Plugin& p, const File& f)
    {
        auto list = p.getOwnerList();
        auto xml = parseXML (f);
        if (list == nullptr || xml == nullptr) return {};
        xml = ProjectRepair::repaired (*xml);   // presets from versions that saved a Saturator / Auto Filter wrongly

        auto state = ValueTree::fromXml (*xml);
        te::EditItemID::remapIDs (state, nullptr, p.edit);
        const int index = list->indexOf (&p);
        auto newPlugin = list->insertPlugin (state, index);
        if (newPlugin != nullptr)
            p.deleteFromParent();
        return newPlugin;
    }

    inline bool saveChain (te::PluginList& list, te::Track* track, const String& name)
    {
        ValueTree chain ("PULSECHAIN");
        chain.setProperty ("name", name, nullptr);
        for (auto p : userChain (list, track))
            chain.appendChild (snapshot (*p), nullptr);

        if (auto xml = chain.createXml())
            return xml->writeTo (chainFolder().getChildFile (File::createLegalFileName (name) + ".pulsechain"));
        return false;
    }

    /** Replaces the track's user chain with the preset's devices. */
    inline void loadChain (te::PluginList& list, te::Track* track, const File& f)
    {
        auto xml = parseXML (f);
        if (xml == nullptr) return;
        ProjectRepair::repairDeviceTypes (*xml);   // chains from versions that saved a Saturator / Auto Filter wrongly

        auto chain = ValueTree::fromXml (*xml);
        for (auto p : userChain (list, track))
            p->deleteFromParent();

        // instruments first, so the MIDI effects in the chain find theirs
        juce::Array<te::Plugin::Ptr> made;
        for (auto v : chain)
        {
            auto state = v.createCopy();
            te::EditItemID::remapIDs (state, nullptr, list.getEdit());
            if (auto p = list.getEdit().getPluginCache().createNewPlugin (state)) made.add (p);
        }
        for (auto& p : made) if (PluginMenu::kindOf (*p) == PluginMenu::Kind::instrument) PluginMenu::insertPlugin (track, list, p, nullptr);
        for (auto& p : made) if (PluginMenu::kindOf (*p) != PluginMenu::Kind::instrument) PluginMenu::insertPlugin (track, list, p, nullptr);
    }

    //==============================================================================
    /** Applies a dragged browser item to a track: a device, a drum rack or a chain preset. */
    inline bool applyDescription (te::Track* track, te::PluginList& list, const String& d)
    {
        list.getEdit().getUndoManager().beginNewTransaction ("Add device");

        if (d.startsWith ("plugin:chain:"))
        {
            loadChain (list, track, File (d.fromFirstOccurrenceOf ("plugin:chain:", false, false)));
            return true;
        }

        if (auto p = ClipImport::createPluginFromDescription (list.getEdit(), d))
        {
            PluginMenu::insertPlugin (track, list, p, nullptr);
            return true;
        }
        return false;
    }

    //==============================================================================
    /** Asks for a name, then calls back on success. */
    inline void askForName (const String& title, const String& defaultName, std::function<void (const String&)> onOk)
    {
        auto* w = new AlertWindow (title, "Preset name:", MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", defaultName);
        w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([w, onOk] (int r)
        {
            auto name = w->getTextEditorContents ("name").trim();
            if (r == 1 && name.isNotEmpty())
                onOk (name);
        }), true);
    }
}
