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
