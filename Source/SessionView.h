#pragma once

#include "EditViewState.h"
#include "ClipImport.h"
#include "PluginMenu.h"

//==============================================================================
/** Launching helpers (adapted from Tracktion's ClipLauncher demo). */
namespace Launcher
{
    inline te::LaunchQuantisation& quantisationFor (te::Clip& c)
    {
        if (! c.usesGlobalLaunchQuatisation())
            if (auto q = c.getLaunchQuantisation())
                return *q;

        return c.edit.getLaunchQuantisation();
    }

    inline te::MonotonicBeat launchPosition (te::Edit& e, const te::LaunchQuantisation& lq)
    {
        auto epc = e.getTransport().getCurrentPlaybackContext();
        if (! epc) return {};

        auto sync = epc->getSyncPoint();
        if (! sync) return {};

        auto pos = sync->beat;
        auto next = pos;
        for (;;)
        {
            next = lq.getNext (next);
            if (next >= te::BeatPosition()) break;
            next = next + te::BeatDuration::fromBeats (0.001);
        }

        return { sync->monotonicBeat.v + (next - pos) };
    }

    inline double clipLengthBeats (te::Clip& c)
    {
        auto& ts = c.edit.tempoSequence;
        auto pos = c.getPosition();
        return ts.toBeats (pos.getEnd()).inBeats() - ts.toBeats (pos.getStart()).inBeats();
    }

    inline void stopClip (te::Clip& c)
    {
        if (auto lh = c.getLaunchHandle())
        {
            auto& e = c.edit;
            lh->stop (e.getTransport().isPlaying() ? std::make_optional (launchPosition (e, quantisationFor (c))) : std::nullopt);
        }
    }

    /** Launches a clip (looping it over its length) and stops the others on its track. */
    inline void launchClip (te::Clip& c)
    {
        auto& tc = c.edit.getTransport();
        const bool wasPlaying = tc.isPlaying();

        if (! wasPlaying)
        {
            auto& ts = c.edit.tempoSequence;
            auto bb = ts.toBarsAndBeats (tc.getPosition());
            tc.setPosition (ts.toTime (te::tempo::BarsAndBeats { bb.bars }));
            if (auto epc = tc.getCurrentPlaybackContext())
                epc->blockUntilSyncPointChange();
        }

        if (auto at = dynamic_cast<te::AudioTrack*> (c.getTrack()))
            for (auto slot : at->getClipSlotList().getClipSlots())
                if (auto other = slot->getClip(); other != nullptr && other != &c)
                    stopClip (*other);

        if (auto lh = c.getLaunchHandle())
        {
            lh->setLooping (te::BeatDuration::fromBeats (jmax (0.25, clipLengthBeats (c))));
            lh->play (launchPosition (c.edit, quantisationFor (c)));
        }

        if (! wasPlaying)
            tc.play (false);
    }

    inline void stopTrack (te::AudioTrack& t)
    {
        for (auto slot : t.getClipSlotList().getClipSlots())
            if (auto c = slot->getClip())
                stopClip (*c);
    }

    inline void launchScene (te::Edit& e, int sceneIndex)
    {
        for (auto at : EngineHelpers::getUserTracks (e))
        {
            auto slots = at->getClipSlotList().getClipSlots();
            if (auto slot = slots[sceneIndex]; slot != nullptr && slot->getClip() != nullptr)
                launchClip (*slot->getClip());
            else
                stopTrack (*at);   // empty slot in a scene stops the track (Ableton behaviour)
        }
    }

    //==============================================================================
    // Session recording into clip slots
    inline te::InputDeviceInstance* inputFor (te::AudioTrack& t)
    {
        for (auto inst : t.edit.getAllInputDevices())
            if (te::isOnTargetTrack (*inst, t, 0))
                return inst;

        // nothing routed yet: MIDI for instrument tracks, the first audio input otherwise
        const bool wantMidi = EngineHelpers::trackHasInstrument (t);
        for (auto inst : t.edit.getAllInputDevices())
        {
            auto type = inst->getInputDevice().getDeviceType();
            const bool isMidi = type == te::InputDevice::physicalMidiDevice || type == te::InputDevice::virtualMidiDevice;
            if ((wantMidi && isMidi) || (! wantMidi && type == te::InputDevice::waveDevice))
                if (inst->setTarget (t.itemID, true, &t.edit.getUndoManager(), 0))
                    return inst;
        }
        return nullptr;
    }

    struct SlotRecording
    {
        te::InputDeviceInstance* input = nullptr;
        te::EditItemID slotID, trackID;
        bool trackWasArmed = false;
    };

    /** Active slot recordings, cleaned up when the transport stops recording. */
    inline std::vector<SlotRecording>& slotRecordings()
    {
        static std::vector<SlotRecording> list;
        return list;
    }

    inline bool recordIntoSlot (te::ClipSlot& slot, te::AudioTrack& t)
    {
        auto inst = inputFor (t);
        if (inst == nullptr)
            return false;

        if (! inst->setTarget (slot.itemID, false, &t.edit.getUndoManager(), std::nullopt))
            return false;

        const bool wasArmed = inst->isRecordingEnabled (t.itemID);
        inst->setRecordingEnabled (t.itemID, false);       // record only into the slot
        inst->setRecordingEnabled (slot.itemID, true);
        slotRecordings().push_back ({ inst, slot.itemID, t.itemID, wasArmed });

        auto& tc = t.edit.getTransport();
        if (! tc.isRecording())
            tc.record (false);
        return true;
    }

    inline bool isRecordingInto (te::ClipSlot& slot)
    {
        for (auto& r : slotRecordings())
            if (r.slotID == slot.itemID)
                return true;
        return false;
    }

    /** Call regularly: once recording has stopped, detach the slots and restore track arming. */
    inline void tidySlotRecordings (te::Edit& e)
    {
        auto& list = slotRecordings();
        if (list.empty() || e.getTransport().isRecording())
            return;

        for (auto& r : list)
        {
            [[maybe_unused]] auto res = r.input->removeTarget (r.slotID, &e.getUndoManager());
            if (r.trackWasArmed)
                r.input->setRecordingEnabled (r.trackID, true);
        }
        list.clear();
    }

    inline te::LaunchHandle::PlayState stateOf (te::Clip& c)
    {
        if (auto lh = c.getLaunchHandle())
            return lh->getPlayingStatus();
        return te::LaunchHandle::PlayState::stopped;
    }

    inline bool isQueued (te::Clip& c)
    {
        if (auto lh = c.getLaunchHandle())
            return lh->getQueuedStatus().has_value();
        return false;
    }
}

//==============================================================================
class SlotComponent : public Component,
                      public DragAndDropTarget,
                      public FileDragAndDropTarget
{
public:
    SlotComponent (EditViewState& e, te::AudioTrack& t, te::ClipSlot& s) : evs (e), track (&t), slot (&s) {}

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        auto clip = slot->getClip();

        if (clip == nullptr)
        {
            const bool recording = Launcher::isRecordingInto (*slot);
            const bool blink = (Time::getMillisecondCounter() / 250) % 2 == 0;
            g.setColour (recording ? Theme::red.withAlpha (blink ? 0.6f : 0.3f) : Colour (0xff1d2025));
            g.fillRoundedRectangle (r, 3.0f);

            if (EngineHelpers::isTrackArmed (*track) || recording)
            {
                // armed: empty slots show a record button
                g.setColour (Theme::red.withAlpha (recording ? 1.0f : 0.7f));
                g.fillEllipse (Rectangle<float> (10.0f, 10.0f).withCentre (r.getCentre()));
            }
            else
            {
                g.setColour (Colours::white.withAlpha (0.12f));
                g.fillRect (Rectangle<float> (8.0f, 8.0f).withCentre (r.getCentre()));
            }
        }
        else
        {
            auto col = track->getColour();
            const auto state = Launcher::stateOf (*clip);
            const bool playing = state == te::LaunchHandle::PlayState::playing;
            const bool queued = Launcher::isQueued (*clip);
            const bool selected = evs.selectionManager.isSelected (clip);

            g.setColour (col.withAlpha (playing ? 0.75f : 0.4f));
            g.fillRoundedRectangle (r, 3.0f);

            // progress through the looped clip
            if (playing)
                if (auto lh = clip->getLaunchHandle())
                    if (auto played = lh->getPlayedRange())
                    {
                        auto len = jmax (0.25, Launcher::clipLengthBeats (*clip));
                        auto prog = (float) std::fmod (played->getLength().inBeats(), len) / (float) len;
                        g.setColour (Colours::white.withAlpha (0.25f));
                        g.fillRect (r.withTop (r.getBottom() - 3.0f).withWidth (r.getWidth() * prog));
                    }

            // play button
            auto playArea = r.removeFromLeft (22.0f);
            Path tri;
            auto c = playArea.getCentre();
            tri.addTriangle (c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
            const bool blink = queued && (Time::getMillisecondCounter() / 200) % 2 == 0;
            g.setColour (playing ? Theme::green : blink ? Theme::orange : Colours::white.withAlpha (0.85f));
            g.fillPath (tri);

            g.setColour (Colours::white);
            g.setFont (FontOptions (12.0f));
            g.drawText (clip->getName(), r.reduced (2.0f, 0.0f), Justification::centredLeft, true);

            if (selected)
            {
                g.setColour (Colours::white);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 3.0f, 1.5f);
            }
        }

        if (dropHighlight)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f, 2.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        auto clip = slot->getClip();

        if (e.mods.isPopupMenu())
            return showMenu();

        if (clip == nullptr)
        {
            evs.selectionManager.selectOnly (track);
            if (EngineHelpers::isTrackArmed (*track))
                Launcher::recordIntoSlot (*slot, *track);   // armed track: record a new clip here
            else
                Launcher::stopTrack (*track);
            return;
        }

        evs.selectionManager.selectOnly (clip);

        if (e.x < 24)
            Launcher::launchClip (*clip);
    }

    void mouseDoubleClick (const MouseEvent&) override
    {
        if (auto clip = slot->getClip())
        {
            if (auto mc = dynamic_cast<te::MidiClip*> (clip))
                if (evs.openPianoRoll) evs.openPianoRoll (mc);
        }
        else
        {
            createMidiClip();
        }
    }

    // Drops: files from the browser / Explorer
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == "PulseFiles"; }
    void itemDragEnter (const SourceDetails&) override { setHighlight (true); }
    void itemDragExit (const SourceDetails&) override  { setHighlight (false); }
    void itemDropped (const SourceDetails& d) override
    {
        setHighlight (false);
        auto files = ClipImport::filesFromDrag (d);
        if (! files.isEmpty()) importFile (files.getFirst());
    }

    bool isInterestedInFileDrag (const StringArray& f) override { return ! f.isEmpty() && ClipImport::isImportable (File (f[0])); }
    void fileDragEnter (const StringArray&, int, int) override { setHighlight (true); }
    void fileDragExit (const StringArray&) override              { setHighlight (false); }
    void filesDropped (const StringArray& f, int, int) override  { setHighlight (false); importFile (File (f[0])); }

private:
    void setHighlight (bool b) { if (b != dropHighlight) { dropHighlight = b; repaint(); } }

    void importFile (const File& f)
    {
        evs.edit.getUndoManager().beginNewTransaction ("Import into slot");
        if (auto c = slot->getClip()) c->removeFromParent();

        if (auto clip = ClipImport::importFile (*slot, evs.edit, f, te::TimePosition()))
            if (auto ac = dynamic_cast<te::AudioClipBase*> (clip.get()))
                ac->setAutoTempo (true);   // loops follow the project tempo
    }

    void createMidiClip()
    {
        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        auto len = evs.edit.tempoSequence.toTime (te::BeatPosition::fromBeats (bpb * 4.0));
        if (auto mc = te::insertMIDIClip (*slot, "Clip", { te::TimePosition(), len }))
        {
            evs.selectionManager.selectOnly (mc.get());
            if (evs.openPianoRoll) evs.openPianoRoll (mc.get());
        }
    }

    void showMenu()
    {
        PopupMenu m;
        if (auto clip = slot->getClip())
        {
            te::Clip::Ptr c (clip);
            m.addItem ("Launch", [c] { Launcher::launchClip (*c); });
            m.addItem ("Stop", [c] { Launcher::stopClip (*c); });
            if (auto mc = dynamic_cast<te::MidiClip*> (clip))
                m.addItem ("Open in Piano Roll", [this, mc] { if (evs.openPianoRoll) evs.openPianoRoll (mc); });
            m.addItem ("Copy to arrangement (at playhead)", [this, c]
            {
                if (auto copy = te::duplicateClip (*c))
                {
                    copy->moveTo (*track);
                    copy->setStart (evs.edit.getTransport().getPosition(), false, true);
                }
            });
            m.addSeparator();
            m.addItem ("Delete", [c] { c->removeFromParent(); });
        }
        else
        {
            m.addItem ("Record into this slot", [this]
            {
                if (! Launcher::recordIntoSlot (*slot, *track))
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Record",
                                                      "No input available for this track. Enable an audio input or connect a MIDI device.");
            });
            m.addItem ("Insert MIDI clip", [this] { createMidiClip(); });
            m.addItem ("Import file...", [this]
            {
                chooser = std::make_shared<FileChooser> ("Import into slot", File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                      [this, sp = SafePointer<SlotComponent> (this)] (const FileChooser& fc)
                                      { if (sp != nullptr && fc.getResult().existsAsFile()) importFile (fc.getResult()); });
            });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

    EditViewState& evs;
    te::AudioTrack* track;
    te::ClipSlot* slot;
    std::shared_ptr<FileChooser> chooser;
    bool dropHighlight = false;
};

//==============================================================================
/** Ableton-style session grid: tracks are columns, scenes are rows. */
class SessionView : public Component,
                    private te::ValueTreeAllEventListener,
                    private FlaggedAsyncUpdater,
                    private Timer
{
public:
    explicit SessionView (EditViewState& e) : evs (e), grid (e)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&grid, false);

        quantBox.setTooltip ("Launch quantisation");
        const char* qNames[] = { "None", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" };
        const te::LaunchQType qTypes[] = { te::LaunchQType::none, te::LaunchQType::eightBars, te::LaunchQType::fourBars,
                                           te::LaunchQType::twoBars, te::LaunchQType::bar, te::LaunchQType::half,
                                           te::LaunchQType::quarter, te::LaunchQType::eighth, te::LaunchQType::sixteenth };
        for (int i = 0; i < 9; ++i)
        {
            quantBox.addItem (qNames[i], i + 1);
            qTypeById[i + 1] = qTypes[i];
            if (evs.edit.getLaunchQuantisation().type.get() == qTypes[i])
                quantBox.setSelectedId (i + 1, dontSendNotification);
        }
        quantBox.onChange = [this] { evs.edit.getLaunchQuantisation().type = qTypeById[quantBox.getSelectedId()]; };
        addAndMakeVisible (quantBox);

        stopAllBtn.onClick = [this] { for (auto t : EngineHelpers::getUserTracks (evs.edit)) Launcher::stopTrack (*t); };
        addSceneBtn.onClick = [this]
        {
            auto& sl = evs.edit.getSceneList();
            sl.ensureNumberOfScenes (sl.getScenes().size() + 1);
        };
        backToArrBtn.setTooltip ("Return every track to the arrangement");
        backToArrBtn.onClick = [this] { for (auto t : EngineHelpers::getUserTracks (evs.edit)) t->playSlotClips = false; };
        for (auto* b : { &stopAllBtn, &addSceneBtn, &backToArrBtn }) addAndMakeVisible (b);

        label.setText ("Session", dontSendNotification);
        label.setFont (FontOptions (13.0f, Font::bold));
        addAndMakeVisible (label);

        evs.edit.state.addListener (this);
        markAndUpdate (rebuildFlag);
        startTimerHz (12);
    }

    ~SessionView() override { evs.edit.state.removeListener (this); }

    void paint (Graphics& g) override { g.fillAll (Colour (0xff15171b)); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (34).reduced (8, 4);
        label.setBounds (bar.removeFromLeft (80));
        quantBox.setBounds (bar.removeFromLeft (90)); bar.removeFromLeft (6);
        addSceneBtn.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (4);
        stopAllBtn.setBounds (bar.removeFromLeft (100)); bar.removeFromLeft (4);
        backToArrBtn.setBounds (bar.removeFromLeft (150));
        viewport.setBounds (r);
        layoutGrid();
    }

private:
    static constexpr int colW = 130, rowH = 40, headH = 46, sceneW = 120;

    struct Grid : public Component
    {
        explicit Grid (EditViewState& e) : evs (e) {}
        void paint (Graphics& g) override { g.fillAll (Colour (0xff15171b)); }

        // the 8x8 area currently shown on Push 2
        void paintOverChildren (Graphics& g) override
        {
            if (! evs.pushConnected) return;
            auto r = Rectangle<int> (8 + evs.pushTrackOffset * colW - 3, headH + evs.pushSceneOffset * rowH - 3,
                                     8 * colW + 2, 8 * rowH + 6);
            g.setColour (Theme::red);
            g.drawRect (r, 2);
        }

        EditViewState& evs;
    };

    struct TrackHead : public Component
    {
        TrackHead (EditViewState& e, te::AudioTrack& t) : evs (e), track (&t)
        {
            arrBtn.setTooltip ("Track is playing session clips - click to return it to the arrangement");
            arrBtn.setColour (TextButton::buttonColourId, Theme::orange);
            arrBtn.onClick = [this] { track->playSlotClips = false; };
            addChildComponent (arrBtn);
        }

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (2.0f);
            const bool sel = evs.selectionManager.isSelected (track);
            g.setColour (Theme::panel2);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (track->getColour());
            g.fillRect (r.removeFromTop (3.0f));
            g.setColour (sel ? Theme::accent : Theme::text);
            g.setFont (FontOptions (12.5f, Font::bold));
            g.drawText (track->getName(), r.reduced (6.0f, 0.0f).removeFromTop (22.0f), Justification::centredLeft, true);
        }

        void resized() override { arrBtn.setBounds (getLocalBounds().reduced (6, 4).removeFromBottom (16).removeFromRight (40)); }
        void mouseDown (const MouseEvent&) override { evs.selectionManager.selectOnly (track); }
        void update() { arrBtn.setVisible (track->playSlotClips.get()); repaint(); }

        EditViewState& evs;
        te::AudioTrack* track;
        TextButton arrBtn { "Arr" };
    };

    void rebuild()
    {
        slots.clear();
        heads.clear();
        sceneBtns.clear();
        stopBtns.clear();

        auto tracks = EngineHelpers::getUserTracks (evs.edit);
        auto& sceneList = evs.edit.getSceneList();
        sceneList.ensureNumberOfScenes (jmax (8, sceneList.getScenes().size()));
        numScenes = sceneList.getScenes().size();

        for (auto t : tracks)
        {
            t->getClipSlotList().ensureNumberOfSlots (numScenes);
            grid.addAndMakeVisible (heads.add (new TrackHead (evs, *t)));

            auto trackSlots = t->getClipSlotList().getClipSlots();
            for (int s = 0; s < numScenes; ++s)
                grid.addAndMakeVisible (slots.add (new SlotComponent (evs, *t, *trackSlots[s])));

            auto* stop = stopBtns.add (new TextButton (String::charToString (0x25A0)));
            stop->setTooltip ("Stop clips on this track");
            stop->onClick = [t] { Launcher::stopTrack (*t); };
            grid.addAndMakeVisible (stop);
        }

        for (int s = 0; s < numScenes; ++s)
        {
            auto* b = sceneBtns.add (new TextButton (String::charToString (0x25B6) + "  Scene " + String (s + 1)));
            b->setTooltip ("Launch every clip in this row");
            b->onClick = [this, s] { Launcher::launchScene (evs.edit, s); };
            grid.addAndMakeVisible (b);
        }

        numTracks = tracks.size();
        layoutGrid();
    }

    void layoutGrid()
    {
        const int w = numTracks * colW + sceneW + 24;
        const int h = headH + numScenes * rowH + 40;
        grid.setSize (jmax (w, viewport.getMaximumVisibleWidth()), jmax (h, viewport.getMaximumVisibleHeight()));

        for (int t = 0; t < numTracks; ++t)
        {
            const int x = 8 + t * colW;
            heads[t]->setBounds (x, 4, colW - 4, headH - 6);
            for (int s = 0; s < numScenes; ++s)
                slots[t * numScenes + s]->setBounds (x, headH + s * rowH, colW - 4, rowH);
            stopBtns[t]->setBounds (x + 2, headH + numScenes * rowH + 6, colW - 8, 24);
        }

        const int sx = 8 + numTracks * colW + 12;
        for (int s = 0; s < numScenes; ++s)
            sceneBtns[s]->setBounds (sx, headH + s * rowH + 4, sceneW - 8, rowH - 8);
    }

    void timerCallback() override
    {
        Launcher::tidySlotRecordings (evs.edit);
        for (auto* s : slots) s->repaint();
        grid.repaint();
        for (auto* h : heads) h->update();
    }

    void valueTreeChanged() override {}

    static bool affectsGrid (const ValueTree& c)
    {
        return te::TrackList::isTrack (c) || c.hasType (te::IDs::SCENE) || c.hasType (te::IDs::CLIPSLOT);
    }

    void valueTreeChildAdded (ValueTree&, ValueTree& c) override         { if (affectsGrid (c)) markAndUpdate (rebuildFlag); }
    void valueTreeChildRemoved (ValueTree&, ValueTree& c, int) override  { if (affectsGrid (c)) markAndUpdate (rebuildFlag); }
    void valueTreeChildOrderChanged (ValueTree& p, int a, int b) override
    {
        if (te::TrackList::isTrack (p.getChild (a)) || te::TrackList::isTrack (p.getChild (b)))
            markAndUpdate (rebuildFlag);
    }

    void handleAsyncUpdate() override
    {
        if (compareAndReset (rebuildFlag))
            rebuild();
    }

    EditViewState& evs;
    Viewport viewport;
    Grid grid;
    OwnedArray<TrackHead> heads;
    OwnedArray<SlotComponent> slots;
    OwnedArray<TextButton> sceneBtns, stopBtns;
    ComboBox quantBox;
    std::map<int, te::LaunchQType> qTypeById;
    TextButton stopAllBtn { "Stop all clips" }, addSceneBtn { "+ Scene" }, backToArrBtn { "Back to arrangement" };
    Label label;
    int numTracks = 0, numScenes = 0;
    bool rebuildFlag = false;
};
