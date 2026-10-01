#pragma once

#include "Common.h"

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

/** Audio Settings dialog: device selector (ASIO etc.) + recording options. */
inline void showAudioSettingsDialog (te::Engine& engine, te::Edit& edit, PropertiesFile& settings)
{
    auto* tabs = new TabbedComponent (TabbedButtonBar::TabsAtTop);
    tabs->setColour (TabbedComponent::backgroundColourId, Theme::panel);
    tabs->addTab ("Audio / MIDI", Theme::panel2,
                  new AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager, 0, 256, 0, 256, true, false, true, false), true);
    tabs->addTab ("Recording", Theme::panel2, new RecordingSettings (engine, edit, settings), true);
    tabs->setSize (580, 660);

    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.content.setOwned (tabs);
    o.launchAsync();
}
