#pragma once

#include "EditViewState.h"
#include "PluginMenu.h"
#include "Routing.h"
#include "ClipImport.h"
#include "Presets.h"
#include "Macros.h"
#include "MidiLearn.h"
#include "DrumRack.h"
#include "StateControls.h"
#include "Icons.h"
#include "CrashGuard.h"
#include "ParamKnob.h"
#include "DeviceDisplays.h"

//==============================================================================
/** Shared look + header buttons for cards in the rack. */
class CardBase : public Component
{
public:
    static constexpr int headerH = 36;

    void paintCard (Graphics& g, Colour stripe, bool enabled)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Colours::black.withAlpha (0.35f));                       // drop shadow
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 8.0f);
        g.setGradientFill (ColourGradient (Theme::panel2.brighter (0.04f), 0.0f, r.getY(), Theme::panel2.darker (0.12f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);

        auto head = r.withHeight ((float) headerH);
        g.setGradientFill (ColourGradient (stripe.withAlpha (0.16f), 0.0f, head.getY(), stripe.withAlpha (0.03f), 0.0f, head.getBottom(), false));
        g.fillRoundedRectangle (head, 8.0f);
        g.setColour (stripe);
        g.fillRoundedRectangle (r.withHeight (3.0f).reduced (6.0f, 0.0f), 1.5f);
        g.setColour (Theme::line.brighter (0.1f));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (Colours::black.withAlpha (0.3f));
        g.drawHorizontalLine (headerH, 1.0f, (float) getWidth() - 1.0f);

        if (! enabled)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.withTrimmedTop ((float) headerH), 8.0f);
        }
    }
};

//==============================================================================
/** One device in the chain: header (power / name / presets / editor / move / remove) + quick knobs. */
class PluginCard : public CardBase,
                   private Timer
{
public:
    PluginCard (EditViewState& e, te::Plugin::Ptr p, std::function<void (te::Plugin&, int)> mover)
        : evs (e), plugin (p), move (std::move (mover))
    {
        power.setClickingTogglesState (true);
        power.setColour (TextButton::buttonOnColourId, Theme::green);
        power.setToggleState (plugin->isEnabled(), dontSendNotification);
        power.onClick = [this] { plugin->setEnabled (power.getToggleState()); repaint(); };
        power.setTooltip ("Bypass");

        title.setText (plugin->getName(), dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        title.setInterceptsMouseClicks (false, false);

        editBtn.onClick  = [this] { plugin->showWindowExplicitly(); };
        leftBtn.onClick  = [this] { move (*plugin, -1); };
        rightBtn.onClick = [this] { move (*plugin, 1); };
        removeBtn.onClick = [this] { plugin->deleteFromParent(); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        presetBtn.setTooltip ("Device presets");
        editBtn.setTooltip (dynamic_cast<te::ExternalPlugin*> (plugin.get()) != nullptr ? "Open plugin window" : "Open all parameters");

        for (auto* c : std::initializer_list<Component*> { &power, &title, &presetBtn, &editBtn, &leftBtn, &rightBtn, &removeBtn })
            addAndMakeVisible (c);
        for (auto* b : { &presetBtn, &leftBtn, &rightBtn, &removeBtn }) b->setFlat (true);

        if (plugin->canSidechain())
        {
            scBtn.setColour (TextButton::buttonOnColourId, Theme::orange);
            scBtn.onClick = [this]
            {
                PopupMenu m;
                auto current = plugin->getSidechainSourceName();
                for (auto& name : plugin->getSidechainSourceNames (true))
                    m.addItem (name, true, name == current, [this, name] { plugin->setSidechainSourceByName (name); updateScButton(); });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (&scBtn));
            };
            addAndMakeVisible (scBtn);
            updateScButton();
        }

        // Pulse devices: labelled knob sections plus a visual display
        if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (plugin.get()))
        {
            buildPulseLayout (*pp);
            startTimerHz (4);
            return;
        }

        // settings stored as plain state (e.g. all of Chorus / Phaser, the Delay time) come first
        for (auto& d : StateControls::forPlugin (*plugin))
            addAndMakeVisible (knobs.add (new StateControls::Control (*plugin, d)));

        if (plugin->getPluginType() == te::DelayPlugin::xmlTypeName)
        {
            auto* sync = new TextButton ("Sync");
            sync->setTooltip ("Set the delay time to a note value at the current tempo");
            sync->onClick = [this, sync] { StateControls::showDelaySyncMenu (*plugin, *sync); };
            addAndMakeVisible (knobs.add (sync));
        }

        auto params = plugin->getAutomatableParameters();
        const int maxKnobs = jmax (0, 8 - knobs.size());
        for (int i = 0; i < jmin (maxKnobs, params.size()); ++i)
            addAndMakeVisible (knobs.add (new ParamKnob (evs, *params[i])));

        if (auto ext = dynamic_cast<te::ExternalPlugin*> (plugin.get()); ext != nullptr && ext->getAudioPluginInstance() == nullptr)
        {
            more.setText (CrashGuard::safeMode() ? "Not loaded (safe mode)"
                          : CrashGuard::isDisabled (ext->desc.fileOrIdentifier) ? "Disabled after a crash - re-enable in Audio Settings > Safety"
                          : "Plugin not found or failed to load", dontSendNotification);
            more.setColour (Label::textColourId, Theme::orange);
            addAndMakeVisible (more);
        }
        else if (knobs.isEmpty() && dynamic_cast<te::ExternalPlugin*> (plugin.get()) == nullptr)
        {
            more.setText ("No adjustable settings", dontSendNotification);
            more.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (more);
        }

        if (params.size() > maxKnobs)
        {
            more.setText ("+" + String (params.size() - jmax (0, maxKnobs)) + " more - open the editor", dontSendNotification);
            more.setColour (Label::textColourId, Theme::muted);
            more.setFont (FontOptions (12.0f));
            addAndMakeVisible (more);
        }

        const int cols = jmax (2, jmin (4, knobs.size()));
        setSize (jmax (290, cols * knobW + 16), 0);
        startTimerHz (4);
    }

    void paint (Graphics& g) override { paintCard (g, plugin->isSynth() ? Theme::orange : Theme::accent, plugin->isEnabled()); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (6);
        removeBtn.setBounds (head.removeFromRight (24)); head.removeFromRight (2);
        rightBtn.setBounds (head.removeFromRight (24));  head.removeFromRight (2);
        leftBtn.setBounds (head.removeFromRight (24));   head.removeFromRight (4);
        editBtn.setBounds (head.removeFromRight (38));   head.removeFromRight (2);
        presetBtn.setBounds (head.removeFromRight (24));
        if (scBtn.isVisible()) { head.removeFromRight (2); scBtn.setBounds (head.removeFromRight (30)); }
        title.setBounds (head);

        r.removeFromTop (6);

        if (! sections.empty())
        {
            r.removeFromBottom (4);
            if (display != nullptr)
            {
                display->setBounds (r.removeFromLeft (displayWidth));
                r.removeFromLeft (8);
            }
            for (auto& sec : sections)
            {
                auto area = r.removeFromLeft (sec.cols * knobW);
                r.removeFromLeft (sectionGap);
                sec.label->setBounds (area.removeFromTop (14));
                for (int i = 0; i < (int) sec.knobs.size(); ++i)
                    sec.knobs[(size_t) i]->setBounds (area.getX() + (i / pulseRows) * knobW, area.getY() + (i % pulseRows) * knobH, knobW, knobH);
            }
            return;
        }

        if (more.isVisible())
            more.setBounds (r.removeFromBottom (18));

        const int cols = jmax (1, r.getWidth() / knobW);
        for (int i = 0; i < knobs.size(); ++i)
        {
            auto cell = Rectangle<int> (r.getX() + (i % cols) * knobW, r.getY() + (i / cols) * knobH, knobW, knobH);
            if (dynamic_cast<TextButton*> (knobs[i]) != nullptr)
                cell = cell.withSizeKeepingCentre (knobW - 12, 24);
            knobs[i]->setBounds (cell);
        }
    }

    void mouseDoubleClick (const MouseEvent&) override { plugin->showWindowExplicitly(); }

private:
    void timerCallback() override
    {
        if (power.getToggleState() != plugin->isEnabled())
        {
            power.setToggleState (plugin->isEnabled(), dontSendNotification);
            repaint();
        }
    }

    void showPresetMenu()
    {
        PopupMenu m;
        m.addItem ("Save preset...", [this]
        {
            Presets::askForName ("Save " + plugin->getName() + " preset", plugin->getName(),
                                 [p = plugin] (const String& name) { Presets::saveDevice (*p, name); });
        });
        auto files = Presets::devicePresets (*plugin);
        if (! files.isEmpty())
        {
            m.addSeparator();
            for (auto& f : files)
                m.addItem (f.getFileNameWithoutExtension(), [p = plugin, f]
                {
                    p->edit.getUndoManager().beginNewTransaction ("Load preset");
                    Presets::loadDevice (*p, f);
                });
        }
        m.addSeparator();
        m.addItem ("Show presets folder", [p = plugin] { Presets::deviceFolder (*p).revealToUser(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetBtn));
    }

    void updateScButton()
    {
        auto src = plugin->getSidechainSourceName();
        const bool active = src.isNotEmpty() && ! src.equalsIgnoreCase ("none") && ! src.equalsIgnoreCase ("<none>");
        scBtn.setToggleState (active, dontSendNotification);
        scBtn.setTooltip (active ? "Sidechain input: " + src : "Sidechain input: none (click to choose a source track)");
    }

    void buildPulseLayout (pulse::PulsePlugin& pp)
    {
        displayWidth = pp.getDisplayWidth();
        display = DeviceDisplays::create (evs, pp);
        if (display != nullptr)
            addAndMakeVisible (*display);

        int width = 12 + (display != nullptr ? displayWidth + 8 : 0);
        for (auto& sec : pp.getSections())
        {
            PulseSection ps;
            ps.label = std::make_unique<Label> (String(), sec.name.toUpperCase());
            ps.label->setFont (FontOptions (11.0f, Font::bold));
            ps.label->setColour (Label::textColourId, Theme::muted);
            ps.label->setJustificationType (Justification::centredLeft);
            addAndMakeVisible (*ps.label);
            for (auto idx : sec.params)
            {
                auto* k = new ParamKnob (evs, *pp.param (idx));
                knobs.add (k);
                // drop the section prefix ("Osc 1 Position" in "Osc 1" -> "Position")
                auto name = k->name.getText();
                if (name.startsWithIgnoreCase (sec.name + " ")) k->name.setText (name.substring (sec.name.length() + 1), dontSendNotification);
                addAndMakeVisible (k);
                ps.knobs.push_back (k);
            }
            ps.cols = jmax (1, ((int) ps.knobs.size() + pulseRows - 1) / pulseRows);
            width += ps.cols * knobW + sectionGap;
            sections.push_back (std::move (ps));
        }
        setSize (jmax (290, width), 0);
    }

    struct PulseSection { std::unique_ptr<Label> label; std::vector<Component*> knobs; int cols = 1; };
    std::vector<PulseSection> sections;
    std::unique_ptr<Component> display;
    int displayWidth = 0;
    static constexpr int pulseRows = 2, sectionGap = 10;

    static constexpr int knobW = 64, knobH = 78;
    EditViewState& evs;
    te::Plugin::Ptr plugin;
    std::function<void (te::Plugin&, int)> move;
    IconButton power { Icons::Type::power, "Device on / off", Theme::green },
               leftBtn { Icons::Type::chevronLeft, "Move left" }, rightBtn { Icons::Type::chevronRight, "Move right" },
               removeBtn { Icons::Type::close, "Remove device" }, presetBtn { Icons::Type::menu, "Device presets" };
    TextButton editBtn { "Edit" }, scBtn { "SC" };
    Label title, more;
    OwnedArray<Component> knobs;   // state controls, the Delay's Sync button, then parameter knobs
};

//==============================================================================
/** The track's eight macro knobs. Right-click any device knob -> Map to Macro. */
class MacrosCard : public CardBase,
                   private Timer
{
public:
    MacrosCard (EditViewState& e, te::AudioTrack& t) : evs (e), track (&t)
    {
        title.setText ("Macros", dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        addAndMakeVisible (title);
        hint.setText ("Right-click any device knob and choose \"Map to Macro\".", dontSendNotification);
        hint.setFont (FontOptions (12.0f));
        hint.setColour (Label::textColourId, Theme::muted);
        hint.setJustificationType (Justification::topLeft);
        addAndMakeVisible (hint);
        build();
        setSize (4 * 64 + 16, 0);
        startTimerHz (2);
    }

    void paint (Graphics& g) override { paintCard (g, Theme::yellow, true); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        title.setBounds (r.removeFromTop (headerH));
        r.removeFromTop (4);
        if (knobs.isEmpty())
        {
            hint.setBounds (r.reduced (0, 4));
            return;
        }
        hint.setBounds ({});
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (r.getX() + (i % 4) * 64, r.getY() + (i / 4) * 78, 64, 78);
    }

private:
    void build()
    {
        knobs.clear();
        for (auto m : Macros::get (*track))
        {
            auto* k = knobs.add (new ParamKnob (evs, *m));
            k->name.setText (m->getParameterName(), dontSendNotification);
            k->setTooltip ("Controls: " + Macros::targets (*track, *m).joinIntoString (", "));
            addAndMakeVisible (k);
        }
        builtCount = knobs.size();
        resized();
    }

    void timerCallback() override
    {
        if (Macros::get (*track).size() != builtCount)
            build();
        for (int i = 0; i < knobs.size(); ++i)
            if (auto m = Macros::get (*track)[i])
            {
                auto targets = Macros::targets (*track, *m);
                knobs[i]->setTooltip (targets.isEmpty() ? "Not mapped yet" : "Controls: " + targets.joinIntoString (", "));
            }
    }

    EditViewState& evs;
    te::AudioTrack::Ptr track;
    Label title, hint;
    OwnedArray<ParamKnob> knobs;
    int builtCount = -1;
};

//==============================================================================
/** Drum Rack editor: 4x4 pads (drop samples, click to audition) + the selected pad's
    sample settings and its own effect chain. */
class DrumRackCard : public CardBase,
                     private Timer
{
public:
    DrumRackCard (EditViewState& e, te::RackInstance& inst) : evs (e), instanceHolder (&inst), instance (&inst), rack (inst.type)
    {
        for (int i = 0; i < DrumRack::numPads; ++i)
            addAndMakeVisible (pads.add (new Pad (*this, i)));

        title.setText ("Drum Rack", dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        addAndMakeVisible (title);
        power.setClickingTogglesState (true);
        power.setColour (TextButton::buttonOnColourId, Theme::green);
        power.setToggleState (instance->isEnabled(), dontSendNotification);
        power.onClick = [this] { instance->setEnabled (power.getToggleState()); };
        removeBtn.onClick = [this] { instance->deleteFromParent(); };
        addAndMakeVisible (power);
        addAndMakeVisible (removeBtn);

        padTitle.setFont (FontOptions (13.0f, Font::bold));
        sampleName.setColour (Label::textColourId, Theme::muted);
        sampleName.setFont (FontOptions (12.5f));
        for (auto* l : { &padTitle, &sampleName }) addAndMakeVisible (l);

        loadBtn.onClick = [this] { browseSample(); };
        clearBtn.onClick = [this] { DrumRack::clearPad (*rack, selected); refreshDetail(); };
        addFxBtn.onClick = [this] { addFx(); };
        for (auto* b : { &loadBtn, &clearBtn, &addFxBtn }) addAndMakeVisible (b);

        auto setup = [this] (Slider& s, Label& l, const String& name, double lo, double hi, double step)
        {
            s.setSliderStyle (Slider::LinearHorizontal);
            s.setTextBoxStyle (Slider::TextBoxRight, false, 52, 18);
            s.setRange (lo, hi, step);
            l.setText (name, dontSendNotification);
            l.setFont (FontOptions (12.0f));
            l.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };
        setup (gain, gainL, "Gain", -36.0, 12.0, 0.1);
        setup (pan, panL, "Pan", -1.0, 1.0, 0.01);
        setup (tune, tuneL, "Tune", -24.0, 24.0, 1.0);
        gain.onValueChange = [this] { applyGainPan(); };
        pan.onValueChange  = [this] { applyGainPan(); };
        tune.onValueChange = [this] { DrumRack::setTune (*rack, selected, (int) tune.getValue()); };

        refreshDetail();
        setSize (620, 0);
        startTimerHz (4);
    }

    void paint (Graphics& g) override
    {
        paintCard (g, Theme::orange, instance->isEnabled());
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.5f, Font::bold));
        g.drawText ("PAD CHAIN", detailArea.getX(), chainTop - 16, 120, 14, Justification::left);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (6);
        removeBtn.setBounds (head.removeFromRight (22));
        title.setBounds (head);
        r.removeFromTop (6);

        auto padArea = r.removeFromLeft (4 * 66);
        const int pw = 64, ph = jmin (52, (padArea.getHeight() - 8) / 4);
        for (int i = 0; i < DrumRack::numPads; ++i)
        {
            const int row = 3 - i / 4, col = i % 4;   // pad 0 bottom-left, like Push / Ableton
            pads[i]->setBounds (padArea.getX() + col * 66, padArea.getY() + row * (ph + 2), pw, ph);
        }

        r.removeFromLeft (12);
        detailArea = r;
        padTitle.setBounds (r.removeFromTop (18));
        sampleName.setBounds (r.removeFromTop (16));
        auto btns = r.removeFromTop (24);
        loadBtn.setBounds (btns.removeFromLeft (90)); btns.removeFromLeft (4);
        clearBtn.setBounds (btns.removeFromLeft (60));
        r.removeFromTop (4);
        auto row = [&r] (Label& l, Slider& s) { auto a = r.removeFromTop (22); l.setBounds (a.removeFromLeft (38)); s.setBounds (a); };
        row (gainL, gain); row (panL, pan); row (tuneL, tune);
        r.removeFromTop (20);
        chainTop = r.getY();
        for (auto* fx : fxRows)
            fx->setBounds (r.removeFromTop (22));
        r.removeFromTop (2);
        addFxBtn.setBounds (r.removeFromTop (22).removeFromLeft (90));
    }

private:
    struct Pad : public Component, public FileDragAndDropTarget, public DragAndDropTarget
    {
        Pad (DrumRackCard& o, int i) : owner (o), index (i) {}

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            auto name = DrumRack::padName (*owner.rack, index);
            const bool sel = owner.selected == index;
            auto col = owner.instance->getOwnerTrack() != nullptr ? owner.instance->getOwnerTrack()->getColour() : Theme::orange;
            g.setColour (name.isNotEmpty() ? col.withAlpha (hover ? 0.6f : 0.4f) : Colour (0xff1b1e23));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (sel ? Colours::white : (highlight ? Theme::accent : Theme::line));
            g.drawRoundedRectangle (r, 4.0f, sel || highlight ? 1.6f : 1.0f);
            g.setColour (name.isNotEmpty() ? Theme::text : Theme::muted);
            g.setFont (FontOptions (11.5f));
            g.drawFittedText (name.isNotEmpty() ? name : DrumRack::defaultPadLabel (index), getLocalBounds().reduced (4, 2), Justification::centred, 2);
            g.setColour (Theme::muted.withAlpha (0.7f));
            g.setFont (FontOptions (9.5f));
            g.drawText (MidiMessage::getMidiNoteName (DrumRack::baseNote + index, true, true, 3), getLocalBounds().reduced (3, 1), Justification::bottomRight);
        }

        void mouseDown (const MouseEvent&) override
        {
            owner.select (index);
            if (auto t = dynamic_cast<te::AudioTrack*> (owner.instance->getOwnerTrack()))
                t->playGuideNote (DrumRack::baseNote + index, te::MidiChannel (1), 110, true, true, true);
        }

        void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }

        bool isInterestedInFileDrag (const StringArray& f) override { return ! f.isEmpty() && ClipImport::isAudioFile (File (f[0])); }
        void fileDragEnter (const StringArray&, int, int) override { highlight = true; repaint(); }
        void fileDragExit (const StringArray&) override              { highlight = false; repaint(); }
        void filesDropped (const StringArray& f, int, int) override  { highlight = false; owner.loadInto (index, File (f[0])); }

        bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == "PulseFiles"; }
        void itemDragEnter (const SourceDetails&) override { highlight = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { highlight = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            highlight = false;
            auto files = ClipImport::filesFromDrag (d);
            // several files dropped: fill consecutive pads
            for (int i = 0; i < files.size() && index + i < DrumRack::numPads; ++i)
                owner.loadInto (index + i, files[i]);
        }

        DrumRackCard& owner;
        int index;
        bool hover = false, highlight = false;
    };

    struct FxRow : public Component
    {
        FxRow (DrumRackCard& o, te::Plugin::Ptr p) : owner (o), plugin (p)
        {
            name.setText (p->getName(), dontSendNotification);
            name.setFont (FontOptions (12.5f));
            editBtn.onClick = [this] { plugin->showWindowExplicitly(); };
            removeBtn.onClick = [this] { auto& o2 = owner; DrumRack::removeEffect (*o2.rack, *plugin); o2.refreshDetail(); };
            for (auto* c : std::initializer_list<Component*> { &name, &editBtn, &removeBtn }) addAndMakeVisible (c);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            removeBtn.setBounds (r.removeFromRight (22));
            editBtn.setBounds (r.removeFromRight (40));
            name.setBounds (r);
        }
        DrumRackCard& owner;
        te::Plugin::Ptr plugin;
        Label name;
        TextButton editBtn { "Edit" };
        IconButton removeBtn { Icons::Type::close, "Remove this effect" };
    };

    void select (int pad)
    {
        selected = pad;
        refreshDetail();
        for (auto* p : pads) p->repaint();
    }

    void loadInto (int pad, const File& f)
    {
        evs.edit.getUndoManager().beginNewTransaction ("Load drum sample");
        DrumRack::loadSample (*rack, pad, f);
        select (pad);
    }

    void browseSample()
    {
        chooser = std::make_shared<FileChooser> ("Load sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this, sp = SafePointer<DrumRackCard> (this)] (const FileChooser& fc)
                              { if (sp != nullptr && fc.getResult().existsAsFile()) loadInto (selected, fc.getResult()); });
    }

    void addFx()
    {
        PluginMenu::show (evs.edit, &addFxBtn, [this, sp = SafePointer<DrumRackCard> (this)] (te::Plugin::Ptr p)
        {
            if (sp == nullptr || p->isSynth()) return;
            DrumRack::addEffect (*rack, selected, p);
            refreshDetail();
        });
    }

    void applyGainPan()
    {
        if (auto s = DrumRack::sampler (*rack, selected); s != nullptr && s->getNumSounds() > 0)
            s->setSoundGains (0, (float) gain.getValue(), (float) pan.getValue());
    }

    void refreshDetail()
    {
        auto name = DrumRack::padName (*rack, selected);
        padTitle.setText ("Pad " + String (selected + 1) + "  -  " + MidiMessage::getMidiNoteName (DrumRack::baseNote + selected, true, true, 3),
                          dontSendNotification);
        sampleName.setText (name.isNotEmpty() ? name : "Empty - drop a sample on the pad", dontSendNotification);

        auto s = DrumRack::sampler (*rack, selected);
        const bool has = s != nullptr && s->getNumSounds() > 0;
        for (auto* c : std::initializer_list<Component*> { &gain, &pan, &tune, &clearBtn })
            c->setEnabled (has);
        if (has)
        {
            gain.setValue (s->getSoundGainDb (0), dontSendNotification);
            pan.setValue (s->getSoundPan (0), dontSendNotification);
            tune.setValue (DrumRack::getTune (*rack, selected), dontSendNotification);
        }

        fxRows.clear();
        shownChain = DrumRack::chain (*rack, selected);
        for (auto p : shownChain)
            addAndMakeVisible (fxRows.add (new FxRow (*this, p)));

        resized();
        repaint();
    }

    void timerCallback() override
    {
        for (auto* p : pads) p->repaint();
        if (DrumRack::chain (*rack, selected) != shownChain)
            refreshDetail();
    }

    EditViewState& evs;
    te::Plugin::Ptr instanceHolder;   // keeps the instance alive while the card exists
    te::RackInstance* instance;
    te::RackType::Ptr rack;
    OwnedArray<Pad> pads;
    OwnedArray<FxRow> fxRows;
    Array<te::Plugin*> shownChain;
    int selected = 0, chainTop = 0;
    Rectangle<int> detailArea;
    Label title, padTitle, sampleName, gainL, panL, tuneL;
    Slider gain, pan, tune;
    IconButton power { Icons::Type::power, "Drum Rack on / off", Theme::green }, removeBtn { Icons::Type::close, "Remove device" };
    TextButton loadBtn { "Load sample..." }, clearBtn { "Clear" }, addFxBtn { "+ Pad FX" };
    std::shared_ptr<FileChooser> chooser;
};

//==============================================================================
/** Standalone Sampler: one sample played chromatically across a key range. */
class SamplerCard : public CardBase,
                    public FileDragAndDropTarget,
                    public DragAndDropTarget,
                    private Timer
{
public:
    SamplerCard (EditViewState& e, te::SamplerPlugin& s) : evs (e), holder (&s), sampler (s)
    {
        title.setText ("Sampler", dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        power.setClickingTogglesState (true);
        power.setColour (TextButton::buttonOnColourId, Theme::green);
        power.setToggleState (sampler.isEnabled(), dontSendNotification);
        power.onClick = [this] { sampler.setEnabled (power.getToggleState()); };
        removeBtn.onClick = [this] { sampler.deleteFromParent(); };
        loadBtn.onClick = [this] { browse(); };
        playBtn.onClick = [this]
        {
            if (auto t = dynamic_cast<te::AudioTrack*> (sampler.getOwnerTrack()))
                t->playGuideNote (has() ? sampler.getKeyNote (0) : 60, te::MidiChannel (1), 110, true, true, true);
        };
        oneShot.onClick = [this] { if (has()) sampler.setSoundOpenEnded (0, oneShot.getToggleState()); };
        oneShot.setTooltip ("On: the whole sample plays on every note. Off: it stops when you release the key.");
        sampleName.setColour (Label::textColourId, Theme::muted);
        for (auto* c : std::initializer_list<Component*> { &title, &power, &removeBtn, &loadBtn, &playBtn, &oneShot, &sampleName })
            addAndMakeVisible (c);

        auto setup = [this] (Slider& sl, Label& l, const String& name, double lo, double hi, double step)
        {
            sl.setSliderStyle (Slider::LinearHorizontal);
            sl.setTextBoxStyle (Slider::TextBoxRight, false, 52, 18);
            sl.setRange (lo, hi, step);
            l.setText (name, dontSendNotification);
            l.setFont (FontOptions (12.0f));
            l.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (sl);
            addAndMakeVisible (l);
        };
        setup (root, rootL, "Root", 0, 127, 1);
        setup (low, lowL, "Low key", 0, 127, 1);
        setup (high, highL, "High key", 0, 127, 1);
        setup (gain, gainL, "Gain", -36.0, 12.0, 0.1);
        setup (pan, panL, "Pan", -1.0, 1.0, 0.01);
        root.textFromValueFunction = [] (double v) { return MidiMessage::getMidiNoteName ((int) v, true, true, 3); };
        low.textFromValueFunction = root.textFromValueFunction;
        high.textFromValueFunction = root.textFromValueFunction;
        for (auto* sl : { &root, &low, &high })
            sl->onValueChange = [this] { if (has()) sampler.setSoundParams (0, (int) root.getValue(), (int) low.getValue(), (int) high.getValue()); };
        for (auto* sl : { &gain, &pan })
            sl->onValueChange = [this] { if (has()) sampler.setSoundGains (0, (float) gain.getValue(), (float) pan.getValue()); };

        refresh();
        setSize (360, 0);
        startTimerHz (2);
    }

    void paint (Graphics& g) override
    {
        paintCard (g, Theme::orange, sampler.isEnabled());
        if (highlight)
        {
            g.setColour (Theme::accent);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 6.0f, 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (6);
        removeBtn.setBounds (head.removeFromRight (24));
        title.setBounds (head);
        r.removeFromTop (6);
        sampleName.setBounds (r.removeFromTop (18));
        auto btns = r.removeFromTop (24);
        loadBtn.setBounds (btns.removeFromLeft (110)); btns.removeFromLeft (4);
        playBtn.setBounds (btns.removeFromLeft (60));  btns.removeFromLeft (8);
        oneShot.setBounds (btns);
        r.removeFromTop (6);
        auto row = [&r] (Label& l, Slider& s) { auto a = r.removeFromTop (22); l.setBounds (a.removeFromLeft (60)); s.setBounds (a); r.removeFromTop (2); };
        row (rootL, root); row (lowL, low); row (highL, high); row (gainL, gain); row (panL, pan);
    }

    // drop a sample anywhere on the card
    bool isInterestedInFileDrag (const StringArray& f) override { return ! f.isEmpty() && ClipImport::isAudioFile (File (f[0])); }
    void fileDragEnter (const StringArray&, int, int) override  { highlight = true; repaint(); }
    void fileDragExit (const StringArray&) override               { highlight = false; repaint(); }
    void filesDropped (const StringArray& f, int, int) override   { highlight = false; load (File (f[0])); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == "PulseFiles"; }
    void itemDragEnter (const SourceDetails&) override { highlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { highlight = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        highlight = false;
        auto files = ClipImport::filesFromDrag (d);
        if (! files.isEmpty()) load (files.getFirst());
    }

private:
    bool has() const { return sampler.getNumSounds() > 0; }

    void load (const File& f)
    {
        te::AudioFile af (evs.edit.engine, f);
        if (! af.isValid()) return;
        evs.edit.getUndoManager().beginNewTransaction ("Load sample");
        while (sampler.getNumSounds() > 0) sampler.removeSound (0);
        if (sampler.addSound (f.getFullPathName(), f.getFileNameWithoutExtension(), 0.0, af.getLength(), 0.0f).isEmpty())
        {
            sampler.setSoundParams (0, 60, 0, 127);   // C3 root, whole keyboard
            sampler.setSoundOpenEnded (0, false);
            sampler.flushPendingUpdates();
        }
        refresh();
    }

    void browse()
    {
        chooser = std::make_shared<FileChooser> ("Load sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this, sp = SafePointer<SamplerCard> (this)] (const FileChooser& fc)
                              { if (sp != nullptr && fc.getResult().existsAsFile()) load (fc.getResult()); });
    }

    void refresh()
    {
        const bool h = has();
        sampleName.setText (h ? sampler.getSoundName (0) : "Empty - drop a sample here or click Load", dontSendNotification);
        for (auto* c : std::initializer_list<Component*> { &root, &low, &high, &gain, &pan, &oneShot })
            c->setEnabled (h);
        if (h)
        {
            root.setValue (sampler.getKeyNote (0), dontSendNotification);
            low.setValue (sampler.getMinKey (0), dontSendNotification);
            high.setValue (sampler.getMaxKey (0), dontSendNotification);
            gain.setValue (sampler.getSoundGainDb (0), dontSendNotification);
            pan.setValue (sampler.getSoundPan (0), dontSendNotification);
            oneShot.setToggleState (sampler.isSoundOpenEnded (0), dontSendNotification);
        }
    }

    void timerCallback() override
    {
        if (! isMouseButtonDownAnywhere())
            refresh();   // follows undo
    }

    EditViewState& evs;
    te::Plugin::Ptr holder;
    te::SamplerPlugin& sampler;
    Label title, sampleName, rootL, lowL, highL, gainL, panL;
    Slider root, low, high, gain, pan;
    IconButton power { Icons::Type::power, "Sampler on / off", Theme::green }, removeBtn { Icons::Type::close, "Remove device" };
    TextButton loadBtn { "Load sample..." }, playBtn { "Play" };
    ToggleButton oneShot { "One-shot" };
    std::shared_ptr<FileChooser> chooser;
    bool highlight = false;
};

//==============================================================================
/** Horizontal device rack for the selected track (or the master bus). */
class DeviceChain : public Component,
                    public DragAndDropTarget,
                    private ChangeListener,
                    private Timer
{
public:
    explicit DeviceChain (EditViewState& e) : evs (e)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&rack, false);
        viewport.setScrollBarsShown (false, true);

        addBtn.onClick = [this] { addDevice(); };
        rack.addAndMakeVisible (addBtn);

        masterBtn.setClickingTogglesState (true);
        masterBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
        masterBtn.onClick = [this] { showMaster = masterBtn.getToggleState(); rebuild(); };
        addAndMakeVisible (masterBtn);

        chainBtn.setTooltip ("Save or load a preset of the whole device chain");
        chainBtn.onClick = [this] { showChainMenu(); };
        addAndMakeVisible (chainBtn);

        heading.setFont (FontOptions (13.5f, Font::bold));
        addAndMakeVisible (heading);

        evs.selectionManager.addChangeListener (this);
        rebuild();
        startTimerHz (4);
    }

    ~DeviceChain() override { evs.selectionManager.removeChangeListener (this); }

    // Devices dragged from the browser land at the end of the chain.
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return ClipImport::isPluginDescription (d.description) && currentList() != nullptr;
    }

    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { dropHighlight = false; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        repaint();
        if (auto* list = currentList())
            Presets::applyDescription (currentTrack(), *list, d.description.toString());
    }

    void paintOverChildren (Graphics& g) override
    {
        if (dropHighlight)
        {
            g.setColour (Theme::accent);
            g.drawRect (getLocalBounds(), 2);
        }
    }

    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30).reduced (8, 4);
        masterBtn.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        chainBtn.setBounds (top.removeFromRight (100));
        heading.setBounds (top);
        viewport.setBounds (r);
        layoutRack();
    }

private:
    te::PluginList* currentList()
    {
        if (showMaster)
            return &evs.edit.getMasterPluginList();

        if (auto t = EngineHelpers::getSelectedTrack (evs.selectionManager))
            return &t->pluginList;

        return nullptr;
    }

    te::Track* currentTrack()
    {
        return showMaster ? nullptr : EngineHelpers::getSelectedTrack (evs.selectionManager);
    }

    /** Plugins shown as cards (hides the fixed volume/pan + meter at the end of a track). */
    Array<te::Plugin*> visiblePlugins()
    {
        Array<te::Plugin*> out;
        auto* at = dynamic_cast<te::AudioTrack*> (currentTrack());

        if (auto list = currentList())
            for (auto p : *list)
            {
                if (dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
                    || dynamic_cast<te::AuxSendPlugin*> (p) != nullptr     // sends live in the mixer
                    || dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr
                    || (at != nullptr && p == at->getVolumePlugin()))
                    continue;

                out.add (p);
            }
        return out;
    }

    void rebuild()
    {
        cards.clear();
        shown = visiblePlugins();

        auto* at = dynamic_cast<te::AudioTrack*> (currentTrack());
        if (at != nullptr && ! Routing::isReturnTrack (*at))
            rack.addAndMakeVisible (cards.add (new MacrosCard (evs, *at)));

        for (auto p : shown)
        {
            Component* c = nullptr;
            if (DrumRack::isDrumRack (p))
                c = new DrumRackCard (evs, *dynamic_cast<te::RackInstance*> (p));
            else if (auto s = dynamic_cast<te::SamplerPlugin*> (p))
                c = new SamplerCard (evs, *s);
            else
                c = new PluginCard (evs, p, [this] (te::Plugin& pl, int dir) { movePlugin (pl, dir); });
            rack.addAndMakeVisible (cards.add (c));
        }

        if (showMaster)
            heading.setText ("Master bus", dontSendNotification);
        else if (auto t = currentTrack())
            heading.setText (t->getName() + "  -  device chain", dontSendNotification);
        else
            heading.setText ("Select a track to see its devices", dontSendNotification);

        addBtn.setEnabled (currentList() != nullptr);
        chainBtn.setEnabled (currentList() != nullptr);
        layoutRack();
    }

    void layoutRack()
    {
        const int h = jmax (60, viewport.getHeight() - viewport.getScrollBarThickness());
        int x = 8;
        for (auto* c : cards)
        {
            c->setBounds (x, 4, c->getWidth(), h - 8);
            x += c->getWidth() + 8;
        }
        addBtn.setBounds (x, 4, 130, jmin (h - 8, 120));
        rack.setSize (x + 140, h);
    }

    void addDevice()
    {
        auto* list = currentList();
        if (list == nullptr) return;

        PluginMenu::show (evs.edit, &addBtn, [this, list, track = currentTrack()] (te::Plugin::Ptr p)
        {
            evs.edit.getUndoManager().beginNewTransaction ("Add device");
            PluginMenu::insertPlugin (track, *list, p, nullptr);
            if (dynamic_cast<te::ExternalPlugin*> (p.get()) != nullptr)
                p->showWindowExplicitly();
        });
    }

    void showChainMenu()
    {
        auto* list = currentList();
        if (list == nullptr) return;

        PopupMenu m;
        m.addItem ("Save chain preset...", [this, list, track = currentTrack()]
        {
            Presets::askForName ("Save device chain", track != nullptr ? track->getName() : "Master chain",
                                 [list, track] (const String& name) { Presets::saveChain (*list, track, name); });
        });
        auto files = Presets::chainPresets();
        if (! files.isEmpty())
        {
            m.addSeparator();
            m.addSectionHeader ("Load (replaces the current devices)");
            for (auto& f : files)
                m.addItem (f.getFileNameWithoutExtension(), [this, list, track = currentTrack(), f]
                {
                    evs.edit.getUndoManager().beginNewTransaction ("Load chain preset");
                    Presets::loadChain (*list, track, f);
                });
        }
        m.addSeparator();
        m.addItem ("Show presets folder", [] { Presets::chainFolder().revealToUser(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&chainBtn));
    }

    void movePlugin (te::Plugin& p, int dir)
    {
        auto idx = shown.indexOf (&p);
        auto other = shown[idx + dir];
        if (idx < 0 || other == nullptr) return;

        auto parent = p.state.getParent();
        parent.moveChild (parent.indexOf (p.state), parent.indexOf (other->state), &evs.edit.getUndoManager());
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (! showMaster)
            rebuild();
    }

    // Plugin lists have no public change broadcaster, so poll for add/remove/reorder.
    void timerCallback() override
    {
        if (visiblePlugins() != shown)
            rebuild();
    }

    struct Rack : public Component {};

    EditViewState& evs;
    Viewport viewport;
    Rack rack;
    OwnedArray<Component> cards;
    Array<te::Plugin*> shown;
    TextButton addBtn { "+ Add device" }, masterBtn { "Master bus" }, chainBtn { "Chain presets" };
    Label heading;
    bool showMaster = false, dropHighlight = false;
};
