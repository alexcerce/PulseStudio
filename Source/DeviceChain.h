#pragma once

#include "EditViewState.h"
#include "PluginMenu.h"
#include "Routing.h"
#include "ClipImport.h"
#include "Presets.h"
#include "Macros.h"
#include "MidiLearn.h"
#include "DrumRack.h"
#include "DragDrop.h"
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

    /** Cards for a device can be dragged by their header: to reorder the chain, or onto a track
        header in the arrangement to move it there (Ctrl copies). */
    te::Plugin::Ptr dragPlugin;

    /** Called when a card changes its own width (so the chain can lay out again). */
    std::function<void()> onSizeChanged;
    void setWidthAndNotify (int w)
    {
        if (w == getWidth()) return;
        setSize (w, getHeight());
        if (onSizeChanged) MessageManager::callAsync ([sp = SafePointer<CardBase> (this)] { if (sp != nullptr && sp->onSizeChanged) sp->onSizeChanged(); });
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragPlugin == nullptr || dragStarted || e.getMouseDownY() > headerH || e.getDistanceFromDragStart() < 6) return;
        dragStarted = true;
        DragDrop::start (*this, DragDrop::devicePrefix + dragPlugin->itemID.toString(), dragPlugin->getName(), Theme::accent);
    }
    void mouseUp (const MouseEvent&) override { dragStarted = false; }
    bool dragStarted = false;

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
/** Settings tools for Pulse devices: snapshots for A/B, randomise, reset, copy / paste. */
namespace DeviceTools
{
    using Values = std::vector<float>;

    inline Values capture (pulse::PulsePlugin& p)
    {
        Values v;
        for (int i = 0; i < p.numParams(); ++i) v.push_back (p.get (i));
        return v;
    }

    inline void apply (pulse::PulsePlugin& p, const Values& v)
    {
        for (int i = 0; i < jmin (p.numParams(), (int) v.size()); ++i)
            if (std::abs (p.get (i) - v[(size_t) i]) > 1.0e-7f) p.set (i, v[(size_t) i]);
    }

    /** Parameters left alone by Randomise (levels and routing, so it can't blast or silence you). */
    inline bool keepsOnRandomise (te::AutomatableParameter& prm)
    {
        auto n = prm.getParameterName().toLowerCase();
        for (auto word : { "gain", "volume", "level", "dry/wet", "mix", "output", "receive", "choke", "makeup", "freeze", "mute", "solo", "sync" })
            if (n.contains (word)) return true;
        return false;
    }

    inline void randomise (pulse::PulsePlugin& p, Random& rng, float amount = 1.0f)
    {
        for (int i = 0; i < p.numParams(); ++i)
        {
            auto* prm = p.param (i);
            if (keepsOnRandomise (*prm)) continue;
            const float cur = prm->valueRange.convertTo0to1 (p.get (i));
            const float target = jlimit (0.0f, 1.0f, cur + (rng.nextFloat() - cur) * amount);
            p.set (i, prm->valueRange.snapToLegalValue (prm->valueRange.convertFrom0to1 (target)));
        }
    }

    inline void resetToDefaults (pulse::PulsePlugin& p)
    {
        for (int i = 0; i < p.numParams(); ++i)
            if (auto def = p.param (i)->getDefaultValue()) p.set (i, *def);
    }

    /** Copy / paste between devices of the same type. */
    struct Clipboard { String type; Values values; };
    inline Clipboard& clipboard() { static Clipboard c; return c; }
}

#include "DeviceEditor.h"

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
        title.setTooltip ("Drag the header to move this device (onto a track header to move it to that track; Ctrl copies)");
        dragPlugin = plugin;

        editBtn.onClick  = [this] { openEditor(); };
        restartBtn.onClick = [this] { if (auto sb = dynamic_cast<pulse::SandboxedPlugin*> (plugin.get())) { sb->restart(); updateSandboxStatus(); } };
        restartBtn.setTooltip ("Restart the plugin's helper process");
        leftBtn.onClick  = [this] { move (*plugin, -1); };
        rightBtn.onClick = [this] { move (*plugin, 1); };
        removeBtn.onClick = [this] { plugin->deleteFromParent(); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        presetBtn.setTooltip ("Device presets");
        editBtn.setTooltip (dynamic_cast<te::ExternalPlugin*> (plugin.get()) != nullptr ? "Open plugin window" : "Open all parameters");

        for (auto* c : std::initializer_list<Component*> { &power, &title, &presetBtn, &editBtn, &leftBtn, &rightBtn, &removeBtn, &collapseBtn })
            addAndMakeVisible (c);
        collapseBtn.setFlat (true);
        collapseBtn.onClick = [this] { setCollapsed (! collapsed); };
        if (dynamic_cast<pulse::PulsePlugin*> (plugin.get()) != nullptr)
        {
            abBtn.setTooltip ("A/B compare: switch between two versions of this device's settings");
            abBtn.onClick = [this] { switchAB(); };
            addAndMakeVisible (abBtn);
        }
        for (auto* b : { &presetBtn, &leftBtn, &rightBtn, &removeBtn }) b->setFlat (true);
        addChildComponent (restartBtn);
        restartBtn.setColour (TextButton::buttonColourId, Theme::red.withAlpha (0.6f));

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
            if ((bool) plugin->state.getProperty ("pulseCollapsed")) MessageManager::callAsync ([sp = SafePointer<PluginCard> (this)] { if (sp != nullptr) sp->setCollapsed (true); });
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
        if ((bool) plugin->state.getProperty ("pulseCollapsed")) MessageManager::callAsync ([sp = SafePointer<PluginCard> (this)] { if (sp != nullptr) sp->setCollapsed (true); });
    }

    void paint (Graphics& g) override
    {
        paintCard (g, plugin->isSynth() ? Theme::orange : Theme::accent, plugin->isEnabled());
        if (collapsed)   // name written down the strip
        {
            g.setColour (Theme::text);
            g.setFont (FontOptions (12.5f, Font::bold));
            Graphics::ScopedSaveState ss (g);
            g.addTransform (AffineTransform::rotation (-MathConstants<float>::halfPi, 0.0f, 0.0f).translated (8.0f, (float) getHeight() - 10.0f));
            g.drawText (plugin->getName(), 0, 0, getHeight() - 70, 18, Justification::centredLeft, true);
        }
    }

    bool isCollapsed() const { return collapsed; }

    /** Shows a page of a paged device card (also used by the self-test). */
    void showPage (int i) { currentPage = jlimit (0, jmax (0, (int) pages.size() - 1), i); resized(); }
    int numPages() const { return (int) pages.size(); }

    /** Folds the card to a slim strip (stored in the project). */
    void setCollapsed (bool c)
    {
        if (c == collapsed) return;
        collapsed = c;
        plugin->state.setProperty ("pulseCollapsed", c, nullptr);
        collapseBtn.setIcon (c ? Icons::Type::chevronRight : Icons::Type::chevronLeft);
        collapseBtn.setTooltip (c ? "Expand" : "Collapse");
        if (c)
        {
            hiddenByCollapse.clear();
            for (auto* ch : getChildren())
                if (ch != &power && ch != &collapseBtn && ch->isVisible()) { hiddenByCollapse.add (ch); ch->setVisible (false); }
            expandedWidth = getWidth();
            setWidthAndNotify (34);
        }
        else
        {
            for (auto* ch : hiddenByCollapse) ch->setVisible (true);
            hiddenByCollapse.clear();
            setWidthAndNotify (sections.empty() ? jmax (290, expandedWidth) : computeWidth (getHeight()));
        }
        resized();
        repaint();
    }

    void switchAB()
    {
        auto* pp = dynamic_cast<pulse::PulsePlugin*> (plugin.get());
        if (pp == nullptr) return;
        abSlots[(size_t) abIndex] = DeviceTools::capture (*pp);
        abIndex = 1 - abIndex;
        if (abSlots[(size_t) abIndex].empty()) abSlots[(size_t) abIndex] = abSlots[(size_t) (1 - abIndex)];
        plugin->edit.getUndoManager().beginNewTransaction ("A/B compare");
        DeviceTools::apply (*pp, abSlots[(size_t) abIndex]);
        abBtn.setButtonText (abIndex == 0 ? "A" : "B");
        abBtn.setToggleState (abIndex == 1, dontSendNotification);
    }

    void resized() override
    {
        if (collapsed)
        {
            auto r = getLocalBounds().reduced (6, 6);
            power.setBounds (r.removeFromTop (22));
            r.removeFromTop (4);
            collapseBtn.setBounds (r.removeFromTop (22));
            return;
        }
        auto r = getLocalBounds().reduced (6, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (2);
        collapseBtn.setBounds (head.removeFromLeft (20));
        head.removeFromLeft (4);
        removeBtn.setBounds (head.removeFromRight (24)); head.removeFromRight (2);
        rightBtn.setBounds (head.removeFromRight (24));  head.removeFromRight (2);
        leftBtn.setBounds (head.removeFromRight (24));   head.removeFromRight (4);
        editBtn.setBounds (head.removeFromRight (38));   head.removeFromRight (2);
        presetBtn.setBounds (head.removeFromRight (24));
        if (abBtn.isVisible()) { head.removeFromRight (2); abBtn.setBounds (head.removeFromRight (26)); }
        if (scBtn.isVisible()) { head.removeFromRight (2); scBtn.setBounds (head.removeFromRight (30)); }
        if (restartBtn.isVisible()) { head.removeFromRight (2); restartBtn.setBounds (head.removeFromRight (58)); }
        title.setBounds (head);

        r.removeFromTop (6);

        if (! sections.empty())
        {
            layoutPulse (r);
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

    void mouseDoubleClick (const MouseEvent&) override { openEditor(); }

private:
    void openEditor()
    {
        if (auto sb = dynamic_cast<pulse::SandboxedPlugin*> (plugin.get())) sb->showEditor();
        else if (auto pp = dynamic_cast<pulse::PulsePlugin*> (plugin.get())) DeviceEditors::show (evs, *pp);
        else plugin->showWindowExplicitly();
    }

    void updateSandboxStatus()
    {
        auto sb = dynamic_cast<pulse::SandboxedPlugin*> (plugin.get());
        if (sb == nullptr) return;
        sb->syncParametersFromPlugin();
        restartBtn.setVisible (sb->hasCrashed() || ! sb->isLoaded());
        title.setText (sb->getName() + (sb->hasCrashed() ? "  (crashed)" : "  (sandboxed)"), dontSendNotification);
        title.setColour (Label::textColourId, sb->hasCrashed() ? Theme::red : Theme::text);
        title.setTooltip (sb->getStatus());
    }

    void timerCallback() override
    {
        updateSandboxStatus();
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
        if (auto& factory = FactoryPresets::forType (plugin->getPluginType()); ! factory.empty())
        {
            PopupMenu f;
            for (auto& pr : factory)
                f.addItem (pr.name, [p = plugin, name = String (pr.name)]
                {
                    p->edit.getUndoManager().beginNewTransaction ("Factory preset");
                    FactoryPresets::apply (*p, name);
                });
            m.addSubMenu ("Factory presets", f);
        }
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
        if (auto ext = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            m.addItem ("Run in its own process (sandbox)", [p = plugin, ext]
            {
                p->edit.getUndoManager().beginNewTransaction ("Sandbox plugin");
                sandbox::moveToSandbox (*ext);
            });
        if (auto sb = dynamic_cast<pulse::SandboxedPlugin*> (plugin.get()))
            m.addItem ("Run inside Pulse (no sandbox)", [p = plugin, sb]
            {
                p->edit.getUndoManager().beginNewTransaction ("Unsandbox plugin");
                sandbox::moveInProcess (*sb);
            });
        m.addItem ("Show presets folder", [p = plugin] { Presets::deviceFolder (*p).revealToUser(); });
        if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (plugin.get()))
        {
            m.addSeparator();
            m.addItem ("Randomise", [p = plugin, pp] { p->edit.getUndoManager().beginNewTransaction ("Randomise"); Random r; DeviceTools::randomise (*pp, r); });
            m.addItem ("Randomise a little", [p = plugin, pp] { p->edit.getUndoManager().beginNewTransaction ("Randomise"); Random r; DeviceTools::randomise (*pp, r, 0.2f); });
            m.addItem ("Reset to defaults", [p = plugin, pp] { p->edit.getUndoManager().beginNewTransaction ("Reset device"); DeviceTools::resetToDefaults (*pp); });
            m.addItem ("Copy settings", [p = plugin, pp] { DeviceTools::clipboard() = { p->getPluginType(), DeviceTools::capture (*pp) }; });
            const bool canPaste = DeviceTools::clipboard().type == plugin->getPluginType() && ! DeviceTools::clipboard().values.empty();
            m.addItem ("Paste settings", canPaste, false, [p = plugin, pp]
            {
                p->edit.getUndoManager().beginNewTransaction ("Paste settings");
                DeviceTools::apply (*pp, DeviceTools::clipboard().values);
            });
        }
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
        displayWidth = jmin (pp.getDisplayWidth(), 220);
        display = DeviceDisplays::create (evs, pp);
        if (display != nullptr)
            addAndMakeVisible (*display);

        const auto accent = plugin->isSynth() ? Theme::orange : Theme::accent;
        for (auto& sec : pp.getSections())
        {
            PulseSection ps;
            ps.name = sec.name;
            ps.label = std::make_unique<Label> (String(), sec.name.toUpperCase());
            ps.label->setFont (FontOptions (10.0f, Font::bold));
            ps.label->setColour (Label::textColourId, Theme::muted);
            ps.label->setJustificationType (Justification::centredLeft);
            addAndMakeVisible (*ps.label);
            for (auto idx : sec.params)
            {
                auto* k = new ParamKnob (evs, *pp.param (idx));
                k->setCompact (accent);
                knobs.add (k);
                // drop the section prefix ("Osc 1 Position" in "Osc 1" -> "Position")
                auto name = k->name.getText();
                if (name.startsWithIgnoreCase (sec.name + " ")) k->name.setText (name.substring (sec.name.length() + 1), dontSendNotification);
                addAndMakeVisible (k);
                ps.knobs.push_back (k);
            }
            sections.push_back (std::move (ps));
        }
        setSize (jmax (220, computeWidth (300)), 0);
    }

    //==============================================================================
    // Compact layout: knobs stack in as many rows as fit; sections that don't fit side by side
    // go on pages with tabs.
    struct PulseSection { String name; std::unique_ptr<Label> label; std::vector<Component*> knobs; int cols = 1; };
    struct Page { std::vector<int> sections; int width = 0; };
    std::vector<PulseSection> sections;
    std::vector<Page> pages;
    OwnedArray<TextButton> pageTabs;
    int currentPage = 0, layoutRows = 2;
    std::unique_ptr<Component> display;
    int displayWidth = 0;
    static constexpr int sectionGap = 10, cKnobW = 54, cKnobH = 64, maxPageW = 6 * 54 + 2 * 10, tabH = 20;

    int rowsFor (int height, bool withTabs) const
    {
        const int avail = height - headerH - 6 - 6 - 14 - (withTabs ? tabH + 4 : 0);
        return jlimit (1, 4, avail / cKnobH);
    }

    /** Groups the sections into pages for `rows` rows. Returns the widest page. */
    int buildPages (int rows)
    {
        pages.clear();
        Page page;
        for (int i = 0; i < (int) sections.size(); ++i)
        {
            auto& sec = sections[(size_t) i];
            sec.cols = jmax (1, ((int) sec.knobs.size() + rows - 1) / rows);
            const int w = sec.cols * cKnobW;
            if (! page.sections.empty() && page.width + sectionGap + w > maxPageW)
            {
                pages.push_back (page);
                page = {};
            }
            page.width += (page.sections.empty() ? 0 : sectionGap) + w;
            page.sections.push_back (i);
        }
        if (! page.sections.empty()) pages.push_back (page);
        int widest = 0;
        for (auto& p : pages) widest = jmax (widest, p.width);
        return widest;
    }

    int computeWidth (int height)
    {
        if (sections.empty()) return getWidth();
        layoutRows = rowsFor (height, false);
        int widest = buildPages (layoutRows);
        if (pages.size() > 1)   // tabs take some height: maybe fewer rows
        {
            layoutRows = rowsFor (height, true);
            widest = buildPages (layoutRows);
        }
        currentPage = jlimit (0, (int) pages.size() - 1, currentPage);
        rebuildTabs();
        return 16 + (display != nullptr ? displayWidth + 8 : 0) + jmax (widest, pages.size() > 1 ? 200 : 0);
    }

    void rebuildTabs()
    {
        if (pageTabs.size() == (int) pages.size() || pages.size() <= 1)
        {
            if (pages.size() <= 1) pageTabs.clear();
            return;
        }
        pageTabs.clear();
        for (int i = 0; i < (int) pages.size(); ++i)
        {
            StringArray names;
            for (int s : pages[(size_t) i].sections) names.add (sections[(size_t) s].name);
            auto* t = pageTabs.add (new TextButton (names.joinIntoString (" / ")));
            t->setTooltip (names.joinIntoString (", "));
            t->onClick = [this, i] { currentPage = i; resized(); repaint(); };
            addAndMakeVisible (t);
        }
    }

    void layoutPulse (Rectangle<int> r)
    {
        const int w = computeWidth (getHeight());
        if (w != getWidth()) setWidthAndNotify (w);
        r.removeFromBottom (4);
        if (display != nullptr)
        {
            display->setBounds (r.removeFromLeft (displayWidth));
            r.removeFromLeft (8);
        }
        if (pages.size() > 1)
        {
            auto tabs = r.removeFromTop (tabH);
            const int tw = tabs.getWidth() / (int) pages.size();
            for (int i = 0; i < pageTabs.size(); ++i)
            {
                pageTabs[i]->setBounds (tabs.removeFromLeft (tw).reduced (1, 0));
                pageTabs[i]->setToggleState (i == currentPage, dontSendNotification);
                pageTabs[i]->setColour (TextButton::buttonOnColourId, (plugin->isSynth() ? Theme::orange : Theme::accent).withAlpha (0.45f));
            }
            r.removeFromTop (4);
        }
        // hide everything, then show the current page
        for (auto& sec : sections)
        {
            sec.label->setVisible (false);
            for (auto* k : sec.knobs) k->setVisible (false);
        }
        if (pages.empty()) return;
        if (auto* pd = dynamic_cast<Graphs::PagedDisplay*> (display.get()))
        {
            StringArray visible;
            for (int si : pages[(size_t) currentPage].sections) visible.add (sections[(size_t) si].name);
            pd->showSections (visible);
        }
        for (int si : pages[(size_t) currentPage].sections)
        {
            auto& sec = sections[(size_t) si];
            auto area = r.removeFromLeft (sec.cols * cKnobW);
            r.removeFromLeft (sectionGap);
            sec.label->setVisible (true);
            sec.label->setBounds (area.removeFromTop (14));
            for (int i = 0; i < (int) sec.knobs.size(); ++i)
            {
                sec.knobs[(size_t) i]->setVisible (true);
                sec.knobs[(size_t) i]->setBounds (area.getX() + (i / layoutRows) * cKnobW, area.getY() + (i % layoutRows) * cKnobH, cKnobW, cKnobH);
            }
        }
    }

    static constexpr int knobW = 64, knobH = 78;
    EditViewState& evs;
    te::Plugin::Ptr plugin;
    std::function<void (te::Plugin&, int)> move;
    IconButton power { Icons::Type::power, "Device on / off", Theme::green },
               leftBtn { Icons::Type::chevronLeft, "Move left" }, rightBtn { Icons::Type::chevronRight, "Move right" },
               removeBtn { Icons::Type::close, "Remove device" }, presetBtn { Icons::Type::menu, "Device presets" };
    TextButton editBtn { "Edit" }, scBtn { "SC" }, restartBtn { "Restart" }, abBtn { "A" };
    IconButton collapseBtn { Icons::Type::chevronLeft, "Collapse" };
    bool collapsed = false;
    int expandedWidth = 290, abIndex = 0;
    std::array<DeviceTools::Values, 2> abSlots;
    Array<Component*> hiddenByCollapse;
    Label title, more;
    OwnedArray<Component> knobs;   // state controls, the Delay's Sync button, then parameter knobs
};

//==============================================================================
/** One modulator (LFO, step, envelope follower...) with its controls and a live output meter. */
class ModulatorCard : public CardBase,
                      private Timer
{
public:
    ModulatorCard (EditViewState& e, te::Track& t, te::Modifier& m) : evs (e), track (&t), mod (&m)
    {
        title.setText (Modulators::nameOf (t, m), dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        title.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (title);
        removeBtn.setFlat (true);
        removeBtn.onClick = [this] { evs.edit.getUndoManager().beginNewTransaction ("Remove modulator"); mod->remove(); };
        addAndMakeVisible (removeBtn);
        for (auto p : m.getAutomatableParameters())
        {
            if (knobs.size() >= 8) break;
            auto* k = knobs.add (new ParamKnob (evs, *p));
            k->formatter = formatterFor (p->getParameterName());
            addAndMakeVisible (k);
        }
        setSize (jmax (2, (knobs.size() + 1) / 2) * 64 + 16, 0);
        startTimerHz (30);
    }

    void paint (Graphics& g) override
    {
        paintCard (g, Colour (0xffb197fc), true);
        // live output
        auto bar = Rectangle<float> (8.0f, (float) headerH - 8.0f, (float) getWidth() - 16.0f, 3.0f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (bar, 1.5f);
        g.setColour (Colour (0xffb197fc));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, mod->getCurrentValue())), 1.5f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        removeBtn.setBounds (head.removeFromRight (24));
        title.setBounds (head);
        r.removeFromTop (6);
        const int cols = jmax (1, (knobs.size() + 1) / 2);
        for (int i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds (r.getX() + (i % cols) * 64, r.getY() + (i / cols) * 78, 64, 78);
    }

private:
    /** Readable values for the modifiers' choice and on/off controls. */
    static std::function<String (float)> formatterFor (const String& name)
    {
        auto pick = [] (StringArray names) { return [names] (float v) { return names[jlimit (0, names.size() - 1, roundToInt (v))]; }; };
        auto n = name.toLowerCase();
        if (n == "wave")                         return pick (te::LFOModifier::getWaveNames());
        if (n.contains ("rate type"))            return pick (te::ModifierCommon::getRateTypeChoices());
        if (n.contains ("sync"))                 return pick ({ "Free", "Transport", "Note" });
        if (n == "enabled" || n == "bipolar")    return pick ({ "Off", "On" });
        if (n == "depth" || n == "phase" || n == "offset" || n == "smooth" || n == "speed")
            return [] (float v) { return String (roundToInt (v * 100.0f)) + "%"; };
        return {};
    }

    void timerCallback() override
    {
        repaint (0, 0, getWidth(), headerH);
        auto targets = Modulators::targetsOf (*track, *mod);
        title.setTooltip (targets.isEmpty() ? "Not connected yet: right-click any device knob > Modulate with"
                                            : "Modulates: " + targets.joinIntoString (", "));
    }

    EditViewState& evs;
    te::Track::Ptr track;
    te::Modifier::Ptr mod;
    Label title;
    IconButton removeBtn { Icons::Type::close, "Remove modulator" };
    OwnedArray<ParamKnob> knobs;
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
        hint.setText ("Right-click any device knob > Map to Macro", dontSendNotification);
        hint.setFont (FontOptions (12.0f));
        hint.setColour (Label::textColourId, Theme::muted);
        hint.setJustificationType (Justification::topLeft);
        addAndMakeVisible (hint);
        build();
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
        // compact until a macro exists
        const int w = knobs.isEmpty() ? 150 : 4 * 64 + 16;
        if (getWidth() != w)
        {
            setSize (w, getHeight());
            if (auto* p = getParentComponent()) p->resized();
        }
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
#include "DrumRackCard.h"

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
        title.setInterceptsMouseClicks (false, false);
        dragPlugin = holder;
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
        modBtn.setTooltip ("Add an LFO, step sequencer, envelope follower... then right-click any knob > Modulate with");
        modBtn.onClick = [this]
        {
            auto* at = dynamic_cast<te::AudioTrack*> (currentTrack());
            if (at == nullptr) return;
            PopupMenu m;
            for (auto& ty : Modulators::types())
                m.addItem (ty.name, [this, id = ty.id, t = te::Track::Ptr (at)]
                {
                    evs.edit.getUndoManager().beginNewTransaction ("Add modulator");
                    Modulators::add (*t, id);
                });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&modBtn));
        };
        addAndMakeVisible (modBtn);

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
        return (ClipImport::isPluginDescription (d.description) || DragDrop::is (d.description, DragDrop::devicePrefix)) && currentList() != nullptr;
    }

    void itemDragEnter (const SourceDetails& d) override { dropHighlight = true; itemDragMove (d); }
    void itemDragExit (const SourceDetails&) override    { dropHighlight = false; insertX = -1; repaint(); }

    void itemDragMove (const SourceDetails& d) override
    {
        auto [before, x] = insertionAt (d.localPosition);
        ignoreUnused (before);
        if (x != insertX) { insertX = x; repaint(); }
    }

    void itemDropped (const SourceDetails& d) override
    {
        dropHighlight = false;
        auto [before, x] = insertionAt (d.localPosition);
        ignoreUnused (x);
        insertX = -1;
        repaint();
        auto* list = currentList();
        if (list == nullptr) return;
        if (DragDrop::is (d.description, DragDrop::devicePrefix))
        {
            if (auto p = DragDrop::pluginFrom (evs.edit, d.description))
                DragDrop::placeDevice (*p, currentTrack(), *list, before, ModifierKeys::currentModifiers.isCtrlDown());
            return;
        }
        evs.edit.getUndoManager().beginNewTransaction ("Add device");
        Presets::applyDescription (currentTrack(), *list, d.description.toString());
        if (before != nullptr)   // browser devices land where they were dropped
            if (auto added = list->getPlugins().getLast().get(); added != nullptr && added != before)
            {
                auto at = dynamic_cast<te::AudioTrack*> (currentTrack());
                auto newest = (at != nullptr && at->getVolumePlugin() != nullptr) ? (*list)[list->indexOf (at->getVolumePlugin()) - 1] : added;
                if (newest != nullptr && newest != before) DragDrop::placeDevice (*newest, currentTrack(), *list, before, false);
            }
    }

    /** The device a drop at this point goes before (nullptr = the end) and the x of the insertion line. */
    std::pair<te::Plugin*, int> insertionAt (juce::Point<int> pos)
    {
        auto p = rack.getLocalPoint (this, pos);
        for (auto& [card, plugin] : pluginCards)
            if (p.x < card->getBounds().getCentreX())
                return { plugin, getLocalPoint (&rack, juce::Point<int> (card->getX() - 4, 0)).x };
        const int endX = pluginCards.empty() ? addBtn.getX() - 4 : pluginCards.back().first->getRight() + 4;
        return { nullptr, getLocalPoint (&rack, juce::Point<int> (endX, 0)).x };
    }

    void paintOverChildren (Graphics& g) override
    {
        if (dropHighlight)
        {
            g.setColour (Theme::accent.withAlpha (0.5f));
            g.drawRect (getLocalBounds(), 1);
        }
        if (insertX >= 0)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle ((float) insertX - 1.5f, 34.0f, 3.0f, (float) getHeight() - 40.0f, 1.5f);
        }
    }

    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    /** Collapses every device card (or expands them all if they're all collapsed). */
    void toggleCollapseAll()
    {
        bool allCollapsed = true;
        for (auto* c : cards) if (auto* pc = dynamic_cast<PluginCard*> (c)) allCollapsed = allCollapsed && pc->isCollapsed();
        for (auto* c : cards) if (auto* pc = dynamic_cast<PluginCard*> (c)) pc->setCollapsed (! allCollapsed);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (30).reduced (8, 4);
        masterBtn.setBounds (top.removeFromRight (110));
        top.removeFromRight (6);
        chainBtn.setBounds (top.removeFromRight (100));
        top.removeFromRight (6);
        modBtn.setBounds (top.removeFromRight (100));
        heading.setBounds (top);
        viewport.setBounds (r);
        layoutRack();
    }

    /** Shows the master's chain (true) or the selected track's (false). */
    void setShowMaster (bool m)
    {
        masterBtn.setToggleState (m, dontSendNotification);
        if (m != showMaster) { showMaster = m; rebuild(); }
    }
    bool isShowingMaster() const { return showMaster; }

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
                    || dynamic_cast<pulse::LoudnessProbe*> (p) != nullptr
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
        pluginCards.clear();
        shown = visiblePlugins();

        auto* at = dynamic_cast<te::AudioTrack*> (currentTrack());
        if (at != nullptr && ! Routing::isReturnTrack (*at))
        {
            rack.addAndMakeVisible (cards.add (new MacrosCard (evs, *at)));
            for (auto m : Modulators::all (*at))
                rack.addAndMakeVisible (cards.add (new ModulatorCard (evs, *at, *m)));
        }
        shownModifiers = at != nullptr ? Modulators::all (*at).size() : 0;

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
            pluginCards.push_back ({ c, p });
            if (auto* cb = dynamic_cast<CardBase*> (c)) cb->onSizeChanged = [this] { layoutRack(); };
        }

        if (showMaster)
            heading.setText ("Master", dontSendNotification);
        else if (auto t = currentTrack())
            heading.setText (t->getName(), dontSendNotification);
        else
            heading.setText ("Select a track to see its devices", dontSendNotification);

        addBtn.setEnabled (currentList() != nullptr);
        chainBtn.setEnabled (currentList() != nullptr);
        modBtn.setEnabled (at != nullptr && ! Routing::isReturnTrack (*at));
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
        auto* curAt = dynamic_cast<te::AudioTrack*> (currentTrack());
        if (visiblePlugins() != shown || (curAt != nullptr && Modulators::all (*curAt).size() != shownModifiers))
            rebuild();
    }

    struct Rack : public Component {};

    EditViewState& evs;
    Viewport viewport;
    Rack rack;
    OwnedArray<Component> cards;
    Array<te::Plugin*> shown;
    TextButton addBtn { "+ Add device" }, masterBtn { "Master" }, chainBtn { "Chain presets" }, modBtn { "+ Modulator" };
    int shownModifiers = 0;
    Label heading;
    bool showMaster = false, dropHighlight = false;
    std::vector<std::pair<Component*, te::Plugin*>> pluginCards;
    int insertX = -1;
};
