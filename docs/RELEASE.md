# Releasing Pulse Studio

What has to be true before a build goes to other people, and how to make one.

## Decisions only the publisher can make

1. **Licence.** JUCE 8 (AGPLv3 or commercial), Tracktion Engine (GPLv3 or commercial) and Ableton Link
   (GPLv2+ or proprietary) are all part of the program. Without commercial licences for them, Pulse Studio
   can only be given to others under the GPL family of licences, with its source code available to whoever
   receives it. See `THIRD-PARTY-NOTICES.md`. Until this is decided there is no `LICENSE` file for
   Pulse Studio's own code in the repository.
2. **Code signing.** The installer and the program are not signed. Windows SmartScreen warns about
   unsigned downloads. Signing needs a code-signing certificate in the publisher's name; then sign
   `Pulse Studio.exe` before building the installer, and the installer after.
3. **Update address.** The update check is off until an address is entered (File > Check for updates).
   A public release needs a published `version.json` at an address you control, or the menu item removed.
4. **Name and trademarks.** "Pulse" is a common name in audio; check it is free to use where you publish.

## Before every release

- `Pulse Studio.exe --dev-profile --selftest report.txt` passes (exit code 0) **three times in a row**.
  The report lists every check; `report.progress.txt` shows how far it got if it stops.
- `Pulse Studio.exe --dev-profile --command-fuzz <file> --seed <n>` ends with `DONE` (exit code 0) for a few
  different seeds: every unattended command, many times, in a random order, on the demo project.
- `Pulse Studio.exe --dev-profile --monkey <file> --seed <n> --events 5000` ends with `DONE` for a few seeds:
  random clicks, drags, wheel turns and key presses on the main window. If it stops early, the last line of
  the file is the last thing it did, and a crash report is in the Crashes folder of the "Pulse Studio Dev" profile.
- `Pulse Studio.exe --dev-profile --idle-cpu <file>`: processor use stopped and playing has not gone up.
- Build the debug configuration too (`cmake --build build-dev --config Debug --target PulseStudio`) and run the
  self-test under it with `--log <file>`: it must pass with no crash report (a debug build checks things the
  release build lets through, such as reading past the end of a list).
- `Pulse Studio.exe --dev-profile --snapshots <folder>` and `--device-shots <folder>`: look at the pictures.
- Install the build on a machine (or user account) that has never had Pulse Studio: first run creates the
  built-in sounds, the demo song plays, a VST3 plugin scans and loads, export to WAV and MP3 works.
- Open a project made with the previous release; save it; open it again.
- `CHANGELOG.md` has an entry; the version is the same in `CMakeLists.txt`, `installer/PulseStudio.iss`
  and `About::stage()` in `Source/About.h` says what kind of build it is (empty for a final release).

## Making the build

```
cmake --build build --config Release --target PulseStudio
"%LOCALAPPDATA%/Programs/Inno Setup 6/ISCC.exe" installer/PulseStudio.iss
```

The installer lands in `dist/`. `Pulse Studio.pdb` (next to the exe in the build folder) is not installed;
keep it with the release so crash reports from that build can be read.

## What the self-test covers (and what it does not)

Covered: every built-in device processes audio and responds to its controls; every device, preset, copy and
whole project comes back from disk unchanged; undo and redo of edits, knobs and faders; random and extreme
settings on every device; every device window at several sizes; exports, recording paths, the crash handler,
backups, old-project upgrades.

Not covered (check by hand): how things sound, real audio interfaces and MIDI controllers, third-party
plugins beyond the scanner and sandbox paths, Ableton Push hardware, behaviour on a second monitor or at
unusual display scaling, and anything that needs a person to look at it for taste.

## Known limits in this release candidate

- The Wavetable's *Level* is before its effects (it sets how hard the distortion is driven); *Output* is after.
- Audio loop takes are not implemented (MIDI takes are).
