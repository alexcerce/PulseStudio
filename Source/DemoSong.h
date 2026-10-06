#pragma once

#include "Common.h"
#include "Routing.h"
#include "DrumRack.h"
#include "PulseSynths.h"
#include "Sections.h"
#include "Chords.h"
#include "Vca.h"
#include "Mastering.h"
#include "SongKey.h"
#include "Notes.h"
#include "FactoryPresets.h"

/** The demo song: a whole techno track, start to finish, made of nothing but the app's own instruments and
    effects. 132 BPM, 120 bars (about three minutes forty), in A with the flat second that makes it dark.

    Intro (16 bars) - Build (16) - Drop (24) - Breakdown (16) - Drop II (32) - Outro (16).

    What is in it, so that it also shows what the app does: a 909 kick on a track of its own, which the bass and
    the drone duck under (sidechain compressors keyed from it); hats, clap and a rim that plays three against the
    kick's four; a rolling bass and an acid line from the Analog synth, the acid's filter opened and closed by
    automation across the whole song; chord stabs into a ping-pong delay; a slow drone; noise risers into each
    drop; a "Metal" return where the drums ring through tuned resonators; and a mastering chain on the master. */
namespace DemoSong
{
    constexpr double bpm = 132.0;
    constexpr int totalBars = 120;

    struct Section { const char* name; int from, to; };
    inline const std::vector<Section>& sections()
    {
        static const std::vector<Section> s { { "Intro", 0, 16 }, { "Build", 16, 32 }, { "Drop", 32, 56 }, { "Breakdown", 56, 72 }, { "Drop II", 72, 104 }, { "Outro", 104, 120 } };
        return s;
    }

    inline void build (te::Edit& edit)
    {
        edit.tempoSequence.getTempo (0)->setBpm (bpm);
        auto bars = [&edit] (double b) { return EngineHelpers::beatToTime (edit, b * 4.0); };
        auto setP = [] (te::Plugin* p, const char* id, float v)
        {
            if (p != nullptr) if (auto prm = p->getAutomatableParameterByID (id)) prm->setParameter (v, sendNotification);
        };
        /** A straight line of automation between two bars. */
        auto ramp = [&] (te::Plugin* p, const char* id, double bar0, float v0, double bar1, float v1)
        {
            if (p == nullptr) return;
            if (auto prm = p->getAutomatableParameterByID (id))
            {
                prm->getCurve().addPoint (bars (bar0), v0, 0.0f, nullptr);
                prm->getCurve().addPoint (bars (bar1), v1, 0.0f, nullptr);
            }
        };

        struct Def { const char* name; int colour; float volume; };
        const Def defs[] = { { "Kick", 0, -9.0f }, { "Drums", 3, -15.0f }, { "Bass", 4, -1.0f }, { "Acid", 2, -11.0f }, { "Stab", 1, -8.0f }, { "Drone", 6, -3.0f }, { "Riser", 5, -12.0f } };
        enum { kick, drums, bass, acid, stab, drone, riser, numTracks };
        edit.ensureNumberOfAudioTracks (numTracks);
        auto tracks = EngineHelpers::getUserTracks (edit);
        if (tracks.size() < numTracks) return;
        for (int i = 0; i < numTracks; ++i)
        {
            tracks[i]->setName (defs[i].name);
            tracks[i]->setColour (Theme::trackColour (defs[i].colour));
            if (auto vp = tracks[i]->getVolumePlugin()) vp->setVolumeDb (defs[i].volume);
        }

        /** One clip for a stretch of bars; `fill` is asked for each bar's notes (bar number in the song; beat within the bar). */
        using AddNote = std::function<void (int note, double beatInBar, double lengthBeats, int velocity)>;
        auto part = [&] (int track, const String& name, int fromBar, int toBar, const std::function<void (int bar, const AddNote&)>& fill)
        {
            auto mc = tracks[track]->insertMIDIClip (name, { bars (fromBar), bars (toBar) }, nullptr);
            if (mc == nullptr) return;
            auto& seq = mc->getSequence();
            for (int bar = fromBar; bar < toBar; ++bar)
                fill (bar, [&] (int note, double beatInBar, double len, int vel)
                {
                    seq.addNote (note, te::BeatPosition::fromBeats ((bar - fromBar) * 4.0 + beatInBar), te::BeatDuration::fromBeats (len), jlimit (1, 127, vel), 0, nullptr);
                });
        };
        auto sectionOf = [] (int bar) -> const Section& { for (auto& s : sections()) if (bar >= s.from && bar < s.to) return s; return sections().back(); };
        /** The same thing over several sections: one clip for each, named after it. */
        auto parts = [&] (int track, std::initializer_list<std::pair<int, int>> ranges, const std::function<void (int bar, const AddNote&)>& fill)
        {
            for (auto [from, to] : ranges) part (track, sectionOf (from).name, from, to, fill);
        };

        //==============================================================================
        // Instruments
        auto drumRack = [&] (int track) -> te::RackType*
        {
            auto rp = DrumRack::create (edit, false);
            if (rp == nullptr) return nullptr;
            auto* rack = DrumRack::rackOf (rp.get());
            DrumRack::fillSynthKit (*rack, 2);   // the 909
            DrumRack::rewire (*rack);
            tracks[track]->pluginList.insertPlugin (rp, 0, nullptr);
            return rack;
        };
        if (auto* rack = drumRack (kick))
            if (auto pad = DrumRack::source (*rack, 36)) { pad->set (pulse::DrumPad::pSynthDecay, pad->get (pulse::DrumPad::pSynthDecay) * 1.15f); pad->set (pulse::DrumPad::pSynthDrive, 0.45f); }
        drumRack (drums);

        auto device = [&] (int track, const char* type) -> te::Plugin*
        {
            auto p = edit.getPluginCache().createNewPlugin (type, {});
            if (p == nullptr) return nullptr;
            // instruments first, effects after what is there already (in front of the track's own fader)
            const bool instrument = EngineHelpers::isInstrument (p.get());
            int index = 0;
            if (! instrument) { index = tracks[track]->pluginList.size(); if (auto vp = tracks[track]->getVolumePlugin()) index = jmin (index, tracks[track]->pluginList.indexOf (vp)); }
            tracks[track]->pluginList.insertPlugin (p, index, nullptr);
            return p.get();
        };
        /** A compressor that ducks the track under the kick. */
        auto duck = [&] (int track, float threshold, float release)
        {
            if (auto c = device (track, pulse::ProCompressor::xmlTypeName))
            {
                setP (c, "threshold", threshold); setP (c, "ratio", 8.0f); setP (c, "attack", 0.5f); setP (c, "release", release); setP (c, "knee", 4.0f);
                c->setSidechainSourceID (tracks[kick]->itemID);
            }
        };

        // Bass: one voice, a saw with a sub under it, a short pluck of the filter on every note
        auto* bassSynth = device (bass, pulse::Analog::xmlTypeName);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "wave1", 0 }, { "wave2", 1 }, { "oct2", -1 }, { "mix", 0.35f }, { "sub", 0.55f }, { "cutoff", 650 }, { "reso", 0.22f },
                 { "envamt", 0.42f }, { "fdrive", 0.45f }, { "fa", 1 }, { "fd", 130 }, { "fs", 0.05f }, { "fr", 90 }, { "a", 1 }, { "d", 170 }, { "s", 0.6f }, { "r", 60 }, { "mode", 1 }, { "glide", 0 },
                 { "keytrack", 0.2f }, { "drift", 0.1f }, { "level", 12 } })
            setP (bassSynth, id, v);
        if (auto s = device (bass, pulse::Saturator::xmlTypeName)) { setP (s, "drive", 12.0f); setP (s, "type", 1.0f); setP (s, "mix", 0.6f); }
        duck (bass, -22.0f, 110.0f);

        // Acid: the same synth told to squeal: a pulse wave, the filter nearly ringing, notes that slide into each other
        auto* acidSynth = device (acid, pulse::Analog::xmlTypeName);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "wave1", 1 }, { "pw", 0.32f }, { "wave2", 0 }, { "mix", 0.3f }, { "sub", 0.0f }, { "cutoff", 420 }, { "reso", 0.8f },
                 { "envamt", 0.5f }, { "fdrive", 0.6f }, { "fa", 1 }, { "fd", 190 }, { "fs", 0.1f }, { "fr", 120 }, { "a", 1 }, { "d", 240 }, { "s", 0.5f }, { "r", 80 }, { "mode", 1 }, { "glide", 55 },
                 { "keytrack", 0.5f }, { "drift", 0.25f }, { "level", 10 } })
            setP (acidSynth, id, v);
        if (auto s = device (acid, pulse::Saturator::xmlTypeName)) { setP (s, "drive", 10.0f); setP (s, "type", 2.0f); setP (s, "mix", 0.5f); }
        if (auto d = device (acid, pulse::Delay::xmlTypeName)) { setP (d, "sync", 1.0f); setP (d, "note", 5.0f); setP (d, "feedback", 0.38f); setP (d, "pingpong", 1.0f); setP (d, "lowcut", 400.0f); setP (d, "mix", 0.18f); }

        // Stab: a short minor chord from the wavetable synth
        auto* stabSynth = device (stab, pulse::WavetableSynth::xmlTypeName);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "set1", 1 }, { "pos1", 0.35f }, { "voices", 4 }, { "detune", 0.28f }, { "cutoff", 1500 }, { "reso", 0.3f }, { "fenv", 0.55f },
                 { "fa", 1 }, { "fd", 160 }, { "fs", 0.0f }, { "a", 2 }, { "d", 200 }, { "s", 0.0f }, { "r", 140 }, { "level", 3 } })
            setP (stabSynth, id, v);
        if (auto f = device (stab, pulse::AutoFilter::xmlTypeName)) { setP (f, "type", 1.0f); setP (f, "cutoff", 1400.0f); setP (f, "reso", 0.35f); setP (f, "lfoamount", 0.45f); setP (f, "lfosync", 6.0f); setP (f, "mix", 0.7f); }

        // Drone: slow, wide and dark, breathing under the kick
        auto* droneSynth = device (drone, pulse::WavetableSynth::xmlTypeName);
        if (auto w = dynamic_cast<pulse::PulsePlugin*> (droneSynth)) FactoryPresets::apply (*w, "Dark Cinematic Drone");
        setP (droneSynth, "a", 1800.0f); setP (droneSynth, "r", 4000.0f); setP (droneSynth, "level", 6.0f);
        duck (drone, -26.0f, 220.0f);

        // Riser: filtered noise that opens up into each drop
        auto* riserSynth = device (riser, pulse::Analog::xmlTypeName);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "noise", 1.0f }, { "mix", 0.5f }, { "wave1", 3 }, { "wave2", 3 }, { "cutoff", 200 }, { "reso", 0.45f }, { "envamt", 0.0f },
                 { "keytrack", 0.0f }, { "a", 400 }, { "s", 1.0f }, { "r", 900 }, { "level", 4 } })
            setP (riserSynth, id, v);

        //==============================================================================
        // The parts
        auto inAny = [] (int bar, std::initializer_list<std::pair<int, int>> ranges) { for (auto [a, b] : ranges) if (bar >= a && bar < b) return true; return false; };
        const auto lastBarBeforeDrop = [] (int bar) { return bar == 31 || bar == 71; };

        // Kick: four to the floor; it steps aside for the last bar before each drop
        parts (kick, { { 0, 16 }, { 16, 32 }, { 32, 56 }, { 72, 104 }, { 104, 116 } }, [&] (int bar, const AddNote& add)
        {
            if (lastBarBeforeDrop (bar)) { add (36, 0.0, 0.25, 118); return; }
            for (int b = 0; b < 4; ++b) add (36, b, 0.25, b == 0 ? 124 : 118);
            if (bar % 8 == 7 && bar >= 32) add (36, 3.5, 0.2, 96);   // a pick-up into the next phrase
        });

        // Drums: hats, clap, a rim in threes over the kick's fours, toms to turn each phrase round
        parts (drums, { { 8, 16 }, { 16, 32 }, { 32, 56 }, { 56, 72 }, { 72, 104 }, { 104, 112 } }, [&] (int bar, const AddNote& add)
        {
            const bool breakdown = bar >= 56 && bar < 72, full = inAny (bar, { { 32, 56 }, { 72, 104 } });
            if (! breakdown)
            {
                static const int hatVel[] = { 52, 34, 98, 40 };
                for (int s = 0; s < 16; ++s)
                    if (bar >= 16 || s % 2 == 0)   // eighths in the intro, sixteenths after
                        add (42, s * 0.25, 0.08, hatVel[s % 4] + ((bar * 7 + s * 3) % 9) - 4);
            }
            if (bar >= 16 && ! breakdown && bar < 108)
                for (int b = 0; b < 4; ++b) add (46, b + 0.5, 0.2, 86);                    // open hat off the beat
            if ((bar >= 24 && bar < 56) || (bar >= 72 && bar < 104)) { add (39, 1.0, 0.2, 104); add (39, 3.0, 0.2, 108); }   // clap
            if (breakdown && bar >= 64) add (39, 3.0, 0.2, 60 + (bar - 64) * 6);            // the clap comes back, louder each bar
            if (full)
                for (int k = 0; k * 0.75 < 4.0; ++k)                                        // the rim: every three sixteenths
                    if ((bar * 5 + k) % 7 != 3) add (37, k * 0.75, 0.1, 62 + ((bar + k * 2) % 4) * 9);
            if (bar >= 80 && bar < 104)
                for (int s : { 0, 3, 6, 10, 13 }) add (56, s * 0.25, 0.1, 48 + (s % 3) * 8); // five against sixteen, quietly
            if (bar >= 88 && bar < 104)
                for (int b = 0; b < 4; ++b) add (49, b + 0.5, 0.3, 44);                     // a wash of cymbal on the off-beats
            if (bar == 32 || bar == 72) add (49, 0.0, 1.0, 112);                            // the crash on each drop
            if (bar % 8 == 7 && ! breakdown && bar >= 16)
                for (int s = 0; s < 4; ++s) add (s < 2 ? 45 : 41, 3.0 + s * 0.25, 0.12, 70 + s * 14);   // tom fill
            if (lastBarBeforeDrop (bar))
                for (int s = 0; s < 16; ++s) add (38, s * 0.25, 0.1, 40 + s * 5);           // a snare roll into the drop
        });

        // Bass: the three sixteenths between the kicks, on the root; now and then it leans on the flat second
        parts (bass, { { 16, 32 }, { 32, 56 }, { 72, 104 }, { 104, 108 } }, [&] (int bar, const AddNote& add)
        {
            if (lastBarBeforeDrop (bar)) return;
            for (int b = 0; b < 4; ++b)
                for (int s = 1; s < 4; ++s)
                {
                    int note = 33;                                           // A1
                    if (bar % 4 == 3 && b == 3) note = s == 3 ? 31 : 34;     // Bb, Bb, G: the turn at the end of four bars
                    if (bar % 8 == 5 && b == 2 && s == 2) note = 45;         // an octave jump
                    add (note, b + s * 0.25, 0.2, s == 2 ? 112 : 92);
                }
        });

        // Acid: sixteen steps in A with the flat second, some held into the next so that they slide
        struct Step { int note; int velocity; bool slide; };
        static const Step acidA[16] = { { 45, 96, false }, { 45, 70, false }, { 57, 120, true }, { 45, 80, false }, { 48, 92, false }, { 0, 0, false }, { 46, 118, true }, { 57, 84, false },
                                        { 45, 96, false }, { 52, 112, true }, { 45, 72, false }, { 46, 90, false }, { 0, 0, false }, { 57, 122, true }, { 48, 86, false }, { 46, 104, false } };
        static const Step acidB[16] = { { 45, 100, false }, { 57, 118, true }, { 45, 74, false }, { 60, 120, true }, { 58, 96, false }, { 45, 80, false }, { 0, 0, false }, { 52, 110, true },
                                        { 45, 96, false }, { 45, 70, false }, { 58, 122, true }, { 57, 90, false }, { 0, 0, false }, { 48, 100, false }, { 46, 116, true }, { 45, 84, false } };
        parts (acid, { { 24, 32 }, { 32, 56 }, { 56, 72 }, { 72, 104 }, { 104, 112 } }, [&] (int bar, const AddNote& add)
        {
            if (bar >= 60 && bar < 64) return;                               // four bars of air in the breakdown
            const Step* pattern = (bar >= 72 && bar % 4 >= 2) || bar % 8 == 7 ? acidB : acidA;
            for (int s = 0; s < 16; ++s)
                if (pattern[s].note > 0) add (pattern[s].note, s * 0.25, pattern[s].slide ? 0.34 : 0.19, pattern[s].velocity);
        });
        // the acid's filter, across the whole song: a glimpse in the build, opening through the drop, shut and
        // re-opened over the breakdown, wide open and moving in the second drop, closing as it leaves
        ramp (acidSynth, "cutoff", 24, 180, 32, 520);
        ramp (acidSynth, "cutoff", 32.01, 420, 48, 1700);
        ramp (acidSynth, "cutoff", 48.01, 1700, 56, 2700);
        ramp (acidSynth, "cutoff", 56.01, 2400, 60, 240);
        ramp (acidSynth, "cutoff", 64, 220, 72, 2000);
        for (int b = 72; b < 104; b += 8) { ramp (acidSynth, "cutoff", b + 0.01, 1100, b + 6, 4300); ramp (acidSynth, "cutoff", b + 6.01, 4300, b + 8, 1300); }
        ramp (acidSynth, "cutoff", 104.01, 1300, 112, 160);
        ramp (acidSynth, "reso", 56, 0.8f, 72, 0.9f);
        ramp (acidSynth, "reso", 72.01, 0.8f, 104, 0.84f);

        // Stab: an A minor chord thrown at the delay; in the breakdown it is all there is to hold on to
        parts (stab, { { 40, 56 }, { 56, 72 }, { 72, 104 } }, [&] (int bar, const AddNote& add)
        {
            const bool flat = bar % 8 >= 6;                                  // the last two bars of eight lean on Bb
            const int chord[3] = { flat ? 58 : 57, flat ? 62 : 60, flat ? 65 : 64 };
            auto hit = [&] (double at, int vel) { for (int n : chord) add (n, at, 0.22, vel); };
            if (bar >= 56 && bar < 72) { if (bar % 2 == 0) hit (0.0, 96); if (bar >= 64) hit (2.5, 78); return; }
            hit (1.5, 100); hit (3.25, 86);
            if (bar % 4 == 3) hit (2.75, 74);
            if (bar >= 88 && bar % 2 == 1) hit (0.75, 70);
        });

        // Drone: the root and its fifth, held eight bars at a time; a third joins for the breakdown
        parts (drone, { { 0, 16 }, { 16, 32 }, { 32, 56 }, { 56, 72 }, { 72, 104 }, { 104, 120 } }, [&] (int bar, const AddNote& add)
        {
            if (bar % 8 != 0) return;
            const int vel = bar >= 56 && bar < 72 ? 96 : 74;
            for (int n : { 33, 40, 45 }) add (n, 0.0, 31.5, vel);
            if (bar >= 56 && bar < 72) { add (60, 0.0, 31.5, 70); add (64, 0.0, 31.5, 64); }
        });

        // Riser: one long note of noise into each drop, and one falling away after the last
        for (auto [from, to] : std::initializer_list<std::pair<int, int>> { { 28, 32 }, { 64, 72 }, { 100, 108 } })
            part (riser, from == 100 ? "Fall" : "Rise", from, to, [&, from = from, to = to] (int bar, const AddNote& add) { if (bar == from) add (60, 0.0, (to - from) * 4.0 - 0.25, 100); });
        ramp (riserSynth, "cutoff", 28, 220, 32, 9000);
        ramp (riserSynth, "cutoff", 64, 180, 72, 11000);
        ramp (riserSynth, "cutoff", 100, 300, 104, 7000);
        ramp (riserSynth, "cutoff", 104.01, 7000, 108, 200);

        //==============================================================================
        // Returns, sends, groups, master
        if (auto rev = Routing::createReturnTrack (edit, "Reverb", pulse::Reverb::xmlTypeName))
        {
            rev->setColour (Theme::trackColour (5));
            auto r = rev->pluginList.findFirstPluginOfType<pulse::Reverb>();
            for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "size", 0.85f }, { "decay", 4.2f }, { "damping", 0.55f }, { "lowcut", 260.0f }, { "predelay", 22.0f }, { "mix", 1.0f } }) setP (r, id, v);
        }
        if (auto del = Routing::createReturnTrack (edit, "Delay", pulse::Delay::xmlTypeName))
        {
            del->setColour (Theme::trackColour (3));
            auto d = del->pluginList.findFirstPluginOfType<pulse::Delay>();
            for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "sync", 1.0f }, { "note", 5.0f }, { "feedback", 0.52f }, { "pingpong", 1.0f }, { "lowcut", 350.0f }, { "highcut", 5200.0f }, { "mix", 1.0f } }) setP (d, id, v);
        }
        if (auto metal = Routing::createReturnTrack (edit, "Metal", pulse::Resonators::xmlTypeName))
        {
            metal->setColour (Theme::trackColour (2));
            auto r = metal->pluginList.findFirstPluginOfType<pulse::Resonators>();
            setP (r, "decay", 2.2f); setP (r, "colour", 5200.0f);
            if (auto vp = metal->getVolumePlugin()) vp->setVolumeDb (-8.0f);
        }
        Routing::setSendDb (*tracks[drums], 0, -20.0f);
        Routing::setSendDb (*tracks[drums], 2, -16.0f);    // the drums ring through the resonators
        Routing::setSendDb (*tracks[acid], 0, -15.0f);
        Routing::setSendDb (*tracks[stab], 0, -10.0f);
        Routing::setSendDb (*tracks[stab], 1, -5.0f);
        Routing::setSendDb (*tracks[drone], 0, -7.0f);
        Routing::setSendDb (*tracks[riser], 0, -6.0f);
        Routing::setSendDb (*tracks[riser], 1, -12.0f);

        const int rhythm = Vca::add (edit, "Rhythm");
        for (int t : { (int) kick, (int) drums, (int) bass }) Vca::join (edit, rhythm, *tracks[t]);
        const int synths = Vca::add (edit, "Synths");
        for (int t : { (int) acid, (int) stab, (int) drone }) Vca::join (edit, synths, *tracks[t]);

        if (auto* lim = Mastering::addChain (edit)) lim->set (pulse::Limiter::pGain, 6.0f);

        //==============================================================================
        // The map of the song: sections, the chord it sits on, its key, and a note about it
        for (auto& s : sections()) Sections::add (edit, s.name, s.from * 4.0, (s.to - s.from) * 4.0);
        for (int bar = 0; bar < totalBars; bar += 8)
        {
            Chords::add (edit, bar * 4.0, 24.0, 9, Chords::minor);                    // A minor for six bars
            Chords::add (edit, bar * 4.0 + 24.0, 8.0, 10, Chords::major);             // Bb for two
        }
        SongKey::set (edit, 0);   // A minor (the notes of C major)
        Notes::setProject (edit, "Demo: a techno track made only of Pulse's own instruments and effects. 132 BPM, A minor with a flat second.\n\n"
                                 "Things to look at:\n"
                                 "- Acid: open its automation lane (A on the track) to see the filter's line across the song.\n"
                                 "- Bass and Drone: a compressor on each is keyed from the Kick, which is why they duck.\n"
                                 "- Drums: the rim plays every three sixteenths against the kick's four.\n"
                                 "- Metal (a return): the drums ring through tuned resonators.\n"
                                 "- Mixer: two VCA faders, Rhythm and Synths; the mastering chain on the master.\n"
                                 "- Session view (Tab): the same parts as clips in four scenes.");
        Notes::set (*tracks[acid], "The squeal is resonance at 80% and a filter that the automation opens and closes.\nNotes marked longer in the piano roll overlap the next one: that is the slide.");

        //==============================================================================
        // Session view: four scenes, each a four-bar loop taken from a place in the arrangement
        {
            struct Scene { const char* name; int fromBar; };
            const Scene scenes[] = { { "Intro", 8 }, { "Build", 24 }, { "Drop", 48 }, { "Breakdown", 64 } };
            auto& sl = edit.getSceneList();
            sl.ensureNumberOfScenes (8);
            for (int s = 0; s < 4; ++s) if (auto sc = sl.getScenes()[s]) sc->name = scenes[s].name;
            for (int t = 0; t < numTracks; ++t)
            {
                tracks[t]->getClipSlotList().ensureNumberOfSlots (8);
                auto slots = tracks[t]->getClipSlotList().getClipSlots();
                for (int s = 0; s < 4; ++s)
                {
                    const double from = scenes[s].fromBar * 4.0, to = from + 16.0;
                    struct N { int note; double at, len; int vel; };
                    std::vector<N> found;
                    for (auto c : tracks[t]->getClips())
                        if (auto mc = dynamic_cast<te::MidiClip*> (c))
                        {
                            const double clipStart = mc->getStartBeat().inBeats();
                            for (auto n : mc->getSequence().getNotes())
                            {
                                const double at = clipStart + n->getStartBeat().inBeats();
                                if (at >= from && at < to) found.push_back ({ n->getNoteNumber(), at - from, jmin (n->getLengthBeats().inBeats(), to - at), n->getVelocity() });
                            }
                        }
                    if (found.empty()) continue;
                    auto mc = te::insertMIDIClip (*slots[s], defs[t].name, { te::TimePosition(), EngineHelpers::beatToTime (edit, 16.0) });
                    if (mc == nullptr) continue;
                    for (auto& n : found) mc->getSequence().addNote (n.note, te::BeatPosition::fromBeats (n.at), te::BeatDuration::fromBeats (n.len), n.vel, 0, nullptr);
                    mc->setLoopRangeBeats ({ te::BeatPosition(), te::BeatPosition::fromBeats (16.0) });
                }
            }
        }

        // the first drop, ready to loop (looping is left off: the song plays through)
        edit.getTransport().setLoopRange ({ bars (32), bars (40) });
        edit.getTransport().looping = false;
    }
}
