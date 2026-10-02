#pragma once

#include "Common.h"
#include "DrumRack.h"
#include "Routing.h"
#include "FactoryPresets.h"
#include "PluginMenu.h"

/** Factory project templates (House, Hip-Hop, Ambient, Empty with returns), built in code the
    first time Pulse runs and kept in "<Documents>/<profile>/Factory Templates". */
namespace FactoryContent
{
    constexpr int version = 1;

    inline File folder (te::Engine& e)
    {
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (e.getPropertyStorage().getApplicationName()).getChildFile ("Factory Templates");
        d.createDirectory();
        return d;
    }

    inline Array<File> templates (te::Engine& e)
    {
        auto files = folder (e).findChildFiles (File::findFiles, false, "*.tracktionedit");
        files.sort();
        return files;
    }

    //==============================================================================
    struct Builder
    {
        te::Edit& edit;
        int colour = 0;

        double bars (double b) const { return b * 4.0; }
        te::TimePosition at (double beat) const { return EngineHelpers::beatToTime (edit, beat); }

        te::AudioTrack* track (const String& name, te::Plugin::Ptr instrument, float volumeDb)
        {
            auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, EngineHelpers::getUserTracks (edit).getLast()), nullptr);
            if (t == nullptr) return nullptr;
            t->setName (name);
            t->setColour (Theme::trackColour (colour++));
            if (instrument != nullptr) t->pluginList.insertPlugin (instrument, 0, nullptr);
            if (auto vp = t->getVolumePlugin()) vp->setVolumeDb (volumeDb);
            return t.get();
        }

        te::Plugin::Ptr device (const char* type, const String& preset)
        {
            auto p = edit.getPluginCache().createNewPlugin (type, {});
            if (p != nullptr && preset.isNotEmpty()) FactoryPresets::apply (*p, preset);
            return p;
        }

        te::MidiClip* clip (te::AudioTrack& t, const String& name, double lengthBeats, double loopBeats)
        {
            auto mc = t.insertMIDIClip (name, { at (0.0), at (lengthBeats) }, nullptr);
            if (mc != nullptr && loopBeats > 0) mc->setLoopRangeBeats ({ te::BeatPosition(), te::BeatPosition::fromBeats (loopBeats) });
            return mc.get();
        }

        static void note (te::MidiClip& c, int pitch, double beat, double len, int vel = 100)
        {
            c.getSequence().addNote (pitch, te::BeatPosition::fromBeats (beat), te::BeatDuration::fromBeats (len), vel, 0, nullptr);
        }

        te::AudioTrack* drumRack (const String& name, int kitStyle, float volumeDb)
        {
            auto rp = DrumRack::create (edit, false);
            if (auto rack = DrumRack::rackOf (rp.get())) DrumRack::fillSynthKit (*rack, kitStyle);
            return track (name, rp, volumeDb);
        }

        te::AudioTrack* returnTrack (const String& name, const char* type, const String& preset)
        {
            auto r = Routing::createReturnTrack (edit, name, type);
            if (r != nullptr)
            {
                r->setColour (Theme::trackColour (colour++));
                for (auto p : r->pluginList)
                    if (p->getPluginType() == String (type)) { FactoryPresets::apply (*p, preset); p->getAutomatableParameterByID ("mix")->setParameter (1.0f, sendNotificationSync); }
            }
            return r;
        }

        void chords (te::MidiClip& c, const std::vector<std::vector<int>>& prog, double beatsEach, std::vector<double> hits, double len, int vel)
        {
            for (size_t i = 0; i < prog.size(); ++i)
                for (double h : hits)
                    for (int n : prog[i]) note (c, n, i * beatsEach + h, len, vel);
        }
    };

    inline void buildHouse (te::Edit& e)
    {
        e.tempoSequence.getTempo (0)->setBpm (124.0);
        Builder b { e };
        if (auto d = b.drumRack ("Drums", 2, -8.0f))
            if (auto c = b.clip (*d, "House beat", 32, 16))
                for (int bar = 0; bar < 4; ++bar)
                    for (int k = 0; k < 4; ++k)
                    {
                        const double t = bar * 4.0 + k;
                        Builder::note (*c, 36, t, 0.25, 120);
                        if (k % 2 == 1) Builder::note (*c, 39, t, 0.25, 100);
                        Builder::note (*c, 46, t + 0.5, 0.25, 85);
                        Builder::note (*c, 42, t + 0.25, 0.1, 60); Builder::note (*c, 42, t + 0.75, 0.1, 65);
                    }
        if (auto t = b.track ("Bass", b.device (pulse::WavetableSynth::xmlTypeName, "Acid Bass"), -9.0f))
            if (auto c = b.clip (*t, "Bassline", 32, 16))
            {
                const int roots[] = { 33, 33, 29, 31 };
                for (int bar = 0; bar < 4; ++bar)
                    for (double off : { 0.5, 1.5, 2.5, 3.5, 3.75 })
                        Builder::note (*c, roots[bar] + (off > 3.6 ? 12 : 0), bar * 4.0 + off, 0.2, off > 3.6 ? 90 : 110);
            }
        if (auto t = b.track ("Chords", b.device (pulse::WavetableSynth::xmlTypeName, "Pluck"), -12.0f))
            if (auto c = b.clip (*t, "Stabs", 32, 16))
                b.chords (*c, { { 57, 60, 64 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } }, 4.0, { 0.75, 2.25, 3.5 }, 0.25, 90);
        if (auto t = b.track ("Pad", b.device (pulse::WavetableSynth::xmlTypeName, "Warm Pad"), -14.0f))
            if (auto c = b.clip (*t, "Pad", 32, 16))
                b.chords (*c, { { 57, 64, 69, 72 }, { 57, 64, 69, 72 }, { 53, 60, 65, 69 }, { 55, 62, 67, 71 } }, 4.0, { 0.0 }, 3.9, 70);
        auto rev = b.returnTrack ("Reverb", pulse::Reverb::xmlTypeName, "Hall");
        auto del = b.returnTrack ("Delay", pulse::Delay::xmlTypeName, "Dotted 8th");
        ignoreUnused (rev, del);
        auto tracks = EngineHelpers::getUserTracks (e);
        if (tracks.size() >= 4) { Routing::setSendDb (*tracks[2], 0, -12.0f); Routing::setSendDb (*tracks[2], 1, -14.0f); Routing::setSendDb (*tracks[3], 0, -6.0f); }
    }

    inline void buildHipHop (te::Edit& e)
    {
        e.tempoSequence.getTempo (0)->setBpm (88.0);
        Builder b { e };
        if (auto d = b.drumRack ("Drums", 1, -8.0f))
            if (auto c = b.clip (*d, "Boom bap", 32, 16))
                for (int bar = 0; bar < 4; ++bar)
                {
                    const double t = bar * 4.0;
                    for (double k : { 0.0, 1.75, 2.5 }) Builder::note (*c, 36, t + k, 0.25, 120);
                    for (double s : { 1.0, 3.0 }) Builder::note (*c, 38, t + s, 0.25, 110);
                    for (int h = 0; h < 8; ++h) Builder::note (*c, 42, t + h * 0.5 + (h % 2 ? 0.08 : 0.0), 0.1, h % 2 ? 60 : 85);
                }
        if (auto t = b.track ("Keys", b.device (pulse::FMSynth::xmlTypeName, "E-Piano"), -10.0f))
            if (auto c = b.clip (*t, "Keys", 32, 16))
                b.chords (*c, { { 62, 65, 69, 72 }, { 60, 64, 67, 71 }, { 58, 62, 65, 69 }, { 57, 60, 64, 67 } }, 4.0, { 0.0, 2.5 }, 1.4, 80);
        if (auto t = b.track ("808", b.device (pulse::WavetableSynth::xmlTypeName, "Sub Bass"), -6.0f))
            if (auto c = b.clip (*t, "Sub", 32, 16))
            {
                const int roots[] = { 38, 36, 34, 33 };
                for (int bar = 0; bar < 4; ++bar) { Builder::note (*c, roots[bar], bar * 4.0, 1.5, 110); Builder::note (*c, roots[bar], bar * 4.0 + 2.5, 1.0, 100); }
            }
        b.returnTrack ("Reverb", pulse::Reverb::xmlTypeName, "Small Room");
        auto tracks = EngineHelpers::getUserTracks (e);
        if (tracks.size() >= 2) Routing::setSendDb (*tracks[1], 0, -10.0f);
    }

    inline void buildAmbient (te::Edit& e)
    {
        e.tempoSequence.getTempo (0)->setBpm (70.0);
        Builder b { e };
        if (auto t = b.track ("Pad", b.device (pulse::WavetableSynth::xmlTypeName, "Warm Pad"), -12.0f))
            if (auto c = b.clip (*t, "Drift", 32, 32))
                b.chords (*c, { { 52, 59, 64, 71 }, { 48, 55, 64, 67 }, { 50, 57, 62, 69 }, { 47, 54, 62, 66 } }, 8.0, { 0.0 }, 7.9, 65);
        if (auto t = b.track ("Bells", b.device (pulse::FMSynth::xmlTypeName, "Bell"), -16.0f))
            if (auto c = b.clip (*t, "Bells", 32, 32))
            {
                const int mel[] = { 76, 79, 83, 81, 79, 74, 76, 71 };
                for (int i = 0; i < 8; ++i) Builder::note (*c, mel[i], i * 4.0 + (i % 3) * 0.5, 2.0, 70);
            }
        if (auto t = b.track ("Choir", b.device (pulse::WavetableSynth::xmlTypeName, "Vowel Choir"), -18.0f))
            if (auto c = b.clip (*t, "Choir", 32, 32))
                b.chords (*c, { { 64, 71 }, { 64, 67 }, { 62, 69 }, { 62, 66 } }, 8.0, { 0.0 }, 7.9, 60);
        b.returnTrack ("Reverb", pulse::Reverb::xmlTypeName, "Ambient Wash");
        b.returnTrack ("Delay", pulse::Delay::xmlTypeName, "Ping-Pong 1/4");
        auto tracks = EngineHelpers::getUserTracks (e);
        for (int i = 0; i < jmin (3, tracks.size()); ++i) Routing::setSendDb (*tracks[i], 0, -4.0f);
        if (tracks.size() >= 2) Routing::setSendDb (*tracks[1], 1, -8.0f);
    }

    inline void buildEmpty (te::Edit& e)
    {
        e.tempoSequence.getTempo (0)->setBpm (120.0);
        Builder b { e };
        b.track ("Instrument", b.device (pulse::WavetableSynth::xmlTypeName, {}), -8.0f);
        b.track ("Drums", nullptr, -8.0f);
        if (auto t = EngineHelpers::getUserTracks (e).getLast()) t->pluginList.insertPlugin (DrumRack::create (e, true), 0, nullptr);
        b.track ("Audio", nullptr, 0.0f);
        b.returnTrack ("Reverb", pulse::Reverb::xmlTypeName, "Hall");
        b.returnTrack ("Delay", pulse::Delay::xmlTypeName, "Dotted 8th");
    }

    /** Builds the factory templates if they are missing or out of date. Returns how many were written. */
    inline int ensureTemplates (te::Engine& engine)
    {
        auto dir = folder (engine);
        auto marker = dir.getChildFile ("version.txt");
        if (marker.loadFileAsString().getIntValue() >= version && templates (engine).size() >= 4) return 0;
        const std::pair<const char*, void (*) (te::Edit&)> list[] = { { "House", buildHouse }, { "Hip-Hop", buildHipHop }, { "Ambient", buildAmbient },
                                                                      { "Empty with returns", buildEmpty } };
        int written = 0;
        for (auto [name, build] : list)
        {
            auto f = dir.getChildFile (String (name) + ".tracktionedit");
            f.deleteFile();
            auto ed = te::createEmptyEdit (engine, f);
            if (ed == nullptr) continue;
            for (auto t : EngineHelpers::getUserTracks (*ed)) ed->deleteTrack (t);   // start from nothing
            build (*ed);
            te::EditFileOperations (*ed).save (true, true, false);
            if (f.existsAsFile()) ++written;
        }
        marker.replaceWithText (String (version));
        return written;
    }
}
