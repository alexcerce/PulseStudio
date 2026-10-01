#pragma once

#include "EditViewState.h"

/** Top bar: transport, tempo, position display and app actions. */
class TransportBar : public Component,
                     private Timer
{
public:
    struct Actions
    {
        std::function<void()> newProject, openProject, saveProject, saveProjectAs, exportWav,
                              audioSettings, pluginManager, addInstrumentTrack, addAudioTrack,
                              toggleBrowser, toggleSession;
        std::function<bool()> isBrowserShown, isSessionShown;
        std::function<String()> controllerStatus;
    };

    TransportBar (te::Engine& eng, Actions a) : engine (eng), actions (std::move (a))
    {
        auto setupToggle = [this] (TextButton& b, Colour on, const String& tip)
        {
            b.setColour (TextButton::buttonOnColourId, on);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setupToggle (stopBtn, Theme::accent, "Stop (press again to return to start)");
        setupToggle (playBtn, Theme::green, "Play / Stop (Space)");
        setupToggle (recBtn, Theme::red, "Record armed tracks (R)");
        setupToggle (loopBtn, Theme::orange, "Loop (L) - drag on the ruler to set the range");
        setupToggle (clickBtn, Theme::accent, "Metronome (M)");
        recBtn.setColour (TextButton::textColourOffId, Theme::red);

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
            l->setFont (FontOptions (Font::getDefaultMonospacedFontName(), 17.0f, Font::plain));
            l->setColour (Label::textColourId, Colour (0xff8ce99a));
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
            l->setFont (FontOptions (9.5f));
            l->setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (l);
        }

        auto addAction = [this] (TextButton& b, std::function<void()>& fn, const String& tip)
        {
            b.onClick = [&fn] { if (fn) fn(); };
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        addAction (instTrackBtn, actions.addInstrumentTrack, "Add an instrument (MIDI) track");
        addAction (audioTrackBtn, actions.addAudioTrack, "Add an audio track");
        addAction (audioBtn, actions.audioSettings, "Audio interface, ASIO driver, buffer size, MIDI inputs");
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
            add ("Save", "Ctrl+S", actions.saveProject);
            m.addSeparator();
            add ("Export audio / stems...", {}, actions.exportWav);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&fileBtn));
        };
        addAndMakeVisible (fileBtn);

        addAction (browserBtn, actions.toggleBrowser, "Show / hide the browser (B)");
        addAction (viewBtn, actions.toggleSession, "Switch between Arrangement and Session view (Tab)");
        browserBtn.setColour (TextButton::buttonOnColourId, Colour (0xff2b3a4d));
        browserBtn.setColour (TextButton::textColourOnId, Theme::text);

        startTimerHz (20);
    }

    void setEdit (te::Edit* e) { edit = e; }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff16181d));
        g.setColour (Theme::line);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.setColour (Colour (0xff0b0d10));
        g.fillRoundedRectangle (lcdArea.toFloat(), 6.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (lcdArea.toFloat(), 6.0f, 1.0f);
        g.setColour (Theme::accent);
        g.setFont (FontOptions (16.0f, Font::bold));
        g.drawText ("PULSE", 12, 0, 70, getHeight(), Justification::centredLeft);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.0f, Font::bold));
        g.drawText ("STUDIO", 70, 2, 50, getHeight(), Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8);
        r.removeFromLeft (115);

        for (auto* b : { &stopBtn, &playBtn, &recBtn, &loopBtn, &clickBtn })
        {
            b->setBounds (r.removeFromLeft (38));
            r.removeFromLeft (4);
        }
        r.removeFromLeft (10);

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
        r.removeFromLeft (12);

        undoBtn.setBounds (r.removeFromLeft (46)); r.removeFromLeft (4);
        redoBtn.setBounds (r.removeFromLeft (46)); r.removeFromLeft (14);
        instTrackBtn.setBounds (r.removeFromLeft (70)); r.removeFromLeft (4);
        audioTrackBtn.setBounds (r.removeFromLeft (70)); r.removeFromLeft (14);
        browserBtn.setBounds (r.removeFromLeft (70)); r.removeFromLeft (4);
        viewBtn.setBounds (r.removeFromLeft (80)); r.removeFromLeft (8);

        cpuLabel.setBounds (r.removeFromRight (64));
        r.removeFromRight (6);
        pluginsBtn.setBounds (r.removeFromRight (70)); r.removeFromRight (4);
        audioBtn.setBounds (r.removeFromRight (112));  r.removeFromRight (4);
        fileBtn.setBounds (r.removeFromRight (60));
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.eventComponent == &bpmLabel && edit != nullptr)
        {
            if (! e.mouseWasDraggedSinceMouseDown()) return;
            if (dragStartBpm < 0) dragStartBpm = edit->tempoSequence.getTempo (0)->getBpm();
            setBpm (std::round ((dragStartBpm - e.getDistanceFromDragStartY() * 0.25) * 10.0) / 10.0);
        }
    }

    void mouseUp (const MouseEvent&) override { dragStartBpm = -1.0; }

private:
    void setBpm (double bpm)
    {
        if (edit != nullptr && bpm >= 20.0 && bpm <= 400.0)
            edit->tempoSequence.getTempo (0)->setBpm (bpm);
    }

    void timerCallback() override
    {
        if (edit == nullptr) return;
        auto& tc = edit->getTransport();
        auto pos = tc.getPosition();
        barsLabel.setText (EngineHelpers::formatBarsBeats (*edit, pos), dontSendNotification);
        timeLabel.setText (EngineHelpers::formatTime (pos), dontSendNotification);

        if (! bpmLabel.isBeingEdited())
            bpmLabel.setText (String (edit->tempoSequence.getTempo (0)->getBpm(), 1), dontSendNotification);

        if (! sigLabel.isBeingEdited())
        {
            auto* ts = edit->tempoSequence.getTimeSig (0);
            sigLabel.setText (String (ts->numerator.get()) + "/" + String (ts->denominator.get()), dontSendNotification);
        }

        playBtn.setToggleState (tc.isPlaying(), dontSendNotification);
        playBtn.setButtonText (tc.isPlaying() ? "||" : String::charToString (0x25B6));
        recBtn.setToggleState (tc.isRecording(), dontSendNotification);
        loopBtn.setToggleState (tc.looping.get(), dontSendNotification);
        clickBtn.setToggleState (edit->clickTrackEnabled.get(), dontSendNotification);
        if (actions.isBrowserShown) browserBtn.setToggleState (actions.isBrowserShown(), dontSendNotification);
        if (actions.isSessionShown) viewBtn.setButtonText (actions.isSessionShown() ? "Arrange" : "Session");
        undoBtn.setEnabled (edit->getUndoManager().canUndo());
        redoBtn.setEnabled (edit->getUndoManager().canRedo());

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
    Rectangle<int> lcdArea;
    double dragStartBpm = -1.0;

    TextButton stopBtn { String::charToString (0x25A0) }, playBtn { String::charToString (0x25B6) },
               recBtn { String::charToString (0x25CF) }, loopBtn { "Loop" }, clickBtn { "Click" },
               undoBtn { "Undo" }, redoBtn { "Redo" },
               instTrackBtn { "+ Inst" }, audioTrackBtn { "+ Audio" },
               fileBtn { "File" }, audioBtn { "Audio Settings" }, pluginsBtn { "Plugins" },
               browserBtn { "Browser" }, viewBtn { "Session" };
    Label barsLabel, timeLabel, bpmLabel, sigLabel,
          barsCap { {}, "BAR.BEAT.16" }, timeCap { {}, "TIME" }, bpmCap { {}, "BPM" }, sigCap { {}, "METER" }, cpuLabel;
};
