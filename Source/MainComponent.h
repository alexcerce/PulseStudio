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
#include "LinkSync.h"
#include "Timeline.h"
#include "MidiLearn.h"
#include "DrumRack.h"
#include "AudioSettings.h"
#include "Push2.h"
#include "CrashGuard.h"
#include "Sharing.h"
#include "BackgroundRender.h"
#include "Commands.h"
#include "ClipEnvelopes.h"

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
        learnBanner.setFont (FontOptions (14.5f, Font::bold));
        addChildComponent (learnBanner);

        PluginMenu::drumRackFactory = [] (te::Edit& ed, bool kit) { return DrumRack::create (ed, kit); };
        drumLibraryFolders = [this]
        {
            Array<File> f { File::getSpecialLocation (File::userMusicDirectory),
                            File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio") };
            for (auto& p : StringArray::fromLines (settings().getValue ("browserFolders")))
                if (File (p).isDirectory()) f.addIfNotAlreadyThere (File (p));
            return f;
        };

        push = std::make_unique<Push2Controller> (engine);
        push->onToggleBrowser = [this] { toggleBrowser(); };

        setWantsKeyboardFocus (true);
        commands = std::make_unique<CommandRegistry> (settings());
        registerCommands();
        enableInputs();
        openStartupProject();
        setSize (1440, 900);
        startTimer (60 * 1000);   // autosave
    }

    te::Engine& getEngine() { return engine; }

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
        const bool shift = k.getModifiers().isShiftDown();

        if (isCtrlKey (k, 'z') && shift) { edit->redo(); return true; }   // alternative redo
        if (commands->dispatch (k))
            return true;

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
            for (auto c : clips)
                if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack())) Timeline::updateCrossfades (*t);
            return true;
        }


        return false;
    }

    //==============================================================================
    void registerCommands()
    {
        auto& c = *commands;
        auto key = [] (int code, ModifierKeys m = {}) { return KeyPress (code, m, 0); };
        const auto ctrl = ModifierKeys::commandModifier, shiftM = ModifierKeys::shiftModifier;
        auto withEdit = [this] (std::function<void (te::Edit&)> fn) { return [this, fn] { if (edit != nullptr) fn (*edit); }; };
        auto um = [this] (const char* name) { edit->getUndoManager().beginNewTransaction (name); };

        c.add ("transport.play", "Play / stop", "Transport", key (KeyPress::spaceKey), withEdit ([] (te::Edit& e) { EngineHelpers::togglePlay (e); }));
        c.add ("transport.record", "Record", "Transport", key ('R'), withEdit ([] (te::Edit& e) { EngineHelpers::toggleRecord (e); }));
        c.add ("transport.loop", "Loop on / off", "Transport", key ('L'), withEdit ([] (te::Edit& e) { e.getTransport().looping = ! e.getTransport().looping.get(); }));
        c.add ("transport.metronome", "Metronome on / off", "Transport", key ('M'), withEdit ([] (te::Edit& e) { e.clickTrackEnabled = ! e.clickTrackEnabled.get(); }));
        c.add ("transport.home", "Go to start", "Transport", key (KeyPress::homeKey), withEdit ([] (te::Edit& e) { e.getTransport().setPosition (0s); }));
        c.add ("transport.capture", "Capture MIDI (what you just played)", "Transport", key ('C', ctrl | shiftM), [this] { captureMidi(); });

        c.add ("edit.undo", "Undo", "Edit", key ('Z', ctrl), withEdit ([] (te::Edit& e) { e.undo(); }));
        c.add ("edit.redo", "Redo", "Edit", key ('Y', ctrl), withEdit ([] (te::Edit& e) { e.redo(); }));
        c.add ("edit.copy", "Copy", "Edit", key ('C', ctrl), [this] { EditingOps::copy (selectionManager); });
        c.add ("edit.cut", "Cut", "Edit", key ('X', ctrl), [this, um] { um ("Cut"); EditingOps::cut (selectionManager); });
        c.add ("edit.paste", "Paste at playhead", "Edit", key ('V', ctrl), [this, um] { um ("Paste"); EditingOps::paste (*edit, selectionManager); });
        c.add ("edit.duplicate", "Duplicate", "Edit", key ('D', ctrl), [this, um] { um ("Duplicate"); EditingOps::duplicate (selectionManager); });
        c.add ("edit.split", "Split at playhead", "Edit", key ('E', ctrl), [this, um] { um ("Split"); EditingOps::splitAtPlayhead (*edit, selectionManager); });
        c.add ("edit.selectAll", "Select all clips", "Edit", key ('A', ctrl), [this] { EditingOps::selectAllClips (*edit, selectionManager); });
        c.add ("edit.loopSelection", "Loop the selection", "Edit", key ('L', ctrl), [this] { loopSelection(); });
        c.add ("edit.group", "Group selected tracks", "Edit", key ('G', ctrl), [this, um]
        {
            auto tracks = selectionManager.getItemsOfType<te::AudioTrack>();
            if (! tracks.isEmpty()) { um ("Group tracks"); EditingOps::groupTracks (*edit, tracks, "Group"); }
        });
        c.add ("edit.bounce", "Bounce selection to a new track", "Edit", key ('J', ctrl), [this]
        {
            auto clips = EditingOps::selectedClips (selectionManager);
            if (auto t = EngineHelpers::getSelectedTrack (selectionManager))
                EditingOps::bounceToNewTrack (*t, clips.isEmpty() ? std::optional<te::TimeRange>() : EditingOps::rangeOf (clips));
        });

        c.add ("marker.add", "Add marker at playhead", "Markers", key ('K', ctrl), [this, um] { um ("Add marker"); Timeline::addMarker (*edit, edit->getTransport().getPosition()); });
        c.add ("marker.next", "Next marker", "Markers", key (KeyPress::rightKey, ctrl), [this] { Timeline::jumpToMarker (*edit, true); });
        c.add ("marker.prev", "Previous marker", "Markers", key (KeyPress::leftKey, ctrl), [this] { Timeline::jumpToMarker (*edit, false); });

        c.add ("view.devices", "Show devices", "View", key ('1'), [this] { showTab (Tab::devices); });
        c.add ("view.mixer", "Show mixer", "View", key ('2'), [this] { showTab (Tab::mixer); });
        c.add ("view.pianoRoll", "Show piano roll", "View", key ('3'), [this] { showTab (Tab::pianoRoll); });
        c.add ("view.session", "Arrangement / Session", "View", key (KeyPress::tabKey), [this] { toggleSession(); });
        c.add ("view.browser", "Show / hide browser", "View", key ('B'), [this] { toggleBrowser(); });
        c.add ("view.zoomSelection", "Zoom to selection", "View", key ('Z'), [this] { if (arrangement) arrangement->zoomToFit (false); });
        c.add ("view.zoomSong", "Zoom to whole song", "View", key ('Z', shiftM), [this] { if (arrangement) arrangement->zoomToFit (true); });
        c.add ("view.zoomIn", "Zoom in", "View", key ('='), [this] { evs->zoom (1.4, 0); });
        c.add ("view.zoomOut", "Zoom out", "View", key ('-'), [this] { evs->zoom (1.0 / 1.4, 0); });
        c.add ("view.palette", "Command palette", "View", key ('P', ctrl), [this] { openPalette(); });
        c.add ("edit.pencil", "Pencil (draw automation)", "Edit", key ('D'), [this] { evs->drawMode = ! evs->drawMode; evs->viewChanged.sendChangeMessage(); });

        c.add ("track.addInstrument", "New instrument track", "Tracks", key ('T', ctrl), [this] { addTrack (true); });
        c.add ("track.addAudio", "New audio track", "Tracks", key ('T', ctrl | shiftM), [this] { addTrack (false); });

        c.add ("file.new", "New project", "File", key ('N', ctrl), [this] { newProject(); });
        c.add ("file.open", "Open project", "File", key ('O', ctrl), [this] { openProject(); });
        c.add ("file.save", "Save", "File", key ('S', ctrl), [this] { saveProject(); });
        c.add ("file.collect", "Collect all and save", "File", {}, [this] { collectAll(); });
        c.add ("file.template", "Save as template", "File", {}, [this] { saveAsTemplate(); });
        c.add ("file.exportAudio", "Export audio / stems", "File", key ('R', ctrl | shiftM), [this] { exportWav(); });
        c.add ("file.exportSelection", "Export selection", "File", key ('E', ctrl | shiftM), [this] { exportSelection(); });
        c.add ("file.exportMidi", "Export MIDI", "File", key ('M', ctrl | shiftM), [this] { exportMidi(); });

        c.add ("app.audioSettings", "Audio settings", "App", {}, [this] { showAudioSettings(); });
        c.add ("app.plugins", "Plugin manager", "App", {}, [this] { showPluginManager(); });
        c.add ("app.shortcuts", "Keyboard shortcuts", "App", {}, [this] { showShortcutEditor(); });
    }

    void openPalette()
    {
        if (edit == nullptr) return;
        std::vector<CommandPalette::Entry> entries;
        for (auto& c : commands->all())
            entries.push_back ({ c.name, CommandRegistry::describe (commands->keyFor (c)), c.run });

        // devices for the selected track
        for (auto& b : ClipImport::builtIns())
            entries.push_back ({ String ("Add ") + b.name, b.synth ? "instrument" : b.midiFx ? "MIDI effect" : "audio effect",
                                 [this, type = String ("plugin:builtin:") + b.type]
                                 {
                                     if (auto t = EngineHelpers::getSelectedTrack (selectionManager))
                                         Presets::applyDescription (t, t->pluginList, type);
                                 } });
        // markers and tracks
        for (auto m : Timeline::markers (*edit))
            entries.push_back ({ "Go to " + m->getName(), "marker", [this, t = m->getPosition().getStart()] { edit->getTransport().setPosition (t); } });
        for (auto t : EngineHelpers::getUserTracks (*edit))
            entries.push_back ({ "Select " + t->getName(), "track", [this, id = t->itemID]
                                 {
                                     if (auto tr = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, id))) selectionManager.selectOnly (tr);
                                 } });

        palette = std::make_unique<CommandPalette> (std::move (entries));
        palette->onDismiss = [this] { palette = nullptr; grabKeyboardFocus(); };
        addAndMakeVisible (*palette);
        palette->setBounds (getLocalBounds().withSizeKeepingCentre (560, 420).withY (70));
        palette->focus();
    }

    void showShortcutEditor()
    {
        DialogWindow::LaunchOptions o;
        o.dialogTitle = "Keyboard shortcuts";
        o.dialogBackgroundColour = Theme::panel;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.content.setOwned (new ShortcutEditor (*commands));
        o.launchAsync();
    }

    /** Ableton-style Capture: turns what was just played on a MIDI keyboard into a clip. */
    void captureMidi()
    {
        if (edit == nullptr) return;
        edit->getUndoManager().beginNewTransaction ("Capture MIDI");
        int made = 0;
        for (auto* inst : edit->getAllInputDevices())
            if (dynamic_cast<te::MidiInputDevice*> (&inst->owner) != nullptr)
                made += inst->applyRetrospectiveRecord (false).size();
        if (made == 0)
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Capture MIDI",
                                              "Nothing to capture yet - play something on your MIDI keyboard first (Pulse keeps the last 2 minutes).");
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
        a.collectAll     = [this] { collectAll(); };
        a.openPalette    = [this] { openPalette(); };
        a.editShortcuts  = [this] { showShortcutEditor(); };
        a.cancelRender   = [] { BackgroundRender::instance().cancel(); };
        a.renderProgress = [] { auto& r = BackgroundRender::instance(); return r.isBusy() ? r.getProgress() : -1.0f; };
        a.renderStatus   = [] { return BackgroundRender::instance().getStatus(); };
        a.saveTemplate   = [this] { saveAsTemplate(); };
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
        a.toggleLink         = [this]
        {
            if (linkSync == nullptr) return;
            linkSync->setEnabled (! linkSync->isEnabled());
            settings().setValue ("linkEnabled", linkSync->isEnabled());
        };
        a.isLinkEnabled      = [this] { return linkSync != nullptr && linkSync->isEnabled(); };
        a.linkStatus         = [this] { return linkSync != nullptr ? linkSync->getStatus() : String(); };
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
            midiIn->updateRetrospectiveBufferLength (120.0);   // for Capture MIDI
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
        DrumRack::migrateAll (*edit);   // racks from Pulse 0.17 and older

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
        linkSync = std::make_unique<LinkSync> (*edit);
        envelopePlayer = std::make_unique<ClipEnvelopes::Player> (*edit);
        if (settings().getBoolValue ("linkEnabled", false))
            linkSync->setEnabled (true);
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
        restoreViewState();
        CrashGuard::endLoading();   // the project's plugins are loaded
        settings().setValue ("lastEdit", file.getFullPathName());
        updateWindowTitle();
        resized();
        offerRecoveryIfNeeded (file);
    }

    void closeEdit()
    {
        if (edit == nullptr) return;
        BackgroundRender::instance().cancelAndWait();
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
        linkSync = nullptr;
        envelopePlayer = nullptr;
        evs = nullptr;
        edit = nullptr;
    }

    void updateWindowTitle()
    {
        if (auto* w = findParentComponentOfClass<DocumentWindow>())
            w->setName (engine.getPropertyStorage().getApplicationName() + " - " + editFile.getFileNameWithoutExtension()
                        + (CrashGuard::safeMode() ? "   [SAFE MODE - plugins not loaded]" : ""));
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
        m.addItem ("Drum Rack (synth kit)", [this, add] { add ("Drums", [this] { return DrumRack::create (*edit, true); }); });
        m.addItem ("Drum Synth", [this, add] { add ("Drums", [this] { return edit->getPluginCache().createNewPlugin (pulse::DrumSynth::xmlTypeName, {}); }); });
        m.addItem ("Multi-sampler", [this, add] { add ("Sampler", [this] { return edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {}); }); });
        m.addItem ("VST instrument...", [this, add] { add ("Instrument", [] { return te::Plugin::Ptr(); }); showTab (Tab::devices); });
        m.showMenuAsync ({});
    }

    //==============================================================================
    //==============================================================================
    // Per-project view state (zoom, scroll, panels, selection) saved inside the project
    static inline const Identifier viewStateID { "PULSEVIEW" };

    void saveViewState()
    {
        if (edit == nullptr || evs == nullptr) return;
        auto v = edit->state.getOrCreateChildWithName (viewStateID, nullptr);
        v.setProperty ("pixelsPerBeat", evs->pixelsPerBeat, nullptr);
        v.setProperty ("viewStartBeat", evs->viewStartBeat, nullptr);
        v.setProperty ("snap", arrangement != nullptr ? arrangement->getSnapIndex() : 2, nullptr);
        v.setProperty ("showBrowser", showBrowser, nullptr);
        v.setProperty ("showSession", showSession, nullptr);
        v.setProperty ("tab", (int) currentTab, nullptr);
        v.setProperty ("bottomHeight", bottomHeight, nullptr);
        if (auto t = EngineHelpers::getSelectedTrack (selectionManager))
            v.setProperty ("selectedTrack", t->itemID.toString(), nullptr);
        StringArray collapsed;
        for (auto& id : evs->collapsed) collapsed.add (id.toString());
        v.setProperty ("collapsed", collapsed.joinIntoString (","), nullptr);
    }

    void restoreViewState()
    {
        auto v = edit->state.getChildWithName (viewStateID);
        if (! v.isValid() || evs == nullptr) return;
        evs->pixelsPerBeat = jlimit (2.0, 600.0, (double) v.getProperty ("pixelsPerBeat", evs->pixelsPerBeat));
        evs->viewStartBeat = jmax (0.0, (double) v.getProperty ("viewStartBeat", 0.0));
        if (arrangement != nullptr) arrangement->setSnapIndex ((int) v.getProperty ("snap", 2));
        showBrowser = (bool) v.getProperty ("showBrowser", showBrowser);
        showSession = (bool) v.getProperty ("showSession", false);
        bottomHeight = jlimit (120, 900, (int) v.getProperty ("bottomHeight", bottomHeight));
        for (auto& id : StringArray::fromTokens (v["collapsed"].toString(), ",", {}))
            if (id.isNotEmpty()) evs->collapsed.insert (te::EditItemID::fromString (id));
        if (auto id = v["selectedTrack"].toString(); id.isNotEmpty())
            if (auto t = dynamic_cast<te::AudioTrack*> (te::findTrackForID (*edit, te::EditItemID::fromString (id))))
                selectionManager.selectOnly (t);
        showTab ((Tab) jlimit (0, 2, (int) v.getProperty ("tab", (int) currentTab)));
        evs->viewChanged.sendChangeMessage();
        evs->layoutChanged.sendChangeMessage();
        resized();
    }

    void saveIfNeeded()
    {
        saveViewState();
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

    /** New project: empty, the demo song, or one of your templates. */
    void newProject()
    {
        PopupMenu m;
        m.addSectionHeader ("New project from...");
        m.addItem ("Empty project", [this] { createProject ({}, false); });
        m.addItem ("Demo song", [this] { createProject ({}, true); });
        auto templates = Sharing::templates();
        if (! templates.isEmpty())
        {
            m.addSeparator();
            for (auto& t : templates)
                m.addItem (t.getFileNameWithoutExtension(), [this, t] { createProject (t, false); });
        }
        m.addSeparator();
        m.addItem ("Show templates folder", [] { Sharing::templatesFolder().revealToUser(); });
        m.showMenuAsync ({});
    }

    void createProject (File templateFile, bool demo)
    {
        chooser = std::make_shared<FileChooser> ("New project", projectsFolder().getNonexistentChildFile ("Untitled", ".tracktionedit"), "*.tracktionedit");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this, templateFile, demo] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File()) return;
                                  f = f.withFileExtension (".tracktionedit");
                                  saveIfNeeded();
                                  f.deleteFile();
                                  if (templateFile.existsAsFile())
                                  {
                                      templateFile.copyFileTo (f);
                                      loadEdit (f, false);
                                      return;
                                  }
                                  loadEdit (f, demo);
                                  if (! demo)
                                  {
                                      addTrack (true);
                                      addTrack (false);
                                  }
                              });
    }

    void collectAll()
    {
        saveIfNeeded();
        auto r = Sharing::collectAll (*edit);
        AlertWindow::showMessageBoxAsync (r.missing.isEmpty() ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon,
                                          "Collect all and save", r.summary());
    }

    void saveAsTemplate()
    {
        saveIfNeeded();
        Presets::askForName ("Save as template", editFile.getFileNameWithoutExtension(), [this] (const String& name)
        {
            const bool ok = Sharing::saveTemplate (*edit, name);
            AlertWindow::showMessageBoxAsync (ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Template",
                                              ok ? "Saved \"" + name + "\". Choose it from File > New Project." : String ("Couldn't save the template."));
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
    std::unique_ptr<LinkSync> linkSync;
    std::unique_ptr<ClipEnvelopes::Player> envelopePlayer;
    std::unique_ptr<CommandRegistry> commands;
    std::unique_ptr<CommandPalette> palette;
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
