#pragma once

#include "Common.h"

/** Send/return buses built on Tracktion's Aux Send/Return plugins.
    A "return track" is an ordinary AudioTrack whose first plugin is an AuxReturnPlugin;
    sends are AuxSendPlugins placed after a track's fader (post-fader). */
namespace Routing
{
    constexpr float sendOffDb = -60.0f;

    /** Bus number if this is a return track, otherwise -1. */
    inline int getReturnBus (te::AudioTrack& t)
    {
        if (auto first = t.pluginList[0])
            if (auto ret = dynamic_cast<te::AuxReturnPlugin*> (first))
                return ret->busNumber.get();

        return -1;
    }

    inline bool isReturnTrack (te::AudioTrack& t)  { return getReturnBus (t) >= 0; }

    inline juce::Array<te::AudioTrack*> getReturnTracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (e))
            if (isReturnTrack (*t))
                out.add (t);
        return out;
    }

    /** Tracks that hold clips (everything except returns). */
    inline juce::Array<te::AudioTrack*> getContentTracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (e))
            if (! isReturnTrack (*t))
                out.add (t);
        return out;
    }

    inline int nextFreeBus (te::Edit& e)
    {
        int bus = 0;
        for (auto r : getReturnTracks (e))
            bus = jmax (bus, getReturnBus (*r) + 1);
        return bus;
    }

    inline te::AuxSendPlugin* findSend (te::AudioTrack& t, int bus)
    {
        for (auto p : t.pluginList)
            if (auto s = dynamic_cast<te::AuxSendPlugin*> (p))
                if (s->getBusNumber() == bus)
                    return s;

        return nullptr;
    }

    inline float getSendDb (te::AudioTrack& t, int bus)
    {
        if (auto s = findSend (t, bus))
            return s->getGainDb();

        return sendOffDb;
    }

    /** Sets a send level, creating the post-fader send plugin on first use. */
    inline void setSendDb (te::AudioTrack& t, int bus, float db)
    {
        auto send = findSend (t, bus);

        if (send == nullptr)
        {
            if (db <= sendOffDb)
                return;

            auto p = t.edit.getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
            send = dynamic_cast<te::AuxSendPlugin*> (p.get());
            if (send == nullptr)
                return;

            send->busNumber = bus;
            int index = t.pluginList.size();
            if (auto vp = t.getVolumePlugin())
                index = t.pluginList.indexOf (vp) + 1;   // post-fader
            t.pluginList.insertPlugin (p, index, nullptr);
        }

        send->setGainDb (db <= sendOffDb ? -100.0f : db);
    }

    /** Is this send placed before the track's fader? */
    inline bool isPreFader (te::AudioTrack& t, int bus)
    {
        auto send = findSend (t, bus);
        auto vp = t.getVolumePlugin();
        return send != nullptr && vp != nullptr && t.pluginList.indexOf (send) < t.pluginList.indexOf (vp);
    }

    /** Moves a send before (pre) or after (post) the fader, creating it if needed. */
    inline void setPreFader (te::AudioTrack& t, int bus, bool pre)
    {
        if (findSend (t, bus) == nullptr)
        {
            setSendDb (t, bus, -59.0f);
            if (auto s = findSend (t, bus)) s->setGainDb (-100.0f);
        }
        auto send = findSend (t, bus);
        auto vp = t.getVolumePlugin();
        if (send == nullptr || vp == nullptr || isPreFader (t, bus) == pre) return;

        auto parent = send->state.getParent();
        const int from = parent.indexOf (send->state), fader = parent.indexOf (vp->state);
        parent.moveChild (from, pre ? fader : fader, &t.edit.getUndoManager());   // either side of the fader
    }

    /** Adds a return track at the end of the track list, optionally with an effect on it. */
    inline te::AudioTrack* createReturnTrack (te::Edit& e, const String& name, const char* effectType = nullptr)
    {
        const int bus = nextFreeBus (e);
        auto all = te::getAudioTracks (e);
        auto t = e.insertNewAudioTrack (te::TrackInsertPoint (nullptr, all.getLast()), nullptr);
        if (t == nullptr)
            return nullptr;

        t->setName (name);
        t->setColour (Colour (0xff8b95a5));
        e.setAuxBusName (bus, name);

        auto ret = e.getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {});
        if (auto r = dynamic_cast<te::AuxReturnPlugin*> (ret.get()))
            r->busNumber = bus;
        t->pluginList.insertPlugin (ret, 0, nullptr);

        if (effectType != nullptr)
            if (auto fx = e.getPluginCache().createNewPlugin (effectType, {}))
                t->pluginList.insertPlugin (fx, 1, nullptr);

        return t.get();
    }
}

//==============================================================================
/** Mixer snapshots stored in the project: volume, pan, mute, solo and send levels of every track. */
namespace MixSnapshots
{
    inline const Identifier listID { "PULSEMIXSNAPSHOTS" }, snapID { "SNAP" }, trackID { "T" };

    inline StringArray names (te::Edit& e)
    {
        StringArray n;
        for (auto c : e.state.getChildWithName (listID)) n.add (c["name"].toString());
        return n;
    }

    inline void store (te::Edit& e, const String& name)
    {
        auto* um = &e.getUndoManager();
        um->beginNewTransaction ("Store mixer snapshot");
        auto list = e.state.getOrCreateChildWithName (listID, um);
        ValueTree snap (snapID);
        snap.setProperty ("name", name.isNotEmpty() ? name : "Mix", nullptr);
        auto returns = Routing::getReturnTracks (e);
        for (auto t : te::getAudioTracks (e))
        {
            ValueTree v (trackID);
            v.setProperty ("id", t->itemID.toString(), nullptr);
            if (auto vp = t->getVolumePlugin())
            {
                v.setProperty ("vol", vp->getVolumeDb(), nullptr);
                v.setProperty ("pan", vp->getPan(), nullptr);
            }
            v.setProperty ("mute", t->isMuted (false), nullptr);
            v.setProperty ("solo", t->isSolo (false), nullptr);
            if (! Routing::isReturnTrack (*t))
                for (auto r : returns)
                {
                    const int bus = Routing::getReturnBus (*r);
                    v.setProperty ("send" + String (bus), Routing::getSendDb (*t, bus), nullptr);
                }
            snap.appendChild (v, nullptr);
        }
        list.appendChild (snap, um);
    }

    inline bool recall (te::Edit& e, int index)
    {
        auto snap = e.state.getChildWithName (listID).getChild (index);
        if (! snap.isValid()) return false;
        e.getUndoManager().beginNewTransaction ("Recall mixer snapshot");
        auto returns = Routing::getReturnTracks (e);
        for (auto v : snap)
            for (auto t : te::getAudioTracks (e))
                if (t->itemID.toString() == v["id"].toString())
                {
                    if (auto vp = t->getVolumePlugin())
                    {
                        if (v.hasProperty ("vol")) vp->setVolumeDb ((float) v["vol"]);
                        if (v.hasProperty ("pan")) vp->setPan ((float) v["pan"]);
                    }
                    t->setMute ((bool) v["mute"]);
                    t->setSolo ((bool) v["solo"]);
                    if (! Routing::isReturnTrack (*t))
                        for (auto r : returns)
                        {
                            const int bus = Routing::getReturnBus (*r);
                            if (v.hasProperty ("send" + String (bus)))
                                Routing::setSendDb (*t, bus, (float) v["send" + String (bus)]);
                        }
                }
        return true;
    }

    inline void remove (te::Edit& e, int index)
    {
        auto list = e.state.getChildWithName (listID);
        if (list.isValid() && isPositiveAndBelow (index, list.getNumChildren()))
        {
            e.getUndoManager().beginNewTransaction ("Delete mixer snapshot");
            list.removeChild (index, &e.getUndoManager());
        }
    }
}
