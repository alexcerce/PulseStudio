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
#include "WelcomeScreen.h"
#include "PluginScan.h"
#include "FactoryContent.h"
#include "ShareTools.h"
#include "Upgrade.h"
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
            b->setColour (TextButton::buttonOnColourId, Theme::accentDim);
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
        WavetableUI::installModDrop();
        PluginMenu::onInserted = [] (te::Plugin& p) { if (auto s = dynamic_cast<pulse::Simpler*> (&p)) FactorySounds::fillSimpler (*s); };
        drumLibraryFolders = [this]
        {
            Array<File> f { File::getSpecialLocation (File::userMusicDirectory),
                            File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio") };
            for (auto& p : StringArray::fromLines (settings().getValue ("browserFolders")))
                if (File (p).isDirectory()) f.addIfNotAlreadyThere (File (p));
            return f;
        };

        StartupTimer::mark ("ui basics");
        push = std::make_unique<Push2Controller> (engine);
        StartupTimer::mark ("push controller");
        push->onToggleBrowser = [this] { toggleBrowser(); };

        setWantsKeyboardFocus (true);
        commands = std::make_unique<CommandRegistry> (settings());
        registerCommands();
        enableInputs();
        StartupTimer::mark ("commands, inputs");
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
        g.setColour (Theme::bg2);
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
            if (showBrowser) browser->setBounds (r.removeFromLeft (330));
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
        const auto ctrl = ModifierKeys::commandModifier, shiftM = ModifierKeys::shiftModifier, altM = ModifierKeys::altModifier;
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
        c.add ("app.themes", "Colour theme...", "View", {}, [this]
        {
            if (edit != nullptr)
                showAudioSettingsDialog (engine, *edit, settings(), [sp = SafePointer<MainComponent> (this)] (int i) { if (sp != nullptr) sp->setTheme (i); }, true, commands.get());
        });
        c.add ("app.nextTheme", "Next colour theme", "View", {}, [this] { setTheme ((Theme::currentIndex() + 1) % (int) Theme::all().size()); });
        c.add ("app.plugins", "Plugin manager", "App", {}, [this] { showPluginManager(); });
        c.add ("app.shortcuts", "Keyboard shortcuts", "App", {}, [this] { showShortcutEditor(); });
        c.add ("edit.undoHistory", "Undo history...", "Edit", key ('H', ctrl | shiftM), [this] { if (edit) UndoHistory::showWindow (*edit); });
        c.add ("app.welcome", "Welcome screen", "App", {}, [this] { showWelcome(); });
        c.add ("midi.humanize", "MIDI: humanize notes", "MIDI", key ('H', ctrl | altM), [this] { if (pianoRoll) { showTab (Tab::pianoRoll); pianoRoll->applyTool ("humanize"); } });
        c.add ("midi.legato", "MIDI: legato", "MIDI", key ('L', ctrl | altM), [this] { if (pianoRoll) { showTab (Tab::pianoRoll); pianoRoll->applyTool ("legato"); } });
        c.add ("midi.fitScale", "MIDI: fit to scale", "MIDI", key ('F', ctrl | altM), [this] { if (pianoRoll) { showTab (Tab::pianoRoll); pianoRoll->applyTool ("fit"); } });
        c.add ("midi.strum", "MIDI: strum chords up", "MIDI", {}, [this] { if (pianoRoll) { showTab (Tab::pianoRoll); pianoRoll->applyTool ("strum"); } });
        c.add ("mixer.snapshot", "Store a mixer snapshot", "Mixer", {}, [this] { if (edit) MixSnapshots::store (*edit, "Mix " + String (MixSnapshots::names (*edit).size() + 1)); });
        c.add ("mixer.resetLoudness", "Reset the loudness meter", "Mixer", {}, [this]
        {
            if (edit) if (auto pr = edit->getMasterPluginList().findFirstPluginOfType<pulse::LoudnessProbe>()) pr->resetRequested = true;
        });
        c.add ("devices.collapseAll", "Collapse / expand all devices", "View", key ('C', ctrl | altM), [this] { if (deviceChain) deviceChain->toggleCollapseAll(); });
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
        a.midiRecordMode = [this] { return midiRecordMode(); };
        a.welcome        = [this] { showWelcome(); };
        a.exportZip      = [this]
        {
            if (edit == nullptr) return;
            auto def = Sharing::editFileOf (*edit).withFileExtension (".zip");
            chooser = std::make_shared<FileChooser> ("Export project as .zip", def, "*.zip");
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const FileChooser& fc)
                                  {
                                      auto f = fc.getResult();
                                      if (f == File() || edit == nullptr) return;
                                      f = f.withFileExtension (".zip");
                                      const auto err = ShareTools::exportZip (*edit, f);
                                      AlertWindow::showMessageBoxAsync (err.isEmpty() ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export project",
                                                                        err.isEmpty() ? "Saved " + f.getFileName() + " (project, samples and edited audio)." : err);
                                      if (err.isEmpty()) f.revealToUser();
                                  });
        };
        a.importZip      = [this]
        {
            chooser = std::make_shared<FileChooser> ("Open project from .zip", File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"), "*.zip");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                if (! fc.getResult().existsAsFile()) return;
                auto project = ShareTools::importZip (fc.getResult(), projectsFolder());
                if (project.existsAsFile()) { saveIfNeeded(); loadEdit (project, false); }
                else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Open project", "No Pulse project was found in that zip.");
            });
        };
        a.crashReports   = [this]
        {
            const int n = ShareTools::numCrashReports();
            if (n == 0) { AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Crash reports", "There are no crash reports. Good news!"); return; }
            auto zip = ShareTools::bundleCrashReports (File::getSpecialLocation (File::userDesktopDirectory), JUCEApplication::getInstance()->getApplicationVersion());
            if (zip.existsAsFile()) zip.revealToUser();
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Crash reports",
                                              zip.existsAsFile() ? String (n) + " report(s) zipped to " + zip.getFullPathName() + ".\nNothing was sent anywhere - attach the file yourself if you want to share it."
                                                                 : String ("Couldn't create the zip."));
        };
        a.checkUpdates   = [this] { checkForUpdates (true); };
        a.setMidiRecordMode = [this] (int m)
        {
            settings().setValue ("midiRecordMode", m);
            settings().setValue ("midiOverdub", m == 0);
            RecordingSettings::applyMidiMode (engine, m);
        };
        a.recentProjects = [this] { return StringArray::fromLines (settings().getValue ("recentProjects")); };
        a.openRecent     = [this] (const File& f) { if (f.existsAsFile()) { saveIfNeeded(); loadEdit (f, false); } };
        a.clearRecent    = [this] { settings().setValue ("recentProjects", String()); };
        a.backups        = [this]
        {
            StringArray out;
            if (edit != nullptr)
                for (auto& f : Backups::list (te::EditFileOperations (*edit).getEditFile())) out.add (f.getFullPathName());
            return out;
        };
        a.restoreBackup  = [this] (const File& f)
        {
            if (edit == nullptr) return;
            saveIfNeeded();
            auto copy = Backups::restoreAsCopy (f, te::EditFileOperations (*edit).getEditFile());
            if (copy.existsAsFile()) loadEdit (copy, false);
        };
        a.saveProject    = [this] { saveProject(); };
        a.exportWav      = [this] { exportWav(); };
        a.collectAll     = [this] { collectAll(); };
        a.openPalette    = [this] { openPalette(); };
        a.editShortcuts  = [this] { showShortcutEditor(); };
        a.cancelRender   = [this] { if (BackgroundRender::instance().isBusy()) BackgroundRender::instance().cancel(); else pluginScan.cancel(); };
        a.renderProgress = [this]
        {
            auto& r = BackgroundRender::instance();
            if (r.isBusy()) return r.getProgress();
            return pluginScan.isScanning() ? pluginScan.getProgress() : -1.0f;
        };
        a.renderStatus   = [this] { return BackgroundRender::instance().isBusy() ? BackgroundRender::instance().getStatus() : pluginScan.getStatus(); };
        a.renderBlocksPlayback = [] { return BackgroundRender::instance().isBusy(); };
        a.saveTemplate   = [this] { saveAsTemplate(); };
        a.exportSelection = [this] { exportSelection(); };
        a.exportMidi     = [this] { exportMidi(); };
        a.audioSettings  = [this] { showAudioSettings(); };
        a.pluginManager  = [this]
        {
            PopupMenu m;
            m.addItem ("Plugin manager...", [this] { showPluginManager(); });
            m.addItem ("Scan for new plugins (in the background)", ! pluginScan.isScanning(), false, [this] { pluginScan.start(); });
            const bool autoScan = settings().getBoolValue ("autoScanPlugins", true);
            m.addItem ("Scan for new plugins at startup", true, autoScan, [this, autoScan] { settings().setValue ("autoScanPlugins", ! autoScan); });
            m.showMenuAsync ({});
        };
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

    /** Update check against the address the user chose (Settings key "updateUrl"); off until set. */
    void checkForUpdates (bool interactive)
    {
        const auto address = settings().getValue ("updateUrl");
        if (address.isEmpty())
        {
            if (! interactive) return;
            auto* w = new AlertWindow ("Check for updates",
                                       "Pulse can check an address you choose for a newer version (a small JSON file: {\"version\": \"1.0.0\", \"url\": \"...\"}).\n"
                                       "It never downloads anything by itself. Leave empty to keep checks off.", MessageBoxIconType::NoIcon);
            w->addTextEditor ("url", {}, "Update address (https://...)");
            w->addButton ("Save and check", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
            {
                const auto url = w->getTextEditorContents ("url").trim();
                if (r == 1 && url.startsWithIgnoreCase ("http")) { settings().setValue ("updateUrl", url); checkForUpdates (true); }
            }), true);
            return;
        }
        const auto current = JUCEApplication::getInstance()->getApplicationVersion();
        ShareTools::checkForUpdate (address, [sp = SafePointer<MainComponent> (this), interactive, current] (std::optional<ShareTools::UpdateInfo> info)
        {
            if (sp == nullptr) return;
            if (info.has_value() && ShareTools::isNewer (info->version, current))
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Update available",
                                                  "Pulse Studio " + info->version + " is available (you have " + current + ")."
                                                  + (info->notes.isNotEmpty() ? "\n\n" + info->notes : String())
                                                  + (info->url.isNotEmpty() ? "\n\nGet it from: " + info->url : String()));
            else if (interactive)
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Check for updates",
                                                  info.has_value() ? "You have the latest version (" + current + ")." : String ("Couldn't read the update address."));
        });
    }

    int midiRecordMode()
    {
        if (settings().containsKey ("midiRecordMode")) return jlimit (0, 2, settings().getIntValue ("midiRecordMode"));
        return settings().getBoolValue ("midiOverdub", true) ? 0 : 2;
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
        RecordingSettings::applyMidiMode (engine, midiRecordMode());

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
        {
            te::EditFileOperations (*edit).saveTempVersion (false);
            if (++minutesChanged >= 5) { Backups::write (*edit); minutesChanged = 0; }   // backup history
        }
    }
    int minutesChanged = 0;

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

        StartupTimer::mark ("project loaded");
        if (! FactorySounds::isReady (engine))   // first run: render the built-in sounds (a few seconds, once)
        {
            FactorySounds::renderAll (engine, [] (const String& s) { if (StartupTimer::status()) StartupTimer::status() ("Creating the built-in sounds (first run only): " + s); });
            StartupTimer::mark ("factory sounds rendered");
        }
        FactoryContent::ensureTemplates (engine);
        StartupTimer::mark ("factory templates");
        if (! JUCEApplication::getCommandLineParameters().contains ("--snapshots")) checkForUpdates (false);   // only if an address was set
        if (settings().getBoolValue ("autoScanPlugins", true) && ! JUCEApplication::getCommandLineParameters().contains ("--snapshots"))
            Timer::callAfterDelay (5000, [sp = SafePointer<MainComponent> (this)] { if (sp != nullptr) sp->pluginScan.start(); });

        if (settings().getBoolValue ("showWelcome", true) && ! JUCEApplication::getCommandLineParameters().contains ("--snapshots"))
            MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)] { if (sp != nullptr) sp->showWelcome(); });
    }

public:
    /** The start screen: recent projects, new project, tips. */
    void showWelcome()
    {
        WelcomeScreen::Callbacks cb;
        cb.openProject = [this] (const File& f) { if (f.existsAsFile()) { saveIfNeeded(); loadEdit (f, false); } };
        cb.newProject = [this] (const File& t, bool demo) { createProject (t, demo); };
        cb.browseProject = [this] { openProject(); };
        cb.setShowAtStartup = [this] (bool on) { settings().setValue ("showWelcome", on); };
        auto templates = FactoryContent::templates (engine);
        templates.addArray (Sharing::templates());
        auto* w = new WelcomeScreen (StringArray::fromLines (settings().getValue ("recentProjects")), settings().getBoolValue ("showWelcome", true), cb, templates);
        addAndMakeVisible (w);
        w->setBounds (getLocalBounds());
        w->grabKeyboardFocus();
    }

private:

    void loadEdit (const File& file, bool createDemo)
    {
        closeEdit();
        editFile = file;

        StartupTimer::mark ("closed previous edit");
        if (file.existsAsFile())
            edit = te::loadEditFromFile (engine, file);
        else
            edit = te::createEmptyEdit (engine, file);
        StartupTimer::mark ("edit file read");

        edit->playInStopEnabled = true;
        DrumRack::migrateAll (*edit);   // racks from Pulse 0.17 and older
        Upgrade::run (*edit);            // editable returns, plain track names

        if (createDemo)
            createDemoContent();

        // Master meter (the mixer reads the master level from it).
        if (edit->getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>() == nullptr)
            edit->getMasterPluginList().insertPlugin (edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);
        if (edit->getMasterPluginList().findFirstPluginOfType<pulse::LoudnessProbe>() == nullptr)   // LUFS readouts
            edit->getMasterPluginList().insertPlugin (edit->getPluginCache().createNewPlugin (pulse::LoudnessProbe::xmlTypeName, {}), -1, nullptr);

        edit->getTransport().ensureContextAllocated();
        edit->getUndoManager().clearUndoHistory();

        StartupTimer::mark ("edit prepared (upgrades, meters, context)");
        evs = std::make_unique<EditViewState> (*edit, selectionManager);
        evs->openPianoRoll = [this] (te::MidiClip* mc) { pianoRoll->setClip (mc); showTab (Tab::pianoRoll); };
        evs->showDevices = [this] (te::Track* t)
        {
            if (t != nullptr) selectionManager.selectOnly (t);
            if (deviceChain) deviceChain->setShowMaster (t == nullptr);
            showTab (Tab::devices);
        };
        evs->isShowingDevicesOf = [this] (te::Track* t)
        {
            if (deviceChain == nullptr || currentTab != Tab::devices) return false;
            if (t == nullptr) return deviceChain->isShowingMaster();
            return ! deviceChain->isShowingMaster() && EngineHelpers::getSelectedTrack (selectionManager) == t;
        };
        evs->openDeviceEditor = [this] (te::Plugin& p)
        {
            if (auto sb = dynamic_cast<pulse::SandboxedPlugin*> (&p)) sb->showEditor();
            else if (auto pp = dynamic_cast<pulse::PulsePlugin*> (&p)) DeviceEditors::show (*evs, *pp);
            else p.showWindowExplicitly();
        };
        midiLearn = std::make_unique<MidiLearn> (*edit);
        linkSync = std::make_unique<LinkSync> (*edit);
        envelopePlayer = std::make_unique<ClipEnvelopes::Player> (*edit);
        if (settings().getBoolValue ("linkEnabled", false))
            linkSync->setEnabled (true);
        midiLearn->addChangeListener (this);
        if (push != nullptr) push->setView (evs.get());
        evs->midiLearn = midiLearn.get();

        transport.setEdit (edit.get());
        StartupTimer::mark ("helpers");
        createViews();

        if (auto first = EngineHelpers::getUserTracks (*edit).getFirst())
        {
            selectionManager.selectOnly (first);
            EngineHelpers::routeMidiInputsTo (*first);
        }

        showTab (currentTab);
        restoreViewState();
        CrashGuard::endLoading();   // the project's plugins are loaded
        settings().setValue ("lastEdit", file.getFullPathName());
        settings().setValue ("recentProjects", RecentProjects::push (StringArray::fromLines (settings().getValue ("recentProjects")), file).joinIntoString ("\n"));
        updateWindowTitle();
        resized();
        offerRecoveryIfNeeded (file);
    }

    /** The views of the open project (arrangement, session, browser, devices, mixer, piano roll). */
    void createViews()
    {
        arrangement = std::make_unique<ArrangementView> (*evs);
        StartupTimer::mark ("arrangement view");
        session     = std::make_unique<SessionView> (*evs);
        StartupTimer::mark ("session view");
        browser     = std::make_unique<Browser> (*evs, settings());
        StartupTimer::mark ("browser");
        deviceChain = std::make_unique<DeviceChain> (*evs);
        StartupTimer::mark ("device chain");
        mixer       = std::make_unique<Mixer> (*evs);
        StartupTimer::mark ("mixer");
        pianoRoll   = std::make_unique<PianoRoll> (*evs);
        pianoRoll->setClip (nullptr);
        showBrowser = settings().getBoolValue ("showBrowser", true);

        for (auto* c : std::initializer_list<Component*> { arrangement.get(), session.get(), browser.get(),
                                                           deviceChain.get(), mixer.get(), pianoRoll.get() })
            addAndMakeVisible (c);

        selectionManager.addChangeListener (this);
    }

public:
    /** Switches the colour theme live (and remembers it). */
    void setTheme (int index)
    {
        const auto before = Theme::tokens();
        Theme::apply (index);
        const auto after = Theme::tokens();
        auto& desk = Desktop::getInstance();
        for (int i = 0; i < desk.getNumComponents(); ++i)
            if (auto* c = desk.getComponent (i)) Theme::remapColours (*c, before, after);
        settings().setValue ("theme", String (Theme::all()[(size_t) Theme::currentIndex()].name));
        if (auto* laf = dynamic_cast<PulseLookAndFeel*> (&LookAndFeel::getDefaultLookAndFeel())) laf->applyTheme();
        rebuildViews();
        auto& d = Desktop::getInstance();
        for (int i = 0; i < d.getNumComponents(); ++i)
            if (auto* c = d.getComponent (i)) { c->sendLookAndFeelChange(); c->repaint(); }
    }

    /** Recreates the views (they take colours when they are built), keeping the open clip and tab. */
    void rebuildViews()
    {
        if (edit == nullptr || evs == nullptr) return;
        saveViewState();
        te::MidiClip::Ptr clipInRoll = pianoRoll != nullptr ? pianoRoll->getClip() : nullptr;
        DeviceEditors::closeAll();
        if (pianoRoll != nullptr) pianoRoll->setClip (nullptr);
        pianoRoll = nullptr;
        mixer = nullptr;
        deviceChain = nullptr;
        browser = nullptr;
        session = nullptr;
        arrangement = nullptr;
        createViews();
        if (clipInRoll != nullptr) pianoRoll->setClip (clipInRoll.get());
        showTab (currentTab);
        restoreViewState();
        resized();
        repaint();
    }

private:

    void closeEdit()
    {
        if (edit == nullptr) return;
        BackgroundRender::instance().cancelAndWait();
        selectionManager.removeChangeListener (this);
        selectionManager.deselectAll();
        edit->getTransport().stop (false, true);
        transport.setEdit (nullptr);
        DeviceEditors::closeAll();
        UndoHistory::closeAll();
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
        edit->tempoSequence.getTempo (0)->setBpm (122.0);
        auto bars = [this] (double b) { return EngineHelpers::beatToTime (*edit, b * 4.0); };
        auto beat = [] (double b) { return te::BeatPosition::fromBeats (b); };
        auto len = [] (double b) { return te::BeatDuration::fromBeats (b); };

        struct Def { const char* name; int colour; };
        const Def defs[] = { { "Drums", 0 }, { "Bass", 4 }, { "Chords", 1 }, { "Lead", 2 }, { "Pad", 6 } };
        edit->ensureNumberOfAudioTracks (5);
        auto tracks = EngineHelpers::getUserTracks (*edit);
        for (int i = 0; i < 5 && i < tracks.size(); ++i)
        {
            tracks[i]->setName (defs[i].name);
            tracks[i]->setColour (Theme::trackColour (defs[i].colour));
        }

        // Drums: synthesised kit, 4 bars of house groove
        if (auto rp = DrumRack::create (*edit, true))
        {
            tracks[0]->pluginList.insertPlugin (rp, 0, nullptr);
            if (auto mc = tracks[0]->insertMIDIClip ("Groove", { bars (0), bars (8) }, nullptr))
            {
                auto& sq = mc->getSequence();
                for (int bar = 0; bar < 4; ++bar)
                    for (int b = 0; b < 4; ++b)
                    {
                        const double t = bar * 4.0 + b;
                        sq.addNote (36, beat (t), len (0.25), 118, 0, nullptr);                    // kick
                        if (b % 2 == 1) sq.addNote (39, beat (t), len (0.25), 100, 0, nullptr);     // clap
                        sq.addNote (42, beat (t + 0.25), len (0.1), 70, 0, nullptr);                // hats
                        sq.addNote (46, beat (t + 0.5), len (0.2), 88, 0, nullptr);                 // open hat
                        sq.addNote (42, beat (t + 0.75), len (0.1), 60, 0, nullptr);
                    }
                mc->setLoopRangeBeats ({ beat (0), beat (16) });
            }
        }

        auto wavetable = [this] (te::AudioTrack& t, std::initializer_list<std::pair<int, float>> params) -> pulse::WavetableSynth*
        {
            auto p = edit->getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {});
            t.pluginList.insertPlugin (p, 0, nullptr);
            auto w = dynamic_cast<pulse::WavetableSynth*> (p.get());
            if (w != nullptr) for (auto [i, v] : params) w->set (i, v);
            return w;
        };
        using W = pulse::WavetableSynth;
        wavetable (*tracks[1], { { W::pVoices, 1.0f }, { W::pSub, 0.6f }, { W::pCutoff, 600.0f }, { W::pFilterEnv, 0.5f }, { W::pFD, 220.0f }, { W::pLevel, -4.0f } });
        wavetable (*tracks[2], { { W::pVoices, 3.0f }, { W::pCutoff, 2400.0f }, { W::pA, 8.0f }, { W::pR, 400.0f }, { W::pLevel, -10.0f } });
        wavetable (*tracks[3], { { W::pSet1, 4.0f }, { W::pVoices, 5.0f }, { W::pDetune, 0.35f }, { W::pCutoff, 4500.0f }, { W::pGlide, 40.0f }, { W::pLevel, -12.0f } });
        wavetable (*tracks[4], { { W::pSet1, 3.0f }, { W::pVoices, 7.0f }, { W::pA, 900.0f }, { W::pR, 2500.0f }, { W::pCutoff, 1800.0f }, { W::pLfoPos, 0.5f }, { W::pLevel, -16.0f } });

        // Bass: off-beat roots, Am - F - C - G
        if (auto mc = tracks[1]->insertMIDIClip ("Bassline", { bars (0), bars (8) }, nullptr))
        {
            const int roots[] = { 33, 29, 36, 31 };
            for (int bar = 0; bar < 4; ++bar)
                for (int b = 0; b < 4; ++b)
                    mc->getSequence().addNote (roots[bar], beat (bar * 4.0 + b + 0.5), len (0.4), 105, 0, nullptr);
            mc->setLoopRangeBeats ({ beat (0), beat (16) });
        }

        // Chords: stabs on the off-beats
        if (auto mc = tracks[2]->insertMIDIClip ("Stabs", { bars (0), bars (8) }, nullptr))
        {
            const int chords[4][3] = { { 57, 60, 64 }, { 57, 60, 65 }, { 55, 60, 64 }, { 55, 59, 62 } };
            for (int bar = 0; bar < 4; ++bar)
                for (double off : { 0.5, 1.5, 2.75, 3.5 })
                    for (int n : chords[bar])
                        mc->getSequence().addNote (n, beat (bar * 4.0 + off), len (0.3), 90, 0, nullptr);
            mc->setLoopRangeBeats ({ beat (0), beat (16) });
        }

        // Lead: arpeggio from bar 5
        if (auto mc = tracks[3]->insertMIDIClip ("Arp", { bars (4), bars (8) }, nullptr))
        {
            const int arp[] = { 69, 72, 76, 72, 65, 69, 72, 69, 67, 72, 76, 72, 67, 71, 74, 71 };
            for (int i = 0; i < 32; ++i)
                mc->getSequence().addNote (arp[i % 16] + (i >= 16 ? 12 : 0), beat (i * 0.5), len (0.3), 92, 0, nullptr);
        }

        // Pad: long chords
        if (auto mc = tracks[4]->insertMIDIClip ("Pad", { bars (2), bars (8) }, nullptr))
        {
            const int chords[4][4] = { { 57, 64, 69, 72 }, { 53, 60, 65, 69 }, { 55, 60, 64, 67 }, { 55, 62, 67, 71 } };
            for (int bar = 0; bar < 6; ++bar)
                for (int n : chords[(bar + 2) % 4])
                    mc->getSequence().addNote (n, beat (bar * 4.0), len (3.9), 70, 0, nullptr);
        }

        // Return buses: Reverb + Delay, fed by sends.
        if (auto rev = Routing::createReturnTrack (*edit, "Reverb", pulse::Reverb::xmlTypeName))
        {
            rev->setColour (Theme::trackColour (5));
            if (auto r = rev->pluginList.findFirstPluginOfType<pulse::Reverb>())
            {
                FactoryPresets::apply (*r, "Hall");
                r->set (pulse::Reverb::pMix, 1.0f);
            }
        }
        if (auto del = Routing::createReturnTrack (*edit, "Delay", pulse::Delay::xmlTypeName))
        {
            del->setColour (Theme::trackColour (3));
            if (auto d = del->pluginList.findFirstPluginOfType<pulse::Delay>())
            {
                FactoryPresets::apply (*d, "Dotted 8th");
                d->set (pulse::Delay::pMix, 1.0f);
            }
        }
        Routing::setSendDb (*tracks[2], 0, -12.0f);
        Routing::setSendDb (*tracks[3], 0, -12.0f);
        Routing::setSendDb (*tracks[3], 1, -9.0f);
        Routing::setSendDb (*tracks[4], 0, -6.0f);
        // gain staging: about -6 dB peaks on the master
        const float vols[] = { -9.0f, -8.0f, -10.0f, -10.0f, -8.0f };
        for (int i = 0; i < 5 && i < tracks.size(); ++i)
            if (auto vp = tracks[i]->getVolumePlugin()) vp->setVolumeDb (vols[i]);

        // Session view: the same parts as clips in scenes (Intro / Groove / Full / Breakdown)
        {
            auto& sl = edit->getSceneList();
            sl.ensureNumberOfScenes (8);
            const char* sceneNames[] = { "Intro", "Groove", "Full", "Breakdown" };
            for (int s = 0; s < 4; ++s)
                if (auto sc = sl.getScenes()[s]) sc->name = sceneNames[s];
            // which tracks play in each scene
            const bool plays[5][4] = { { true, true, true, false },     // Drums
                                       { false, true, true, false },    // Bass
                                       { false, true, true, true },     // Chords
                                       { false, false, true, true },    // Lead
                                       { true, false, true, true } };   // Pad
            for (int t = 0; t < 5 && t < tracks.size(); ++t)
            {
                auto src = dynamic_cast<te::MidiClip*> (tracks[t]->getClips().getFirst());
                if (src == nullptr) continue;
                tracks[t]->getClipSlotList().ensureNumberOfSlots (8);
                auto slots = tracks[t]->getClipSlotList().getClipSlots();
                const double lenBeats = 16.0;
                for (int s = 0; s < 4; ++s)
                {
                    if (! plays[t][s]) continue;
                    auto mc = te::insertMIDIClip (*slots[s], src->getName(), { te::TimePosition(), EngineHelpers::beatToTime (*edit, lenBeats) });
                    if (mc == nullptr) continue;
                    for (auto n : src->getSequence().getNotes())
                        if (n->getStartBeat().inBeats() < lenBeats)
                            mc->getSequence().addNote (n->getNoteNumber(), n->getStartBeat(), n->getLengthBeats(), n->getVelocity(), 0, nullptr);
                    mc->setLoopRangeBeats ({ te::BeatPosition(), te::BeatPosition::fromBeats (lenBeats) });
                }
            }
        }

        // song sections over the demo
        Sections::add (*edit, "Intro", 0.0, 8.0);
        Sections::add (*edit, "Build", 8.0, 8.0);
        Sections::add (*edit, "Drop", 16.0, 16.0);

        auto& tc = edit->getTransport();
        tc.setLoopRange ({ bars (0), bars (8) });
        tc.looping = true;
        te::EditFileOperations (*edit).save (true, true, false);

    }

    //==============================================================================
public:
    /** --snapshots: opens a fresh demo, plays it and saves a PNG of each main view. */
    void prepareSnapshot (int step)
    {
        auto tracks = EngineHelpers::getUserTracks (*edit);
        auto selectTrack = [&] (int i) { if (auto t = tracks[i]) selectionManager.selectOnly (t); };
        switch (step)
        {
            case 0: showSession = false; selectTrack (0); showTab (Tab::devices); break;
            case 1: showSession = false; showTab (Tab::mixer); break;
            case 2: showSession = true;  showTab (Tab::mixer); break;
            case 3: showSession = false; selectTrack (2);
                    if (auto mc = dynamic_cast<te::MidiClip*> (tracks[2]->getClips().getFirst())) { pianoRoll->setClip (mc); }
                    showTab (Tab::pianoRoll); break;
            case 4: showSession = false; selectTrack (3); showTab (Tab::devices); break;
            case 5: showSession = false; selectTrack (0);
                    if (auto mc = dynamic_cast<te::MidiClip*> (tracks[0]->getClips().getFirst())) { pianoRoll->setClip (mc); }
                    showTab (Tab::pianoRoll); break;
            case 6: showTab (Tab::devices); showWelcome(); break;
            case 7: removeWelcome(); themeBefore = Theme::currentIndex(); setTheme (Theme::indexOf ("Daylight")); selectTrack (3); showTab (Tab::devices); break;
            case 8: setTheme (Theme::indexOf ("Synthwave")); showTab (Tab::mixer); break;
            case 9: setTheme (Theme::indexOf ("Nord")); showTab (Tab::devices); break;
            case 10: setTheme (Theme::indexOf ("Paper")); showTab (Tab::mixer); break;
            case 11: setTheme (themeBefore); break;
            default: break;
        }
        resized();
    }

    int themeBefore = 0;
    void removeWelcome()
    {
        for (int i = getNumChildComponents(); --i >= 0;)
            if (auto* w = dynamic_cast<WelcomeScreen*> (getChildComponent (i))) delete w;
    }

    void saveSnapshot (const File& f)
    {
        auto img = createComponentSnapshot (getLocalBounds());
        f.deleteFile();
        if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
    }

    void startSnapshotDemo (const File& dir)
    {
        dir.createDirectory();
        auto f = dir.getChildFile ("Snapshot Demo.tracktionedit");
        f.deleteFile();
        loadEdit (f, true);
        edit->getTransport().play (false);
    }

private:
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
            const bool changed = edit->hasChangedSinceSaved();
            ops.save (true, true, false);
            if (! edit->hasChangedSinceSaved())
                ops.deleteTempVersion();
            if (changed) Backups::write (*edit);
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
        auto factory = FactoryContent::templates (engine);
        if (! factory.isEmpty())
        {
            m.addSeparator();
            m.addSectionHeader ("Factory templates");
            for (auto& t : factory)
                m.addItem (t.getFileNameWithoutExtension(), [this, t] { createProject (t, false); });
        }
        auto templates = Sharing::templates();
        if (! templates.isEmpty())
        {
            m.addSeparator();
            m.addSectionHeader ("Your templates");
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
        showAudioSettingsDialog (engine, *edit, settings(), [sp = SafePointer<MainComponent> (this)] (int i) { if (sp != nullptr) sp->setTheme (i); }, false, commands.get());
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
public:
    BackgroundPluginScan pluginScan { engine };
private:
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
