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
2. **Instruments (v0.18.0)**: the owner wants the Drum Rack to match Ableton's and go further:
   - Drum Rack: 128 pads in banks of 16 with bank switching, choke groups, velocity layers and round-robin per
     pad, per-pad start/end/ADSR/filter/pitch/reverse, per-pad mute/solo/volume/pan/sends to rack-internal
     returns, pad note remapping, copy/move/swap pads, drag samples and devices onto pads, macros for the rack,
     "extract pad to its own track", randomise kit.
   - Drum synth instrument (kick, snare, hats, clap, toms synthesised, no samples) usable on pads.
   - Multi-sample mapping for Simpler (key and velocity zones).
3. Then **think of more improvements** and do them in a cycle: polish, reliability, performance, UI clarity,
   missing DAW features (e.g. sidechain UI improvements, audio quantize, tuner/spectrum devices, MIDI
   learn for Push encoders, better browser search/tags, recent projects, auto-update of README docs).
   Pick the most valuable next item, add it to this queue, implement it, test it, commit.

## Log

- 2026-10-02: v0.13 UI refresh, v0.14 plugin sandbox / CPU / background export, v0.15 command palette /
  shortcuts / capture MIDI / group freeze, v0.16 automation curves / pencil / modulators / clip envelopes.
  All self-tests pass (107). Next: queue item 1 (audio editing).
- 2026-10-02: v0.17.0 audio editing done (sample editor, slice to Drum Rack, warp modes incl. bundled Signalsmith stretch). 111 checks pass. Next: queue item 2 (instruments / Drum Rack overhaul).
