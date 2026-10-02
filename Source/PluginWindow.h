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
/** Pulse's frame around a plugin's own editor: a header with presets (the plugin's programs and
    your saved states), A/B compare, randomise (undoable) and a strip of parameter knobs you can
    search; the parameters you touched last come first. */
struct ExternalEditorComponent : public te::Plugin::EditorComponent,
                                 private Timer
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

        title.setText (plugin.getName(), dontSendNotification);
        title.setFont (FontOptions (14.0f, Font::bold));
        title.setColour (Label::textColourId, Theme::text);
        addAndMakeVisible (title);
        for (auto* b : { &presetBtn, &abBtn, &randomBtn, &paramsBtn })
            addAndMakeVisible (b);
        presetBtn.onClick = [this] { showPresets(); };
        abBtn.setTooltip ("Compare two versions of the settings (click to switch between A and B)");
        abBtn.onClick = [this] { toggleAB(); };
        randomBtn.onClick = [this] { showRandomMenu(); };
        paramsBtn.setClickingTogglesState (true);
        paramsBtn.setTooltip ("Show the plugin's parameters as knobs (the ones you touched last come first)");
        paramsBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        paramsBtn.onClick = [this] { strip.setVisible (paramsBtn.getToggleState()); search.setVisible (paramsBtn.getToggleState()); rebuildKnobs(); resizeToFitEditor(); };

        search.setTextToShowWhenEmpty ("Search parameters...", Theme::muted);
        search.onTextChange = [this] { rebuildKnobs(); };
        addChildComponent (search);
        strip.setViewedComponent (&knobHolder, false);
        strip.setScrollBarsShown (false, true);
        addChildComponent (strip);

        for (auto* prm : plugin.getAutomatableParameters())
            lastValues.push_back (prm->getCurrentValue());
        resizeToFitEditor();
        startTimerHz (10);
    }

    bool allowWindowResizing() override { return false; }
    ComponentBoundsConstrainer* getBoundsConstrainer() override { return nullptr; }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::header);
        g.fillRect (getLocalBounds().removeFromTop (headerH));
        g.setColour (Theme::accent);
        g.fillRect (0, headerH - 2, getWidth(), 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (headerH).reduced (8, 6).withTrimmedBottom (2);
        for (auto* b : { &paramsBtn, &randomBtn, &abBtn, &presetBtn }) { b->setBounds (top.removeFromRight (b == &abBtn ? 44 : 92)); top.removeFromRight (6); }
        title.setBounds (top);
        if (strip.isVisible())
        {
            auto bottom = r.removeFromBottom (stripH);
            search.setBounds (bottom.removeFromTop (24).reduced (8, 2).withWidth (240));
            strip.setBounds (bottom);
            knobHolder.setSize (jmax (strip.getWidth(), knobs.size() * knobW + 8), stripH - 24 - strip.getScrollBarThickness());
            for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (4 + i * knobW, 0, knobW, knobHolder.getHeight());
        }
        if (editor != nullptr) editor->setBounds (r.withWidth (editor->getWidth()).withHeight (editor->getHeight()));
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
        const int ew = editor != nullptr ? editor->getWidth() : 0, eh = editor != nullptr ? editor->getHeight() : 0;
        setSize (jmax (520, ew), headerH + jmax (8, eh) + (strip.isVisible() ? stripH : 0));
        resized();
    }

    //==============================================================================
    /** A knob for one parameter (drag, double-click resets, shows the value). */
    struct Knob : public Component
    {
        explicit Knob (te::AutomatableParameter& prm) : param (&prm)
        {
            slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            auto range = prm.getValueRange();
            slider.setRange (range.getStart(), range.getEnd(), 0.0);
            slider.setValue (prm.getCurrentValue(), dontSendNotification);
            slider.onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName()); param->parameterChangeGestureBegin(); };
            slider.onDragEnd   = [this] { param->parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), sendNotificationSync); };
            if (auto d = prm.getDefaultValue()) slider.setDoubleClickReturnValue (true, *d);
            name.setText (prm.getParameterName(), dontSendNotification);
            name.setTooltip (prm.getParameterName());
            for (auto* l : { &name, &value })
            {
                l->setJustificationType (Justification::centred);
                l->setFont (FontOptions (10.5f));
                l->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (l);
            }
            name.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (slider);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (14));
            value.setBounds (r.removeFromBottom (14));
            slider.setBounds (r.reduced (6, 0));
        }
        void refresh()
        {
            if (! slider.isMouseButtonDown()) slider.setValue (param->getCurrentValue(), dontSendNotification);
            value.setText (param->getCurrentValueAsString(), dontSendNotification);
        }
        te::AutomatableParameter::Ptr param;
        Slider slider;
        Label name, value;
    };

    int numKnobs() const { return knobs.size(); }
    void setSearch (const String& t) { search.setText (t, false); rebuildKnobs(); }
    void showParameters (bool show) { paramsBtn.setToggleState (show, sendNotificationSync); }
    String knobName (int i) const { return knobs[i] != nullptr ? knobs[i]->param->getParameterName() : String(); }

    /** Settings: the plugin's state plus every parameter value (some plugins only fold parameter
        changes into their state while audio runs). */
    struct Snapshot
    {
        MemoryBlock state;
        std::vector<float> params;
        bool isEmpty() const { return state.getSize() == 0 && params.empty(); }
        void reset() { state.reset(); params.clear(); }
    };

    Snapshot capture() const
    {
        Snapshot sn;
        if (auto pi = plugin.getAudioPluginInstance())
        {
            pi->getStateInformation (sn.state);
            for (auto* prm : pi->getParameters()) sn.params.push_back (prm->getValue());
        }
        return sn;
    }

    void restore (const Snapshot& sn)
    {
        auto pi = plugin.getAudioPluginInstance();
        if (pi == nullptr || sn.isEmpty()) return;
        if (sn.state.getSize() > 0) pi->setStateInformation (sn.state.getData(), (int) sn.state.getSize());
        auto& ps = pi->getParameters();
        for (int i = 0; i < ps.size() && i < (int) sn.params.size(); ++i)
            if (std::abs (ps[i]->getValue() - sn.params[(size_t) i]) > 1.0e-6f) ps[i]->setValueNotifyingHost (sn.params[(size_t) i]);
        plugin.edit.pluginChanged (plugin);
    }

    /** A / B: the current settings go into this slot, the other slot's come back. */
    void toggleAB()
    {
        auto pi = plugin.getAudioPluginInstance();
        if (pi == nullptr) return;
        auto& mine = abIsB ? slotB : slotA;
        auto& other = abIsB ? slotA : slotB;
        mine = capture();
        restore (other);
        abIsB = ! abIsB;
        abBtn.setButtonText (abIsB ? "B" : "A");
        plugin.edit.pluginChanged (plugin);
    }

    /** Randomises every parameter (amount 1) or nudges them (0.2); undo restores. */
    void randomise (float amount)
    {
        auto pi = plugin.getAudioPluginInstance();
        if (pi == nullptr) return;
        beforeRandom = capture();
        Random rng;
        for (auto* prm : pi->getParameters())
        {
            if (prm->isMetaParameter() || ! prm->isAutomatable()) continue;
            const float v = prm->getValue();
            prm->setValueNotifyingHost (jlimit (0.0f, 1.0f, amount >= 1.0f ? rng.nextFloat() : v + (rng.nextFloat() * 2.0f - 1.0f) * amount));
        }
        plugin.edit.pluginChanged (plugin);
    }

    void undoRandomise()
    {
        restore (beforeRandom);
        beforeRandom.reset();
    }

    static File stateFolder (te::ExternalPlugin& p)
    {
        auto f = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio").getChildFile ("Presets").getChildFile ("Plugin States")
                     .getChildFile (File::createLegalFileName (p.getName()));
        f.createDirectory();
        return f;
    }

    bool saveState (const String& name)
    {
        auto pi = plugin.getAudioPluginInstance();
        if (pi == nullptr) return false;
        MemoryBlock mb;
        pi->getStateInformation (mb);
        return stateFolder (plugin).getChildFile (File::createLegalFileName (name) + ".pulsestate").replaceWithData (mb.getData(), mb.getSize());
    }

    bool loadState (const File& f)
    {
        auto pi = plugin.getAudioPluginInstance();
        MemoryBlock mb;
        if (pi == nullptr || ! f.loadFileAsData (mb) || mb.getSize() == 0) return false;
        pi->setStateInformation (mb.getData(), (int) mb.getSize());
        plugin.edit.pluginChanged (plugin);
        return true;
    }

private:
    void showPresets()
    {
        PopupMenu m;
        if (auto pi = plugin.getAudioPluginInstance(); pi != nullptr && pi->getNumPrograms() > 1)
        {
            PopupMenu progs;
            for (int i = 0; i < jmin (512, pi->getNumPrograms()); ++i)
                progs.addItem (pi->getProgramName (i).isNotEmpty() ? pi->getProgramName (i) : "Program " + String (i + 1), true, i == pi->getCurrentProgram(),
                               [this, i] { if (auto p2 = plugin.getAudioPluginInstance()) { p2->setCurrentProgram (i); plugin.edit.pluginChanged (plugin); } });
            m.addSubMenu ("Plugin programs", progs);
        }
        auto files = stateFolder (plugin).findChildFiles (File::findFiles, false, "*.pulsestate");
        files.sort();
        if (! files.isEmpty())
        {
            m.addSectionHeader ("Your saved settings");
            for (auto& f : files) m.addItem (f.getFileNameWithoutExtension(), [this, f] { loadState (f); });
        }
        m.addSeparator();
        m.addItem ("Save current settings...", [this]
        {
            auto* w = new AlertWindow ("Save settings", "Name:", MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", plugin.getName() + " 1");
            w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
            {
                const auto n = w->getTextEditorContents ("name").trim();
                if (r == 1 && n.isNotEmpty()) saveState (n);
            }), true);
        });
        m.addItem ("Open the settings folder", [this] { stateFolder (plugin).startAsProcess(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn));
    }

    void showRandomMenu()
    {
        PopupMenu m;
        m.addItem ("Randomise", [this] { randomise (1.0f); });
        m.addItem ("Randomise a little", [this] { randomise (0.2f); });
        m.addItem ("Undo randomise", ! beforeRandom.isEmpty(), false, [this] { undoRandomise(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&randomBtn));
    }

    void rebuildKnobs()
    {
        knobs.clear();
        if (! strip.isVisible()) return;
        const auto q = search.getText().trim();
        auto params = plugin.getAutomatableParameters();
        std::vector<int> order;
        for (auto idx : touched) if (idx < params.size()) order.push_back (idx);
        for (int i = 0; i < params.size(); ++i) if (std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i);
        for (int idx : order)
        {
            auto* prm = params[idx];
            if (q.isNotEmpty() && ! prm->getParameterName().containsIgnoreCase (q)) continue;
            knobHolder.addAndMakeVisible (knobs.add (new Knob (*prm)));
            if (knobs.size() >= 256) break;
        }
        resized();
    }

    void timerCallback() override
    {
        auto params = plugin.getAutomatableParameters();
        bool moved = false;
        for (int i = 0; i < params.size() && i < (int) lastValues.size(); ++i)
        {
            const float v = params[i]->getCurrentValue();
            if (std::abs (v - lastValues[(size_t) i]) > 1.0e-5f)
            {
                lastValues[(size_t) i] = v;
                touched.erase (std::remove (touched.begin(), touched.end(), i), touched.end());
                touched.insert (touched.begin(), i);
                if (touched.size() > 64) touched.pop_back();
                moved = true;
            }
        }
        for (auto* k : knobs) k->refresh();
        if (moved && strip.isVisible() && search.getText().isEmpty() && ! isMouseButtonDownAnywhere()) rebuildKnobs();
    }

    static constexpr int headerH = 38, stripH = 120, knobW = 76;
    te::ExternalPlugin& plugin;
    std::unique_ptr<AudioProcessorEditor> editor;
    Label title;
    TextButton presetBtn { "Presets" }, abBtn { "A" }, randomBtn { "Randomise" }, paramsBtn { "Parameters" };
    TextEditor search;
    Viewport strip;
    Component knobHolder;
    OwnedArray<Knob> knobs;
    std::vector<float> lastValues;
    std::vector<int> touched;
    Snapshot slotA, slotB, beforeRandom;
    bool abIsB = false;
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
