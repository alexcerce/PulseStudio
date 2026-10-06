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

/** The demo song: a dark, stripped, hypnotic techno track of the kind made for long blends in a club, built of
    nothing but the app's own instruments and effects. 126 BPM, 208 bars (about six and a half minutes), on F.

    Intro (32 bars) - Groove (32) - Main (32) - Break (16) - Main II (64) - Outro (32).

    Nothing in it is loud for its own sake, and there are no fills, rolls or crashes: things come in and go out
    sixteen bars at a time, and what moves is a filter. A tight kick sits on a rumble (the kick itself, sent to a
    reverb that is cut down to its lows and ducked by the kick: the "Rumble" return); a short bass note answers on
    every off-beat; quiet sixteenth hats and a few rim and tom hits roll underneath; one repeating tone on F, in a
    three-three-two figure, is the whole tune, and its filter is opened and closed by automation over minutes; a
    high blip falls into a long delay now and then. */
namespace DemoSong
{
    constexpr double bpm = 126.0;
    constexpr int totalBars = 208;

    struct Section { const char* name; int from, to; };
    inline const std::vector<Section>& sections()
    {
        static const std::vector<Section> s { { "Intro", 0, 32 }, { "Groove", 32, 64 }, { "Main", 64, 96 }, { "Break", 96, 112 }, { "Main II", 112, 176 }, { "Outro", 176, 208 } };
        return s;
    }

    enum { kick, hats, perc, bass, tone, blip, atmos, noise, numTracks };

    inline void build (te::Edit& edit)
    {
        edit.tempoSequence.getTempo (0)->setBpm (bpm);
        auto bars = [&edit] (double b) { return EngineHelpers::beatToTime (edit, b * 4.0); };
        auto setP = [] (te::Plugin* p, const char* id, float v)
        {
            if (p != nullptr) if (auto prm = p->getAutomatableParameterByID (id)) prm->setParameter (v, sendNotification);
        };
        using Settings = std::initializer_list<std::pair<const char*, float>>;
        auto setAll = [&] (te::Plugin* p, Settings s) { for (auto [id, v] : s) setP (p, id, v); };
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
        const Def defs[numTracks] = { { "Kick", 0, -8.0f }, { "Hats", 3, -19.0f }, { "Perc", 1, -16.0f }, { "Bass", 4, 0.0f }, { "Tone", 2, -7.0f }, { "Blip", 5, -13.0f }, { "Atmos", 6, -3.0f }, { "Noise", 7, -18.0f } };
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
        using Fill = std::function<void (int bar, const AddNote&)>;
        auto sectionOf = [] (int bar) -> const Section& { for (auto& s : sections()) if (bar >= s.from && bar < s.to) return s; return sections().back(); };
        auto part = [&] (int track, int fromBar, int toBar, const Fill& fill, const String& name = {})
        {
            auto mc = tracks[track]->insertMIDIClip (name.isNotEmpty() ? name : String (sectionOf (fromBar).name), { bars (fromBar), bars (toBar) }, nullptr);
            if (mc == nullptr) return;
            auto& seq = mc->getSequence();
            for (int bar = fromBar; bar < toBar; ++bar)
                fill (bar, [&] (int note, double beatInBar, double len, int vel)
                {
                    seq.addNote (note, te::BeatPosition::fromBeats ((bar - fromBar) * 4.0 + beatInBar), te::BeatDuration::fromBeats (len), jlimit (1, 127, vel), 0, nullptr);
                });
        };
        /** The same thing from one bar to another, cut into one clip for each section it crosses. */
        auto parts = [&] (int track, int fromBar, int toBar, const Fill& fill)
        {
            for (auto& s : sections())
                if (const int a = jmax (fromBar, s.from), b = jmin (toBar, s.to); b > a) part (track, a, b, fill);
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
        using P = pulse::DrumPad;
        if (auto* rack = drumRack (kick))
            if (auto pad = DrumRack::source (*rack, 36)) { pad->set (P::pSynthDecay, pad->get (P::pSynthDecay) * 0.8f); pad->set (P::pSynthDrive, 0.3f); pad->set (P::pSynthSnap, pad->get (P::pSynthSnap) * 0.8f); }   // short and dry: the rumble is the tail
        if (auto* rack = drumRack (hats))
            for (int slot : { 42, 46 })
                if (auto pad = DrumRack::source (*rack, slot)) { pad->set (P::pSynthDecay, pad->get (P::pSynthDecay) * (slot == 42 ? 0.6f : 0.7f)); pad->set (P::pSynthTone, pad->get (P::pSynthTone) * 0.8f); }   // short and dull
        drumRack (perc);

        /** Puts a device on a track: an instrument first, an effect after what is there (in front of the fader). */
        auto deviceOn = [&] (te::AudioTrack& t, const char* type) -> te::Plugin*
        {
            auto p = edit.getPluginCache().createNewPlugin (type, {});
            if (p == nullptr) return nullptr;
            int index = 0;
            if (! EngineHelpers::isInstrument (p.get())) { index = t.pluginList.size(); if (auto vp = t.getVolumePlugin()) index = jmin (index, t.pluginList.indexOf (vp)); }
            t.pluginList.insertPlugin (p, index, nullptr);
            return p.get();
        };
        auto device = [&] (int track, const char* type) { return deviceOn (*tracks[track], type); };
        /** A compressor that ducks a track under the kick. */
        auto duck = [&] (te::AudioTrack& t, float threshold, float release)
        {
            if (auto c = deviceOn (t, pulse::ProCompressor::xmlTypeName))
            {
                setAll (c, { { "threshold", threshold }, { "ratio", 8.0f }, { "attack", 0.5f }, { "release", release }, { "knee", 4.0f } });
                c->setSidechainSourceID (tracks[kick]->itemID);
            }
        };

        // Bass: one short low note, darker than it is loud
        auto* bassSynth = device (bass, pulse::Analog::xmlTypeName);
        setAll (bassSynth, { { "wave1", 0 }, { "wave2", 3 }, { "oct2", -1 }, { "mix", 0.5f }, { "sub", 0.6f }, { "cutoff", 420 }, { "reso", 0.15f }, { "envamt", 0.3f }, { "fdrive", 0.4f }, { "fa", 1 }, { "fd", 110 },
                             { "fs", 0.1f }, { "fr", 80 }, { "a", 1 }, { "d", 150 }, { "s", 0.5f }, { "r", 70 }, { "mode", 1 }, { "keytrack", 0.2f }, { "drift", 0.05f }, { "level", 12 } });
        if (auto s = device (bass, pulse::Saturator::xmlTypeName)) setAll (s, { { "drive", 12.0f }, { "type", 1.0f }, { "mix", 0.5f } });
        duck (*tracks[bass], -22.0f, 120.0f);

        // Tone: the one repeating note. A narrow pulse through a filter that is never fully open, with a little ring to it
        auto* toneSynth = device (tone, pulse::Analog::xmlTypeName);
        setAll (toneSynth, { { "wave1", 1 }, { "pw", 0.38f }, { "wave2", 0 }, { "semi2", 0 }, { "fine2", 6 }, { "mix", 0.25f }, { "sub", 0.0f }, { "cutoff", 200 }, { "reso", 0.42f }, { "envamt", 0.3f },
                             { "fdrive", 0.3f }, { "fa", 1 }, { "fd", 120 }, { "fs", 0.1f }, { "fr", 90 }, { "a", 1 }, { "d", 160 }, { "s", 0.15f }, { "r", 110 }, { "mode", 1 }, { "glide", 0 },
                             { "keytrack", 0.3f }, { "drift", 0.2f }, { "level", 10 } });
        if (auto d = device (tone, pulse::Delay::xmlTypeName)) setAll (d, { { "sync", 1.0f }, { "note", 5.0f }, { "feedback", 0.42f }, { "pingpong", 1.0f }, { "lowcut", 500.0f }, { "highcut", 3800.0f }, { "mix", 0.2f } });

        // Blip: a short high sine that is mostly heard as its echoes
        auto* blipSynth = device (blip, pulse::Analog::xmlTypeName);
        setAll (blipSynth, { { "wave1", 3 }, { "wave2", 3 }, { "mix", 0.0f }, { "sub", 0.0f }, { "cutoff", 7000 }, { "reso", 0.0f }, { "envamt", 0.0f }, { "a", 1 }, { "d", 70 }, { "s", 0.0f }, { "r", 90 },
                             { "mode", 1 }, { "drift", 0.3f }, { "level", 6 } });

        // Atmos: a slow dark bed that breathes under the kick
        auto* atmosSynth = device (atmos, pulse::WavetableSynth::xmlTypeName);
        if (auto w = dynamic_cast<pulse::PulsePlugin*> (atmosSynth)) FactoryPresets::apply (*w, "Dark Cinematic Drone");
        setAll (atmosSynth, { { "a", 2500.0f }, { "r", 5000.0f }, { "cutoff", 420.0f }, { "level", 6.0f } });
        duck (*tracks[atmos], -28.0f, 240.0f);

        // Noise: filtered hiss that swells across a change, and nothing more
        auto* noiseSynth = device (noise, pulse::Analog::xmlTypeName);
        setAll (noiseSynth, { { "noise", 1.0f }, { "mix", 0.5f }, { "wave1", 3 }, { "wave2", 3 }, { "cutoff", 200 }, { "reso", 0.2f }, { "envamt", 0.0f }, { "keytrack", 0.0f }, { "a", 1500 }, { "s", 1.0f },
                              { "r", 1500 }, { "level", 2 } });

        //==============================================================================
        // The parts. Sixteen bars is the unit: something changes at each, and nothing announces it.

        // Kick: every beat, from the first bar to the last but eight; out for the break
        const Fill kickFill = [] (int, const AddNote& add) { for (int b = 0; b < 4; ++b) add (36, b, 0.25, 120); };
        parts (kick, 0, 96, kickFill);
        parts (kick, 112, 200, kickFill);

        // Hats: sixteenths that are felt more than heard, the off-beat one open
        parts (hats, 16, 96, [] (int bar, const AddNote& add)
        {
            static const int vel[] = { 40, 26, 0, 30 };
            for (int s = 0; s < 16; ++s) if (vel[s % 4] > 0) add (42, s * 0.25, 0.06, vel[s % 4] + ((bar * 5 + s * 3) % 7) - 3);
            if (bar >= 32) for (int b = 0; b < 4; ++b) add (46, b + 0.5, 0.18, 74 + ((bar + b) % 3) * 3);
            else           for (int b = 0; b < 4; ++b) add (42, b + 0.5, 0.06, 62);
        });
        parts (hats, 104, 192, [] (int bar, const AddNote& add)
        {
            static const int vel[] = { 42, 28, 0, 32 };
            if (bar < 112) { for (int b = 0; b < 4; ++b) add (42, b + 0.5, 0.06, 40 + (bar - 104) * 4); return; }   // creeping back in under the break
            for (int s = 0; s < 16; ++s) if (vel[s % 4] > 0) add (42, s * 0.25, 0.06, vel[s % 4] + ((bar * 5 + s * 3) % 7) - 3);
            if (bar < 184) for (int b = 0; b < 4; ++b) add (46, b + 0.5, 0.18, 76 + ((bar + b) % 3) * 3);
            if (bar >= 144 && bar < 176) for (int b = 0; b < 4; ++b) add (49, b + 0.5, 0.25, 30);                   // a thin wash on top for the last stretch
        });

        // Perc: a rim that leans against the beat, two toms, and a clap that is hardly there
        const Fill percFill = [] (int bar, const AddNote& add)
        {
            for (int s : { 3, 6, 11, 14 }) add (37, s * 0.25, 0.08, 66 + ((bar + s) % 4) * 6);
            if (bar >= 48) { add (45, 2.5, 0.1, 58); if (bar % 2 == 1) add (41, 3.75, 0.1, 64); }
            if ((bar >= 64 && bar < 96) || (bar >= 112 && bar < 176)) { add (39, 1.0, 0.15, 58); add (39, 3.0, 0.15, 62); }
        };
        parts (perc, 32, 96, percFill);
        parts (perc, 112, 184, percFill);

        // Bass: the off-beat, on F; a ghost note before the bar turns
        const Fill bassFill = [] (int bar, const AddNote& add)
        {
            for (int b = 0; b < 4; ++b) add (29, b + 0.5, 0.22, 108);
            if (bar % 2 == 1) add (29, 3.75, 0.18, 76);
            if (bar % 8 == 7) add (32, 2.75, 0.18, 84);   // Ab, once in eight bars
        };
        parts (bass, 16, 96, bassFill);
        parts (bass, 112, 184, bassFill);

        // Tone: F, three-three-two, for nearly the whole track; a neighbour note now and then is all that changes
        parts (tone, 32, 192, [] (int bar, const AddNote& add)
        {
            static const int steps[] = { 0, 3, 6, 8, 11, 14 };
            for (int i = 0; i < 6; ++i)
            {
                int note = 53;                                             // F3
                if (bar % 4 == 3 && steps[i] == 14) note = 56;             // Ab
                if (bar % 8 == 7 && steps[i] == 11) note = 54;             // Gb: the flat second, in passing
                if (bar % 16 == 15 && steps[i] == 6) note = 60;            // C
                add (note, steps[i] * 0.25, 0.17, i % 2 == 0 ? 100 : 76);
            }
        });
        // its filter is the arrangement: barely open in the groove, half open in the main part, shut and brought
        // back over the break, then rising and falling over each sixteen bars, never the same height twice
        ramp (toneSynth, "cutoff", 32, 170, 48, 330);
        ramp (toneSynth, "cutoff", 48.01, 330, 64, 620);
        ramp (toneSynth, "cutoff", 64.01, 620, 80, 1250);
        ramp (toneSynth, "cutoff", 80.01, 1250, 92, 1600);
        ramp (toneSynth, "cutoff", 92.01, 1600, 100, 280);
        ramp (toneSynth, "cutoff", 100.01, 280, 112, 1500);
        const float peaks[] = { 1700, 2100, 1500, 2400 };
        for (int k = 0; k < 4; ++k)
        {
            const int b = 112 + k * 16;
            ramp (toneSynth, "cutoff", b + 0.01, 800, b + 12, peaks[k]);
            ramp (toneSynth, "cutoff", b + 12.01, peaks[k], b + 16, 850);
        }
        ramp (toneSynth, "cutoff", 176.01, 850, 192, 150);
        ramp (toneSynth, "reso", 96, 0.42f, 104, 0.6f);
        ramp (toneSynth, "reso", 104.01, 0.6f, 112, 0.42f);

        // Blip: once every two bars, high up; more of it in the break
        parts (blip, 64, 176, [] (int bar, const AddNote& add)
        {
            const bool brk = bar >= 96 && bar < 112;
            if (bar % 2 == 1) add (77, 1.75, 0.1, 92);
            if (bar % 4 == 2) add (75, 3.25, 0.1, 80);
            if (brk && bar % 2 == 0) add (80, 0.75, 0.1, 86);
        });

        // Atmos: F and its fifth, sixteen bars at a time; a minor third on top through the break
        parts (atmos, 0, 208, [] (int bar, const AddNote& add)
        {
            if (bar % 16 != 0) return;
            const bool brk = bar >= 96 && bar < 112;
            for (int n : { 29, 36, 41 }) add (n, 0.0, 63.0, brk ? 100 : 72);
            if (brk) { add (53, 0.0, 63.0, 70); add (56, 0.0, 63.0, 62); }
        });

        // Noise: a swell across each change
        for (auto [from, to] : std::initializer_list<std::pair<int, int>> { { 60, 66 }, { 104, 114 }, { 172, 180 } })
            part (noise, from, to, [from = from, to = to] (int bar, const AddNote& add) { if (bar == from) add (60, 0.0, (to - from) * 4.0 - 0.5, 90); }, "Swell");
        ramp (noiseSynth, "cutoff", 60, 250, 64, 4200);   ramp (noiseSynth, "cutoff", 64.01, 4200, 66, 300);
        ramp (noiseSynth, "cutoff", 104, 250, 112, 6500); ramp (noiseSynth, "cutoff", 112.01, 6500, 114, 300);
        ramp (noiseSynth, "cutoff", 172, 250, 176, 4200); ramp (noiseSynth, "cutoff", 176.01, 4200, 180, 250);

        //==============================================================================
        // Returns. The first is the rumble: the kick through a reverb, cut down to its lows, pressed, and ducked by the kick itself
        if (auto rumble = Routing::createReturnTrack (edit, "Rumble", pulse::Reverb::xmlTypeName))
        {
            rumble->setColour (Theme::trackColour (0));
            setAll (rumble->pluginList.findFirstPluginOfType<pulse::Reverb>(), { { "size", 0.55f }, { "decay", 1.3f }, { "damping", 0.8f }, { "lowcut", 20.0f }, { "predelay", 0.0f }, { "diffusion", 0.9f }, { "mix", 1.0f } });
            setAll (deviceOn (*rumble, pulse::AutoFilter::xmlTypeName), { { "type", 4.0f }, { "cutoff", 105.0f }, { "reso", 0.1f }, { "lfoamount", 0.0f }, { "envamount", 0.0f }, { "mix", 1.0f } });
            setAll (deviceOn (*rumble, pulse::Saturator::xmlTypeName), { { "drive", 8.0f }, { "type", 1.0f }, { "mix", 0.6f } });
            duck (*rumble, -30.0f, 140.0f);
            if (auto vp = rumble->getVolumePlugin()) vp->setVolumeDb (-4.0f);
        }
        if (auto rev = Routing::createReturnTrack (edit, "Reverb", pulse::Reverb::xmlTypeName))
        {
            rev->setColour (Theme::trackColour (5));
            setAll (rev->pluginList.findFirstPluginOfType<pulse::Reverb>(), { { "size", 0.8f }, { "decay", 3.4f }, { "damping", 0.6f }, { "lowcut", 320.0f }, { "predelay", 18.0f }, { "mix", 1.0f } });
        }
        if (auto del = Routing::createReturnTrack (edit, "Delay", pulse::Delay::xmlTypeName))
        {
            del->setColour (Theme::trackColour (3));
            setAll (del->pluginList.findFirstPluginOfType<pulse::Delay>(), { { "sync", 1.0f }, { "note", 5.0f }, { "feedback", 0.6f }, { "pingpong", 1.0f }, { "lowcut", 500.0f }, { "highcut", 4200.0f }, { "mix", 1.0f } });
        }
        enum { toRumble, toReverb, toDelay };
        Routing::setSendDb (*tracks[kick], toRumble, -5.0f);
        Routing::setSendDb (*tracks[hats], toReverb, -22.0f);
        Routing::setSendDb (*tracks[perc], toReverb, -13.0f);
        Routing::setSendDb (*tracks[perc], toDelay, -20.0f);
        Routing::setSendDb (*tracks[tone], toReverb, -12.0f);
        Routing::setSendDb (*tracks[blip], toDelay, -3.0f);
        Routing::setSendDb (*tracks[blip], toReverb, -8.0f);
        Routing::setSendDb (*tracks[atmos], toReverb, -8.0f);
        Routing::setSendDb (*tracks[noise], toReverb, -6.0f);

        const int drumGroup = Vca::add (edit, "Drums");
        for (int t : { (int) kick, (int) hats, (int) perc }) Vca::join (edit, drumGroup, *tracks[t]);
        const int musicGroup = Vca::add (edit, "Music");
        for (int t : { (int) bass, (int) tone, (int) blip, (int) atmos }) Vca::join (edit, musicGroup, *tracks[t]);

        if (auto* lim = Mastering::addChain (edit)) lim->set (pulse::Limiter::pGain, 6.0f);

        //==============================================================================
        // The map of the song: sections, the one chord it sits on, its key, and a note about it
        for (auto& s : sections()) Sections::add (edit, s.name, s.from * 4.0, (s.to - s.from) * 4.0);
        for (int bar = 0; bar < totalBars; bar += 16) Chords::add (edit, bar * 4.0, 64.0, 5, Chords::minor);   // F minor, throughout
        SongKey::set (edit, 8);   // F minor (the notes of Ab major)
        Notes::setProject (edit, "Demo: dark, stripped techno for long blends, made only of Pulse's own instruments and effects. 126 BPM, on F.\n\n"
                                 "Things to look at:\n"
                                 "- Rumble (a return): the kick is sent to a reverb that is cut down to its lows and ducked by the kick. Mute it to hear what it adds.\n"
                                 "- Tone: its automation lane (A on the track) is the arrangement: one filter, opened and closed over minutes.\n"
                                 "- Bass and Atmos: a compressor on each is keyed from the Kick.\n"
                                 "- Blip: nearly all of what you hear of it is the Delay return.\n"
                                 "- Mixer: two VCA faders, Drums and Music; the mastering chain on the master.\n"
                                 "- Session view (Tab): the same parts as clips in four scenes.");
        Notes::set (*tracks[tone], "Six notes a bar, in threes and a two, all on F. The filter does the rest: see the automation lane.");

        //==============================================================================
        // Session view: four scenes, each a four-bar loop taken from a place in the arrangement
        {
            struct Scene { const char* name; int fromBar; };
            const Scene scenes[] = { { "Intro", 16 }, { "Groove", 48 }, { "Main", 80 }, { "Break", 104 } };
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

        // sixteen bars of the main part, ready to loop (looping is left off: the song plays through)
        edit.getTransport().setLoopRange ({ bars (64), bars (80) });
        edit.getTransport().looping = false;
    }
}
