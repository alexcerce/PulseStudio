# Changelog

Every version of Pulse Studio, newest first. The README describes each feature in detail.

## 0.99.39 - 2026-10-06

- Video: a window that shows the project's film in step with the song (File menu)
- File menu: Notes

## 0.99.38 - 2026-10-06

- Effect Rack: chains of effects side by side, each with level, mute and listen-alone
- Drum Rack mixer: the level bars fill from the left (they filled from 0 dB, so a pad at 0 dB looked empty)

## 0.99.37 - 2026-10-06

- Controller mapping pages: eight pages of MIDI mappings, a mappings window, and page buttons learned from the controller

## 0.99.36 - 2026-10-06

- Track alternatives: several versions of a track's clips, one in use (track menu > Alternatives)
- Fixed: Flatten left some of the original clips behind on a track that had more than one

## 0.99.35 - 2026-10-06

- Window layouts: five ready-made ones and your own (Layout button on the lower tab bar)

## 0.99.34 - 2026-10-06

- A track's mapped macros appear as dials on its header when the track is made taller

## 0.99.33 - 2026-10-06

- Browser: previews in the song's key (Key button), beside previews in time

## 0.99.32 - 2026-10-06

- Notes for the project and for each track, in one window; tracks with notes are marked on their header

## 0.99.31 - 2026-10-06

- Reference song: A/B the mix against a finished song at matched loudness (Mastering menu on the master strip, or the Reference device)

## 0.99.30 - 2026-10-06

- Freeze under load: the CPU readout warns of an overload and freezes the heaviest track on a click, or by itself when playback stops

## 0.99.29 - 2026-10-06

- New device: Match EQ (learns this track and a reference, and applies the difference in linear phase)

## 0.99.28 - 2026-10-06

- Prism EQ: linear-phase modes (Linear and Deep) beside the natural one

## 0.99.27 - 2026-10-06

- VCA faders: group faders in the mixer that move their member tracks together

## 0.99.26 - 2026-10-06

- Mastering: a ready chain for the master and an assistant that sets the loudness (Master button on the mixer's master strip)
- Fixed: only two devices could be added to the master (the limit is now 24, and 48 on a track)

## 0.99.25 - 2026-10-06

- Session clips recorded into the arrangement now start and end on the launch grid (down to sixteenths), not only on whole beats

## 0.99.24 - 2026-10-06

- Warped audio clips show the hits in their audio as ticks; dragging one pins it with a warp marker and moves it

## 0.99.23 - 2026-10-06

- Clips: crop to the loop range, and consolidate several clips into one (Ctrl+Shift+J)

## 0.99.22 - 2026-10-06

- Step grid: the piano roll can show a drum clip as a grid of steps, a row per drum pad

## 0.99.21 - 2026-10-06

- Chord track: a strip of chords above the arrangement, progressions in any key, chords written out as notes, notes fitted to the chords, and the Scale effect can follow them

## 0.99.20 - 2026-10-06

- Arpeggiator: a drawn pattern of up to 16 steps (levels and rests), pattern length and swing
- Note Echo: the level of each repeat can be drawn

## 0.99.19 - 2026-10-06

- New MIDI effects: Note Length, Pitch, Random and Velocity

## 0.99.18 - 2026-10-06

- Simpler: Slice mode (cut a sample at its hits or into equal pieces, one key per slice), with slices made longer or shorter to fit a note length without changing their pitch

## 0.99.17 - 2026-10-06

- New instruments: Pulse Analog (two oscillators, a four-pole ladder filter) and Pulse String (a plucked, picked, struck or malleted string)

## 0.99.16 - 2026-10-06

- New instruments: Tonewheel Organ (drawbars, percussion, vibrato and chorus, rotary speaker) and Electric Piano (tine and reed)
- Fixed: Pulse Spectral could get louder without end with Freeze and Feedback together

## 0.99.15 - 2026-10-06

- Wavetable: a third oscillator (the B / C tabs on the second oscillator's card show it; it is silent until its Level is turned up, so existing sounds are unchanged)
- Wavetable: make a wavetable from any audio file (DRAW WAVE tab, "Import from audio..."): sixteen single cycles from across the file become the "Imported" table, which is stored in the project

## 0.99.14 - 2026-10-06

- New effect: Pulse Spectral (freeze, blur, pitch shift and a delay per frequency)

## 0.99.13 - 2026-10-06

- New effects: Beat Repeat and Looper, each with its own window and presets

## 0.99.12 - 2026-10-06

- New effects: Resonators (five tuned strings), Shimmer (a reverb that climbs in pitch) and Grain Delay, each with its own window and presets

## 0.99.11 - 2026-10-06

- New effect: Pulse Amp, five amplifiers and four speaker cabinets in one device

## 0.99.10 - 2026-10-06

- New effects: Frequency Shifter (with ring modulation), Redux (sample-rate and bit reduction) and Exciter, each with its own window and presets

## 0.99.9 - 2026-10-06

- New effects: Pulse Chorus (chorus, ensemble, vibrato), Phaser-Flanger and Auto Pan (auto-pan and tremolo), each with its own window and presets

## 0.99.8 - 2026-10-06

- New effect: Pulse Space, a convolution reverb with twelve built-in spaces; any audio file can be loaded as a space

## 0.99.7 - 2026-10-05

- New effect: Pulse Gate (gate, expander and ducker with hysteresis, hold, lookahead and a filtered key)
- New effect: Transient Shaper (attack and sustain of every hit, up or down)
- New effect: De-esser (split band or wide band)
- Each has its own window to work in directly, and presets
- Fixed: a crash in the Drum Rack when clicking Clear, Spread velocity or the pad's synth switch

## 0.99.6 - 2026-10-05

- The Drum Synth has its own window

## 0.99.4 - 2026-10-05

- Fixed: bouncing, freezing a group or exporting while the audio engine was live could crash or glitch; the project is now taken off the engine for the duration of a render
- Fixed: a rare click or crash in the Delay and Reverb (a read just past the end of the delay line)
- Mixer strips in the card look (0.99.3)

## 0.99.2 - 2026-10-05

- Fixed: a crash in the piano roll after undoing the creation of the clip it was showing (or deleting its track)
- Fixed: splitting exactly at the start or end of a clip could close the app

## 0.99.1 - 2026-10-05

- Automation inside a song section moves and duplicates with the section

## 0.98.0 - 2026-10-05

- Mute and solo can be undone
- File > User guide (F1)

## 0.97.0 - 2026-10-05

- Uses about half the processor when nothing is happening: controls redraw only when they change, effects rest on silence, audio worker threads sleep when idle

## 0.96.0 - 2026-10-05

- Fixed: undoing a tempo change left the loop on the wrong beats
- Presets and device chains saved by older versions load correctly (0.95)
- Device windows fit smaller screens (0.95)

## 0.94.0 - 2026-10-05

- Fixed: Undo and Redo after moving a knob or fader put the saved value back but left the knob (and the sound) where it was. They now move knobs, faders, pans and sends

## 0.93.0 - 2026-10-05

- Device windows: presets grouped by kind of sound, previous / next buttons, and the name of the current preset

## 0.92.0 - 2026-10-05

- Fixed: a Saturator or Auto Filter whose Type had been changed could be missing after reopening the project. Affected projects are repaired when opened (a copy of the original is kept next to them)
- A project double-clicked while Pulse Studio is running opens in the running app
- Wavetable: an Output knob after the effects; presets play at more even levels

## 0.91.0 - 2026-10-05

- A damaged project file is left untouched (an empty project opens instead, with a pointer to the backups)
- Instrument presets play at more even levels

## 0.90.0 - 1.0 release candidate 1

- About box (File menu), third-party licences file, this changelog
- Projects open when you double-click them (the installer can link the file type to Pulse Studio)
- No memory is allocated on the audio thread by the Saturator, Pitch Correction or the instruments' MIDI handling
- Device cards and Drum Rack pads in the look of the device windows; on / off settings are buttons and choices are menus

## 0.60.0 - 2026-10-04

- Device cards and Drum Rack pads in the new look; choices are menus, not knobs

## 0.59.0 - 2026-10-04

- Simpler window

## 0.58.0 - 2026-10-04

- Sweep arcs on cutoff knobs; no controls lost in the new windows

## 0.57.0 - 2026-10-03

- FM Synth window

## 0.56.0 - 2026-10-03

- EQ Six display polish

## 0.55.0 - 2026-10-03

- Wavetable overhaul (engine and tabbed window)

## 0.54.0 - 2026-10-03

- One pro look for every device window, new displays, Wavetable refinements

## 0.53.0 - 2026-10-02

- Graph-first Reverb, Delay and Saturator editors

## 0.52.0 - 2026-10-02

- Arrangement overview / scrolling, pro Wavetable panel

## 0.51.0 - 2026-10-02

- Drops and inserts that make sense

## 0.50.0 - 2026-10-02

- MP3 export, streaming-ready loudness

## 0.49.0 - 2026-10-02

- Song sections

## 0.48.0 - 2026-10-02

- Recording workflow

## 0.47.0 - 2026-10-02

- Flagship Wavetable, graph-first pro editors, buttons for choices

## 0.46.0 - 2026-10-02

- Pro dynamics - Pulse Compressor, Multiband Dynamics, Limiter upgrade

## 0.45.0 - 2026-10-02

- Pulse frame for sandboxed plugins, A/B and presets in every device editor

## 0.44.0 - 2026-10-02

- Version number and log

## 0.43.0 - 2026-10-02

- Built-in sound library, return / master automation

## 0.42.0 - 2026-10-02

- Prism EQ, a six-band precision EQ

## 0.41.0 - 2026-10-02

- 20 colour themes, selectable in settings

## 0.40.0 - 2026-10-02

- Plugin frame for VSTs, startup splash and timings

## 0.39.0 - 2026-10-02

- Piano roll tools, lane heights, clip rename, audit fixes

## 0.38.0 - 2026-10-02

- 183 factory presets, MIDI effect / vocoder / utility views

## 0.37.0 - 2026-10-02

- Return / master rows in the arrangement, old project upgrades

## 0.36.0 - 2026-10-02

- Big device editors, better graphs, note entry, editable returns

## 0.35.0 - 2026-10-02

- Project zip, crash report bundle, opt-in update check

## 0.34.0 - 2026-10-02

- Factory content and drawn wavetables

## 0.33.0 - 2026-10-02

- Clip gain envelopes, slip edit, fade shapes, groove extraction

## 0.32.0 - 2026-10-02

- Background plugin scan, Push encoder assignments

## 0.31.0 - 2026-10-02

- Draggable graphs and filter envelopes

## 0.30.0 - 2026-10-02

- Welcome screen and more commands

## 0.29.0 - 2026-10-02

- Compact device cards

## 0.28.0 - 2026-10-02

- Quality - backup history, stress render test

## 0.27.0 - 2026-10-02

- Performance - arrangement record, scene tempo, launch quantisation, Push banks

## 0.26.0 - 2026-10-02

- Instruments and effects - FM synth, reverb, delay, vocoder, pitch correction, granular

## 0.25.0 - 2026-10-02

- Mixing - loudness metering, sidechain ducking, linked faders, snapshots

## 0.24.0 - 2026-10-02

- Editing - MIDI tools, audio quantize, MIDI record modes

## 0.23.0 - 2026-10-02

- Browser sidebar, piano roll look

## 0.22.0 - 2026-10-02

- Drag and drop for tracks, devices and session clips

## 0.21.0 - 2026-10-02

- UI - level meters everywhere, track colours and numbers

## 0.20.0 - 2026-10-02

- Browser search and favourites, Open recent

## 0.19.0 - 2026-10-02

- Utility, Tuner and Spectrum devices

## 0.18.0 - 2026-10-02

- Instruments - Drum Rack overhaul, Drum Synth, Simpler multisample

## 0.17.0 - 2026-10-02

- Audio editing - sample editor, slice to Drum Rack, warp modes

## 0.16.0 - 2026-10-02

- Automation - curves, pencil, modulators, clip envelopes

## 0.15.0 - 2026-10-02

- Workflow - command palette, custom shortcuts, capture MIDI, group freeze/flatten

## 0.14.0 - 2026-10-02

- Performance - plugin sandbox, per-track CPU, background exports

## 0.13.0 - 2026-10-02

- UI refresh - vector icons, Segoe UI, restyled controls

## 0.12.0 - 2026-10-01

- Sharing - Collect All and Save, project templates

## 0.11.0 - 2026-10-01

- MIDI editing - piano roll scales and fold, MIDI effects, MPE

## 0.10.0 - 2026-10-01

- Arrangement - markers, tempo lane, time signatures, take comping, auto-crossfades

## 0.9.0 - 2026-10-01

- Mixing - limiter, pre/post sends, latency readout, freeze buttons, Push sends to all returns

## 0.8.0 - 2026-10-01

- Stability - crash reports, plugin blocklist, safe mode, per-project view, undo grouping

## 0.7.0 - 2026-10-01

- Audio to MIDI, Demucs stem separation, ARA hosting

## 0.6.0 - 2026-10-01

- Performance - Push 2 display + step sequencer, follow actions, Ableton Link

## 0.5.0 - 2026-10-01

- Sound - Wavetable synth, Simpler, EQ Six, Glue Compressor, Saturator, Auto Filter, groove

## 0.4.0 - 2026-10-01

- Workflow - clip looping, warp markers, keyboard editing, MIDI export, track presets

## 0.3.1 - 2026-10-01

- Native DAW on JUCE + Tracktion Engine
