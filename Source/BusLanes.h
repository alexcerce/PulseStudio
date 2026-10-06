#pragma once

#include "EditViewState.h"
#include "PluginMenu.h"
#include "Presets.h"
#include "DragDrop.h"
#include "Meters.h"
#include "Routing.h"
#include "Loudness.h"
#include "AutomationLane.h"

//==============================================================================
/** Slim rows pinned under the arrangement for every return track and the master: click a row to
    open its devices, drop devices on it, add one with +, and see / drag / open the devices it holds. */
namespace BusLanes
{
    /** Devices a user put on a bus (hides the return input, fader, meter and loudness probe). */
    inline Array<te::Plugin*> userDevices (te::PluginList& list)
    {
        Array<te::Plugin*> out;
        for (auto p : list)
        {
            if (dynamic_cast<te::AuxReturnPlugin*> (p) != nullptr || dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr
                || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr || p->getPluginType() == pulse::LoudnessProbe::xmlTypeName)
                continue;
            out.add (p);
        }
        return out;
    }

    //==============================================================================
    class Row : public Component,
                public DragAndDropTarget,
                private Timer
    {
    public:
        static constexpr int height = 26;

        /** track == nullptr: the master. */
        Row (EditViewState& e, te::AudioTrack* t, int headerW) : evs (e), track (t), headerWidth (headerW)
        {
            name.setText (track != nullptr ? track->getName() : String ("Master"), dontSendNotification);
            name.setFont (FontOptions (11.5f, Font::bold));
            name.setMinimumHorizontalScale (0.7f);
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);

            autoBtn.setClickingTogglesState (false);
            autoBtn.setTooltip ("Show / hide an automation lane for " + name.getText());
            autoBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
            autoBtn.onClick = [this]
            {
                if (auto* t = automationTrack())
                    evs.setAutomationVisible (*t, ! evs.isAutomationVisible (*t));
            };
            addAndMakeVisible (autoBtn);
            addBtn.setTooltip ("Add a device to " + name.getText());
            addBtn.onClick = [this] { addDevice(); };
            addAndMakeVisible (addBtn);

            if (track != nullptr)
            {
                for (auto* b : { &muteBtn, &soloBtn })
                {
                    b->setClickingTogglesState (false);
                    addAndMakeVisible (b);
                }
                muteBtn.setColour (TextButton::buttonOnColourId, Theme::yellow);
                soloBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
                muteBtn.setTooltip ("Mute");
                soloBtn.setTooltip ("Solo");
                muteBtn.onClick = [this] { EngineHelpers::toggleMute (*track); };
                soloBtn.onClick = [this] { EngineHelpers::toggleSolo (*track); };
            }

            volume.setSliderStyle (Slider::LinearHorizontal);
            volume.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            volume.setPopupDisplayEnabled (true, true, nullptr);
            volume.setRange (-60.0, 6.0, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setTextValueSuffix (" dB");
            volume.setColour (Slider::trackColourId, colour().withAlpha (0.45f));
            volume.setColour (Slider::backgroundColourId, Theme::well);
            volume.setColour (Slider::textBoxTextColourId, Theme::text);
            volume.setTooltip ("Volume (double-click: 0 dB)");
            volume.onDragStart = [this] { evs.edit.getUndoManager().beginNewTransaction ("Volume"); };
            volume.onValueChange = [this] { if (auto vp = volumePlugin()) vp->setVolumeDb ((float) volume.getValue()); };
            if (auto vp = volumePlugin()) volume.setValue (vp->getVolumeDb(), dontSendNotification);
            addAndMakeVisible (volume);

            te::LevelMeterPlugin* lm = track != nullptr ? track->getLevelMeterPlugin()
                                                        : evs.edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>();
            if (lm != nullptr)
            {
                meter = std::make_unique<LevelMeter> (*lm, false);
                addAndMakeVisible (*meter);
            }
            setTooltip();
            rebuildChips();
            startTimerHz (5);
        }

        te::AudioTrack* getTrack() const { return track; }

        /** The track whose automation this row shows (the master track for the master row). */
        te::Track* automationTrack() const { return track != nullptr ? (te::Track*) track : (te::Track*) evs.edit.getMasterTrack(); }

        void paint (Graphics& g) override
        {
            const bool sel = isShowing() && isSelected();
            auto r = getLocalBounds();
            g.setColour (sel ? Theme::accentDim : Theme::header);
            g.fillRect (r);
            g.setColour (Theme::bg2);
            g.fillRect (r.withLeft (headerWidth));
            g.setColour (colour());
            g.fillRect (r.removeFromLeft (4));
            g.setColour (Theme::line);
            g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
            g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
            if (chips.isEmpty())
            {
                g.setColour (Theme::muted.withAlpha (0.5f));
                g.setFont (FontOptions (11.0f));
                g.drawText ("No devices - drop one here or click +", getLocalBounds().withLeft (headerWidth + 10), Justification::centredLeft);
            }
        }

        void paintOverChildren (Graphics& g) override
        {
            if (! dropHover) return;
            g.setColour (Theme::accent.withAlpha (0.18f));
            g.fillRect (getLocalBounds());
            g.setColour (Theme::accent);
            g.drawRect (getLocalBounds(), 2);
        }

        void resized() override
        {
            auto r = getLocalBounds().withWidth (headerWidth).reduced (8, 3).withTrimmedLeft (2);
            if (meter != nullptr) { meter->setBounds (r.removeFromRight (20).reduced (0, 6)); r.removeFromRight (3); }
            volume.setBounds (r.removeFromRight (44).reduced (0, 2)); r.removeFromRight (3);
            addBtn.setBounds (r.removeFromRight (18)); r.removeFromRight (2);
            autoBtn.setBounds (r.removeFromRight (18)); r.removeFromRight (2);
            if (track != nullptr)
            {
                soloBtn.setBounds (r.removeFromRight (18)); r.removeFromRight (2);
                muteBtn.setBounds (r.removeFromRight (18)); r.removeFromRight (4);
            }
            name.setBounds (r);

            int x = headerWidth + 6;
            for (auto* c : chips)
            {
                const int w = jlimit (60, 170, GlyphArrangement::getStringWidthInt (FontOptions (11.0f), c->getName()) + 22);
                c->setBounds (x, 3, w, getHeight() - 6);
                x += w + 4;
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (e.mods.isPopupMenu()) { showMenu(); return; }
            if (evs.showDevices) evs.showDevices (track);
        }

        // devices dropped here go to the end of this bus's chain
        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            return DragDrop::is (d.description, DragDrop::devicePrefix)
                   || (ClipImport::isPluginDescription (d.description) && ! d.description.toString().startsWith ("plugin:track:"));
        }
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            dropHover = false;
            repaint();
            auto& list = pluginList();
            if (DragDrop::is (d.description, DragDrop::devicePrefix))
            {
                if (auto p = DragDrop::pluginFrom (evs.edit, d.description))
                    DragDrop::placeDevice (*p, track, list, track == nullptr ? masterEnd() : nullptr, ModifierKeys::currentModifiers.isCtrlDown());
            }
            else
            {
                evs.edit.getUndoManager().beginNewTransaction ("Add device");
                Presets::applyDescription (track, list, d.description.toString());
            }
            if (evs.showDevices) evs.showDevices (track);
        }

    private:
        //==========================================================================
        /** A device on the bus: click = show the devices, double-click = open its editor,
            drag = move it (Ctrl copies), right-click = bypass / delete. */
        struct Chip : public Component
        {
            Chip (Row& o, te::Plugin& p) : owner (o), plugin (&p)
            {
                setName (p.getName());
                setTooltip (p.getName() + " - double-click to open, drag to move (Ctrl copies), right-click for more");
            }
            void setTooltip (const String& t) { tip = t; }
            String getTooltip() { return tip; }

            void paint (Graphics& g) override
            {
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                const bool on = plugin->isEnabled();
                const auto c = owner.colour();
                g.setColour ((on ? c.withAlpha (0.28f) : Theme::panel2).withMultipliedBrightness (hover ? 1.3f : 1.0f));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (on ? c : Theme::line);
                g.drawRoundedRectangle (r, 4.0f, 1.0f);
                g.setColour (on ? Theme::ink.withAlpha (0.92f) : Theme::muted);
                g.setFont (FontOptions (11.0f, Font::bold));
                g.drawText (getName(), r.reduced (8.0f, 0.0f), Justification::centredLeft, true);
            }
            void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
            void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }
            void mouseDown (const MouseEvent& e) override
            {
                dragging = false;
                if (e.mods.isPopupMenu())
                {
                    PopupMenu m;
                    te::Plugin::Ptr p = plugin;
                    m.addItem ("Open editor", [this, p] { if (owner.evs.openDeviceEditor) owner.evs.openDeviceEditor (*p); });
                    m.addItem (p->isEnabled() ? "Bypass" : "Turn on", [p] { p->setEnabled (! p->isEnabled()); });
                    m.addSeparator();
                    m.addItem ("Delete", [p] { p->edit.getUndoManager().beginNewTransaction ("Delete device"); p->deleteFromParent(); });
                    SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this));
                    return;
                }
                if (owner.evs.showDevices) owner.evs.showDevices (owner.track);
            }
            void mouseDrag (const MouseEvent& e) override
            {
                if (dragging || e.getDistanceFromDragStart() < 6) return;
                dragging = true;
                DragDrop::start (*this, DragDrop::devicePrefix + plugin->itemID.toString(), plugin->getName(), owner.colour());
            }
            void mouseDoubleClick (const MouseEvent&) override { if (owner.evs.openDeviceEditor) owner.evs.openDeviceEditor (*plugin); }

            Row& owner;
            te::Plugin::Ptr plugin;
            String tip;
            bool hover = false, dragging = false;
        };

        te::PluginList& pluginList() { return track != nullptr ? track->pluginList : evs.edit.getMasterPluginList(); }

        te::VolumeAndPanPlugin* volumePlugin() const
        {
            return track != nullptr ? track->getVolumePlugin() : evs.edit.getMasterVolumePlugin().get();
        }

        /** Master devices go before the master meter so it shows their result. */
        te::Plugin* masterEnd() { return evs.edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>(); }

        Colour colour() const { return track != nullptr ? track->getColour() : Theme::accent; }

        bool isSelected() const
        {
            if (evs.isShowingDevicesOf) return evs.isShowingDevicesOf (track);
            return track != nullptr && evs.selectionManager.isSelected (track);
        }

        void setTooltip()
        {
            const auto tip = String ("Click to show ") + (track != nullptr ? "this return's" : "the master's") + " devices; drop devices here; right-click for more";
            name.setTooltip (tip);
        }

        void addDevice()
        {
            PluginMenu::show (evs.edit, &addBtn, [this] (te::Plugin::Ptr p)
            {
                evs.edit.getUndoManager().beginNewTransaction ("Add device");
                auto& list = pluginList();
                if (! PluginMenu::insertPlugin (track, list, p, nullptr)) return;
                if (evs.showDevices) evs.showDevices (track);
                if (evs.openDeviceEditor && dynamic_cast<te::ExternalPlugin*> (p.get()) != nullptr) evs.openDeviceEditor (*p);
            }, PluginMenu::Allowed { false, false });
        }

        void showMenu()
        {
            PopupMenu m;
            m.addItem ("Show devices", [this] { if (evs.showDevices) evs.showDevices (track); });
            m.addItem ("Add device...", [this] { addDevice(); });
            if (track != nullptr)
            {
                m.addSeparator();
                m.addItem ("Rename...", [this]
                {
                    auto* w = new AlertWindow ("Rename return", {}, MessageBoxIconType::NoIcon);
                    w->addTextEditor ("name", track->getName());
                    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
                    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
                    te::Track::Ptr t = track;
                    w->enterModalState (true, ModalCallbackFunction::create ([w, t] (int r)
                    {
                        const auto n = w->getTextEditorContents ("name").trim();
                        if (r == 1 && n.isNotEmpty()) t->setName (n);
                    }), true);
                });
                m.addItem ("Delete return", [this]
                {
                    evs.edit.getUndoManager().beginNewTransaction ("Delete return");
                    evs.edit.deleteTrack (track);
                });
            }
            SearchMenu::withSearch (m).showMenuAsync ({});
        }

        void rebuildChips()
        {
            chips.clear();
            for (auto p : userDevices (pluginList()))
                addAndMakeVisible (chips.add (new Chip (*this, *p)));
            lastSig = signature();
            resized();
            repaint();
        }

        String signature()
        {
            String s = name.getText();
            for (auto p : userDevices (pluginList())) s << p->itemID.toString() << (p->isEnabled() ? "+" : "-") << p->getName();
            return s;
        }

        void timerCallback() override
        {
            if (track != nullptr)
            {
                if (name.getText() != track->getName()) name.setText (track->getName(), dontSendNotification);
                muteBtn.setToggleState (track->isMuted (false), dontSendNotification);
                soloBtn.setToggleState (track->isSolo (false), dontSendNotification);
            }
            if (signature() != lastSig) rebuildChips();
            if (auto* t = automationTrack()) autoBtn.setToggleState (evs.isAutomationVisible (*t), dontSendNotification);
            if (auto vp = volumePlugin(); vp != nullptr && ! volume.isMouseButtonDown())
                volume.setValue (vp->getVolumeDb(), dontSendNotification);
            const bool sel = isSelected();
            if (sel != wasSelected) { wasSelected = sel; repaint(); }
        }

        EditViewState& evs;
        te::AudioTrack* track;
        te::Track::Ptr holder { track };
        const int headerWidth;
        Label name;
        Slider volume { Slider::LinearHorizontal, Slider::NoTextBox };
        TextButton muteBtn { "M" }, soloBtn { "S" }, addBtn { "+" }, autoBtn { "A" };
        std::unique_ptr<LevelMeter> meter;
        OwnedArray<Chip> chips;
        String lastSig;
        bool dropHover = false, wasSelected = false;
    };

    //==============================================================================
    /** All bus rows, returns first, then the master. */
    class Area : public Component
    {
    public:
        Area (EditViewState& e, int headerW) : evs (e), headerWidth (headerW) { rebuild(); }

        void rebuild()
        {
            autoHeaders.clear();
            autoLanes.clear();
            rows.clear();
            for (auto t : Routing::getReturnTracks (evs.edit))
                addAndMakeVisible (rows.add (new Row (evs, t, headerWidth)));
            addAndMakeVisible (rows.add (new Row (evs, nullptr, headerWidth)));
            for (auto* r : rows)
            {
                auto* t = r->automationTrack();
                const bool show = t != nullptr && evs.isAutomationVisible (*t);
                autoHeaders.add (show ? new AutomationHeader (evs, t) : nullptr);
                autoLanes.add (show ? new AutomationLane (evs, t) : nullptr);
                if (show) { addAndMakeVisible (autoHeaders.getLast()); addAndMakeVisible (autoLanes.getLast()); }
            }
            resized();
        }

        void repaintLanes() { for (auto* l : autoLanes) if (l != nullptr) l->repaint(); }

        int idealHeight() const
        {
            int h = 18 + rows.size() * Row::height;
            for (auto* l : autoLanes) if (l != nullptr) h += EditViewState::automationHeight;
            return h;
        }
        int numRows() const { return rows.size(); }
        Row* row (int i) const { return rows[i]; }

        void paint (Graphics& g) override
        {
            g.fillAll (Theme::well);
            g.setColour (Theme::line);
            g.drawHorizontalLine (0, 0.0f, (float) getWidth());
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.0f, Font::bold));
            g.drawText ("RETURNS AND MASTER", 10, 1, headerWidth - 20, 16, Justification::centredLeft);
        }

        void resized() override
        {
            int y = 18;
            for (int i = 0; i < rows.size(); ++i)
            {
                rows[i]->setBounds (0, y, getWidth(), Row::height);
                y += Row::height;
                if (auto* l = autoLanes[i])
                {
                    autoHeaders[i]->setBounds (0, y, headerWidth, EditViewState::automationHeight);
                    l->setBounds (headerWidth, y, getWidth() - headerWidth, EditViewState::automationHeight);
                    y += EditViewState::automationHeight;
                }
            }
        }

    private:
        EditViewState& evs;
        const int headerWidth;
        OwnedArray<Row> rows;
        OwnedArray<AutomationHeader> autoHeaders;
        OwnedArray<AutomationLane> autoLanes;
    };
}
