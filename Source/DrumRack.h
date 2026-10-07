#pragma once

#include "Common.h"
#include "PulseSynths.h"

/** Drum Rack built on a Tracktion RackType (Ableton-style, and then some):
      - 128 pads; a pad's slot is its default note (pad 36 = C1). Pads exist only once filled.
      - each pad: rack MIDI in -> Drum Pad (sample layers or synth, choke group, receive note)
                  -> its own effects -> Pad Mixer (volume / pan / mute / solo / send A / send B)
      - Pad Mixer main out -> rack L/R; sends -> Return A / Return B chains -> rack L/R.
      - 8 rack macros, bank view, copy / move / swap pads, extract a pad to its own track.
    Membership is stored on each plugin's state: "pulsePad" (slot, 128/129 = returns),
    "pulseRole" (src / fx / out) and "pulseOrder" (effect order). */
namespace DrumRack
{
    constexpr int numPads = 128, baseNote = 36, padsPerBank = 16, returnA = 128, returnB = 129, numMacros = 8;
    inline const Identifier padProp { "pulsePad" }, orderProp { "pulseOrder" }, rackFlag { "pulseDrumRack" },
                            roleProp { "pulseRole" }, versionProp { "pulseDrumRackVersion" }, bankProp { "pulseBank" };

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
    inline bool isReturnSlot (int slot) { return slot == returnA || slot == returnB; }

    inline pulse::DrumPad* source (te::RackType& rack, int slot)
    {
        for (auto p : rack.getPlugins())
            if (auto d = dynamic_cast<pulse::DrumPad*> (p))
                if (padOf (*d) == slot)
                    return d;
        return nullptr;
    }

    inline pulse::PadOut* output (te::RackType& rack, int slot)
    {
        for (auto p : rack.getPlugins())
            if (auto d = dynamic_cast<pulse::PadOut*> (p))
                if (padOf (*d) == slot)
                    return d;
        return nullptr;
    }

    /** The pad's (or return's) effects, in order. */
    inline Array<te::Plugin*> chain (te::RackType& rack, int slot)
    {
        Array<te::Plugin*> fx;
        for (auto p : rack.getPlugins())
            if (padOf (*p) == slot && dynamic_cast<pulse::DrumPad*> (p) == nullptr && dynamic_cast<pulse::PadOut*> (p) == nullptr
                && dynamic_cast<te::SamplerPlugin*> (p) == nullptr)
                fx.add (p);
        std::sort (fx.begin(), fx.end(), [] (auto* a, auto* b) { return (int) a->state[orderProp] < (int) b->state[orderProp]; });
        return fx;
    }

    /** Every plugin belonging to a slot (source, effects, output). */
    inline Array<te::Plugin*> allOf (te::RackType& rack, int slot)
    {
        Array<te::Plugin*> out;
        for (auto p : rack.getPlugins())
            if (padOf (*p) == slot)
                out.add (p);
        return out;
    }

    /** Slots that have a pad, low to high. */
    inline Array<int> usedPads (te::RackType& rack)
    {
        Array<int> s;
        for (auto p : rack.getPlugins())
            if (auto d = dynamic_cast<pulse::DrumPad*> (p))
                s.addIfNotAlreadyThere (padOf (*d));
        s.sort();
        return s;
    }

    //==============================================================================
    /** Rebuilds every connection in the rack from the plugins' pad / role / order tags. */
    inline void rewire (te::RackType& rack)
    {
        for (auto c : rack.getConnections())
            rack.removeConnection (c->sourceID.get(), c->sourcePin.get(), c->destID.get(), c->destPin.get());

        // the first node of each return chain (where the sends arrive)
        auto returnHead = [&rack] (int slot) -> te::Plugin*
        {
            auto fx = chain (rack, slot);
            if (! fx.isEmpty()) return fx.getFirst();
            return output (rack, slot);
        };

        auto wireChain = [&rack] (te::Plugin* first, const Array<te::Plugin*>& fx, te::Plugin* out)
        {
            te::Plugin* prev = first;
            for (auto p : fx)
            {
                if (prev != nullptr)
                {
                    rack.addConnection (prev->itemID, 1, p->itemID, 1);
                    rack.addConnection (prev->itemID, 2, p->itemID, 2);
                }
                prev = p;
            }
            if (out != nullptr && prev != nullptr && prev != out)
            {
                rack.addConnection (prev->itemID, 1, out->itemID, 1);
                rack.addConnection (prev->itemID, 2, out->itemID, 2);
            }
            auto* last = out != nullptr ? out : prev;
            if (last != nullptr)
            {
                rack.addConnection (last->itemID, 1, {}, 1);
                rack.addConnection (last->itemID, 2, {}, 2);
            }
        };

        auto* headA = returnHead (returnA);
        auto* headB = returnHead (returnB);

        for (int slot : usedPads (rack))
        {
            auto src = source (rack, slot);
            auto out = output (rack, slot);
            rack.addConnection ({}, 0, src->itemID, 0);   // MIDI in
            wireChain (src, chain (rack, slot), out);
            if (out != nullptr)
            {
                if (headA != nullptr) { rack.addConnection (out->itemID, 3, headA->itemID, 1); rack.addConnection (out->itemID, 4, headA->itemID, 2); }
                if (headB != nullptr) { rack.addConnection (out->itemID, 5, headB->itemID, 1); rack.addConnection (out->itemID, 6, headB->itemID, 2); }
            }
        }

        for (int r : { returnA, returnB })
        {
            auto fx = chain (rack, r);
            auto out = output (rack, r);
            if (fx.isEmpty() && out == nullptr) continue;
            wireChain (nullptr, fx, out);
        }
    }

    inline te::Plugin::Ptr makePlugin (te::Edit& edit, const String& type, int slot, const String& role)
    {
        ValueTree v (te::IDs::PLUGIN);
        v.setProperty (te::IDs::type, type, nullptr);
        v.setProperty (padProp, slot, nullptr);
        v.setProperty (roleProp, role, nullptr);
        return edit.getPluginCache().createNewPlugin (v);
    }

    inline juce::Point<float> layoutPos (int slot, float column) { return { column, ((float) slot + 1.0f) / 131.0f }; }

    inline void ensureReturns (te::RackType& rack)
    {
        for (int r : { returnA, returnB })
            if (output (rack, r) == nullptr)
                if (auto p = makePlugin (rack.edit, pulse::PadOut::xmlTypeName, r, "out"))
                    rack.addPlugin (p, layoutPos (r, 0.9f), false);
    }

    /** Creates a pad's source + mixer if it doesn't exist yet. */
    inline pulse::DrumPad* ensurePad (te::RackType& rack, int slot)
    {
        if (slot < 0 || slot >= numPads) return nullptr;
        if (auto s = source (rack, slot)) return s;
        auto src = makePlugin (rack.edit, pulse::DrumPad::xmlTypeName, slot, "src");
        auto out = makePlugin (rack.edit, pulse::PadOut::xmlTypeName, slot, "out");
        if (src == nullptr || out == nullptr) return nullptr;
        rack.addPlugin (src, layoutPos (slot, 0.15f), false);
        rack.addPlugin (out, layoutPos (slot, 0.75f), false);
        rewire (rack);
        return source (rack, slot);
    }

    //==============================================================================
    /** Current view: first note of the 4x4 bank shown (multiple of 4, 0..112). */
    inline int getBank (te::RackType& rack)            { return jlimit (0, 112, (int) rack.state.getProperty (bankProp, baseNote)); }
    inline void setBank (te::RackType& rack, int first) { rack.state.setProperty (bankProp, jlimit (0, 112, first / 4 * 4), nullptr); }

    inline String padName (te::RackType& rack, int slot)
    {
        if (auto s = source (rack, slot)) return s->displayName();
        return {};
    }

    /** The note a pad responds to (its slot unless remapped). */
    inline int noteOfPad (te::RackType& rack, int slot)
    {
        if (auto s = source (rack, slot)) return s->getInt (pulse::DrumPad::pNote);
        return slot;
    }

    /** The pad that plays a note (-1 = none). */
    inline int padForNote (te::RackType& rack, int note)
    {
        for (auto p : rack.getPlugins())
            if (auto d = dynamic_cast<pulse::DrumPad*> (p))
                if (d->getInt (pulse::DrumPad::pNote) == note)
                    return padOf (*d);
        return -1;
    }

    inline bool loadSample (te::RackType& rack, int slot, const File& f)
    {
        te::AudioFile af (rack.edit.engine, f);
        if (! af.isValid()) return false;
        auto s = ensurePad (rack, slot);
        if (s == nullptr) return false;
        s->setSample (f);
        return true;
    }

    inline bool addLayer (te::RackType& rack, int slot, const File& f)
    {
        te::AudioFile af (rack.edit.engine, f);
        if (! af.isValid()) return false;
        auto s = ensurePad (rack, slot);
        return s != nullptr && s->addLayer (f);
    }

    inline pulse::DrumPad* loadSynth (te::RackType& rack, int slot, int drumType)
    {
        auto s = ensurePad (rack, slot);
        if (s != nullptr) s->setSynth (drumType);
        return s;
    }

    inline void clearPad (te::RackType& rack, int slot)
    {
        for (auto p : allOf (rack, slot))
            p->deleteFromParent();
        rewire (rack);
    }

    /** Tune in semitones. */
    inline void setTune (te::RackType& rack, int slot, int semis)
    {
        if (auto s = source (rack, slot)) s->set (pulse::DrumPad::pTranspose, (float) semis);
    }

    inline int getTune (te::RackType& rack, int slot)
    {
        if (auto s = source (rack, slot)) return s->getInt (pulse::DrumPad::pTranspose);
        return 0;
    }

    inline void addEffect (te::RackType& rack, int slot, te::Plugin::Ptr p)
    {
        if (p == nullptr || p->isSynth()) return;
        if (! isReturnSlot (slot) && ensurePad (rack, slot) == nullptr) return;
        auto fx = chain (rack, slot);
        p->state.setProperty (padProp, slot, nullptr);
        p->state.setProperty (roleProp, "fx", nullptr);
        p->state.setProperty (orderProp, fx.isEmpty() ? 0 : (int) fx.getLast()->state[orderProp] + 1, nullptr);
        rack.addPlugin (p, layoutPos (slot, 0.45f), false);
        rewire (rack);
    }

    inline void removeEffect (te::RackType& rack, te::Plugin& p)
    {
        p.deleteFromParent();
        rewire (rack);
    }

    /** Moves an effect one place left (-1) or right (+1) in its chain. */
    inline void moveEffect (te::RackType& rack, te::Plugin& p, int dir)
    {
        auto fx = chain (rack, padOf (p));
        const int i = fx.indexOf (&p), j = i + dir;
        if (i < 0 || j < 0 || j >= fx.size()) return;
        for (int k = 0; k < fx.size(); ++k)   // renumber, swapping the two
            fx[k]->state.setProperty (orderProp, k == i ? j : k == j ? i : k, &rack.edit.getUndoManager());
        rewire (rack);
    }

    //==============================================================================
    /** Moves a pad to another slot (swapping if the target has a pad). Notes that followed the
        slot (receive note == slot) follow the move. */
    inline void movePad (te::RackType& rack, int from, int to)
    {
        if (from == to || from < 0 || to < 0 || from >= numPads || to >= numPads) return;
        auto a = allOf (rack, from), b = allOf (rack, to);
        auto retag = [] (const Array<te::Plugin*>& ps, int oldSlot, int newSlot)
        {
            for (auto p : ps)
            {
                p->state.setProperty (padProp, newSlot, &p->edit.getUndoManager());
                if (auto d = dynamic_cast<pulse::DrumPad*> (p); d != nullptr && d->getInt (pulse::DrumPad::pNote) == oldSlot)
                    d->set (pulse::DrumPad::pNote, (float) newSlot);
            }
        };
        retag (a, from, to);
        retag (b, to, from);
        rewire (rack);
    }

    /** Copies a pad (sound, settings, effects, mixer) onto another slot, replacing it. */
    inline void copyPad (te::RackType& rack, int from, int to)
    {
        if (from == to || from < 0 || to < 0 || from >= numPads || to >= numPads || source (rack, from) == nullptr) return;
        for (auto p : allOf (rack, to)) p->deleteFromParent();
        for (auto p : allOf (rack, from))
        {
            p->flushPluginStateToValueTree();
            auto v = p->state.createCopy();
            te::EditItemID::remapIDs (v, nullptr, rack.edit);
            v.setProperty (padProp, to, nullptr);
            if (auto np = rack.edit.getPluginCache().createNewPlugin (v))
            {
                rack.addPlugin (np, layoutPos (to, dynamic_cast<pulse::DrumPad*> (np.get()) != nullptr ? 0.15f : 0.6f), false);
                if (auto d = dynamic_cast<pulse::DrumPad*> (np.get()); d != nullptr && d->getInt (pulse::DrumPad::pNote) == from)
                    d->set (pulse::DrumPad::pNote, (float) to);
            }
        }
        rewire (rack);
    }

    /** First slot >= start without a pad (-1 = full). */
    inline int nextFreeSlot (te::RackType& rack, int start)
    {
        for (int s = jmax (0, start); s < numPads; ++s)
            if (source (rack, s) == nullptr) return s;
        return -1;
    }

    //==============================================================================
    // Rack macros (8 knobs on the Drum Rack that can drive any pad / effect parameter)
    inline void ensureMacros (te::RackType& rack)
    {
        auto& list = rack.getMacroParameterListForWriting();
        while (list.getMacroParameters().size() < numMacros)
            if (auto m = list.createMacroParameter())
                m->macroName = "Macro " + String (list.getMacroParameters().size());
    }

    inline ReferenceCountedArray<te::MacroParameter> macros (te::RackType& rack) { return rack.getMacroParameters(); }

    //==============================================================================
    /** General MIDI drum names for empty pads. */
    inline String defaultPadLabel (int slot)
    {
        static const char* gm[] = { "Kick 2", "Kick", "Rim", "Snare", "Clap", "Snare 2", "Low Tom", "Closed HH", "Low Tom 2",
                                    "Pedal HH", "Mid Tom", "Open HH", "Mid Tom 2", "High Tom", "Crash", "High Tom 2", "Ride",
                                    "China", "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap", "Ride 2",
                                    "Hi Bongo", "Lo Bongo", "Mute Conga", "Hi Conga", "Lo Conga", "Hi Timbale", "Lo Timbale",
                                    "Hi Agogo", "Lo Agogo", "Cabasa", "Maracas", "Whistle", "Whistle 2", "Guiro", "Guiro 2",
                                    "Claves", "Hi Block", "Lo Block", "Cuica", "Cuica 2", "Triangle", "Triangle 2" };
        if (slot >= 35 && slot <= 81) return gm[slot - 35];
        return MidiMessage::getMidiNoteName (slot, true, true, 3);
    }

    /** A playable synthesised kit on the GM slots (hats share a choke group). */
    inline const StringArray& kitStyles() { static StringArray s { "Classic", "808", "909", "Lo-fi", "Electro", "Techno", "Industrial" }; return s; }

    /** A playable synthesised kit on the GM slots (hats share a choke group), in a style:
        0 classic, 1 808 (long boomy kick, soft hats), 2 909 (punchy, bright), 3 lo-fi (dark,
        driven), 4 electro (snappy, bright, short). */
    /** The synth settings (tune, decay, tone, snap, drive) of one voice of a kit style. */
    inline std::array<float, 5> kitVoiceParams (int style, int slot, int type)
    {
        using D = pulse::DrumVoice;
        //                      tune  decay  tone  snap  drive
        const float styles[7][5] = { { 0.0f, 1.0f, 0.5f, 0.5f, 0.0f },
                                     { -3.0f, 1.7f, 0.25f, 0.3f, 0.1f },
                                     { 2.0f, 0.8f, 0.7f, 0.9f, 0.2f },
                                     { -3.0f, 0.8f, 0.15f, 0.4f, 0.7f },
                                     { 0.0f, 0.6f, 0.85f, 1.0f, 0.35f },
                                     { 1.0f, 0.85f, 0.6f, 0.85f, 0.5f },     // Techno: tight, punchy, driven
                                     { -2.0f, 1.1f, 0.9f, 1.0f, 0.95f } };   // Industrial: bright, hard, as driven as it goes
        const auto& st = styles[jlimit (0, 6, style)];
        const float tomTune = type == D::tom ? (slot == 41 ? -5.0f : slot == 48 ? 4.0f : 0.0f) : 0.0f;
        return { tomTune + (type == D::kick || type == D::tom ? st[0] : st[0] * 0.5f),
                 type == D::kick ? st[1] : jmin (2.0f, 0.5f + st[1] * 0.5f), st[2], st[3], st[4] };
    }

    inline void fillSynthKit (te::RackType& rack, int style = 0)
    {
        using D = pulse::DrumVoice;
        using P = pulse::DrumPad;
        const std::pair<int, int> kit[] = { { 36, D::kick }, { 37, D::rim }, { 38, D::snare }, { 39, D::clap }, { 41, D::tom },
                                            { 42, D::closedHat }, { 45, D::tom }, { 46, D::openHat }, { 48, D::tom },
                                            { 49, D::cymbal }, { 56, D::cowbell } };
        for (auto [slot, type] : kit)
            if (auto p = loadSynth (rack, slot, type))
            {
                const auto v = kitVoiceParams (style, slot, type);
                p->set (P::pSynthTune, v[0]);
                p->set (P::pSynthDecay, v[1]);
                p->set (P::pSynthTone, v[2]);
                p->set (P::pSynthSnap, v[3]);
                p->set (P::pSynthDrive, v[4]);
                if (type == D::closedHat || type == D::openHat) p->set (P::pChoke, 1.0f);
            }
    }

    /** Creates a Drum Rack instance ready to insert into a track. */
    inline te::Plugin::Ptr create (te::Edit& edit, bool withSynthKit = false)
    {
        auto rack = edit.getRackList().addNewRack();
        if (rack == nullptr) return {};

        rack->rackName = "Drum Rack";
        rack->state.setProperty (rackFlag, true, nullptr);
        rack->state.setProperty (versionProp, 2, nullptr);
        if (rack->getOutputNames().isEmpty())
        {
            rack->addOutput (-1, "Left");
            rack->addOutput (-1, "Right");
        }
        ensureReturns (*rack);
        if (withSynthKit) fillSynthKit (*rack);
        rewire (*rack);
        return edit.getPluginCache().createNewPlugin (te::RackInstance::create (*rack));
    }

    /** Upgrades a rack made by Pulse 0.17 or older (16 Samplers on slots 0-15) in place. */
    inline bool migrate (te::RackType& rack)
    {
        if (! (bool) rack.state.getProperty (rackFlag) || (int) rack.state.getProperty (versionProp, 1) >= 2)
            return false;

        rack.state.setProperty (versionProp, 2, nullptr);
        Array<te::Plugin*> old;
        for (auto p : rack.getPlugins())
            if (padOf (*p) >= 0 && padOf (*p) < 16)
                old.add (p);

        for (auto p : old)
        {
            const int slot = baseNote + padOf (*p);
            if (auto s = dynamic_cast<te::SamplerPlugin*> (p))
            {
                if (s->getNumSounds() > 0)
                {
                    const File f (s->getSoundFile (0).getFile());
                    if (auto d = ensurePad (rack, slot))
                    {
                        d->setSample (f);
                        d->set (pulse::DrumPad::pTranspose, (float) (slot - s->getKeyNote (0)));
                        d->set (pulse::DrumPad::pGain, s->getSoundGainDb (0));
                    }
                    if (auto o = output (rack, slot))
                        o->set (pulse::PadOut::pPan, s->getSoundPan (0));
                }
                s->deleteFromParent();
            }
            else
            {
                p->state.setProperty (padProp, slot, nullptr);
                p->state.setProperty (roleProp, "fx", nullptr);
            }
        }
        // effects on pads that were empty keep a pad so they aren't orphaned
        for (auto p : rack.getPlugins())
            if (padOf (*p) >= 0 && padOf (*p) < numPads && source (rack, padOf (*p)) == nullptr)
                ensurePad (rack, padOf (*p));
        ensureReturns (rack);
        rewire (rack);
        return true;
    }

    inline void migrateAll (te::Edit& edit)
    {
        for (auto r : edit.getRackList().getTypes())
            migrate (*r);
    }

    //==============================================================================
    /** Moves a pad to its own new track: the pad's sound as the instrument, its effects after it,
        its mixer settings on the track, and its notes moved from this track's MIDI clips. */
    inline te::AudioTrack* extractToTrack (te::AudioTrack& track, te::RackType& rack, int slot)
    {
        auto src = source (rack, slot);
        if (src == nullptr) return nullptr;
        auto& edit = track.edit;
        auto& um = edit.getUndoManager();

        auto nt = edit.insertNewAudioTrack (te::TrackInsertPoint (track, false), nullptr);
        if (nt == nullptr) return nullptr;
        auto name = src->displayName();
        nt->setName (name.isNotEmpty() ? name : defaultPadLabel (slot));
        nt->setColour (track.getColour());

        int index = 0;
        auto copyIn = [&] (te::Plugin& p)
        {
            p.flushPluginStateToValueTree();
            auto v = p.state.createCopy();
            te::EditItemID::remapIDs (v, nullptr, edit);
            for (auto id : { padProp, roleProp, orderProp }) v.removeProperty (id, nullptr);
            if (auto np = edit.getPluginCache().createNewPlugin (v))
                nt->pluginList.insertPlugin (np, index++, nullptr);
        };
        copyIn (*src);
        for (auto p : chain (rack, slot)) copyIn (*p);
        if (auto o = output (rack, slot))
            if (auto vp = nt->getVolumePlugin())
            {
                vp->setVolumeDb (jmax (-60.0f, o->get (pulse::PadOut::pVol)));
                vp->setPan (o->get (pulse::PadOut::pPan));
            }

        const int note = src->getInt (pulse::DrumPad::pNote);
        for (auto c : track.getClips())
            if (auto mc = dynamic_cast<te::MidiClip*> (c))
            {
                Array<te::MidiNote*> mine;
                for (auto n : mc->getSequence().getNotes())
                    if (n->getNoteNumber() == note) mine.add (n);
                if (mine.isEmpty()) continue;

                auto nc = nt->insertMIDIClip (mc->getName(), mc->getEditTimeRange(), nullptr);
                if (nc == nullptr) continue;
                nc->setOffset (mc->getPosition().getOffset());
                if (mc->isLooping()) nc->setLoopRangeBeats (mc->getLoopRangeBeats());
                for (auto n : mine)
                {
                    nc->getSequence().addNote (note, n->getStartBeat(), n->getLengthBeats(), n->getVelocity(), n->getColour(), &um);
                    mc->getSequence().removeNote (*n, &um);
                }
            }

        clearPad (rack, slot);
        return nt.get();
    }

    //==============================================================================
    /** Kit pieces used by "Randomise kit": slot, filename keywords, synth fallback. */
    struct KitPiece { int slot; StringArray keywords; int synthType; int choke; };

    inline std::vector<KitPiece> kitPieces()
    {
        using D = pulse::DrumVoice;
        return { { 36, { "kick", "bd", "bassdrum" }, D::kick, 0 },
                 { 37, { "rim", "side" }, D::rim, 0 },
                 { 38, { "snare", "sd", "snr" }, D::snare, 0 },
                 { 39, { "clap", "cp" }, D::clap, 0 },
                 { 42, { "closed", "chh", "hhc", "hat", "hh" }, D::closedHat, 1 },
                 { 46, { "open", "ohh", "hho" }, D::openHat, 1 },
                 { 45, { "tom" }, D::tom, 0 },
                 { 49, { "crash", "cymbal", "ride" }, D::cymbal, 0 },
                 { 56, { "perc", "cowbell", "shaker", "conga", "bongo" }, D::cowbell, 0 } };
    }

    /** Audio files under the folders (bounded, so a huge library doesn't stall the UI). */
    inline Array<File> findSamples (const Array<File>& folders, int maxFiles = 6000)
    {
        Array<File> out;
        for (auto& d : folders)
        {
            if (! d.isDirectory()) continue;
            for (const auto& e : RangedDirectoryIterator (d, true, "*.wav;*.aif;*.aiff;*.flac", File::findFiles))
            {
                if (e.getFileSize() < 4 * 1024 * 1024) out.add (e.getFile());
                if (out.size() >= maxFiles) return out;
            }
        }
        return out;
    }

    /** Picks a random sample for each kit piece from `samples` (by file name), or a randomised
        synth drum when nothing matches. Returns how many pads got samples. */
    inline int randomiseKit (te::RackType& rack, const Array<File>& samples, Random& rng)
    {
        int fromFiles = 0;
        for (auto& piece : kitPieces())
        {
            Array<File> matches;
            for (auto& f : samples)
            {
                const auto name = f.getFileNameWithoutExtension().toLowerCase();
                for (auto& k : piece.keywords)
                    if (k.length() <= 3 ? (name.startsWith (k) || name.contains ("_" + k) || name.contains (" " + k) || name.contains ("-" + k))
                                        : name.contains (k))
                    {
                        matches.add (f);
                        break;
                    }
            }

            pulse::DrumPad* p = nullptr;
            if (! matches.isEmpty() && loadSample (rack, piece.slot, matches[rng.nextInt (matches.size())]))
            {
                p = source (rack, piece.slot);
                ++fromFiles;
            }
            else
            {
                p = loadSynth (rack, piece.slot, piece.synthType);
                if (p != nullptr)
                {
                    p->set (pulse::DrumPad::pSynthTune, (rng.nextFloat() - 0.5f) * 8.0f);
                    p->set (pulse::DrumPad::pSynthDecay, 0.6f + rng.nextFloat() * 0.9f);
                    p->set (pulse::DrumPad::pSynthTone, rng.nextFloat());
                    p->set (pulse::DrumPad::pSynthSnap, rng.nextFloat());
                    p->set (pulse::DrumPad::pSynthDrive, rng.nextFloat() * 0.4f);
                }
            }
            if (p != nullptr) p->set (pulse::DrumPad::pChoke, (float) piece.choke);
        }
        return fromFiles;
    }
}
