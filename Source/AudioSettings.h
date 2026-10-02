#pragma once

#include "Common.h"
#include "CrashGuard.h"

/** Recording options: count-in, MIDI overdub and per-input latency compensation. */
class RecordingSettings : public Component
{
public:
    RecordingSettings (te::Engine& eng, te::Edit& e, PropertiesFile& props) : engine (eng), edit (e), settings (props)
    {
        countInL.setText ("Count-in", dontSendNotification);
        countIn.addItemList ({ "None", "1 bar", "2 bars", "1 beat", "2 beats" }, 1);
        const te::Edit::CountIn modes[] = { te::Edit::CountIn::none, te::Edit::CountIn::oneBar, te::Edit::CountIn::twoBar,
                                            te::Edit::CountIn::oneBeat, te::Edit::CountIn::twoBeat };
        for (int i = 0; i < 5; ++i)
            if (edit.getCountInMode() == modes[i])
                countIn.setSelectedId (i + 1, dontSendNotification);
        countIn.onChange = [this, modes] { edit.setCountInMode (modes[countIn.getSelectedId() - 1]); };
        addAndMakeVisible (countInL);
        addAndMakeVisible (countIn);

        overdub.setButtonText ("MIDI overdub (record new notes into the existing clip instead of replacing it)");
        overdub.setToggleState (settings.getBoolValue ("midiOverdub", true), dontSendNotification);
        overdub.onClick = [this]
        {
            settings.setValue ("midiOverdub", overdub.getToggleState());
            applyOverdub (engine, overdub.getToggleState());
        };
        addAndMakeVisible (overdub);

        latencyTitle.setText ("Recording offset per input (ms) - shift recorded audio to line up with playback.\n"
                              "Negative moves recordings earlier. Measure with a loopback cable if timing is off.",
                              dontSendNotification);
        latencyTitle.setColour (Label::textColourId, Theme::muted);
        latencyTitle.setJustificationType (Justification::topLeft);
        addAndMakeVisible (latencyTitle);

        auto& dm = engine.getDeviceManager();
        for (int i = 0; i < dm.getNumWaveInDevices(); ++i)
            if (auto wip = dm.getWaveInDevice (i))
            {
                auto* row = rows.add (new Row());
                row->device = wip;
                row->name.setText (wip->getName(), dontSendNotification);
                row->slider.setSliderStyle (Slider::LinearHorizontal);
                row->slider.setTextBoxStyle (Slider::TextBoxRight, false, 60, 20);
                row->slider.setRange (-100.0, 100.0, 0.1);
                row->slider.setValue (wip->getRecordAdjustmentMs(), dontSendNotification);
                row->slider.setDoubleClickReturnValue (true, 0.0);
                row->slider.onValueChange = [row] { row->device->setRecordAdjustmentMs (row->slider.getValue()); };
                addAndMakeVisible (row->name);
                addAndMakeVisible (row->slider);
            }

        if (rows.isEmpty())
        {
            latencyTitle.setText ("No audio inputs enabled. Enable input channels on the Audio tab first.", dontSendNotification);
        }
    }

    /** Applies the overdub preference to every MIDI input (also called at startup). */
    static void applyOverdub (te::Engine& engine, bool overdubOn)
    {
        for (auto& d : engine.getDeviceManager().getMidiInDevices())
        {
            d->mergeRecordings = overdubOn;
            d->replaceExistingClips = ! overdubOn;
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto row = r.removeFromTop (28);
        countInL.setBounds (row.removeFromLeft (100));
        countIn.setBounds (row.removeFromLeft (140));
        r.removeFromTop (10);
        overdub.setBounds (r.removeFromTop (26));
        r.removeFromTop (14);
        latencyTitle.setBounds (r.removeFromTop (44));
        for (auto* rw : rows)
        {
            auto a = r.removeFromTop (26);
            rw->name.setBounds (a.removeFromLeft (200));
            rw->slider.setBounds (a);
            r.removeFromTop (4);
        }
    }

private:
    struct Row
    {
        te::WaveInputDevice* device = nullptr;
        Label name;
        Slider slider;
    };

    te::Engine& engine;
    te::Edit& edit;
    PropertiesFile& settings;
    Label countInL, latencyTitle;
    ComboBox countIn;
    ToggleButton overdub;
    OwnedArray<Row> rows;
};

/** Safety tab: plugins disabled after crashes, crash logs, safe mode. */
class SafetySettings : public Component,
                       private ListBoxModel
{
public:
    SafetySettings()
    {
        title.setText ("Plugins disabled after a crash are not loaded (their settings stay in your projects).", dontSendNotification);
        title.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (title);
        list.setModel (this);
        list.setRowHeight (24);
        list.setColour (ListBox::backgroundColourId, Theme::panel);
        addAndMakeVisible (list);

        enableBtn.onClick = [this]
        {
            if (auto row = list.getSelectedRow(); row >= 0 && row < items.size())
                CrashGuard::setDisabled (items[row], false);
            refresh();
        };
        logsBtn.onClick = [] { CrashGuard::crashFolder().revealToUser(); };
        sandboxToggle.onClick = [this] { if (onSandboxChanged) onSandboxChanged (sandboxToggle.getToggleState()); };
        safeInfo.setText (CrashGuard::safeMode() ? "Running in SAFE MODE: no plugins are loaded this session. Restart normally to load them."
                                                 : "Safe mode: hold Shift while starting Pulse Studio to start without loading any plugins.",
                          dontSendNotification);
        safeInfo.setColour (Label::textColourId, CrashGuard::safeMode() ? Theme::orange : Theme::muted);
        for (auto* c : std::initializer_list<Component*> { &enableBtn, &logsBtn, &safeInfo, &sandboxToggle })
            addAndMakeVisible (c);
        refresh();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (24));
        safeInfo.setBounds (r.removeFromBottom (40));
        sandboxToggle.setBounds (r.removeFromBottom (28));
        r.removeFromBottom (6);
        auto buttons = r.removeFromBottom (32);
        enableBtn.setBounds (buttons.removeFromLeft (160).reduced (0, 2));
        buttons.removeFromLeft (8);
        logsBtn.setBounds (buttons.removeFromLeft (160).reduced (0, 2));
        r.removeFromBottom (8);
        list.setBounds (r);
    }

private:
    void refresh()
    {
        items = CrashGuard::disabledPlugins();
        list.updateContent();
        list.repaint();
        enableBtn.setEnabled (! items.isEmpty());
    }

    int getNumRows() override { return jmax (1, items.size()); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (selected && ! items.isEmpty()) g.fillAll (Theme::accent.withAlpha (0.3f));
        g.setColour (items.isEmpty() ? Theme::muted : Theme::text);
        g.setFont (FontOptions (13.5f));
        g.drawText (items.isEmpty() ? String ("No plugins are disabled.") : File (items[row]).getFileName() + "   -   " + items[row],
                    8, 0, w - 16, h, Justification::centredLeft, true);
    }

public:
    std::function<void (bool)> onSandboxChanged;
    void setSandbox (bool b) { sandboxToggle.setToggleState (b, dontSendNotification); }
private:
    ToggleButton sandboxToggle { "Run new VST3 plugins in their own process (sandbox: a crash can't take Pulse down)" };
    Label title, safeInfo;
    ListBox list;
    TextButton enableBtn { "Re-enable selected" }, logsBtn { "Show crash logs" };
    StringArray items;
};

/** Audio Settings dialog: device selector (ASIO etc.) + recording options. */
inline void showAudioSettingsDialog (te::Engine& engine, te::Edit& edit, PropertiesFile& settings)
{
    auto* tabs = new TabbedComponent (TabbedButtonBar::TabsAtTop);
    tabs->setColour (TabbedComponent::backgroundColourId, Theme::panel);
    tabs->addTab ("Audio / MIDI", Theme::panel2,
                  new AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager, 0, 256, 0, 256, true, false, true, false), true);
    tabs->addTab ("Recording", Theme::panel2, new RecordingSettings (engine, edit, settings), true);
    auto* safety = new SafetySettings();
    safety->setSandbox (engine.getPropertyStorage().getPropertiesFile().getBoolValue ("sandboxPlugins", false));
    safety->onSandboxChanged = [&engine] (bool b) { engine.getPropertyStorage().getPropertiesFile().setValue ("sandboxPlugins", b); };
    tabs->addTab ("Safety", Theme::panel2, safety, true);
    tabs->setSize (580, 660);

    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.content.setOwned (tabs);
    o.launchAsync();
}
