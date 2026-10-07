#pragma once

#include <map>
#include <vector>

/** More factory presets (merged into FactoryPresets::all()). Values are in real units, by
    parameter id; anything not listed stays at the device's default. */
#if JUCE_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4244)   // whole numbers in the tables become floats
#endif

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
            // Wavetable (v0.47): sets 0 basic, 1 pulse width, 2 harmonics, 3 vowels, 4 sync saw; warp 1 bend+, 2 bend-, 3 sync,
            // 4 mirror, 5 quantize, 6 FM from osc 2; f2type 1 LP12, 2 LP24, 3 BP, 4 HP, 5 notch; routing 0 serial, 1 parallel, 2 split;
            // LFO shape 0 sine .. 5 random, 6 drawn; sync 1 1/16 .. 7 4 bars; routes "mod:<source>:<param id>"
            // v0.55: sets 6 resonant saw, 7 organ, 8 FM bell, 9 FM bright, 10 wavefold, 11 phase reso, 12 digital steps, 13 hollow to full,
            // 14 comb sweep, 15 growl, 16 glass; sources 14..17 = macros 1..4; drivetype 0 soft, 1 hard, 2 fold, 3 crush
            // v0.99.15: the third oscillator (set3, pos3, oct3, semi3, fine3, level3, pan3, warp3, warpamt3); it is silent unless level3 is set
                { "Three-Osc Supersaw Stack", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 0 }, { "pos2", 0.66f }, { "fine2", 9 }, { "mix", 0.5f }, { "set3", 0 }, { "pos3", 0.66f }, { "oct3", 1 },
                                                { "fine3", -7 }, { "level3", 0.7f }, { "pan1", -0.4f }, { "pan2", 0.4f }, { "voices", 5 }, { "detune", 0.3f }, { "ftype", 1 }, { "cutoff", 5000 },
                                                { "fenv", 0.25f }, { "a", 5 }, { "s", 0.85f }, { "r", 350 }, { "reverbmix", 0.2f }, { "level", -14 } } },
                { "Fifth Organ Pad (osc 3 = the fifth)", { { "set1", 7 }, { "pos1", 0.5f }, { "set2", 13 }, { "pos2", 0.4f }, { "mix", 0.4f }, { "set3", 2 }, { "pos3", 0.3f }, { "semi3", 7 }, { "level3", 0.5f },
                                                           { "voices", 3 }, { "detune", 0.15f }, { "ftype", 0 }, { "cutoff", 3500 }, { "fenv", 0.1f }, { "a", 400 }, { "s", 1 }, { "r", 900 },
                                                           { "mod:1:pos3", 0.3f }, { "chorusmix", 0.3f }, { "reverbmix", 0.4f }, { "level", -12 } } },
                { "Glass Over Sub Bass", { { "set1", 0 }, { "pos1", 0.0f }, { "oct1", -1 }, { "mix", 0 }, { "set3", 16 }, { "pos3", 0.5f }, { "oct3", 2 }, { "level3", 0.35f }, { "pan3", 0.2f },
                                           { "warp3", 1 }, { "warpamt3", 0.3f }, { "mod:5:pos3", 0.5f }, { "voices", 1 }, { "ftype", 0 }, { "cutoff", 8000 }, { "fenv", 0 }, { "a", 2 }, { "d", 600 },
                                           { "s", 0.6f }, { "r", 300 }, { "md", 500 }, { "ms", 0 }, { "level", -10 } } },
                { "Acid Reso (macro 1 = squelch)", { { "set1", 6 }, { "pos1", 0.25f }, { "mix", 0 }, { "voices", 1 }, { "mode", 1 }, { "glide", 60 }, { "ftype", 1 }, { "cutoff", 500 },
                                                     { "reso", 0.65f }, { "fenv", 0.55f }, { "fd", 220 }, { "fs", 0.05f }, { "fdrive", 0.5f }, { "mod:14:pos1", 0.6f }, { "mod:14:cutoff", 0.3f },
                                                     { "drive", 0.35f }, { "drivetype", 1 }, { "s", 0.8f }, { "r", 90 }, { "level", -13 }, { "output", -8 } } },
                { "Cathedral Organ", { { "set1", 7 }, { "pos1", 0.8f }, { "set2", 7 }, { "pos2", 0.35f }, { "oct2", 1 }, { "mix", 0.4f }, { "voices", 3 }, { "detune", 0.08f }, { "sub", 0.35f },
                                       { "ftype", 0 }, { "cutoff", 9000 }, { "fenv", 0 }, { "a", 20 }, { "s", 1 }, { "r", 400 }, { "chorusmix", 0.3f }, { "reverbmix", 0.45f }, { "reverbsize", 0.85f }, { "level", -10 } } },
                { "Crystal Bells", { { "set1", 8 }, { "pos1", 0.45f }, { "set2", 16 }, { "pos2", 0.6f }, { "oct2", 1 }, { "mix", 0.35f }, { "voices", 2 }, { "detune", 0.1f }, { "mod:5:pos1", -0.35f },
                                   { "md", 900 }, { "ms", 0 }, { "ftype", 0 }, { "cutoff", 12000 }, { "a", 1 }, { "d", 1800 }, { "s", 0 }, { "r", 1500 }, { "delaymix", 0.2f }, { "reverbmix", 0.4f }, { "level", -10 } } },
                { "FM Tines (keys)", { { "set1", 9 }, { "pos1", 0.18f }, { "mix", 0 }, { "voices", 1 }, { "mod:8:pos1", 0.35f }, { "mod:5:pos1", 0.25f }, { "md", 500 }, { "ms", 0 }, { "ftype", 0 },
                                       { "cutoff", 8000 }, { "keytrack", 0.5f }, { "a", 1 }, { "d", 2200 }, { "s", 0.15f }, { "r", 500 }, { "chorusmix", 0.35f }, { "phasermix", 0.25f }, { "phaserrate", 0.4f }, { "level", -9 } } },
                { "Wavefold Lead (macro 1 = fold)", { { "set1", 10 }, { "pos1", 0.2f }, { "mix", 0 }, { "voices", 3 }, { "detune", 0.15f }, { "mode", 1 }, { "glide", 45 }, { "mod:14:pos1", 0.7f },
                                                      { "mod:9:pos1", 0.3f }, { "ftype", 1 }, { "cutoff", 6000 }, { "fenv", 0.15f }, { "delaymix", 0.22f }, { "reverbmix", 0.2f }, { "s", 0.9f }, { "r", 180 }, { "level", -10 } } },
                { "CZ Reso Bass", { { "set1", 11 }, { "pos1", 0.5f }, { "mix", 0 }, { "voices", 1 }, { "sub", 0.55f }, { "subshape", 1 }, { "mod:6:pos1", 0.4f }, { "fd", 260 }, { "fs", 0.1f },
                                    { "ftype", 1 }, { "cutoff", 2500 }, { "fenv", 0.3f }, { "s", 0.85f }, { "r", 90 }, { "level", -9 } } },
                { "8-bit Arcade", { { "set1", 12 }, { "pos1", 0.75f }, { "set2", 1 }, { "pos2", 0.2f }, { "oct2", 1 }, { "mix", 0.3f }, { "voices", 1 }, { "ftype", 0 }, { "cutoff", 14000 }, { "fenv", 0 },
                                    { "drive", 0.35f }, { "drivetype", 3 }, { "l2rate", 6 }, { "mod:2:fine1", 0.15f }, { "a", 1 }, { "s", 0.9f }, { "r", 60 }, { "level", -11 } } },
                { "Hollow Pad", { { "set1", 13 }, { "pos1", 0.1f }, { "set2", 13 }, { "pos2", 0.7f }, { "fine2", 9 }, { "pan1", -0.4f }, { "pan2", 0.4f }, { "mix", 0.5f }, { "voices", 5 }, { "detune", 0.3f },
                                  { "spread", 1 }, { "l2rate", 0.12f }, { "mod:2:pos1", 0.5f }, { "l3rate", 0.09f }, { "mod:3:pos2", -0.4f }, { "ftype", 0 }, { "cutoff", 2600 }, { "a", 700 }, { "s", 1 },
                                  { "r", 1600 }, { "chorusmix", 0.4f }, { "reverbmix", 0.5f }, { "reverbsize", 0.85f }, { "level", -11 } } },
                { "Comb Sweep Pad", { { "set1", 14 }, { "pos1", 0.3f }, { "set2", 6 }, { "pos2", 0.15f }, { "mix", 0.3f }, { "voices", 4 }, { "detune", 0.25f }, { "l2sync", 6 }, { "mod:2:pos1", 0.6f },
                                      { "phasermix", 0.5f }, { "phaserrate", 0.15f }, { "ftype", 0 }, { "cutoff", 5000 }, { "a", 400 }, { "s", 1 }, { "r", 1200 }, { "reverbmix", 0.4f }, { "level", -11 } } },
                { "Talking Growl (macro 1 = vowel)", { { "set1", 15 }, { "pos1", 0.2f }, { "set2", 4 }, { "pos2", 0.3f }, { "oct2", -1 }, { "mix", 0.3f }, { "voices", 2 }, { "detune", 0.2f }, { "sub", 0.5f },
                                                       { "mod:14:pos1", 0.75f }, { "l2sync", 3 }, { "mod:2:pos1", 0.35f }, { "ftype", 1 }, { "cutoff", 3000 }, { "fdrive", 0.6f }, { "drive", 0.45f },
                                                       { "drivetype", 2 }, { "s", 1 }, { "r", 90 }, { "level", -15 }, { "output", -10 } } },
                { "Glass Choir", { { "set1", 16 }, { "pos1", 0.35f }, { "set2", 3 }, { "pos2", 0.4f }, { "mix", 0.45f }, { "voices", 5 }, { "detune", 0.22f }, { "noise", 0.12f }, { "noisecolour", 0.6f },
                                   { "l2rate", 0.2f }, { "mod:2:pos2", 0.4f }, { "ftype", 0 }, { "cutoff", 4500 }, { "a", 500 }, { "s", 1 }, { "r", 1400 }, { "reverbmix", 0.55f }, { "reverbsize", 0.9f }, { "level", -11 } } },
                { "Noise Riser (macro 1 = rise)", { { "level1", 0.3f }, { "set1", 6 }, { "pos1", 0.5f }, { "mix", 0 }, { "noise", 1 }, { "noisecolour", 0.3f }, { "ftype", 2 }, { "cutoff", 400 }, { "reso", 0.55f },
                                                    { "fenv", 0 }, { "mod:14:cutoff", 0.6f }, { "mod:14:semi1", 0.5f }, { "phasermix", 0.4f }, { "a", 200 }, { "s", 1 }, { "r", 600 }, { "reverbmix", 0.4f }, { "level", -12 } } },
                { "Wide Supersaw (macros)", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 0 }, { "pos2", 0.66f }, { "oct2", 1 }, { "pan1", -0.3f }, { "pan2", 0.3f }, { "mix", 0.4f }, { "voices", 7 },
                                              { "detune", 0.3f }, { "spread", 1 }, { "mod:14:cutoff", 0.5f }, { "mod:15:detune", 0.5f }, { "mod:16:reverbmix", 0.6f }, { "mod:17:delaymix", 0.5f },
                                              { "ftype", 1 }, { "cutoff", 2500 }, { "fenv", 0.2f }, { "a", 5 }, { "s", 0.9f }, { "r", 300 }, { "eqhigh", 3 }, { "level", -12 } } },
                { "Sub Square Bass", { { "set1", 0 }, { "pos1", 0.66f }, { "level1", 0.6f }, { "mix", 0 }, { "voices", 1 }, { "sub", 0.9f }, { "subshape", 3 }, { "suboct", 0 }, { "ftype", 1 }, { "cutoff", 600 },
                                       { "fenv", 0.4f }, { "fd", 180 }, { "fs", 0.1f }, { "fdrive", 0.4f }, { "keytrack", 0.4f }, { "mode", 1 }, { "eqlow", 3 }, { "s", 0.9f }, { "r", 70 }, { "level", -8 } } },
                { "Phaser Strings", { { "set1", 6 }, { "pos1", 0.3f }, { "set2", 0 }, { "pos2", 0.66f }, { "fine2", -8 }, { "mix", 0.5f }, { "voices", 5 }, { "detune", 0.25f }, { "ftype", 0 }, { "cutoff", 4200 },
                                      { "fenv", 0.1f }, { "a", 250 }, { "s", 1 }, { "r", 700 }, { "phasermix", 0.6f }, { "phaserrate", 0.2f }, { "chorusmix", 0.3f }, { "reverbmix", 0.35f }, { "eqlow", -3 }, { "level", -11 } } },
                { "Growl Bass (LFO warp)", { { "set1", 0 }, { "pos1", 0.66f }, { "warp1", 3 }, { "warpamt1", 0.3f }, { "mix", 0.3f }, { "voices", 2 }, { "sub", 0.5f },
                                              { "ftype", 1 }, { "cutoff", 700 }, { "reso", 0.4f }, { "fenv", 0.2f }, { "l2sync", 2 }, { "l2shape", 0 },
                                              { "mod:2:warpamt1", 0.5f }, { "mod:2:cutoff", 0.35f }, { "drive", 0.4f }, { "s", 1 }, { "r", 80 }, { "level", -9 }, { "output", -6 } } },
                { "Neuro Reese", { { "set1", 4 }, { "pos1", 0.4f }, { "set2", 0 }, { "pos2", 0.66f }, { "fine2", -15 }, { "voices", 4 }, { "detune", 0.35f }, { "sub", 0.4f },
                                   { "ftype", 1 }, { "cutoff", 900 }, { "f2type", 5 }, { "f2cutoff", 1200 }, { "l2sync", 3 }, { "mod:2:f2cutoff", 0.4f },
                                   { "mod:3:pos1", 0.3f }, { "l3rate", 0.3f }, { "drive", 0.5f }, { "s", 1 }, { "r", 100 }, { "level", -10 }, { "output", -6 } } },
                { "FM Pluck Bell", { { "set1", 0 }, { "pos1", 0.0f }, { "set2", 0 }, { "pos2", 0.0f }, { "semi2", 19 }, { "warp1", 6 }, { "warpamt1", 0.5f }, { "mix", 0 },
                                     { "voices", 1 }, { "mod:5:warpamt1", 0.4f }, { "md", 300 }, { "ms", 0 }, { "a", 1 }, { "d", 900 }, { "s", 0 }, { "r", 700 },
                                     { "reverbmix", 0.25f }, { "level", -10 } } },
                { "Dreamy Pad", { { "set1", 3 }, { "pos1", 0.3f }, { "set2", 2 }, { "pos2", 0.5f }, { "voices", 6 }, { "detune", 0.35f }, { "ftype", 0 }, { "cutoff", 2200 },
                                  { "a", 1200 }, { "s", 0.9f }, { "r", 3000 }, { "l2rate", 0.1f }, { "mod:2:pos1", 0.3f }, { "mod:3:cutoff", 0.15f }, { "l3rate", 0.07f },
                                  { "chorusmix", 0.5f }, { "reverbmix", 0.5f }, { "reverbsize", 0.9f }, { "level", -13 } } },
                { "Supersaw Anthem", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 0 }, { "pos2", 0.66f }, { "semi2", 12 }, { "voices", 7 }, { "detune", 0.55f }, { "mix", 0.4f },
                                       { "ftype", 0 }, { "cutoff", 7000 }, { "a", 5 }, { "s", 0.9f }, { "r", 400 }, { "chorusmix", 0.3f }, { "delaymix", 0.2f }, { "delaytime", 2 },
                                       { "reverbmix", 0.3f }, { "level", -11 } } },
                { "Wobble 1/8 (synced)", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 4 }, { "pos2", 0.6f }, { "mix", 0.4f }, { "sub", 0.5f }, { "ftype", 1 }, { "cutoff", 400 },
                                           { "reso", 0.5f }, { "fenv", 0 }, { "l2sync", 2 }, { "l2shape", 1 }, { "mod:2:cutoff", 0.55f }, { "mod:2:pos2", 0.3f },
                                           { "drive", 0.35f }, { "s", 1 }, { "r", 80 }, { "level", -9 }, { "output", -6 } } },
                { "Stutter Gate (drawn LFO)", { { "set1", 0 }, { "pos1", 0.66f }, { "voices", 5 }, { "detune", 0.3f }, { "ftype", 0 }, { "cutoff", 5000 }, { "l2sync", 1 },
                                                { "l2shape", 6 }, { "mod:2:level", -0.5f }, { "s", 1 }, { "r", 100 }, { "level", -14 }, { "output", -5 } } },
                { "Vowel Talker", { { "set1", 3 }, { "pos1", 0.0f }, { "voices", 2 }, { "l2sync", 3 }, { "l2shape", 1 }, { "mod:2:pos1", 0.9f }, { "ftype", 2 }, { "cutoff", 1200 },
                                    { "reso", 0.3f }, { "fenv", 0 }, { "s", 1 }, { "r", 120 }, { "level", -9 } } },
                { "Glassy Keys", { { "set1", 2 }, { "pos1", 0.8f }, { "set2", 0 }, { "pos2", 0.0f }, { "semi2", 12 }, { "warp1", 4 }, { "warpamt1", 0.3f }, { "mix", 0.3f },
                                   { "voices", 2 }, { "ftype", 0 }, { "cutoff", 4000 }, { "fenv", 0.3f }, { "fd", 600 }, { "a", 2 }, { "d", 1300 }, { "s", 0.3f }, { "r", 600 },
                                   { "mod:8:cutoff", 0.3f }, { "reverbmix", 0.2f }, { "level", -10 } } },
                { "Acid Squelch 303", { { "set1", 1 }, { "pos1", 0.35f }, { "mix", 0 }, { "voices", 1 }, { "ftype", 1 }, { "cutoff", 250 }, { "reso", 0.85f }, { "fenv", 0.75f },
                                        { "fa", 1 }, { "fd", 200 }, { "fs", 0 }, { "a", 1 }, { "d", 250 }, { "s", 0.7f }, { "r", 60 }, { "glide", 70 }, { "mod:8:cutoff", 0.25f },
                                        { "drive", 0.45f }, { "delaymix", 0.15f }, { "delaytime", 2 }, { "level", -17 }, { "output", -5 } } },
                { "Hyper Pluck", { { "set1", 0 }, { "pos1", 0.66f }, { "voices", 5 }, { "detune", 0.3f }, { "ftype", 1 }, { "cutoff", 600 }, { "reso", 0.3f }, { "fenv", 0.85f },
                                   { "fd", 160 }, { "fs", 0 }, { "a", 0.5f }, { "d", 300 }, { "s", 0 }, { "r", 250 }, { "delaymix", 0.25f }, { "delaytime", 1 }, { "delayfb", 0.4f },
                                   { "reverbmix", 0.2f }, { "level", -10 } } },
                { "Dark Cinematic Drone", { { "set1", 3 }, { "pos1", 0.1f }, { "set2", 2 }, { "pos2", 0.2f }, { "semi2", -12 }, { "voices", 5 }, { "detune", 0.4f }, { "ftype", 1 },
                                            { "cutoff", 600 }, { "reso", 0.35f }, { "a", 3000 }, { "s", 1 }, { "r", 5000 }, { "l2rate", 0.05f }, { "l3rate", 0.11f },
                                            { "mod:2:cutoff", 0.25f }, { "mod:3:pos1", 0.4f }, { "mod:3:pos2", -0.3f }, { "reverbmix", 0.6f }, { "reverbsize", 1.0f }, { "level", -8 } } },
                { "Trance Gate Pad (drawn)", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 2 }, { "pos2", 0.6f }, { "voices", 7 }, { "detune", 0.5f }, { "ftype", 0 }, { "cutoff", 4000 },
                                               { "a", 10 }, { "s", 1 }, { "r", 300 }, { "l2sync", 1 }, { "l2shape", 6 }, { "mod:2:level", -0.6f }, { "delaymix", 0.2f },
                                               { "reverbmix", 0.3f }, { "level", -16 }, { "output", -5 } } },
                { "Sync Lead Sweep", { { "set1", 0 }, { "pos1", 0.66f }, { "warp1", 3 }, { "warpamt1", 0.2f }, { "mix", 0 }, { "voices", 3 }, { "detune", 0.2f }, { "ftype", 0 },
                                       { "cutoff", 6000 }, { "mod:5:warpamt1", 0.6f }, { "md", 700 }, { "ms", 0.2f }, { "glide", 40 }, { "delaymix", 0.2f }, { "level", -10 } } },
                { "Mod Wheel Filter Lead", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 1 }, { "pos2", 0.5f }, { "fine2", 10 }, { "voices", 3 }, { "ftype", 1 }, { "cutoff", 900 },
                                             { "reso", 0.45f }, { "fenv", 0.2f }, { "mod:9:cutoff", 0.6f }, { "mod:9:reso", 0.3f }, { "glide", 50 }, { "level", -10 } } },
                { "Bit Quantize Lead", { { "set1", 0 }, { "pos1", 0.33f }, { "warp1", 5 }, { "warpamt1", 0.7f }, { "mix", 0 }, { "voices", 1 }, { "ftype", 0 }, { "cutoff", 9000 },
                                         { "mod:2:warpamt1", 0.25f }, { "l2rate", 2 }, { "a", 1 }, { "s", 0.8f }, { "r", 120 }, { "level", -12 } } },
                { "Mirror Bass", { { "set1", 0 }, { "pos1", 0.4f }, { "warp1", 4 }, { "warpamt1", 0.6f }, { "mix", 0 }, { "voices", 1 }, { "sub", 0.6f }, { "ftype", 1 },
                                   { "cutoff", 900 }, { "fenv", 0.4f }, { "fd", 300 }, { "fs", 0.2f }, { "s", 1 }, { "r", 70 }, { "level", -8 }, { "output", -4 } } },
                { "Parallel Filter Pad", { { "set1", 2 }, { "pos1", 0.5f }, { "set2", 3 }, { "pos2", 0.5f }, { "voices", 5 }, { "detune", 0.3f }, { "ftype", 0 }, { "cutoff", 800 },
                                           { "f2type", 4 }, { "f2cutoff", 3000 }, { "routing", 1 }, { "a", 700 }, { "s", 0.9f }, { "r", 2000 }, { "mod:2:cutoff", 0.3f },
                                           { "mod:3:f2cutoff", -0.3f }, { "l2rate", 0.2f }, { "l3rate", 0.13f }, { "chorusmix", 0.4f }, { "reverbmix", 0.4f }, { "level", -13 } } },
                { "Split Filter Duo", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 1 }, { "pos2", 0.3f }, { "semi2", 12 }, { "mix", 0.5f }, { "voices", 3 }, { "ftype", 1 },
                                        { "cutoff", 1200 }, { "f2type", 3 }, { "f2cutoff", 2500 }, { "f2reso", 0.5f }, { "routing", 2 }, { "l2sync", 3 },
                                        { "mod:2:f2cutoff", 0.4f }, { "s", 0.8f }, { "r", 300 }, { "level", -11 } } },
                { "Random Sample & Hold Bleeps", { { "set1", 0 }, { "pos1", 0.33f }, { "voices", 1 }, { "ftype", 2 }, { "cutoff", 1500 }, { "reso", 0.7f }, { "l2sync", 1 },
                                                   { "l2shape", 5 }, { "mod:2:cutoff", 0.5f }, { "a", 1 }, { "s", 1 }, { "r", 150 }, { "delaymix", 0.3f },
                                                   { "delaytime", 1 }, { "level", -11 } } },
                { "Breathy Flute", { { "set1", 2 }, { "pos1", 0.15f }, { "voices", 1 }, { "sub", 0.1f }, { "ftype", 0 }, { "cutoff", 2500 }, { "a", 80 }, { "s", 0.9f }, { "r", 250 },
                                     { "l2rate", 5 }, { "mod:2:fine2", 0.05f }, { "mod:2:level", 0.06f }, { "mod:12:cutoff", 0.08f }, { "reverbmix", 0.25f }, { "level", -10 } } },
                { "Velocity Keys", { { "set1", 0 }, { "pos1", 0.2f }, { "set2", 2 }, { "pos2", 0.4f }, { "voices", 2 }, { "ftype", 0 }, { "cutoff", 1200 }, { "fenv", 0.2f },
                                     { "a", 2 }, { "d", 1200 }, { "s", 0.4f }, { "r", 500 }, { "mod:8:cutoff", 0.45f }, { "mod:8:pos1", 0.3f }, { "level", -10 } } },
                { "Key-Tracked Strings", { { "set1", 0 }, { "pos1", 0.66f }, { "voices", 6 }, { "detune", 0.25f }, { "ftype", 0 }, { "cutoff", 1500 }, { "mod:11:cutoff", 0.35f },
                                           { "a", 400 }, { "s", 0.9f }, { "r", 900 }, { "l2rate", 5 }, { "mod:2:fine2", 0.03f }, { "chorusmix", 0.4f }, { "reverbmix", 0.35f },
                                           { "level", -12 } } },
                { "Pressure Swell (MPE)", { { "set1", 3 }, { "pos1", 0.3f }, { "voices", 4 }, { "ftype", 1 }, { "cutoff", 700 }, { "mod:10:cutoff", 0.5f }, { "mod:10:pos1", 0.4f },
                                            { "a", 300 }, { "s", 1 }, { "r", 800 }, { "reverbmix", 0.3f }, { "level", -11 } } },
                { "Laser Zap", { { "set1", 0 }, { "pos1", 0.66f }, { "voices", 1 }, { "mix", 0 }, { "mod:5:fine2", 0.0f }, { "warp1", 1 }, { "warpamt1", 0.8f },
                                 { "mod:5:warpamt1", -0.8f }, { "md", 120 }, { "ms", 0 }, { "a", 0.5f }, { "d", 200 }, { "s", 0 }, { "r", 100 }, { "level", -12 } } },
                { "Sub Drop 808", { { "set1", 0 }, { "pos1", 0.0f }, { "mix", 0 }, { "voices", 1 }, { "sub", 0.2f }, { "ftype", 0 }, { "cutoff", 600 }, { "glide", 120 },
                                    { "a", 1 }, { "d", 1800 }, { "s", 0.2f }, { "r", 400 }, { "drive", 0.3f }, { "level", -6 }, { "output", -6 } } },
                { "Chorus Electric Piano", { { "set1", 2 }, { "pos1", 0.25f }, { "set2", 0 }, { "pos2", 0.0f }, { "semi2", 12 }, { "warp1", 6 }, { "warpamt1", 0.15f }, { "mix", 0.25f },
                                             { "voices", 1 }, { "ftype", 0 }, { "cutoff", 3000 }, { "a", 2 }, { "d", 1600 }, { "s", 0.3f }, { "r", 500 },
                                             { "chorusmix", 0.6f }, { "chorusrate", 0.7f }, { "level", -9 } } },
                { "Rhythmic Arp Synth", { { "set1", 1 }, { "pos1", 0.4f }, { "voices", 3 }, { "detune", 0.2f }, { "ftype", 1 }, { "cutoff", 900 }, { "reso", 0.35f }, { "fenv", 0.6f },
                                          { "fd", 180 }, { "fs", 0 }, { "a", 1 }, { "d", 250 }, { "s", 0 }, { "r", 200 }, { "l2sync", 5 }, { "mod:2:cutoff", 0.3f },
                                          { "delaymix", 0.3f }, { "delaytime", 2 }, { "delayfb", 0.45f }, { "level", -10 } } },
                { "Tape Warble Keys", { { "set1", 0 }, { "pos1", 0.33f }, { "voices", 2 }, { "ftype", 0 }, { "cutoff", 2400 }, { "l2rate", 0.6f }, { "mod:2:fine2", 0.08f },
                                        { "l3shape", 5 }, { "l3rate", 3 }, { "mod:3:level", 0.05f }, { "a", 3 }, { "d", 900 }, { "s", 0.5f }, { "r", 400 },
                                        { "drive", 0.2f }, { "chorusmix", 0.3f }, { "level", -10 }, { "output", -5 } } },
                { "Huge Hoover", { { "set1", 4 }, { "pos1", 0.6f }, { "set2", 1 }, { "pos2", 0.2f }, { "semi2", -12 }, { "voices", 7 }, { "detune", 0.65f }, { "ftype", 1 },
                                   { "cutoff", 2500 }, { "glide", 150 }, { "mod:5:pos1", 0.4f }, { "md", 800 }, { "drive", 0.4f }, { "chorusmix", 0.4f }, { "level", -15 }, { "output", -5 } } },
                { "Space Bells", { { "set1", 2 }, { "pos1", 0.95f }, { "set2", 2 }, { "pos2", 0.8f }, { "semi2", 24 }, { "mix", 0.3f }, { "voices", 2 }, { "ftype", 3 }, { "cutoff", 400 },
                                   { "a", 1 }, { "d", 2000 }, { "s", 0 }, { "r", 2500 }, { "delaymix", 0.35f }, { "delaytime", 4 }, { "delayfb", 0.55f },
                                   { "reverbmix", 0.5f }, { "reverbsize", 0.9f }, { "level", -12 } } },
                { "Distorted Lead", { { "set1", 0 }, { "pos1", 0.66f }, { "set2", 1 }, { "pos2", 0.5f }, { "voices", 3 }, { "detune", 0.2f }, { "ftype", 0 }, { "cutoff", 4500 },
                                      { "drive", 0.8f }, { "glide", 40 }, { "l2rate", 5.5f }, { "mod:2:fine2", 0.04f }, { "delaymix", 0.2f }, { "level", -18 }, { "output", -5 } } },
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
                               { "op3s", 0 }, { "feedback", 0.3f }, { "volume", -14 } } },
                { "Tubular Bell", { { "algorithm", 5 }, { "op0ratio", 1 }, { "op0d", 5000 }, { "op0s", 0 }, { "op0r", 4000 }, { "op1ratio", 3.5f }, { "op1level", 0.5f },
                                    { "op1d", 3000 }, { "op1s", 0 }, { "op2ratio", 2 }, { "op2level", 0.8f }, { "op2d", 4500 }, { "op2s", 0 }, { "op3ratio", 7 },
                                    { "op3level", 0.35f }, { "op3d", 1500 }, { "op3s", 0 }, { "volume", -14 } } },
                { "Glass Pad", { { "algorithm", 6 }, { "op0ratio", 1 }, { "op0a", 900 }, { "op0s", 1 }, { "op0r", 2500 }, { "op1ratio", 4 }, { "op1level", 0.2f }, { "op1a", 1500 },
                                 { "op1s", 0.8f }, { "op1r", 2500 }, { "op2ratio", 2 }, { "op2level", 0.6f }, { "op2a", 1200 }, { "op2s", 1 }, { "op2r", 2500 }, { "op3ratio", 6 },
                                 { "op3level", 0.15f }, { "op3a", 2000 }, { "op3s", 1 }, { "op3r", 2500 }, { "volume", -14 } } },
                { "Clav", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0d", 600 }, { "op0s", 0 }, { "op0r", 60 }, { "op1ratio", 3 }, { "op1level", 0.55f }, { "op1d", 250 },
                            { "op1s", 0 }, { "op2ratio", 1 }, { "op2level", 0.3f }, { "op2d", 120 }, { "op2s", 0 }, { "op3ratio", 5 }, { "op3level", 0.2f }, { "op3d", 40 },
                            { "op3s", 0 }, { "volume", -14 } } },
                { "Marimba", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0d", 700 }, { "op0s", 0 }, { "op0r", 300 }, { "op1ratio", 10 }, { "op1level", 0.3f }, { "op1d", 40 },
                               { "op1s", 0 }, { "op2ratio", 4 }, { "op2level", 0.25f }, { "op2d", 400 }, { "op2s", 0 }, { "op3ratio", 1 }, { "op3level", 0.2f }, { "op3d", 30 },
                               { "op3s", 0 }, { "volume", -8 } } },
                { "Organ", { { "algorithm", 7 }, { "op0ratio", 1 }, { "op0a", 3 }, { "op0s", 1 }, { "op0r", 60 }, { "op1ratio", 2 }, { "op1level", 0.6f }, { "op1a", 3 },
                             { "op1s", 1 }, { "op1r", 60 }, { "op2ratio", 3 }, { "op2level", 0.4f }, { "op2a", 3 }, { "op2s", 1 }, { "op2r", 60 }, { "op3ratio", 4 },
                             { "op3level", 0.25f }, { "op3a", 3 }, { "op3s", 1 }, { "op3r", 60 }, { "volume", -12 } } },
                { "Growl Lead", { { "algorithm", 0 }, { "op0ratio", 1 }, { "op0s", 0.9f }, { "op0r", 150 }, { "op1ratio", 1 }, { "op1level", 0.8f }, { "op1s", 0.7f },
                                  { "op2ratio", 2 }, { "op2level", 0.4f }, { "op3ratio", 1 }, { "op3level", 0.3f }, { "feedback", 0.7f }, { "cutoff", 5000 }, { "reso", 0.3f },
                                  { "glide", 60 }, { "volume", -14 } } },
                { "Sitar-ish", { { "algorithm", 1 }, { "op0ratio", 1 }, { "op0d", 2500 }, { "op0s", 0.1f }, { "op1ratio", 1 }, { "op1level", 0.5f }, { "op1d", 1800 },
                                 { "op1s", 0.1f }, { "op2ratio", 5 }, { "op2level", 0.3f }, { "op2d", 900 }, { "op2s", 0 }, { "op3ratio", 1.01f }, { "op3level", 0.6f },
                                 { "op3d", 2000 }, { "op3s", 0.2f }, { "feedback", 0.4f }, { "volume", -14 } } },
                { "Kalimba", { { "algorithm", 4 }, { "op0ratio", 1 }, { "op0d", 900 }, { "op0s", 0 }, { "op0r", 600 }, { "op1ratio", 7 }, { "op1level", 0.4f }, { "op1d", 60 },
                               { "op1s", 0 }, { "op2ratio", 2 }, { "op2level", 0.3f }, { "op2d", 500 }, { "op2s", 0 }, { "op3ratio", 13 }, { "op3level", 0.15f }, { "op3d", 30 },
                               { "op3s", 0 }, { "volume", -12 } } },
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
            // Pulse Space: 0 small room, 1 tiled room, 2 studio, 3 chamber, 4 plate, 5 spring, 6 hall, 7 large hall, 8 cathedral, 9 canyon, 10 gated, 11 bloom
            { "pulseSpace", {
                { "Drum Room", { { "space", 0 }, { "size", 1.1f }, { "lowcut", 120 }, { "highcut", 12000 }, { "mix", 0.22f } } },
                { "Tiled Bathroom", { { "space", 1 }, { "lowcut", 200 }, { "highcut", 16000 }, { "mix", 0.3f } } },
                { "Vocal Booth Air", { { "space", 2 }, { "size", 0.8f }, { "predelay", 10 }, { "lowcut", 180 }, { "mix", 0.15f } } },
                { "Echo Chamber", { { "space", 3 }, { "predelay", 25 }, { "lowcut", 150 }, { "highcut", 9000 }, { "mix", 0.3f } } },
                { "Vocal Plate", { { "space", 4 }, { "predelay", 40 }, { "lowcut", 250 }, { "highcut", 11000 }, { "mix", 0.25f } } },
                { "Snare Plate (short)", { { "space", 4 }, { "decay", 0.5f }, { "lowcut", 300 }, { "mix", 0.3f } } },
                { "Guitar Spring", { { "space", 5 }, { "lowcut", 200 }, { "highcut", 6000 }, { "mix", 0.3f } } },
                { "Concert Hall", { { "space", 6 }, { "predelay", 20 }, { "lowcut", 100 }, { "mix", 0.3f } } },
                { "Strings Hall (wide)", { { "space", 7 }, { "predelay", 30 }, { "width", 1.4f }, { "highcut", 9000 }, { "mix", 0.35f } } },
                { "Cathedral Pad", { { "space", 8 }, { "lowcut", 200 }, { "highcut", 7000 }, { "mix", 0.5f } } },
                { "Canyon Echoes", { { "space", 9 }, { "size", 1.3f }, { "lowcut", 250 }, { "mix", 0.35f } } },
                { "80s Gated Snare", { { "space", 10 }, { "size", 0.9f }, { "lowcut", 200 }, { "mix", 0.45f } } },
                { "Reverse Swell", { { "space", 6 }, { "reverse", 1 }, { "decay", 0.6f }, { "lowcut", 200 }, { "mix", 0.5f } } },
                { "Blooming Pad", { { "space", 11 }, { "size", 1.4f }, { "width", 1.6f }, { "lowcut", 150 }, { "mix", 0.6f } } },
                { "Send (100% wet hall)", { { "space", 6 }, { "predelay", 20 }, { "lowcut", 150 }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            // Note Length: mode 0 time, 1 synced; rate 0 1/32, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 bar; trigger 0 key down, 1 key up
            { "pulseNoteLength", {
                { "Staccato 1/16", { { "mode", 1 }, { "rate", 1 }, { "gate", 0.5f } } },
                { "Even 1/8 Notes", { { "mode", 1 }, { "rate", 2 }, { "gate", 0.9f } } },
                { "Long Pads (1 bar)", { { "mode", 1 }, { "rate", 5 }, { "gate", 1.0f } } },
                { "Drum Triggers (20 ms)", { { "mode", 0 }, { "time", 20 } } },
                { "Play on Release", { { "mode", 1 }, { "rate", 2 }, { "trigger", 1 } } },
                { "Overlapping Legato", { { "mode", 1 }, { "rate", 3 }, { "gate", 1.6f } } } } },

            //----------------------------------------------------------------------------------
            { "pulsePitch", {
                { "Octave Up", { { "octaves", 1 } } },
                { "Octave Down", { { "octaves", -1 } } },
                { "Fifth Up", { { "semis", 7 } } },
                { "Bass Notes Only (below C3)", { { "lowest", 0 }, { "range", 60 } } },
                { "Top Half Only (C3 and up)", { { "lowest", 60 }, { "range", 68 } } } } },

            //----------------------------------------------------------------------------------
            // Random: direction 0 up, 1 down, 2 both; scale 0 any note, 1.. = the scales
            { "pulseRandom", {
                { "Octave Jumps", { { "chance", 0.3f }, { "choices", 2 }, { "step", 12 }, { "direction", 2 } } },
                { "Sometimes a Fifth", { { "chance", 0.25f }, { "choices", 1 }, { "step", 7 }, { "direction", 0 } } },
                { "Wander in C Major", { { "chance", 0.6f }, { "choices", 4 }, { "step", 1 }, { "direction", 2 }, { "root", 0 }, { "scale", 1 } } },
                { "Total Chaos", { { "chance", 1.0f }, { "choices", 24 }, { "step", 1 }, { "direction", 2 } } },
                { "Melody Maker (A minor)", { { "chance", 0.8f }, { "choices", 7 }, { "step", 2 }, { "direction", 2 }, { "root", 9 }, { "scale", 2 } } } } },

            //----------------------------------------------------------------------------------
            // Velocity: mode 0 curve, 1 fixed
            { "pulseVelocity", {
                { "All Full", { { "mode", 1 }, { "outhigh", 127 } } },
                { "All Medium (90)", { { "mode", 1 }, { "outhigh", 90 } } },
                { "Bring Up Soft Playing", { { "curve", 0.5f } } },
                { "Tame Hard Playing", { { "curve", -0.4f }, { "outhigh", 110 } } },
                { "Narrow Range (70 to 110)", { { "outlow", 70 }, { "outhigh", 110 } } },
                { "Humanise", { { "random", 0.2f } } },
                { "Ghost Notes", { { "outlow", 15 }, { "outhigh", 50 }, { "random", 0.15f } } } } },

            //----------------------------------------------------------------------------------
            // Pulse Analog: wave 0 saw, 1 pulse, 2 triangle, 3 sine; mode 0 poly, 1 mono
            { "pulseAnalog", {
                { "Fat Brass", { { "wave1", 0 }, { "wave2", 0 }, { "fine2", 9 }, { "cutoff", 900 }, { "reso", 0.15f }, { "envamt", 0.55f }, { "fa", 40 }, { "fd", 500 }, { "fs", 0.5f }, { "a", 15 }, { "s", 0.9f }, { "r", 250 }, { "unison", 0.3f } } },
                { "Round Mono Bass", { { "wave1", 0 }, { "oct1", -1 }, { "wave2", 1 }, { "oct2", -1 }, { "fine2", 5 }, { "sub", 0.5f }, { "cutoff", 350 }, { "reso", 0.3f }, { "envamt", 0.45f }, { "fd", 220 }, { "fs", 0.1f },
                                       { "s", 0.9f }, { "r", 120 }, { "mode", 1 }, { "glide", 40 }, { "fdrive", 0.5f }, { "level", -6 } } },
                { "Acid Squelch", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 300 }, { "reso", 0.85f }, { "envamt", 0.6f }, { "fd", 180 }, { "fs", 0 }, { "s", 1 }, { "r", 80 }, { "mode", 1 }, { "glide", 60 },
                                    { "fdrive", 0.7f }, { "keytrack", 0.5f }, { "level", -10 } } },
                { "PWM Strings", { { "wave1", 1 }, { "wave2", 1 }, { "fine2", 11 }, { "pw", 0.5f }, { "lfopw", 0.7f }, { "lforate", 0.7f }, { "cutoff", 3500 }, { "reso", 0.1f }, { "envamt", 0.1f }, { "a", 350 }, { "s", 1 },
                                   { "r", 900 }, { "unison", 0.5f } } },
                { "Sync Lead", { { "wave1", 0 }, { "wave2", 0 }, { "semi2", 7 }, { "sync", 1 }, { "mix", 0.9f }, { "cutoff", 4500 }, { "reso", 0.3f }, { "envamt", 0.3f }, { "lfopitch", 0.12f }, { "lforate", 5.5f },
                                 { "mode", 1 }, { "glide", 50 }, { "s", 0.85f } } },
                { "Soft Pad", { { "wave1", 2 }, { "wave2", 0 }, { "oct2", 1 }, { "mix", 0.3f }, { "cutoff", 1400 }, { "envamt", 0.25f }, { "fa", 900 }, { "fd", 1500 }, { "fs", 0.6f }, { "a", 600 }, { "s", 0.9f }, { "r", 1400 },
                                { "unison", 0.4f }, { "drift", 0.5f } } },
                { "Pluck", { { "wave1", 1 }, { "pw", 0.3f }, { "wave2", 0 }, { "cutoff", 600 }, { "reso", 0.35f }, { "envamt", 0.6f }, { "fd", 180 }, { "fs", 0 }, { "d", 350 }, { "s", 0 }, { "r", 300 } } },
                { "Self-Singing Filter", { { "wave1", 3 }, { "mix", 0 }, { "noise", 0.05f }, { "cutoff", 800 }, { "reso", 1.0f }, { "keytrack", 1.0f }, { "envamt", 0.15f }, { "fd", 400 }, { "s", 0.8f }, { "level", -14 } } },
                { "Fifth Stab", { { "wave1", 0 }, { "wave2", 0 }, { "semi2", 7 }, { "cutoff", 1800 }, { "reso", 0.2f }, { "envamt", 0.4f }, { "fd", 300 }, { "fs", 0.2f }, { "d", 400 }, { "s", 0.3f }, { "r", 350 }, { "unison", 0.25f } } },
                // The acid bank: one saw or one square into the filter, mono, notes tied where they overlap, hard notes accented.
                // Write the line with short notes; make a note reach into the next one for a slide, and play it hard for an accent.
                { "Acid: Classic Saw", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 260 }, { "reso", 0.82f }, { "envamt", 0.5f }, { "fa", 1 }, { "fd", 220 }, { "fs", 0 }, { "fr", 120 }, { "a", 1 }, { "d", 600 }, { "s", 0.7f }, { "r", 60 },
                                         { "mode", 1 }, { "glide", 60 }, { "glidemode", 1 }, { "accent", 0.8f }, { "keytrack", 0.4f }, { "fdrive", 0.55f }, { "drift", 0.05f }, { "level", -9 } } },
                { "Acid: Hollow Square", { { "wave1", 1 }, { "pw", 0.5f }, { "mix", 0 }, { "cutoff", 320 }, { "reso", 0.78f }, { "envamt", 0.45f }, { "fa", 1 }, { "fd", 260 }, { "fs", 0 }, { "fr", 120 }, { "a", 1 }, { "d", 600 }, { "s", 0.7f },
                                           { "r", 60 }, { "mode", 1 }, { "glide", 60 }, { "glidemode", 1 }, { "accent", 0.8f }, { "keytrack", 0.4f }, { "fdrive", 0.5f }, { "drift", 0.05f }, { "level", -10 } } },
                { "Acid: Rubber (low resonance)", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 420 }, { "reso", 0.45f }, { "envamt", 0.4f }, { "fa", 1 }, { "fd", 160 }, { "fs", 0 }, { "a", 1 }, { "d", 400 }, { "s", 0.6f }, { "r", 50 },
                                                    { "mode", 1 }, { "glide", 80 }, { "glidemode", 1 }, { "accent", 0.6f }, { "keytrack", 0.5f }, { "fdrive", 0.4f }, { "sub", 0.25f }, { "drift", 0.05f }, { "level", -8 } } },
                { "Acid: Screamer (driven, wide open on accents)", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 380 }, { "reso", 0.93f }, { "envamt", 0.62f }, { "fa", 1 }, { "fd", 300 }, { "fs", 0 }, { "a", 1 }, { "d", 700 }, { "s", 0.7f },
                                                                    { "r", 70 }, { "mode", 1 }, { "glide", 55 }, { "glidemode", 1 }, { "accent", 1.0f }, { "keytrack", 0.5f }, { "fdrive", 0.9f }, { "drift", 0.05f }, { "level", -6 } } },
                { "Acid: Deep (closed, for the low octave)", { { "wave1", 1 }, { "pw", 0.45f }, { "oct1", -1 }, { "mix", 0 }, { "cutoff", 140 }, { "reso", 0.7f }, { "envamt", 0.42f }, { "fa", 1 }, { "fd", 350 }, { "fs", 0 }, { "a", 1 }, { "d", 800 },
                                                               { "s", 0.8f }, { "r", 80 }, { "mode", 1 }, { "glide", 70 }, { "glidemode", 1 }, { "accent", 0.7f }, { "keytrack", 0.6f }, { "fdrive", 0.6f }, { "drift", 0.05f }, { "level", -7 } } },
                { "Acid: Long Decay (slow sweeps)", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 220 }, { "reso", 0.85f }, { "envamt", 0.55f }, { "fa", 1 }, { "fd", 1400 }, { "fs", 0 }, { "a", 1 }, { "d", 1500 }, { "s", 0.8f }, { "r", 90 },
                                                      { "mode", 1 }, { "glide", 90 }, { "glidemode", 1 }, { "accent", 0.7f }, { "keytrack", 0.4f }, { "fdrive", 0.5f }, { "drift", 0.05f }, { "level", -10 } } },
                { "Acid: Blip (short, dry, percussive)", { { "wave1", 1 }, { "pw", 0.35f }, { "mix", 0 }, { "cutoff", 500 }, { "reso", 0.88f }, { "envamt", 0.5f }, { "fa", 1 }, { "fd", 70 }, { "fs", 0 }, { "a", 1 }, { "d", 130 }, { "s", 0 }, { "r", 40 },
                                                           { "mode", 1 }, { "glide", 40 }, { "glidemode", 1 }, { "accent", 0.9f }, { "keytrack", 0.6f }, { "fdrive", 0.45f }, { "drift", 0.05f }, { "level", -8 } } },
                { "Acid: Wobbling (LFO on the cutoff too)", { { "wave1", 0 }, { "mix", 0 }, { "cutoff", 300 }, { "reso", 0.84f }, { "envamt", 0.4f }, { "fa", 1 }, { "fd", 240 }, { "fs", 0 }, { "a", 1 }, { "d", 600 }, { "s", 0.7f }, { "r", 60 },
                                                              { "lfocutoff", 0.3f }, { "lforate", 0.35f }, { "mode", 1 }, { "glide", 60 }, { "glidemode", 1 }, { "accent", 0.8f }, { "keytrack", 0.4f }, { "fdrive", 0.6f }, { "drift", 0.05f }, { "level", -10 } } },
                { "Wobble Bass (LFO > cutoff)", { { "wave1", 0 }, { "oct1", -1 }, { "wave2", 1 }, { "oct2", -1 }, { "cutoff", 500 }, { "reso", 0.5f }, { "envamt", 0 }, { "lfocutoff", 0.7f }, { "lforate", 3.0f }, { "mode", 1 },
                                                  { "sub", 0.4f }, { "s", 1 }, { "level", -8 } } } } },

            //----------------------------------------------------------------------------------
            // Pulse String: exciter 0 pluck, 1 pick, 2 hammer, 3 mallet
            { "pulseString", {
                { "Nylon Guitar", { { "exciter", 0 }, { "position", 0.22f }, { "brightness", 0.5f }, { "decay", 3.5f }, { "damping", 0.4f }, { "body", 0.5f }, { "detune", 0 } } },
                { "Steel String (picked)", { { "exciter", 1 }, { "position", 0.12f }, { "brightness", 0.85f }, { "decay", 6 }, { "damping", 0.2f }, { "body", 0.4f }, { "detune", 0 } } },
                { "Twelve String", { { "exciter", 1 }, { "position", 0.15f }, { "brightness", 0.8f }, { "decay", 5 }, { "damping", 0.25f }, { "detune", 9 }, { "body", 0.35f } } },
                { "Harp", { { "exciter", 0 }, { "position", 0.4f }, { "brightness", 0.55f }, { "decay", 7 }, { "damping", 0.3f }, { "body", 0.2f }, { "release", 1500 } } },
                { "Muted Bass", { { "exciter", 0 }, { "position", 0.3f }, { "brightness", 0.3f }, { "decay", 1.2f }, { "damping", 0.7f }, { "body", 0.3f }, { "detune", 0 }, { "release", 80 } } },
                { "Piano-ish (hammer)", { { "exciter", 2 }, { "position", 0.12f }, { "brightness", 0.6f }, { "decay", 8 }, { "damping", 0.25f }, { "stiffness", 0.15f }, { "detune", 3 }, { "body", 0.3f } } },
                { "Marimba (mallet)", { { "exciter", 3 }, { "position", 0.5f }, { "brightness", 0.5f }, { "decay", 1.2f }, { "damping", 0.8f }, { "detune", 0 }, { "body", 0.6f } } },
                { "Glass Bars", { { "exciter", 3 }, { "position", 0.2f }, { "brightness", 0.9f }, { "decay", 5 }, { "damping", 0.15f }, { "stiffness", 0.8f }, { "detune", 6 }, { "body", 0.1f } } },
                { "Koto", { { "exciter", 1 }, { "position", 0.06f }, { "brightness", 0.9f }, { "decay", 4 }, { "damping", 0.35f }, { "stiffness", 0.05f }, { "body", 0.45f }, { "detune", 0 } } },
                { "Dulcimer", { { "exciter", 2 }, { "position", 0.1f }, { "brightness", 0.85f }, { "decay", 6 }, { "damping", 0.2f }, { "detune", 7 }, { "body", 0.3f }, { "release", 2500 } } } } },

            //----------------------------------------------------------------------------------
            // Tonewheel Organ: bar1..bar9 = 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1' (0 in .. 8 out); perc 0 off, 1 2nd, 2 3rd;
            // vibrato 0 off, 1-3 V1-V3, 4-6 C1-C3; rotary 0 stop, 1 slow, 2 fast
            { "pulseOrgan", {
                { "Jazz Trio (888 + 3rd perc)", { { "bar1", 8 }, { "bar2", 8 }, { "bar3", 8 }, { "perc", 2 }, { "percdecay", 0 }, { "vibrato", 6 }, { "drive", 0.25f }, { "rotary", 1 } } },
                { "Gospel Full", { { "bar1", 8 }, { "bar2", 8 }, { "bar3", 8 }, { "bar4", 8 }, { "bar5", 8 }, { "bar6", 8 }, { "bar7", 8 }, { "bar8", 8 }, { "bar9", 8 }, { "drive", 0.4f }, { "rotary", 2 }, { "level", -10 } } },
                { "Rock Scream", { { "bar1", 8 }, { "bar2", 8 }, { "bar3", 8 }, { "bar4", 8 }, { "bar5", 0 }, { "bar6", 0 }, { "bar7", 0 }, { "bar8", 0 }, { "bar9", 8 }, { "drive", 0.8f }, { "click", 0.5f }, { "rotary", 2 }, { "level", -9 } } },
                { "Ballad Pad", { { "bar1", 0 }, { "bar2", 0 }, { "bar3", 8 }, { "bar4", 4 }, { "bar5", 0 }, { "bar6", 2 }, { "vibrato", 5 }, { "drive", 0.05f }, { "click", 0.05f }, { "rotary", 1 } } },
                { "Reggae Bubble", { { "bar1", 0 }, { "bar2", 0 }, { "bar3", 8 }, { "bar4", 8 }, { "bar5", 0 }, { "bar6", 0 }, { "bar7", 0 }, { "bar8", 0 }, { "bar9", 6 }, { "click", 0.6f }, { "rotary", 0 }, { "rotarymix", 0.5f } } },
                { "Whistle (flutes)", { { "bar1", 0 }, { "bar2", 0 }, { "bar3", 8 }, { "bar4", 0 }, { "bar5", 0 }, { "bar6", 4 }, { "bar9", 3 }, { "perc", 1 }, { "percdecay", 1 }, { "vibrato", 3 }, { "drive", 0 }, { "rotary", 1 } } },
                { "Bass Pedals", { { "bar1", 8 }, { "bar2", 0 }, { "bar3", 6 }, { "click", 0.1f }, { "drive", 0.15f }, { "rotary", 0 }, { "rotarymix", 0 } } },
                { "Cathedral-ish", { { "bar1", 8 }, { "bar2", 4 }, { "bar3", 8 }, { "bar4", 6 }, { "bar5", 4 }, { "bar6", 6 }, { "bar7", 2 }, { "bar8", 3 }, { "bar9", 4 }, { "click", 0 }, { "drive", 0 }, { "rotary", 0 }, { "rotarymix", 0 }, { "level", -10 } } } } },

            //----------------------------------------------------------------------------------
            // Electric Piano: model 0 tine, 1 reed
            { "pulseEPiano", {
                { "Classic Tine", { { "model", 0 }, { "bell", 0.5f }, { "bark", 0.45f }, { "tremolo", 0.25f }, { "tremrate", 4.5f } } },
                { "Mellow Ballad", { { "model", 0 }, { "bell", 0.25f }, { "bark", 0.25f }, { "hardness", 0.5f }, { "tone", 3500 }, { "decay", 5 }, { "tremolo", 0.4f }, { "tremrate", 3.2f } } },
                { "Bright Bell Keys", { { "model", 0 }, { "bell", 0.95f }, { "bark", 0.3f }, { "tone", 12000 }, { "tremolo", 0.15f }, { "tremrate", 5.5f } } },
                { "Barking Funk", { { "model", 0 }, { "bell", 0.4f }, { "bark", 0.9f }, { "hardness", 1.0f }, { "drive", 0.35f }, { "decay", 2.5f }, { "tremolo", 0 } } },
                { "Dyno (hard and glassy)", { { "model", 0 }, { "bell", 0.8f }, { "bark", 0.6f }, { "tone", 14000 }, { "drive", 0.15f }, { "tremolo", 0.2f }, { "tremstereo", 1.0f } } },
                { "Reed Classic", { { "model", 1 }, { "bell", 0.3f }, { "bark", 0.5f }, { "decay", 2.2f }, { "tone", 5000 }, { "tremolo", 0.45f }, { "tremrate", 5.8f }, { "tremstereo", 0 } } },
                { "Reed Overdriven", { { "model", 1 }, { "bell", 0.4f }, { "bark", 0.85f }, { "drive", 0.6f }, { "decay", 1.8f }, { "tone", 4500 }, { "tremolo", 0.2f }, { "tremstereo", 0 }, { "level", -9 } } },
                { "Soft Reed Lullaby", { { "model", 1 }, { "bell", 0.1f }, { "bark", 0.2f }, { "hardness", 0.4f }, { "tone", 3000 }, { "decay", 3 }, { "tremolo", 0.3f }, { "tremrate", 4 }, { "tremstereo", 0 } } } } },

            //----------------------------------------------------------------------------------
            { "pulseSpectral", {
                { "Hold That Sound", { { "freeze", 1 }, { "mix", 1.0f } } },
                { "Frozen Pad Under It", { { "freeze", 1 }, { "blur", 0.5f }, { "mix", 0.5f } } },
                { "Smear", { { "blur", 0.8f }, { "mix", 0.7f } } },
                { "Ghost Wash", { { "blur", 1.0f }, { "delay", 250 }, { "feedback", 0.5f }, { "mix", 0.5f } } },
                { "Rainbow Up (highs later)", { { "delay", 700 }, { "tilt", 1.0f }, { "mix", 0.6f } } },
                { "Rainbow Down (lows later)", { { "delay", 700 }, { "tilt", -1.0f }, { "mix", 0.6f } } },
                { "Spectral Echoes", { { "delay", 375 }, { "tilt", 0.3f }, { "feedback", 0.6f }, { "mix", 0.4f } } },
                { "Octave Up", { { "shift", 12 }, { "mix", 1.0f } } },
                { "Fifth Harmony", { { "shift", 7 }, { "mix", 0.5f } } },
                { "Sub Octave Layer", { { "shift", -12 }, { "mix", 0.5f } } },
                { "Rising Echoes", { { "shift", 5 }, { "delay", 300 }, { "feedback", 0.4f }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            // Beat Repeat: interval 0 1/4, 1 1/2, 2 1 bar, 3 2 bars, 4 4 bars; grid 0 1/32, 1 1/16T, 2 1/16, 3 1/8T, 4 1/8, 5 1/4, 6 1/2;
            // gate 0 1/8, 1 1/4, 2 1/2, 3 1 bar, 4 2 bars; mode 0 mix, 1 insert, 2 gate
            { "pulseBeatRepeat", {
                { "Classic Stutter (every bar)", { { "interval", 2 }, { "grid", 2 }, { "gate", 1 }, { "chance", 1.0f }, { "mode", 1 } } },
                { "End-of-Phrase Fill", { { "interval", 4 }, { "grid", 2 }, { "gate", 2 }, { "chance", 1.0f }, { "variation", 0.3f }, { "mode", 1 } } },
                { "Now and Then", { { "interval", 2 }, { "grid", 4 }, { "gate", 1 }, { "chance", 0.4f }, { "mode", 1 } } },
                { "Machine Gun Roll", { { "interval", 3 }, { "grid", 0 }, { "gate", 1 }, { "decay", 0.2f }, { "mode", 1 } } },
                { "Falling Tape Stop", { { "interval", 3 }, { "grid", 4 }, { "gate", 3 }, { "pitch", -12 }, { "pitchdecay", 1.0f }, { "decay", 0.3f }, { "mode", 1 } } },
                { "Triplet Shuffle", { { "interval", 2 }, { "grid", 3 }, { "gate", 2 }, { "chance", 0.7f }, { "mode", 1 } } },
                { "Fading Echo (mix)", { { "interval", 2 }, { "grid", 5 }, { "gate", 3 }, { "decay", 0.6f }, { "mode", 0 } } },
                { "Chopped (gate)", { { "interval", 1 }, { "grid", 2 }, { "gate", 1 }, { "chance", 1.0f }, { "variation", 0.5f }, { "mode", 2 } } } } },

            //----------------------------------------------------------------------------------
            // Match EQ (what it has learned is not part of a preset: these are ways of applying it)
            { "pulseMatchEQ", {
                { "Full Match", { { "amount", 1.0f }, { "smooth", 0.33f }, { "range", 12 }, { "low", 20 }, { "high", 20000 } } },
                { "Half Way There", { { "amount", 0.5f }, { "smooth", 0.5f }, { "range", 9 }, { "low", 20 }, { "high", 20000 } } },
                { "Broad Strokes", { { "amount", 0.8f }, { "smooth", 1.5f }, { "range", 6 }, { "low", 20 }, { "high", 20000 } } },
                { "Fine Detail", { { "amount", 1.0f }, { "smooth", 0.12f }, { "range", 12 }, { "low", 20 }, { "high", 20000 } } },
                { "Mids and Highs Only", { { "amount", 1.0f }, { "smooth", 0.33f }, { "range", 12 }, { "low", 250 }, { "high", 20000 } } },
                { "Leave the Air Alone", { { "amount", 1.0f }, { "smooth", 0.33f }, { "range", 12 }, { "low", 20 }, { "high", 9000 } } },
                { "Mastering (gentle)", { { "amount", 0.6f }, { "smooth", 1.0f }, { "range", 4 }, { "low", 30 }, { "high", 18000 } } } } },

            // Reference (the song is not part of a preset; "listen" always starts on the mix)
            { "pulseReference", {
                { "Matched Loudness", { { "listen", 0 }, { "match", 1 }, { "trim", 0 }, { "offset", 0 }, { "loop", 1 } } },
                { "As Released (no matching)", { { "listen", 0 }, { "match", 0 }, { "trim", 0 }, { "offset", 0 }, { "loop", 1 } } },
                { "Skip the Intro (start at 0:30)", { { "listen", 0 }, { "match", 1 }, { "trim", 0 }, { "offset", 30 }, { "loop", 1 } } },
                { "From the First Chorus (1:00)", { { "listen", 0 }, { "match", 1 }, { "trim", 0 }, { "offset", 60 }, { "loop", 1 } } },
                { "Once Through (no loop)", { { "listen", 0 }, { "match", 1 }, { "trim", 0 }, { "offset", 0 }, { "loop", 0 } } } } },

            // Looper: length 0 free, 1 1 bar, 2 2 bars, 3 4 bars, 4 8 bars (the state is never stored in a preset: it starts stopped)
            { "pulseLooper", {
                { "Free Loop", { { "length", 0 }, { "feedback", 1.0f }, { "monitor", 1 } } },
                { "One-Bar Loop", { { "length", 1 }, { "feedback", 1.0f }, { "monitor", 1 } } },
                { "Four-Bar Loop", { { "length", 3 }, { "feedback", 1.0f }, { "monitor", 1 } } },
                { "Fading Layers", { { "length", 2 }, { "feedback", 0.7f }, { "monitor", 1 } } },
                { "Half Speed", { { "length", 2 }, { "speed", 0.5f }, { "monitor", 1 } } },
                { "Backwards", { { "length", 2 }, { "reverse", 1 }, { "monitor", 1 } } },
                { "Loop Only (no monitor)", { { "length", 0 }, { "monitor", 0 } } } } },

            //----------------------------------------------------------------------------------
            // Resonators: chord 0 octaves, 1 fifths, 2 major, 3 minor, 4 sus 4, 5 major 7, 6 minor 7, 7 harmonics; note = MIDI note of the lowest string
            { "pulseResonators", {
                { "Drums to Chords (A minor)", { { "note", 45 }, { "chord", 3 }, { "decay", 1.2f }, { "mix", 0.5f } } },
                { "Sympathetic Strings", { { "note", 38 }, { "chord", 1 }, { "decay", 4 }, { "colour", 3000 }, { "mix", 0.25f } } },
                { "Major Glow", { { "note", 48 }, { "chord", 2 }, { "decay", 2.5f }, { "detune", 8 }, { "mix", 0.4f } } },
                { "Dreamy Major 7", { { "note", 41 }, { "chord", 5 }, { "decay", 5 }, { "colour", 2500 }, { "detune", 10 }, { "mix", 0.45f } } },
                { "Moody Minor 7", { { "note", 40 }, { "chord", 6 }, { "decay", 3.5f }, { "colour", 3500 }, { "mix", 0.45f } } },
                { "Metal Pipe", { { "note", 57 }, { "chord", 7 }, { "decay", 0.6f }, { "colour", 12000 }, { "detune", 0 }, { "mix", 0.6f } } },
                { "Sitar Drone", { { "note", 36 }, { "chord", 0 }, { "decay", 8 }, { "colour", 9000 }, { "detune", 14 }, { "mix", 0.35f } } },
                { "Short Tuned Body", { { "note", 50 }, { "chord", 4 }, { "decay", 0.2f }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            // Shimmer: interval 0 octave down, 1 fourth up, 2 fifth up, 3 octave up, 4 octave + fifth, 5 two octaves
            { "pulseShimmer", {
                { "Classic Shimmer", { { "decay", 6 }, { "shimmer", 0.5f }, { "interval", 3 }, { "mix", 0.35f } } },
                { "Endless Heaven", { { "decay", 20 }, { "shimmer", 0.7f }, { "interval", 3 }, { "size", 1.0f }, { "damping", 6000 }, { "mix", 0.5f } } },
                { "Fifths Choir", { { "decay", 8 }, { "shimmer", 0.6f }, { "interval", 2 }, { "mix", 0.4f } } },
                { "Glass Octaves", { { "decay", 5 }, { "shimmer", 0.6f }, { "interval", 5 }, { "damping", 14000 }, { "lowcut", 300 }, { "mix", 0.3f } } },
                { "Dark Descent", { { "decay", 10 }, { "shimmer", 0.6f }, { "interval", 0 }, { "damping", 4000 }, { "lowcut", 60 }, { "mix", 0.4f } } },
                { "Organ Swell", { { "decay", 7 }, { "shimmer", 0.8f }, { "interval", 4 }, { "damping", 5000 }, { "mix", 0.45f } } },
                { "Just a Big Hall", { { "decay", 4 }, { "shimmer", 0 }, { "mix", 0.3f } } },
                { "Send (100% wet)", { { "decay", 8 }, { "shimmer", 0.55f }, { "interval", 3 }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            // Grain Delay: sync 0 free, 1 1/16, 2 1/8, 3 1/4, 4 1/2, 5 1 bar
            { "pulseGrainDelay", {
                { "Octave Up Echo", { { "time", 300 }, { "pitch", 12 }, { "feedback", 0.4f }, { "mix", 0.35f } } },
                { "Rising Stairs (1/8)", { { "sync", 2 }, { "pitch", 7 }, { "feedback", 0.65f }, { "mix", 0.4f } } },
                { "Falling Stairs (1/4)", { { "sync", 3 }, { "pitch", -5 }, { "feedback", 0.6f }, { "mix", 0.4f } } },
                { "Cloud", { { "time", 150 }, { "grain", 120 }, { "spray", 300 }, { "randompitch", 0.05f }, { "feedback", 0.5f }, { "mix", 0.5f } } },
                { "Sparkle Dust", { { "time", 80 }, { "pitch", 12 }, { "grain", 30 }, { "spray", 200 }, { "randompitch", 1.0f }, { "feedback", 0.3f }, { "mix", 0.3f } } },
                { "Thickener (tiny detune)", { { "time", 25 }, { "pitch", 0.12f }, { "grain", 80 }, { "spray", 10 }, { "feedback", 0 }, { "mix", 0.4f } } },
                { "Sub Shadow", { { "time", 40 }, { "pitch", -12 }, { "grain", 100 }, { "feedback", 0 }, { "mix", 0.4f } } },
                { "Smeared Repeat", { { "time", 400 }, { "grain", 200 }, { "spray", 120 }, { "feedback", 0.7f }, { "mix", 0.4f } } },
                { "Glitch Scatter (1/16)", { { "sync", 1 }, { "grain", 20 }, { "spray", 400 }, { "randompitch", 0.5f }, { "feedback", 0.4f }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            // Pulse Amp: model 0 clean, 1 crunch, 2 lead, 3 heavy, 4 bass; cabinet 0 none, 1 1x12 open, 2 2x12, 3 4x12 closed, 4 bass 1x15
            { "pulseAmp", {
                { "Sparkling Clean", { { "model", 0 }, { "gain", 0.35f }, { "bass", 0.45f }, { "mid", 0.4f }, { "treble", 0.65f }, { "presence", 0.6f }, { "cabinet", 1 }, { "mic", 0.2f } } },
                { "Edge of Breakup", { { "model", 0 }, { "gain", 0.85f }, { "bass", 0.5f }, { "mid", 0.6f }, { "treble", 0.55f }, { "cabinet", 2 } } },
                { "Blues Crunch", { { "model", 1 }, { "gain", 0.45f }, { "bass", 0.5f }, { "mid", 0.65f }, { "treble", 0.5f }, { "cabinet", 2 }, { "mic", 0.35f } } },
                { "Classic Rock Rhythm", { { "model", 1 }, { "gain", 0.7f }, { "bass", 0.55f }, { "mid", 0.6f }, { "treble", 0.6f }, { "presence", 0.6f }, { "cabinet", 3 } } },
                { "Singing Lead", { { "model", 2 }, { "gain", 0.7f }, { "bass", 0.45f }, { "mid", 0.75f }, { "treble", 0.5f }, { "presence", 0.55f }, { "cabinet", 3 }, { "mic", 0.4f } } },
                { "Scooped Heavy", { { "model", 3 }, { "gain", 0.75f }, { "bass", 0.7f }, { "mid", 0.25f }, { "treble", 0.65f }, { "presence", 0.6f }, { "cabinet", 3 }, { "mic", 0.15f }, { "level", -3 } } },
                { "Tight Modern Rhythm", { { "model", 3 }, { "gain", 0.6f }, { "bass", 0.5f }, { "mid", 0.5f }, { "treble", 0.55f }, { "cabinet", 3 }, { "mic", 0.25f }, { "level", -3 } } },
                { "Round Bass", { { "model", 4 }, { "gain", 0.3f }, { "bass", 0.6f }, { "mid", 0.5f }, { "treble", 0.4f }, { "cabinet", 4 } } },
                { "Driven Bass (parallel)", { { "model", 4 }, { "gain", 0.8f }, { "bass", 0.55f }, { "mid", 0.6f }, { "cabinet", 4 }, { "mix", 0.5f } } },
                { "Gritty Keys (no cabinet)", { { "model", 1 }, { "gain", 0.4f }, { "cabinet", 0 }, { "mix", 0.6f } } },
                { "Lo-fi Drums in a Combo", { { "model", 1 }, { "gain", 0.55f }, { "cabinet", 1 }, { "mic", 0.8f }, { "mix", 0.5f } } } } },

            //----------------------------------------------------------------------------------
            // Frequency Shifter: mode 0 shift, 1 ring mod
            { "pulseShifter", {
                { "Slow Phasing (0.3 Hz)", { { "shift", 0 }, { "fine", 0.3f }, { "mix", 0.5f } } },
                { "Stereo Drift", { { "shift", 0 }, { "fine", 1.5f }, { "spread", 1.0f }, { "mix", 0.5f } } },
                { "Bell Maker", { { "shift", 240 }, { "mix", 1.0f } } },
                { "Detuned Double", { { "shift", 6 }, { "spread", 1.0f }, { "mix", 0.4f } } },
                { "Barber Pole Up", { { "shift", 12 }, { "feedback", 0.8f }, { "delay", 60 }, { "mix", 0.6f } } },
                { "Falling Spiral", { { "shift", -35 }, { "feedback", 0.85f }, { "delay", 120 }, { "mix", 0.5f } } },
                { "Robot Ring", { { "mode", 1 }, { "shift", 90 }, { "mix", 1.0f } } },
                { "Tremolo Ring (8 Hz)", { { "mode", 1 }, { "shift", 8 }, { "mix", 1.0f } } },
                { "Seasick", { { "shift", 0 }, { "lfoamount", 30 }, { "lforate", 0.3f }, { "mix", 1.0f } } } } },

            //----------------------------------------------------------------------------------
            { "pulseRedux", {
                { "12-bit Sampler", { { "rate", 26000 }, { "bits", 12 }, { "soft", 1 } } },
                { "SP-style Crunch", { { "rate", 13000 }, { "bits", 12 }, { "tone", 9000 } } },
                { "8-bit Console", { { "rate", 11000 }, { "bits", 8 } } },
                { "Telephone", { { "rate", 6000 }, { "bits", 10 }, { "soft", 1 }, { "tone", 3200 } } },
                { "Aliasing Bells", { { "rate", 3500 }, { "bits", 16 } } },
                { "Broken Speaker", { { "rate", 9000 }, { "bits", 3.5f }, { "tone", 5000 } } },
                { "Unstable Clock", { { "rate", 8000 }, { "jitter", 0.8f }, { "bits", 12 } } },
                { "Dust (parallel)", { { "rate", 5000 }, { "bits", 6 }, { "mix", 0.3f } } } } },

            //----------------------------------------------------------------------------------
            // Rumble: dry 1 = the kick stays in, 0 = rumble only (for a return track)
            { "pulseRumble", {
                { "Classic Techno Rumble", { { "amount", -8 }, { "decay", 1.2f }, { "tone", 110 }, { "lowcut", 30 }, { "drive", 8 }, { "duck", 0.9f }, { "recover", 160 }, { "width", 0 }, { "dry", 1 } } },
                { "Short and Tight (fast tracks)", { { "amount", -7 }, { "decay", 0.5f }, { "tone", 130 }, { "lowcut", 35 }, { "drive", 10 }, { "duck", 1.0f }, { "recover", 90 }, { "width", 0 }, { "dry", 1 } } },
                { "Long Dark Hall", { { "amount", -10 }, { "decay", 3.5f }, { "tone", 85 }, { "lowcut", 28 }, { "drive", 6 }, { "duck", 0.85f }, { "recover", 260 }, { "width", 0.15f }, { "dry", 1 } } },
                { "Driven Growl", { { "amount", -9 }, { "decay", 1.0f }, { "tone", 220 }, { "lowcut", 40 }, { "drive", 24 }, { "duck", 0.95f }, { "recover", 140 }, { "width", 0 }, { "dry", 1 } } },
                { "Sub Swell (slow to come back)", { { "amount", -6 }, { "decay", 2.0f }, { "tone", 70 }, { "lowcut", 25 }, { "drive", 4 }, { "duck", 1.0f }, { "recover", 420 }, { "width", 0 }, { "dry", 1 } } },
                { "Wide Warehouse", { { "amount", -11 }, { "decay", 2.6f }, { "tone", 320 }, { "lowcut", 60 }, { "drive", 12 }, { "duck", 0.8f }, { "recover", 200 }, { "width", 0.8f }, { "dry", 1 } } },
                { "Rumble Only (on a return fed by the kick)", { { "amount", 0 }, { "decay", 1.4f }, { "tone", 100 }, { "lowcut", 30 }, { "drive", 10 }, { "duck", 0.9f }, { "recover", 170 }, { "width", 0 }, { "dry", 0 } } } } },

            { "pulseExciter", {
                { "Vocal Presence", { { "freq", 3500 }, { "drive", 12 }, { "colour", 0.3f }, { "amount", 0.25f } } },
                { "Air", { { "freq", 8000 }, { "drive", 14 }, { "colour", 0 }, { "amount", 0.3f } } },
                { "Acoustic Guitar Sparkle", { { "freq", 5000 }, { "drive", 10 }, { "colour", 0.5f }, { "amount", 0.3f } } },
                { "Snare Crack", { { "freq", 2500 }, { "drive", 16 }, { "colour", -0.5f }, { "amount", 0.35f } } },
                { "Dull Recording Rescue", { { "freq", 2000 }, { "drive", 18 }, { "colour", 0 }, { "amount", 0.45f } } },
                { "Bass Definition", { { "freq", 700 }, { "drive", 14 }, { "colour", -0.3f }, { "amount", 0.3f } } },
                { "Master Sheen (subtle)", { { "freq", 6500 }, { "drive", 8 }, { "colour", 0.2f }, { "amount", 0.12f } } } } },

            //----------------------------------------------------------------------------------
            // Pulse Chorus: mode 0 chorus, 1 ensemble, 2 vibrato
            { "pulseChorus", {
                { "Classic Chorus", { { "mode", 0 }, { "rate", 0.8f }, { "amount", 0.5f }, { "delay", 7 }, { "mix", 0.5f } } },
                { "Wide Clean Guitar", { { "mode", 0 }, { "rate", 0.45f }, { "amount", 0.6f }, { "delay", 12 }, { "lowcut", 150 }, { "mix", 0.4f } } },
                { "String Ensemble", { { "mode", 1 }, { "rate", 0.6f }, { "amount", 0.7f }, { "delay", 9 }, { "highcut", 9000 }, { "mix", 0.5f } } },
                { "Thick Pad", { { "mode", 1 }, { "rate", 0.3f }, { "amount", 0.9f }, { "delay", 14 }, { "mix", 0.6f } } },
                { "Bass Chorus (lows stay put)", { { "mode", 0 }, { "rate", 0.7f }, { "amount", 0.5f }, { "delay", 8 }, { "lowcut", 250 }, { "mix", 0.4f } } },
                { "Slow Detune", { { "mode", 0 }, { "rate", 0.12f }, { "amount", 1.0f }, { "delay", 18 }, { "mix", 0.5f } } },
                { "Jet Feedback", { { "mode", 0 }, { "rate", 0.2f }, { "amount", 0.9f }, { "delay", 2 }, { "feedback", 0.7f }, { "mix", 0.5f } } },
                { "Tape Wobble", { { "mode", 2 }, { "rate", 0.9f }, { "amount", 0.25f }, { "delay", 6 } } },
                { "Deep Vibrato", { { "mode", 2 }, { "rate", 5.5f }, { "amount", 0.5f }, { "delay", 4 } } } } },

            //----------------------------------------------------------------------------------
            // Phaser-Flanger: mode 0 phaser, 1 flanger; stages 0..4 = 2, 4, 6, 8, 12; sync 0 free, 1 1/16 ... 7 4 bars
            { "pulsePhaser", {
                { "Classic Phaser", { { "mode", 0 }, { "rate", 0.4f }, { "amount", 0.6f }, { "centre", 700 }, { "feedback", 0.4f }, { "stages", 1 } } },
                { "Slow Deep Sweep", { { "mode", 0 }, { "rate", 0.08f }, { "amount", 0.9f }, { "centre", 500 }, { "feedback", 0.6f }, { "stages", 3 } } },
                { "Funky Clav (1/8)", { { "mode", 0 }, { "sync", 2 }, { "amount", 0.7f }, { "centre", 900 }, { "feedback", 0.5f }, { "stages", 1 } } },
                { "Twelve-Stage Swirl", { { "mode", 0 }, { "rate", 0.25f }, { "amount", 0.8f }, { "centre", 800 }, { "feedback", 0.7f }, { "stages", 4 }, { "spread", 180 } } },
                { "Subtle Shimmer", { { "mode", 0 }, { "rate", 0.6f }, { "amount", 0.3f }, { "centre", 2500 }, { "feedback", 0.2f }, { "stages", 0 }, { "mix", 0.35f } } },
                { "Classic Flanger", { { "mode", 1 }, { "rate", 0.25f }, { "amount", 0.6f }, { "centre", 400 }, { "feedback", 0.6f } } },
                { "Jet Plane (2 bars)", { { "mode", 1 }, { "sync", 6 }, { "amount", 1.0f }, { "centre", 600 }, { "feedback", 0.85f } } },
                { "Through-Zero-ish", { { "mode", 1 }, { "rate", 0.15f }, { "amount", 0.9f }, { "centre", 1500 }, { "feedback", -0.7f } } },
                { "Metallic Drone", { { "mode", 1 }, { "amount", 0 }, { "centre", 220 }, { "feedback", 0.9f }, { "mix", 0.6f } } } } },

            //----------------------------------------------------------------------------------
            // Auto Pan: shape 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square, 5 random; phase 180 = auto-pan, 0 = tremolo
            { "pulseAutoPan", {
                { "Slow Pan", { { "amount", 0.7f }, { "rate", 0.4f }, { "phase", 180 } } },
                { "Pan in 1/4", { { "amount", 0.8f }, { "sync", 3 }, { "phase", 180 } } },
                { "Classic Tremolo", { { "amount", 0.6f }, { "rate", 5.5f }, { "phase", 0 } } },
                { "Vintage Amp Tremolo", { { "amount", 0.5f }, { "rate", 4.2f }, { "shape", 1 }, { "phase", 0 } } },
                { "Helicopter (1/16 square)", { { "amount", 1.0f }, { "sync", 1 }, { "shape", 4 }, { "phase", 0 }, { "smooth", 0.25f } } },
                { "Sidechain Pump (1/4)", { { "amount", 0.8f }, { "sync", 3 }, { "shape", 2 }, { "phase", 0 }, { "smooth", 0.3f } } },
                { "Random Flicker", { { "amount", 0.7f }, { "sync", 1 }, { "shape", 5 }, { "phase", 90 }, { "smooth", 0.2f } } },
                { "Wide Rotary", { { "amount", 0.5f }, { "rate", 6.5f }, { "phase", 90 } } } } },

            //----------------------------------------------------------------------------------
            // Pulse Gate: mode 0 gate, 1 expander, 2 ducker
            { "pulseGate", {
                { "Tight Drum Gate", { { "threshold", -30 }, { "range", 80 }, { "hysteresis", 4 }, { "attack", 0.05f }, { "hold", 20 }, { "release", 60 }, { "lookahead", 1.5f } } },
                { "Kick (lows keyed)", { { "threshold", -26 }, { "range", 60 }, { "attack", 0.05f }, { "hold", 40 }, { "release", 90 }, { "sclpf", 800 }, { "lookahead", 2 } } },
                { "Snare (mids keyed)", { { "threshold", -28 }, { "range", 50 }, { "attack", 0.05f }, { "hold", 50 }, { "release", 140 }, { "schpf", 150 }, { "sclpf", 6000 }, { "lookahead", 2 } } },
                { "Tom Clean-up", { { "threshold", -32 }, { "range", 40 }, { "hysteresis", 5 }, { "attack", 0.2f }, { "hold", 120 }, { "release", 300 }, { "lookahead", 2 } } },
                { "Vocal Breath Tamer", { { "mode", 1 }, { "threshold", -38 }, { "ratio", 2 }, { "range", 14 }, { "attack", 3 }, { "release", 200 }, { "schpf", 120 } } },
                { "Guitar Amp Hush", { { "mode", 1 }, { "threshold", -45 }, { "ratio", 3 }, { "range", 30 }, { "attack", 1 }, { "release", 250 } } },
                { "Soft Noise Floor", { { "mode", 1 }, { "threshold", -50 }, { "ratio", 1.5f }, { "range", 12 }, { "attack", 5 }, { "release", 400 } } },
                { "Trance Gate (key it)", { { "threshold", -30 }, { "range", 80 }, { "hysteresis", 2 }, { "attack", 1 }, { "hold", 0 }, { "release", 30 } } },
                { "Pump (duck to the kick)", { { "mode", 2 }, { "threshold", -30 }, { "range", 12 }, { "attack", 2 }, { "hold", 20 }, { "release", 220 }, { "sclpf", 500 } } },
                { "Voice-over Duck", { { "mode", 2 }, { "threshold", -36 }, { "range", 9 }, { "hysteresis", 6 }, { "attack", 30 }, { "hold", 400 }, { "release", 900 }, { "schpf", 150 } } } } },

            //----------------------------------------------------------------------------------
            { "pulseTransient", {
                { "Punchy Drums", { { "attack", 0.5f }, { "sustain", -0.1f }, { "length", 20 } } },
                { "Snappy Snare", { { "attack", 0.7f }, { "sustain", 0 }, { "length", 12 } } },
                { "Tight Kick", { { "attack", 0.35f }, { "sustain", -0.5f }, { "length", 25 } } },
                { "Dry Up The Room", { { "attack", 0.1f }, { "sustain", -0.8f }, { "length", 20 } } },
                { "Bring Up The Room", { { "attack", -0.1f }, { "sustain", 0.7f }, { "length", 20 }, { "output", -3 } } },
                { "Soften The Pick", { { "attack", -0.6f }, { "sustain", 0.15f }, { "length", 8 } } },
                { "Push It Back", { { "attack", -0.8f }, { "sustain", 0.3f }, { "length", 30 }, { "output", -1 } } },
                { "Bass Definition", { { "attack", 0.4f }, { "sustain", 0.1f }, { "length", 35 } } },
                { "Parallel Smack", { { "attack", 1.0f }, { "sustain", -0.3f }, { "length", 15 }, { "mix", 0.4f } } } } },

            //----------------------------------------------------------------------------------
            // De-esser: mode 0 split band, 1 wide band
            { "pulseDeEsser", {
                { "Gentle Vocal", { { "freq", 6500 }, { "threshold", -32 }, { "range", 5 } } },
                { "Bright Female Vocal", { { "freq", 8000 }, { "threshold", -34 }, { "range", 8 } } },
                { "Male Vocal", { { "freq", 5200 }, { "threshold", -32 }, { "range", 7 } } },
                { "Heavy Sibilance", { { "freq", 7000 }, { "threshold", -38 }, { "range", 14 }, { "release", 40 } } },
                { "Spoken Word (wide)", { { "freq", 6000 }, { "threshold", -30 }, { "range", 6 }, { "mode", 1 }, { "release", 90 } } },
                { "Cymbal Tamer", { { "freq", 9500 }, { "threshold", -28 }, { "range", 6 }, { "release", 120 } } },
                { "Harsh Guitar", { { "freq", 3500 }, { "threshold", -28 }, { "range", 6 }, { "release", 100 } } } } },

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
            // Prism EQ: types 0 bell, 1 low shelf, 2 high shelf, 3 low cut, 4 high cut, 5 notch, 6 band pass,
            // 7 tilt shelf, 8 all pass, 9 flat tilt; slope 0..8 = 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct;
            // place 0 stereo, 1 L, 2 R, 3 mid, 4 side; movement 1 LFO, 2 envelope; sync 1 1/16 .. 7 4 bars
            { "pulsePrismEQ", {
                { "Vocal Clarity", { { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 90 }, { "b1slope", 3 }, { "b3on", 1 }, { "b3freq", 300 }, { "b3gain", -3 }, { "b3q", 1.2f },
                                     { "b4on", 1 }, { "b4freq", 3200 }, { "b4gain", 2.5f }, { "b4q", 0.9f }, { "b6on", 1 }, { "b6type", 2 }, { "b6freq", 11000 }, { "b6gain", 3 } } },
                { "Dynamic De-Esser", { { "b3on", 0 }, { "b4on", 0 }, { "b5on", 1 }, { "b5type", 0 }, { "b5freq", 6500 }, { "b5q", 3 }, { "b5gain", 0 }, { "b5dyn", 1 },
                                        { "b5range", -9 }, { "b5thresh", -32 }, { "b5attack", 1 }, { "b5release", 60 } } },
                { "Kick Punch", { { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 28 }, { "b1slope", 6 }, { "b2on", 1 }, { "b2type", 0 }, { "b2freq", 60 }, { "b2gain", 4 }, { "b2q", 1.4f },
                                  { "b3on", 1 }, { "b3freq", 320 }, { "b3gain", -5 }, { "b3q", 2 }, { "b4on", 1 }, { "b4freq", 4000 }, { "b4gain", 3 }, { "b4q", 1.5f } } },
                { "Bass Tamer (dynamic)", { { "b3on", 1 }, { "b3type", 1 }, { "b3freq", 120 }, { "b3gain", 0 }, { "b3dyn", 1 }, { "b3range", -6 }, { "b3thresh", -24 },
                                            { "b3attack", 10 }, { "b3release", 150 }, { "b4on", 0 } } },
                { "Mid/Side Air", { { "b3on", 0 }, { "b4on", 0 }, { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 140 }, { "b1place", 4 }, { "b1slope", 1 },
                                    { "b6on", 1 }, { "b6type", 2 }, { "b6freq", 9000 }, { "b6gain", 3.5f }, { "b6place", 4 } } },
                { "Brighter Tilt", { { "b3on", 0 }, { "b4on", 0 }, { "b2on", 1 }, { "b2type", 9 }, { "b2freq", 1000 }, { "b2gain", 3 } } },
                { "Warmer Tilt", { { "b3on", 0 }, { "b4on", 0 }, { "b2on", 1 }, { "b2type", 9 }, { "b2freq", 1000 }, { "b2gain", -3 } } },
                { "Telephone", { { "b3on", 0 }, { "b4on", 0 }, { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 450 }, { "b1slope", 3 }, { "b6on", 1 }, { "b6type", 4 }, { "b6freq", 3200 },
                                 { "b6slope", 3 }, { "b2on", 1 }, { "b2freq", 1500 }, { "b2gain", 6 }, { "b2q", 1.2f } } },
                { "Wah Sweep (LFO)", { { "b3on", 1 }, { "b3type", 0 }, { "b3freq", 900 }, { "b3gain", 12 }, { "b3q", 4 }, { "b3modsrc", 1 }, { "b3modsync", 3 }, { "b3modfreq", 1.6f }, { "b4on", 0 } } },
                { "Auto-Wah (envelope)", { { "b3on", 1 }, { "b3type", 0 }, { "b3freq", 500 }, { "b3gain", 10 }, { "b3q", 3 }, { "b3modsrc", 2 }, { "b3modfreq", 2.5f },
                                           { "b3attack", 3 }, { "b3release", 150 }, { "b4on", 0 } } },
                { "Phaser Notches", { { "b3on", 1 }, { "b3type", 5 }, { "b3freq", 600 }, { "b3q", 3 }, { "b3modsrc", 1 }, { "b3modrate", 0.3f }, { "b3modfreq", 1.5f },
                                      { "b4on", 1 }, { "b4type", 5 }, { "b4freq", 1800 }, { "b4q", 3 }, { "b4modsrc", 1 }, { "b4modrate", 0.3f }, { "b4modfreq", 1.5f } } },
                { "Rhythmic Filter (1/8 square)", { { "b3on", 0 }, { "b4on", 0 }, { "b6on", 1 }, { "b6type", 4 }, { "b6freq", 1500 }, { "b6slope", 3 }, { "b6q", 2 },
                                                    { "b6modsrc", 1 }, { "b6modshape", 3 }, { "b6modsync", 2 }, { "b6modfreq", 2 } } },
                { "Resonant Low Cut 48", { { "b3on", 0 }, { "b4on", 0 }, { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 120 }, { "b1slope", 6 }, { "b1q", 3 } } },
                { "Master Polish", { { "b1on", 1 }, { "b1type", 3 }, { "b1freq", 22 }, { "b1slope", 5 }, { "b2on", 1 }, { "b2type", 1 }, { "b2freq", 70 }, { "b2gain", 1 },
                                     { "b3on", 1 }, { "b3freq", 400 }, { "b3gain", -1 }, { "b3q", 0.8f }, { "b4on", 0 }, { "b6on", 1 }, { "b6type", 2 }, { "b6freq", 12000 }, { "b6gain", 1.5f } } },
                { "Hum Remover 50 Hz", { { "b3on", 0 }, { "b4on", 0 }, { "b1on", 1 }, { "b1type", 5 }, { "b1freq", 50 }, { "b1q", 12 }, { "b2on", 1 }, { "b2type", 5 },
                                         { "b2freq", 100 }, { "b2q", 12 }, { "b3type", 5 } } } } },

            //----------------------------------------------------------------------------------
            // Pulse Compressor: character 0 clean, 1 punchy, 2 vintage, 3 opto, 4 bus
            { "pulseCompressor", {
                { "Vocal Leveler (opto)", { { "threshold", -24 }, { "ratio", 3 }, { "knee", 10 }, { "attack", 10 }, { "release", 300 }, { "character", 3 }, { "automakeup", 1 } } },
                { "Drum Punch", { { "threshold", -20 }, { "ratio", 4 }, { "knee", 4 }, { "attack", 25 }, { "release", 80 }, { "character", 1 }, { "makeup", 3 } } },
                { "Bus Glue", { { "threshold", -14 }, { "ratio", 2 }, { "knee", 8 }, { "attack", 30 }, { "release", 200 }, { "autorelease", 1 }, { "character", 4 }, { "makeup", 1.5f } } },
                { "Vintage Bass", { { "threshold", -22 }, { "ratio", 5 }, { "knee", 6 }, { "attack", 8 }, { "release", 120 }, { "character", 2 }, { "makeup", 4 } } },
                { "Parallel Smash", { { "threshold", -40 }, { "ratio", 20 }, { "knee", 0 }, { "attack", 1 }, { "release", 60 }, { "character", 1 }, { "makeup", 18 }, { "mix", 0.3f } } },
                { "Transparent Peak Control", { { "threshold", -10 }, { "ratio", 8 }, { "knee", 2 }, { "attack", 0.5f }, { "release", 60 }, { "lookahead", 2 }, { "character", 0 } } },
                { "Sidechain Duck (kick key)", { { "threshold", -30 }, { "ratio", 6 }, { "knee", 6 }, { "attack", 1 }, { "release", 140 }, { "schpf", 10 }, { "sclpf", 200 } } },
                { "De-Esser (listen to the SC)", { { "threshold", -30 }, { "ratio", 4 }, { "attack", 1 }, { "release", 60 }, { "schpf", 5000 }, { "range", 8 } } } } },
            // Multiband: modes 0 compress, 1 expand, 2 upward; bands 0 = 2, 1 = 3, 2 = 4 bands
            { "pulseMultiband", {
                { "Master Glue (4 bands)", { { "bands", 2 }, { "x1", 120 }, { "x2", 1000 }, { "x3", 6000 }, { "m1thresh", -20 }, { "m1ratio", 2 }, { "m2thresh", -22 }, { "m2ratio", 1.8f },
                                             { "m3thresh", -24 }, { "m3ratio", 1.8f }, { "m4thresh", -26 }, { "m4ratio", 2 } } },
                { "Tame the Lows", { { "bands", 0 }, { "x1", 150 }, { "m1thresh", -24 }, { "m1ratio", 4 }, { "m1attack", 20 }, { "m1release", 150 }, { "m2on", 0 } } },
                { "De-Harsh 2-5 kHz", { { "bands", 1 }, { "x1", 2000 }, { "x2", 5000 }, { "m1on", 0 }, { "m2thresh", -28 }, { "m2ratio", 4 }, { "m2attack", 2 }, { "m3on", 0 } } },
                { "Upward Detail", { { "bands", 1 }, { "x1", 200 }, { "x2", 4000 }, { "m1on", 0 }, { "m2mode", 2 }, { "m2thresh", -35 }, { "m2ratio", 2 },
                                     { "m3mode", 2 }, { "m3thresh", -40 }, { "m3ratio", 2 } } },
                { "Noise Gate Lows (expand)", { { "bands", 0 }, { "x1", 200 }, { "m1mode", 1 }, { "m1thresh", -45 }, { "m1ratio", 3 }, { "m2on", 0 } } } } },
            // Limiter: style 0 transparent, 1 punchy, 2 loud
            { "pulseLimiter", {
                { "Streaming -14 LUFS start", { { "gain", 4 }, { "ceiling", -1 }, { "truepeak", 1 }, { "style", 0 } } },
                { "Punchy Master", { { "gain", 6 }, { "ceiling", -0.5f }, { "truepeak", 1 }, { "style", 1 } } },
                { "Loud Club Master", { { "gain", 9 }, { "ceiling", -0.2f }, { "truepeak", 1 }, { "style", 2 } } } } },

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

#if JUCE_MSVC
 #pragma warning (pop)
#endif
