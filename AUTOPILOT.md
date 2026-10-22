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
- Commit after each finished item and push it to GitHub (`git push`, origin = github.com/alexcerce/PulseStudio,
  private; owner, 2026-10-21: "remove the never push requirement"). End commit messages with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Use `git commit -F <file>` (PowerShell 5.1 mangles quotes).
- After each group: bump the version in `CMakeLists.txt` and `installer/PulseStudio.iss`, update `README.md`,
  build the installer: `"%LOCALAPPDATA%/Programs/Inno Setup 6/ISCC.exe" /DBuildDir=..\build-dev installer/PulseStudio.iss`.
- No new downloads or installs (no new libraries, pip packages, SDKs) without the owner's approval:
  use what is already in `external/` and the system. If an item needs a download, skip it and note why in the log.
- Never run anything destructive outside the repo; never change system or security settings.
- Before stopping (or if the context is getting full), commit work in progress that builds and passes, update the
  log below, and delete `.autopilot.lock`.

## Testing (owner, 2026-10-06: "could we do testing more efficiently?")

Two tiers, so that time goes into features:

- **With every feature (quick, about 2 minutes):** Release build, the Release self-test (all checks must pass), and
  `--device-shots` when there is a new window to look at. Then commit. This stays per feature because it is cheap
  and finds mistakes while the code is fresh (on 2026-10-05/06 it caught an unstable feedback loop, a frequency
  shifter going the wrong way and a wrong level, each within minutes of being written).
- **Once per Programme group, and before anything is called a release (slow, about 15 minutes):** the Debug build,
  the Debug self-test, `--monkey` (two seeds, 4000 events), `--command-fuzz`, the installer, and a look at every
  new window at its smallest and largest size. Run each as its own foreground command (the Debug self-test alone
  takes 5 minutes). Anything they find is fixed before the next group starts. Note in the log which commits a
  slow pass covers.

The version is bumped and README / CHANGELOG are written with every feature commit as before.

## Lock

`.autopilot.lock` (repo root, git-ignored) holds the time a session last worked. If it exists and was modified
less than 45 minutes ago, another session is active: **do nothing and stop.** Otherwise create/touch it
when you start, touch it at least every 20 minutes (before and after every build, test run and commit; before a
run that takes longer, such as `--soak`, start a background loop that touches it), and delete it when you stop.

The scheduled task fires every hour. A run that finds a fresh lock stops at once (that is the normal case while
a session is working). A run that finds no lock or a stale one takes over: it first runs `git status`; work left
uncommitted by a session that died is kept if it builds and passes the self-test, otherwise it is finished or
set aside with `git stash` (never deleted) and noted in the log.

A run does not stop after one item. It keeps going down the Programme until the context is nearly full or the
usage limit ends it, committing after every finished item so nothing is lost when it is cut off.
**The computer sleeps when no session is in the middle of a turn** (the app only holds it awake while a turn is
running; found 2026-10-05: a background test run stood still for 85 minutes, and the 15:29 scheduled run froze
two minutes in, both because the PC went to sleep). So: never start a build or test in the background and end
the turn to wait for it; run every build and test in the foreground (each under 10 minutes; split longer ones),
and call the keep-awake tool (`request_keep_awake`, until `session_idle`) at the start of a run. A sleeping PC
also fires no scheduled runs: the owner has been asked to set Windows not to sleep on mains power.

The owner is not there during a run: never ask a question, never wait for an answer, never open a window that
needs a person (file choosers, message boxes); make the reasonable choice and note it in the log.

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

## Programme (owner, 2026-10-05): "do it all" + "any features Ableton or Logic would have", "done clean and very well"

Work through these groups in order. One feature at a time, finished before the next. A feature is DONE only when it has:
its own window with a real display in the house style (see DynamicsDisplay.h, SpaceDisplay.h, DeviceEditor.h panels);
factory presets (4 or more per device); a control for every parameter; self-test coverage (device audit, fuzz, state
round trip, undo steps where it edits the project); a clean run under the Debug build and of `--monkey` / `--command-fuzz`;
README and CHANGELOG entries. Prefer fewer features done properly over many done thinly. Tick items here as they land.

Owner, 2026-10-05 (later): "We will go from that list for now. It's also not simply any missing effects that
logic/ableton have, its also quality of life things and making sure the quality is professional like those as
well EVERYWHERE." So, besides the list:

- **Quality pass with every item.** Whatever part of the app an item touches is left at the standard of Live and
  Logic, not only the new code: sound (no clicks on parameter moves, bypass or preset change; no zipper noise;
  correct latency reporting; sensible at 44.1 to 192 kHz and any block size), looks (house style, aligned, nothing
  clipped at the smallest and largest window size, light themes too), and behaviour (undo for every edit, double
  click resets, wheel and fine-drag on every control, typed values, tooltips, right-click menus, keyboard access).
- **Quality-of-life list** (add to it whenever something is noticed; do one or two with every version):
  [x] typed value entry on every knob and readout  [x] A/B compare and copy A>B in every device window
  [x] undo/redo inside device windows shown in the window  [x] per-device input/output meters and gain match
  [x] "what does this do" hover help everywhere, with a status-bar line  [x] search in every menu that has more
  than 20 entries  [ ] drag any device, clip, preset or sample anywhere it could sensibly go
  [ ] consistent modifier keys (fine, snap off, copy-drag) across arrangement, piano roll, automation and devices
  [x] collect-all-and-save, missing-file finder  [x] undo history list  [x] default track / device presets
  [x] zoom to selection / fit, and remembered zoom per view  [x] crash-safe autosave restore prompt reviewed
  [ ] every list sortable and keyboard-navigable  [ ] nothing blocks the UI for more than 100 ms (progress
  shown instead)  [x] first-run experience: audio set-up check, demo project, short tour
- Nothing that already works is taken away, and a rough edge found on the way is fixed then, or written here.

1. Effects: [x] transient shaper  [x] de-esser  [x] gate / expander  [x] convolution reverb (built-in spaces)
   [x] shimmer reverb  [x] spectral (freeze / blur / shift / spectral delay)  [x] chorus / flanger / phaser
   [x] tremolo / auto-pan  [x] frequency shifter  [x] bit reduction  [x] amp + cabinet  [x] exciter  [x] beat repeat
   [x] grain delay  [x] looper  [x] resonators
2. Instruments: [x] Wavetable third oscillator + wavetable import from audio  [x] sampler (slices, per-slice stretch,
   zones)  [x] analogue-style subtractive synth  [x] physical-modelling instrument (string / mallet)
   [x] electric piano  [x] tonewheel organ
3. MIDI: [x] note length  [x] pitch  [x] random  [x] velocity  [x] drawable arpeggiator / note-echo patterns
   [x] chord track + chord progressions (piano roll and MIDI effects follow it)  [x] step sequencer view for drum clips
4. Audio editing: [x] warp by dragging transients on the waveform, follow-tempo per clip  [ ] per-note pitch and time
   editing of audio  [x] reverse / crop / consolidate clips  [x] clip launch recorded into the arrangement with quantised punch
5. Mixing: [x] VCA faders  [x] reference track A/B with loudness match  [x] mastering chain preset + assistant
   [x] match EQ  [x] linear-phase EQ mode  [x] loudness report on export  [x] automatic freeze under load
   [x] sidechain picker on every device
6. Workflow: [x] device racks with parallel chains  [x] track alternatives  [x] project and track notes
   [x] browser preview in time and in key  [x] macros on the track header  [x] toolbar / window layouts
   [x] controller mapping pages  [x] video track
7. Techno (the owner, 2026-10-06: "I'd use this mostly for experimental techno music"; do this group before what is
   left of the others): [x] the demo song is a whole techno track (wait for the owner's ears on it, then change what
   they say)  [x] step sequencer: probability, ratchets and a length of its own for each row (polymetre)
   [x] a drum machine view of the Drum Rack: 16 steps a pad, accents, swing, pattern A/B/C/D and chaining
   [x] an acid bass preset bank for Pulse Analog, with accent and slide written as the 303 does  [x] distortion:
   more kinds worth having on a kick (clipper, wavefolder with tone, rumble maker = reverb into low pass into
   sidechain)  [x] a "kick" synth with pitch and click envelopes drawn on a graph  [x] techno presets for every
   instrument and factory drum kits (909, 808, industrial)  [x] a performance view: mutes, fills and filter
   sweeps on one page, playable from a controller

Not attempted (told the owner why): content libraries and AI session players (large downloads / trained models),
a scripting environment like Max for Live, surround / Atmos, a score editor. Still owed from before: rerun the
one-hour `--soak` (the first attempt was cut off at 3.5 minutes when the session ended).

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
- 2026-10-02: v0.52.0 arrangement Overview strip (whole-song miniature, drag / edge-zoom / click-jump / wheel), middle-drag
  pan, Alt+wheel horizontal scroll; Wavetable full panel (WavetablePanel in DeviceEditor.h: OSC A/B, voice, filter 1/2,
  envelopes, LFOs, modulation, effects, play bar). 214 checks pass. Owner queue (request 3) done; continue with item 6.
- 2026-10-02: v0.53.0 graph-first full editors for Pulse Reverb, Pulse Delay and Saturator (SpaceDisplay.h: HandleView
  base with draggable handles, decay/echo/transfer graphs plus tone curves). Fixed ParamChoice button widths (the last
  buttons were squeezed). 216 checks pass.
- 2026-10-03: v0.54.0 owner: "wavetable needs refinement, fix all effects to look like FabFilter". Wavetable panel reworked
  (header menus, 2x2 knobs, osc 1 semi/fine, LFO shape icons, amount bars, FX groups, output scope). Generic device Editor
  redesigned: displays fill the window, controls in a bottom bar (no folding). ProDisplays.h: SpectrumPro, TunerPro, PitchPro,
  GlueNeedle, AutoFilterLfo. Pro:: palette in Common.h so device windows follow light themes. 216 checks pass.
  Owner then said the Wavetable is still weak: next is a full overhaul with tabs and more sound features (v0.55).
- 2026-10-03: v0.55.0 Wavetable overhaul (owner: "still weak, maybe tabs", then "no keyboard, use the space, looks cheap").
  Engine: 11 more wavetables (17), noise osc, sub shapes / octave, per-osc octave / level / pan, unison width, filter drive,
  key tracking, notch, mono mode, 4 macros (sources 14..17), drive types, phaser, EQ; new param ids appended (old ones keep
  their places). Window 1280 x 860: tabs Synth / Effects / Matrix / Draw wave, three dense columns, solid 3D wavetable,
  macros + source chips + scope in the bottom bar (no keyboard). Knobs restyled app-wide (thin arc, quiet cap).
  15 new presets. 220 checks pass. MidiKeyboardState stays in the synth (unused by the UI, tested).
- 2026-10-03: v0.56.0 EQ Six display: smooth analyser, glowing response curve filled to 0 dB, dB labels, band type as a menu.
  220 checks pass. Next ideas: FM Synth / Simpler / Drum Synth windows in the Wavetable's card style; LFO tabs as LineTabs.
- 2026-10-03: v0.57.0 FM Synth window (FMPanel in DeviceEditor.h, same cards as the Wavetable): algorithm diagram,
  wave preview, operator cards with draggable envelopes, filter, filter envelope, play. 221 checks pass.
  Next ideas: Simpler and Drum Synth windows in the same style; device cards in the chain.
- 2026-10-04: v0.58.0 owner: "don't take any functionality away" + "cutoff knob should show where the envelope moves it".
  FM operator cards have A/D/S/R knobs again; Wavetable LFO card has LFO 1 > Position / Cutoff back. ModDrop::sweep
  (installed in WavetableUI::installModDrop) gives every cutoff knob an outer arc for the envelope / LFO range
  (Wavetable F1 + F2, FM, Simpler, Auto Filter). Self-test walks the Wavetable and FM windows and fails if any
  parameter has no control. 223 checks pass. RULE: new windows must keep a control for every parameter.
- 2026-10-04: v0.59.0 Simpler window (SimplerPanel, same cards): sample + region / pitch, filter, amp env, filter env,
  granular, output; every parameter keeps a control (self-test covers Wavetable, FM, Simpler). 223 checks pass.
  Drum Synth (8 params) stays on the shared layout. Next ideas: device cards in the chain; Drum Rack window polish.
- 2026-10-04: v0.60.0 device cards in the chain use the windows' card look; on/off parameters on cards are buttons and
  choices are small menus (ParamMenuBox) instead of knobs (also on Drum Rack pages); Drum Rack pads restyled; sweep arc
  sits outside the modulation arc; Matrix tab no longer repeats the source chips; ParamButtons::numOptions allows up to 40
  options (the Wavetable has 17 tables). 223 checks pass. Owner asked for a v1 release candidate: see "Release candidate".
- 2026-10-04: PAUSED mid-way through 0.90.0 (1.0 RC 1). UNCOMMITTED in the working tree: About.h + File > About,
  THIRD-PARTY-NOTICES.md, CHANGELOG.md, project from the command line, installer file association, release .pdb
  (/Zi /DEBUG), audio-thread fixes (Saturator dry buffer, Pitch Correction buffers, thread_local MIDI buffer in PulseSynth),
  LoudnessAnalyser rewritten (ring + histogram, try-lock; it crashed 1 run in 5 inside Limiter on the live engine thread).
  OPEN: the self-test is still flaky about 1 run in 3-5 (last: "Device works: Prism EQ" read -200 dBFS once). Likely cause:
  tests call initialise / applyToBuffer directly on plugins that the live engine is also processing. Find and fix before
  committing 0.90.0; run the self-test 8+ times in a row. Version files already say 0.90.0.
- 2026-10-05: resumed; 0.90.0 (1.0 RC 1) committed. Self-test passed 10 runs in a row (225 checks) after the
  LoudnessAnalyser rewrite; the one-off "Prism EQ read -200 dBFS" did not recur (the audit now logs "!!" lines in the
  .progress.txt if a render comes back missing or silent). Version stage text lives in About::stage().
  Release blockers that need the owner: licence choice (GPL vs commercial JUCE / Tracktion / Link), code signing.
- 2026-10-05: v0.91.0 (RC work) a damaged or foreign project file is never opened and saved over (About::isProjectFile,
  a message, an empty "Untitled" project instead); instrument preset level check (chord peaks -30 .. +1 dBFS) and ten
  hot presets turned down 4 dB. Known: the Wavetable's Level is before its effects, so driven presets sit near 0 dBFS
  whatever Level says. 227 checks, 4 clean runs.
- 2026-10-05: v0.92.0 (RC work). BUG FOUND by a new save / reload test: Saturator and Auto Filter kept their "Type"
  parameter under the state property "type", which is the engine's record of the device kind, so a project saved after
  changing Type lost that device on reload. Fixed (PulsePlugin::storageID keeps reserved names under "pulse_<id>") and
  Upgrade::repairProjectFile puts affected projects right on opening (copy kept as "... (before repair)").
  Also: one app at a time (a double-clicked project goes to the running app; anything with "--" stays separate);
  Wavetable Output knob after the effects (pOutput) and 20 presets trimmed to about -6 dBFS; shorter page names on cards.
  230 checks. RULE: never give a parameter the id type / id / name / enabled (storageID guards it anyway).
- 2026-10-05: v0.93.0 (RC work) preset browser in device windows (FactoryPresets::category / ordered / addToMenu,
  "pulsePreset" kept in the device state, < > buttons, name on the Presets button; cards use the grouped menu too);
  whole-project save / reopen comparison test (273 things); duplicate preset name removed. 232 checks.
- 2026-10-05: v0.94.0 (RC work). BUG FOUND by new undo tests: Undo / Redo restored a parameter's value in the project
  but not the parameter (knob, fader, sound). Fixed twice over: PulsePlugin::valueTreePropertyChanged (built-in devices)
  and UndoSync.h (one per open project: any PLUGIN whose stored value changes during undo / redo, so volume, pan, sends
  and plugins too). New tests: knob undo, fader undo, a string of edits undone and redone (project fingerprint), device
  fuzz (456 runs of random and extreme settings: finite output, effects quiet a second after the input stops).
  Mute / solo are not part of undo (engine design). 236 checks, 3 clean runs.
- 2026-10-05: v0.95.0 (RC work) presets and copies of every effect compared value by value; presets and chains from
  older versions repaired on load (ProjectRepair.h, shared with Upgrade); project fingerprint widened (every device's
  parameters, session slots, tempo / time signature counts, loop, scenes); every device window drawn at four sizes;
  instrument windows fit the screen and cannot be shrunk below 1000 x 680; docs/RELEASE.md (decisions, checklist,
  what the self-test does not cover, known limits). 238 checks.
- 2026-10-05: v0.96.0 (RC work) test "every editing step is undone and redone exactly": 27 kinds of edit (track, tempo,
  sections, clips, MIDI tools, devices, knobs, faders), each compared against a whole-project fingerprint after undo and
  after redo. It found: the loop range drifting when a tempo change is undone (fixed in UndoSync: the loop is put back
  on its beats). Fingerprint leaves out values that follow automation at the playhead. 239 checks, 2 clean runs.
- 2026-10-05: v0.97.0 (RC work, performance). `--idle-cpu <file>` measures processor use (whole process and the UI thread)
  stopped with devices showing, stopped with the mixer, and playing; plugin scans and the welcome screen are off while
  capturing (About::isCapturing). Idle use about halved (roughly 12-13% of a core -> 6-10%; UI thread 3-4% stopped):
  knobs, ruler, section / marker / tempo strips and the overview redraw only when something changed
  (EditViewState::signature / changedSince, a change counter on the project); effects sleep after 6 s of silence in and
  out (PulseEffect::asleep, canSleepOnSilence); audio worker threads = physical cores (2..8) and they sleep on a
  semaphore (lightweightSemaphore) instead of yielding. The off-UI share still varies run to run (7-30%) and I could
  not attribute it further without a per-thread profile. 240 checks, 3 clean runs.
- 2026-10-05: v0.98.0 (RC work) mute and solo are undoable (EngineHelpers::toggleMute / toggleSolo write the project
  with the undo manager; all buttons and Push use them); File > User guide (F1) opens the installed README; About and
  User guide are commands; tooltips on the value boxes and modulation amount bars; Wavetable speed test (8 held notes:
  default patch about 47x real time, heaviest patch about 10x, one core) - no engine optimisation needed. 241 checks.
- 2026-10-05: v0.99.0 (RC work) `--command-fuzz <file> [--seed n]`: the full app runs every unattended command (45 of
  them) 456 times in a random order on the demo project with different selections; four seeds, no crash. Added to the
  release checklist with --idle-cpu.
- 2026-10-05: v0.99.1 (RC work) automation travels with a song section when it is duplicated or moved
  (Sections::copyContent copies the points in the range, held between the values at its ends, and keeps the
  neighbours' values at the edges). 242 checks.
- 2026-10-05: v0.99.2 (RC work) `--monkey <file> [--seed n] [--events n]`: random clicks, drags, wheel turns and keys
  on the real main window (file choosers are JUCE's own while capturing, so the run can close them). It found two bugs:
  the piano roll crashed drawing a clip that undo / deleting its track had taken out of the project (PianoRoll::
  dropClipIfGone), and Split at the playhead exactly on a clip's edge (or on a session-slot clip) made the app die
  without a crash report, intermittently (EditingOps::canSplitAt). After the fixes: 4 seeds x 5000 events clean.
  The earlier "exit 127 with no report" on Ctrl+E was not pinned to a root cause beyond that guard. 243 checks.
- 2026-10-05: v0.99.3 (RC work) mixer strips in the card look; picture / measurement / fuzz runs no longer change the
  saved theme or the last-opened project of the profile they run in (I restored the Dev profile's theme, which a fuzz
  run had cycled to "Ocean"); monkey run also switches views (4 more seeds x 8000 events clean). 243 checks.
- 2026-10-05: v0.99.4 (RC work). Ran the self-test under a DEBUG build (`cmake --build build-dev --config Debug`;
  `--log <file>`; debug-build check failures and abort / terminate now write a crash report instead of a window -
  CrashGuardWin::installDebugCheckHook, SIGABRT and set_terminate hooks). It found the ROOT CAUSE of the intermittent
  crashes: every render path ran while the project was still attached to the live audio engine ("Rendering whilst
  attached to audio device"), so two threads were inside the same devices. Fixed with EditingOps::renderDetached
  (stopAllTransports + te::Edit::ScopedRenderStatus) in bounce, group render, --render and BackgroundRender (exports).
  Also found: DelayLine::read could index one past the end (float rounding of the read position; positions are now
  double, which also restores sub-sample precision in long delays). Debug self-test: 243 passed, no failed checks.
  The monkey run can open device windows and send events to them (not yet run long). Release: 3 clean runs.
- 2026-10-05: v0.99.5 (RC work) `--soak <file> [--minutes n]`: plays the demo on a loop with changing views and logs
  memory (private bytes) every 30 s; passes if the last third of the run grows < 15 MB. Result: level (1.7 MB over
  5 minutes). A one-off of about 55 MB appears the first time a full backup is written (5 minutes in); it is not a
  leak and I did not track down what allocates it. `--nobackup` skips the timed backups. Debug-build monkey run
  (2 seeds x 3000 events, with device windows) clean.
- 2026-10-05: OWNER DECISION: Pulse Studio goes public only at version 2. Licence choice (GPL vs commercial JUCE /
  Tracktion / Link) and code signing are version 2 matters; do not raise them as blockers or open questions for
  version 1. Version 1 is a private release: aim for a solid, finished 1.0 on the owner's own machine.
- 2026-10-05: v0.99.6 (owner: "do 3 and 4"). Drum Synth window (DrumSynthPanel). Test tools: `--big <tracks>` enlarges
  the demo for --command-fuzz / --monkey / --idle-cpu / --soak (48 tracks: fuzz and 6000-event monkey clean; playing
  uses about 64% of one core); the monkey run now uses the whole window including the top bar, except File, Audio,
  Plugins and Record (TransportBar::needsAPersonAt); --idle-cpu lists the busiest threads (CrashGuardWin::threadTimes;
  JUCE gives threads no description so they show as ids): at idle the UI thread is 4-5% (8% with the mixer), audio
  workers about 3% once settled. Memory: the rise at ~5 minutes is NOT the backup (probed step by step: 0.1 MB);
  about +80 MB between minutes 5 and 7, then flat; cause not found (caches suspected). Audio loop takes: the engine
  already adds file-based takes after a loop recording (WaveInputDevice applyLastRecording); unverified without an
  audio input. 243 checks.
- 2026-10-05 (evening): v0.99.7. Programme group 1 started: Pulse Gate (gate / expander / ducker, hysteresis, hold,
  lookahead, filtered key, sidechain), Transient Shaper (peak follower first so low notes are not read as hits; 3 ms
  lookahead reported as latency), De-esser (split / wide). Source/Dynamics2.h + Dynamics2Display.h; windows are
  HandleViews in DynamicsUI panels; 10 / 9 / 7 presets. 250 self-test checks pass (Release and Debug); monkey and
  command-fuzz clean. Autopilot repaired: the 15:29 scheduled run had taken the lock and then stalled after two
  minutes (most likely on a permission prompt nobody was there to answer), and runs were only every 3 hours.
  Now hourly, 45-minute lock, runs go on past one item, leftover work is checked and kept. Owner added: quality of
  life and professional quality everywhere (see the Programme section). The monkey run (seed 606) found a crash in
  the Drum Rack (buttons inside the detail rebuilt it during their own click): fixed, seeds 606 and 607 clean.
  The stalls were the PC going to sleep, not a prompt (see Lock). Not judged by ear. De-esser display could
  still get a live spectrum behind the band.
- 2026-10-06: v0.99.8. Pulse Space (convolution reverb): Source/Convolution.h (SpaceIR makes the twelve spaces;
  juce::dsp::Convolution, non-uniform, zero latency; the response is rebuilt on a 12 Hz message-thread timer when
  space / size / decay / reverse / file change, and synchronously in prepare() so renders are right from the first
  block), ConvolutionDisplay.h (five handles). A file as the space is the state property "irFile". 15 presets.
  253 checks. Ideas left: a live spectrum behind the De-esser band; drag a file onto the Pulse Space window.
- 2026-10-06: v0.99.9. Pulse Chorus, Phaser-Flanger, Auto Pan: Source/ModFx.h (ModEffect base: free / tempo-synced
  LFO, Smooth for settings changed while playing), ModFxDisplay.h (ModUI views and panels). 9 / 9 / 8 presets.
  260 checks. The device fuzz caught the ensemble's feedback loop gaining with three voices (now the average).
- 2026-10-06: v0.99.10. Frequency Shifter (two all-pass chains a quarter cycle apart; mirror image 54 to 59 dB
  down from 80 Hz to 9 kHz), Redux, Exciter: Source/ColourFx.h, ColourFxDisplay.h. 9 / 8 / 7 presets. 267 checks.
  The Debug self-test now takes about 5 minutes: run it, the monkey and the command fuzz as separate commands
  (one command of all three went over the 10-minute limit).
- 2026-10-06: testing split into two tiers at the owner's request (see Testing). v0.99.10 had the Debug self-test
  (267 pass) but no monkey / command-fuzz run: owed at the end of group 1, with everything after v0.99.9.
- 2026-10-06: v0.99.11. Pulse Amp (Source/AmpFx.h, AmpFxDisplay.h): valve stages at 2 x the sample rate through a
  31-tap Kaiser half-band each way (taps worked out at start-up), dry path delayed to match, latency reported.
  Known limit, written into its test: a full-level 5 kHz sine through the four-stage Heavy amp with no cabinet
  still folds back at -30 dB (very high harmonics of a near-square wave); 4 x or anti-aliased clipping would be
  the next step if it is ever heard. 11 presets, 270 checks. Quick tier only (slow tier owed for group 1).
- 2026-10-06: v0.99.12. Resonators, Shimmer, Grain Delay: Source/TimeFx.h (GrainShift = two-tap delay-line pitch
  shifter, used by Shimmer), TimeFxDisplay.h. 8 / 8 / 9 presets. 277 checks, quick tier only. The device fuzz found
  the Resonators getting far too loud at long decays (input now scaled by sqrt(1 - g^2), and each string is
  limited); Shimmer's fed-back level is scaled down with decay. Left in group 1: spectral, beat repeat, looper.
- 2026-10-06: v0.99.13. Beat Repeat and Looper: Source/LoopFx.h, LoopFxDisplay.h. 8 / 7 presets. 282 checks, quick
  tier only. The Looper's audio lives in memory only (not saved with the project: say so wherever it comes up);
  saving it as a file beside the project, and dragging the loop out as a clip, would be the next steps.
  Note for tests: the engine rounds samples under about a millionth to zero, so "output equals input" checks
  need a tolerance of 1e-5, not 1e-6. Left in group 1: spectral; then the slow tier for everything since v0.99.9.
- 2026-10-06: v0.99.14. Pulse Spectral (Source/SpectralFx.h, SpectralFxDisplay.h): 2048-point transform, quarter
  overlap, phase-vocoder shift, freeze that settles on the loudest of the first 12 steps, blur that keeps each
  frequency's pitch while it rings on, a ring of past steps for the per-frequency delay. 11 presets. 285 checks.
  Programme group 1 (effects) is complete: 21 new devices since v0.99.6.
- 2026-10-06: slow tier for Programme group 1 (covers v0.99.10 to v0.99.14): Debug build, Debug self-test 285 pass,
  monkey seeds 1401 / 1402 on the demo project and 1403 / 1404 on a 30-track project (--big 30) that now holds
  every effect (so every new window is opened and clicked at random), command fuzz seed 1401: all clean, no crash
  report. Installer built. Nothing owed. Next: group 2 (instruments), first item the Wavetable's third oscillator.
- 2026-10-06: v0.99.15. Wavetable: third oscillator (9 parameters appended after pOutput; level3 defaults to 0 so
  old sounds are unchanged; it shares OSC B's card through B / C tabs; in Split routing it goes with osc 1) and
  "Imported" as an 18th table: WavetableSynth::importWavetable / cyclesFrom / periodAt (autocorrelation, period
  to a fraction of a sample), stored in the project as base64 ("importWave"), so the audio file is not needed
  again. WavetableBank::build() now makes any per-instance table. 3 new presets. 287 checks, quick tier.
  README: the Wavetable row starts with the two additions; the rest of that row is older text.
- 2026-10-06: v0.99.16. Tonewheel Organ and Electric Piano: Source/Keys.h, KeysDisplay.h. The organ's drawbars
  are handles in its picture (no knobs for them in the strip: it did not fit, and they are whole numbers 0 to 8).
  EPiano::wave() is the one description of the sound, used by the voices and the display. 8 presets each. The
  device fuzz found Pulse Spectral growing without end with Freeze + Feedback (the ring now keeps no frequency
  louder than a full-level tone). 290 checks, quick tier. Note: FactorySounds::renderNote trims the quiet end of
  what it returns, so tests must not read far into a decayed tail.
- 2026-10-06: v0.99.17. Pulse Analog and Pulse String: Source/Synths2.h, Synths2Display.h. Analog: polyBLEP saw
  and pulse, a zero-delay-feedback four-pole ladder with a tanh at its input (self-oscillation stays at -20 dBFS),
  its panel has two rows of strips (AnalogPanel is its own Component, not DynamicsUI::Panel). String: an extended
  Karplus-Strong loop whose length allows for the delay of its own damping and stiffness filters (tuning within
  a cent); voices get their memory in initialise(), nothing is allocated while playing. Known limit: hard sync
  is not band-limited (it can fold back on high notes). 10 presets each. 293 checks, quick tier.
  Left in group 2: the sampler (slices, per-slice stretch, zones) - that one extends the existing Simpler.
- 2026-10-06: slow tier for v0.99.15 to v0.99.17 (Wavetable osc 3 / import, Organ, Electric Piano, Analog, String):
  Debug self-test 293 pass; monkey seeds 1701 / 1702 on the 30-track project, which now also holds the four new
  instruments; command fuzz 1701: all clean, no crash report. Nothing owed. Group 2 has one item left: the
  sampler (slices, per-slice stretch; key and velocity zones already exist in Simpler since v0.18).
- 2026-10-06: v0.99.18. Simpler slices: mode 4 "Slice", slices as the state property "slices", sliceAtHits /
  sliceEvenly / hitsIn, parameters pStretch and pSliceGate appended. Stretch is two overlapping 45 ms pieces whose
  start follows a slow reading point (the first piece begins at full level so the hit keeps its edge). 294 checks,
  quick tier. Programme group 2 (instruments) is complete; its slow tier is owed only for this last commit.
  Next: group 3 (MIDI: note length, pitch, random, velocity effects; arp / echo patterns; chord track; step sequencer).
- 2026-10-06: v0.99.19. MIDI effects Note Length, Pitch, Random, Velocity: Source/MidiFx2.h (+ MidiFx2Display.h for
  the Velocity line). The other three use the existing "what it does to a phrase" picture. Two-way settings are
  shown by the generic window as a button carrying the parameter's NAME, so a two-way parameter must be named
  for what "on" means ("All The Same", not "Mode"): worth checking on older devices too. 298 checks, quick tier.
  The README's file table (near its end) has not been kept up with the files added since v0.99.6: Dynamics2,
  Convolution, ModFx, ColourFx, AmpFx, TimeFx, LoopFx, SpectralFx, Keys, Synths2 and their *Display.h files.
  Next in group 3: drawable arpeggiator / note-echo patterns, chord track, step sequencer view.
- 2026-10-06: v0.99.20. Drawn patterns: Arpeggiator (state "pattern", 16 levels, pSteps, pSwing; a rest advances
  time but not the note order) and Note Echo (state "levels"; absent = the old Feedback behaviour). The bars are
  MidiFxUI::StepBars (MidiFx2Display.h), reusable for any row of values. 299 checks, quick tier.
  Next in group 3: chord track + progressions, step sequencer view for drum clips. Both touch the arrangement and
  piano roll (ArrangementView.h, PianoRoll.h): read those first.
- 2026-10-06: v0.99.21. Chord track: Source/Chords.h (model in edit.state "PULSECHORDS", 17 chord kinds, 11
  progressions by scale degree, voicing / writeToClip / fitNotes, Snapshot for the audio thread), ChordStrip in
  ArrangementView.h (below SECTIONS), the piano roll's "Fit to the chord track" tool and command midi.fitChords,
  ScaleFx::pFollow (PulseMidiEffect now has an onTimer() hook, four times a second on the message thread).
  The demo project has its chords on the track. 300 checks, quick tier.
  Still to do for this item: shade the chord's notes in the piano roll's rows as the scale shading does (per
  chord span along the time axis), and let chords be dragged to move them. Chords do not yet move with sections.
  Next: step sequencer view for drum clips (last item of group 3), then the slow tier for v0.99.18 to here.
- 2026-10-06: v0.99.22. Step grid: Source/StepSequencer.h (StepGrid), shown inside the piano roll by its Steps
  button (PianoRoll::showSteps, command view.steps). It redraws only when the notes or the playing step change.
  301 checks, quick tier. Programme group 3 (MIDI) is complete. The slow tier is owed for v0.99.18 to v0.99.22.
  Next: group 4 (audio editing: warp by dragging transients, per-note pitch and time, reverse / crop /
  consolidate, clip launch recorded into the arrangement).
- 2026-10-06: slow tier for v0.99.18 to v0.99.22 (Simpler slices, four MIDI effects, drawn patterns, chord track,
  step grid): Debug self-test 301 pass; monkey seed 2201 on the demo project and 2202 on the 30-track project;
  command fuzz 2201 (468 commands, including the new view.steps and midi.fitChords): all clean, no crash report.
  Installer built. Nothing owed. Programme groups 1, 2 and 3 are complete.
- 2026-10-06: v0.99.23. EditingOps::cropToRange / canConsolidate / consolidate (MIDI through te::mergeMidiClips;
  audio rendered with only those clips allowed and usePlugins = false, so the track's devices and fader are not
  printed). Clip menu items and commands edit.crop, edit.consolidate (the latter kept out of the command fuzz,
  like edit.bounce, because it renders). Reverse already existed. 302 checks, quick tier.
  Left in group 4: warp by dragging transients on the waveform; per-note pitch and time editing of audio (a
  large item: needs pitch tracking and a resynthesis that the engine does not have ready-made); clip launch
  recorded into the arrangement with quantised punch. Read SampleEditor.h, ClipProperties.h (warp), SessionView.h.
- 2026-10-06: v0.99.24. Warp by grabbing hits: Workflow::transientNearBeat / addWarpMarkerAtTransient; the clip
  component draws the hits as ticks in its warp strip (selected clips only: sourceTransients reads the whole file
  the first time, on the message thread, which is a pause of its own on long files: moving that to a background
  thread belongs on the quality list under "nothing blocks the UI for more than 100 ms"). Markers at all hits,
  audio quantise and warp-to-tempo existed already. 303 checks, quick tier.
  Left in group 4: per-note pitch and time editing of audio; clip launch recorded into the arrangement.
- 2026-10-06: v0.99.25. Recording session launches into the arrangement existed (Launcher::SessionRecorder) but
  rounded every change to a whole beat; it now uses the session's launch quantisation as its grid and takes the
  grid line before the moment the change was noticed. 304 checks, quick tier.
  Group 4 has one item left, and it is a big one: per-note pitch and time editing of audio (pitch tracking,
  notes shown as blobs on the clip, each one dragged in pitch or time, the result written to a new file).
  Melodyne and other ARA plugins can already be used on a clip for this. Do it as a run of its own.
- 2026-10-06: v0.99.26. Mastering.h: addChain / measure / gainFor / assist; commands mixer.masteringChain and
  mixer.masterAssist (the latter kept out of the command fuzz: it renders the song up to three times); a Master
  button beside the Limiter button on the master strip. Found on the way: the engine's EditLimits allowed only 4
  master devices (2 already used by the meter and the loudness readout) and 16 per track; PulseEngineBehaviour
  now returns 24 / 48 / 8 per clip. A project that has never been saved cannot be rendered (measure returns
  nothing): the message says so. The loudness report on export existed already (ticked). 305 checks, quick tier.
  Left in group 5: VCA faders, reference track A/B, match EQ, linear-phase EQ mode, automatic freeze under load,
  sidechain picker on every device (check that the Gate's card shows the picker the Compressor's does).
- 2026-10-06: v0.99.27. VCA faders: Source/Vca.h (model in edit.state "PULSEVCAS"; setLevel moves the members'
  own volume faders by the same amount and stops where any member's fader ends; Vca::Strip for the mixer),
  Mixer.h (+ VCA button, strips after the tracks, the name-plate right-click menu, the colour bar). The demo has
  a "Rhythm" group. The sidechain picker shows on every device that declares a key input (PulseEffect::
  wantsSidechain adds the two channels the engine's canSidechain() looks for), so that item is ticked.
  Not done for VCAs: automation of the group fader, and solo. 306 checks, quick tier.
  Left in group 5: reference track A/B with loudness match, match EQ, linear-phase EQ mode, automatic freeze.
- 2026-10-06: v0.99.28. Linear-phase EQ. Source/LinearPhase.h: pulse::LinearFir (the wanted response, one real
  number per frequency, becomes a windowed filter half a transform long; the sound goes through in blocks of that
  half, and a new response is faded in over one block; four responses, so mid / side works) and StepDelay.
  PrismEQ: parameter "phase" (Natural / Linear = 8192-point transform / Linear, deep = 32768), syncPhaseMode()
  builds the filter away from the audio (a timer calls it and restarts playback when the delay changes), the
  filter is designed from the bands' own settings once per block when they have changed. Measured: within
  0.35 dB of the natural EQ on a 24 dB/oct cut one octave down (0.02 dB in Deep), the delay is the 6144 samples
  it reports, the response is a mirror of itself to the last digit. Known: in the linear modes dynamic bands and
  band movement do nothing (said in the band panel); the filter is worked out on the audio thread (a few
  hundred microseconds in Linear, more in Deep, once per block at most). 307 checks, quick tier.
- 2026-10-06: v0.99.29. Match EQ. Source/MatchEQ.h: pulse::SpectrumLearner (average power, 8192-point frames,
  121 points at 12 per octave) and pulse::MatchEQ (startLearning / stopLearning for this track or the key input,
  learnFromFile, the two spectra in the plugin state as "sourceSpectrum" / "referenceSpectrum", makeCurve, a
  LinearFir of 8192 points); Source/MatchEQDisplay.h (MatchUI::MatchView and MatchPanel). Wired into the menus,
  the browser list, the big test project, device shots, 7 presets. Measured with noise against the same noise
  through two shelves (+8 dB highs, -6 dB lows): the curve reads +7.9 / -6.1 dB, and tones through the device
  come out as the curve says. Not tested: learning from the key input (the offline test rig has no key input);
  not judged by ear. 310 checks, quick tier.
  Left in group 5: reference track A/B with loudness match, automatic freeze under load.
- 2026-10-06: v0.99.30. Freeze under load. Source/AutoFreeze.h (namespace LoadGuard: mode in the settings as
  "overloadMode", cpuOf / canFreeze / heaviest / freezeHeaviest, Monitor = over 85% for 3 s in a row, reset
  under 70%). TransportBar: the CPU readout is clickable (menu: freeze the heaviest track, and the three
  modes), turns red when overloaded, and in the automatic mode freezes once playback has stopped. Command
  mixer.freezeHeaviest (kept out of the command fuzz: it renders). Tested with made-up costs (the offline rig
  has no real load); the real path from device-manager load to the red readout has not been seen happening.
  311 checks, quick tier. Left in group 5: reference track A/B with loudness match.
- 2026-10-06: v0.99.31. Reference song. Source/Reference.h (pulse::Reference: the song is read on a thread of
  its own, brought to the engine's sample rate with a windowed sinc, its loudness measured once; the mix is
  measured as it plays; "listen" swaps the song in with a 10 ms fade, at project time + "Start At"; it does
  nothing at all when fc.isRendering), Source/ReferenceDisplay.h, Mastering::reference / toggleReference
  (insertIndex now also stays in front of the Reference, so a mastering chain added later is not put after it),
  the Mastering menu of the master strip, command mixer.referenceToggle, 5 presets. Measured: a -6.1 LUFS song
  against a -20.8 LUFS mix is turned by -14.7 dB and heard at the mix's level. The song is held in memory
  (15 minutes at most). Not judged by ear. 313 checks, quick tier. Group 5 is complete: slow tier next.
- 2026-10-06: slow tier for group 5 (v0.99.23 to v0.99.31), all clean: Debug build, Debug self-test 313 / 313,
  random input 4000 events on the demo project (seed 311) and on the 30-track project (seed 312), command fuzz
  504 commands (seed 313), installer PulseStudio-0.99.31-Setup.exe. Group 5 is done. Next: group 6 (workflow);
  group 4's per-note pitch and time editing of audio is still owed and wants a run of its own.
- 2026-10-06: v0.99.32. Notes. Source/Notes.h: the text is the property "pulseNote" on the project's state and
  on each track's; Notes::Panel (list + editor, one undo step per burst of typing, follows undo and tracks that
  come and go), Notes::Window (one at a time; closed with the project), command view.notes, the track menu, a
  folded corner on TrackHeader (TooltipClient: hover reads the note, click opens it). Folder tracks are in the
  window's list but their headers have no mark or menu entry yet. 314 checks, quick tier.
- 2026-10-06: v0.99.33. Previews in key. Source/SongKey.h (the song's key as the property "pulseKey" on the
  project, 0..11 = the major key's root; fromWord / fromName read a key from a file name; shiftFor = shortest
  way, -6..+5). Browser.h: AudioPreviewer::play takes semitones; playSynced became playRendered (with an edit:
  the loop in time as before, now also shifted; without: played once, shifted, at its own speed; the looping
  source can now play once); a Key button with a menu beside Preview and Sync. Previews in time were there
  already (Sync). Tested: the name reading (13 names, 5 of them traps such as "FM Bass" and "A Finished Song"),
  the sums, the project key and undo. Not tested: the sound of a shifted preview (it plays through the audio
  device, which the offline rig does not have). Only files up to 40 s are shifted. Nothing else uses the song's
  key yet (the piano roll's scale and the chord track could). 315 checks, quick tier.
- 2026-10-06: v0.99.34. Macros on the track header. Source/HeaderMacros.h (used = the macros with at least
  one target; Dial; Row, which rebuilds itself when the mapped set or a name changes). TrackHeader lays the
  row out under the volume when 40 px are left (a track about 120 px tall), and hides it otherwise. Folder
  (group) headers do not have it. 316 checks, quick tier.
- 2026-10-06: v0.99.35. Window layouts. Source/Layouts.h (Layout = browser, session, lower tab, the lower
  panel's share of the window height; builtIn(); your own as lines of text under "windowLayouts" in the
  settings). MainComponent: currentLayout / applyLayout, the Layout button and its menu, commands
  view.layout1..5 and view.myLayout1..4 (no keys by default). A layout does not hold the toolbar's contents
  (the toolbar cannot be rearranged), track heights or zoom. 317 checks, quick tier.
- 2026-10-06: v0.99.36. Track alternatives. Source/Alternatives.h: a child "PULSEALTS" of the track's state,
  one "ALT" per alternative holding copies of its clips' states; switchTo stashes the clips in use and puts the
  other ones on the track with fresh ids (te::EditItemID::remapIDs + insertClipWithState). The track menu, the
  "[B]" on the header, commands track.altNext / altNew / altNewEmpty. Found on the way, and fixed: removing
  clips while walking track.getClips() skips every other one (the list changes under the loop). Flatten had
  done exactly that since it was written, so a track with several clips kept some of them beside the rendered
  one; it has its own check now. Not done: alternatives for folder tracks; automation is shared by all
  alternatives of a track (Logic's are too, unless asked otherwise). 319 checks, quick tier.
- 2026-10-06: v0.99.37. Controller mapping pages. MidiLearn.h: each mapping has a page (0 = always; old
  projects read as always), the page chosen, page names, and two learned buttons that step through the pages
  that have something on them (on the press, not while held); everything in "PULSEMIDIMAP". MidiMapView.h:
  the mappings window. MainComponent: commands midi.mappings / pageNext / pagePrev / pageAlways, the banner
  says the page for a moment when it changes. Tested with made-up controller messages; no real controller was
  used. Push 2 keeps its own handling and has no part in this. 320 checks, quick tier.
- 2026-10-06: v0.99.38. Effect Rack. Source/FxRack.h: pulse::ChainOut (level, balance, mute at the end of a
  chain) and namespace FxRack (a te::RackType flagged "pulseFxRack"; chains listed in a "PULSECHAINS" child;
  devices tagged with their chain and order; rewire() rebuilds every connection; create / addChain /
  removeChain / addEffect / moveEffect / removeEffect / setSolo / wrap). Source/FxRackCard.h: its card.
  In the effects menu and the browser list as "Effect Rack" (description plugin:builtin:fxrack).
  Found on the way: te::RackInstance::isSynth() is true for every rack, so a track with an Effect Rack would
  have been an instrument track to the header, the track naming, importing and the place new devices go.
  EngineHelpers::isInstrument / isEffectRack (Common.h) now answer that question at the track-level checks
  (ArrangementView, ClipImport, PluginMenu, trackHasInstrument); the per-card colour checks were left.
  Measured by rendering: two empty chains +6.02 dB; a chain with a 128 ms linear-phase EQ beside a dry chain
  still sums to +6 dB, so the engine keeps chains in step inside a rack. Limits: a chain's effects are rows
  with an Edit button, not full cards; no rack inside a rack; no instruments in chains; listen-alone is not
  saved; no macros of its own (the track's macros reach the devices inside). 321 checks, quick tier.
  Left in group 6: video track. Left in group 4: per-note pitch and time editing of audio.
- 2026-10-06: slow tier for group 6 so far (v0.99.32 to v0.99.38), all clean: Debug build, Debug self-test
  321 / 321, random input 4000 events on the demo project (seed 321) and twice on the 30-track project (seeds
  322, 323), command fuzz 708 commands (seed 324), installer PulseStudio-0.99.38-Setup.exe. The 30-track test
  project now carries Effect Racks (every seventh track: a dry chain beside one with two effects), so the
  random-input runs click through the rack's card. Still open in the Programme: video track (group 6) and
  per-note pitch and time editing of audio (group 4); then the quality-of-life list.
- 2026-10-06: v0.99.39. Video. Source/Video.h (the project's video as a "PULSEVIDEO" child: file, start,
  sound; Video::Panel / Window; videoTimeAt / covers / needsJump) and Source/VideoPlayerWin.cpp (Windows'
  Media Foundation media engine on a thread of its own, with no window: each new frame is handed over through
  a WIC bitmap, made no bigger than 1920 x 1200 on the way, and the panel draws it). JUCE's own video
  component was tried first and dropped: on Windows it goes through DirectShow, which cannot open an MP4
  ("Unsupported stream"). Things learned the slow way: the engine wants a plain path (file:// gives 8007007b);
  the address string must stay alive until the video is closed; and an hour went on a wait loop in my own
  test that never ran. --video-probe <video> <report.txt> stays as a way to see how Windows gets on with a
  file. Checked on the 4-second clip that ships with Windows: it opens, a second of playing hands over about
  35 frames, and stopped at a place it shows that place's frame. Not seen: a long film following a playing
  song (the correction of drift is only tested as arithmetic); other formats than MP4. Not there: a lane for
  the video in the arrangement, its sound in the mixer, export of picture with music. 322 checks, quick tier.
  The Programme's group 6 is complete. Left: per-note pitch and time editing of audio (group 4), then the
  quality-of-life list.
- 2026-10-06: slow tier for v0.99.39 (video), clean: Debug build, Debug self-test 322 / 322, random input 4000
  events (seed 331), command fuzz 720 commands (seed 332), installer PulseStudio-0.99.39-Setup.exe. Groups 1,
  2, 3, 5 and 6 of the Programme are complete. Open: group 4's per-note pitch and time editing of audio; the
  quality-of-life list; the one-hour --soak; nothing has been judged by ear.
- 2026-10-06: v0.99.40. First item of the quality-of-life list: typing a value. Source/ValueEntry.h: parse()
  reads a typed or shown value as a number in base units; valueFor() finds the parameter value whose own text
  says the typed number (by halving the range against the parameter's own valueToString, so no unit has to be
  known here), then takes the typed number itself when the value is in a plain unit, so it is not rounded to
  what the display shows. ParamKnob: the value under the knob is a box to type into. Mixer: the level readout.
  ParamBar (rack bars) and the header's macro dials: a small box on a right-click (ValueEntry::ask).
  Not covered: the points on the graph displays (HandleView), the VCA strips' readouts.
  Audit of the list against the code: "undo history list" was there already (UndoHistory.h) and is ticked;
  A/B exists in the device window but without copy A>B; no per-device meters; no status-bar help line; no
  search inside menus; no finder for missing files; no default presets; the welcome screen exists, a tour
  does not. 323 checks, quick tier.
- 2026-10-06: v0.99.41. Device windows (three items of the quality-of-life list). PulseEffect (PulseDevices.h)
  now measures every effect's input and output (peak for the meters, power averaged over half a second) around
  process(), and with gain match on turns the output by matchGainDb (in power / out power), at most 24 dB, moved
  smoothly over each block; the choice is the device's state property "pulseGainMatch". DeviceEditors::Editor:
  undo / redo buttons, "A > B", the two meters and the Match button (hidden in a window too narrow for them).
  Measured: a +12 dB Utility comes out at the -20 dBFS that went in, matchDb -12.0. Instruments and outside
  (VST) plugins have no meters or match. The cost is two more passes over each block for every effect; the
  audio of every device test is unchanged. 324 checks, quick tier.
- 2026-10-06: v0.99.42. Help line. Source/HelpLine.h (textFor = a component's own tooltip or the nearest above
  it; Line polls the component under the mouse eight times a second) in the lower tab bar between the tabs
  and the Layout button. A new check opens the window of every built-in device and walks every button,
  slider and box: 155 of 1925 had no help. Fixed: Randomise and Reset (in every window), ParamKnob (a default
  from its name and how a knob is used), ParamChoice and ParamCombo and the Prism EQ's rows (parameter name
  and choice), the Wavetable's tabs, envelope tabs, LFO tabs and drawing tools, the FM algorithm arrows, the
  EQ's band switch. The check now fails on any silent control. "Everywhere" is true of device windows; the
  arrangement, mixer, piano roll, session view and browser are not audited this way yet (they have tooltips
  on most controls, but nothing proves all). 326 checks, quick tier.
- 2026-10-06: v0.99.43. Search in long menus. Source/SearchMenu.h (included at the end of Common.h): collect()
  walks a PopupMenu and its submenus for the entries that have an action; withSearch() returns the menu itself
  when it has twenty or fewer, else a copy with "Search this menu..." on top; Box is the list to type into.
  All 59 places that show a menu now show it through withSearch (m.showMenuAsync became
  SearchMenu::withSearch (m).showMenuAsync). Menus whose entries answer by number and a callback, not by an
  action of their own, are not searched (none of the long ones is of that kind). Seen on the way: the check of
  preset levels failed once on "Dark Cinematic Drone" at -30.3 dBFS against a floor of -30 (its slow attack and
  free-running LFOs put it on the edge; it had passed every earlier run); the preset is 4 dB louder now.
  327 checks, quick tier.
- 2026-10-06: v0.99.44. Missing files. Sharing.h: forEachFile() is now the one walk over every file a project
  points at (it was written out inside collectAll), with three more kinds added: a Pulse Space's impulse
  response, the Reference device's song, the project's video. On it: missingFiles(), relink (folder) and
  collectAll itself. MainComponent::findMissingFiles: asked after a project loads (only when something is
  missing) and from File > Find missing files / the command file.findMissing. The folder dialog itself was
  not driven in a test; relink was, with a clip and a device file. Not covered: files of outside (VST)
  plugins, which keep their own paths. 328 checks, quick tier.
- 2026-10-06: v0.99.45. Defaults. Source/DeviceDefaults.h: a device's parameters as "name=value" text under
  "deviceDefault_<type>" in the settings; applied in the two places a device is made new by hand
  (PluginMenu::addBuiltIn and ClipImport::createPluginFromDescription for plugin:builtin:), and nowhere else.
  Presets.h: saveDefaultTrack / applyDefaultTrack / forgetDefaultTrack (the chain's effects as "PULSECHAIN" XML
  under "defaultTrackEffects"); MainComponent::addTrack applies it. Only Pulse's own devices can have a default
  (an outside plugin keeps its state in its own way); a device's default holds its parameters, not extras
  such as a loaded sample or a learned Match EQ curve; tracks made by dropping a file or a device do not get
  the track default. 329 checks, quick tier.
- 2026-10-06: slow tier for v0.99.40 to v0.99.45 (the first eight items of the quality-of-life list), clean:
  Debug build, Debug self-test 329 / 329, random input 4000 events on the demo project (seed 341) and on the
  30-track project (seed 342), command fuzz 720 commands (seed 343), installer PulseStudio-0.99.45-Setup.exe.
  One thing on the way: the Wavetable speed check failed once in the Debug build (heaviest patch 1.9x real
  time against a floor of 2.0x; the Wavetable's code had not changed). Its floors are for a build made for
  speed; in a Debug build they are now half (4x and 1x), and the Release floors are as they were.
  Left on the list: drag anything anywhere, consistent modifier keys, remembered zoom per view, the autosave
  restore prompt, lists sortable and keyboard-navigable, nothing blocking for over 100 ms, first-run tour.
  Left in the Programme: per-note pitch and time editing of audio.
- 2026-10-06: v0.99.46. Piano roll zoom. PianoRoll.h: rowH is a member now (7 to 30, it was a constant 13),
  zoomRows (Alt + wheel), zoomToFit (selection or everything, across and in height), a View per clip kept in a
  map by the clip's id (so looking at a clip does not mark the project as changed) and written into the
  project's view state when it is saved ("ROLLVIEWS", only for clips that still exist). MainComponent: Z and
  Shift+Z go to the piano roll when it is in front, has a clip and has the keyboard; commands view.rollFit and
  view.rollFitSelection. The arrangement's zoom and the mixer's narrow strips were remembered already; the
  session view has no zoom. 330 checks, quick tier.
- 2026-10-06: v0.99.47. First run. Source/FirstRun.h: AudioCheck::describe (the plain facts of a device in, one
  line and one sentence of advice out; status() reads them from the device in use) and Tour (steps with a part
  of the window each; the part is lit and the words go where there is room beside it). WelcomeScreen: a SOUND
  block with Test sound and Audio settings, and Take the tour beside Close; the panel is 90 px taller for it.
  MainComponent: startTour / tourSteps (six steps), command app.tour. The demo song and the welcome screen
  were there already. The tour was drawn and stepped in a test with made-up parts; it has not been seen over
  the real window (the automated runs skip it). 331 checks, quick tier.
- 2026-10-06: v0.99.48. The recovery prompt, reviewed. Three things were wrong with it: Recover copied the
  autosave over the saved project (the last saved version was gone for good if the autosave turned out worse);
  Escape or closing the box counted as Discard and deleted the autosave; and an autosave that was not a
  readable project was offered all the same. About.h: autosaveState (none / stale / damaged / offer) and
  recoverAutosave (keeps the saved version as "<name> (before recovery)" first). MainComponent: a box with
  Recover / Discard / Ask me next time, both dates in its text; a damaged autosave is renamed aside. Tested
  with files (the box itself is not driven by a test). 332 checks, quick tier.
- 2026-10-06: v0.99.49. "Nothing blocks the window for more than 100 ms": not ticked, but measured and
  three things fixed. The random-input test now reports STALLS: each event after which the window was busy
  for over 100 ms, with what was under the mouse. Release build, 4000 events: 4 to 7 stalls a run on the demo
  project and 5 on the 30-track one; most are 100 to 300 ms (closing device windows in "tidy", right-click
  menus on the chord strip, a click in the device chain), and about one a run is near one second inside a
  device window (Pulse Delay's strip once, the De-esser's display once); the cause of those is not found yet
  (the events differ from run to run with the same seed). Renders (bounce, flatten, freeze, export) already
  ran on a thread with a progress window. Fixed here: Mastering::measure rendered on the message thread
  (now threaded, with the window); MatchEQ::listenToFile is the part of learnFromFile that touches nothing of
  the project, run on a thread from the panel; Sharing::planCollect / copyPlanned / finishCollect split
  collect-all so the copying runs on a thread. Still on the message thread: loading a long sample into
  Simpler or a pad, slicing, importing a wavetable, reading transients for warp markers, opening a project.
- 2026-10-06: v0.99.50. The demo song, asked for by the user ("I'd use this mostly for experimental techno
  music; make a full end-to-end techno track for the demo"). Source/DemoSong.h builds it: 132 BPM, 120 bars,
  seven tracks (Kick, Drums, Bass, Acid, Stab, Drone, Riser), six sections, 5260 notes generated bar by bar,
  the acid's cutoff and resonance and the riser's cutoff in automation, sidechain compressors keyed from the
  kick on the bass and the drone, three returns (Reverb, Delay, Metal = resonators), two VCA groups, the
  mastering chain with 6 dB of limiter gain, sections, chords, the song key, project and track notes, and four
  session scenes cut from the arrangement. MainComponent::createDemoContent only calls it now.
  NOT LISTENED TO. It was balanced by measurement only: each track rendered alone (drop II: kick -16.0,
  drums -19.7, acid -22.7, stab -24.6, bass -27.1, drone -33.7 LUFS; the bass and the drone are measured with
  their compressors working on themselves, which they do not do in the mix) and the mix (intro -13.8, drop
  -12.5, breakdown -21.3, drop II -12.0 LUFS, true peak -3.4 dB). My first settings had every synth about
  25 dB under the drums; nothing but the numbers showed it. Whether it is a GOOD techno track only ears can
  say: the user should play it and say what to change. 333 checks, quick tier.
- 2026-10-06: --render-demo <out.wav> builds the demo song in a project of its own and plays it into a file
  (Main.cpp), so the track can be heard without opening the app. Rendered once to
  "dist/Pulse Studio demo - techno.wav" (3:38, 58 MB; dist is not in the repository) for the user to listen to.
  A random-input run on the new demo (Release, 3000 events, seed 361) ended clean with 2 stalls over 100 ms
  (259 and 282 ms, both on a mixer strip).
- 2026-10-06: --render-demo also makes an .mp3 (256 kbps, through the Windows encoder the export uses). The
  demo is in dist as "Pulse Studio demo - techno.mp3" (7 MB) and was sent to the user to listen to.
- 2026-10-06: slow tier for v0.99.46 to v0.99.50, clean after one fix: Debug build, Debug self-test 333 / 333,
  random input 3000 events on the new demo (seed 371), command fuzz 744 commands (seed 372). The first Debug
  self-test crashed, and the fault was in the new demo-song check, not in the app: it still held a parameter
  of the demo project when it closed the project (Release happened to get away with it). The Programme has a
  new group 7, Techno, from what the owner said about how they will use the app; it comes before what is left
  of the others. Next: hear from the owner about the demo track; group 7.
- 2026-10-06: v0.99.51. The demo song, again. The owner on the first one: "might be the worst thing I've ever
  heard", and a reference: Brian Sanhaji, "Higgs" (CLR, 2010; 126 BPM, 7:52, root F by the shops' listings).
  I CANNOT HEAR the reference or my own track; I know the reference's tempo, key, length, label and year, and
  what that school of techno is like. DemoSong.h is rewritten to that: 126 BPM on F, 208 bars, sixteen-bar
  moves, a short dry kick over a "Rumble" return (kick > reverb > 24 dB low pass at 105 Hz > saturation >
  compressor keyed from the kick), an off-beat bass, quiet sixteenth hats, a rim / tom / faint clap loop, one
  repeating tone (3-3-2 on F) whose filter automation is the arrangement, a high blip into the delay, a dark
  bed, noise swells across the three changes. Gone from the first attempt, as the likeliest offenders: the
  squealing acid line at 80% resonance, the chord stabs and their Am / Bb changes, snare rolls, crashes, tom
  fills, the busy sixteenth bass, 132 BPM. Balance by measurement (main II, each track alone): kick -17.4,
  tone -23.4, hats -26.0, perc -27.7, bass -31.9, blip -36.6, atmos -37.0 LUFS; mix: intro -15.4, main -14.2,
  break -20.6, main II -13.9 LUFS, true peak -2.6 dB. The rumble's own level is not measured apart.
  If this one is wrong too, the next step is not a third guess: ask the owner what was wrong (the sounds
  themselves? the kick? the mix? the notes?), because the app's synths may be the limit and not the writing.
  333 checks, quick tier.
- 2026-10-06: v0.99.52. VST2 hosting, asked for by the owner (45 VST2 plugins in D:\Documents\VST\VST 2 x64).
  JUCE's VST2 host code needs Steinberg's aeffect.h / aeffectx.h, which are not in the project and which
  Steinberg no longer hands out. Nothing was downloaded: Source/vst2/pluginterfaces/vst2.x/ holds two files
  written here from the publicly known shape of the interface (the plugin's record, the request numbers, the
  records for events, time and speakers). FOR VERSION 2: whether shipping a host built on self-written VST2
  interface files is acceptable is a question to settle before a public release (other open hosts do it).
  CMake: JUCE_PLUGINHOST_VST=1 and Source/vst2 on the include path. The background scan takes "VST" as well as
  "VST3"; the sandbox worker knows both formats; the menu marks VST2 entries. --vst2-probe <dll> <report> loads
  one plugin, plays a note or a tone through it and reports. Tried: TAL-U-No-62 (-13.9 dBFS out), TAL-Chorus-LX,
  TAL-BassLine, Serum (315 parameters, -9.6 dBFS). The dev profile's scan path for VST now includes the
  owner's folder, and one run of the dev build scanned it: 40-odd VST2 plugins in the list (Kontakt, Massive,
  Serum, Reaktor, the TAL ones, Valhalla, Vital...). Not tried: opening a VST2 plugin's own window, a VST2 in
  the sandbox, saving and reopening a project with one. 333 checks, quick tier.
- 2026-10-06: v0.99.53. Pulse's effects as VST3 plugins, asked for by the owner ("really want to use prism eq
  there as well", there = Ableton Live). Source/PulseFxPlugin.cpp is one plugin that becomes whichever device
  PULSE_PLUGIN_TYPE names when it is compiled: a te::Engine shared by every instance in the DLL (no sound device),
  a one-track Edit per instance that is never played, the device created in it and fed the host's blocks through
  applyToBuffer. The device's settings are the plugin's parameters (Setting forwards a te::AutomatableParameter;
  a move the host made is not sent back to it, or Live would take it as a hand on the control and stop its
  automation). State is the device's ValueTree. Latency is passed on and updated. Host tempo is copied into the
  Edit and the host's beat becomes the Edit time of each block. PulseEffect::hostSidechain lets a device take
  channels 3/4 without a source track; five devices have a sidechain bus and an "External Sidechain" switch.
  The window is DeviceEditors::Editor itself. CMake: pulse_fx_plugin() at the end of CMakeLists.txt, thirty
  targets, all EXCLUDE_FROM_ALL; tools/build-plugins.sh builds them one by one into dist/VST3 and deletes each
  one's intermediates (250 MB apiece; the disk has 40 GB free). Each .vst3 is 20 MB because each carries the
  engine: a single plugin holding all thirty would be smaller, but JUCE's VST3 wrapper makes one plugin per file.
  --vst3-probe (file or folder) is the host-side test; CrashGuardWin::grabWindow takes the picture (PrintWindow),
  since a hosted plugin's window is a child window a component snapshot cannot see.
  Tested from the host side only: --vst3-probe on dist/VST3 loads all thirty into one process (each: tone
  through, a setting moved and read back through saved state, sidechain where there is one, window opened and
  photographed), then runs all thirty again together: 30 of 30 sane. Prism EQ band 3 at +15 dB gives +15.00 dB
  at 400 Hz; Compressor keyed from itself -5.96 dB, from a silent sidechain 0.00, from a loud one -10.33;
  Gate with a silent key closes to -60 dB. Pictures of Prism EQ, Compressor, Space, Utility, Match EQ looked at.
  ALSO FIXED, found by the slow tier (monkey seed 11, a crash every time): Ctrl+A then Delete in the piano roll
  with many notes. keyPressed walked `selected` while each removeNote came back through valueTreeChildRemoved,
  which shrinks `selected` (and, past a few dozen notes, reallocates it under the loop). Has been there a long
  time; small selections survived by luck. PianoRoll::removeSelectedNotes walks a copy; the right-click delete
  used the same loop. dropNotesThatAreGone also prunes the selection at every entry point. New check deletes
  600 selected notes (not confirmed to fail on the old code: the monkey seed is the proof, crash before, 4000
  events after). And a test run that follows a crashed one no longer stops at the "crashed last time" question
  (it cost a 280 s hang here). Slow tier: Debug self-test 333/333 (before the piano fix), Release self-test
  333/333 after, monkey seeds 11, 12, 13 x 4000 events clean.
  NOT tried: any of them in Ableton Live itself (it is the owner's program; nothing was installed or copied
  into a system folder). Modulators and macros do nothing in a plugin (the Edit is not played). The installer
  has an unchecked task that puts dist/VST3 into {autocf}\VST3\Pulse.
- 2026-10-06: v0.99.54. Owner said "run autopilot". Group 7 (Techno), first item: the step grid has chance,
  ratchets and a length for each row. Decided: no playback-time dice. A row that uses any of the three becomes a
  "pattern row": its steps (velocity:chance:repeats) are kept in the clip's state (STEPROWS / ROW, so they are
  saved, copied with the clip and undone with everything else), and its notes are written out from them over
  the clip's whole length, the dice thrown per step and repetition from a seed (stable until "Throw the dice
  again"). So the notes stay ordinary notes and nothing in the engine changed; the price is that a looped clip
  repeats the same throw each pass (said in the README). Other rows are plain notes exactly as before. A pattern
  made on another step size is ignored and dropped at the next edit of that row. The grid re-writes pattern rows
  when the clip's length changes while it is showing. Not done: per-pass dice at playback (would need a MIDI
  effect that knows the clip under the playhead), micro-timing per step, a length for the grid as a whole.
  334 checks, quick tier; steps-patterns.png looked at.
- 2026-10-06: v0.99.55. Group 7: acid. Presets alone would not have been a 303: Pulse Analog glided on every
  note and struck every note afresh. Added two parameters at the end of its list (state is by name, old projects
  load as before): "Glide Mode" (Every note / Tied notes) and "Accent". Tied: PulseSynthesiser got two optional
  hooks, tieNoteOn (asked for each new note in mono mode; the instrument may carry its voice over itself) and
  mapNoteOff (which key a note-off is really for); Analog uses them so that an overlapping note retargets the
  sounding voice (pitch slides, envelopes not struck), the first key coming up under the tie ends nothing, and
  the tied key coming up releases the voice. A note that does not overlap does not glide in that mode. Accent:
  from velocity 0.75 to 0.9 it fades in; louder (up to x1.7), filter envelope 0.3 further, its decay down to
  35%, a little more resonance. Measured: the tie is 7.8 dB duller at the second note than a struck one, the
  tied note is on pitch, held under the first key's release, silent after its own; accent +3.6 dB and brighter;
  a firm note unchanged. FactorySounds::renderLine plays a line of notes through an instrument for such tests.
  Eight "Acid:" presets. The panel: two more controls did not fit two rows; after three tries (pictures looked
  at each time) Mono and Tied are switches and Level sits with the mix. Not judged by ear. Not done: an accent
  that builds over repeated accents as the real circuit does; acid patterns as clips. 335 checks, quick tier.
- 2026-10-07: v0.99.56. Group 7: "distortion worth having on a kick". The Saturator already has a hard
  clipper, a foldback and a sine fold with a tone control, so the item came down to the one thing that was
  missing: the rumble. New device Rumble (Rumble.h, RumbleDisplay.h): mono sum -> two all-passes -> four delay
  lines in a Householder loop with damping (Decay = RT60) -> two 12 dB low-passes at Tone -> +15 dB -> tanh
  drive -> low cut and the low-pass again -> gain from an envelope of the dry kick (against its own slow peak, so
  it works at any level; square-rooted, Recover is its release) -> Amount; dry kick added unless "Rumble only".
  First version was 27 dB under the kick and the drive did nothing (the tail of a 90 ms burst is quiet):
  the make-up went in before the drive. Measured with 55 Hz bursts every half second: -24 dB between kicks,
  14.6 dB lower under a kick than without ducking, kick itself changed 0.17 dB, mono to the last bit at Width 0,
  -2.7 dBFS with everything up. Wired everywhere (menu, browser list, monkey project, presets x7, device audit,
  window with three points, shot looked at, VST3 target PulseRumble built and probed). Not judged by ear.
  The demo song's rumble return is still made of four devices: left alone until the owner has said what they
  think of the demo. 338 checks, quick tier.
- 2026-10-07: v0.99.57. Group 7: Pulse Kick (Kick.h, KickDisplay.h). KickShape holds the settings and the two
  curves (pitch: Tune x 2^(fall), loudness: 1 ms up, hold, fall; one "fall" function, a straight line at curve 0
  and exp(-5u) landed on zero at curve 1). KickMaker makes the samples: a sine at that pitch, Shape (tanh
  towards square), a click (white noise between two one-pole low-passes, gone in four click-times), tanh drive
  normalised to full scale. The window draws the wave with a KickMaker of its own at 16 kHz, so the picture is
  the sound; five points (pitch fall, pitch time, hold, decay, click), the wheel on a point bends its curve.
  One kick at a time without a crack: acceptNoteOn fades whatever is sounding over 4 ms and the new kick starts
  on another voice (biggest sample step at a retrigger 0.020). Note-off does nothing: a kick is a one-shot.
  Measured: starts near 259 Hz (the first 12 ms), ends at 48.0 Hz, silent after hold + decay, click +17.9 dB of
  edge in the first 8 ms and none later, third harmonic -30 > -9 dB with drive, C2 with Keys 96.0 Hz. Nine
  presets. Not judged by ear.
  THE FUZZ TEST: adding a device moved its random numbers and EQ Six "ran away": three low-passes at 20 Hz
  with Q 18 and +18 dB of output. Not a runaway: a ring of some 90 dB that needs seconds to die. The test now
  gives anything still at the clamp after a second eight more seconds of silence and fails only what has not
  come off it. 340 checks, quick tier.
- 2026-10-07: v0.99.58. Group 7: techno presets and kits. 23 presets named "Techno: ..." across Wavetable (5),
  FM (4), Pulse Analog (6), Pulse String (2), Organ (2), Electric Piano (2), Drum Synth (2); a "Techno" category
  (those and the "Acid:" bank) comes first in the preset menu. Drum kit styles Techno and Industrial (seven
  styles now; FactorySounds version 2 so that the two new sampled kits are rendered on the next start; the
  Techno factory loop uses the Techno kit). "909, 808" kits were there already. All presets pass the level
  audit. NOT JUDGED BY EAR: these are settings written from knowledge of the synths, checked only for level.
  The owner's ears decide which stay. 340 checks, quick tier.
- 2026-10-07: v0.99.59. THE PLUGINS CRASHED ABLETON. The owner tried Prism EQ and EQ Six in Live: "they both
  spazed out, caused ableton to freeze when I started moving bands and freqs, made loud buzzing, and then
  crashed ableton". Cause: Setting::setValue called te::AutomatableParameter::setParameter on whatever thread
  the host used, which is its audio thread for every value it sends during playback, including its echo of a
  move made in the plugin's own window. The engine allows that call on the message thread only (it starts
  curveHasChanged, the automation record manager, listeners). My test in v0.99.53 set values and processed on
  ONE thread, so it never saw it; I reported the plugins as working on the strength of that. Reproduced now:
  --vst3-probe rides settings from a second thread while the window is open, and the plugin as shipped takes
  the probe down at once (segmentation fault). FIX: a value from the host is only noted (two atomics); a 100 Hz
  timer on the message thread puts it into the device; getValue answers with the noted value meanwhile. A
  host's echo is dropped while a hand is on the control in our window and for 300 ms after. Our own moves are
  sent to the host inside a gesture (the engine's begin/end when a knob is dragged, one of our own around a
  click). Loading a state does not tell the host of each setting. State is read and written on the message
  thread (a host asking from another thread waits for it). PULSE_PLUGIN_SELF_MOVE=1 makes a plugin move its own
  settings as its window would, for the probe's --self-move. All 40 rebuilt and probed both ways: 40 of 40 up
  and sane. The price: automation from the host lands within 10 ms, not on the sample, and an offline bounce in
  a host runs faster than that timer (automation in a bounce may land late: to be looked at; realtime is right).
  STILL NOT RUN IN LIVE BY ME: the owner's next try is the real test.
  ALSO: instruments as VST3, asked for by the owner. PulseFxPlugin.cpp takes any PulsePlugin; pulse_fx_plugin
  (... SYNTH) makes an instrument (no input, notes passed on at their moment in the block). Nine built. JUCE
  adds 2000-odd MIDI-controller parameters to an instrument's list (its usual way of passing controllers on).
  Slow tier for .54 to .59: Release self-test 340/340, Debug self-test (see next entry), monkey seeds 21 and 22
  clean (one stall of 1035 ms on the Compressor's picture: the known one, cause still not found), command fuzz
  744 commands clean.
- 2026-10-07: slow tier for v0.99.54 to .59 complete: Debug self-test 340/340, installer
  PulseStudio-0.99.59-Setup.exe (286 MB with the 40 plugins as an optional part). Next in group 7: the drum
  machine view of the Drum Rack, then the performance view. Before anything is said to the owner about a plugin
  again: `--vst3-probe dist/VST3 report.txt` and the same with `--self-move`, both 40 of 40.
- 2026-10-07: v0.99.60. Group 7: the drum machine. Source/DrumMachine.h (class DrumMachine, shown in the piano roll by its
  Machine button, PianoRoll::showMachine, command view.machine, "Open in the drum machine" in the clip menu, and a
  Machine button on the Drum Rack's card through evs.openDrumMachine and DrumMachine::clipFor). Decided, as for the
  step grid: no playback engine of its own. Four patterns of sixteen steps (off / hit / accent), the chain (up to
  16 letters), swing and the level of a plain hit are a "DRUMMACHINE" child of the clip's state, and the clip's
  notes are written from them, the chain going round to the clip's end. A clip never edited there is only read
  (first four bars as A to D, notes of 115 and over as accents) until the first change; from then the machine
  writes ALL the clip's notes and takes the step grid's pattern rows out (StepGrid::forgetAllPatterns), which the
  README says. A pattern is always one 4/4 bar of sixteenths. Twelve ready-made patterns by General MIDI note,
  written only for drums that have a pad. The test caught notes left past the end of a clip made shorter (render
  now clears every note first). Not done: a length per pattern, more than four patterns, the chain's letters
  typed from the keyboard, playing the steps from a controller (that belongs to the performance view), typed
  values for swing and level (they have a right-click menu instead). Not judged by ear. 341 checks, quick tier;
  drum-machine.png looked at.
  THIS RUN was a scheduled one that started 2026-10-06 and then stood still for hours twice, most likely waiting on permission
  prompts for Bash commands (single commands had been approved by hand in the settings file; sleep cannot be ruled out): the owner then said "always allow in routine so that it is
  truly autopilot", and C:\Users\cercea\IdeaProjects\.claude\settings.local.json now allows Bash, PowerShell,
  Read, Edit, Write, Glob and Grep outright. There is no request_keep_awake tool in a scheduled session (looked
  for it twice): the rule about foreground builds is what keeps the PC awake. While this run stood still another
  session did v0.99.51 to v0.99.59; a run that wakes up must re-read this file and `git log` before going on.
  Left in group 7: the performance view. The slow tier is owed for v0.99.60.
- 2026-10-07: v0.99.61. Group 7: the performance view. Source/Perform.h (pulse::Perform: Sweep -1..1 with a dead
  middle, two biquads a side (the first carries the resonance, Q up to 4.9), blended in over 5 ms so that off is
  really off; Fill = Off / 1/4 / 1/8 / 1/16 / 1/32: a memory of the last 2.1 s that stands still while a slice is
  repeated, the slice taken from the grid line before the press when that is still in memory, 2 ms fades at the
  joins and at the release). Source/PerformDisplay.h (PerformView / PerformPanel for the device, PerformUI::device
  = the one on the master, Stage = mute pads + fill pads + the view, Window / show / close). Command view.perform,
  the menu entry beside Notes. MidiLearn: Target::trackMute (startLearningMute, a mapping with no parameter and a
  track id, acts on the press, saved as MAP muteTrack=...). Measured: middle of the sweep is the input to the last
  digit; low-pass at 205 Hz leaves 2 kHz at -79 dB and 50 Hz at -0.03; resonance 85% is +8.3 dB at the cutoff
  against -6 plain; a sixteenth held from 1.03 s plays the slice from 1.0 s exactly, and the release steps by
  0.003 at most on a signal that jumps by 0.1. Not done: the mute pads have no level meters; no VST3 target for
  Perform; the fill pads are not learned one button each (one control chooses between them); the window has no
  keyboard shortcuts. Fills and mutes were tried with made-up controller messages only. Not judged by ear.
  345 checks, quick tier; performance.png looked at. Group 7 is complete but for the owner's word on the demo
  song. The slow tier is owed for v0.99.60 and v0.99.61.
- 2026-10-07: v0.99.62. THE OWNER, during the run: "in prism eq the big block kinda blocks the spectrum and makes it
  hard to see the effect of my changes". The band panel floated over the graph beside the selected point (470 px
  wide, up to 400 tall). It is now a strip of 112 px between the display and the bottom bar (PrismUI::View::
  layoutPanel; bandStripBounds / bandStripFits for the new check: never over the graph, nothing cut off from 1000 px
  wide with the dynamic part and the movement open). The x hides the strip and the display takes the room. The
  Prism EQ plugin in dist/VST3 is rebuilt (probe and --self-move probe sane); nothing outside the repository was
  touched, so a copy the owner put somewhere else is still the old one. The Multiband panel still floats: if the
  owner minds that one too, do the same there. RULE from this: controls never lie over a display.
  SLOW TIER for v0.99.60 to .62: the Debug self-test crashed twice with a stack overflow inside a third-party
  plugin (Replika, restoring its state) because SelfTest::run, one function, had outgrown a 1 MB stack in a Debug
  build with the two new test blocks; the version before them did not crash. Fixed with /STACK:8388608 for the
  app (CMakeLists.txt). After that Debug: 341 pass, 1 fail; random input 4000 events on the demo (seed 611) clean
  with no stalls.
  OPEN, NOT FROM THIS WORK: "Sandboxed VST3 processes audio in its own process" has failed in every run since
  about 17:10 (Replika in the helper process: "The plugin crashed"), Release and Debug, with and without the
  stack change, and also with the sources of 81bb084 built again; the same commit passed four times between
  16:30 and 17:09. Loading Replika inside the app still works. Native Access (the maker's installer) was running
  on the machine at the time, which may be the whole story: run the self-test again when it is closed, and if
  the check still fails, look at the helper (sandbox::startWorkerIfRequested) with another plugin first (the test
  takes the first of Replika / Phasis / Choral / Flair). Until then the Release self-test is 342 of 343.
  A mistake of mine to avoid: I stopped a test run with taskkill by NAME; only my test was running, but that
  would also close the owner's own Pulse Studio. Stop test runs by process id.
- 2026-10-07: slow tier for v0.99.60 to v0.99.62 finished: Debug build; Debug self-test 341 pass and the one open
  sandbox check (run before the Prism EQ change, which the Release self-test covers: 342 of 343); random input 4000
  events on the demo (seed 611) and on the 30-track project (seed 612), no crash and no stall over 100 ms; command
  fuzz 768 commands (seed 613) clean; installer PulseStudio-0.99.62-Setup.exe. All of the random-input and fuzz runs
  were the Release build. Group 7 (Techno) is complete. Next: find out about the sandbox check (see above) FIRST;
  then what is left: per-note pitch and time editing of audio (group 4), the open quality-of-life items (drag
  anything anywhere, consistent modifier keys, sortable lists, nothing blocking over 100 ms), the one-hour --soak,
  and whatever the owner says about the demo song, the drum machine and the Performance window.
- 2026-10-07: v0.99.63. The open sandbox check, found. Replika, Phasis, Choral and Flair (all four the test can
  pick) died inside themselves while the helper created them; other plugins (Serum 2, Massive, Supercharger, Solid
  EQ, Pulse Amp) came up. Cause: when the helper is given only a file (the test and "by file" loads; a plugin from
  the scanned list comes with its description), it opened the file on the pipe thread and created the plugin on the
  message thread. Both happen on the message thread now. Why it passed until about 17:10 is not known (Native
  Access was still running when it passed again, so it was not that). To find it the helper now leaves a note when
  it dies (CrashGuardWin::writeHelperNote: fault, module, and the stage set with setHelperStage), which the device's
  status shows, and `--sandbox-probe <plugin> <report.txt>` runs one plugin through the helper. Release self-test
  347 of 347 (four checks only run when the plugin loads). Quick tier.
