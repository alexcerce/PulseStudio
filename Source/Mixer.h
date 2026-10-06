#pragma once

#include "EditViewState.h"
#include "Routing.h"
#include "EditingOps.h"
#include "PulseSynths.h"
#include "MidiLearn.h"
#include "Meters.h"
#include "FactoryPresets.h"
#include "Icons.h"
#include "PluginMenu.h"
#include "Mastering.h"
#include "Vca.h"
#include "About.h"

//==============================================================================
/** A strip's EQ at a glance: the curve of the first EQ on the channel (Prism EQ or EQ Six).
    Click it to open that EQ, or to add a Prism EQ when there is none. */
class MiniEQ : public Component,
               public SettableTooltipClient,
               private Timer
{
public:
    MiniEQ (EditViewState& e, te::Track* t) : evs (e), track (t)
    {
        setTooltip ("EQ: click to open it (or to add a Prism EQ)");
        startTimerHz (8);
    }

    te::Plugin* findEQ() const
    {
        auto* t = track != nullptr ? track : (te::Track*) evs.edit.getMasterTrack();
        if (t == nullptr) return nullptr;
        for (auto p : EditViewState::pluginsOf (*t))
            if (dynamic_cast<pulse::PrismEQ*> (p) != nullptr || dynamic_cast<pulse::PulseEQ*> (p) != nullptr) return p;
        return nullptr;
    }

    static double dbOf (te::Plugin& p, double f)
    {
        if (auto q = dynamic_cast<pulse::PrismEQ*> (&p)) return q->responseDb (f) - q->get (q->g (pulse::PrismEQ::gOutput));
        if (auto q = dynamic_cast<pulse::PulseEQ*> (&p)) return q->responseDb (f) - q->get (q->numParams() - 1);
        return 0.0;
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (Theme::well);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (hover ? Theme::accent : Theme::line);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        auto* eq = findEQ();
        const float cy = r.getCentreY();
        g.setColour (Theme::ink.withAlpha (0.08f));
        g.drawHorizontalLine ((int) cy, r.getX() + 2, r.getRight() - 2);
        if (eq == nullptr || ! eq->isEnabled())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.0f));
            g.drawText (eq == nullptr ? "+ EQ" : "EQ off", r, Justification::centred);
            return;
        }
        Path p;
        for (float x = r.getX() + 2; x <= r.getRight() - 2; x += 1.0f)
        {
            const double f = 20.0 * std::pow (1000.0, (x - r.getX()) / r.getWidth());
            const float y = jlimit (r.getY() + 1, r.getBottom() - 1, cy - (float) (dbOf (*eq, f) / 18.0) * r.getHeight() * 0.5f);
            if (x == r.getX() + 2) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (Theme::accent);
        g.strokePath (p, PathStrokeType (1.3f));
    }

    void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition())) return;
        if (auto* eq = findEQ()) { if (evs.openDeviceEditor) evs.openDeviceEditor (*eq); return; }
        evs.edit.getUndoManager().beginNewTransaction ("Add Prism EQ");
        auto p = evs.edit.getPluginCache().createNewPlugin (pulse::PrismEQ::xmlTypeName, {});
        if (p == nullptr) return;
        if (track != nullptr) PluginMenu::insertPlugin (track, track->pluginList, p, nullptr);
        else
        {
            auto& ml = evs.edit.getMasterPluginList();
            ml.insertPlugin (p, PluginMenu::insertIndexFor (ml, nullptr), nullptr);
        }
        if (evs.openDeviceEditor) evs.openDeviceEditor (*p);
    }

private:
    void timerCallback() override
    {
        double sig = 0;
        if (auto* eq = findEQ())
        {
            sig = eq->isEnabled() ? 1.0 : 2.0;
            for (auto* prm : eq->getAutomatableParameters()) sig = sig * 1.0001 + prm->getCurrentValue();
        }
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }
    EditViewState& evs;
    te::Track* track;
    double lastSig = -1;
    bool hover = false;
};

/** Every track's send to every return, as a grid of sliders. */
class SendMatrix : public Component
{
public:
    explicit SendMatrix (te::Edit& e) : edit (e)
    {
        for (auto t : te::getAudioTracks (edit))
            if (! Routing::isReturnTrack (*t)) tracks.add (t);
        returns = Routing::getReturnTracks (edit);
        for (auto* t : tracks)
            for (auto* r : returns)
            {
                auto* s = cells.add (new Slider (Slider::LinearBar, Slider::TextBoxLeft));
                const int bus = Routing::getReturnBus (*r);
                s->setRange (Routing::sendOffDb, 6.0, 0.1);
                s->setSkewFactorFromMidPoint (-18.0);
                s->setTextValueSuffix (" dB");
                s->setNumDecimalPlacesToDisplay (1);
                s->setDoubleClickReturnValue (true, Routing::sendOffDb);
                s->setColour (Slider::trackColourId, r->getColour().withAlpha (0.55f));
                s->setColour (Slider::backgroundColourId, Theme::well);
                s->setValue (Routing::getSendDb (*t, bus), dontSendNotification);
                s->onDragStart = [this] { edit.getUndoManager().beginNewTransaction ("Send level"); };
                s->onValueChange = [t, bus, s] { Routing::setSendDb (*t, bus, (float) s->getValue()); };
                s->textFromValueFunction = [] (double v) { return v <= Routing::sendOffDb + 0.05 ? String ("off") : String (v, 1) + " dB"; };
                s->setTooltip (t->getName() + " -> " + r->getName());
                addAndMakeVisible (s);
            }
        setSize (nameW + jmax (1, returns.size()) * cellW + 12, headerH + jmax (1, tracks.size()) * rowH + 12);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setFont (FontOptions (11.5f, Font::bold));
        for (int c = 0; c < returns.size(); ++c)
        {
            g.setColour (returns[c]->getColour());
            g.fillRoundedRectangle ((float) (nameW + c * cellW + 4), 6.0f, (float) cellW - 8.0f, 4.0f, 2.0f);
            g.setColour (Theme::text);
            g.drawText (returns[c]->getName(), nameW + c * cellW, 12, cellW, headerH - 14, Justification::centred, true);
        }
        g.setFont (FontOptions (12.0f));
        for (int r = 0; r < tracks.size(); ++r)
        {
            const int y = headerH + r * rowH;
            g.setColour (tracks[r]->getColour());
            g.fillRect (6, y + 5, 4, rowH - 10);
            g.setColour (Theme::text);
            g.drawText (tracks[r]->getName(), 14, y, nameW - 18, rowH, Justification::centredLeft, true);
        }
        if (returns.isEmpty() || tracks.isEmpty())
        {
            g.setColour (Theme::muted);
            g.drawText (returns.isEmpty() ? "No returns yet - add one with + Return" : "No tracks", getLocalBounds(), Justification::centred);
        }
    }

    void resized() override
    {
        int i = 0;
        for (int r = 0; r < tracks.size(); ++r)
            for (int c = 0; c < returns.size(); ++c)
                cells[i++]->setBounds (nameW + c * cellW + 4, headerH + r * rowH + 3, cellW - 8, rowH - 6);
    }

    int numCells() const { return cells.size(); }
    Slider* cell (int i) const { return cells[i]; }

private:
    static constexpr int nameW = 140, cellW = 100, rowH = 28, headerH = 34;
    te::Edit& edit;
    juce::Array<te::AudioTrack*> tracks, returns;
    OwnedArray<Slider> cells;
};

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
        miniEQ = std::make_unique<MiniEQ> (evs, t);
        addAndMakeVisible (*miniEQ);
        if (auto f = dynamic_cast<te::FolderTrack*> (t))
        {
            foldBtn.setTooltip ("Fold / unfold this group's tracks (in the mixer and the arrangement)");
            foldBtn.onClick = [this, f]
            {
                if (evs.collapsed.count (f->itemID)) evs.collapsed.erase (f->itemID); else evs.collapsed.insert (f->itemID);
                evs.layoutChanged.sendChangeMessage();
            };
            foldBtn.setButtonText (evs.collapsed.count (f->itemID) ? CharPointer_UTF8 ("\xe2\x96\xb8") : CharPointer_UTF8 ("\xe2\x96\xbe"));
            addAndMakeVisible (foldBtn);
        }
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
        // click the level to type one
        db.setEditable (true, false, true);
        db.setTooltip ("The fader's level in dB: click to type one");
        db.setColour (Label::backgroundWhenEditingColourId, Theme::well);
        db.setColour (Label::outlineWhenEditingColourId, Theme::accent);
        db.setColour (Label::textWhenEditingColourId, Theme::text);
        db.onEditorShow = [this] { if (auto* ed = db.getCurrentTextEditor()) { ed->setJustification (Justification::centred); ed->selectAll(); } };
        db.onTextChange = [this] { typeLevel (db.getText()); };
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
            muteBtn.onClick = [this] { EngineHelpers::toggleMute (*track); };
            soloBtn.onClick = [this] { EngineHelpers::toggleSolo (*track); };
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
            masterBtn.setTooltip ("Mastering: put a ready chain on the master, or let the assistant set the loudness");
            masterBtn.onClick = [this] { showMasteringMenu(); };
            addAndMakeVisible (masterBtn);
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
        // the same quiet card as the devices; the selected strip is a little lighter
        g.setColour (Pro::shade (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 7.0f);
        g.setGradientFill (ColourGradient (Pro::cardTop().brighter (selected ? 0.10f : 0.0f), 0.0f, r.getY(),
                                          Pro::cardBot().brighter (selected ? 0.05f : 0.0f), 0.0f, r.getY() + 120.0f, false));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (Theme::ink.withAlpha (0.07f));
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        // name plate in the track colour
        const auto col = track != nullptr ? track->getColour() : Theme::line;
        auto plate = r.withHeight (24.0f);
        g.setColour (col.withAlpha (selected ? 1.0f : 0.85f));
        g.fillRoundedRectangle (plate, 6.0f);
        if (track != nullptr)
            if (const int vca = Vca::groupOf (evs.edit, *track); vca >= 0)   // a member of a VCA group wears its colour
            {
                g.setColour (Vca::colourOf (vca));
                g.fillRect (r.getX() + 6.0f, 27.0f, r.getWidth() - 12.0f, 2.5f);
            }
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

    /** Narrow strips: one small send knob per row, no send names, a smaller pan. */
    void setNarrow (bool n) { narrow = n; resized(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (narrow ? 3 : 6);
        auto plate = getLocalBounds().removeFromTop (24).reduced (4, 0);
        if (devicesBtn.isVisible()) devicesBtn.setBounds (plate.removeFromRight (20).reduced (0, 3));
        if (foldBtn.isVisible()) foldBtn.setBounds (plate.removeFromLeft (18).reduced (0, 3));
        name.setBounds (plate);
        r.removeFromTop (22);
        miniEQ->setBounds (r.removeFromTop (narrow ? 20 : 26).reduced (narrow ? 0 : 2, 1));
        r.removeFromTop (3);

        if (narrow)
        {
            for (auto* s : sends)
            {
                s->label.setVisible (false);
                s->knob.setBounds (r.removeFromTop (26).withSizeKeepingCentre (24, 24));
            }
        }
        else
        {
            // sends: two per row
            for (int i = 0; i < sends.size(); i += 2)
            {
                auto row = r.removeFromTop (44);
                for (int j = i; j < jmin (i + 2, sends.size()); ++j)
                {
                    auto cell = row.removeFromLeft (r.getWidth() / 2);
                    sends[j]->label.setVisible (true);
                    sends[j]->label.setBounds (cell.removeFromBottom (12));
                    sends[j]->knob.setBounds (cell.withSizeKeepingCentre (32, 32));
                }
            }
        }
        if (! sends.isEmpty())
            r.removeFromTop (4);

        const int panSize = narrow ? 28 : 40;
        pan.setBounds (r.removeFromTop (panSize).withSizeKeepingCentre (panSize, panSize));
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
            r.removeFromBottom (3);
            masterBtn.setBounds (r.removeFromBottom (bottom.getHeight()).reduced (2, 0));   // (its own row: the strip is too narrow for both side by side)
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
        // right-click the name plate of a track: which VCA group it belongs to
        if (e.mods.isPopupMenu() && e.eventComponent == this && e.position.y < 26.0f && audioTrack() != nullptr) { showVcaMenu(); return; }
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
            if (! db.isBeingEdited()) db.setText (v <= -59.9f ? "-inf" : String (v, 1), dontSendNotification);
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

    /** Sets the fader from typed text ("-6", "-inf", "+2.5 dB"), as the level readout does. False for nonsense. */
    bool typeLevel (const String& text)
    {
        double v = 0.0;
        if (volPlugin == nullptr || ! ValueEntry::parse (text, v)) return false;
        evs.edit.getUndoManager().beginNewTransaction ("Type level");
        volPlugin->setVolumeDb ((float) jlimit (fader.getMinimum(), fader.getMaximum(), v));
        return true;
    }

    void showVcaMenu()
    {
        auto* t = audioTrack();
        if (t == nullptr) return;
        auto& edit = evs.edit;
        const int current = Vca::groupOf (edit, *t);
        te::Track::Ptr keep (t);
        PopupMenu m;
        m.addSectionHeader ("VCA group");
        m.addItem ("None", true, current < 0, [&edit, keep] { edit.getUndoManager().beginNewTransaction ("Leave VCA"); Vca::leave (edit, *keep); });
        for (auto& g : Vca::all (edit))
            m.addItem (g.name, true, g.index == current, [&edit, keep, i = g.index] { edit.getUndoManager().beginNewTransaction ("Join VCA"); Vca::join (edit, i, *keep); });
        m.addSeparator();
        m.addItem ("New VCA group with this track", [&edit, keep] { edit.getUndoManager().beginNewTransaction ("New VCA"); Vca::join (edit, Vca::add (edit), *keep); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showMasteringMenu()
    {
        PopupMenu m;
        auto& edit = evs.edit;
        m.addItem ("Put the mastering chain on the master (EQ, multiband, exciter, limiter)", [&edit]
        {
            edit.getUndoManager().beginNewTransaction ("Mastering chain");
            Mastering::addChain (edit);
        });
        m.addSectionHeader ("Assistant: measure the song and set the loudness");
        auto assist = [&edit] (double target)
        {
            return [&edit, target]
            {
                if (About::isCapturing()) return;   // (the random-input test must not start renders of the whole song)
                edit.getUndoManager().beginNewTransaction ("Mastering assistant");
                const auto outcome = Mastering::assist (edit, target);
                if (! About::isCapturing()) AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Mastering assistant", outcome.text());
            };
        };
        m.addItem ("For streaming (-14 LUFS)", assist (-14.0));
        m.addItem ("For podcasts and Apple (-16 LUFS)", assist (-16.0));
        m.addItem ("Loud, for clubs (-9 LUFS)", assist (-9.0));
        m.addSectionHeader ("Reference song: compare the mix with a finished song");
        m.addItem ("Load a reference song...", [this, &edit]
        {
            auto* now = Mastering::reference (edit, false);
            const auto start = now != nullptr && now->getFile().existsAsFile() ? now->getFile() : File::getSpecialLocation (File::userMusicDirectory);
            refChooser = std::make_shared<FileChooser> ("Choose a reference song", start, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", ! About::isCapturing());
            refChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                     [sp = SafePointer<ChannelStrip> (this)] (const FileChooser& fc)
                                     {
                                         if (sp == nullptr || fc.getResult() == File()) return;
                                         auto& e = sp->evs.edit;
                                         e.getUndoManager().beginNewTransaction ("Reference song");
                                         if (auto* r = Mastering::reference (e, true)) r->setFile (fc.getResult());
                                     });
        });
        auto* refNow = Mastering::reference (edit, false);
        m.addItem ("Listen to the reference (instead of the mix)", refNow != nullptr, refNow != nullptr && refNow->getInt (pulse::Reference::pListen) > 0, [&edit]
        {
            edit.getUndoManager().beginNewTransaction ("Reference");
            Mastering::toggleReference (edit);
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&masterBtn));
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
    TextButton muteBtn { "M" }, soloBtn { "S" }, freezeBtn { "F" }, limiterBtn { "Limiter" }, masterBtn { "Mastering" };
    std::shared_ptr<FileChooser> refChooser;
    Label latency, loudLbl;
    IconButton devicesBtn { Icons::Type::devices, "Show devices" };
    std::unique_ptr<MiniEQ> miniEQ;
    TextButton foldBtn;
    bool narrow = false;
};

//==============================================================================
class Mixer : public Component,
              private te::ValueTreeAllEventListener,
              private FlaggedAsyncUpdater,
              private ChangeListener
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
        vcaBtn.setTooltip ("New VCA group: one fader that moves the faders of the selected tracks together (right-click a track's name to join or leave a group)");
        vcaBtn.onClick = [this]
        {
            evs.edit.getUndoManager().beginNewTransaction ("New VCA");
            const int g = Vca::add (evs.edit);
            for (auto t : evs.selectionManager.getItemsOfType<te::AudioTrack>()) Vca::join (evs.edit, g, *t);
        };
        content.addAndMakeVisible (vcaBtn);

        snapBtn.setTooltip ("Mixer snapshots: store and recall every track's volume, pan, mute, solo and sends");
        snapBtn.onClick = [this] { showSnapshotMenu(); };
        content.addAndMakeVisible (snapBtn);

        sendsBtn.setTooltip ("Every track's send to every return, in one grid");
        sendsBtn.onClick = [this]
        {
            auto m = std::make_unique<SendMatrix> (evs.edit);
            auto* vp = new Viewport();
            vp->setViewedComponent (m.release(), true);
            vp->setSize (jmin (900, vp->getViewedComponent()->getWidth() + 12), jmin (560, vp->getViewedComponent()->getHeight() + 12));
            CallOutBox::launchAsynchronously (std::unique_ptr<Component> (vp), sendsBtn.getScreenBounds(), nullptr);
        };
        content.addAndMakeVisible (sendsBtn);

        narrowBtn.setClickingTogglesState (true);
        narrowBtn.setTooltip ("Narrow strips (fit more channels on screen)");
        narrowBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
        narrowBtn.setToggleState (evs.edit.state.getProperty ("pulseMixerNarrow", false), dontSendNotification);
        narrowBtn.onClick = [this]
        {
            evs.edit.state.setProperty ("pulseMixerNarrow", narrowBtn.getToggleState(), nullptr);
            for (auto* s : strips) s->setNarrow (narrowBtn.getToggleState());
            layout();
        };
        content.addAndMakeVisible (narrowBtn);

        evs.layoutChanged.addChangeListener (this);
        evs.edit.state.addListener (this);
        markAndUpdate (rebuildFlag);
    }

    ~Mixer() override { evs.edit.state.removeListener (this); evs.layoutChanged.removeChangeListener (this); }

    bool isNarrow() const { return narrowBtn.getToggleState(); }
    void setNarrow (bool n) { narrowBtn.setToggleState (n, sendNotificationSync); }
    int numStrips() const { return strips.size(); }
    int stripWidth (int i) const { return strips[i] != nullptr ? strips[i]->getWidth() : 0; }

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
        vcaStrips.clear();
        numContentStrips = 0;

        // tracks and groups, in arrangement order
        for (auto t : te::getAllTracks (evs.edit))
        {
            ChannelStrip* s = nullptr;
            bool hidden = false;   // inside a folded group
            for (auto f = t->getParentFolderTrack(); f != nullptr; f = f->getParentFolderTrack())
                hidden = hidden || evs.collapsed.count (f->itemID) > 0;
            if (hidden) continue;

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

        // the VCA groups, after the tracks they move
        for (auto& g : Vca::all (evs.edit))
            content.addAndMakeVisible (vcaStrips.add (new Vca::Strip (evs, g.index)));

        for (auto t : Routing::getReturnTracks (evs.edit))
        {
            auto* s = strips.add (new ChannelStrip (evs, t, t->getVolumePlugin(), t->getLevelMeterPlugin()));
            content.addAndMakeVisible (s);
        }

        auto masterMeter = evs.edit.getMasterPluginList().findFirstPluginOfType<te::LevelMeterPlugin>();
        auto* m = strips.add (new ChannelStrip (evs, nullptr, evs.edit.getMasterVolumePlugin(), masterMeter));
        content.addAndMakeVisible (m);
        for (auto* s : strips) s->setNarrow (isNarrow());
        layout();
    }

    void layout()
    {
        const int h = jmax (200, viewport.getHeight() - viewport.getScrollBarThickness());
        int x = 8;
        for (int i = 0; i < strips.size(); ++i)
        {
            if (i == numContentStrips)
            {
                for (auto* v : vcaStrips) { v->setBounds (x + 8, 6, 78, h - 12); x += 86; }
                x += 16;          // gap before returns
            }
            if (i == strips.size() - 1)                    // + Return, then master
            {
                addReturnBtn.setBounds (x, 6, 80, 28);
                snapBtn.setBounds (x, 40, 80, 28);
                sendsBtn.setBounds (x, 74, 80, 28);
                narrowBtn.setBounds (x, 108, 80, 28);
                vcaBtn.setBounds (x, 142, 80, 28);
                x += 92;
            }
            const int w = isNarrow() && i != strips.size() - 1 ? 58 : 92;
            strips[i]->setBounds (x, 6, w, h - 12);
            x += w + 4;
        }
        content.setSize (x + 8, h);
    }

    void valueTreeChanged() override {}

    // Only structural changes rebuild: tracks, and return plugins (which create/remove buses).
    // Sends are created lazily while a knob is dragged, so they must not trigger a rebuild.
    static bool affectsLayout (const ValueTree& c)
    {
        return te::TrackList::isTrack (c) || c.hasType (Vca::vcaID)
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
    OwnedArray<Vca::Strip> vcaStrips;
    TextButton addReturnBtn { "+ Return" }, snapBtn { "Snapshots" }, sendsBtn { "Sends" }, narrowBtn { "Narrow" }, vcaBtn { "+ VCA" };

    void changeListenerCallback (ChangeBroadcaster*) override { markAndUpdate (rebuildFlag); }

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
