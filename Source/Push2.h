#pragma once

#include "EditViewState.h"
#include "SessionView.h"
#include "EditingOps.h"
#include "DrumRack.h"
#include "Routing.h"

/** Ableton Push 2 support over its "Live" MIDI port (protocol from Ableton's public
    Push 2 MIDI & Display Interface Manual). The display (USB bulk) is not driven.

    Modes
      Session : 8x8 clip grid, scene buttons launch rows, upper row stops tracks, lower row selects tracks
      Note    : in-key 4ths layout for the selected track (scale/root via Scale button)
      Drum    : 4x4 pads for a Drum Rack track (automatic when the selected track has one)
    Encoders : Device (selected track's device, Page < > to switch device) or Mix (vol -> pan -> sends)
*/
class Push2Controller : private MidiInputCallback,
                        private AsyncUpdater,
                        private Timer
{
public:
    explicit Push2Controller (te::Engine& e) : engine (e) { startTimerHz (25); }

    ~Push2Controller() override
    {
        stopTimer();
        cancelPendingUpdate();
        disconnect();
    }

    void setView (EditViewState* v)
    {
        view = v;
        trackOffset = sceneOffset = 0;
        updateNoteTarget();
        markAllDirty();
    }

    bool isConnected() const { return output != nullptr; }

    String getStatus()
    {
        if (! isConnected()) return {};
        String s = "Push 2: ";
        switch (mode)
        {
            case Mode::session: s << "Session  tracks " << (trackOffset + 1) << "-" << (trackOffset + 8)
                                  << "  scenes " << (sceneOffset + 1) << "-" << (sceneOffset + 8); break;
            case Mode::note:    s << (isDrumTarget() ? "Drums" : "Notes  " + rootName() + " " + scaleName()
                                                                 + "  oct " + String (octave)); break;
        }
        s << "  |  encoders: " << (encoderMode == EncoderMode::device ? "Device" : mixModeName());
        return s;
    }

    std::function<void()> onToggleBrowser;   // Browse button

private:
    //==============================================================================
    enum class Mode { session, note };
    enum class EncoderMode { device, mix };
    enum class MixParam { volume, pan, sendA, sendB };

    // Push 2 control numbers
    enum CC
    {
        ccTapTempo = 3, ccMetronome = 9, ccTempoEnc = 14, ccLowerRow = 20, ccStopClip = 29, ccSceneLast = 36, ccSceneFirst = 43,
        ccLeft = 44, ccRight = 45, ccUp = 46, ccDown = 47, ccSelect = 48, ccShift = 49, ccNote = 50, ccSession = 51,
        ccOctDown = 54, ccOctUp = 55, ccScale = 58, ccMute = 60, ccSolo = 61, ccPageLeft = 62, ccPageRight = 63,
        ccTrackEnc = 71, ccMasterEnc = 79, ccPlay = 85, ccRecord = 86, ccDuplicate = 88, ccUpperRow = 102,
        ccDevice = 110, ccBrowse = 111, ccMix = 112, ccClip = 113, ccQuantize = 116, ccDelete = 118, ccUndo = 119
    };

    // Palette: 1..9 = our track colours, 10 = dim red, defaults for 122 white .. 127 red
    static constexpr int palDimRed = 10, palWhite = 122, palGrey = 123, palDark = 124, palBlue = 125, palGreen = 126, palRed = 127;

    //==============================================================================
    // Connection
    static bool isLivePort (const String& n)
    {
        return n.containsIgnoreCase ("Push 2") && ! n.containsIgnoreCase ("MIDIIN2") && ! n.containsIgnoreCase ("MIDIOUT2")
               && ! n.containsIgnoreCase ("User");
    }

    void tryConnect()
    {
        String inID, outID;
        for (auto& d : MidiInput::getAvailableDevices())  if (isLivePort (d.name)) inID = d.identifier;
        for (auto& d : MidiOutput::getAvailableDevices()) if (isLivePort (d.name)) outID = d.identifier;
        if (inID.isEmpty() || outID.isEmpty())
            return;

        // Release the Push ports from Tracktion so pads don't also play tracks directly.
        auto& dm = engine.getDeviceManager();
        for (auto& d : dm.getMidiInDevices())
            if (d->getName().containsIgnoreCase ("Push 2") && d->isEnabled())
                d->setEnabled (false);
        for (int i = 0; i < dm.getNumMidiOutDevices(); ++i)
            if (auto d = dm.getMidiOutDevice (i))
                if (d->getName().containsIgnoreCase ("Push 2") && d->isEnabled())
                    d->setEnabled (false);

        input = MidiInput::openDevice (inID, this);
        output = MidiOutput::openDevice (outID);
        if (input == nullptr || output == nullptr)
        {
            input = nullptr; output = nullptr;   // port still busy, retry later
            return;
        }

        portName = inID;
        input->start();
        sysex ({ 0x0A, 0x00 });          // Live mode
        setupPalette();
        markAllDirty();
        if (view != nullptr) view->pushConnected = true;
    }

    void disconnect()
    {
        if (output != nullptr)
        {
            for (int n = 36; n < 100; ++n) output->sendMessageNow (MidiMessage::noteOn (1, n, (uint8) 0));
            for (int c = 0; c < 128; ++c)  output->sendMessageNow (MidiMessage::controllerEvent (1, c, 0));
        }
        if (input != nullptr) input->stop();
        input = nullptr;
        output = nullptr;
        if (view != nullptr) view->pushConnected = false;
    }

    void sysex (std::initializer_list<uint8> body)
    {
        if (output == nullptr) return;
        std::vector<uint8> data { 0x00, 0x21, 0x1D, 0x01, 0x01 };
        data.insert (data.end(), body.begin(), body.end());
        output->sendMessageNow (MidiMessage::createSysExMessage (data.data(), (int) data.size()));
    }

    void setPaletteEntry (int idx, Colour c, int white = 0)
    {
        auto lo = [] (int v) { return (uint8) (v & 0x7F); };
        auto hi = [] (int v) { return (uint8) ((v >> 7) & 0x01); };
        const int r = c.getRed(), g = c.getGreen(), b = c.getBlue();
        sysex ({ 0x03, (uint8) idx, lo (r), hi (r), lo (g), hi (g), lo (b), hi (b), lo (white), hi (white) });
    }

    void setupPalette()
    {
        for (int i = 0; i < 9; ++i)
            setPaletteEntry (1 + i, Theme::trackColour (i));
        setPaletteEntry (palDimRed, Colour (0xff400000));
        sysex ({ 0x05 });   // re-apply palette
    }

    static int paletteFor (Colour c)
    {
        int best = 1;
        float bestDist = 1.0e9f;
        for (int i = 0; i < 9; ++i)
        {
            auto t = Theme::trackColour (i);
            auto d = (float) (square (t.getRed() - c.getRed()) + square (t.getGreen() - c.getGreen()) + square (t.getBlue() - c.getBlue()));
            if (d < bestDist) { bestDist = d; best = 1 + i; }
        }
        return best;
    }

    //==============================================================================
    // Incoming MIDI (MIDI thread)
    void handleIncomingMidiMessage (MidiInput*, const MidiMessage& m) override
    {
        // Low-latency path: playing notes in note/drum mode goes straight to the track.
        if ((m.isNoteOnOrOff() || m.isAftertouch()) && m.getNoteNumber() >= 36 && m.getNoteNumber() < 100)
        {
            const SpinLock::ScopedLockType sl (noteLock);
            if (noteMode && noteTarget != nullptr)
            {
                const int pitch = padPitch[(size_t) (m.getNoteNumber() - 36)];
                if (pitch >= 0)
                {
                    MidiMessage out = m.isAftertouch() ? MidiMessage::aftertouchChange (1, pitch, m.getAfterTouchValue())
                                    : m.isNoteOn()     ? MidiMessage::noteOn (1, pitch, (uint8) m.getVelocity())
                                                       : MidiMessage::noteOff (1, pitch);
                    noteTarget->injectLiveMidiMessage (out, sourceID);
                    if (! m.isAftertouch())
                        pressed[(size_t) (m.getNoteNumber() - 36)] = m.isNoteOn();
                }
                if (! m.isAftertouch()) { queueEvent (m); }   // also update LEDs
                return;
            }
        }

        if (m.isPitchWheel() || m.isChannelPressure())
        {
            const SpinLock::ScopedLockType sl (noteLock);
            if (noteMode && noteTarget != nullptr)
                noteTarget->injectLiveMidiMessage (m, sourceID);
            return;
        }

        if (m.isController() || m.isNoteOn())
            queueEvent (m);
    }

    void queueEvent (const MidiMessage& m)
    {
        {
            const SpinLock::ScopedLockType sl (queueLock);
            queue.push_back (m);
        }
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        std::vector<MidiMessage> events;
        {
            const SpinLock::ScopedLockType sl (queueLock);
            events.swap (queue);
        }

        if (view == nullptr) return;

        for (auto& m : events)
        {
            if (m.isController())
            {
                const int cc = m.getControllerNumber(), v = m.getControllerValue();
                if (isEncoder (cc)) handleEncoder (cc, v < 64 ? v : v - 128);
                else                handleButton (cc, v > 0);
            }
            else if (m.isNoteOn() && ! noteMode)
            {
                handlePad (m.getNoteNumber() - 36);
            }
        }
        refreshLeds();
    }

    static bool isEncoder (int cc) { return cc == ccTempoEnc || cc == 15 || cc == ccMasterEnc || (cc >= ccTrackEnc && cc < ccTrackEnc + 8); }

    //==============================================================================
    te::Edit& edit() { return view->edit; }
    juce::Array<te::AudioTrack*> tracks() { return EngineHelpers::getUserTracks (edit()); }
    te::AudioTrack* selectedTrack() { return EngineHelpers::getSelectedTrack (view->selectionManager); }

    void selectTrack (te::AudioTrack* t)
    {
        if (t == nullptr) return;
        view->selectionManager.selectOnly (t);
        EngineHelpers::routeMidiInputsTo (*t);
        updateNoteTarget();
    }

    void handleButton (int cc, bool down)
    {
        if (cc == ccShift)     { shift = down; return; }
        if (cc == ccDelete)    { deleteHeld = down; return; }
        if (cc == ccDuplicate) { if (down) { dupUsed = false; dupHeld = true; } else { dupHeld = false; if (! dupUsed) EditingOps::duplicate (view->selectionManager); } return; }
        if (! down) return;

        auto& tc = edit().getTransport();
        auto& um = edit().getUndoManager();

        if (cc >= ccLowerRow && cc < ccLowerRow + 8)
        {
            selectTrack (tracks()[trackOffset + cc - ccLowerRow]);
            return;
        }

        if (cc >= ccUpperRow && cc < ccUpperRow + 8)
        {
            const int i = cc - ccUpperRow;
            if (encoderMode == EncoderMode::device) { deviceIndex = i; return; }   // pick a device
            if (auto t = tracks()[trackOffset + i]) Launcher::stopTrack (*t);
            return;
        }

        if (cc >= ccSceneLast && cc <= ccSceneFirst)
        {
            Launcher::launchScene (edit(), sceneOffset + (ccSceneFirst - cc));
            return;
        }

        switch (cc)
        {
            case ccPlay:      if (shift) { tc.stop (false, false); tc.setPosition (0s); } else EngineHelpers::togglePlay (edit()); break;
            case ccRecord:    EngineHelpers::toggleRecord (edit()); break;
            case ccMetronome: edit().clickTrackEnabled = ! edit().clickTrackEnabled.get(); break;
            case ccUndo:      if (shift) edit().redo(); else edit().undo(); break;
            case ccSession:   setMode (Mode::session); break;
            case ccNote:      setMode (Mode::note); break;
            case ccOctUp:     octave = jmin (8, octave + 1); updateNoteTarget(); break;
            case ccOctDown:   octave = jmax (-1, octave - 1); updateNoteTarget(); break;
            case ccScale:     if (shift) root = (root + 1) % 12; else scaleIndex = (scaleIndex + 1) % (int) scales().size(); updateNoteTarget(); break;
            case ccLeft:      navigate (-1, 0); break;
            case ccRight:     navigate (1, 0); break;
            case ccUp:        navigate (0, -1); break;
            case ccDown:      navigate (0, 1); break;
            case ccDevice:    encoderMode = EncoderMode::device; break;
            case ccMix:       if (encoderMode == EncoderMode::mix) mixParam = (MixParam) (((int) mixParam + 1) % 4);
                              encoderMode = EncoderMode::mix; break;
            case ccPageLeft:  deviceIndex = jmax (0, deviceIndex - 1); break;
            case ccPageRight: deviceIndex = jmin (7, deviceIndex + 1); break;
            case ccBrowse:    if (onToggleBrowser) onToggleBrowser(); break;
            case ccClip:
                if (auto mc = view->selectionManager.getFirstItemOfType<te::MidiClip>())
                    if (view->openPianoRoll) view->openPianoRoll (mc);
                break;
            case ccQuantize:
                if (auto mc = view->selectionManager.getFirstItemOfType<te::MidiClip>())
                {
                    um.beginNewTransaction ("Quantize");
                    for (auto n : mc->getSequence().getNotes())
                        n->setStartAndLength (te::BeatPosition::fromBeats (std::round (n->getStartBeat().inBeats() * 4.0) / 4.0), n->getLengthBeats(), &um);
                }
                break;
            case ccTapTempo: tapTempo(); break;
            case ccMute: if (auto t = selectedTrack()) t->setMute (! t->isMuted (false)); break;
            case ccSolo: if (auto t = selectedTrack()) t->setSolo (! t->isSolo (false)); break;
            default: break;
        }
    }

    void navigate (int dx, int dy)
    {
        const int step = shift ? 8 : 1;
        if (mode == Mode::session)
        {
            trackOffset = jlimit (0, jmax (0, tracks().size() - 1), trackOffset + dx * step);
            sceneOffset = jmax (0, sceneOffset + dy * step);
            edit().getSceneList().ensureNumberOfScenes (sceneOffset + 8);
            for (auto t : tracks()) t->getClipSlotList().ensureNumberOfSlots (sceneOffset + 8);
        }
        else if (dx != 0)
        {
            auto all = tracks();
            selectTrack (all[jlimit (0, all.size() - 1, all.indexOf (selectedTrack()) + dx)]);
        }
        else
        {
            octave = jlimit (-1, 8, octave - dy);
            updateNoteTarget();
        }
    }

    void tapTempo()
    {
        const auto now = Time::getMillisecondCounterHiRes();
        if (now - lastTap < 2000.0 && lastTap > 0.0)
            if (auto bpm = 60000.0 / (now - lastTap); bpm >= 40.0 && bpm <= 300.0)
                edit().tempoSequence.getTempo (0)->setBpm (std::round (bpm * 10.0) / 10.0);
        lastTap = now;
    }

    void handlePad (int pad)
    {
        if (pad < 0 || pad >= 64) return;
        const int row = pad / 8, col = pad % 8;
        auto t = tracks()[trackOffset + col];
        if (t == nullptr) return;

        const int scene = sceneOffset + (7 - row);
        t->getClipSlotList().ensureNumberOfSlots (scene + 1);
        auto slot = t->getClipSlotList().getClipSlots()[scene];
        if (slot == nullptr) return;

        selectTrack (t);

        if (auto clip = slot->getClip())
        {
            if (deleteHeld) { edit().getUndoManager().beginNewTransaction ("Delete clip"); clip->removeFromParent(); return; }
            view->selectionManager.selectOnly (clip);
            Launcher::launchClip (*clip);
        }
        else if (EngineHelpers::isTrackArmed (*t))
        {
            Launcher::recordIntoSlot (*slot, *t);
        }
        else
        {
            Launcher::stopTrack (*t);
        }
    }

    //==============================================================================
    // Encoders
    te::Plugin* currentDevice()
    {
        auto t = selectedTrack();
        if (t == nullptr) return nullptr;
        Array<te::Plugin*> devices;
        for (auto p : t->pluginList)
            if (p != t->getVolumePlugin() && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr
                && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr && dynamic_cast<te::AuxReturnPlugin*> (p) == nullptr)
                devices.add (p);
        return devices[jmin (deviceIndex, devices.size() - 1)];
    }

    static void nudge (te::AutomatableParameter& p, int delta, bool fine)
    {
        auto n = jlimit (0.0f, 1.0f, p.getCurrentNormalisedValue() + (float) delta * (fine ? 0.001f : 0.01f));
        p.setNormalisedParameter (n, sendNotificationSync);
    }

    void handleEncoder (int cc, int delta)
    {
        if (cc == ccTempoEnc)
        {
            auto* tempo = edit().tempoSequence.getTempo (0);
            tempo->setBpm (jlimit (20.0, 400.0, tempo->getBpm() + delta * (shift ? 0.1 : 1.0)));
            return;
        }
        if (cc == ccMasterEnc)
        {
            if (auto vp = edit().getMasterVolumePlugin()) nudge (*vp->volParam, delta, shift);
            return;
        }
        if (cc < ccTrackEnc || cc >= ccTrackEnc + 8) return;

        const int i = cc - ccTrackEnc;
        if (encoderMode == EncoderMode::device)
        {
            if (auto d = currentDevice())
                if (auto p = d->getAutomatableParameters()[i])
                    nudge (*p, delta, shift);
            return;
        }

        auto t = tracks()[trackOffset + i];
        if (t == nullptr) return;
        switch (mixParam)
        {
            case MixParam::volume: if (auto vp = t->getVolumePlugin()) nudge (*vp->volParam, delta, shift); break;
            case MixParam::pan:    if (auto vp = t->getVolumePlugin()) nudge (*vp->panParam, delta, shift); break;
            case MixParam::sendA:
            case MixParam::sendB:
            {
                auto returns = Routing::getReturnTracks (edit());
                if (auto r = returns[mixParam == MixParam::sendA ? 0 : 1])
                {
                    const int bus = Routing::getReturnBus (*r);
                    Routing::setSendDb (*t, bus, jlimit (Routing::sendOffDb, 6.0f, Routing::getSendDb (*t, bus) + (float) delta * (shift ? 0.1f : 0.75f)));
                }
                break;
            }
        }
    }

    String mixModeName() const
    {
        switch (mixParam) { case MixParam::volume: return "Volume"; case MixParam::pan: return "Pan";
                            case MixParam::sendA: return "Send A"; case MixParam::sendB: return "Send B"; }
        return {};
    }

    //==============================================================================
    // Note / drum layout
    struct Scale { const char* name; std::vector<int> steps; };
    static const std::vector<Scale>& scales()
    {
        static const std::vector<Scale> s {
            { "Major", { 0, 2, 4, 5, 7, 9, 11 } }, { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
            { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } }, { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
            { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } }, { "Minor Pent", { 0, 3, 5, 7, 10 } },
            { "Major Pent", { 0, 2, 4, 7, 9 } }, { "Blues", { 0, 3, 5, 6, 7, 10 } },
            { "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } } };
        return s;
    }
    String scaleName() const { return scales()[(size_t) scaleIndex].name; }
    String rootName() const  { return MidiMessage::getMidiNoteName (root, true, false, 3); }

    bool isDrumTarget()
    {
        auto t = selectedTrack();
        return t != nullptr && DrumRack::findOnTrack (*t) != nullptr;
    }

    void setMode (Mode m)
    {
        mode = m;
        updateNoteTarget();
        markAllDirty();
    }

    /** Recomputes the pad -> pitch table and the target track (shared with the MIDI thread). */
    void updateNoteTarget()
    {
        std::array<int, 64> table;
        table.fill (-1);
        te::AudioTrack::Ptr target;
        const bool notes = mode == Mode::note && view != nullptr;

        if (notes)
        {
            target = selectedTrack();
            if (isDrumTarget())
            {
                for (int pad = 0; pad < DrumRack::numPads; ++pad)
                    table[(size_t) ((pad / 4) * 8 + pad % 4)] = DrumRack::baseNote + pad;
            }
            else
            {
                auto& steps = scales()[(size_t) scaleIndex].steps;
                const int n = (int) steps.size();
                const int rowStep = n == 12 ? 5 : 3;   // rows go up a fourth
                for (int row = 0; row < 8; ++row)
                    for (int col = 0; col < 8; ++col)
                    {
                        const int idx = row * rowStep + col;
                        const int pitch = 12 * (octave + 1) + root + steps[(size_t) (idx % n)] + 12 * (idx / n);
                        table[(size_t) (row * 8 + col)] = pitch <= 127 ? pitch : -1;
                    }
            }
        }

        const SpinLock::ScopedLockType sl (noteLock);
        padPitch = table;
        noteTarget = target;
        noteMode = notes;
    }

    //==============================================================================
    // LEDs
    void markAllDirty()
    {
        sentPad.fill (-1);
        sentCC.fill (-1);
    }

    void setPad (int pad, int colour)
    {
        if (output != nullptr && sentPad[(size_t) pad] != colour)
        {
            output->sendMessageNow (MidiMessage::noteOn (1, 36 + pad, (uint8) colour));
            sentPad[(size_t) pad] = colour;
        }
    }

    void setButton (int cc, int value)
    {
        if (output != nullptr && sentCC[(size_t) cc] != value)
        {
            output->sendMessageNow (MidiMessage::controllerEvent (1, cc, value));
            sentCC[(size_t) cc] = value;
        }
    }

    void refreshLeds()
    {
        if (output == nullptr || view == nullptr) return;

        const bool blinkOn = (Time::getMillisecondCounter() / 200) % 2 == 0;
        auto all = tracks();
        auto sel = selectedTrack();

        if (mode == Mode::session)
        {
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; ++col)
                {
                    int colour = 0;
                    if (auto t = all[trackOffset + col])
                    {
                        auto slots = t->getClipSlotList().getClipSlots();
                        auto slot = slots[sceneOffset + (7 - row)];
                        if (slot != nullptr && slot->getClip() != nullptr)
                        {
                            auto c = slot->getClip();
                            if (Launcher::stateOf (*c) == te::LaunchHandle::PlayState::playing) colour = blinkOn ? palGreen : paletteFor (t->getColour());
                            else if (Launcher::isQueued (*c))                                    colour = blinkOn ? paletteFor (t->getColour()) : 0;
                            else                                                                  colour = paletteFor (t->getColour());
                        }
                        else if (slot != nullptr && Launcher::isRecordingInto (*slot)) colour = blinkOn ? palRed : palDimRed;
                        else if (EngineHelpers::isTrackArmed (*t))                     colour = palDimRed;
                    }
                    setPad (row * 8 + col, colour);
                }
        }
        else
        {
            const bool drums = isDrumTarget();
            auto* rack = drums ? DrumRack::rackOf (DrumRack::findOnTrack (*sel)) : nullptr;
            const int trackCol = sel != nullptr ? paletteFor (sel->getColour()) : palBlue;
            auto& steps = scales()[(size_t) scaleIndex].steps;

            for (int pad = 0; pad < 64; ++pad)
            {
                const int pitch = padPitch[(size_t) pad];
                int colour = 0;
                if (pitch >= 0)
                {
                    if (pressed[(size_t) pad])      colour = palGreen;
                    else if (drums)                  colour = rack != nullptr && DrumRack::padName (*rack, pitch - DrumRack::baseNote).isNotEmpty() ? trackCol : palDark;
                    else if (steps.size() == 12)     colour = std::find (std::begin (majorSteps), std::end (majorSteps), ((pitch - root) % 12 + 12) % 12) != std::end (majorSteps)
                                                              ? ((pitch - root) % 12 == 0 ? trackCol : palWhite) : palDark;
                    else                             colour = ((pitch - root) % 12 == 0) ? trackCol : palWhite;
                }
                setPad (pad, colour);
            }
        }

        // scene buttons, upper / lower rows
        for (int i = 0; i < 8; ++i)
        {
            bool sceneHasClips = false;
            for (auto t : all)
                if (auto s = t->getClipSlotList().getClipSlots()[sceneOffset + i]; s != nullptr && s->getClip() != nullptr)
                    sceneHasClips = true;
            setButton (ccSceneFirst - i, mode == Mode::session && sceneHasClips ? palWhite : 0);

            auto t = all[trackOffset + i];
            setButton (ccLowerRow + i, t == nullptr ? 0 : (t == sel ? palWhite : paletteFor (t->getColour())));

            int upper = 0;
            if (encoderMode == EncoderMode::device) upper = (i == deviceIndex) ? palBlue : 0;
            else if (t != nullptr)
            {
                bool playing = false;
                for (auto s : t->getClipSlotList().getClipSlots())
                    if (auto c = s->getClip(); c != nullptr && Launcher::stateOf (*c) == te::LaunchHandle::PlayState::playing)
                        playing = true;
                upper = playing ? palRed : palDark;
            }
            setButton (ccUpperRow + i, upper);
        }

        auto& tc = edit().getTransport();
        setButton (ccPlay, tc.isPlaying() ? palGreen : palDark);
        setButton (ccRecord, tc.isRecording() ? palRed : palDimRed);
        setButton (ccMetronome, edit().clickTrackEnabled.get() ? 127 : 16);
        setButton (ccSession, mode == Mode::session ? 127 : 16);
        setButton (ccNote, mode == Mode::note ? 127 : 16);
        setButton (ccDevice, encoderMode == EncoderMode::device ? 127 : 16);
        setButton (ccMix, encoderMode == EncoderMode::mix ? 127 : 16);
        for (int cc : { (int) ccLeft, (int) ccRight, (int) ccUp, (int) ccDown, (int) ccOctUp, (int) ccOctDown, (int) ccUndo, (int) ccDelete,
                        (int) ccDuplicate, (int) ccQuantize, (int) ccShift, (int) ccScale, (int) ccBrowse, (int) ccClip, (int) ccTapTempo,
                        (int) ccPageLeft, (int) ccPageRight })
            setButton (cc, 16);
        setButton (ccMute, sel != nullptr && sel->isMuted (false) ? 127 : 16);
        setButton (ccSolo, sel != nullptr && sel->isSolo (false) ? 127 : 16);
    }

    static constexpr int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };

    //==============================================================================
    void timerCallback() override
    {
        if (++tick % 50 == 0)   // every 2 s: hot-plug / unplug
        {
            if (output == nullptr)
                tryConnect();
            else
            {
                bool present = false;
                for (auto& d : MidiInput::getAvailableDevices()) if (d.identifier == portName) present = true;
                if (! present) disconnect();
            }
        }

        if (view != nullptr)
        {
            view->pushConnected = isConnected();
            view->pushTrackOffset = trackOffset;
            view->pushSceneOffset = sceneOffset;

            // follow selection changes made in the app
            if (mode == Mode::note && selectedTrack() != lastSelected)
            {
                lastSelected = selectedTrack();
                updateNoteTarget();
            }
        }

        refreshLeds();
    }

    //==============================================================================
    te::Engine& engine;
    EditViewState* view = nullptr;
    std::unique_ptr<MidiInput> input;
    std::unique_ptr<MidiOutput> output;
    String portName;
    const te::MPESourceID sourceID = te::createUniqueMPESourceID();

    Mode mode = Mode::session;
    EncoderMode encoderMode = EncoderMode::mix;
    MixParam mixParam = MixParam::volume;
    int trackOffset = 0, sceneOffset = 0, deviceIndex = 0;
    int octave = 2, root = 0, scaleIndex = 0;
    bool shift = false, deleteHeld = false, dupHeld = false, dupUsed = false;
    double lastTap = 0.0;
    int tick = 0;
    te::AudioTrack* lastSelected = nullptr;

    SpinLock noteLock;
    std::array<int, 64> padPitch {};
    std::array<bool, 64> pressed {};
    te::AudioTrack::Ptr noteTarget;
    bool noteMode = false;

    SpinLock queueLock;
    std::vector<MidiMessage> queue;

    std::array<int, 64> sentPad {};
    std::array<int, 128> sentCC {};
};
