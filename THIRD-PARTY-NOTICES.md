# Third-party software in Pulse Studio

Pulse Studio is built with the components below. Each one stays under its own licence; the
full licence texts are in the source tree under `external/` (and at the addresses given).

| Component | What it does here | Licence |
| --- | --- | --- |
| **JUCE 8** (Raw Material Software) | Windows, drawing, audio and MIDI devices, plugin hosting | AGPLv3, or a commercial JUCE licence - https://juce.com/legal/juce-8-licence/ |
| **Tracktion Engine** (Tracktion Corporation) | The audio engine: tracks, clips, playback, recording, rendering | GPLv3, or a commercial Tracktion Engine licence - https://github.com/Tracktion/tracktion_engine |
| **Ableton Link** | Tempo sync with other apps on the network | GPLv2 or later, or a proprietary licence from Ableton - https://github.com/Ableton/link |
| **ARA SDK** (Celemony) | Hosting ARA plugins such as Melodyne on audio clips | Apache License 2.0 - https://github.com/Celemony/ARA_SDK |
| **VST3 SDK** (Steinberg), through JUCE | Hosting VST3 plugins | GPLv3 or the Steinberg VST3 licence. VST is a registered trademark of Steinberg Media Technologies GmbH |
| **libusb** | Talking to the Ableton Push 2 display over USB | LGPL 2.1 or later - https://libusb.info |
| **Windows Media Foundation** (part of Windows) | MP3 export | Used through the operating system; nothing is bundled |

Optional, only if you choose to install it from inside the app: **Demucs** (stem separation, MIT
licence) with **PyTorch** (BSD-style licence). They are downloaded to your user folder on request
and are not part of the installer.

## What this means for distribution

JUCE, Tracktion Engine and Ableton Link are each offered under a copyleft licence (AGPL / GPL) or a
paid commercial one. Without commercial licences for them, Pulse Studio as a whole can only be
distributed under the GPL family of licences, which includes making its source code available to
the people who receive it. That choice belongs to the publisher of Pulse Studio.

Ableton, Push and Link are trademarks of Ableton AG. Melodyne is a trademark of Celemony Software GmbH.
Pulse Studio is not affiliated with or endorsed by any of the companies named here.
