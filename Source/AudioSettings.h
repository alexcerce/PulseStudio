#pragma once

#include "Common.h"
#include "Commands.h"
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
    static void applyOverdub (te::Engine& engine, bool overdubOn) { applyMidiMode (engine, overdubOn ? 0 : 2); }

    /** MIDI recording: 0 = overdub (merge into the clip), 1 = a new take on every loop pass,
        2 = replace. Returns how many inputs were set. */
    static int applyMidiMode (te::Engine& engine, int mode)
    {
        int n = 0;
        for (auto& d : engine.getDeviceManager().getMidiInDevices())
        {
            d->mergeRecordings = mode == 0;
            d->replaceExistingClips = mode == 2;
            ++n;
        }
        return n;
    }

    static inline const StringArray midiModeNames { "Overdub (merge into the clip)", "New take on every loop pass", "Replace" };

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

/** Theme chooser: a card per theme with a small preview of its colours; click to switch. */
class AppearanceSettings : public Component
{
public:
    explicit AppearanceSettings (std::function<void (int)> pick) : onPick (std::move (pick))
    {
        title.setText ("Colour theme", dontSendNotification);
        title.setFont (FontOptions (15.0f, Font::bold));
        addAndMakeVisible (title);
        info.setText ("Click a theme to use it. It applies right away and is remembered.", dontSendNotification);
        info.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (info);
        viewport.setViewedComponent (&grid, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (int i = 0; i < (int) Theme::all().size(); ++i)
            grid.addAndMakeVisible (cards.add (new Card (*this, i)));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        title.setBounds (r.removeFromTop (22));
        info.setBounds (r.removeFromTop (20));
        r.removeFromTop (6);
        viewport.setBounds (r);
        const int w = r.getWidth() - viewport.getScrollBarThickness() - 4;
        const int cols = jmax (1, w / 170), cw = w / cols, ch = 112;
        for (int i = 0; i < cards.size(); ++i)
            cards[i]->setBounds ((i % cols) * cw, (i / cols) * ch, cw, ch);
        grid.setSize (w, ((cards.size() + cols - 1) / cols) * ch);
    }

    void picked (int i)
    {
        if (onPick) onPick (i);
        for (auto* c : cards) c->repaint();
    }

private:
    struct Card : public Component
    {
        Card (AppearanceSettings& o, int i) : owner (o), index (i) { setMouseCursor (MouseCursor::PointingHandCursor); }
        void paint (Graphics& g) override
        {
            auto& d = Theme::all()[(size_t) index];
            auto r = getLocalBounds().toFloat().reduced (5.0f);
            const bool current = index == Theme::currentIndex();
            g.setColour (Colour (d.bg));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (current ? Colour (d.accent) : (hover ? Colour (d.muted) : Colour (d.line)));
            g.drawRoundedRectangle (r, 6.0f, current ? 2.5f : 1.0f);
            auto inner = r.reduced (7.0f);
            auto nameRow = inner.removeFromBottom (18.0f);
            // a tiny mock: header bar, a panel with three "clips", a display with a curve
            auto top = inner.removeFromTop (10.0f);
            g.setColour (Colour (d.header));
            g.fillRoundedRectangle (top, 2.0f);
            g.setColour (Colour (d.accent));
            g.fillRoundedRectangle (top.removeFromLeft (16.0f).reduced (2.0f), 1.5f);
            inner.removeFromTop (3.0f);
            auto left = inner.removeFromLeft (inner.getWidth() * 0.55f);
            g.setColour (Colour (d.panel));
            g.fillRoundedRectangle (left, 2.0f);
            const uint32 clipCols[] = { d.red, d.green, d.accent };
            for (int k = 0; k < 3; ++k)
            {
                auto row = Rectangle<float> (left.getX() + 3.0f + k * 8.0f, left.getY() + 3.0f + k * 12.0f, left.getWidth() * 0.55f, 9.0f);
                g.setColour (Colour (clipCols[k]).withAlpha (0.8f));
                g.fillRoundedRectangle (row, 1.5f);
            }
            inner.removeFromLeft (3.0f);
            g.setColour (Colour (d.well));
            g.fillRoundedRectangle (inner, 2.0f);
            Path curve;
            for (int x = 0; x <= 20; ++x)
            {
                const float fx = inner.getX() + inner.getWidth() * x / 20.0f;
                const float fy = inner.getCentreY() - std::sin (x * 0.45f) * inner.getHeight() * 0.3f;
                if (x == 0) curve.startNewSubPath (fx, fy); else curve.lineTo (fx, fy);
            }
            g.setColour (Colour (d.orange));
            g.strokePath (curve, PathStrokeType (1.5f));
            g.setColour (Colour (d.text));
            g.setFont (FontOptions (12.0f, current ? Font::bold : Font::plain));
            g.drawText (String (d.name) + (current ? "  (current)" : String()), nameRow, Justification::centredLeft);
        }
        void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const MouseEvent& e) override  { if (! e.mouseWasDraggedSinceMouseDown()) owner.picked (index); }
        AppearanceSettings& owner;
        int index;
        bool hover = false;
    };

    std::function<void (int)> onPick;
    Label title, info;
    Viewport viewport;
    Component grid;
    OwnedArray<Card> cards;
};

/** Settings dialog: audio / MIDI devices, recording, safety and appearance (themes). */
inline void showAudioSettingsDialog (te::Engine& engine, te::Edit& edit, PropertiesFile& settings,
                                     std::function<void (int)> onTheme = {}, bool openAppearance = false, CommandRegistry* shortcuts = nullptr)
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
    if (onTheme)
        tabs->addTab ("Appearance", Theme::panel2, new AppearanceSettings (onTheme), true);
    if (openAppearance && onTheme) tabs->setCurrentTabIndex (tabs->getNumTabs() - 1);
    if (shortcuts != nullptr)
        tabs->addTab ("Shortcuts", Theme::panel2, new ShortcutEditor (*shortcuts), true);
    tabs->setSize (720, 660);

    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.content.setOwned (tabs);
    o.launchAsync();
}
