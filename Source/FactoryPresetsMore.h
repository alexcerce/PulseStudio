#pragma once

#include <map>
#include <vector>

/** More factory presets (merged into FactoryPresets::all()). Values are in real units, by
    parameter id; anything not listed stays at the device's default. */
namespace FactoryPresetsMore
{
    struct P { const char* name; std::vector<std::pair<const char*, float>> values; };
    using Map = std::map<juce::String, std::vector<P>>;

    inline const Map& all()
    {
        static const Map m {
            //----------------------------------------------------------------------------------
            // Wavetable: set 0 basic shapes, 1 pulse width, 2 harmonics, 3 vowels, 4 sync saw
            // ftype 0 LP12, 1 LP24, 2 BP, 3 HP
            { "pulseWavetable", {
                { "Reese Bass", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 0 }, { "pos2", 0.66f }, { "fine2", -18 }, { "mix", 0.5f }, { "voices", 3 }, { "detune", 0.3f },
                                  { "sub", 0.3f }, { "ftype", 1 }, { "cutoff", 900 }, { "reso", 0.2f }, { "fenv", 0 }, { "a", 2 }, { "s", 1 }, { "r", 120 }, { "lfocut", 0.25f },
                                  { "lforate", 0.3f }, { "level", -9 } } },
                { "Square Lead", { { "set1", 1 }, { "pos1", 0.5f }, { "mix", 0 }, { "voices", 1 }, { "ftype", 0 }, { "cutoff", 4500 }, { "reso", 0.25f }, { "fenv", 0.25f },
                                   { "fd", 250 }, { "a", 3 }, { "d", 300 }, { "s", 0.8f }, { "r", 150 }, { "glide", 50 }, { "level", -10 } } },
                { "Trance Gate Pad", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 2 }, { "pos2", 0.6f }, { "voices", 7 }, { "detune", 0.5f }, { "ftype", 0 }, { "cutoff", 3500 },
                                       { "fenv", 0 }, { "a", 5 }, { "d", 120 }, { "s", 0.2f }, { "r", 120 }, { "level", -12 } } },
                { "Glass Bells", { { "set1", 2 }, { "pos1", 0.9f }, { "set2", 2 }, { "pos2", 0.7f }, { "semi2", 12 }, { "fine2", 3 }, { "mix", 0.4f }, { "voices", 2 },
                                   { "detune", 0.1f }, { "ftype", 3 }, { "cutoff", 300 }, { "fenv", 0 }, { "a", 1 }, { "d", 1600 }, { "s", 0 }, { "r", 1400 }, { "level", -12 } } },
                { "Dark Drone", { { "set1", 3 }, { "pos1", 0.2f }, { "set2", 0 }, { "pos2", 0.66f }, { "semi2", -12 }, { "voices", 5 }, { "detune", 0.4f }, { "ftype", 1 },
                                  { "cutoff", 700 }, { "reso", 0.35f }, { "fenv", 0 }, { "a", 2500 }, { "s", 1 }, { "r", 4000 }, { "lfopos", 0.5f }, { "lfocut", 0.3f },
                                  { "lforate", 0.08f }, { "level", -12 } } },
                { "Stab Chord", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 1 }, { "pos2", 0.4f }, { "voices", 5 }, { "detune", 0.3f }, { "ftype", 1 }, { "cutoff", 1200 },
                                  { "reso", 0.2f }, { "fenv", 0.6f }, { "fd", 160 }, { "fs", 0 }, { "a", 1 }, { "d", 250 }, { "s", 0 }, { "r", 180 }, { "level", -10 } } },
                { "Wobble Bass", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 4 }, { "pos2", 0.5f }, { "mix", 0.4f }, { "voices", 2 }, { "sub", 0.4f }, { "ftype", 1 },
                                   { "cutoff", 500 }, { "reso", 0.5f }, { "fenv", 0 }, { "lfocut", 0.7f }, { "lforate", 3 }, { "s", 1 }, { "r", 80 }, { "level", -9 } } },
                { "Soft Keys", { { "set1", 2 }, { "pos1", 0.3f }, { "set2", 0 }, { "pos2", 0.33f }, { "mix", 0.3f }, { "voices", 2 }, { "detune", 0.1f }, { "ftype", 0 },
                                 { "cutoff", 1800 }, { "fenv", 0.3f }, { "fd", 700 }, { "fs", 0.2f }, { "a", 3 }, { "d", 1200 }, { "s", 0.3f }, { "r", 500 }, { "level", -10 } } },
                { "Chiptune", { { "set1", 1 }, { "pos1", 0.25f }, { "mix", 0 }, { "voices", 1 }, { "ftype", 0 }, { "cutoff", 20000 }, { "fenv", 0 }, { "a", 0.5f },
                                { "d", 100 }, { "s", 0.7f }, { "r", 40 }, { "level", -14 } } },
                { "Evolving Texture", { { "set1", 3 }, { "pos1", 0.5f }, { "set2", 2 }, { "pos2", 0.5f }, { "semi2", 7 }, { "voices", 4 }, { "detune", 0.3f }, { "ftype", 2 },
                                        { "cutoff", 1500 }, { "reso", 0.3f }, { "a", 1500 }, { "s", 0.9f }, { "r", 3000 }, { "lfopos", 0.8f }, { "lfocut", 0.4f },
                                        { "lforate", 0.12f }, { "level", -12 } } },
                { "Hoover", { { "set1", 4 }, { "pos1", 0.7f }, { "set2", 1 }, { "pos2", 0.2f }, { "semi2", -12 }, { "voices", 7 }, { "detune", 0.6f }, { "ftype", 1 },
                              { "cutoff", 3000 }, { "fenv", 0.2f }, { "glide", 120 }, { "a", 10 }, { "s", 1 }, { "r", 300 }, { "level", -10 } } },
                { "Deep House Organ", { { "set1", 2 }, { "pos1", 0.15f }, { "set2", 2 }, { "pos2", 0.45f }, { "semi2", 12 }, { "mix", 0.4f }, { "voices", 1 }, { "ftype", 0 },
                                        { "cutoff", 2400 }, { "fenv", 0.2f }, { "fd", 120 }, { "fs", 0 }, { "a", 1 }, { "d", 300 }, { "s", 0.5f }, { "r", 80 }, { "level", -10 } } } } },

            //----------------------------------------------------------------------------------
            { "pulseFM", {
                { "DX Bass", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0d", 400 }, { "op0s", 0.6f }, { "op0r", 60 }, { "op1ratio", 1 }, { "op1level", 0.7f }, { "op1d", 180 },
                               { "op1s", 0.1f }, { "op2ratio", 1 }, { "op2level", 0.3f }, { "op2d", 120 }, { "op2s", 0 }, { "op3ratio", 3 }, { "op3level", 0.2f }, { "op3d", 60 },
                               { "op3s", 0 }, { "feedback", 0.3f }, { "volume", -6 } } },
                { "Tubular Bell", { { "algorithm", 5 }, { "op0ratio", 1 }, { "op0d", 5000 }, { "op0s", 0 }, { "op0r", 4000 }, { "op1ratio", 3.5f }, { "op1level", 0.5f },
                                    { "op1d", 3000 }, { "op1s", 0 }, { "op2ratio", 2 }, { "op2level", 0.8f }, { "op2d", 4500 }, { "op2s", 0 }, { "op3ratio", 7 },
                                    { "op3level", 0.35f }, { "op3d", 1500 }, { "op3s", 0 }, { "volume", -10 } } },
                { "Glass Pad", { { "algorithm", 6 }, { "op0ratio", 1 }, { "op0a", 900 }, { "op0s", 1 }, { "op0r", 2500 }, { "op1ratio", 4 }, { "op1level", 0.2f }, { "op1a", 1500 },
                                 { "op1s", 0.8f }, { "op1r", 2500 }, { "op2ratio", 2 }, { "op2level", 0.6f }, { "op2a", 1200 }, { "op2s", 1 }, { "op2r", 2500 }, { "op3ratio", 6 },
                                 { "op3level", 0.15f }, { "op3a", 2000 }, { "op3s", 1 }, { "op3r", 2500 }, { "volume", -14 } } },
                { "Clav", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0d", 600 }, { "op0s", 0 }, { "op0r", 60 }, { "op1ratio", 3 }, { "op1level", 0.55f }, { "op1d", 250 },
                            { "op1s", 0 }, { "op2ratio", 1 }, { "op2level", 0.3f }, { "op2d", 120 }, { "op2s", 0 }, { "op3ratio", 5 }, { "op3level", 0.2f }, { "op3d", 40 },
                            { "op3s", 0 }, { "volume", -9 } } },
                { "Marimba", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0d", 700 }, { "op0s", 0 }, { "op0r", 300 }, { "op1ratio", 10 }, { "op1level", 0.3f }, { "op1d", 40 },
                               { "op1s", 0 }, { "op2ratio", 4 }, { "op2level", 0.25f }, { "op2d", 400 }, { "op2s", 0 }, { "op3ratio", 1 }, { "op3level", 0.2f }, { "op3d", 30 },
                               { "op3s", 0 }, { "volume", -8 } } },
                { "Organ", { { "algorithm", 7 }, { "op0ratio", 1 }, { "op0a", 3 }, { "op0s", 1 }, { "op0r", 60 }, { "op1ratio", 2 }, { "op1level", 0.6f }, { "op1a", 3 },
                             { "op1s", 1 }, { "op1r", 60 }, { "op2ratio", 3 }, { "op2level", 0.4f }, { "op2a", 3 }, { "op2s", 1 }, { "op2r", 60 }, { "op3ratio", 4 },
                             { "op3level", 0.25f }, { "op3a", 3 }, { "op3s", 1 }, { "op3r", 60 }, { "volume", -12 } } },
                { "Growl Lead", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0s", 0.9f }, { "op0r", 150 }, { "op1ratio", 1 }, { "op1level", 0.8f }, { "op1s", 0.7f },
                                  { "op2ratio", 2 }, { "op2level", 0.4f }, { "op3ratio", 1 }, { "op3level", 0.3f }, { "feedback", 0.7f }, { "cutoff", 5000 }, { "reso", 0.3f },
                                  { "glide", 60 }, { "volume", -10 } } },
                { "Sitar-ish", { { "algorithm", 1 }, { "op0ratio", 1 }, { "op0d", 2500 }, { "op0s", 0.1f }, { "op1ratio", 1 }, { "op1level", 0.5f }, { "op1d", 1800 },
                                 { "op1s", 0.1f }, { "op2ratio", 5 }, { "op2level", 0.3f }, { "op2d", 900 }, { "op2s", 0 }, { "op3ratio", 1.01f }, { "op3level", 0.6f },
                                 { "op3d", 2000 }, { "op3s", 0.2f }, { "feedback", 0.4f }, { "volume", -10 } } },
                { "Kalimba", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0d", 900 }, { "op0s", 0 }, { "op0r", 600 }, { "op1ratio", 7 }, { "op1level", 0.4f }, { "op1d", 60 },
                               { "op1s", 0 }, { "op2ratio", 2 }, { "op2level", 0.3f }, { "op2d", 500 }, { "op2s", 0 }, { "op3ratio", 13 }, { "op3level", 0.15f }, { "op3d", 30 },
                               { "op3s", 0 }, { "volume", -8 } } },
                { "Noise Sweep", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0a", 1500 }, { "op0s", 1 }, { "op0r", 1500 }, { "op1ratio", 7.3f }, { "op1level", 1 },
                                   { "op2ratio", 13.7f }, { "op2level", 1 }, { "op3ratio", 3.1f }, { "op3level", 1 }, { "feedback", 1 }, { "cutoff", 400 }, { "reso", 0.5f },
                                   { "fenv", 0.9f }, { "fa", 2000 }, { "fs", 1 }, { "volume", -16 } } } } },

            //----------------------------------------------------------------------------------
            // Simpler: mode 0 classic, 1 one-shot, 2 mono, 3 granular; ftype 0 off, 1 LP, 2 BP, 3 HP
            { "pulseSimpler", {
                { "One-shot", { { "mode", 1 }, { "a", 0.5f }, { "s", 1 }, { "r", 30 } } },
                { "Sustained (loop)", { { "loop", 1 }, { "loopstart", 0.3f }, { "xfade", 0.1f }, { "a", 20 }, { "s", 1 }, { "r", 400 } } },
                { "Plucked", { { "a", 0.5f }, { "d", 250 }, { "s", 0 }, { "r", 150 }, { "ftype", 1 }, { "cutoff", 2500 }, { "fenv", 0.5f }, { "fd", 200 }, { "fs", 0 } } },
                { "Soft Pad", { { "loop", 1 }, { "loopstart", 0.2f }, { "xfade", 0.2f }, { "a", 800 }, { "s", 1 }, { "r", 1500 }, { "ftype", 1 }, { "cutoff", 3000 } } },
                { "Lo-fi Keys", { { "ftype", 1 }, { "cutoff", 2200 }, { "reso", 0.25f }, { "fine", -12 }, { "a", 2 }, { "d", 800 }, { "s", 0.5f }, { "r", 300 } } },
                { "Mono Bass", { { "mode", 2 }, { "transpose", -12 }, { "ftype", 1 }, { "cutoff", 900 }, { "reso", 0.3f }, { "fenv", 0.4f }, { "fd", 180 }, { "fs", 0.2f },
                                 { "s", 1 }, { "r", 60 } } },
                { "Granular Cloud", { { "mode", 3 }, { "grainsize", 180 }, { "spray", 0.5f }, { "grainpos", 0.4f }, { "a", 600 }, { "s", 1 }, { "r", 1500 } } },
                { "Granular Freeze", { { "mode", 3 }, { "grainsize", 60 }, { "spray", 0.05f }, { "grainpos", 0.5f }, { "a", 50 }, { "s", 1 }, { "r", 600 } } },
                { "Reverse Swell", { { "a", 1200 }, { "s", 1 }, { "r", 200 }, { "ftype", 3 }, { "cutoff", 200 } } },
                { "Octave Down", { { "transpose", -12 }, { "ftype", 1 }, { "cutoff", 5000 } } } } },

            //----------------------------------------------------------------------------------
            // Drum Synth: map 0 = GM kit
            { "pulseDrumSynth", {
                { "Deep House", { { "tune", -1 }, { "decay", 1.1f }, { "tone", 0.4f }, { "snap", 0.5f }, { "drive", 0.15f } } },
                { "Techno Rumble", { { "tune", -3 }, { "decay", 2.2f }, { "tone", 0.25f }, { "snap", 0.6f }, { "drive", 0.6f } } },
                { "Trap", { { "tune", -5 }, { "decay", 2.8f }, { "tone", 0.35f }, { "snap", 0.4f }, { "drive", 0.3f } } },
                { "Tight Funk", { { "tune", 3 }, { "decay", 0.5f }, { "tone", 0.65f }, { "snap", 0.7f }, { "drive", 0.1f } } },
                { "Industrial", { { "tune", -2 }, { "decay", 1.0f }, { "tone", 0.9f }, { "snap", 1.0f }, { "drive", 1.0f } } },
                { "Minimal Click", { { "tune", 5 }, { "decay", 0.3f }, { "tone", 0.75f }, { "snap", 0.9f }, { "drive", 0 } } },
                { "Boom Bap", { { "tune", -1 }, { "decay", 0.8f }, { "tone", 0.3f }, { "snap", 0.55f }, { "drive", 0.45f } } },
                { "Soft (no velocity)", { { "velsens", 0 }, { "decay", 0.9f }, { "tone", 0.4f }, { "gain", -4 } } } } },

            //----------------------------------------------------------------------------------
            // Glue: ratio 0 2:1, 1 4:1, 2 10:1; attack 0..6 = 0.01, 0.1, 0.3, 1, 3, 10, 30 ms;
            // release 0..6 = 0.1, 0.2, 0.4, 0.6, 0.8, 1.2 s, auto
            { "pulseGlue", {
                { "Gentle Bus", { { "threshold", -8 }, { "ratio", 0 }, { "attack", 5 }, { "release", 6 }, { "makeup", 1 } } },
                { "Vocal Leveler", { { "threshold", -20 }, { "ratio", 1 }, { "attack", 3 }, { "release", 2 }, { "makeup", 5 } } },
                { "Bass Control", { { "threshold", -16 }, { "ratio", 1 }, { "attack", 4 }, { "release", 3 }, { "makeup", 3 }, { "schpf", 20 } } },
                { "Sidechain Duck", { { "threshold", -28 }, { "ratio", 2 }, { "attack", 0 }, { "release", 1 }, { "makeup", 0 }, { "schpf", 20 } } },
                { "Snare Crack", { { "threshold", -22 }, { "ratio", 1 }, { "attack", 5 }, { "release", 1 }, { "makeup", 5 } } },
                { "Room Smash", { { "threshold", -38 }, { "ratio", 2 }, { "attack", 0 }, { "release", 0 }, { "makeup", 14 }, { "softclip", 1 }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            { "pulseReverb", {
                { "Drum Room", { { "predelay", 0 }, { "size", 0.25f }, { "decay", 0.45f }, { "damping", 9000 }, { "lowcut", 200 }, { "mix", 0.18f } } },
                { "Cathedral", { { "predelay", 50 }, { "size", 1.0f }, { "decay", 7.0f }, { "damping", 5000 }, { "diffusion", 0.85f }, { "mix", 0.35f } } },
                { "Bright Plate", { { "predelay", 5 }, { "size", 0.45f }, { "decay", 2.2f }, { "damping", 16000 }, { "diffusion", 0.9f }, { "lowcut", 250 }, { "mix", 0.22f } } },
                { "Dark Chamber", { { "predelay", 20 }, { "size", 0.6f }, { "decay", 2.5f }, { "damping", 2500 }, { "mix", 0.25f } } },
                { "Shimmer-ish Wash", { { "predelay", 80 }, { "size", 1.0f }, { "decay", 14.0f }, { "damping", 11000 }, { "mod", 0.9f }, { "width", 1 }, { "mix", 0.5f } } },
                { "Frozen Pad", { { "size", 1.0f }, { "decay", 20.0f }, { "freeze", 1 }, { "mix", 1.0f } } },
                { "Narrow Mono Verb", { { "size", 0.4f }, { "decay", 1.2f }, { "width", 0.0f }, { "mix", 0.2f } } },
                { "Return 100% Wet", { { "predelay", 20 }, { "size", 0.7f }, { "decay", 2.5f }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            // Delay: note 0 1/32, 1 1/16, 2 1/16., 3 1/8T, 4 1/8, 5 1/8., 6 1/4T, 7 1/4, 8 1/4., 9 1/2, 10 1 bar
            { "pulseDelay", {
                { "1/8 Echo", { { "sync", 1 }, { "note", 4 }, { "feedback", 0.3f }, { "mix", 0.22f } } },
                { "Triplet Bounce", { { "sync", 1 }, { "note", 6 }, { "pingpong", 1 }, { "feedback", 0.45f }, { "mix", 0.25f } } },
                { "Long Ambient", { { "sync", 1 }, { "note", 9 }, { "feedback", 0.75f }, { "highcut", 4000 }, { "lowcut", 300 }, { "mod", 0.3f }, { "mix", 0.3f } } },
                { "Lo-fi Cassette", { { "sync", 1 }, { "note", 5 }, { "feedback", 0.55f }, { "highcut", 2200 }, { "lowcut", 400 }, { "mod", 0.8f }, { "drive", 0.7f },
                                      { "mix", 0.35f } } },
                { "Haas Widener", { { "sync", 0 }, { "time", 18 }, { "feedback", 0 }, { "pingpong", 1 }, { "mix", 0.4f } } },
                { "Rhythmic 1/16", { { "sync", 1 }, { "note", 1 }, { "feedback", 0.6f }, { "pingpong", 1 }, { "highcut", 6000 }, { "mix", 0.2f } } },
                { "Return 100% Wet", { { "sync", 1 }, { "note", 5 }, { "feedback", 0.4f }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            // EQ Six: types 0 low cut, 1 low shelf, 2 bell, 3 notch, 4 high shelf, 5 high cut
            { "pulseEQ", {
                { "Air Lift", { { "b5on", 1 }, { "b5type", 4 }, { "b5gain", 4 }, { "b5freq", 12000 } } },
                { "Mud Cut", { { "b3on", 1 }, { "b3type", 2 }, { "b3gain", -4 }, { "b3freq", 300 }, { "b3q", 1.2f } } },
                { "Bass Boost", { { "b2on", 1 }, { "b2type", 1 }, { "b2gain", 4 }, { "b2freq", 90 } } },
                { "Snare Body", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 120 }, { "b3gain", 3 }, { "b3freq", 200 }, { "b4gain", 3 }, { "b4freq", 5000 } } },
                { "Hi-Hat Clean", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 400 }, { "b4gain", -2 }, { "b4freq", 3000 } } },
                { "Guitar Bite", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 80 }, { "b3gain", -3 }, { "b3freq", 250 }, { "b4gain", 3 }, { "b4freq", 2500 } } },
                { "Radio", { { "b1on", 1 }, { "b1type", 0 }, { "b1freq", 600 }, { "b6on", 1 }, { "b6type", 5 }, { "b6freq", 5000 }, { "b4gain", 5 }, { "b4freq", 1800 } } },
                { "Master Smile", { { "b2gain", 1.5f }, { "b2freq", 80 }, { "b3gain", -1 }, { "b3freq", 600 }, { "b5gain", 1.5f }, { "b5freq", 10000 } } },
                { "Notch 50 Hz Hum", { { "b3on", 1 }, { "b3type", 3 }, { "b3freq", 50 }, { "b3q", 10 } } } } },

            //----------------------------------------------------------------------------------
            // Auto Filter: type 0 LP, 1 BP, 2 HP, 3 notch, 4 LP24; lfosync 0 free, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 bar, 6 2 bars, 7 4 bars
            // lfoshape 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square, 5 random
            { "pulseAutoFilter", {
                { "High-Pass Build", { { "type", 2 }, { "cutoff", 200 }, { "reso", 0.4f }, { "lfoamount", 0.7f }, { "lfosync", 7 }, { "lfoshape", 2 } } },
                { "Gated 1/16", { { "type", 0 }, { "cutoff", 400 }, { "reso", 0.3f }, { "lfoamount", 0.9f }, { "lfosync", 1 }, { "lfoshape", 4 } } },
                { "Random S&H", { { "type", 4 }, { "cutoff", 1200 }, { "reso", 0.6f }, { "lfoamount", 0.7f }, { "lfosync", 1 }, { "lfoshape", 5 } } },
                { "Phaser-ish Notch", { { "type", 3 }, { "cutoff", 1000 }, { "reso", 0.5f }, { "lfoamount", 0.6f }, { "lfosync", 0 }, { "lfoshape", 0 }, { "phase", 90 } } },
                { "Band-Pass Talk", { { "type", 1 }, { "cutoff", 900 }, { "reso", 0.7f }, { "lfoamount", 0.5f }, { "lfosync", 3 }, { "lfoshape", 1 } } },
                { "Dark Static", { { "type", 4 }, { "cutoff", 600 }, { "reso", 0.2f }, { "lfoamount", 0 }, { "drive", 4 } } },
                { "Auto-Wah", { { "type", 1 }, { "cutoff", 400 }, { "reso", 0.65f }, { "lfoamount", 0 }, { "envamount", 0.9f }, { "envattack", 2 }, { "envrelease", 200 } } } } },

            //----------------------------------------------------------------------------------
            // Saturator: 0 analog, 1 tape, 2 tube, 3 hard clip, 4 foldback, 5 sine fold, 6 bit crush
            { "pulseSaturator", {
                { "Subtle Warmth", { { "type", 0 }, { "drive", 3 }, { "tone", -0.1f } } },
                { "Drum Crunch", { { "type", 3 }, { "drive", 10 }, { "tone", 0.2f }, { "mix", 0.6f }, { "output", -4 } } },
                { "Bass Grit", { { "type", 2 }, { "drive", 12 }, { "tone", -0.3f }, { "mix", 0.5f } } },
                { "Sine Shaper", { { "type", 5 }, { "drive", 14 }, { "mix", 0.6f }, { "output", -5 } } },
                { "Asymmetric Fuzz", { { "type", 0 }, { "drive", 24 }, { "bias", 0.4f }, { "output", -10 } } },
                { "8-bit", { { "type", 6 }, { "drive", 20 }, { "mix", 1.0f }, { "output", -6 } } },
                { "Tape Glue (Bus)", { { "type", 1 }, { "drive", 4 }, { "tone", -0.15f }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            { "pulseUtility", {
                { "Phase Flip", { { "invert", 3 } } },
                { "Swap L/R", { { "channels", 3 } } },
                { "Left Only", { { "channels", 1 } } },
                { "Narrow 50%", { { "width", 0.5f } } },
                { "-6 dB Trim", { { "gain", -6 } } },
                { "Club Ready (mono lows, DC)", { { "bassmono", 1 }, { "bassfreq", 150 }, { "dc", 1 } } } } },

            //----------------------------------------------------------------------------------
            { "pulseVocoder", {
                { "Daft Robot", { { "carrier", 1 }, { "pitch", 36 }, { "noise", 0.05f }, { "attack", 2 }, { "release", 30 } } },
                { "High Formant", { { "carrier", 1 }, { "pitch", 55 }, { "formant", 7 }, { "noise", 0.15f } } },
                { "Deep Formant", { { "carrier", 1 }, { "pitch", 40 }, { "formant", -7 }, { "noise", 0.1f } } },
                { "Rhythmic Noise", { { "carrier", 2 }, { "noise", 1 }, { "attack", 1 }, { "release", 15 } } },
                { "Slow Pad Voice", { { "carrier", 0 }, { "attack", 30 }, { "release", 250 }, { "noise", 0.1f } } } } },

            //----------------------------------------------------------------------------------
            // root 0 = C; scale index into the pitch-correction scale list
            { "pulsePitchCorrect", {
                { "Gentle", { { "speed", 200 }, { "amount", 0.6f } } },
                { "Medium", { { "speed", 60 }, { "amount", 0.9f } } },
                { "Robotic (chromatic)", { { "speed", 0 }, { "amount", 1 }, { "scale", 0 } } },
                { "Parallel Tune", { { "speed", 20 }, { "amount", 1 }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            { "pulseLimiter", {
                { "Transparent", { { "gain", 2 }, { "ceiling", -1 }, { "auto", 1 } } },
                { "Streaming -1 dBTP", { { "gain", 4 }, { "ceiling", -1 }, { "auto", 1 } } },
                { "Brickwall Club", { { "gain", 10 }, { "ceiling", -0.1f }, { "auto", 0 }, { "release", 40 } } },
                { "Drum Bus Peak Catch", { { "gain", 3 }, { "ceiling", -3 }, { "auto", 0 }, { "release", 60 } } } } },

            //----------------------------------------------------------------------------------
            // MIDI effects
            // Arp rate 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32; mode 0 up, 1 down, 2 up/down, 3 as played, 4 random, 5 chord
            { "pulseArp", {
                { "Classic Up 1/16", { { "rate", 3 }, { "mode", 0 }, { "octaves", 1 }, { "gate", 0.6f } } },
                { "Trance 2 Octaves", { { "rate", 3 }, { "mode", 2 }, { "octaves", 2 }, { "gate", 0.5f } } },
                { "Down 1/8", { { "rate", 1 }, { "mode", 1 }, { "octaves", 1 }, { "gate", 0.7f } } },
                { "Random Sparkle", { { "rate", 5 }, { "mode", 4 }, { "octaves", 3 }, { "gate", 0.3f } } },
                { "Triplet Roll", { { "rate", 4 }, { "mode", 0 }, { "octaves", 2 }, { "gate", 0.5f } } },
                { "Chord Stabs 1/8", { { "rate", 1 }, { "mode", 5 }, { "octaves", 1 }, { "gate", 0.25f } } },
                { "Staccato Fixed Velocity", { { "rate", 3 }, { "mode", 3 }, { "gate", 0.15f }, { "velocity", 100 } } } } },
            { "pulseChord", {
                { "Major Triad", { { "shift1", 4 }, { "shift2", 7 }, { "shift3", 0 }, { "shift4", 0 } } },
                { "Minor Triad", { { "shift1", 3 }, { "shift2", 7 }, { "shift3", 0 }, { "shift4", 0 } } },
                { "Major 7", { { "shift1", 4 }, { "shift2", 7 }, { "shift3", 11 }, { "shift4", 0 } } },
                { "Minor 7", { { "shift1", 3 }, { "shift2", 7 }, { "shift3", 10 }, { "shift4", 0 } } },
                { "Power Chord", { { "shift1", 7 }, { "shift2", 12 }, { "shift3", 0 }, { "shift4", 0 } } },
                { "Octaves", { { "shift1", 12 }, { "shift2", -12 }, { "shift3", 0 }, { "shift4", 0 } } },
                { "Sus 4", { { "shift1", 5 }, { "shift2", 7 }, { "shift3", 0 }, { "shift4", 0 } } },
                { "Minor 9", { { "shift1", 3 }, { "shift2", 7 }, { "shift3", 10 }, { "shift4", 14 } } } } },
            // Scale: scale index into Scales::all() (0 major, 1 minor, 2 dorian, ... 8 minor pentatonic, 10 blues)
            { "pulseScale", {
                { "C Major", { { "root", 0 }, { "scale", 0 } } },
                { "A Minor", { { "root", 9 }, { "scale", 1 } } },
                { "D Dorian", { { "root", 2 }, { "scale", 2 } } },
                { "E Phrygian", { { "root", 4 }, { "scale", 3 } } },
                { "A Minor Pentatonic", { { "root", 9 }, { "scale", 8 } } },
                { "E Blues", { { "root", 4 }, { "scale", 10 } } },
                { "F Lydian", { { "root", 5 }, { "scale", 4 } } } } },
            // Note Echo time uses the arp rate list
            { "pulseNoteEcho", {
                { "1/8 Echoes", { { "time", 1 }, { "repeats", 3 }, { "feedback", 0.6f } } },
                { "1/16 Stutter", { { "time", 3 }, { "repeats", 6 }, { "feedback", 0.75f } } },
                { "Rising Octave", { { "time", 1 }, { "repeats", 3 }, { "feedback", 0.7f }, { "pitch", 12 } } },
                { "Falling Fifths", { { "time", 0 }, { "repeats", 4 }, { "feedback", 0.65f }, { "pitch", -7 } } },
                { "Triplet Trail", { { "time", 2 }, { "repeats", 5 }, { "feedback", 0.55f } } } } },
        };
        return m;
    }
}
