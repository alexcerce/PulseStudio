#pragma once

#include "Common.h"
#include "EditViewState.h"
#include "PluginMenu.h"
#include "ArrangementView.h"
#include "PianoRoll.h"
#include "DeviceChain.h"
#include "Mixer.h"
#include "TransportBar.h"
#include "Routing.h"
#include "Browser.h"
#include "SessionView.h"
#include "EditingOps.h"
#include "ExportDialog.h"
#include "Workflow.h"
#include "MidiLearn.h"
#include "DrumRack.h"
#include "AudioSettings.h"
#include "Push2.h"

//==============================================================================
/** Owns the current Edit and lays out transport, browser, arrangement/session and the
    bottom panel (Devices / Mixer / Piano Roll). */
class MainComponent : public Component,
                      public DragAndDropContainer,
                      private ChangeListener,
                      private Timer
{
public:
    explicit MainComponent (te::Engine& e)
        : engine (e),
          transport (e, makeActions())
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (resizer);
        resizer.onDrag = [this] (int dy) { bottomHeight = jlimit (120, getHeight() - 220, bottomHeight - dy); resized(); };

        for (auto* b : { &devicesTab, &mixerTab, &pianoTab })
        {
            b->setRadioGroupId (1);
            b->setClickingTogglesState (true);
            b->setColour (TextButton::buttonOnColourId, Colour (0xff2b3a4d));
            b->setColour (TextButton::textColourOnId, Theme::text);
            addAndMakeVisible (b);
        }
        devicesTab.onClick = [this] { showTab (Tab::devices); };
        mixerTab.onClick   = [this] { showTab (Tab::mixer); };
        pianoTab.onClick   = [this] { showTab (Tab::pianoRoll); };

        learnBanner.setJustificationType (Justification::centred);
        learnBanner.setColour (Label::backgroundColourId, Theme::orange);
        learnBanner.setColour (Label::textColourId, Colours::black);
        learnBanner.setFont (FontOptions (14.0f, Font::bold));
        addChildComponent (learnBanner);

        PluginMenu::drumRackFactory = [] (te::Edit& ed) { return DrumRack::create (ed); };

        push = std::make_unique<Push2Controller> (engine);
        push->onToggleBrowser = [this] { toggleBrowser(); };

        setWantsKeyboardFocus (true);
        enableInputs();
        openStartupProject();
        setSize (1440, 900);
        startTimer (60 * 1000);   // autosave
    }

    ~MainComponent() override
    {
        saveIfNeeded();
        closeEdit();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg);
        g.setColour (Colour (0xff16181d));
        g.fillRect (tabBar);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds (r.removeFromTop (52));
        learnBanner.setBounds (getLocalBounds().withSizeKeepingCentre (520, 40).withY (60));
        auto bottom = r.removeFromBottom (jmin (bottomHeight, r.getHeight() - 150));
        resizer.setBounds (r.removeFromBottom (5));

        if (browser)
        {
            browser->setVisible (showBrowser);
            if (showBrowser) browser->setBounds (r.removeFromLeft (250));
        }

        if (arrangement) { arrangement->setBounds (r); arrangement->setVisible (! showSession); }
        if (session)     { session->setBounds (r);     session->setVisible (showSession); }

        tabBar = bottom.removeFromTop (30);
        auto tb = tabBar.reduced (8, 3);
        for (auto* b : { &devicesTab, &mixerTab, &pianoTab })
        {
            b->setBounds (tb.removeFromLeft (100));
            tb.removeFromLeft (2);
        }

        for (auto* c : { (Component*) deviceChain.get(), (Component*) mixer.get(), (Component*) pianoRoll.get() })
            if (c != nullptr) c->setBounds (bottom);
    }

    /** Offline-renders the current project (used by the --render command-line option). */
    bool renderTo (const File& f)
    {
        if (edit == nullptr) return false;
        EditingOps::waitForClipProxies (*edit);
        return te::Renderer::renderToFile (*edit, f, false);
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (edit == nullptr) return false;

        if (k == KeyPress::escapeKey && midiLearn != nullptr && midiLearn->isLearning())
        {
            midiLearn->cancelLearning();
            return true;
        }
        auto& tc = edit->getTransport();
        const bool shift = k.getModifiers().isShiftDown();

        if (k == KeyPress::spaceKey)                       { EngineHelpers::togglePlay (*edit); return true; }
        if (isCtrlKey (k, 'r') && shift)                   { exportWav(); return true; }
        if (isCtrlKey (k, 'e') && shift)                   { exportSelection(); return true; }
        if (isCtrlKey (k, 'm') && shift)                   { exportMidi(); return true; }
        if (isCtrlKey (k, 'z') && ! shift)                 { edit->undo(); return true; }
        if (isCtrlKey (k, 'y') || (isCtrlKey (k, 'z') && shift)) { edit->redo(); return true; }
        if (isCtrlKey (k, 's'))                            { saveProject(); return true; }
        if (k == KeyPress ('r'))                           { EngineHelpers::toggleRecord (*edit); return true; }
        if (k == KeyPress ('l'))                           { tc.looping = ! tc.looping.get(); return true; }
        if (k == KeyPress ('m'))                           { edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); return true; }
        if (k == KeyPress ('1'))                           { showTab (Tab::devices); return true; }
        if (k == KeyPress ('2'))                           { showTab (Tab::mixer); return true; }
        if (k == KeyPress ('3'))                           { showTab (Tab::pianoRoll); return true; }
        if (k == KeyPress::homeKey)                        { tc.setPosition (0s); return true; }
        if (isCtrlKey (k, 'l'))                            { loopSelection(); return true; }
        if (CharacterFunctions::toUpperCase ((juce_wchar) k.getKeyCode()) == 'Z' && ! k.getModifiers().isCommandDown() && arrangement != nullptr)
        {
            arrangement->zoomToFit (shift);   // Z = selection, Shift+Z = whole song
            return true;
        }
        if ((k.getTextCharacter() == '=' || k.getTextCharacter() == '+') && ! k.getModifiers().isCommandDown())
        {
            evs->zoom (1.4, 0);
            return true;
        }
        if (k.getTextCharacter() == '-' && ! k.getModifiers().isCommandDown())
        {
            evs->zoom (1.0 / 1.4, 0);
            return true;
        }
        if (k == KeyPress::tabKey)                         { toggleSession(); return true; }
        if (k == KeyPress ('b'))                           { toggleBrowser(); return true; }

        auto& um = edit->getUndoManager();

        if (k == KeyPress::deleteKey || k == KeyPress::backspaceKey)
        {
            if (selectionManager.getFirstItemOfType<te::Clip>() != nullptr)
            {
                if (pianoRoll->getClip() != nullptr && selectionManager.isSelected (pianoRoll->getClip()))
                    pianoRoll->setClip (nullptr);
                um.beginNewTransaction ("Delete clips");
                EditingOps::deleteSelected (selectionManager);
                return true;
            }
        }

        // arrow keys move the selected clips: left/right by the grid (Shift = 1 bar), up/down between tracks
        if (! showSession && (k.isKeyCode (KeyPress::leftKey) || k.isKeyCode (KeyPress::rightKey)
                               || k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::downKey)))
        {
            auto clips = EditingOps::selectedClips (selectionManager);
            if (clips.isEmpty())
                return false;

            if (k.isKeyCode (KeyPress::upKey) || k.isKeyCode (KeyPress::downKey))
            {
                um.beginNewTransaction ("Move clips to track");
                te::SelectableList moved;
                for (auto c : Workflow::moveClipsToAdjacentTrack (*edit, clips, k.isKeyCode (KeyPress::upKey) ? -1 : 1))
                    moved.add (c);
                selectionManager.select (moved);
                return true;
            }

            const double grid = shift ? (double) EngineHelpers::beatsPerBar (*edit)
                                      : (evs->snapBeats > 0.0 ? evs->snapBeats : 0.25);
            um.beginNewTransaction ("Nudge clips");
            Workflow::nudgeClips (clips, k.isKeyCode (KeyPress::leftKey) ? -grid : grid);
            return true;
        }

        if (isCtrlKey (k, 'd')) { um.beginNewTransaction ("Duplicate");  EditingOps::duplicate (selectionManager); return true; }
        if (isCtrlKey (k, 'c')) { EditingOps::copy (selectionManager); return true; }
        if (isCtrlKey (k, 'x')) { um.beginNewTransaction ("Cut");        EditingOps::cut (selectionManager); return true; }
        if (isCtrlKey (k, 'v')) { um.beginNewTransaction ("Paste");      EditingOps::paste (*edit, selectionManager); return true; }
        if (isCtrlKey (k, 'e')) { um.beginNewTransaction ("Split");      EditingOps::splitAtPlayhead (*edit, selectionManager); return true; }
        if (isCtrlKey (k, 'a')) { EditingOps::selectAllClips (*edit, selectionManager); return true; }

        if (isCtrlKey (k, 'g'))
        {
            auto tracks = selectionManager.getItemsOfType<te::AudioTrack>();
            if (! tracks.isEmpty())
            {
                um.beginNewTransaction ("Group tracks");
                EditingOps::groupTracks (*edit, tracks, "Group");
            }
            return true;
        }

        if (isCtrlKey (k, 'j'))   // bounce the selected clips' time range of the selected track
        {
            auto clips = EditingOps::selectedClips (selectionManager);
            if (auto t = EngineHelpers::getSelectedTrack (selectionManager))
                EditingOps::bounceToNewTrack (*t, clips.isEmpty() ? std::optional<te::TimeRange>() : EditingOps::rangeOf (clips));
            return true;
        }

        return false;
    }

private:
    enum class Tab { devices, mixer, pianoRoll };

    struct Resizer : public Component
    {
        std::function<void (int)> onDrag;
        int last = 0;
        Resizer() { setMouseCursor (MouseCursor::UpDownResizeCursor); }
        void paint (Graphics& g) override { g.fillAll (isMouseOverOrDragging() ? Theme::accent : Theme::line); }
        void mouseDown (const MouseEvent& e) override { last = e.getScreenY(); }
        void mouseDrag (const MouseEvent& e) override { if (onDrag) onDrag (e.getScreenY() - last); last = e.getScreenY(); }
        void mouseEnter (const MouseEvent&) override { repaint(); }
        void mouseExit (const MouseEvent&) override { repaint(); }
    };

    //==============================================================================
    TransportBar::Actions makeActions()
    {
        TransportBar::Actions a;
        a.newProject     = [this] { newProject(); };
        a.openProject    = [this] { openProject(); };
        a.saveProject    = [this] { saveProject(); };
        a.exportWav      = [this] { exportWav(); };
        a.exportSelection = [this] { exportSelection(); };
        a.exportMidi     = [this] { exportMidi(); };
        a.audioSettings  = [this] { showAudioSettings(); };
        a.pluginManager  = [this] { showPluginManager(); };
        a.addInstrumentTrack = [this] { addTrack (true); };
        a.addAudioTrack      = [this] { addTrack (false); };
        a.toggleBrowser      = [this] { toggleBrowser(); };
        a.toggleSession      = [this] { toggleSession(); };
        a.isBrowserShown     = [this] { return showBrowser; };
        a.isSessionShown     = [this] { return showSession; };
        a.controllerStatus   = [this] { return push != nullptr ? push->getStatus() : String(); };
        return a;
    }

    void toggleBrowser()
    {
        showBrowser = ! showBrowser;
        settings().setValue ("showBrowser", showBrowser);
        resized();
    }

    void toggleSession()
    {
        showSession = ! showSession;
        resized();
    }

    void showTab (Tab t)
    {
        currentTab = t;
        devicesTab.setToggleState (t == Tab::devices, dontSendNotification);
        mixerTab.setToggleState (t == Tab::mixer, dontSendNotification);
        pianoTab.setToggleState (t == Tab::pianoRoll, dontSendNotification);
        if (deviceChain) deviceChain->setVisible (t == Tab::devices);
        if (mixer)       mixer->setVisible (t == Tab::mixer);
        if (pianoRoll)   pianoRoll->setVisible (t == Tab::pianoRoll);
    }

    //==============================================================================
    void enableInputs()
    {
        auto& dm = engine.getDeviceManager();
        RecordingSettings::applyOverdub (engine, settings().getBoolValue ("midiOverdub", true));

        for (auto& midiIn : dm.getMidiInDevices())
        {
            midiIn->setMonitorMode (te::InputDevice::MonitorMode::automatic);
            midiIn->setEnabled (true);
        }

        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto wip = dm.getWaveInDevice (i))
            {
                wip->setMonitorMode (te::InputDevice::MonitorMode::automatic);
                wip->setEnabled (true);
            }
    }

    File projectsFolder()
    {
        // "Pulse Studio" normally; a separate folder for the --dev-profile build.
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (engine.getPropertyStorage().getApplicationName());
        d.createDirectory();
        return d;
    }

    //==============================================================================
    // Autosave: every minute unsaved changes go to a temp copy next to the project.
    // If the app dies, the next launch offers to recover it.
    void timerCallback() override
    {
        if (edit != nullptr && edit->hasChangedSinceSaved())
            te::EditFileOperations (*edit).saveTempVersion (false);
    }

    void offerRecoveryIfNeeded (const File& file)
    {
        auto temp = te::EditFileOperations::getTempVersionOfEditFile (file);

        if (! temp.existsAsFile())
            return;

        if (file.existsAsFile() && temp.getLastModificationTime() <= file.getLastModificationTime())
        {
            temp.deleteFile();
            return;
        }

        MessageManager::callAsync ([this, file, temp, sp = SafePointer<MainComponent> (this)]
        {
            if (sp == nullptr) return;
            AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Recover unsaved changes?",
                                          "Pulse Studio didn't close cleanly last time. An autosaved version of \""
                                            + file.getFileNameWithoutExtension() + "\" from "
                                            + temp.getLastModificationTime().toString (true, true) + " is available.",
                                          "Recover", "Discard", nullptr,
                                          ModalCallbackFunction::create ([this, file, temp, sp] (int result)
                                          {
                                              if (sp == nullptr) return;
                                              if (result == 1 && temp.copyFileTo (file))
                                              {
                                                  temp.deleteFile();
                                                  loadEdit (file, false);
                                              }
                                              else
                                              {
                                                  temp.deleteFile();
                                              }
                                          }));
        });
    }

    PropertiesFile& settings() { return engine.getPropertyStorage().getPropertiesFile(); }

    void openStartupProject()
    {
        File last (settings().getValue ("lastEdit"));

        if (last.existsAsFile())
            loadEdit (last, false);
        else
            loadEdit (projectsFolder().getNonexistentChildFile ("Demo Song", ".tracktionedit", false), true);
    }

    void loadEdit (const File& file, bool createDemo)
    {
        closeEdit();
        editFile = file;

        if (file.existsAsFile())
            edit = te::loadEditFromFile (engine, file);
        else
            edit = te::createEmptyEdit (engine, file);

        edit->playInStopEnabled = true;

        if (createDemo)
            createDemoContent();

        // Master meter (the mixer reads the master level from it).
        if (edit->getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>() == nullptr)
            edit->getMasterPluginList().insertPlugin (edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);

        edit->getTransport().ensureContextAllocated();
        edit->getUndoManager().clearUndoHistory();

        evs = std::make_unique<EditViewState> (*edit, selectionManager);
        evs->openPianoRoll = [this] (te::MidiClip* mc) { pianoRoll->setClip (mc); showTab (Tab::pianoRoll); };
        midiLearn = std::make_unique<MidiLearn> (*edit);
        midiLearn->addChangeListener (this);
        if (push != nullptr) push->setView (evs.get());
        evs->midiLearn = midiLearn.get();

        transport.setEdit (edit.get());
        arrangement = std::make_unique<ArrangementView> (*evs);
        session     = std::make_unique<SessionView> (*evs);
        browser     = std::make_unique<Browser> (*evs, settings());
        deviceChain = std::make_unique<DeviceChain> (*evs);
        mixer       = std::make_unique<Mixer> (*evs);
        pianoRoll   = std::make_unique<PianoRoll> (*evs);
        pianoRoll->setClip (nullptr);
        showBrowser = settings().getBoolValue ("showBrowser", true);

        for (auto* c : std::initializer_list<Component*> { arrangement.get(), session.get(), browser.get(),
                                                           deviceChain.get(), mixer.get(), pianoRoll.get() })
            addAndMakeVisible (c);

        selectionManager.addChangeListener (this);

        if (auto first = EngineHelpers::getUserTracks (*edit).getFirst())
        {
            selectionManager.selectOnly (first);
            EngineHelpers::routeMidiInputsTo (*first);
        }

        showTab (currentTab);
        settings().setValue ("lastEdit", file.getFullPathName());
        updateWindowTitle();
        resized();
        offerRecoveryIfNeeded (file);
    }

    void closeEdit()
    {
        if (edit == nullptr) return;
        selectionManager.removeChangeListener (this);
        selectionManager.deselectAll();
        edit->getTransport().stop (false, true);
        transport.setEdit (nullptr);
        pianoRoll = nullptr;
        mixer = nullptr;
        deviceChain = nullptr;
        browser = nullptr;
        session = nullptr;
        arrangement = nullptr;
        if (push != nullptr) push->setView (nullptr);
        Launcher::slotRecordings().clear();
        midiLearn = nullptr;
        evs = nullptr;
        edit = nullptr;
    }

    void updateWindowTitle()
    {
        if (auto* w = findParentComponentOfClass<DocumentWindow>())
            w->setName (engine.getPropertyStorage().getApplicationName() + " - " + editFile.getFileNameWithoutExtension());
    }

    void parentHierarchyChanged() override { updateWindowTitle(); }

    void changeListenerCallback (ChangeBroadcaster* source) override
    {
        if (midiLearn != nullptr && source == midiLearn.get())
        {
            learnBanner.setText ("MIDI Learn: move a knob or fader on your controller for  " + midiLearn->getLearningName()
                                   + "   (Esc to cancel)", dontSendNotification);
            learnBanner.setVisible (midiLearn->isLearning());
            learnBanner.toFront (false);
            return;
        }

        // The selected track receives live MIDI input (so you can play it right away).
        if (auto t = EngineHelpers::getSelectedTrack (selectionManager))
            EngineHelpers::routeMidiInputsTo (*t);
    }

    //==============================================================================
    void createDemoContent()
    {
        edit->tempoSequence.getTempo (0)->setBpm (120.0);
        edit->ensureNumberOfAudioTracks (4);
        auto tracks = EngineHelpers::getUserTracks (*edit);
        const char* names[] = { "Keys", "Bass", "Lead", "Audio" };

        for (int i = 0; i < tracks.size(); ++i)
        {
            tracks[i]->setName (names[i]);
            tracks[i]->setColour (Theme::trackColour (i + 1));
        }

        auto addSynth = [this] (te::AudioTrack& t)
        {
            if (auto p = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
                t.pluginList.insertPlugin (p, 0, nullptr);
        };
        auto bars = [this] (double b) { return EngineHelpers::beatToTime (*edit, b * 4.0); };

        for (int i = 0; i < 3; ++i)
            addSynth (*tracks[i]);

        // Keys: Am - F - C - G
        if (auto mc = tracks[0]->insertMIDIClip ("Chords", { bars (0), bars (4) }, nullptr))
        {
            const int chords[4][3] = { { 57, 60, 64 }, { 57, 60, 65 }, { 55, 60, 64 }, { 55, 59, 62 } };
            for (int bar = 0; bar < 4; ++bar)
                for (int n : chords[bar])
                    mc->getSequence().addNote (n, te::BeatPosition::fromBeats (bar * 4.0), te::BeatDuration::fromBeats (3.9), 85, 0, nullptr);
        }

        // Bass: off-beat roots
        if (auto mc = tracks[1]->insertMIDIClip ("Bassline", { bars (0), bars (4) }, nullptr))
        {
            const int roots[] = { 33, 29, 36, 31 };
            for (int bar = 0; bar < 4; ++bar)
                for (int b = 0; b < 4; ++b)
                    mc->getSequence().addNote (roots[bar], te::BeatPosition::fromBeats (bar * 4.0 + b + 0.5), te::BeatDuration::fromBeats (0.4), 105, 0, nullptr);
        }

        // Lead: arpeggio in bars 3-4
        if (auto mc = tracks[2]->insertMIDIClip ("Arp", { bars (2), bars (4) }, nullptr))
        {
            const int arp[] = { 69, 72, 76, 72, 65, 69, 72, 69, 67, 72, 76, 72, 67, 71, 74, 71 };
            for (int i = 0; i < 16; ++i)
                mc->getSequence().addNote (arp[i], te::BeatPosition::fromBeats (i * 0.5), te::BeatDuration::fromBeats (0.3), 90, 0, nullptr);
        }

        if (auto vp = tracks[0]->getVolumePlugin()) vp->setVolumeDb (-8.0f);
        if (auto vp = tracks[1]->getVolumePlugin()) vp->setVolumeDb (-4.0f);
        if (auto vp = tracks[2]->getVolumePlugin()) vp->setVolumeDb (-10.0f);

        // Return buses: Reverb + Delay, fed by sends on the synths.
        if (auto rev = Routing::createReturnTrack (*edit, "Reverb", te::ReverbPlugin::xmlTypeName))
            if (auto r = rev->pluginList.findFirstPluginOfType<te::ReverbPlugin>())
            {
                r->setDry (0.0f);
                r->setWet (1.0f);
                r->setRoomSize (0.75f);
            }

        if (auto del = Routing::createReturnTrack (*edit, "Delay", te::DelayPlugin::xmlTypeName))
            if (auto d = del->pluginList.findFirstPluginOfType<te::DelayPlugin>())
            {
                d->mixValue = 1.0f;
                d->lengthMs = 375;   // dotted eighth at 120 BPM
            }

        Routing::setSendDb (*tracks[0], 0, -10.0f);
        Routing::setSendDb (*tracks[2], 0, -14.0f);
        Routing::setSendDb (*tracks[2], 1, -9.0f);

        auto& tc = edit->getTransport();
        tc.setLoopRange ({ bars (0), bars (4) });
        tc.looping = true;
        te::EditFileOperations (*edit).save (true, true, false);
    }

    //==============================================================================
    void addTrack (bool instrument)
    {
        const auto n = EngineHelpers::getUserTracks (*edit).size() + 1;

        if (! instrument)
        {
            if (auto t = ClipImport::createContentTrack (*edit, "Audio " + String (n), false))
                selectionManager.selectOnly (t);
            return;
        }

        // instrument tracks: pick what goes on them
        PopupMenu m;
        auto add = [this, n] (const String& name, std::function<te::Plugin::Ptr()> make)
        {
            edit->getUndoManager().beginNewTransaction ("Add track");
            if (auto t = ClipImport::createContentTrack (*edit, name + " " + String (n), false))
            {
                if (auto p = make()) PluginMenu::insertPlugin (t, t->pluginList, p, nullptr);
                selectionManager.selectOnly (t);
                showTab (Tab::devices);
            }
        };
        m.addItem ("Wavetable synth", [this, add] { add ("Wavetable", [this] { return edit->getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {}); }); });
        m.addItem ("Simpler (sampler)", [this, add] { add ("Simpler", [this] { return edit->getPluginCache().createNewPlugin (pulse::Simpler::xmlTypeName, {}); }); });
        m.addItem ("4OSC synth", [this, add] { add ("Synth", [this] { return edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}); }); });
        m.addItem ("Drum Rack",  [this, add] { add ("Drums", [this] { return DrumRack::create (*edit); }); });
        m.addItem ("Multi-sampler", [this, add] { add ("Sampler", [this] { return edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {}); }); });
        m.addItem ("VST instrument...", [this, add] { add ("Instrument", [] { return te::Plugin::Ptr(); }); showTab (Tab::devices); });
        m.showMenuAsync ({});
    }

    //==============================================================================
    void saveIfNeeded()
    {
        if (edit != nullptr)
        {
            te::EditFileOperations ops (*edit);
            ops.save (true, true, false);
            if (! edit->hasChangedSinceSaved())
                ops.deleteTempVersion();
        }
    }

    void saveProject()
    {
        saveIfNeeded();
    }

    void newProject()
    {
        chooser = std::make_shared<FileChooser> ("New project", projectsFolder().getNonexistentChildFile ("Untitled", ".tracktionedit"), "*.tracktionedit");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  saveIfNeeded();
                                  f.deleteFile();
                                  loadEdit (f.withFileExtension (".tracktionedit"), false);
                                  addTrack (true);
                                  addTrack (false);
                              });
    }

    void openProject()
    {
        chooser = std::make_shared<FileChooser> ("Open project", projectsFolder(), "*.tracktionedit");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (! f.existsAsFile()) return;
                                  saveIfNeeded();
                                  loadEdit (f, false);
                              });
    }

    /** Sets the loop range to the selected clips and turns looping on. */
    void loopSelection()
    {
        auto clips = EditingOps::selectedClips (selectionManager);
        if (clips.isEmpty())
            return;
        auto& tc = edit->getTransport();
        tc.setLoopRange (EditingOps::rangeOf (clips));
        tc.looping = true;
    }

    void exportSelection()
    {
        auto clips = EditingOps::selectedClips (selectionManager);
        if (clips.isEmpty())
        {
            if (edit->getTransport().getLoopRange().isEmpty())
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export selection",
                                                  "Select some clips (or set a loop range) first.");
                return;
            }
            showExportDialog (*edit, edit->getTransport().getLoopRange());
            return;
        }
        showExportDialog (*edit, EditingOps::rangeOf (clips));
    }

    /** Exports the selected MIDI clips, or every MIDI clip in the song. */
    void exportMidi()
    {
        Array<te::MidiClip*> clips;
        for (auto c : EditingOps::selectedClips (selectionManager))
            if (auto mc = dynamic_cast<te::MidiClip*> (c))
                clips.add (mc);

        if (clips.isEmpty())
            clips = Workflow::allMidiClips (*edit);

        Workflow::chooseAndExportMidi (*edit, clips, edit->getName());
    }

    void exportWav()
    {
        showExportDialog (*edit);
    }

    void showAudioSettings()
    {
        showAudioSettingsDialog (engine, *edit, settings());
    }

    void showPluginManager()
    {
        DialogWindow::LaunchOptions o;
        o.dialogTitle = "Plugins";
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.useBottomRightCornerResizer = true;

        auto& pm = engine.getPluginManager();
        auto* v = new PluginListComponent (pm.pluginFormatManager, pm.knownPluginList,
                                           engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                           std::addressof (settings()), true);
        v->setSize (820, 600);
        o.content.setOwned (v);
        o.launchAsync();
    }

    //==============================================================================
    te::Engine& engine;
    te::SelectionManager selectionManager { engine };
    std::unique_ptr<te::Edit> edit;
    File editFile;
    std::unique_ptr<EditViewState> evs;
    std::unique_ptr<MidiLearn> midiLearn;
    Label learnBanner;

    std::unique_ptr<Push2Controller> push;   // declared before transport: transport polls its status
    TransportBar transport;
    std::unique_ptr<ArrangementView> arrangement;
    std::unique_ptr<SessionView> session;
    std::unique_ptr<Browser> browser;
    bool showBrowser = true, showSession = false;
    std::unique_ptr<DeviceChain> deviceChain;
    std::unique_ptr<Mixer> mixer;
    std::unique_ptr<PianoRoll> pianoRoll;
    Resizer resizer;

    TextButton devicesTab { "Devices" }, mixerTab { "Mixer" }, pianoTab { "Piano Roll" };
    Rectangle<int> tabBar;
    Tab currentTab = Tab::devices;
    int bottomHeight = 330;
    std::shared_ptr<FileChooser> chooser;
};
