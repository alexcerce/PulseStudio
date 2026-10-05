#pragma once

#include "EditViewState.h"
#include "SessionView.h"
#include "EditingOps.h"
#include "DrumRack.h"
#include "PushMap.h"
#include "Routing.h"
#include "Push2Display.h"

/** Ableton Push 2 support over its "Live" MIDI port (protocol from Ableton's public
    Push 2 MIDI & Display Interface Manual). The display is driven over USB (Push2Display.h).

    Modes
      Session : 8x8 clip grid, scene buttons launch rows, upper row stops tracks, lower row selects tracks
      Note    : in-key 4ths layout for the selected track (scale/root via Scale button)
      Drum    : 4x4 pads for a Drum Rack track (automatic when the selected track has one)
      Step    : Layout button in drum mode: top 4 rows = 32 sixteenth steps of the selected drum,
                bottom-left 4x4 = drums (play + select), bottom-right 4x4 = 2-bar pages
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
            case Mode::note:    s << (isDrumTarget() ? (stepMode ? "Step sequencer" : "Drums") : "Notes  " + rootName() + " " + scaleName()
                                                                 + "  oct " + String (octave)); break;
        }
        if (display != nullptr) s << "  |  " << display->getStatus();
        s << "  |  encoders: " << (encoderMode == EncoderMode::device ? "Device" : mixModeName());
        return s;
    }

    std::function<void()> onToggleBrowser;   // Browse button

private:
    //==============================================================================
    enum class Mode { session, note };
    enum class EncoderMode { device, mix };
    // Mix mode page: 0 = volume, 1 = pan, 2+ = send to return N (any number of returns)
    int numMixPages() { return 2 + (view != nullptr ? Routing::getReturnTracks (edit()).size() : 0); }
    te::AudioTrack* mixReturn() { return mixPage >= 2 ? Routing::getReturnTracks (edit())[mixPage - 2] : nullptr; }

    // Push 2 control numbers
    enum CC
    {
        ccTapTempo = 3, ccMetronome = 9, ccTempoEnc = 14, ccLowerRow = 20, ccStopClip = 29, ccSceneLast = 36, ccSceneFirst = 43,
        ccLeft = 44, ccRight = 45, ccUp = 46, ccDown = 47, ccSelect = 48, ccShift = 49, ccNote = 50, ccSession = 51,
        ccOctDown = 54, ccOctUp = 55, ccScale = 58, ccMute = 60, ccSolo = 61, ccPageLeft = 62, ccPageRight = 63,
        ccTrackEnc = 71, ccMasterEnc = 79, ccPlay = 85, ccRecord = 86, ccDuplicate = 88, ccUpperRow = 102,
        ccDevice = 110, ccBrowse = 111, ccMix = 112, ccClip = 113, ccQuantize = 116, ccDelete = 118, ccUndo = 119,
        ccLayout = 31
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
        display = std::make_unique<Push2Display>();
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
        display = nullptr;
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
            else if (m.isNoteOn() && stepMode)
            {
                handleStepPad (m.getNoteNumber() - 36);
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
            if (encoderMode == EncoderMode::device) { deviceIndex = i; paramBank = 0; return; }   // pick a device
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
            case ccLayout:    if (mode == Mode::note && isDrumTarget()) { stepMode = ! stepMode; updateNoteTarget(); markAllDirty(); } break;
            case ccOctUp:
            case ccOctDown:
            {
                const int dir = cc == ccOctUp ? 1 : -1;
                auto t = selectedTrack();
                if (auto* rack = (t != nullptr && isDrumTarget()) ? DrumRack::rackOf (DrumRack::findOnTrack (*t)) : nullptr)
                    DrumRack::setBank (*rack, DrumRack::getBank (*rack) + dir * 16);   // drums: next / previous 16 pads
                else
                    octave = jlimit (-1, 8, octave + dir);
                updateNoteTarget();
                break;
            }
            case ccScale:     if (shift) root = (root + 1) % 12; else scaleIndex = (scaleIndex + 1) % (int) scales().size(); updateNoteTarget(); break;
            case ccLeft:      navigate (-1, 0); break;
            case ccRight:     navigate (1, 0); break;
            case ccUp:        navigate (0, -1); break;
            case ccDown:      navigate (0, 1); break;
            case ccDevice:    encoderMode = EncoderMode::device; break;
            case ccMix:       if (encoderMode == EncoderMode::mix) mixPage = (mixPage + (shift ? numMixPages() - 1 : 1)) % numMixPages();
                              encoderMode = EncoderMode::mix; break;
            case ccPageLeft:
            case ccPageRight:
            {
                const int dir = cc == ccPageRight ? 1 : -1;
                if (shift && encoderMode == EncoderMode::device)   // Shift + Page: the next 8 parameters
                {
                    const int count = currentDevice() != nullptr ? currentDevice()->getAutomatableParameters().size() : 0;
                    paramBank = jlimit (0, jmax (0, (count - 1) / 8), paramBank + dir);
                }
                else
                {
                    deviceIndex = jlimit (0, 7, deviceIndex + dir);
                    paramBank = 0;
                }
                break;
            }
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
            case ccMute: if (auto t = selectedTrack()) EngineHelpers::toggleMute (*t); break;
            case ccSolo: if (auto t = selectedTrack()) EngineHelpers::toggleSolo (*t); break;
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
            if (auto t = selectedTrack(); t != nullptr && paramBank == 0)
                if (auto mapped = PushMap::find (*t, i)) { nudge (*mapped, delta, shift); return; }   // your assignments come first
            if (auto d = currentDevice())
                if (auto p = d->getAutomatableParameters()[paramBank * 8 + i])
                    nudge (*p, delta, shift);
            return;
        }

        auto t = tracks()[trackOffset + i];
        if (t == nullptr) return;
        if (mixPage == 0)      { if (auto vp = t->getVolumePlugin()) nudge (*vp->volParam, delta, shift); }
        else if (mixPage == 1) { if (auto vp = t->getVolumePlugin()) nudge (*vp->panParam, delta, shift); }
        else if (auto r = mixReturn())
        {
            const int bus = Routing::getReturnBus (*r);
            Routing::setSendDb (*t, bus, jlimit (Routing::sendOffDb, 6.0f, Routing::getSendDb (*t, bus) + (float) delta * (shift ? 0.1f : 0.75f)));
        }
    }

    String mixModeName()
    {
        if (mixPage == 0) return "Volume";
        if (mixPage == 1) return "Pan";
        if (auto r = mixReturn()) return "Send " + r->getName();
        mixPage = 0;
        return "Volume";
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

    /** The note played by pad 0-15 of the Drum Rack's current bank (follows remapped pads). */
    int drumNote (int padIndex)
    {
        auto t = selectedTrack();
        auto* rack = t != nullptr ? DrumRack::rackOf (DrumRack::findOnTrack (*t)) : nullptr;
        if (rack == nullptr) return DrumRack::baseNote + padIndex;
        return DrumRack::noteOfPad (*rack, DrumRack::getBank (*rack) + padIndex);
    }

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
                for (int pad = 0; pad < DrumRack::padsPerBank; ++pad)
                    table[(size_t) ((pad / 4) * 8 + pad % 4)] = drumNote (pad);
            }
            else
            {
                stepMode = false;
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

        if (! notes) stepMode = false;
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

            if (stepMode)
                refreshStepLeds (trackCol, blinkOn);

            for (int pad = 0; pad < 64; ++pad)
            {
                if (stepMode && ! (pad / 8 < 4 && pad % 8 < 4))
                    continue;   // steps and pages are drawn by refreshStepLeds
                const int pitch = padPitch[(size_t) pad];
                int colour = 0;
                if (pitch >= 0)
                {
                    if (pressed[(size_t) pad])      colour = palGreen;
                    else if (stepMode && pitch == drumNote (selectedDrum)) colour = palWhite;
                    else if (drums)                  colour = rack != nullptr && DrumRack::padForNote (*rack, pitch) >= 0 ? trackCol : palDark;
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
        setButton (ccLayout, stepMode ? 127 : (mode == Mode::note && isDrumTarget() ? 16 : 0));
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

        if (display != nullptr && view != nullptr && tick % 2 == 0)   // ~12 fps
        {
            Image img (Image::RGB, Push2Display::width, Push2Display::height, true);
            Graphics g (img);
            paintDisplay (g);
            display->setFrame (img);
        }
    }

    //==============================================================================
    // Step sequencer (drum tracks)
    static constexpr int stepsPerPage = 32;
    static constexpr double stepBeats = 0.25;

    /** The clip being step-edited: the selected MIDI clip on the track, else the one under the
        playhead, else a new 2-bar clip at the current bar. */
    te::MidiClip* stepClip (bool create)
    {
        auto t = selectedTrack();
        if (t == nullptr) return nullptr;

        if (auto mc = view->selectionManager.getFirstItemOfType<te::MidiClip>())
            if (mc->getTrack() == t) return mc;

        const auto now = edit().getTransport().getPosition();
        for (auto c : t->getClips())
            if (auto mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && c->getEditTimeRange().containsInclusive (now))
                return mc;

        for (auto c : t->getClips())   // otherwise the first one on the track
            if (auto mc = dynamic_cast<te::MidiClip*> (c)) return mc;

        if (! create) return nullptr;
        const int bpb = EngineHelpers::beatsPerBar (edit());
        const double bar = std::floor (EngineHelpers::timeToBeat (edit(), now) / bpb) * bpb;
        auto mc = t->insertMIDIClip ("Steps", { EngineHelpers::beatToTime (edit(), bar), EngineHelpers::beatToTime (edit(), bar + 2.0 * bpb) }, nullptr);
        if (mc != nullptr) view->selectionManager.selectOnly (mc.get());
        return mc.get();
    }

    int clipSteps (te::MidiClip& mc) const
    {
        const double len = mc.isLooping() ? mc.getLoopLengthBeats().inBeats() : mc.getLengthInBeats().inBeats();
        return jmax (1, (int) std::round (len / stepBeats));
    }

    te::MidiNote* noteAtStep (te::MidiClip& mc, int step)
    {
        const double beat = (mc.isLooping() ? mc.getLoopStartBeats().inBeats() : mc.getOffsetInBeats().inBeats()) + step * stepBeats;
        const int pitch = drumNote (selectedDrum);
        for (auto n : mc.getSequence().getNotes())
            if (n->getNoteNumber() == pitch && std::abs (n->getStartBeat().inBeats() - beat) < stepBeats * 0.5)
                return n;
        return nullptr;
    }

    void handleStepPad (int pad)
    {
        if (pad < 0 || pad >= 64) return;
        const int row = pad / 8, col = pad % 8;

        if (row < 4 && col < 4)   // drum pads: select (they also play, via the low-latency path)
        {
            selectedDrum = row * 4 + col;
            return;
        }

        auto& um = edit().getUndoManager();
        if (row < 4)              // pages (2 bars each); pressing past the end lengthens the clip
        {
            const int page = (3 - row) * 4 + (col - 4);
            if (auto mc = stepClip (true))
            {
                const int needed = (page + 1) * stepsPerPage;
                if (needed > clipSteps (*mc) && ! mc->isLooping())
                {
                    um.beginNewTransaction ("Lengthen clip");
                    mc->setEnd (EngineHelpers::beatToTime (edit(), mc->getStartBeat().inBeats() + needed * stepBeats), true);
                }
                stepPage = page;
            }
            return;
        }

        // steps: top row first
        const int step = stepPage * stepsPerPage + (7 - row) * 8 + col;
        auto mc = stepClip (true);
        if (mc == nullptr || step >= clipSteps (*mc)) return;

        um.beginNewTransaction ("Step edit");
        if (auto n = noteAtStep (*mc, step))
            mc->getSequence().removeNote (*n, &um);
        else
        {
            const double beat = (mc->isLooping() ? mc->getLoopStartBeats().inBeats() : mc->getOffsetInBeats().inBeats()) + step * stepBeats;
            mc->getSequence().addNote (drumNote (selectedDrum), te::BeatPosition::fromBeats (beat),
                                       te::BeatDuration::fromBeats (stepBeats), 100, 0, &um);
        }
    }

    /** Index of the step under the playhead (or -1). */
    int playingStep (te::MidiClip& mc)
    {
        auto& tc = edit().getTransport();
        if (! tc.isPlaying()) return -1;
        const auto now = tc.getPosition();
        if (! mc.getEditTimeRange().contains (now)) return -1;
        const double rel = EngineHelpers::timeToBeat (edit(), now) - mc.getStartBeat().inBeats();
        return (int) std::floor (std::fmod (rel, clipSteps (mc) * stepBeats) / stepBeats);
    }

    void refreshStepLeds (int trackCol, bool blinkOn)
    {
        ignoreUnused (blinkOn);
        auto mc = stepClip (false);
        const int total = mc != nullptr ? clipSteps (*mc) : 2 * stepsPerPage;
        const int pages = (total + stepsPerPage - 1) / stepsPerPage;
        stepPage = jlimit (0, jmax (0, pages - 1), stepPage);
        const int now = mc != nullptr ? playingStep (*mc) : -1;

        for (int i = 0; i < stepsPerPage; ++i)
        {
            const int step = stepPage * stepsPerPage + i;
            const int pad = (7 - i / 8) * 8 + i % 8;
            int colour = 0;
            if (step < total)
            {
                const bool on = mc != nullptr && noteAtStep (*mc, step) != nullptr;
                colour = step == now ? palGreen : on ? trackCol : ((i / 4) % 2 == 0 ? palGrey : palDark);
            }
            setPad (pad, colour);
        }

        for (int k = 0; k < 16; ++k)
        {
            const int pad = (3 - k / 4) * 8 + 4 + k % 4;
            const bool playingHere = now >= 0 && now / stepsPerPage == k;
            setPad (pad, k == stepPage ? palWhite : playingHere ? palGreen : k < pages ? palGrey : 0);
        }
    }

    //==============================================================================
    // Display
    void paintDisplay (Graphics& g)
    {
        const int w = Push2Display::width, h = Push2Display::height, colW = w / 8;
        g.fillAll (Colour (0xff0d0f12));
        auto all = tracks();
        auto sel = selectedTrack();

        auto label = [&g] (Rectangle<int> r, const String& text, Colour c, bool bold = false, float size = 13.0f)
        {
            g.setColour (c);
            g.setFont (FontOptions (size, bold ? Font::bold : Font::plain));
            g.drawFittedText (text, r.reduced (4, 0), Justification::centred, 1, 0.8f);
        };

        // bottom row: track names (lower buttons select)
        for (int i = 0; i < 8; ++i)
            if (auto t = all[trackOffset + i])
            {
                auto r = Rectangle<int> (i * colW, h - 22, colW, 22);
                g.setColour (t->getColour().withAlpha (t == sel ? 1.0f : 0.5f));
                g.fillRect (r.removeFromTop (3).reduced (4, 0));
                label (r, t->getName(), t == sel ? Colours::white : Colour (0xffb0b6c0), t == sel);
            }

        // top row: what the upper buttons do
        Array<te::Plugin*> devices;
        if (sel != nullptr)
            for (auto p : sel->pluginList)
                if (p != sel->getVolumePlugin() && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr
                    && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr && dynamic_cast<te::AuxReturnPlugin*> (p) == nullptr)
                    devices.add (p);

        for (int i = 0; i < 8; ++i)
        {
            auto r = Rectangle<int> (i * colW, 0, colW, 22);
            if (encoderMode == EncoderMode::device)
            {
                if (auto d = devices[i])
                {
                    if (i == jmin (deviceIndex, devices.size() - 1)) { g.setColour (Colour (0xff2b5b8a)); g.fillRect (r.reduced (3, 2)); }
                    label (r, d->getName(), Colours::white);
                }
            }
            else if (mode == Mode::session && all[trackOffset + i] != nullptr)
            {
                label (r, "Stop", Colour (0xff80868f), false, 11.0f);
            }
        }

        // middle: the 8 encoders (device parameters or the mix)
        auto knobArea = Rectangle<int> (0, 30, w, 92);
        for (int i = 0; i < 8; ++i)
        {
            auto r = knobArea.withX (i * colW).withWidth (colW);
            te::AutomatableParameter* param = nullptr;
            String name, value;
            float norm = 0.0f;
            bool bipolar = false;

            if (encoderMode == EncoderMode::device)
            {
                if (auto t = selectedTrack(); t != nullptr && paramBank == 0)
                    param = PushMap::find (*t, i);
                if (param == nullptr)
                    if (auto d = currentDevice())
                        if (auto p = d->getAutomatableParameters()[paramBank * 8 + i])
                            param = p;
            }
            else if (auto t = all[trackOffset + i])
            {
                if (mixPage == 0)      { if (auto vp = t->getVolumePlugin()) param = vp->volParam.get(); }
                else if (mixPage == 1) { if (auto vp = t->getVolumePlugin()) param = vp->panParam.get(); bipolar = true; }
                else
                {
                    auto returns = Routing::getReturnTracks (edit());
                    if (auto ret = mixReturn())
                    {
                        const float db = Routing::getSendDb (*t, Routing::getReturnBus (*ret));
                        name = "Send " + ret->getName();
                        value = db <= Routing::sendOffDb + 0.01f ? String ("-inf") : String (db, 1) + " dB";
                        norm = jlimit (0.0f, 1.0f, (db - Routing::sendOffDb) / (6.0f - Routing::sendOffDb));
                    }
                }
            }

            if (param != nullptr)
            {
                name = param->getParameterName();
                value = param->getCurrentValueAsStringWithLabel();
                norm = param->getCurrentNormalisedValue();
            }
            if (name.isEmpty()) continue;

            label (r.removeFromTop (16), name, Colour (0xffb0b6c0), false, 12.0f);
            label (r.removeFromBottom (18), value, Colours::white, true, 13.0f);
            auto knob = r.toFloat().withSizeKeepingCentre (52.0f, 52.0f);
            const float a0 = MathConstants<float>::pi * 1.2f, a1 = MathConstants<float>::pi * 2.8f;
            Path track, fill;
            track.addCentredArc (knob.getCentreX(), knob.getCentreY(), 22.0f, 22.0f, 0.0f, a0, a1, true);
            g.setColour (Colour (0xff2a2e35));
            g.strokePath (track, PathStrokeType (5.0f));
            const float from = bipolar ? (a0 + a1) * 0.5f : a0, to = a0 + (a1 - a0) * norm;
            fill.addCentredArc (knob.getCentreX(), knob.getCentreY(), 22.0f, 22.0f, 0.0f, jmin (from, to), jmax (from, to), true);
            g.setColour (Colour (0xff4aa3ff));
            g.strokePath (fill, PathStrokeType (5.0f));
        }

        // mode banner over the knob row
        String banner;
        if (mode == Mode::session)
            banner = "SESSION   " + String (edit().tempoSequence.getTempo (0)->getBpm(), 1) + " BPM   "
                   + EngineHelpers::formatBarsBeats (edit(), edit().getTransport().getPosition());
        else if (isDrumTarget())
            banner = stepMode ? "STEP SEQ   pad " + String (selectedDrum + 1) + "   page " + String (stepPage + 1) : String ("DRUMS");
        else
            banner = "NOTES   " + rootName() + " " + scaleName() + "   octave " + String (octave);
        banner << "   |   " << (encoderMode == EncoderMode::device ? String ("DEVICE") : "MIX: " + mixModeName().toUpperCase());

        g.setColour (Colour (0xff1a1e24));
        g.fillRect (0, 124, w, 14);
        label ({ 0, 124, w, 14 }, banner, Colour (0xffe0a24a), true, 11.0f);
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
    int mixPage = 0;
    int trackOffset = 0, sceneOffset = 0, deviceIndex = 0, paramBank = 0;
    int octave = 2, root = 0, scaleIndex = 0;
    bool shift = false, deleteHeld = false, dupHeld = false, dupUsed = false;
    bool stepMode = false;
    int selectedDrum = 0, stepPage = 0;
    std::unique_ptr<Push2Display> display;
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
