#pragma once

// Included from PulseSynths.h (needs PulseEffect).

/** Effect Rack: several chains of effects side by side. The sound goes into every chain at once, each chain does
    its own thing to it, and what comes out of them is added together: the dry sound beside a crushed copy, a
    reverb only on a filtered copy, three different delays at once. Each chain has its own level, pan and mute,
    and an empty chain is the sound itself (so "dry beside wet" is one empty chain and one with the effect). */
namespace pulse
{
    /** The end of a chain: its level, pan and mute. */
    class ChainOut : public PulseEffect
    {
    public:
        enum { pVol, pPan, pMute };
        static inline const char* xmlTypeName = "pulseChainOut";
        static const char* getPluginName() { return "Chain Mixer"; }

        explicit ChainOut (te::PluginCreationInfo info) : PulseEffect (info)
        {
            add ("vol", "Volume", skewed (-70.0f, 12.0f, -12.0f), 0.0f, [] (float v) { return v <= -69.5f ? String ("-inf dB") : fmt::db (v); });
            add ("pan", "Pan", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); });
            add ("mute", "Mute", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
        }

        String getName() const override        { return "Chain Mixer"; }
        String getPluginType() override        { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Chain", { pVol, pPan, pMute } } }; }

        /** Silenced because another chain of the rack is being listened to alone (not kept in the project). */
        std::atomic<bool> silencedBySolo { false }, soloed { false };
        std::atomic<float> levelDb { -100.0f };

    protected:
        void prepare() override { gl = gr = -1.0f; }

        void process (float* l, float* r, int n, const te::PluginRenderContext&) override
        {
            const bool off = getInt (pMute) > 0 || silencedBySolo.load();
            const float vol = get (pVol), pan = get (pPan);
            const float g = off || vol <= -69.5f ? 0.0f : Decibels::decibelsToGain (vol);
            // a balance control: the centre leaves both sides alone, the ends turn one side off
            const float tl = g * jmin (1.0f, 1.0f - pan), tr = g * jmin (1.0f, 1.0f + pan);
            if (gl < 0.0f) { gl = tl; gr = tr; }
            const float step = 1.0f / (float) (sampleRate * 0.01);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                gl += jlimit (-step, step, tl - gl);
                gr += jlimit (-step, step, tr - gr);
                l[i] *= gl; r[i] *= gr;
                peak = jmax (peak, std::abs (l[i]), std::abs (r[i]));
            }
            levelDb = jmax (Decibels::gainToDecibels (peak, -100.0f), levelDb.load() - 1.5f);
        }

    private:
        float gl = -1.0f, gr = -1.0f;
    };
}

namespace FxRack
{
    inline const Identifier rackFlag { "pulseFxRack" }, chainProp { "pulseChain" }, orderProp { "pulseOrder" }, roleProp { "pulseRole" },
                            chainsID { "PULSECHAINS" }, chainID { "CHAIN" }, idProp { "id" }, nameProp { "name" };

    inline te::RackType* rackOf (te::Plugin* p)
    {
        if (auto ri = dynamic_cast<te::RackInstance*> (p)) return ri->type.get();
        return nullptr;
    }

    inline bool isFxRack (te::Plugin* p)
    {
        auto r = rackOf (p);
        return r != nullptr && (bool) r->state.getProperty (rackFlag);
    }

    /** A rack that is an instrument (the Drum Rack), as opposed to a rack of effects. */
    inline bool isInstrumentRack (te::Plugin* p) { return dynamic_cast<te::RackInstance*> (p) != nullptr && ! isFxRack (p); }

    struct Chain { int id = 0; String name; };

    /** The chains of a rack, in order. */
    inline std::vector<Chain> chains (te::RackType& rack)
    {
        std::vector<Chain> out;
        for (auto c : rack.state.getChildWithName (chainsID))
            out.push_back ({ (int) c[idProp], c[nameProp].toString() });
        return out;
    }

    inline int chainOf (te::Plugin& p) { return p.state.getProperty (chainProp, -1); }

    /** The mixer at the end of a chain. */
    inline pulse::ChainOut* output (te::RackType& rack, int chain)
    {
        for (auto p : rack.getPlugins())
            if (auto o = dynamic_cast<pulse::ChainOut*> (p))
                if (chainOf (*o) == chain) return o;
        return nullptr;
    }

    /** The effects of a chain, in order. */
    inline Array<te::Plugin*> effects (te::RackType& rack, int chain)
    {
        Array<te::Plugin*> fx;
        for (auto p : rack.getPlugins())
            if (chainOf (*p) == chain && dynamic_cast<pulse::ChainOut*> (p) == nullptr) fx.add (p);
        std::sort (fx.begin(), fx.end(), [] (auto* a, auto* b) { return (int) a->state[orderProp] < (int) b->state[orderProp]; });
        return fx;
    }

    /** Rebuilds every connection of the rack: its input into the head of each chain, each chain in a row, and every
        chain's mixer into the rack's output (where they are added together). */
    inline void rewire (te::RackType& rack)
    {
        for (auto c : rack.getConnections())
            rack.removeConnection (c->sourceID.get(), c->sourcePin.get(), c->destID.get(), c->destPin.get());
        for (auto& ch : chains (rack))
        {
            auto* out = output (rack, ch.id);
            if (out == nullptr) continue;
            te::Plugin* prev = nullptr;
            auto join = [&] (te::Plugin* to)
            {
                for (int pin : { 1, 2 }) rack.addConnection (prev != nullptr ? prev->itemID : te::EditItemID(), pin, to->itemID, pin);
                prev = to;
            };
            for (auto p : effects (rack, ch.id)) join (p);
            join (out);
            for (int pin : { 1, 2 }) rack.addConnection (out->itemID, pin, {}, pin);
        }
    }

    inline juce::Point<float> layoutPos (int chainIndex, float column) { return { column, ((float) chainIndex + 1.0f) / 20.0f }; }

    /** A new, empty chain at the end. Returns its id. */
    inline int addChain (te::RackType& rack, const String& name = {})
    {
        auto* um = &rack.edit.getUndoManager();
        auto list = rack.state.getOrCreateChildWithName (chainsID, um);
        int id = 0;
        for (auto c : list) id = jmax (id, (int) c[idProp] + 1);
        ValueTree v (te::IDs::PLUGIN);
        v.setProperty (te::IDs::type, pulse::ChainOut::xmlTypeName, nullptr);
        v.setProperty (chainProp, id, nullptr);
        v.setProperty (roleProp, "out", nullptr);
        auto p = rack.edit.getPluginCache().createNewPlugin (v);
        if (p == nullptr) return -1;
        ValueTree c (chainID);
        c.setProperty (idProp, id, nullptr);
        c.setProperty (nameProp, name.isNotEmpty() ? name : "Chain " + String (list.getNumChildren() + 1), nullptr);
        list.appendChild (c, um);
        rack.addPlugin (p, layoutPos (list.getNumChildren(), 0.8f), false);
        rewire (rack);
        return id;
    }

    /** Removes a chain and everything on it (never the last one). */
    inline bool removeChain (te::RackType& rack, int chain)
    {
        auto list = rack.state.getChildWithName (chainsID);
        if (list.getNumChildren() < 2) return false;
        auto c = list.getChildWithProperty (idProp, chain);
        if (! c.isValid()) return false;
        Array<te::Plugin::Ptr> gone;
        for (auto p : rack.getPlugins()) if (chainOf (*p) == chain) gone.add (p);
        for (auto p : gone) p->deleteFromParent();
        list.removeChild (c, &rack.edit.getUndoManager());
        rewire (rack);
        return true;
    }

    inline void renameChain (te::RackType& rack, int chain, const String& name)
    {
        auto c = rack.state.getChildWithName (chainsID).getChildWithProperty (idProp, chain);
        if (c.isValid() && name.trim().isNotEmpty()) c.setProperty (nameProp, name.trim(), &rack.edit.getUndoManager());
    }

    /** Puts an effect at the end of a chain. */
    inline bool addEffect (te::RackType& rack, int chain, te::Plugin::Ptr p)
    {
        if (p == nullptr || p->isSynth() || output (rack, chain) == nullptr) return false;
        auto fx = effects (rack, chain);
        p->state.setProperty (chainProp, chain, nullptr);
        p->state.setProperty (roleProp, "fx", nullptr);
        p->state.setProperty (orderProp, fx.isEmpty() ? 0 : (int) fx.getLast()->state[orderProp] + 1, nullptr);
        if (p->getOwnerTrack() != nullptr || p->getOwnerRackType() != nullptr) p->removeFromParent();
        rack.addPlugin (p, layoutPos (chain, 0.4f), false);
        rewire (rack);
        return true;
    }

    inline void removeEffect (te::RackType& rack, te::Plugin& p)
    {
        p.deleteFromParent();
        rewire (rack);
    }

    /** Moves an effect one place earlier (-1) or later (+1) in its chain. */
    inline void moveEffect (te::RackType& rack, te::Plugin& p, int dir)
    {
        auto fx = effects (rack, chainOf (p));
        const int i = fx.indexOf (&p), j = i + dir;
        if (i < 0 || j < 0 || j >= fx.size()) return;
        for (int k = 0; k < fx.size(); ++k)
            fx[k]->state.setProperty (orderProp, k == i ? j : k == j ? i : k, &rack.edit.getUndoManager());
        rewire (rack);
    }

    /** Listens to one chain alone (-1: to all of them again). Not kept in the project. */
    inline void setSolo (te::RackType& rack, int chain)
    {
        for (auto& ch : chains (rack))
            if (auto o = output (rack, ch.id))
            {
                o->soloed = ch.id == chain;
                o->silencedBySolo = chain >= 0 && ch.id != chain;
            }
    }

    inline int soloed (te::RackType& rack)
    {
        for (auto& ch : chains (rack))
            if (auto o = output (rack, ch.id); o != nullptr && o->soloed.load()) return ch.id;
        return -1;
    }

    /** A new Effect Rack with one empty chain, ready to be put on a track. */
    inline te::Plugin::Ptr create (te::Edit& edit, const String& firstChain = {})
    {
        auto rack = edit.getRackList().addNewRack();
        if (rack == nullptr) return {};
        rack->rackName = "Effect Rack";
        rack->state.setProperty (rackFlag, true, nullptr);
        while (rack->getInputNames().size() < 3)  rack->addInput (-1, rack->getInputNames().size() < 2 ? "Left" : "Right");
        while (rack->getOutputNames().size() < 3) rack->addOutput (-1, rack->getOutputNames().size() < 2 ? "Left" : "Right");
        addChain (*rack, firstChain);
        return edit.getPluginCache().createNewPlugin (te::RackInstance::create (*rack));
    }

    /** Puts a device of a track into a new rack, in its place: the device becomes the rack's first chain, ready for
        a second chain beside it. Returns the rack. */
    inline te::Plugin::Ptr wrap (te::Plugin& device)
    {
        auto* track = device.getOwnerTrack();
        te::PluginList* list = device.getOwnerList();
        if (list == nullptr || device.isSynth() || dynamic_cast<te::RackInstance*> (&device) != nullptr) return {};
        const int index = list->indexOf (&device);
        auto rackPlugin = create (device.edit, device.getName());
        if (rackPlugin == nullptr) return {};
        te::Plugin::Ptr keep (&device);
        auto* rack = rackOf (rackPlugin.get());
        addEffect (*rack, chains (*rack).front().id, keep);
        list->insertPlugin (rackPlugin, index, nullptr);
        ignoreUnused (track);
        return rackPlugin;
    }
}
