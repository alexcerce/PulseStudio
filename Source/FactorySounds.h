#pragma once

#include "Common.h"
#include "DrumRack.h"
#include "PulseSynths.h"
#include "FactoryPresets.h"

/** Built-in sounds, rendered from Pulse's own synths the first time Pulse runs (nothing is
    downloaded) into "<Documents>/<profile>/Factory Sounds":
      Drums/<style>/<voice>.wav    one-shots for every kit style (kick, snare, clap, hats, toms, ...)
      Loops/<name> <bpm> BPM.wav   two-bar drum loops
      Instruments/<name>/<name> <note>.wav   multisamples (keys, bass, pad, pluck, bell, organ)
    Simpler loads a matching instrument when it starts empty, and Drum Racks can load a
    sampled kit. */
namespace FactorySounds
{
    constexpr int version = 1;
    constexpr double sampleRate = 48000.0;

    inline File folder (te::Engine& e)
    {
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (e.getPropertyStorage().getApplicationName()).getChildFile ("Factory Sounds");
        d.createDirectory();
        return d;
    }

    inline bool isReady (te::Engine& e) { return folder (e).getChildFile ("version.txt").loadFileAsString().getIntValue() == version; }

    //==============================================================================
    inline bool writeWav (const File& f, const AudioBuffer<float>& b)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        std::unique_ptr<OutputStream> os (f.createOutputStream());
        if (os == nullptr) return false;
        WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), sampleRate, (unsigned int) b.getNumChannels(), 24, {}, 0));
        if (w == nullptr) return false;
        os.release();
        return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    /** Trims trailing silence (keeps a short fade). */
    inline void trim (AudioBuffer<float>& b)
    {
        int last = b.getNumSamples() - 1;
        while (last > 0 && b.getMagnitude (last, 1) < 0.0005f) --last;
        const int len = jmin (b.getNumSamples(), last + 480);
        b.setSize (b.getNumChannels(), jmax (1, len), true);
        const int fade = jmin (480, b.getNumSamples());
        b.applyGainRamp (b.getNumSamples() - fade, fade, 1.0f, 0.0f);
    }

    //==============================================================================
    // drums
    struct Voice { int slot, type; const char* name; };
    inline const std::vector<Voice>& voices()
    {
        using D = pulse::DrumVoice;
        static const std::vector<Voice> v { { 36, D::kick, "Kick" }, { 37, D::rim, "Rim" }, { 38, D::snare, "Snare" }, { 39, D::clap, "Clap" },
                                            { 41, D::tom, "Tom Low" }, { 42, D::closedHat, "Hat Closed" }, { 45, D::tom, "Tom Mid" },
                                            { 46, D::openHat, "Hat Open" }, { 48, D::tom, "Tom High" }, { 49, D::cymbal, "Crash" }, { 56, D::cowbell, "Cowbell" } };
        return v;
    }

    inline AudioBuffer<float> renderHit (int style, int slot, int type, uint32 seed = 1234u, float velocity = 1.0f)
    {
        const auto p = DrumRack::kitVoiceParams (style, slot, type);
        pulse::DrumVoice v;
        v.start (sampleRate, type, p[0], p[1], p[2], p[3], p[4], seed);
        AudioBuffer<float> b (2, (int) (sampleRate * 4.0));
        b.clear();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = v.next() * 0.9f * velocity;
            b.setSample (0, i, x);
            b.setSample (1, i, x);
        }
        trim (b);
        return b;
    }

    inline File drumFile (te::Engine& e, int style, const char* voiceName)
    {
        return folder (e).getChildFile ("Drums").getChildFile (DrumRack::kitStyles()[style]).getChildFile (String (voiceName) + ".wav");
    }

    struct Loop { const char* name; int bpm; int style; std::vector<std::pair<int, const char*>> lanes; };   // slot -> 32 steps (2 bars of 16ths)
    inline const std::vector<Loop>& loops()
    {
        static const std::vector<Loop> l {
            { "House",     124, 2, { { 36, "x...x...x...x...x...x...x...x..." }, { 39, "....x.......x.......x.......x..." },
                                     { 42, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x." }, { 46, "..x...x...x...x...x...x...x...x." } } },
            { "Hip Hop",    90, 3, { { 36, "x.....x...x.....x.....x..x......" }, { 38, "....x.......x.......x.......x..." },
                                     { 42, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.xx" } } },
            { "Breakbeat", 130, 0, { { 36, "x.........x.....x.x.......x....." }, { 38, "....x..x.x..x.......x..x.x..x..." },
                                     { 42, "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x." }, { 49, "x..............................." } } },
            { "Techno",    130, 4, { { 36, "x...x...x...x...x...x...x...x..." }, { 37, "..x..x....x..x....x..x....x..x.." },
                                     { 42, "..x...x...x...x...x...x...x...x." }, { 39, "....x.......x.......x.......x..." } } },
            { "Trap",      140, 1, { { 36, "x......x..x.....x.......x.x....." }, { 39, "........x...............x......." },
                                     { 42, "x.x.x.xxx.x.x.x.x.x.xxx.x.x.xxxx" }, { 46, "..............x..............x.." } } },
            { "Electro",   120, 4, { { 36, "x.....x...x.....x.....x...x....." }, { 38, "....x.......x.......x.......x..." },
                                     { 42, "x.xxx.xxx.xxx.xxx.xxx.xxx.xxx.xx" }, { 56, "......x.......x.......x.......x." } } },
        };
        return l;
    }

    inline AudioBuffer<float> renderLoop (const Loop& loop)
    {
        const double stepSec = 60.0 / loop.bpm / 4.0;
        const int len = (int) std::round (stepSec * 32 * sampleRate);
        AudioBuffer<float> out (2, len);
        out.clear();
        for (auto& [slot, pattern] : loop.lanes)
        {
            int type = -1;
            for (auto& v : voices()) if (v.slot == slot) type = v.type;
            if (type < 0) continue;
            for (int s = 0; s < 32; ++s)
                if (pattern[s] == 'x')
                {
                    const float vel = (s % 4 == 0) ? 1.0f : 0.78f;
                    auto hit = renderHit (loop.style, slot, type, 1000u + (uint32) s * 77u, vel * (slot == 42 || slot == 46 ? 0.6f : 1.0f));
                    const int at = (int) std::round (s * stepSec * sampleRate);
                    const int n = jmin (hit.getNumSamples(), len - at);
                    for (int ch = 0; ch < 2; ++ch) out.addFrom (ch, at, hit, ch, 0, n);
                }
        }
        const float peak = out.getMagnitude (0, len);
        if (peak > 0.0f) out.applyGain (0.89f / peak);
        return out;
    }

    //==============================================================================
    // instruments
    struct Instrument { const char* name; const char* type; const char* preset; int lo, hi, step; double hold; };
    inline const std::vector<Instrument>& instruments()
    {
        static const std::vector<Instrument> i {
            { "Keys",  pulse::FMSynth::xmlTypeName,        "E-Piano",    36, 84, 6, 1.8 },
            { "Bass",  pulse::WavetableSynth::xmlTypeName, "Reese Bass", 24, 60, 6, 1.2 },
            { "Pad",   pulse::WavetableSynth::xmlTypeName, "Warm Pad",   36, 84, 6, 2.5 },
            { "Pluck", pulse::WavetableSynth::xmlTypeName, "Pluck",      36, 84, 6, 0.8 },
            { "Bell",  pulse::FMSynth::xmlTypeName,        "Bell",       48, 96, 6, 1.0 },
            { "Organ", pulse::FMSynth::xmlTypeName,        "Organ",      36, 84, 6, 1.5 },
        };
        return i;
    }

    inline File instrumentFolder (te::Engine& e, const String& name) { return folder (e).getChildFile ("Instruments").getChildFile (name); }

    inline Array<File> instrumentFiles (te::Engine& e, const String& name)
    {
        auto files = instrumentFolder (e, name).findChildFiles (File::findFiles, false, "*.wav");
        files.sort();
        return files;
    }

    /** Plays one note through an instrument (not in any playing edit) and returns the audio. */
    inline AudioBuffer<float> renderNote (te::Plugin& p, int note, double hold)
    {
        const int block = 480;
        const double tail = 2.0;
        p.initialise ({ te::TimePosition(), sampleRate, block });
        const int blocks = (int) std::ceil ((hold + tail) * sampleRate / block);
        AudioBuffer<float> out (2, blocks * block), audio (2, block);
        const auto src = te::createUniqueMPESourceID();
        const int offBlock = (int) (hold * sampleRate / block);
        for (int k = 0; k < blocks; ++k)
        {
            audio.clear();
            te::MidiMessageArray midi;
            if (k == 0) midi.addMidiMessage (MidiMessage::noteOn (1, note, (uint8) 100), 0.0, src);
            if (k == offBlock) midi.addMidiMessage (MidiMessage::noteOff (1, note), 0.0, src);
            te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sampleRate), te::TimePosition::fromSeconds ((k + 1) * block / sampleRate) },
                                        true, false, true, false);
            p.applyToBuffer (fc);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, k * block, audio, ch, 0, block);
        }
        p.deinitialise();
        trim (out);
        return out;
    }

    //==============================================================================
    /** Renders everything (a few seconds). Instruments are created in a scratch edit on the
        calling (message) thread; the audio rendering runs here too, so call it once at a quiet
        moment (first run) or from a test. Returns the number of files written. */
    inline int renderAll (te::Engine& engine, std::function<void (const String&)> status = {})
    {
        int written = 0;
        auto root = folder (engine);
        root.getChildFile ("version.txt").deleteFile();

        if (status) status ("Drum kits");
        for (int style = 0; style < DrumRack::kitStyles().size(); ++style)
            for (auto& v : voices())
                if (writeWav (drumFile (engine, style, v.name), renderHit (style, v.slot, v.type))) ++written;

        if (status) status ("Drum loops");
        for (auto& l : loops())
            if (writeWav (root.getChildFile ("Loops").getChildFile (String (l.name) + " " + String (l.bpm) + " BPM.wav"), renderLoop (l))) ++written;

        auto scratch = te::createEmptyEdit (engine, File::getSpecialLocation (File::tempDirectory).getChildFile ("pulse-factory-sounds.tracktionedit"));
        for (auto& inst : instruments())
        {
            if (status) status (String ("Instrument: ") + inst.name);
            auto p = scratch->getPluginCache().createNewPlugin (inst.type, {});
            if (p == nullptr) continue;
            FactoryPresets::apply (*p, inst.preset);
            for (int note = inst.lo; note <= inst.hi; note += inst.step)
            {
                auto audio = renderNote (*p, note, inst.hold);
                const float peak = audio.getMagnitude (0, audio.getNumSamples());
                if (peak > 0.0f) audio.applyGain (jmin (4.0f, 0.7f / peak));
                const auto f = instrumentFolder (engine, inst.name).getChildFile (String (inst.name) + " " + MidiMessage::getMidiNoteName (note, true, true, 3) + ".wav");
                if (writeWav (f, audio)) ++written;
            }
        }
        scratch = nullptr;
        root.getChildFile ("version.txt").replaceWithText (String (version));
        return written;
    }

    //==============================================================================
    // using the sounds
    /** Which instrument suits a Simpler preset (keys unless the name says otherwise). */
    inline String instrumentFor (const String& presetName)
    {
        const auto n = presetName.toLowerCase();
        if (n.contains ("bass") || n.contains ("octave down")) return "Bass";
        if (n.contains ("pad") || n.contains ("granular") || n.contains ("swell") || n.contains ("sustained")) return "Pad";
        if (n.contains ("pluck")) return "Pluck";
        return "Keys";
    }

    /** Gives an empty Simpler a factory multisample (it keeps any sample it already has). */
    inline bool fillSimpler (pulse::Simpler& s, const String& instrument = "Keys")
    {
        if (s.numZones() > 0 || s.state[pulse::Simpler::sampleFileID].toString().isNotEmpty()) return false;
        auto files = instrumentFiles (s.edit.engine, instrument);
        if (files.isEmpty()) return false;
        s.autoMap (files);
        return true;
    }

    /** Loads a factory sampled kit (one layer per GM slot). */
    inline int loadKit (te::RackType& rack, int style)
    {
        int loaded = 0;
        for (auto& v : voices())
            if (DrumRack::loadSample (rack, v.slot, drumFile (rack.edit.engine, style, v.name)))
            {
                ++loaded;
                if (v.type == pulse::DrumVoice::closedHat || v.type == pulse::DrumVoice::openHat)
                    if (auto p = DrumRack::source (rack, v.slot)) p->set (pulse::DrumPad::pChoke, 1.0f);
            }
        return loaded;
    }
}
