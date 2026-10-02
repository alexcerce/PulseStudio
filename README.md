# Pulse Studio

A native Windows DAW built with C++, [JUCE](https://juce.com) and [Tracktion Engine](https://github.com/Tracktion/tracktion_engine). It hosts VST3 plugins, runs on ASIO audio interfaces and supports Ableton Push 2.

## Install

Run `dist/PulseStudio-0.10.0-Setup.exe`. It installs for your user only (no admin needed) or for all users, and adds a Start-menu entry and an optional desktop shortcut.

## Build

You need Visual Studio 2022 Build Tools (C++ workload) and CMake. Then:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

The app is built to `build/PulseStudio_artefacts/Release/Pulse Studio.exe`. The first build compiles JUCE and Tracktion Engine, so it takes a few minutes. Later builds only recompile `Source/`.

To build the installer, use [Inno Setup 6](https://jrsoftware.org/isinfo.php):

```bash
ISCC.exe installer\PulseStudio.iss
```

The installer is written to `dist/`. Add `/DBuildDir=..\build-dev` to package a different build folder.

## First run

1. **Audio Settings → Audio / MIDI**: pick your interface. Choose **ASIO** for the lowest latency, then set the buffer size.
2. **Audio Settings → Recording**: set the count-in, MIDI overdub and a per-input recording offset.
3. **Plugins**: click *Options → Scan for new or updated VST3 plugins*. Scanning runs in a separate process, so a crashing plugin is skipped instead of taking the app down.
4. Press **Space** to play the demo song.

Projects are saved as `.tracktionedit` files in `Documents/Pulse Studio/`. Presets go in `Documents/Pulse Studio/Presets/`, and bounces in a `Bounces` folder next to the project.

## Features

### Arrangement
- **Tracks:** instrument, audio and **group** tracks. Select tracks with Ctrl-click, then press Ctrl+G to group them into a submix; ▾ collapses a group.
- **Selection:** drag on empty lane space for a **rubber band** (Shift adds to the selection), or Ctrl/Shift-click clips. Selected clips move together.
- **Editing:** cut, copy and paste at the playhead (Ctrl+X/C/V), duplicate (Ctrl+D), **split at the playhead** (Ctrl+E), select all (Ctrl+A).
- **Loop-range edits:** right-click the ruler to duplicate the region, insert silence, delete time (closing the gap), or bounce the region.
- **Render:** right-click a track header to **freeze/unfreeze**, **flatten** (replace with audio), or **bounce to a new audio track**. Ctrl+J bounces the selected clips' range.
- **Automation:** the **A** button opens a lane for any parameter. Click to add points and drag to move them; right-click removes one.
- **Audio clips:** drag the fade handles; double-click for gain, pitch, fades, reverse and **warp to tempo**.
- **Markers:** the strip above the ruler holds song markers (verse, chorus...).
  - Double-click the strip or press **Ctrl+K** to add one at the playhead.
  - Click a marker to jump to it, drag to move it, and double-click to rename it.
  - **Ctrl+←/→** jumps to the previous or next marker.
- **Tempo and time signature changes:**
  - **Tempo** (above the track headers) opens the tempo lane: double-click to add a tempo change, drag points up or down (BPM) and sideways (position), and right-click to delete one.
  - Right-click the marker strip to add a time signature (e.g. 3/4, 7/8) at that bar.
  - The ruler, grid and LCD follow both.
- **Takes and comping:**
  - Loop-recorded MIDI takes appear under right-click → *Takes*.
  - For audio, select several recorded passes and choose *Combine into takes*.
  - Then use the track menu → *Show take lanes*. Each take gets a row under the clip. Click a take to use it for the whole clip, or drag across part of it to use that take for just that range: the clip is split there.
- **Crossfades:** overlapping audio clips crossfade automatically (the **X-fade** button toggles this). Moving them apart removes the fades again.
- **Keyboard editing:** the arrow keys move the selected clips. ←/→ move by the grid (Shift = 1 bar) and ↑/↓ move them to the track above or below. **Z** zooms to the selection (Shift+Z = whole song), **+**/**−** zoom, and **Ctrl+L** loops the selection.
- **Clip looping:** right-click a MIDI or audio clip → *Loop clip*, then drag the right edge (or use *Clip length*) to repeat it. For example, loop a 1-bar clip across 8 bars. *Loop length* sets the looped region; dashed lines mark each repeat.
- **Warp markers** (audio): right-click → *Warp markers*. Double-click the strip at the bottom of the clip to add a marker, drag markers to move audio onto the beat (Alt = no snap), and right-click a marker to delete it. You can also add markers at the playhead or at every detected transient.

### Audio tools (right-click an audio clip)
- **Convert to new MIDI track:** puts a new track under the clip with a MIDI clip lined up to the audio.
  - **Drums:** onsets become kick, snare and hi-hat notes on a Drum Rack.
  - **Melody:** monophonic pitch tracking, played by a Wavetable synth.
  - **Harmony:** chords and polyphony, played by a Wavetable synth.
- **Separate stems (Demucs):** splits the clip into drums, bass, vocals and other with Meta's open-source Demucs model. The stems arrive as a group of four tracks lined up with the clip, and the original clip is muted.
  - The first use offers a one-time setup: a private Python environment with PyTorch (CPU) and Demucs, about 1.5 GB, in `Documents/Pulse Studio/Tools/stems-env`. Your system Python is left alone. It needs Python 3.9-3.12 from python.org.
  - The first separation downloads the model (about 80 MB).
  - Expect roughly 1-2× the song's length on a typical CPU.
- **ARA (Melodyne, etc.):** *ARA → Edit with …* hands the clip to an ARA plugin, and double-clicking the clip opens its editor. Edits are saved with the project, and *Stop using ARA* returns to normal playback. ARA plugins appear after a VST3 rescan (*Plugins → Options → Scan*).

### Session view (Tab)
- An Ableton-style clip launcher with scenes and launch quantisation. Clips loop until stopped.
- **Session recording:** on an armed track, click an empty slot (shown with a red dot) to record a new clip into it. Right-click any empty slot → *Record into this slot*.
- *Back to arrangement* (or a track's **Arr** button) hands tracks back to the timeline.
- **Follow actions:** right-click a clip → *Follow action* to choose what happens after it has played for N loops or bars: next, previous, first, last, any, other, round robin, play again or stop. Add a *second action* with a chance (e.g. 75% next / 25% any) for generative arrangements. A ↪ marks clips that have one.

### Ableton Link
Click **Link** in the transport bar to join a Link session with other apps and devices on your network (Ableton Live, Traktor, phone apps and so on).
- Tempo changes go both ways.
- Bars line up: playback is nudged to stay in phase.
- Start/stop is shared.

The button's tooltip shows how many peers are connected. The first time you turn it on, Windows may ask whether Pulse Studio can use the network. Allow private networks.

### Ableton Push 2
Plug it in and it connects automatically (hot-plug supported). The Push's area is outlined in red in the session view, and **PUSH** appears next to the CPU meter, whose tooltip shows the current mode.

| Control | Action |
| --- | --- |
| **Session** button | 8×8 grid = clips. A pad launches a clip; on an armed track an empty pad records into the slot; otherwise it stops the track. Hold **Delete** + pad deletes a clip. |
| Scene buttons (right column) | Launch scenes |
| Upper row | Session: stop the track. Device mode: pick the device |
| Lower row | Select a track (and route MIDI to it) |
| Arrows | Move the 8×8 bank (Shift = 8 at a time). Note mode: ←/→ selects the track, ↑/↓ changes octave |
| **Note** button | Play the selected track: in-key 4ths layout, or a 4×4 **drum layout** for Drum Rack tracks. Velocity, aftertouch and the touch strip (pitch bend) are passed through. |
| Octave ↑/↓, **Scale** | Octave; cycle the scale (Shift + Scale = root note) |
| **Device** / **Mix** | Encoders control the selected device's parameters (Page ◀ ▶ changes device), or the bank's volume → pan → send A → send B (press Mix again to cycle) |
| Tempo / master encoders | Tempo (Shift = fine) / master volume |
| **Layout** (drum tracks, Note mode) | **Step sequencer**: the top 4 rows are 32 sixteenth-note steps for the selected drum (tap to add or remove), the bottom-left 4×4 plays and selects drums, and the bottom-right 4×4 picks 2-bar pages. Pressing a page past the end lengthens the clip. Steps go into the selected clip, else the clip under the playhead, else a new 2-bar clip. |
| Play, Record, Metronome, Undo (Shift = redo), Duplicate, Quantize, Tap Tempo, Mute, Solo, Browse, Clip | What you'd expect |

**Display:** the Push screen shows the eight encoder values (device parameters or the mix) with arcs, plus the upper-button functions, track names and the current mode. The display uses USB through libusb and needs the Push 2's WinUSB driver. That driver installs with Ableton Live, or Windows installs it automatically on most systems. The PUSH status tooltip says whether the display is connected.

### Controllers and MIDI
- **MIDI Learn:** right-click any knob, fader or pan → *MIDI Learn...*, then move a control on your hardware (Esc cancels). Mappings are saved in the project; an orange dot marks mapped knobs.
- The selected track automatically receives live input from every connected MIDI controller.
- **Piano roll** controller lanes: velocity, pitch bend, mod wheel, volume, pan, expression and sustain.

### Instruments and effects
Pulse's own devices have cards with labelled sections and a live display. Every knob can be automated, MIDI-learned and mapped to a macro. Knobs follow the parameter's natural scale (for example, 1 kHz sits at the middle of a frequency knob).

| Device | What it does |
| --- | --- |
| **Wavetable** | Two wavetable oscillators. There are five table sets (basic shapes, pulse width, harmonics, vowels and sync saw), and **Position** morphs through each set. Also has up to 7-voice unison with detune and stereo spread, a sub oscillator, a 12/24 dB low-pass, band-pass or high-pass filter with its own envelope, an amp envelope, an LFO to position and cutoff, and glide. Tables are band-limited per octave, so high notes don't alias. |
| **Simpler** | A one-sample instrument. Drag start, end and loop-start markers on the waveform, and loop with a crossfade. Also has root note, transpose and detune, ADSR, a filter, and velocity, gain and pan. Modes are Classic, One-shot and Mono. Drop a file from the browser or Explorer onto the waveform, or click *Load*. |
| **EQ Six** | A 6-band EQ: low and high cut (24 dB/oct), shelves, bells and notch. Drag the numbered nodes on the curve; scroll over a node to change Q, and double-click one to turn it on or off. A live spectrum shows behind the curve. |
| **Glue Compressor** | A bus compressor with stepped attack and release (including Auto), 2:1, 4:1 and 10:1 ratios, a range limit, makeup gain, dry/wet and soft clip. It has a gain-reduction meter. |
| **Saturator** | Seven shapes (analog, tape, tube, hard clip, foldback, sine fold and bit crush) with 4× oversampling, bias, a tone tilt and dry/wet. It shows the transfer curve. |
| **Auto Filter** | A low-pass, band-pass, high-pass, notch or 24 dB low-pass filter with drive. The LFO is free-running or tempo-synced (1/16 to 4 bars) with six shapes and a stereo phase offset. An envelope follower and a dry/wet control round it out. |

**Groove / swing:** right-click a MIDI clip → *Groove* to pick a template (e.g. *Basic 8th Swing*, *Basic 16th Swing*) and an amount. The groove is applied as the clip plays. *Commit groove to notes* moves the notes for real.

### Sound design
- **Drum Rack:** from the *+ Inst* menu, the browser or *+ Add device*.
  - 16 pads; drag samples onto them (several files fill consecutive pads). Click a pad to audition it.
  - Each pad has its own gain, pan and tune, plus its own **effects chain** (*+ Pad FX*).
- **Presets:**
  - **Track presets:** right-click a track header → *Save track preset*. This saves the instrument, effects (Drum Rack samples included) and mixer settings, but no clips. Saved presets appear under *Track presets* in the browser's Devices tab; drag one into the arrangement, or double-click it, to create a new track.
  - **Device presets:** the ☰ button on any device saves and loads its full state (VST3s included).
  - **Chain presets:** saves and loads the whole device chain. Chain presets also appear in the browser, so you can drag one onto a track.
- **Macros:** 8 per track. Right-click any device knob → *Map to Macro*; a blue dot marks mapped knobs.
- **Browser:** files with preview, plus **Sync**. While the song plays, loops are time-stretched to the project tempo and start on the next bar. Tempo is detected from the file's tag, "120bpm" in the name, or the loop length.

### Mixer and routing
- Faders, pan, meters and mute/solo, plus submix strips for groups.
- **Return tracks** with **sends** on every track, and **sidechain** inputs (the **SC** button on devices that support it).
- **Pre/post-fader sends:** right-click a send knob. Pre-fader sends get an orange label and keep sending when the fader is down, which is useful for headphone mixes and parallel effects.
- **Latency:** each strip shows its plugin latency. Pulse compensates automatically, so everything stays in time.
- **Freeze (F):** renders a track to audio and switches off its plugins, to save CPU with heavy VST instruments. Click F again to unfreeze.
- **Master Limiter:** a button on the master strip adds a look-ahead brickwall limiter before the master meter. Its Gain, Ceiling and (auto) Release are in the master device chain, and it's also available as a device on any track.
- **Push 2 Mix mode:** press Mix repeatedly to cycle volume → pan → a send to every return (Shift+Mix goes back).

### Export (File menu)
- **Export audio / stems** (Ctrl+Shift+R):
  - WAV, AIFF, FLAC or OGG; 16/24/32-bit float; 44.1–96 kHz.
  - Normalise to a peak or to **LUFS**.
  - Range: whole song, loop range or the selected clips.
  - **Stems:** one file per track and return.
- **Export selection** (Ctrl+Shift+E): exports just the selected clips' time range (or the loop range).
- **Export MIDI** (Ctrl+Shift+M): writes the selected MIDI clips, or every MIDI clip, to a `.mid` file. You get one track per Pulse track with the tempo map, and looped clips are written out in full. A track header's right-click menu exports that track's clips.

### Safety
- **Autosave every minute**, with recovery after a crash.
- **Crash reports:** if Pulse Studio crashes, it saves a log with a stack trace. It also notes which plugin the crash happened in, or which plugin was loading at the time.
- **After a crash:** the next launch offers to *disable that plugin*, start in **safe mode**, or continue. Disabled plugins aren't loaded, but their settings stay in your projects. Re-enable them in **Audio Settings → Safety**, which also opens the crash-log folder.
- **Safe mode:** hold **Shift** while starting, or run with `--safe-mode`. No plugins load, and the title bar says so.
- **Per-project view:** zoom, scroll, snap, arrangement/session view, browser, bottom panel, panel size, selected track and collapsed groups are saved in the project. The window size and position are remembered too.
- **Undo:** each knob drag is one undo step, and the Undo/Redo tooltips name the action.

## Shortcuts

| Key | Action |
| --- | --- |
| Space | Play/stop |
| Tab | Arrangement ↔ Session |
| B | Show/hide browser |
| R / L / M | Record / Loop / Metronome |
| Home | Return to start |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save |
| Ctrl+C / X / V | Copy / cut / paste at the playhead |
| Ctrl+D | Duplicate the selected clips |
| Ctrl+E | Split at the playhead |
| Ctrl+A | Select all clips (all notes in the piano roll) |
| Ctrl+G | Group the selected tracks |
| Ctrl+J | Bounce the selected clips' range to a new track |
| ← / → | Move the selected clips by the grid (Shift = 1 bar) |
| ↑ / ↓ | Move the selected clips to the track above / below |
| Z / Shift+Z | Zoom to the selection / whole song |
| + / − | Zoom in / out |
| Ctrl+L | Loop the selected clips |
| Ctrl+K | Add a marker at the playhead |
| Ctrl+← / → | Previous / next marker |
| Ctrl+Shift+R / E / M | Export audio / export selection / export MIDI |
| Delete | Delete the selected clips (or notes in the piano roll) |
| 1 / 2 / 3 | Devices / Mixer / Piano Roll |
| Ctrl+wheel / Shift+wheel | Zoom / scroll the timeline |
| Esc | Cancel MIDI Learn |

## Command line

- `--render out.wav` renders the last project offline and exits.
- `--selftest report.txt` builds a scratch project and runs 89 checks (including a real Demucs separation when it's set up), including an audit of every built-in device, rendering audio to check the results, then writes a pass/fail report. The exit code is the number of failures.
- `--dev-profile` uses separate settings and a separate projects folder, for testing builds without touching your real projects.

## Code layout

All app code is header-only in `Source/` and compiled through `Main.cpp`.

| File | Contents |
| --- | --- |
| `Main.cpp` | App, window, engine setup, out-of-process plugin scanning, command line |
| `MainComponent.h` | Project lifecycle, layout, shortcuts, autosave, demo song, dialogs |
| `TransportBar.h` | Transport, tempo/position, view toggles, file menu, Push status |
| `ArrangementView.h` | Ruler, track and group rows, clips (fades, multi-select), rubber band, drag & drop |
| `AutomationLane.h` | Automation lanes |
| `SessionView.h` | Clip launcher, scenes, session recording |
| `Push2.h` | Ableton Push 2 controller (modes, step sequencer, display drawing) |
| `Push2Display.h`, `Push2Usb.cpp` | Push 2 display over USB (libusb) |
| `LinkSync.h` | Ableton Link session |
| `AudioToMidi.h` | Drum / melody / harmony detection and conversion |
| `Stems.h` | Demucs setup, separation and stem import |
| `Browser.h` | File browser with synced preview, device list |
| `PianoRoll.h` | MIDI editor and controller lanes |
| `DeviceChain.h` | Device rack: device cards, macros, Drum Rack editor, presets |
| `DrumRack.h`, `Presets.h`, `Macros.h`, `MidiLearn.h` | Sound design and controller features |
| `Timeline.h` | Markers, tempo / time signature maths, crossfades, comping |
| `EditingOps.h` | Clipboard, split, time edits, bounce/flatten/freeze, grouping |
| `Workflow.h` | Keyboard nudging, clip looping, warp markers, MIDI export, track presets, groove |
| `PulseDevices.h`, `PulseSynths.h` | Pulse's own devices (EQ, Glue, Saturator, Auto Filter, Wavetable, Simpler) |
| `DeviceDisplays.h`, `ParamKnob.h` | Device card displays and knobs |
| `ExportDialog.h`, `AudioSettings.h` | Export and audio/recording settings |
| `Mixer.h`, `Routing.h` | Mixer strips, sends/returns |
| `ClipImport.h`, `ClipProperties.h` | File import, audio clip panel |
| `CrashGuard.h`, `CrashGuardWin.cpp` | Crash reports, plugin blocklist, safe mode |
| `PluginWindow.h`, `PluginMenu.h` | Plugin windows, task runner, plugin menu |
| `SelfTest.h` | `--selftest` |
| `PulseLookAndFeel.h` | Dark theme |

## Licensing

Third-party code is fetched into `external/` by `scripts/get-dependencies.ps1`: Tracktion Engine with JUCE, Ableton Link (GPLv2+), libusb (LGPL-2.1) and the ARA SDK (Apache 2.0). Demucs (MIT) and PyTorch (BSD) are installed on demand into their own Python environment and run as a separate process. JUCE (AGPLv3) and Tracktion Engine (GPLv3) are used under their open-source licences, so this project must stay GPL-compatible. Selling a closed-source build would require commercial licences for both.
