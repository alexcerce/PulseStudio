#pragma once

#include "EditViewState.h"
#include "Routing.h"
#include "EditingOps.h"
#include "PulseSynths.h"
#include "MidiLearn.h"
#include "Meters.h"
#include "FactoryPresets.h"
#include "Icons.h"

//==============================================================================
class ChannelStrip : public Component,
                     private Timer
{
public:
    ChannelStrip (EditViewState& e, te::Track* t, te::VolumeAndPanPlugin::Ptr vp, te::LevelMeterPlugin* meterPlugin)
        : evs (e), track (t), volPlugin (vp)
    {
        name.setText (t != nullptr ? t->getName() : "Master", dontSendNotification);
        name.setJustificationType (Justification::centred);
        name.setFont (FontOptions (12.5f, Font::bold));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

        devicesBtn.setTooltip ("Show this channel's devices (double-click the strip does the same)");
        devicesBtn.onClick = [this] { if (evs.showDevices) evs.showDevices (track); };
        devicesBtn.setFlat (true);
        addAndMakeVisible (devicesBtn);   // the master's opens the master devices
        peakLbl.setJustificationType (Justification::centred);
        peakLbl.setFont (FontOptions (10.5f, Font::bold));
        peakLbl.setTooltip ("Highest peak (click to reset)");
        peakLbl.addMouseListener (this, false);
        addAndMakeVisible (peakLbl);
        addAndMakeVisible (scale);

        pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTooltip ("Pan");
        pan.onValueChange = [this] { if (volPlugin) volPlugin->setPan ((float) pan.getValue()); };
        addAndMakeVisible (pan);
        pan.addMouseListener (this, false);

        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-10.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.onValueChange = [this]
        {
            if (volPlugin == nullptr) return;
            const float before = volPlugin->getVolumeDb();
            volPlugin->setVolumeDb ((float) fader.getValue());
            // linked faders: the other selected tracks move by the same amount
            if (track != nullptr && evs.selectionManager.isSelected (track) && fader.isMouseButtonDown())
            {
                const float delta = volPlugin->getVolumeDb() - before;
                for (auto t : evs.selectionManager.getItemsOfType<te::AudioTrack>())
                    if (t != track)
                        if (auto vp = t->getVolumePlugin())
                            vp->setVolumeDb (jlimit (-60.0f, 6.0f, vp->getVolumeDb() + delta));
            }
        };
        addAndMakeVisible (fader);
        fader.addMouseListener (this, false);

        db.setJustificationType (Justification::centred);
        db.setFont (FontOptions (12.0f));
        db.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (db);

        if (meterPlugin != nullptr)
        {
            meter = std::make_unique<LevelMeter> (*meterPlugin);
            addAndMakeVisible (*meter);
        }

        // Send knobs (one per return bus) on normal tracks.
        if (audioTrack() != nullptr && ! Routing::isReturnTrack (*audioTrack()))
        {
            for (auto ret : Routing::getReturnTracks (evs.edit))
            {
                auto* s = sends.add (new Send());
                s->bus = Routing::getReturnBus (*ret);
                s->knob.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
                s->knob.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
                s->knob.setRange (Routing::sendOffDb, 6.0, 0.1);
                s->knob.setSkewFactorFromMidPoint (-18.0);
                s->knob.setDoubleClickReturnValue (true, Routing::sendOffDb);
                s->knob.setColour (Slider::rotarySliderFillColourId, Theme::orange);
                s->knob.setTooltip ("Send to " + ret->getName());
                s->knob.onValueChange = [this, s] { Routing::setSendDb (*audioTrack(), s->bus, (float) s->knob.getValue()); };
                s->knob.addMouseListener (this, false);
                s->label.setText (ret->getName().substring (0, 6), dontSendNotification);
                s->label.setFont (FontOptions (10.5f));
                s->label.setColour (Label::textColourId, Theme::muted);
                s->label.setJustificationType (Justification::centred);
                addAndMakeVisible (s->knob);
                addAndMakeVisible (s->label);
            }
        }

        if (track == nullptr)
        {
            loudLbl.setJustificationType (Justification::centred);
            loudLbl.setFont (FontOptions (10.5f));
            loudLbl.setTooltip ("Loudness while playing: S = short-term (3 s), I = integrated since reset, TP = true peak. Click to reset.");
            loudLbl.addMouseListener (this, false);
            addAndMakeVisible (loudLbl);
        }

        if (track != nullptr)
        {
            muteBtn.setColour (TextButton::buttonOnColourId, Theme::yellow);
            soloBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
            muteBtn.onClick = [this] { track->setMute (! track->isMuted (false)); };
            soloBtn.onClick = [this] { track->setSolo (! track->isSolo (false)); };
            addAndMakeVisible (muteBtn);
            addAndMakeVisible (soloBtn);

            if (auto at = audioTrack(); at != nullptr && ! Routing::isReturnTrack (*at))
            {
                freezeBtn.setColour (TextButton::buttonOnColourId, Theme::accent);
                freezeBtn.setTooltip ("Freeze: render the track to audio and switch its plugins off to save CPU (click again to unfreeze)");
                freezeBtn.onClick = [this]
                {
                    if (auto t = audioTrack())
                        EditingOps::toggleFreeze (*t);
                };
                addAndMakeVisible (freezeBtn);
            }
        }
        else
        {
            limiterBtn.setColour (TextButton::buttonOnColourId, Theme::orange);
            limiterBtn.setTooltip ("Master limiter (look-ahead brickwall). Its settings are in the master device chain.");
            limiterBtn.onClick = [this] { toggleMasterLimiter(); };
            addAndMakeVisible (limiterBtn);
        }

        latency.setJustificationType (Justification::centred);
        latency.setFont (FontOptions (10.5f));
        latency.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (latency);

        refresh();
        startTimerHz (8);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool selected = track != nullptr && evs.selectionManager.isSelected (track);
        g.setGradientFill (ColourGradient (Theme::panel2.brighter (selected ? 0.08f : 0.02f), 0.0f, r.getY(),
                                          Theme::panel2.darker (0.15f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 6.0f);
        // name plate in the track colour
        const auto col = track != nullptr ? track->getColour() : Theme::line;
        auto plate = r.withHeight (24.0f);
        g.setColour (col.withAlpha (selected ? 1.0f : 0.85f));
        g.fillRoundedRectangle (plate, 6.0f);
        g.fillRect (plate.withTrimmedTop (12.0f));
        name.setColour (Label::textColourId, TrackColours::textOn (col));
        g.setColour (selected ? Colours::white : Theme::line);
        g.drawRoundedRectangle (r, 6.0f, selected ? 1.5f : 1.0f);
        // fader groove marks
        if (! faderArea.isEmpty())
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (faderArea.toFloat().withSizeKeepingCentre (4.0f, (float) faderArea.getHeight() - 10.0f), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto plate = getLocalBounds().removeFromTop (24).reduced (4, 0);
        if (devicesBtn.isVisible()) devicesBtn.setBounds (plate.removeFromRight (20).reduced (0, 3));
        name.setBounds (plate);
        r.removeFromTop (22);

        // sends: two per row
        for (int i = 0; i < sends.size(); i += 2)
        {
            auto row = r.removeFromTop (44);
            for (int j = i; j < jmin (i + 2, sends.size()); ++j)
            {
                auto cell = row.removeFromLeft (r.getWidth() / 2);
                sends[j]->label.setBounds (cell.removeFromBottom (12));
                sends[j]->knob.setBounds (cell.withSizeKeepingCentre (32, 32));
            }
        }
        if (! sends.isEmpty())
            r.removeFromTop (4);

        pan.setBounds (r.removeFromTop (40).withSizeKeepingCentre (40, 40));
        auto bottom = r.removeFromBottom (24);
        if (track != nullptr)
        {
            const int w = bottom.getWidth() / (freezeBtn.isVisible() ? 3 : 2);
            muteBtn.setBounds (bottom.removeFromLeft (w).reduced (2, 0));
            soloBtn.setBounds (bottom.removeFromLeft (w).reduced (2, 0));
            if (freezeBtn.isVisible()) freezeBtn.setBounds (bottom.reduced (2, 0));
        }
        else
        {
            limiterBtn.setBounds (bottom.reduced (2, 0));
        }
        if (loudLbl.isVisible()) loudLbl.setBounds (r.removeFromBottom (26));
        latency.setBounds (r.removeFromBottom (13));
        auto readouts = r.removeFromBottom (18);
        db.setBounds (readouts.removeFromLeft (readouts.getWidth() / 2));
        peakLbl.setBounds (readouts);
        auto area = r.reduced (0, 4);
        if (meter != nullptr)
            meter->setBounds (area.removeFromRight (12));
        area.removeFromRight (2);
        scale.setBounds (area.removeFromRight (22));
        faderArea = area;
        fader.setBounds (area.withSizeKeepingCentre (26, area.getHeight()));
    }

    void mouseDoubleClick (const MouseEvent&) override { if (evs.showDevices) evs.showDevices (track); }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.eventComponent == &peakLbl && meter != nullptr) { meter->resetPeak(); return; }
        if (e.eventComponent == &loudLbl)
        {
            if (auto pr = evs.edit.getMasterPluginList().findFirstPluginOfType<pulse::LoudnessProbe>()) pr->resetRequested = true;
            return;
        }

        // right-click a send: pre / post fader
        for (auto* snd : sends)
            if (e.eventComponent == &snd->knob && e.mods.isPopupMenu())
            {
                const bool pre = Routing::isPreFader (*audioTrack(), snd->bus);
                PopupMenu m;
                m.addItem ("Post-fader (follows the volume fader)", true, ! pre, [this, b = snd->bus] { Routing::setPreFader (*audioTrack(), b, false); });
                m.addItem ("Pre-fader (independent of the fader)", true, pre, [this, b = snd->bus] { Routing::setPreFader (*audioTrack(), b, true); });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (&snd->knob));
                return;
            }

        // right-click on the fader or pan knob: MIDI Learn
        if (e.mods.isPopupMenu() && volPlugin != nullptr && evs.midiLearn != nullptr
            && (e.eventComponent == &fader || e.eventComponent == &pan))
        {
            auto& p = e.eventComponent == &fader ? *volPlugin->volParam : *volPlugin->panParam;
            PopupMenu m;
            evs.midiLearn->addToMenu (m, p);
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (e.eventComponent));
            return;
        }

        if (track != nullptr)
            evs.selectionManager.selectOnly (track);
    }

private:
    void timerCallback() override { refresh(); }

    void refresh()
    {
        if (volPlugin != nullptr)
        {
            if (! fader.isMouseButtonDown()) fader.setValue (volPlugin->getVolumeDb(), dontSendNotification);
            if (! pan.isMouseButtonDown())   pan.setValue (volPlugin->getPan(), dontSendNotification);
            auto v = volPlugin->getVolumeDb();
            db.setText (v <= -59.9f ? "-inf" : String (v, 1), dontSendNotification);
        }
        if (meter != nullptr)
        {
            const float pk = meter->getMaxPeak();
            peakLbl.setText (pk <= -99.0f ? String ("-inf") : String (pk, 1), dontSendNotification);
            peakLbl.setColour (Label::textColourId, pk > 0.0f ? Colour (0xffff4d4d) : pk > -3.0f ? Theme::orange : Theme::text);
            peakLbl.setColour (Label::backgroundColourId, pk > 0.0f ? Colour (0x40ff4d4d) : Theme::bg);
        }

        if (track != nullptr)
        {
            muteBtn.setToggleState (track->isMuted (false), dontSendNotification);
            soloBtn.setToggleState (track->isSolo (false), dontSendNotification);
            name.setText (track->getName(), dontSendNotification);

            for (auto* s : sends)
            {
                if (! s->knob.isMouseButtonDown())
                    s->knob.setValue (Routing::getSendDb (*audioTrack(), s->bus), dontSendNotification);
                const bool pre = Routing::isPreFader (*audioTrack(), s->bus);
                s->label.setColour (Label::textColourId, pre ? Theme::orange : Theme::muted);
                s->knob.setTooltip (String (pre ? "Pre-fader" : "Post-fader") + " send (right-click to change)");
            }

            if (auto at = audioTrack())
                freezeBtn.setToggleState (at->isFrozen (te::Track::individualFreeze), dontSendNotification);
        }
        else
        {
            limiterBtn.setToggleState (findMasterLimiter() != nullptr, dontSendNotification);
            if (auto pr = evs.edit.getMasterPluginList().findFirstPluginOfType<pulse::LoudnessProbe>())
            {
                auto fmtL = [] (float v) { return v <= -199.0f ? String ("--") : String (v, 1); };
                loudLbl.setText ("S " + fmtL (pr->shortTermLufs.load()) + "  I " + fmtL (pr->integratedLufs.load()) + "\nTP " + fmtL (pr->truePeakDb.load()) + " LUFS",
                                 dontSendNotification);
                loudLbl.setColour (Label::textColourId, pr->truePeakDb.load() > -1.0f ? Theme::orange : Theme::muted);
            }
        }

        // plugin latency (the engine compensates; shown so you know what's adding delay)
        double secs = 0;
        if (auto at = audioTrack())
            for (auto p : at->pluginList) { if (p->isEnabled()) secs += p->getLatencySeconds(); }
        if (track == nullptr)
            for (auto p : evs.edit.getMasterPluginList()) { if (p->isEnabled()) secs += p->getLatencySeconds(); }
        double cpu = 0;
        if (auto at = audioTrack())
            for (auto p : at->pluginList) cpu += p->getCpuUsage();
        if (track == nullptr)
            for (auto p : evs.edit.getMasterPluginList()) cpu += p->getCpuUsage();
        String info;
        if (cpu > 0.0005) info << "CPU " << roundToInt (cpu * 100.0) << "%";
        if (secs > 0.00005) info << (info.isNotEmpty() ? "  " : "") << String (secs * 1000.0, 1) << " ms";
        latency.setText (info, dontSendNotification);
        latency.setColour (Label::textColourId, cpu > 0.3 ? Theme::orange : Theme::muted);
        latency.setTooltip ("CPU used by this channel's devices, and their latency (Pulse delays the other tracks to keep everything in time).");
        repaint();
    }

    te::Plugin* findMasterLimiter()
    {
        for (auto p : evs.edit.getMasterPluginList())
            if (dynamic_cast<pulse::Limiter*> (p) != nullptr) return p;
        return nullptr;
    }

    void toggleMasterLimiter()
    {
        auto& list = evs.edit.getMasterPluginList();
        evs.edit.getUndoManager().beginNewTransaction ("Master limiter");
        if (auto l = findMasterLimiter()) { l->deleteFromParent(); return; }

        int index = list.size();
        if (auto meter = list.findFirstPluginOfType<te::LevelMeterPlugin>())
            index = list.indexOf (meter);   // limit before the master meter
        list.insertPlugin (evs.edit.getPluginCache().createNewPlugin (pulse::Limiter::xmlTypeName, {}), index, nullptr);
    }

    struct Send
    {
        int bus = 0;
        Slider knob;
        Label label;
    };
    OwnedArray<Send> sends;

    EditViewState& evs;
    te::Track* track;
    te::AudioTrack* audioTrack() const { return dynamic_cast<te::AudioTrack*> (track); }
    te::VolumeAndPanPlugin::Ptr volPlugin;
    Label name, db, peakLbl;
    Slider pan, fader;
    Rectangle<int> faderArea;
    DbScale scale { [this] (float v) { return (float) (fader.getY() - scale.getY()) + (float) fader.getPositionOfValue (jlimit (-60.0, 6.0, (double) v)); } };
    std::unique_ptr<LevelMeter> meter;
    TextButton muteBtn { "M" }, soloBtn { "S" }, freezeBtn { "F" }, limiterBtn { "Limiter" };
    Label latency, loudLbl;
    IconButton devicesBtn { Icons::Type::devices, "Show devices" };
};

//==============================================================================
class Mixer : public Component,
              private te::ValueTreeAllEventListener,
              private FlaggedAsyncUpdater
{
public:
    explicit Mixer (EditViewState& e) : evs (e)
    {
        addAndMakeVisible (viewport);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);

        addReturnBtn.setTooltip ("Add a return (aux) track - every track gets a send knob for it");
        addReturnBtn.onClick = [this]
        {
            PopupMenu m;
            auto make = [this] (const char* name, const char* type, const char* preset)
            {
                if (auto r = Routing::createReturnTrack (evs.edit, name, type))
                    for (auto p : r->pluginList)
                        if (p->getPluginType() == String (type))
                        {
                            FactoryPresets::apply (*p, preset);
                            if (auto mix = p->getAutomatableParameterByID ("mix")) mix->setParameter (1.0f, sendNotificationSync);
                        }
            };
            m.addItem ("Reverb return", [make] { make ("Reverb", pulse::Reverb::xmlTypeName, "Hall"); });
            m.addItem ("Delay return",  [make] { make ("Delay", pulse::Delay::xmlTypeName, "Dotted 8th"); });
            m.addItem ("Empty return",  [this] { Routing::createReturnTrack (evs.edit, "Return " + String (Routing::nextFreeBus (evs.edit) + 1)); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addReturnBtn));
        };
        content.addAndMakeVisible (addReturnBtn);

        snapBtn.setTooltip ("Mixer snapshots: store and recall every track's volume, pan, mute, solo and sends");
        snapBtn.onClick = [this] { showSnapshotMenu(); };
        content.addAndMakeVisible (snapBtn);

        evs.edit.state.addListener (this);
        markAndUpdate (rebuildFlag);
    }

    ~Mixer() override { evs.edit.state.removeListener (this); }

    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
        layout();
    }

private:
    void rebuild()
    {
        strips.clear();
        numContentStrips = 0;

        // tracks and groups, in arrangement order
        for (auto t : te::getAllTracks (evs.edit))
        {
            ChannelStrip* s = nullptr;

            if (auto f = dynamic_cast<te::FolderTrack*> (t))
            {
                if (f->isSubmixFolder())
                    s = new ChannelStrip (evs, f, f->getVolumePlugin(), nullptr);
            }
            else if (auto at = dynamic_cast<te::AudioTrack*> (t); at != nullptr && ! Routing::isReturnTrack (*at))
            {
                s = new ChannelStrip (evs, at, at->getVolumePlugin(), at->getLevelMeterPlugin());
            }

            if (s != nullptr)
            {
                content.addAndMakeVisible (strips.add (s));
                ++numContentStrips;
            }
        }

        for (auto t : Routing::getReturnTracks (evs.edit))
        {
            auto* s = strips.add (new ChannelStrip (evs, t, t->getVolumePlugin(), t->getLevelMeterPlugin()));
            content.addAndMakeVisible (s);
        }

        auto masterMeter = evs.edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>();
        auto* m = strips.add (new ChannelStrip (evs, nullptr, evs.edit.getMasterVolumePlugin(), masterMeter));
        content.addAndMakeVisible (m);
        layout();
    }

    void layout()
    {
        const int h = jmax (200, viewport.getHeight() - viewport.getScrollBarThickness());
        int x = 8;
        for (int i = 0; i < strips.size(); ++i)
        {
            if (i == numContentStrips) x += 16;          // gap before returns
            if (i == strips.size() - 1)                    // + Return, then master
            {
                addReturnBtn.setBounds (x, 6, 80, 28);
                snapBtn.setBounds (x, 40, 80, 28);
                x += 92;
            }
            strips[i]->setBounds (x, 6, 92, h - 12);
            x += 96;
        }
        content.setSize (x + 8, h);
    }

    void valueTreeChanged() override {}

    // Only structural changes rebuild: tracks, and return plugins (which create/remove buses).
    // Sends are created lazily while a knob is dragged, so they must not trigger a rebuild.
    static bool affectsLayout (const ValueTree& c)
    {
        return te::TrackList::isTrack (c)
            || (c.hasType (te::IDs::PLUGIN) && c[te::IDs::type].toString() == te::AuxReturnPlugin::xmlTypeName);
    }

    void valueTreeChildAdded (ValueTree&, ValueTree& c) override            { if (affectsLayout (c)) markAndUpdate (rebuildFlag); }
    void valueTreeChildRemoved (ValueTree&, ValueTree& c, int) override     { if (affectsLayout (c)) markAndUpdate (rebuildFlag); }

    void valueTreeChildOrderChanged (ValueTree& p, int a, int b) override
    {
        if (te::TrackList::isTrack (p.getChild (a)) || te::TrackList::isTrack (p.getChild (b)))
            markAndUpdate (rebuildFlag);
    }

    void handleAsyncUpdate() override
    {
        if (compareAndReset (rebuildFlag))
            rebuild();
    }

    struct Content : public Component {};

    EditViewState& evs;
    Viewport viewport;
    Content content;
    OwnedArray<ChannelStrip> strips;
    TextButton addReturnBtn { "+ Return" }, snapBtn { "Snapshots" };

    void showSnapshotMenu()
    {
        PopupMenu m;
        m.addItem ("Store snapshot...", [this]
        {
            auto* w = new AlertWindow ("Store mixer snapshot", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", "Mix " + String (MixSnapshots::names (evs.edit).size() + 1));
            w->addButton ("Store", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w] (int r)
            {
                if (r == 1) MixSnapshots::store (evs.edit, w->getTextEditorContents ("name").trim());
            }), true);
        });
        auto names = MixSnapshots::names (evs.edit);
        if (! names.isEmpty())
        {
            m.addSectionHeader ("Recall");
            for (int i = 0; i < names.size(); ++i)
                m.addItem (names[i], [this, i] { MixSnapshots::recall (evs.edit, i); });
            PopupMenu del;
            for (int i = 0; i < names.size(); ++i)
                del.addItem (names[i], [this, i] { MixSnapshots::remove (evs.edit, i); });
            m.addSubMenu ("Delete", del);
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&snapBtn));
    }
    int numContentStrips = 0;
    bool rebuildFlag = false;
};
