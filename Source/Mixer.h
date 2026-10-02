#pragma once

#include "EditViewState.h"
#include "Routing.h"
#include "EditingOps.h"
#include "PulseSynths.h"
#include "MidiLearn.h"

//==============================================================================
/** Stereo peak meter fed by a LevelMeterPlugin's measurer (fast attack, peak hold). */
class LevelMeter : public Component,
                   private Timer
{
public:
    explicit LevelMeter (te::LevelMeterPlugin& p) : holder (&p), plugin (p)
    {
        plugin.measurer.addClient (client);
        startTimerHz (30);
    }

    ~LevelMeter() override { plugin.measurer.removeClient (client); }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float w = (r.getWidth() - 2.0f) / 2.0f;
        drawBar (g, r.withWidth (w), level[0], peak[0]);
        drawBar (g, r.withTrimmedLeft (w + 2.0f), level[1], peak[1]);
    }

private:
    static float norm (float db) { return jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); }

    void drawBar (Graphics& g, Rectangle<float> r, float db, float pk)
    {
        g.setColour (Colour (0xff0d0f12));
        g.fillRect (r);
        auto n = norm (db);
        if (n > 0.0f)
        {
            ColourGradient grad (Colour (0xff2f9e44), 0, r.getBottom(), Theme::red, 0, r.getY(), false);
            grad.addColour (norm (-12.0f), Theme::green);
            grad.addColour (norm (-3.0f), Theme::yellow);
            g.setGradientFill (grad);
            g.fillRect (r.withTrimmedTop (r.getHeight() * (1.0f - n)));
        }
        auto pn = norm (pk);
        if (pn > 0.0f)
        {
            g.setColour (pk > 0.0f ? Theme::red : Theme::text);
            g.fillRect (r.getX(), r.getY() + r.getHeight() * (1.0f - pn), r.getWidth(), 1.5f);
        }
    }

    void timerCallback() override
    {
        const auto now = Time::getMillisecondCounter();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto db = client.getAndClearAudioLevel (jmin (ch, jmax (0, client.getNumChannelsUsed() - 1))).dB;
            level[ch] = jmax (db, level[ch] - 0.8f);
            if (db >= peak[ch] || now - peakTime[ch] > 1200) { peak[ch] = db; peakTime[ch] = now; }
        }
        repaint();
    }

    te::Plugin::Ptr holder;          // keeps the plugin (and its measurer) alive
    te::LevelMeterPlugin& plugin;
    te::LevelMeasurer::Client client;
    float level[2] { -100.0f, -100.0f }, peak[2] { -100.0f, -100.0f };
    uint32 peakTime[2] {};
};

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
        name.setFont (FontOptions (13.0f, Font::bold));
        name.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (name);

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
        fader.onValueChange = [this] { if (volPlugin) volPlugin->setVolumeDb ((float) fader.getValue()); };
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
        g.setColour (track == nullptr ? Colour (0xff23272e) : Theme::panel2);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (track != nullptr ? track->getColour() : Theme::text);
        g.fillRoundedRectangle (r.withHeight (3.0f), 1.5f);
        g.setColour (selected ? Theme::accent : Theme::line);
        g.drawRoundedRectangle (r, 6.0f, selected ? 1.5f : 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        name.setBounds (r.removeFromTop (20));

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
        latency.setBounds (r.removeFromBottom (13));
        db.setBounds (r.removeFromBottom (18));
        auto faderArea = r.reduced (0, 4);
        if (meter != nullptr)
            meter->setBounds (faderArea.removeFromRight (14));
        faderArea.removeFromRight (6);
        fader.setBounds (faderArea.withSizeKeepingCentre (26, faderArea.getHeight()));
    }

    void mouseDown (const MouseEvent& e) override
    {
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
            db.setText (v <= -59.9f ? "-inf" : String (v, 1) + " dB", dontSendNotification);
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
        }

        // plugin latency (the engine compensates; shown so you know what's adding delay)
        double secs = 0;
        if (auto at = audioTrack())
            for (auto p : at->pluginList) { if (p->isEnabled()) secs += p->getLatencySeconds(); }
        if (track == nullptr)
            for (auto p : evs.edit.getMasterPluginList()) { if (p->isEnabled()) secs += p->getLatencySeconds(); }
        latency.setText (secs > 0.00005 ? "latency " + String (secs * 1000.0, 1) + " ms" : String(), dontSendNotification);
        latency.setTooltip ("Plugin latency on this channel. Pulse delays the other tracks to keep everything in time.");
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
    Label name, db;
    Slider pan, fader;
    std::unique_ptr<LevelMeter> meter;
    TextButton muteBtn { "M" }, soloBtn { "S" }, freezeBtn { "F" }, limiterBtn { "Limiter" };
    Label latency;
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
            m.addItem ("Reverb return", [this] { Routing::createReturnTrack (evs.edit, "Reverb", te::ReverbPlugin::xmlTypeName); });
            m.addItem ("Delay return",  [this] { Routing::createReturnTrack (evs.edit, "Delay", te::DelayPlugin::xmlTypeName); });
            m.addItem ("Empty return",  [this] { Routing::createReturnTrack (evs.edit, "Return " + String (Routing::nextFreeBus (evs.edit) + 1)); });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addReturnBtn));
        };
        content.addAndMakeVisible (addReturnBtn);

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
    TextButton addReturnBtn { "+ Return" };
    int numContentStrips = 0;
    bool rebuildFlag = false;
};
