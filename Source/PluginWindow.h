#pragma once

#include "Common.h"
#include "StateControls.h"

//==============================================================================
/** Generic editor for plugins with no GUI of their own (the built-in Tracktion effects):
    one rotary per automatable parameter. */
class GenericParameterEditor : public te::Plugin::EditorComponent,
                               private Timer
{
public:
    explicit GenericParameterEditor (te::Plugin& p) : plugin (p)
    {
        for (auto& d : StateControls::forPlugin (plugin))
            addAndMakeVisible (stateControls.add (new StateControls::Control (plugin, d)));

        for (auto* param : plugin.getAutomatableParameters())
        {
            auto* row = rows.add (new Row());
            row->param = param;
            row->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            row->slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            auto range = param->getValueRange();
            row->slider.setRange (range.getStart(), range.getEnd(), 0.0);
            row->slider.setValue (param->getCurrentValue(), dontSendNotification);
            row->slider.onDragStart = [param] { param->parameterChangeGestureBegin(); };
            row->slider.onDragEnd   = [param] { param->parameterChangeGestureEnd(); };
            row->slider.onValueChange = [row]
            {
                row->param->setParameter ((float) row->slider.getValue(), sendNotificationSync);
            };
            if (auto def = param->getDefaultValue())
                row->slider.setDoubleClickReturnValue (true, *def);

            row->name.setText (param->getParameterName(), dontSendNotification);
            row->name.setJustificationType (Justification::centred);
            row->name.setFont (FontOptions (12.0f));
            row->name.setColour (Label::textColourId, Theme::muted);
            row->value.setJustificationType (Justification::centred);
            row->value.setFont (FontOptions (12.0f));

            addAndMakeVisible (row->slider);
            addAndMakeVisible (row->name);
            addAndMakeVisible (row->value);
        }

        const int total = rows.size() + stateControls.size();
        const int cols = jlimit (1, 6, total);
        const int numRows = jmax (1, (total + cols - 1) / cols);
        setSize (jmax (200, cols * cellW + 16), numRows * cellH + 16);
        startTimerHz (15);
    }

    bool allowWindowResizing() override                         { return false; }
    ComponentBoundsConstrainer* getBoundsConstrainer() override { return nullptr; }

    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        const int cols = jmax (1, (getWidth() - 16) / cellW);
        auto cellAt = [cols] (int i) { return Rectangle<int> (8 + (i % cols) * cellW, 8 + (i / cols) * cellH, cellW, cellH); };

        for (int i = 0; i < stateControls.size(); ++i)
            stateControls[i]->setBounds (cellAt (i).reduced (0, 8));

        for (int i = 0; i < rows.size(); ++i)
        {
            auto cell = cellAt (i + stateControls.size());
            rows[i]->name.setBounds (cell.removeFromTop (16));
            rows[i]->value.setBounds (cell.removeFromBottom (16));
            rows[i]->slider.setBounds (cell.reduced (8, 0));
        }
    }

private:
    struct Row
    {
        te::AutomatableParameter* param = nullptr;
        Slider slider;
        Label name, value;
    };

    void timerCallback() override
    {
        for (auto* r : rows)
        {
            if (! r->slider.isMouseButtonDown())
                r->slider.setValue (r->param->getCurrentValue(), dontSendNotification);

            r->value.setText (r->param->getCurrentValueAsStringWithLabel(), dontSendNotification);
        }
    }

    static constexpr int cellW = 92, cellH = 96;
    te::Plugin& plugin;
    OwnedArray<Row> rows;
    OwnedArray<StateControls::Control> stateControls;
};

//==============================================================================
/** Hosts a VST3 plugin's own editor (falls back to JUCE's generic editor). */
struct ExternalEditorComponent : public te::Plugin::EditorComponent
{
    explicit ExternalEditorComponent (te::ExternalPlugin& p) : plugin (p)
    {
        if (auto pi = plugin.getAudioPluginInstance())
        {
            editor.reset (pi->createEditorAndMakeActive());

            if (editor == nullptr)
                editor = std::make_unique<GenericAudioProcessorEditor> (*pi);

            addAndMakeVisible (*editor);
        }

        resizeToFitEditor();
    }

    bool allowWindowResizing() override { return false; }

    ComponentBoundsConstrainer* getBoundsConstrainer() override
    {
        return editor != nullptr ? editor->getConstrainer() : nullptr;
    }

    void resized() override
    {
        if (editor != nullptr)
            editor->setBounds (getLocalBounds());
    }

    void childBoundsChanged (Component* c) override
    {
        if (c == editor.get())
        {
            plugin.edit.pluginChanged (plugin);
            resizeToFitEditor();
        }
    }

    void resizeToFitEditor()
    {
        setSize (jmax (8, editor != nullptr ? editor->getWidth() : 0),
                 jmax (8, editor != nullptr ? editor->getHeight() : 0));
    }

    te::ExternalPlugin& plugin;
    std::unique_ptr<AudioProcessorEditor> editor;
};

//==============================================================================
class PluginWindow : public DocumentWindow
{
public:
    explicit PluginWindow (te::Plugin& p)
        : DocumentWindow (p.getName(), Theme::panel, DocumentWindow::closeButton, true),
          plugin (p)
    {
        setUsingNativeTitleBar (true);
        getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
        setResizeLimits (100, 50, 4000, 4000);
        setEditor (createEditorFor (plugin));
        setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());
        updateStoredBounds = true;
    }

    ~PluginWindow() override
    {
        updateStoredBounds = false;
        plugin.edit.flushPluginStateIfNeeded (plugin);
        setEditor (nullptr);
    }

    static std::unique_ptr<te::Plugin::EditorComponent> createEditorFor (te::Plugin& p)
    {
        if (auto ext = dynamic_cast<te::ExternalPlugin*> (&p))
        {
            if (ext->getAudioPluginInstance() == nullptr)
                return {};

            return std::make_unique<ExternalEditorComponent> (*ext);
        }

        if (auto ed = p.createEditor())
            return ed;

        return std::make_unique<GenericParameterEditor> (p);
    }

    static std::unique_ptr<Component> create (te::Plugin& plugin)
    {
        auto w = std::make_unique<PluginWindow> (plugin);

        if (w->editor == nullptr)
            return {};

        w->setVisible (true);
        w->toFront (true);
        return w;
    }

    void recreateEditorAsync()
    {
        setEditor (nullptr);
        Timer::callAfterDelay (50, [sp = SafePointer<PluginWindow> (this)]
        {
            if (sp != nullptr)
                sp->setEditor (createEditorFor (sp->plugin));
        });
    }

private:
    void setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
    {
        setConstrainer (nullptr);
        editor.reset();

        if (newEditor != nullptr)
        {
            editor = std::move (newEditor);
            setContentNonOwned (editor.get(), true);
        }

        setResizable (editor == nullptr || editor->allowWindowResizing(), false);
    }

    void moved() override
    {
        if (updateStoredBounds)
        {
            plugin.windowState->lastWindowBounds = getBounds();
            plugin.edit.pluginChanged (plugin);
        }
    }

    void closeButtonPressed() override  { plugin.windowState->closeWindowExplicitly(); }
    float getDesktopScaleFactor() const override { return 1.0f; }

    te::Plugin& plugin;
    std::unique_ptr<te::Plugin::EditorComponent> editor;
    bool updateStoredBounds = false;
};

//==============================================================================
/** Tells the engine how to open plugin windows, run long tasks and report warnings. */
class PulseUIBehaviour : public te::UIBehaviour
{
public:
    /** Renders, freezes and exports run through here. Tracktion's default does nothing,
        so without this override every render silently produced no file. */
    void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
    {
        struct Runner : public ThreadWithProgressWindow
        {
            explicit Runner (te::ThreadPoolJobWithProgress& j) : ThreadWithProgressWindow (j.getJobName(), true, true), job (j) {}

            void run() override
            {
                while (! threadShouldExit())
                {
                    if (job.runJob() != ThreadPoolJob::jobNeedsRunningAgain)
                        break;
                    setProgress (job.getCurrentTaskProgress());
                }
                if (threadShouldExit())
                    job.signalJobShouldExit();
            }

            te::ThreadPoolJobWithProgress& job;
        };

        Runner runner (task);
        runner.runThread();
    }

    void showWarningMessage (const String& message) override
    {
        warnings().add (message);
        UIBehaviour::showWarningMessage (message);
    }

    /** Warnings raised by the engine (the self-test includes them in its report). */
    static StringArray& warnings() { static StringArray w; return w; }

    std::unique_ptr<Component> createPluginWindow (te::PluginWindowState& pws) override
    {
        if (auto ws = dynamic_cast<te::Plugin::WindowState*> (&pws))
            return PluginWindow::create (ws->plugin);

        return {};
    }

    void recreatePluginWindowContentAsync (te::Plugin& p) override
    {
        if (auto* w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
            return w->recreateEditorAsync();

        UIBehaviour::recreatePluginWindowContentAsync (p);
    }
};
