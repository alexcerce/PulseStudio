#pragma once

#include "Common.h"

/** Drum Rack built on a Tracktion RackType:
      rack MIDI in -> 16 Samplers (one per pad, notes 36..51) -> per-pad effect chain -> rack L/R out.
    Pad membership is stored on each plugin's state ("pulsePad" + "pulseOrder"). */
namespace DrumRack
{
    constexpr int numPads = 16, baseNote = 36;
    inline const Identifier padProp { "pulsePad" }, orderProp { "pulseOrder" }, rackFlag { "pulseDrumRack" };

    inline te::RackType* rackOf (te::Plugin* p)
    {
        if (auto ri = dynamic_cast<te::RackInstance*> (p))
            return ri->type.get();
        return nullptr;
    }

    inline bool isDrumRack (te::Plugin* p)
    {
        auto r = rackOf (p);
        return r != nullptr && (bool) r->state.getProperty (rackFlag);
    }

    inline te::RackInstance* findOnTrack (te::AudioTrack& t)
    {
        for (auto p : t.pluginList)
            if (isDrumRack (p))
                return dynamic_cast<te::RackInstance*> (p);
        return nullptr;
    }

    inline int padOf (te::Plugin& p) { return p.state.getProperty (padProp, -1); }

    inline te::SamplerPlugin* sampler (te::RackType& rack, int pad)
    {
        for (auto p : rack.getPlugins())
            if (auto s = dynamic_cast<te::SamplerPlugin*> (p))
                if (padOf (*s) == pad)
                    return s;
        return nullptr;
    }

    inline Array<te::Plugin*> chain (te::RackType& rack, int pad)
    {
        Array<te::Plugin*> fx;
        for (auto p : rack.getPlugins())
            if (dynamic_cast<te::SamplerPlugin*> (p) == nullptr && padOf (*p) == pad)
                fx.add (p);
        std::sort (fx.begin(), fx.end(), [] (auto* a, auto* b) { return (int) a->state[orderProp] < (int) b->state[orderProp]; });
        return fx;
    }

    /** Reconnects one pad: rack MIDI -> sampler -> fx... -> rack outputs. */
    inline void rewirePad (te::RackType& rack, int pad)
    {
        auto s = sampler (rack, pad);
        if (s == nullptr) return;

        auto fx = chain (rack, pad);
        Array<te::EditItemID> ids { s->itemID };
        for (auto p : fx) ids.add (p->itemID);

        // drop old connections touching this pad's plugins
        for (auto c : rack.getConnections())
            if (ids.contains (c->sourceID.get()) || ids.contains (c->destID.get()))
                rack.removeConnection (c->sourceID.get(), c->sourcePin.get(), c->destID.get(), c->destPin.get());

        rack.addConnection ({}, 0, s->itemID, 0);   // MIDI in
        te::Plugin* prev = s;
        for (auto p : fx)
        {
            rack.addConnection (prev->itemID, 1, p->itemID, 1);
            rack.addConnection (prev->itemID, 2, p->itemID, 2);
            prev = p;
        }
        rack.addConnection (prev->itemID, 1, {}, 1);
        rack.addConnection (prev->itemID, 2, {}, 2);
    }

    /** Creates a Drum Rack instance ready to insert into a track. */
    inline te::Plugin::Ptr create (te::Edit& edit)
    {
        auto rack = edit.getRackList().addNewRack();
        if (rack == nullptr) return {};

        rack->rackName = "Drum Rack";
        rack->state.setProperty (rackFlag, true, nullptr);
        if (rack->getOutputNames().isEmpty())
        {
            rack->addOutput (-1, "Left");
            rack->addOutput (-1, "Right");
        }

        for (int pad = 0; pad < numPads; ++pad)
        {
            auto p = edit.getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
            p->state.setProperty (padProp, pad, nullptr);
            rack->addPlugin (p, { 0.2f, (float) (pad + 1) / (float) (numPads + 1) }, false);
            rewirePad (*rack, pad);
        }

        return edit.getPluginCache().createNewPlugin (te::RackInstance::create (*rack));
    }

    //==============================================================================
    inline String padName (te::RackType& rack, int pad)
    {
        if (auto s = sampler (rack, pad))
            if (s->getNumSounds() > 0)
                return s->getSoundName (0);
        return {};
    }

    inline bool loadSample (te::RackType& rack, int pad, const File& f)
    {
        auto s = sampler (rack, pad);
        if (s == nullptr) return false;

        te::AudioFile af (rack.edit.engine, f);
        if (! af.isValid()) return false;

        while (s->getNumSounds() > 0)
            s->removeSound (0);

        auto err = s->addSound (f.getFullPathName(), f.getFileNameWithoutExtension(), 0.0, af.getLength(), 0.0f);
        if (err.isNotEmpty()) return false;

        const int note = baseNote + pad;
        s->setSoundParams (0, note, note, note);
        s->setSoundOpenEnded (0, true);   // one-shot: plays the whole sample
        s->flushPendingUpdates();         // load now, so renders/bounces right after include it
        return true;
    }

    inline void clearPad (te::RackType& rack, int pad)
    {
        if (auto s = sampler (rack, pad))
            while (s->getNumSounds() > 0)
                s->removeSound (0);
    }

    /** Tune in semitones: shifts the sample's root key. */
    inline void setTune (te::RackType& rack, int pad, int semis)
    {
        if (auto s = sampler (rack, pad); s != nullptr && s->getNumSounds() > 0)
        {
            const int note = baseNote + pad;
            s->setSoundParams (0, note - semis, note, note);
        }
    }

    inline int getTune (te::RackType& rack, int pad)
    {
        if (auto s = sampler (rack, pad); s != nullptr && s->getNumSounds() > 0)
            return (baseNote + pad) - s->getKeyNote (0);
        return 0;
    }

    inline void addEffect (te::RackType& rack, int pad, te::Plugin::Ptr p)
    {
        if (p == nullptr || p->isSynth()) return;
        p->state.setProperty (padProp, pad, nullptr);
        p->state.setProperty (orderProp, chain (rack, pad).size(), nullptr);
        rack.addPlugin (p, { 0.6f, (float) (pad + 1) / (float) (numPads + 1) }, false);
        rewirePad (rack, pad);
    }

    inline void removeEffect (te::RackType& rack, te::Plugin& p)
    {
        const int pad = padOf (p);
        p.deleteFromParent();
        rewirePad (rack, pad);
    }

    /** Default GM-ish pad names used on empty pads. */
    inline String defaultPadLabel (int pad)
    {
        static const char* names[] = { "Kick", "Rim", "Snare", "Clap", "Snare 2", "Low Tom", "Closed HH", "Low Tom 2",
                                       "Pedal HH", "Mid Tom", "Open HH", "Mid Tom 2", "High Tom", "Crash", "High Tom 2", "Ride" };
        return names[pad];
    }
}
