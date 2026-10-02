#pragma once

#include "EditViewState.h"
#include "Icons.h"
#include "Meters.h"

/** Top bar: transport, tempo, position display and app actions. */
class TransportBar : public Component,
                     private Timer
{
public:
    struct Actions
    {
        std::function<void()> newProject, openProject, saveProject, saveProjectAs, exportWav, exportSelection, exportMidi,
                              audioSettings, pluginManager, addInstrumentTrack, addAudioTrack,
                              toggleBrowser, toggleSession, collectAll, saveTemplate, openPalette, editShortcuts;
        std::function<bool()> isBrowserShown, isSessionShown;
        std::function<StringArray()> recentProjects;
        std::function<void (const File&)> openRecent;
        std::function<void()> clearRecent;
        std::function<String()> controllerStatus;
        std::function<void()> toggleLink, cancelRender;
        std::function<float()> renderProgress;     // < 0 when idle
        std::function<String()> renderStatus;
        std::function<bool()> isLinkEnabled;
        std::function<String()> linkStatus;
    };

    TransportBar (te::Engine& eng, Actions a) : engine (eng), actions (std::move (a))
    {
        auto setupToggle = [this] (IconButton& b, Colour on, const String& tip)
        {
            b.setOnColour (on);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setupToggle (stopBtn, Theme::accent, "Stop (press again to return to start)");
        setupToggle (playBtn, Theme::green, "Play / Stop (Space)");
        setupToggle (recBtn, Theme::red, "Record armed tracks (R)");
        setupToggle (loopBtn, Theme::orange, "Loop (L) - drag on the ruler to set the range");
        setupToggle (clickBtn, Theme::accent, "Metronome (M)");
        setupToggle (linkBtn, Theme::green, "Ableton Link: sync tempo, bars and start/stop with other apps and devices on your network");
        linkBtn.onClick = [this] { if (actions.toggleLink) actions.toggleLink(); };
        recBtn.setIconColour (Theme::red);
        linkBtn.setText ("Link");

        stopBtn.onClick = [this]
        {
            auto& tc = edit->getTransport();
            if (tc.isPlaying()) tc.stop (false, false);
            else tc.setPosition (0s);
        };
        playBtn.onClick = [this] { EngineHelpers::togglePlay (*edit); };
        recBtn.onClick  = [this] { EngineHelpers::toggleRecord (*edit); };
        loopBtn.onClick = [this] { auto& tc = edit->getTransport(); tc.looping = ! tc.looping.get(); };
        clickBtn.onClick = [this] { edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); };

        undoBtn.onClick = [this] { edit->undo(); };
        redoBtn.onClick = [this] { edit->redo(); };
        undoBtn.setTooltip ("Undo (Ctrl+Z)");
        redoBtn.setTooltip ("Redo (Ctrl+Y)");

        for (auto* l : { &barsLabel, &timeLabel, &bpmLabel, &sigLabel })
        {
            l->setJustificationType (Justification::centred);
            l->setFont (FontOptions ("Consolas", 18.0f, Font::plain));
            l->setColour (Label::textColourId, Colour (0xff9be7b0));
            addAndMakeVisible (l);
        }
        bpmLabel.setEditable (false, true);
        bpmLabel.setTooltip ("Tempo - double-click to type, drag up/down to change");
        bpmLabel.onTextChange = [this] { setBpm (bpmLabel.getText().getDoubleValue()); };
        bpmLabel.addMouseListener (this, false);
        sigLabel.setEditable (false, true);
        sigLabel.setTooltip ("Time signature - double-click to edit (e.g. 3/4)");
        sigLabel.onTextChange = [this]
        {
            edit->tempoSequence.getTimeSig (0)->setStringTimeSig (sigLabel.getText());
        };

        for (auto* l : { &barsCap, &timeCap, &bpmCap, &sigCap, &cpuLabel })
        {
            l->setJustificationType (Justification::centred);
            l->setFont (FontOptions (10.5f));
            l->setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (l);
        }

        auto addAction = [this] (Button& b, std::function<void()>& fn, const String& tip)
        {
            b.onClick = [&fn] { if (fn) fn(); };
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        instTrackBtn.setText ("Inst");   audioTrackBtn.setText ("Audio");
        fileBtn.setText ("File");        audioBtn.setText ("Audio");    pluginsBtn.setText ("Plugins");
        browserBtn.setText ("Browser");  viewBtn.setText ("Session");
        addAction (instTrackBtn, actions.addInstrumentTrack, "Add an instrument (MIDI) track");
        addAction (audioTrackBtn, actions.addAudioTrack, "Add an audio track");
        addAction (audioBtn, actions.audioSettings, "Audio settings: interface, ASIO driver, buffer size, MIDI inputs, recording, safety");
        addAction (pluginsBtn, actions.pluginManager, "Scan and manage VST3 plugins");
        addAndMakeVisible (undoBtn);
        addAndMakeVisible (redoBtn);

        fileBtn.setTooltip ("New, open, save and export");
        fileBtn.onClick = [this]
        {
            PopupMenu m;
            auto add = [&m] (const String& name, const String& key, std::function<void()>& fn)
            {
                PopupMenu::Item item (name);
                item.shortcutKeyDescription = key;
                item.action = [&fn] { if (fn) fn(); };
                m.addItem (item);
            };
            add ("New Project...", {}, actions.newProject);
            add ("Open Project...", {}, actions.openProject);
            {
                PopupMenu recent;
                auto list = actions.recentProjects ? actions.recentProjects() : StringArray();
                for (auto& path : list)
                {
                    const File f (path);
                    recent.addItem (f.getFileNameWithoutExtension() + "   (" + f.getParentDirectory().getFileName() + ")", f.existsAsFile(), false,
                                    [this, f] { if (actions.openRecent) actions.openRecent (f); });
                }
                if (! list.isEmpty())
                {
                    recent.addSeparator();
                    recent.addItem ("Clear list", [this] { if (actions.clearRecent) actions.clearRecent(); });
                }
                m.addSubMenu ("Open recent", recent, ! list.isEmpty());
            }
            add ("Save", "Ctrl+S", actions.saveProject);
            add ("Collect all and save", {}, actions.collectAll);
            add ("Save as template...", {}, actions.saveTemplate);
            m.addSeparator();
            add ("Export audio / stems...", "Ctrl+Shift+R", actions.exportWav);
            add ("Export selection...", "Ctrl+Shift+E", actions.exportSelection);
            add ("Export MIDI...", "Ctrl+Shift+M", actions.exportMidi);
            m.addSeparator();
            add ("Command palette...", "Ctrl+P", actions.openPalette);
            add ("Keyboard shortcuts...", {}, actions.editShortcuts);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&fileBtn));
        };
        addAndMakeVisible (fileBtn);
        addChildComponent (cancelRenderBtn);
        cancelRenderBtn.setFlat (true);
        cancelRenderBtn.onClick = [this] { if (actions.cancelRender) actions.cancelRender(); };

        addAction (browserBtn, actions.toggleBrowser, "Show / hide the browser (B)");
        addAction (viewBtn, actions.toggleSession, "Switch between Arrangement and Session view (Tab)");
        browserBtn.setOnColour (Colour (0xff2f4f73));
        for (auto* b : { &instTrackBtn, &audioTrackBtn })            b->setIconColour (Theme::accent);
        for (auto* b : { &undoBtn, &redoBtn })                       b->setFlat (true);

        startTimerHz (20);
    }

    void setEdit (te::Edit* e)
    {
        edit = e;
        masterMeter.reset();
        meterPlugin = nullptr;
        resized();
    }

    /** Follows the master meter plugin (created / replaced after the edit loads). */
    void updateMasterMeter()
    {
        te::LevelMeterPlugin* m = edit != nullptr ? edit->getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>() : nullptr;
        if (m == meterPlugin && (masterMeter != nullptr) == (m != nullptr)) return;
        meterPlugin = m;
        masterMeter.reset();
        if (m != nullptr)
        {
            masterMeter = std::make_unique<LevelMeter> (*m, false);
            masterMeter->setTooltip ("Master output level (click to reset the clip light)");
            addAndMakeVisible (*masterMeter);
        }
        resized();
    }

    void paint (Graphics& g) override
    {
        g.setGradientFill (ColourGradient (Colour (0xff1c2027), 0, 0, Colour (0xff14171b), 0, (float) getHeight(), false));
        g.fillAll();
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // logo: a pulse waveform mark + wordmark
        Path mark;
        const float my = (float) getHeight() * 0.5f;
        mark.startNewSubPath (12.0f, my); mark.lineTo (18.0f, my); mark.lineTo (21.0f, my - 9.0f);
        mark.lineTo (25.0f, my + 9.0f); mark.lineTo (28.0f, my - 4.0f); mark.lineTo (30.0f, my); mark.lineTo (34.0f, my);
        g.setColour (Theme::accent.withAlpha (0.3f));
        g.strokePath (mark, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Theme::accent);
        g.strokePath (mark, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Theme::text);
        g.setFont (FontOptions (16.0f, Font::bold));
        g.drawText ("Pulse", 40, 0, 60, getHeight(), Justification::centredLeft);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (12.0f));
        g.drawText ("Studio", 86, 1, 50, getHeight(), Justification::centredLeft);

        if (renderShown)
        {
            auto pill = renderArea.toFloat().withRight ((float) cancelRenderBtn.getRight());
            g.setColour (Colour (0xff0d0f12));
            g.fillRoundedRectangle (pill, 6.0f);
            g.setColour (Theme::accent.withAlpha (0.6f));
            g.fillRoundedRectangle (pill.withWidth (pill.getWidth() * jlimit (0.0f, 1.0f, renderFraction)), 6.0f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (12.0f));
            g.drawText (renderText + "  " + String (roundToInt (renderFraction * 100)) + "%", pill.reduced (8.0f, 0.0f).withTrimmedRight (20.0f),
                        Justification::centredLeft, true);
        }

        auto lcd = lcdArea.toFloat();
        g.setGradientFill (ColourGradient (Colour (0xff07090b), 0, lcd.getY(), Colour (0xff0d1013), 0, lcd.getBottom(), false));
        g.fillRoundedRectangle (lcd, 7.0f);
        g.setColour (Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (lcd.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (Colours::white.withAlpha (0.05f));
        for (auto* l : { &timeLabel, &bpmLabel, &sigLabel })
            g.drawVerticalLine (l->getX() - 1, lcd.getY() + 6.0f, lcd.getBottom() - 6.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        r.removeFromLeft (122);

        for (auto* b : { &stopBtn, &playBtn, &recBtn, &loopBtn, &clickBtn })
        {
            b->setBounds (r.removeFromLeft (36));
            r.removeFromLeft (4);
        }
        linkBtn.setBounds (r.removeFromLeft (58));
        r.removeFromLeft (8);

        lcdArea = r.removeFromLeft (322);
        auto lcd = lcdArea.reduced (4, 2);
        auto cell = [&lcd] (Label& v, Label& c, int w)
        {
            auto a = lcd.removeFromLeft (w);
            c.setBounds (a.removeFromBottom (12));
            v.setBounds (a);
        };
        cell (barsLabel, barsCap, 82);
        cell (timeLabel, timeCap, 100);
        cell (bpmLabel, bpmCap, 72);
        cell (sigLabel, sigCap, 60);
        r.removeFromLeft (8);

        undoBtn.setBounds (r.removeFromLeft (32)); r.removeFromLeft (2);
        redoBtn.setBounds (r.removeFromLeft (32)); r.removeFromLeft (8);
        instTrackBtn.setBounds (r.removeFromLeft (60)); r.removeFromLeft (4);
        audioTrackBtn.setBounds (r.removeFromLeft (68)); r.removeFromLeft (8);
        browserBtn.setBounds (r.removeFromLeft (80)); r.removeFromLeft (4);
        viewBtn.setBounds (r.removeFromLeft (84)); r.removeFromLeft (8);

        if (masterMeter != nullptr)
        {
            auto m = r.removeFromRight (120);
            masterMeter->setBounds (m.withSizeKeepingCentre (m.getWidth(), 14));
            r.removeFromRight (6);
        }
        cpuLabel.setBounds (r.removeFromRight (62));
        renderArea = cpuLabel.getBounds().withX (cpuLabel.getX() - 170).withWidth (166).reduced (0, 6);
        cancelRenderBtn.setBounds (renderArea.removeFromRight (22));
        r.removeFromRight (6);
        pluginsBtn.setBounds (r.removeFromRight (80)); r.removeFromRight (4);
        audioBtn.setBounds (r.removeFromRight (72));  r.removeFromRight (4);
        fileBtn.setBounds (r.removeFromRight (70));
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.eventComponent == &bpmLabel && edit != nullptr)
        {
            if (! e.mouseWasDraggedSinceMouseDown()) return;
            if (dragStartBpm < 0) dragStartBpm = edit->tempoSequence.getTempoAt (edit->getTransport().getPosition()).getBpm();
            setBpm (std::round ((dragStartBpm - e.getDistanceFromDragStartY() * 0.25) * 10.0) / 10.0);
        }
    }

    void mouseUp (const MouseEvent&) override { dragStartBpm = -1.0; }

private:
    void setBpm (double bpm)
    {
        if (edit != nullptr && bpm >= 20.0 && bpm <= 400.0)
            edit->tempoSequence.getTempoAt (edit->getTransport().getPosition()).setBpm (bpm);
    }

    void timerCallback() override
    {
        if (edit == nullptr) return;
        if (++meterCheck % 20 == 0) updateMasterMeter();
        auto& tc = edit->getTransport();
        auto pos = tc.getPosition();
        barsLabel.setText (EngineHelpers::formatBarsBeats (*edit, pos), dontSendNotification);
        timeLabel.setText (EngineHelpers::formatTime (pos), dontSendNotification);

        if (! bpmLabel.isBeingEdited())
            bpmLabel.setText (String (edit->tempoSequence.getTempoAt (pos).getBpm(), 1), dontSendNotification);

        if (! sigLabel.isBeingEdited())
        {
            auto* ts = &edit->tempoSequence.getTimeSigAt (pos);
            sigLabel.setText (String (ts->numerator.get()) + "/" + String (ts->denominator.get()), dontSendNotification);
        }

        playBtn.setToggleState (tc.isPlaying(), dontSendNotification);
        playBtn.setIcon (tc.isPlaying() ? Icons::Type::pause : Icons::Type::play);
        recBtn.setToggleState (tc.isRecording(), dontSendNotification);
        recBtn.setIconColour (tc.isRecording() ? Colours::black.withAlpha (0.85f) : Theme::red);
        loopBtn.setToggleState (tc.looping.get(), dontSendNotification);
        clickBtn.setToggleState (edit->clickTrackEnabled.get(), dontSendNotification);
        if (actions.isLinkEnabled)
        {
            linkBtn.setToggleState (actions.isLinkEnabled(), dontSendNotification);
            auto ls = actions.linkStatus ? actions.linkStatus() : String();
            linkBtn.setTooltip (ls.isNotEmpty() ? ls + " (click to turn Link off)" : "Ableton Link: sync tempo, bars and start/stop with other apps and devices on your network");
        }
        if (actions.isBrowserShown) browserBtn.setToggleState (actions.isBrowserShown(), dontSendNotification);
        if (actions.isSessionShown)
        {
            const bool session = actions.isSessionShown();
            viewBtn.setText (session ? "Arrange" : "Session");
            viewBtn.setIcon (session ? Icons::Type::arrange : Icons::Type::session);
        }
        auto& um = edit->getUndoManager();
        undoBtn.setEnabled (um.canUndo());
        redoBtn.setEnabled (um.canRedo());
        undoBtn.setTooltip (um.canUndo() ? "Undo " + um.getUndoDescription() + " (Ctrl+Z)" : String ("Nothing to undo"));
        redoBtn.setTooltip (um.canRedo() ? "Redo " + um.getRedoDescription() + " (Ctrl+Y)" : String ("Nothing to redo"));

        // background export progress
        const float rp = actions.renderProgress ? actions.renderProgress() : -1.0f;
        const bool shown = rp >= 0.0f;
        if (shown != renderShown || (shown && (std::abs (rp - renderFraction) > 0.004f)))
        {
            renderShown = shown;
            renderFraction = jmax (0.0f, rp);
            renderText = actions.renderStatus ? actions.renderStatus() : String();
            cancelRenderBtn.setVisible (shown);
            repaint();
        }
        playBtn.setEnabled (! shown);
        recBtn.setEnabled (! shown);

        auto cpu = engine.getDeviceManager().getCpuUsage();
        cpuLabel.setText ("CPU " + String (roundToInt (cpu * 100.0)) + "%", dontSendNotification);

        auto status = actions.controllerStatus ? actions.controllerStatus() : String();
        cpuLabel.setTooltip (status.isNotEmpty() ? status : "No Push 2 connected");
        if (status.isNotEmpty())
            cpuLabel.setText ("PUSH  CPU " + String (roundToInt (cpu * 100.0)) + "%", dontSendNotification);
    }

    te::Engine& engine;
    te::Edit* edit = nullptr;
    Actions actions;
    Rectangle<int> lcdArea, renderArea;
    IconButton cancelRenderBtn { Icons::Type::close, "Cancel the export" };
    bool renderShown = false;
    float renderFraction = 0.0f;
    String renderText;
    double dragStartBpm = -1.0;

    IconButton stopBtn { Icons::Type::stop }, playBtn { Icons::Type::play }, recBtn { Icons::Type::record },
               loopBtn { Icons::Type::loop }, clickBtn { Icons::Type::metronome }, linkBtn { Icons::Type::link },
               undoBtn { Icons::Type::undo }, redoBtn { Icons::Type::redo },
               instTrackBtn { Icons::Type::plus }, audioTrackBtn { Icons::Type::plus },
               fileBtn { Icons::Type::folder }, audioBtn { Icons::Type::gear }, pluginsBtn { Icons::Type::devices },
               browserBtn { Icons::Type::browser }, viewBtn { Icons::Type::session };
    Label barsLabel, timeLabel, bpmLabel, sigLabel,
          barsCap { {}, "BAR.BEAT.16" }, timeCap { {}, "TIME" }, bpmCap { {}, "BPM" }, sigCap { {}, "METER" }, cpuLabel;
public:
    std::unique_ptr<LevelMeter> masterMeter;
    te::LevelMeterPlugin* meterPlugin = nullptr;
    int meterCheck = 0;
};
