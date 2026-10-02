#pragma once

#include "Common.h"
#include "FactoryPresetsMore.h"

/** Built-in presets for Pulse's own devices (sounds and effect settings). A preset resets the
    device to its defaults and then sets the listed parameters (by id, in real units). */
namespace FactoryPresets
{
    struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };

    inline const std::map<String, std::vector<Preset>>& base()
    {
        static const std::map<String, std::vector<Preset>> presets {
            { "pulseWavetable", {
                { "Supersaw Lead", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 0 }, { "pos2", 0.66f }, { "fine2", 12 }, { "mix", 0.5f }, { "voices", 7 }, { "detune", 0.45f },
                                     { "ftype", 1 }, { "cutoff", 6000 }, { "reso", 0.15f }, { "fenv", 0.2f }, { "fd", 300 }, { "a", 2 }, { "d", 400 }, { "s", 0.8f }, { "r", 200 }, { "level", -10 } } },
                { "Sub Bass", { { "set1", 0 }, { "pos1", 0 }, { "mix", 0 }, { "voices", 1 }, { "sub", 0.5f }, { "ftype", 1 }, { "cutoff", 400 }, { "fenv", 0 },
                                { "a", 2 }, { "s", 1 }, { "r", 80 }, { "level", -6 } } },
                { "Acid Bass", { { "set1", 0 }, { "pos1", 0.66f }, { "mix", 0 }, { "voices", 1 }, { "ftype", 1 }, { "cutoff", 300 }, { "reso", 0.75f }, { "fenv", 0.7f },
                                 { "fa", 1 }, { "fd", 220 }, { "fs", 0 }, { "a", 1 }, { "d", 300 }, { "s", 0.6f }, { "r", 60 }, { "glide", 60 }, { "level", -8 } } },
                { "Pluck", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 1 }, { "pos2", 0.2f }, { "voices", 3 }, { "detune", 0.2f }, { "ftype", 1 }, { "cutoff", 800 },
                             { "reso", 0.3f }, { "fenv", 0.8f }, { "fd", 180 }, { "fs", 0 }, { "a", 1 }, { "d", 350 }, { "s", 0 }, { "r", 250 }, { "level", -8 } } },
                { "Warm Pad", { { "set1", 2 }, { "pos1", 0.5f }, { "set2", 3 }, { "pos2", 0.4f }, { "voices", 5 }, { "detune", 0.35f }, { "ftype", 0 }, { "cutoff", 2500 },
                                { "reso", 0.1f }, { "fenv", 0.1f }, { "a", 900 }, { "d", 1500 }, { "s", 0.8f }, { "r", 2500 }, { "lfopos", 0.4f }, { "lforate", 0.2f }, { "level", -14 } } },
                { "Vowel Choir", { { "set1", 3 }, { "pos1", 0.5f }, { "set2", 3 }, { "pos2", 0.7f }, { "voices", 5 }, { "detune", 0.3f }, { "a", 400 }, { "s", 1 }, { "r", 1500 },
                                   { "lfopos", 0.6f }, { "lforate", 0.15f }, { "cutoff", 7000 }, { "level", -12 } } },
                { "Sync Lead", { { "set1", 4 }, { "pos1", 0.4f }, { "set2", 0 }, { "pos2", 0.66f }, { "voices", 3 }, { "lfopos", 0.3f }, { "lforate", 3 }, { "cutoff", 8000 },
                                 { "glide", 40 }, { "level", -10 } } },
                { "Hollow Keys", { { "set1", 1 }, { "pos1", 0.1f }, { "set2", 2 }, { "pos2", 0.3f }, { "voices", 2 }, { "ftype", 1 }, { "cutoff", 2200 }, { "fenv", 0.4f },
                                   { "fd", 500 }, { "fs", 0.2f }, { "a", 2 }, { "d", 900 }, { "s", 0.4f }, { "r", 400 }, { "level", -10 } } } } },
            { "pulseFM", {
                { "E-Piano", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0level", 1 }, { "op0d", 1800 }, { "op0s", 0.25f }, { "op0r", 400 },
                               { "op1ratio", 1 }, { "op1level", 0.45f }, { "op1d", 600 }, { "op1s", 0 }, { "op2ratio", 1 }, { "op2level", 0.6f }, { "op2d", 1200 }, { "op2s", 0.2f },
                               { "op3ratio", 14 }, { "op3level", 0.15f }, { "op3d", 150 }, { "op3s", 0 }, { "volume", -8 } } },
                { "Bell", { { "algorithm", 6 }, { "op0ratio", 1 }, { "op0d", 4000 }, { "op0s", 0 }, { "op0r", 3000 }, { "op1ratio", 2 }, { "op1level", 0.3f }, { "op1d", 3000 }, { "op1s", 0 },
                            { "op2ratio", 1 }, { "op2level", 0.7f }, { "op2d", 3500 }, { "op2s", 0 }, { "op3ratio", 3.5f }, { "op3level", 0.6f }, { "op3d", 2500 }, { "op3s", 0 }, { "volume", -10 } } },
                { "FM Bass", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0s", 0.8f }, { "op0r", 80 }, { "op1ratio", 1 }, { "op1level", 0.6f }, { "op1d", 250 }, { "op1s", 0.2f },
                               { "op2ratio", 0.5f }, { "op2level", 0.3f }, { "op3ratio", 2 }, { "op3level", 0.1f }, { "feedback", 0.2f }, { "cutoff", 3000 }, { "volume", -6 } } },
                { "Brass", { { "algorithm", 1 }, { "op0ratio", 1 }, { "op0a", 60 }, { "op0s", 0.8f }, { "op1ratio", 1 }, { "op1level", 0.55f }, { "op1a", 80 }, { "op1s", 0.7f },
                             { "op2ratio", 1 }, { "op2level", 0.2f }, { "op3ratio", 2 }, { "op3level", 0.15f }, { "fenv", 0.3f }, { "cutoff", 4000 }, { "fd", 300 }, { "volume", -8 } } },
                { "Mallet", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0d", 500 }, { "op0s", 0 }, { "op1ratio", 4 }, { "op1level", 0.5f }, { "op1d", 80 }, { "op1s", 0 },
                              { "op2ratio", 3 }, { "op2level", 0.4f }, { "op2d", 300 }, { "op2s", 0 }, { "op3ratio", 7 }, { "op3level", 0.3f }, { "op3d", 60 }, { "op3s", 0 }, { "volume", -8 } } },
                { "Metallic Pad", { { "algorithm", 7 }, { "op0ratio", 1 }, { "op0a", 800 }, { "op0s", 1 }, { "op0r", 2000 }, { "op1ratio", 2.5f }, { "op1level", 0.4f }, { "op1a", 1200 },
                                    { "op1s", 1 }, { "op1r", 2000 }, { "op2ratio", 3.5f }, { "op2level", 0.25f }, { "op2a", 1500 }, { "op2s", 1 }, { "op2r", 2000 },
                                    { "op3ratio", 5 }, { "op3level", 0.15f }, { "op3a", 2000 }, { "op3s", 1 }, { "op3r", 2000 }, { "volume", -14 } } } } },
            { "pulseDrumSynth", {
                { "808", { { "tune", -2 }, { "decay", 1.6f }, { "tone", 0.3f }, { "snap", 0.3f }, { "drive", 0.1f } } },
                { "909", { { "tune", 2 }, { "decay", 0.9f }, { "tone", 0.6f }, { "snap", 0.8f }, { "drive", 0.25f } } },
                { "Lo-fi", { { "tune", -4 }, { "decay", 0.7f }, { "tone", 0.2f }, { "snap", 0.4f }, { "drive", 0.7f } } },
                { "Electro", { { "tune", 0 }, { "decay", 0.6f }, { "tone", 0.8f }, { "snap", 1.0f }, { "drive", 0.4f } } } } },
            { "pulseGlue", {
                { "Drum Bus", { { "threshold", -18 }, { "ratio", 1 }, { "attack", 4 }, { "release", 1 }, { "makeup", 4 } } },
                { "Mix Bus Glue", { { "threshold", -12 }, { "ratio", 0 }, { "attack", 5 }, { "release", 6 }, { "makeup", 2 } } },
                { "Pumping", { { "threshold", -30 }, { "ratio", 2 }, { "attack", 1 }, { "release", 1 }, { "makeup", 3 } } },
                { "Parallel Crush", { { "threshold", -35 }, { "ratio", 2 }, { "attack", 2 }, { "release", 2 }, { "makeup", 12 }, { "mix", 0.35f }, { "softclip", 1 } } } } },
            { "pulseReverb", {
                { "Small Room", { { "predelay", 5 }, { "size", 0.3f }, { "decay", 0.6f }, { "damping", 6000 }, { "mix", 0.2f } } },
                { "Hall", { { "predelay", 25 }, { "size", 0.8f }, { "decay", 3.0f }, { "damping", 7000 }, { "mix", 0.3f } } },
                { "Plate", { { "predelay", 10 }, { "size", 0.5f }, { "decay", 1.8f }, { "damping", 12000 }, { "diffusion", 0.85f }, { "mix", 0.25f } } },
                { "Ambient Wash", { { "predelay", 60 }, { "size", 1.0f }, { "decay", 9.0f }, { "damping", 5000 }, { "mod", 0.6f }, { "mix", 0.5f } } },
                { "Vocal Space", { { "predelay", 40 }, { "size", 0.55f }, { "decay", 1.4f }, { "lowcut", 300 }, { "damping", 8000 }, { "mix", 0.2f } } } } },
            { "pulseDelay", {
                { "Dotted 8th", { { "sync", 1 }, { "note", 5 }, { "feedback", 0.35f }, { "mix", 0.25f } } },
                { "Ping-Pong 1/4", { { "sync", 1 }, { "note", 7 }, { "pingpong", 1 }, { "feedback", 0.4f }, { "mix", 0.25f } } },
                { "Tape Echo", { { "sync", 1 }, { "note", 4 }, { "feedback", 0.5f }, { "highcut", 3500 }, { "lowcut", 200 }, { "mod", 0.4f }, { "drive", 0.4f }, { "mix", 0.3f } } },
                { "Slapback", { { "sync", 0 }, { "time", 90 }, { "feedback", 0 }, { "mix", 0.3f } } },
                { "Dub Throw", { { "sync", 1 }, { "note", 8 }, { "feedback", 0.7f }, { "highcut", 2500 }, { "mix", 0.35f } } } } },
            { "pulseEQ", {
                { "Low Cut 100 Hz", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 100 } } },
                { "Vocal Presence", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 90 }, { "b3gain", -2.5f }, { "b3freq", 350 }, { "b4gain", 2 }, { "b4freq", 3000 },
                                      { "b5gain", 3 }, { "b5freq", 10000 } } },
                { "Kick Clean", { { "b2gain", 3 }, { "b2freq", 60 }, { "b3gain", -5 }, { "b3freq", 350 }, { "b3q", 2 }, { "b5gain", 2 }, { "b5freq", 4000 } } },
                { "Telephone", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 400 }, { "b6on", 1 }, { "b6type", 5 }, { "b6freq", 3000 }, { "b4gain", 6 }, { "b4freq", 1500 } } } } },
            { "pulseAutoFilter", {
                { "Sweep Low Pass", { { "type", 0 }, { "cutoff", 800 }, { "reso", 0.5f }, { "lfoamount", 0.6f }, { "lfosync", 6 }, { "lfoshape", 1 } } },
                { "Wobble 1/8", { { "type", 4 }, { "cutoff", 500 }, { "reso", 0.6f }, { "lfoamount", 0.8f }, { "lfosync", 2 }, { "lfoshape", 0 }, { "drive", 6 } } },
                { "Envelope Funk", { { "type", 1 }, { "cutoff", 600 }, { "reso", 0.55f }, { "lfoamount", 0 }, { "envamount", 0.8f }, { "envattack", 3 }, { "envrelease", 120 } } } } },
            { "pulseSaturator", {
                { "Warm Tape", { { "type", 1 }, { "drive", 8 }, { "tone", -0.2f } } },
                { "Tube Drive", { { "type", 2 }, { "drive", 14 }, { "tone", 0.1f } } },
                { "Bit Crush", { { "type", 6 }, { "drive", 12 }, { "mix", 0.7f } } },
                { "Fold Fuzz", { { "type", 4 }, { "drive", 20 }, { "tone", -0.4f }, { "output", -6 } } } } },
            { "pulseUtility", {
                { "Mono Bass (below 120 Hz)", { { "bassmono", 1 }, { "bassfreq", 120 } } },
                { "Extra Wide", { { "width", 1.6f } } },
                { "Mono", { { "mono", 1 } } } } },
            { "pulseVocoder", {
                { "Robot", { { "carrier", 1 }, { "pitch", 43 }, { "noise", 0.1f } } },
                { "Whisper", { { "carrier", 2 }, { "noise", 0.6f } } },
                { "Synth Choir (sidechain)", { { "carrier", 0 }, { "noise", 0.1f }, { "formant", 2 } } } } },
            { "pulsePitchCorrect", {
                { "Hard Tune", { { "speed", 0 }, { "amount", 1 } } },
                { "Natural", { { "speed", 120 }, { "amount", 0.8f } } } } },
            { "pulseLimiter", {
                { "Master -1 dBTP", { { "gain", 3 }, { "ceiling", -1 } } },
                { "Loud", { { "gain", 8 }, { "ceiling", -0.3f } } } } }
        };
        return presets;
    }

    /** Every factory preset: the originals, then the extra set (FactoryPresetsMore.h). */
    inline const std::map<String, std::vector<Preset>>& all()
    {
        static const auto merged = []
        {
            auto m = base();
            for (auto& [type, list] : FactoryPresetsMore::all())
                for (auto& p : list)
                    m[type].push_back ({ p.name, p.values });
            return m;
        }();
        return merged;
    }

    inline const std::vector<Preset>& forType (const String& type)
    {
        static const std::vector<Preset> none;
        auto& m = all();
        auto it = m.find (type);
        return it != m.end() ? it->second : none;
    }

    /** Applies a preset by name: defaults first, then the preset's values. */
    /** Device-internal modulation in presets: values named "mod:<source>:<parameter id>" are
        routes (the amount is the value). Devices with a matrix set these hooks. */
    inline std::function<void (te::Plugin&)> clearRoutes;
    inline std::function<void (te::Plugin&, int source, const String& dest, float amount)> addRoute;

    inline bool apply (te::Plugin& p, const String& name)
    {
        for (auto& pr : forType (p.getPluginType()))
            if (name == pr.name)
            {
                for (auto prm : p.getAutomatableParameters())
                    if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync);
                if (clearRoutes) clearRoutes (p);
                for (auto& [id, v] : pr.values)
                {
                    const String key (id);
                    if (key.startsWith ("mod:"))
                    {
                        if (addRoute) addRoute (p, key.fromFirstOccurrenceOf ("mod:", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                                key.fromLastOccurrenceOf (":", false, false), v);
                        continue;
                    }
                    if (auto prm = p.getAutomatableParameterByID (id))
                        prm->setParameter (prm->valueRange.snapToLegalValue (v), sendNotificationSync);
                }
                return true;
            }
        return false;
    }

    /** Browser / drag description for a factory preset. */
    inline String description (const String& type, const String& name) { return "plugin:factory:" + type + ":" + name; }
}
