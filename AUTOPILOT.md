# Pulse Studio autopilot

The owner asked for Pulse Studio to keep improving automatically, in a cycle: finish the queued
work, then think of more improvements and do them, without asking. A scheduled task resumes
this work after usage-limit windows. **Read this whole file before doing anything.**

## Rules (always)

- Project: `C:\Users\cercea\IdeaProjects\pulse-studio` (C++ / JUCE 8 / Tracktion Engine, header-only `Source/`, Windows).
- Build only in `build-dev`: `cmake --build build-dev --config Release --target PulseStudio`.
  Never touch `build/` or the owner's installed copy. Test the app with `--dev-profile` only.
- Self-test: `"build-dev/PulseStudio_artefacts/Release/Pulse Studio.exe" --selftest <report.txt>`
  (exit code = number of failures). Every feature gets self-test checks; all must pass before a commit.
- Commit locally after each finished item (never push). End commit messages with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Use `git commit -F <file>` (PowerShell 5.1 mangles quotes).
- After each group: bump the version in `CMakeLists.txt` and `installer/PulseStudio.iss`, update `README.md`,
  build the installer: `"%LOCALAPPDATA%/Programs/Inno Setup 6/ISCC.exe" /DBuildDir=..\build-dev installer/PulseStudio.iss`.
- No new downloads or installs (no new libraries, pip packages, SDKs) without the owner's approval:
  use what is already in `external/` and the system. If an item needs a download, skip it and note why in the log.
- Never run anything destructive outside the repo; never change system or security settings.
- Before stopping (or if the context is getting full), commit work in progress that builds and passes, update the
  log below, and delete `.autopilot.lock`.

## Lock

`.autopilot.lock` (repo root, git-ignored) holds the time a session last worked. If it exists and was modified
less than 90 minutes ago, another session is active: **do nothing and stop.** Otherwise create/touch it
when you start, touch it after each step, and delete it when you stop.

## Queue (top first)

1. ~~**Audio editing (v0.17.0)**~~ (done):
   - Sample editor for audio clips: zoom, select a range, cut / normalise / fade in-out / reverse / silence the
     range (writes a new file next to the project, repoints the clip, undoable).
   - Transient slicing: slice an audio clip at transients (reuse `AudioToMidi` onset detection) to a new Drum Rack
     track (one slice per pad) plus a MIDI clip that replays the slices.
   - Warp modes per clip: pick the time-stretch mode from those the engine supports (`te::TimeStretcher::getPossibleModes`).
2. ~~**Instruments (v0.18.0)**~~ (done): the owner wants the Drum Rack to match Ableton's and go further:
   - Drum Rack: 128 pads in banks of 16 with bank switching, choke groups, velocity layers and round-robin per
     pad, per-pad start/end/ADSR/filter/pitch/reverse, per-pad mute/solo/volume/pan/sends to rack-internal
     returns, pad note remapping, copy/move/swap pads, drag samples and devices onto pads, macros for the rack,
     "extract pad to its own track", randomise kit.
   - Drum synth instrument (kick, snare, hats, clap, toms synthesised, no samples) usable on pads.
   - Multi-sample mapping for Simpler (key and velocity zones).
3. **Owner request (2026-10-02): "All of it" + massive UI upgrade (professional, Ableton-like but not a copy).**
   Use `Pulse Studio.exe --dev-profile --snapshots <dir>` to screenshot the main views and LOOK at them after UI work.
   - ~~v0.21 UI levels + colours~~ (done): shared LevelMeter (peak hold, clip LED); meters in track headers, session
     columns, master meter in the top bar, better mixer meters; track colour palette + inline rename, track
     numbers, master track row; clips use track colour; session view mixer footer.
   - ~~v0.22 Drag and drop~~ (done): reorder tracks by dragging headers, drag device cards to reorder / onto other tracks
     (Ctrl = copy), drag clips between arrangement and session slots, drag images + insertion indicators.
   - ~~v0.23 Look and feel~~ (done): browser with category sidebar (Instruments, Audio Effects, MIDI Effects, Drums,
     Plugins, Samples, Favourites, Places); piano roll notes in track colour with velocity shading; device and
     instrument displays polish; transport polish.
   - ~~v0.24 Editing~~ (done): audio quantize, MIDI tools (velocity editing, humanize, legato, strum, fit to scale),
     draggable clip fades, MIDI loop-record takes.
   - ~~v0.25 Mixing~~ (done): sidechain picker, mixer groups / VCA, LUFS + true-peak master meter and export
     normalisation, mixer snapshots.
   - ~~v0.26 Instruments/FX~~ (done): Pulse Reverb and Delay with displays, FM synth (Operator-like), granular mode in
     Simpler, vocoder, pitch correction.
   - ~~v0.27 Performance~~ (done; Push encoder MIDI-learn left for later): Push encoder MIDI learn + drum bank + step seq on remapped notes, follow actions,
     per-scene tempo, per-clip launch quantize, record session to arrangement.
   - ~~v0.28 Quality~~ (done: backups, stress render; startup / scan speed not changed): autosave history (last 20), faster startup / plugin scan, UI snapshot tests, playback stress test.
4. **Owner request 2 (2026-10-02): "Do all of that" + compact device cards.**
   - ~~v0.29 compact device cards (rows by height, pages with tabs, collapse, A/B, randomise, reset, copy/paste)~~ (done)
   - ~~v0.30 welcome screen~~ (done) (recent projects, demo, templates, getting started tips), shortcuts for the new features
   - ~~v0.31 owner request~~ (done): filter envelopes (full ADSR + amount) on every instrument filter, and every graph display
     interactive with draggable nodes mapped to the knobs (envelopes, filter curves, compressor / saturator / limiter
     curves, reverb, delay); the display follows the card page
   - ~~v0.32 background plugin scan; Push encoder MIDI learn~~ (done) - with progress; Push encoder MIDI learn
   - ~~v0.33 audio editing~~ (done): clip gain envelope, slip edit (Alt-drag content), crossfade editor; groove pool (extract groove)
   - ~~v0.34 content~~ (done): factory presets for Pulse devices, synth drum kits, project templates; wavetable draw editor
   - ~~v0.35 sharing~~ (done): export project as a single zip, crash report bundle, update checker (URL configurable)
5. **Owner request 3 (2026-10-02), do in this order:**
   - v0.46 pro dynamics like Prism EQ: Pulse Compressor (detailed display: level history, gain reduction, draggable
     transfer curve, character modes clean / punchy / vintage, sidechain filter, lookahead), Multiband dynamics (up to
     4 draggable bands over the Prism analyser), Limiter upgrade (waveform + GR history, loudness, true peak).
   - v0.47 flagship Wavetable: drag-and-drop modulation onto any knob, mod matrix, 4 drawable LFOs, warp modes,
     dual filter with routing, built-in FX, 50+ presets.
   - v0.48 recording: count-in, punch in/out, loop recording stacking takes, better metronome sounds, input meter
     with clip warning on armed tracks, latency-compensated monitoring.
   - v0.49 arranger: section track (Intro / Verse / Chorus ...) to move / copy / delete whole sections; session
     follow actions.
   - v0.50 export: MP3 / FLAC / OGG (only with codecs already in JUCE - no downloads), streaming loudness target,
     MIDI file export.
   - v0.51 drop audit: check EVERY drop target / insert path makes sense (no instruments on returns or the master,
     no MIDI effects where there is no instrument, audio files onto instrument tracks, devices onto groups ...);
     refuse with a clear message instead.
   - Then continue the cycle (item 6) with features the owner would like. If a usage limit stops the work, the
     scheduled task resumes it later.
6. Then **think of more improvements** and do them in a cycle: polish, reliability, performance, UI clarity,
   missing DAW features (e.g. sidechain UI improvements, audio quantize, tuner/spectrum devices, MIDI
   learn for Push encoders, better browser search/tags, recent projects, auto-update of README docs).
   Pick the most valuable next item, add it to this queue, implement it, test it, commit.
   - ~~v0.19.0 Utility / Tuner / Spectrum devices~~ (done)
   - ~~v0.20.0 browser search + favourites, File > Open recent~~ (done)
   - Next ideas: audio quantize (warp markers to transients on the grid), sidechain source picker on compressor cards, MIDI learn for Push encoders.

## Log

- 2026-10-02: v0.13 UI refresh, v0.14 plugin sandbox / CPU / background export, v0.15 command palette /
  shortcuts / capture MIDI / group freeze, v0.16 automation curves / pencil / modulators / clip envelopes.
  All self-tests pass (107). Next: queue item 1 (audio editing).
- 2026-10-02: v0.17.0 audio editing done (sample editor, slice to Drum Rack, warp modes incl. bundled Signalsmith stretch). 111 checks pass. Next: queue item 2 (instruments / Drum Rack overhaul).
- 2026-10-02: v0.18.0 instruments: Drum Rack rebuilt (128 pads / banks, Drum Pad device with 8 velocity / round-robin
  layers, synth source, choke groups, receive-note remap, per-pad envelope / filter / reverse; Pad Mixer with mute /
  solo / sends to two rack returns; move / swap / copy / extract pads; rack macros; synth kit; randomise kit; old racks
  migrate on load), Drum Synth instrument, Simpler multisample zones. 128 checks pass. Next: queue item 3 (cycle).
- 2026-10-02: v0.19.0 Utility (width / mono / bass mono / phase / channels), Tuner (YIN pitch, cents needle),
  Spectrum analyser. 134 checks pass.
- 2026-10-02: v0.20.0 browser search (recursive, background thread) and favourites, File > Open recent. 138 checks pass.
- 2026-10-02: v0.21.0 UI: shared LevelMeter (peak hold, clip light) in track headers, session column footers,
  top bar and mixer (dB scale, peak readout); 32-colour palette; track numbers; richer demo song (drums, bass,
  chords, lead, pad, session scenes); --snapshots mode. 139 checks pass. Note: snapshot playback depends on the
  audio device being free; meter logic is covered by a self-test instead.
- 2026-10-02: v0.22.0 drag and drop: tracks, devices (reorder / to other tracks, Ctrl copy), session clips, scene rename. 143 checks pass.
- 2026-10-02: v0.23.0 browser sidebar (collections / categories / places, one search box), piano roll notes and drum pad names. 143 checks pass.
- 2026-10-02: v0.24.0 MIDI tools, audio quantize (own onset detector; the engine transient detector returns none), MIDI record modes (overdub / takes / replace). 146 checks pass.
- 2026-10-02: v0.25.0 mixing: BS.1770 loudness (probe on master, export report), Glue sidechain + duck menu (needs guessSidechainRouting), linked faders, mixer snapshots. 149 checks pass.
- 2026-10-02: v0.26.0 Pulse Reverb (FDN), Pulse Delay, Vocoder, Pitch Correction, FM Synth, Simpler granular, device displays. 159 checks pass.
- 2026-10-02: v0.27.0 session arrangement record, scene tempo, per-clip launch quantisation, Push parameter / drum banks. 162 checks pass.
- 2026-10-02: v0.28.0 backup history (20, restore as copy), stress render test (16 heavy tracks at 4.4x real time). 164 checks pass. The "all of it" list is done; next: cycle (Push encoder MIDI learn, startup speed, more UI polish).
- 2026-10-02: v0.29.0 compact device cards (FM 1460 -> 360 px). 165 checks pass.
- 2026-10-02: v0.30.0 welcome screen, commands for MIDI tools / collapse all / snapshots / loudness reset. 165 checks pass.
- 2026-10-02: v0.31.0 Graphs.h (envelope / filter / dynamics editors with draggable nodes), paged displays, filter envelopes on Simpler / FM / Drum pads. 166 checks pass.
- 2026-10-02: v0.32.0 background plugin scan (out of process, new files only), Push encoder assignments. 167 checks pass.
- 2026-10-02: v0.33.0 clip gain envelope (rendered copy), slip edit, fade shapes, groove extraction (MIDI / audio). 169 checks pass.
- 2026-10-02: v0.34.0 53 factory presets, kit styles, 4 factory templates (per profile folder), drawn wavetables. 173 checks pass.
- 2026-10-02: v0.35.0 project zip export / import, crash report bundle, opt-in update check. 176 checks pass.
- Owner feedback (2026-10-02) -> v0.36: friendly track names from browser drops ("drumrackkit"), return track
  devices must be editable, big resizable device editor windows with detailed graphs and collapsible sections,
  better graph quality, piano roll note entry (grid-length notes, drag to size, easier resize).
- 2026-10-02: v0.36.0 done: plain track names, Pulse Reverb / Delay returns + devices button on mixer strips,
  DeviceEditor.h (big resizable editor per Pulse device: frames view, drum hits, GR history, parameter bars,
  foldable control groups), graphs with axis labels / glow when large, piano roll draw / edge resize. 180 checks pass.
- Owner feedback (2026-10-02) -> v0.37+: returns / master as slim rows in the arrangement, raw track names in old
  projects ("pulseDrumSynth"), no "- device chain" heading text, many more presets, then the v0.36 "what next" list
  (old-return upgrade, editability audit, MIDI fx / vocoder / utility graphs, VST frame, piano roll, arrangement
  polish, startup speed).
- 2026-10-02: v0.37.0 BusLanes.h (return / master rows with device chips, drop, +), Upgrade.h (old returns -> Pulse
  devices, raw names), auto-named tracks follow their instrument. 183 checks pass.
- 2026-10-02: v0.38.0 183 factory presets (FactoryPresetsMore.h, >= 4 per device incl. MIDI fx), DeviceViews.h (MIDI fx
  phrase preview via an offline copy, vocoder bands, Utility stereo field). 185 checks pass.
- 2026-10-02: v0.39.0 piano roll draw tool / Alt-drag copy / ghost notes / velocity bars, per-track lane heights,
  clip rename, audit fixes (master devices button, bus row volume, clickable instrument label). 187 checks pass.
- 2026-10-02: v0.40.0 VST frame (presets / programs, saved states, A/B with parameter snapshots, randomise + undo,
  searchable knob strip), StartupTimer + splash. Startup: ~2.0 s of 2.9 s is the audio interface opening
  (WASAPI, Komplete Audio 2) - driver cost; the rest of startup is ~0.5 s. 188 checks pass.
- Owner request (2026-10-02): all of the v0.40 "what next" list, 15+ selectable themes, and a very in-depth
  FabFilter-style 6-band filter / EQ with every filter type and movement.
- 2026-10-02: v0.41.0 themes: Theme tokens are mutable (20 themes incl. 3 light), hard-coded UI greys mapped to tokens,
  Appearance tab with preview cards, live switch (remaps colours set at construction + rebuilds views). 189 checks pass.
- 2026-10-02: v0.42.0 Prism EQ (PrismEQ.h DSP: 10 shapes, Butterworth cascades to 96 dB/oct, dynamic bands, LFO /
  envelope movement, St/L/R/M/S placement, solo, auto gain, pre/post analyser; PrismEQDisplay.h editor) + 15 presets.
  193 checks pass.
- 2026-10-02: v0.43.0 FactorySounds.h (113 files rendered on first run in ~1 s: kits, loops, multisamples; Simpler and
  sampled Drum Rack use them), automation lanes for returns and the master (AutomationLane takes any track). 196 checks pass.
- 2026-10-02: v0.44.0 mixer: MiniEQ per strip (click adds / opens Prism EQ), SendMatrix grid, narrow strips, folded groups;
  UndoHistory.h (menus on undo / redo, history window), Shortcuts tab in Settings. 199 checks pass.
  (The 0.43.0 installer in dist was rebuilt by mistake from the 0.44 code; the 0.44.0 one is current.)
- 2026-10-02: v0.45.0 sandboxed plugins open in the Pulse device editor (parameter groups of 16, plugin window button),
  A/B and your own presets in every device editor. The v0.42 "do all of these" list is done. 201 checks pass.
- 2026-10-02: v0.46.0 Dynamics.h: Pulse Compressor (5 characters, lookahead, SC filters / listen / external key), Multiband
  Dynamics (LR4 crossovers, flat sum, compress / expand / upward), Limiter moved here and upgraded (true peak, styles,
  link, LUFS + true peak readouts, level history); DynamicsDisplay.h views; 16 presets. 207 checks pass.
- Owner feedback (2026-10-02): dynamics / Prism EQ must look and work like their FabFilter counterparts (graph-first, few
  knobs, buttons not knobs for solo / mode etc.) and look very clean and pro. Done as a style (not a copy of their design).
- 2026-10-02: v0.47.0 flagship Wavetable (warp, filter 2 + routing, mod env, 4 LFOs incl. drawn, mod matrix with drag-onto-
  knob, drive / chorus / delay / reverb, 52 presets; old ids kept), graph-first full panels for Prism EQ / Compressor /
  Multiband / Limiter (floating band panels, bottom bar), ParamToggle / ParamChoice / ParamCombo instead of knobs for
  choices, --device-shots <dir> for quick UI pictures. 209 checks pass.
- 2026-10-02: v0.48.0 recording: punch in / out button (ruler shows the punch range), count-in in the record / metronome
  menus, 5 rendered metronome sounds + level + accent, input meter with clip warning on armed tracks (guarded against
  devices going away). Audio loop takes: the engine only splits audio loop passes into takes with Tracktion projects;
  not done (MIDI takes work). 210 checks pass.
- 2026-10-02: v0.49.0 Sections.h + SectionStrip (move / duplicate / delete whole song sections with their content, split at
  edges, sections shift with the edits; demo song has Intro / Build / Drop). Session follow actions already existed.
  Automation inside a moved section is not copied (clips only). 211 checks pass.
- 2026-10-02: v0.50.0 export: MP3 via Windows Media Foundation (Mp3EncoderWin.cpp, system libs only), "Streaming ready"
  (-14 LUFS + offline true-peak limiter to -1 dBTP, ExportTools.h), stems in MP3. FLAC / OGG / MIDI export already
  existed. 212 checks pass.
- 2026-10-02: v0.51.0 drop audit: PluginMenu::refusal / kindOf / allowedFor used by insertPlugin and placeDevice (all insert
  paths), filtered + menus, instrument replace, drum pad rules, audio files on instrument tracks -> new track (arrangement
  and session), chain presets load instruments first. 213 checks pass.
- Owner request (2026-10-02): better horizontal scrolling in the arrangement; the Wavetable synth's window must look very
  pro (Vital / Serum-like, not a copy), graph-first like the FabFilter-style panels. Next: v0.52.
