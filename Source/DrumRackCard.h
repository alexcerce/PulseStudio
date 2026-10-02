#pragma once

// Included from DeviceChain.h (uses CardBase, ParamKnob, PluginMenu, ClipImport).

/** Folders "Randomise kit" searches for samples (set by the main window from the browser's places). */
inline std::function<Array<File>()> drumLibraryFolders;

//==============================================================================
/** A compact horizontal bar bound to a parameter (for the pad mixer list). */
class ParamBar : public Slider,
                 private Timer
{
public:
    explicit ParamBar (te::AutomatableParameter& p) : param (&p)
    {
        auto range = p.getValueRange();
        NormalisableRange<double> nr (range.getStart(), range.getEnd(),
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.convertFrom0to1 ((float) v); },
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.convertTo0to1 ((float) v); },
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.snapToLegalValue ((float) v); });
        setSliderStyle (Slider::LinearBar);
        setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        setNormalisableRange (nr);
        setValue (p.getCurrentValue(), dontSendNotification);
        textFromValueFunction = [this] (double) { return param->getParameterShortName (8) + " " + param->getCurrentValueAsStringWithLabel(); };
        onDragStart = [this] { param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName()); param->parameterChangeGestureBegin(); };
        onDragEnd = [this] { param->parameterChangeGestureEnd(); };
        onValueChange = [this] { param->setParameter ((float) getValue(), sendNotificationSync); };
        if (auto def = p.getDefaultValue()) setDoubleClickReturnValue (true, *def);
        setTooltip (p.getParameterName() + " (double-click to reset)");
        startTimerHz (8);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        g.setColour (Colour (0xff111317));
        g.fillRoundedRectangle (r, 3.0f);
        const float v = (float) valueToProportionOfLength (getValue());
        g.setColour (Theme::accent.withAlpha (isMouseOverOrDragging() ? 0.55f : 0.38f));
        if (getMinimum() < 0.0 && getMaximum() > 0.0)   // bipolar (pan): fill from the centre
        {
            const float c = r.getX() + r.getWidth() * (float) valueToProportionOfLength (0.0), x = r.getX() + r.getWidth() * v;
            g.fillRect (Rectangle<float> (jmin (c, x), r.getY(), jmax (2.0f, std::abs (x - c)), r.getHeight()));
        }
        else
            g.fillRoundedRectangle (r.withWidth (jmax (2.0f, r.getWidth() * v)), 3.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (Theme::text);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText (param->getParameterShortName (8) + "  " + param->getCurrentValueAsStringWithLabel(), getLocalBounds().reduced (5, 0),
                          Justification::centred, 1);
    }

private:
    void timerCallback() override
    {
        if (! isMouseButtonDown()) setValue (param->getCurrentBaseValue(), dontSendNotification);
        repaint();
    }
    te::AutomatableParameter::Ptr param;
};

//==============================================================================
/** Drum Rack editor: bank overview + 4x4 pads (drag to move, Ctrl+drag to copy, drop samples /
    effects, right-click for more) and pages for the selected pad: sample layers, shape (envelope /
    filter), synth, chain (effects + pad mixer), plus a mixer of all pads, the two returns and the
    rack's macros. */
class DrumRackCard : public CardBase,
                     private Timer
{
public:
    enum class Page { sample, shape, synth, chain, mixer, returns, macros };

    DrumRackCard (EditViewState& e, te::RackInstance& inst) : evs (e), instanceHolder (&inst), instance (&inst), rack (inst.type)
    {
        DrumRack::migrate (*rack);
        DrumRack::ensureReturns (*rack);

        for (int i = 0; i < DrumRack::padsPerBank; ++i)
            addAndMakeVisible (pads.add (new Pad (*this, i)));
        addAndMakeVisible (overview);

        title.setText ("Drum Rack", dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        addAndMakeVisible (title);
        power.setClickingTogglesState (true);
        power.setToggleState (instance->isEnabled(), dontSendNotification);
        power.onClick = [this] { instance->setEnabled (power.getToggleState()); repaint(); };
        removeBtn.onClick = [this] { instance->deleteFromParent(); };
        kitBtn.setTooltip ("Kit: synth kit, randomise, clear");
        kitBtn.onClick = [this] { showKitMenu(); };
        bankUp.onClick = [this] { setBank (DrumRack::getBank (*rack) + 16); };
        bankDown.onClick = [this] { setBank (DrumRack::getBank (*rack) - 16); };
        for (auto* c : std::initializer_list<Component*> { &power, &removeBtn, &kitBtn, &bankUp, &bankDown })
            addAndMakeVisible (c);

        const char* names[] = { "Sample", "Shape", "Synth", "Chain", "Mixer", "Returns", "Macros" };
        const char* tips[] = { "The pad's samples: layers, start / end, reverse, play mode, pitch",
                               "Amp envelope, filter and velocity response of the pad",
                               "Synthesise the pad's sound instead of playing a sample",
                               "The pad's effects, volume, pan, sends, receive note and choke group",
                               "Volume, pan, sends, mute and solo of every pad",
                               "The two return chains the pad sends feed",
                               "Eight macro knobs for the rack (right-click any knob here > Map to Rack Macro)" };
        for (int i = 0; i < 7; ++i)
        {
            auto* b = tabs.add (new TextButton (names[i]));
            b->setClickingTogglesState (false);
            b->setColour (TextButton::buttonOnColourId, Theme::accent.withAlpha (0.55f));
            b->setTooltip (tips[i]);
            b->onClick = [this, i] { page = (Page) i; refreshDetail(); };
            addAndMakeVisible (b);
        }

        padTitle.setFont (FontOptions (13.0f, Font::bold));
        addAndMakeVisible (padTitle);
        addAndMakeVisible (pageHolder);

        auto used = DrumRack::usedPads (*rack);
        selected = used.isEmpty() ? DrumRack::getBank (*rack) : used.getFirst();
        if (! inBank (selected)) DrumRack::setBank (*rack, selected / 4 * 4);
        lastStructure = structureHash();
        setSize (1060, 0);
        startTimerHz (15);
    }

    void paint (Graphics& g) override
    {
        paintCard (g, Theme::orange, instance->isEnabled());
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (6);
        removeBtn.setBounds (head.removeFromRight (22));
        head.removeFromRight (6);
        kitBtn.setBounds (head.removeFromRight (64));
        title.setBounds (head.removeFromLeft (110));
        r.removeFromTop (6);
        r.removeFromBottom (6);

        auto ov = r.removeFromLeft (30);
        bankUp.setBounds (ov.removeFromTop (18));
        bankDown.setBounds (ov.removeFromBottom (18));
        overview.setBounds (ov.reduced (0, 2));
        r.removeFromLeft (6);

        auto padArea = r.removeFromLeft (4 * 72);
        const int pw = 70, ph = jmax (24, (padArea.getHeight() - 6) / 4);
        for (int i = 0; i < DrumRack::padsPerBank; ++i)
        {
            const int row = 3 - i / 4, col = i % 4;   // lowest note bottom-left, like Push / Ableton
            pads[i]->setBounds (padArea.getX() + col * 72, padArea.getY() + row * (ph + 2), pw, ph);
        }

        r.removeFromLeft (12);
        auto tabRow = r.removeFromTop (22);
        padTitle.setBounds (tabRow.removeFromRight (jmax (120, tabRow.getWidth() - 7 * 66)));
        for (auto* t : tabs) t->setBounds (tabRow.removeFromLeft (64).reduced (1, 0));
        r.removeFromTop (6);
        pageHolder.setBounds (r);
        refreshDetail();
    }

    int getSelected() const { return selected; }

    /** Shows a page (0 = Sample ... 6 = Macros) for a pad (used by the self-test). */
    void showPage (int pageIndex, int slot)
    {
        page = (Page) jlimit (0, 6, pageIndex);
        if (! inBank (slot)) setBank (slot / 4 * 4);
        select (slot);
    }

private:
    //==============================================================================
    bool inBank (int slot) const { const int b = DrumRack::getBank (*rack); return slot >= b && slot < b + 16; }

    void setBank (int first)
    {
        DrumRack::setBank (*rack, first);
        for (auto* p : pads) p->repaint();
        overview.repaint();
    }

    pulse::DrumPad* sel() const   { return DrumRack::source (*rack, selected); }
    pulse::PadOut* selOut() const { return DrumRack::output (*rack, selected); }

    void select (int slot)
    {
        selected = slot;
        if (auto s = DrumRack::source (*rack, slot))   // show what the pad is made of
        {
            if (page == Page::sample && s->isSynth()) page = Page::synth;
            else if (page == Page::synth && ! s->isSynth() && s->numLayers() > 0) page = Page::sample;
        }
        refreshDetail();
        for (auto* p : pads) p->repaint();
    }

    void audition (int slot, int velocity = 110)
    {
        if (auto t = dynamic_cast<te::AudioTrack*> (instance->getOwnerTrack()))
            t->playGuideNote (DrumRack::noteOfPad (*rack, slot), te::MidiChannel (1), velocity, true, true, true);
    }

    void begin (const String& what) { evs.edit.getUndoManager().beginNewTransaction (what); }

    //==============================================================================
    // Pads
    struct Pad : public Component, public FileDragAndDropTarget, public DragAndDropTarget, public SettableTooltipClient
    {
        Pad (DrumRackCard& o, int i) : owner (o), index (i)
        {
            setTooltip ("Click to play. Drag to another pad to move (Ctrl = copy). Drop samples (Shift = as layers) or effects. Right-click for more.");
        }

        int slot() const { return DrumRack::getBank (*owner.rack) + index; }

        Rectangle<float> muteRect() const { return { 3.0f, (float) getHeight() - 15.0f, 13.0f, 12.0f }; }
        Rectangle<float> soloRect() const { return { 18.0f, (float) getHeight() - 15.0f, 13.0f, 12.0f }; }

        void paint (Graphics& g) override
        {
            const int s = slot();
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            auto src = DrumRack::source (*owner.rack, s);
            auto out = DrumRack::output (*owner.rack, s);
            const bool filled = src != nullptr && ! src->isEmpty();
            const bool sel = owner.selected == s;
            auto col = owner.instance->getOwnerTrack() != nullptr ? owner.instance->getOwnerTrack()->getColour() : Theme::orange;

            g.setColour (filled ? col.withAlpha (hover ? 0.55f : 0.38f) : Colour (0xff1b1e23));
            g.fillRoundedRectangle (r, 5.0f);
            if (flash > 0.01f)
            {
                g.setColour (Colours::white.withAlpha (flash * 0.45f));
                g.fillRoundedRectangle (r, 5.0f);
            }
            g.setColour (sel ? Colours::white : (highlight ? Theme::accent : Theme::line));
            g.drawRoundedRectangle (r, 5.0f, sel || highlight ? 1.8f : 1.0f);

            g.setColour (filled ? Theme::text : Theme::muted.withAlpha (0.8f));
            g.setFont (FontOptions (11.5f, filled ? Font::bold : Font::plain));
            auto name = filled ? src->displayName() : DrumRack::defaultPadLabel (s);
            g.drawFittedText (name, getLocalBounds().reduced (4, 3).withTrimmedBottom (12), Justification::centred, 2);

            g.setFont (FontOptions (9.5f));
            g.setColour (Theme::muted.withAlpha (0.8f));
            const int note = DrumRack::noteOfPad (*owner.rack, s);
            g.drawText (MidiMessage::getMidiNoteName (note, true, true, 3) + (note != s ? "*" : ""), getLocalBounds().reduced (4, 2), Justification::bottomRight);

            if (src != nullptr)
            {
                const bool muted = out != nullptr && out->getInt (pulse::PadOut::pMute) > 0;
                const bool solo = out != nullptr && out->getInt (pulse::PadOut::pSolo) > 0;
                auto box = [&g] (Rectangle<float> b, const String& t, bool on, Colour c)
                {
                    g.setColour (on ? c : Colours::black.withAlpha (0.35f));
                    g.fillRoundedRectangle (b, 2.0f);
                    g.setColour (on ? Colours::black : Theme::muted);
                    g.setFont (FontOptions (9.0f, Font::bold));
                    g.drawText (t, b, Justification::centred);
                };
                box (muteRect(), "M", muted, Theme::orange);
                box (soloRect(), "S", solo, Theme::yellow);
                if (const int ch = src->getInt (pulse::DrumPad::pChoke); ch > 0)
                {
                    g.setColour (Theme::muted);
                    g.drawText ("C" + String (ch), Rectangle<float> (34.0f, (float) getHeight() - 15.0f, 20.0f, 12.0f), Justification::centredLeft);
                }
            }
        }

        void mouseDown (const MouseEvent& e) override
        {
            const int s = slot();
            dragging = false;
            if (e.mods.isPopupMenu()) { owner.select (s); owner.showPadMenu (s, this); return; }
            if (auto out = DrumRack::output (*owner.rack, s); out != nullptr && DrumRack::source (*owner.rack, s) != nullptr)
            {
                if (muteRect().contains (e.position)) { owner.begin ("Mute pad"); out->set (pulse::PadOut::pMute, out->getInt (pulse::PadOut::pMute) > 0 ? 0.0f : 1.0f); repaint(); return; }
                if (soloRect().contains (e.position)) { owner.begin ("Solo pad"); out->set (pulse::PadOut::pSolo, out->getInt (pulse::PadOut::pSolo) > 0 ? 0.0f : 1.0f); owner.repaintPads(); return; }
            }
            owner.select (s);
            // velocity from where the pad is hit: top = loud
            owner.audition (s, jlimit (20, 127, 127 - (int) (e.position.y / (float) getHeight() * 60.0f)));
        }

        void mouseDrag (const MouseEvent& e) override
        {
            if (dragging || e.getDistanceFromDragStart() < 6 || DrumRack::source (*owner.rack, slot()) == nullptr) return;
            if (auto c = DragAndDropContainer::findParentDragContainerFor (this))
            {
                dragging = true;
                c->startDragging ("DrumPad:" + String (slot()), this);
            }
        }

        void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }

        bool isInterestedInFileDrag (const StringArray& f) override { return ! f.isEmpty() && ClipImport::isAudioFile (File (f[0])); }
        void fileDragEnter (const StringArray&, int, int) override { highlight = true; repaint(); }
        void fileDragExit (const StringArray&) override              { highlight = false; repaint(); }
        void filesDropped (const StringArray& f, int, int) override
        {
            highlight = false;
            Array<File> files;
            for (auto& s : f) if (ClipImport::isAudioFile (File (s))) files.add (File (s));
            owner.dropFiles (slot(), files);
        }

        bool isInterestedInDragSource (const SourceDetails& d) override
        {
            const auto s = d.description.toString();
            return s.startsWith ("DrumPad:") || s == "PulseFiles" || ClipImport::isPluginDescription (d.description);
        }
        void itemDragEnter (const SourceDetails&) override { highlight = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { highlight = false; repaint(); }
        void itemDropped (const SourceDetails& d) override
        {
            highlight = false;
            repaint();
            const auto desc = d.description.toString();
            if (desc.startsWith ("DrumPad:"))
            {
                const int from = desc.fromFirstOccurrenceOf (":", false, false).getIntValue();
                owner.dropPad (from, slot(), ModifierKeys::currentModifiers.isCtrlDown());
            }
            else if (desc == "PulseFiles")
            {
                Array<File> files;
                for (auto& f : ClipImport::filesFromDrag (d)) if (ClipImport::isAudioFile (f)) files.add (f);
                owner.dropFiles (slot(), files);
            }
            else
                owner.dropDevice (slot(), desc);
        }

        DrumRackCard& owner;
        int index;
        int seenTriggers = -1;
        float flash = 0;
        bool hover = false, highlight = false, dragging = false;
    };

    void repaintPads() { for (auto* p : pads) p->repaint(); overview.repaint(); }

    /** Rebuilds the page after the current callback returns (rows may delete themselves). */
    void refreshSoon()
    {
        MessageManager::callAsync ([sp = SafePointer<DrumRackCard> (this)] { if (sp != nullptr) { sp->refreshDetail(); sp->repaintPads(); } });
    }

    //==============================================================================
    /** All 128 pads at a glance (rows of 4, low notes at the bottom); click to show a bank. */
    struct Overview : public Component, public SettableTooltipClient
    {
        explicit Overview (DrumRackCard& o) : owner (o) { setTooltip ("All 128 pads. Click or scroll to choose which 16 are shown."); }

        float rowH() const { return (float) getHeight() / 32.0f; }

        void paint (Graphics& g) override
        {
            auto used = DrumRack::usedPads (*owner.rack);
            const float rh = rowH(), cw = (float) getWidth() / 4.0f;
            const int bank = DrumRack::getBank (*owner.rack);
            for (int s = 0; s < 128; ++s)
            {
                const int row = s / 4, col = s % 4;
                Rectangle<float> cell (col * cw, (31 - row) * rh, cw, rh);
                g.setColour (used.contains (s) ? Theme::orange.withAlpha (0.85f) : Colour (0xff23272e));
                if (s == owner.selected) g.setColour (Colours::white);
                g.fillRect (cell.reduced (0.6f));
            }
            g.setColour (Theme::accent);
            g.drawRect (Rectangle<float> (0.0f, (31 - (bank / 4 + 3)) * rh, (float) getWidth(), rh * 4).expanded (0.5f), 1.2f);
            // C1 marker
            g.setColour (Theme::muted.withAlpha (0.5f));
            g.drawHorizontalLine ((int) ((31 - 9) * rh + rh), 0.0f, (float) getWidth());
        }

        void mouseDown (const MouseEvent& e) override { mouseDrag (e); }
        void mouseDrag (const MouseEvent& e) override
        {
            const int row = 31 - jlimit (0, 31, (int) (e.position.y / rowH()));
            owner.setBank ((row - 1) * 4);
        }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            owner.setBank (DrumRack::getBank (*owner.rack) + (w.deltaY > 0 ? 4 : -4));
        }

        DrumRackCard& owner;
    } overview { *this };

    //==============================================================================
    /** Waveform of the selected pad's (last played) layer with draggable start / end. */
    struct WaveView : public Component
    {
        WaveView (DrumRackCard& o, pulse::DrumPad& p) : owner (o), pad (&p) {}

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (Colour (0xff0f1115));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Theme::line);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

            auto layers = pad->getLayers();
            if (layers == nullptr || layers->empty())
            {
                g.setColour (Theme::muted);
                g.setFont (FontOptions (12.0f));
                g.drawText (pad->isSynth() ? "Synth pad (see the Synth page)" : "Drop a sample on the pad", r, Justification::centred);
                return;
            }
            const int li = jlimit (0, (int) layers->size() - 1, pad->lastLayer.load());
            auto& data = *(*layers)[(size_t) li].data;
            const float s = xFor (jmin (pad->get (pulse::DrumPad::pStart), pad->get (pulse::DrumPad::pEnd)));
            const float e = xFor (jmax (pad->get (pulse::DrumPad::pStart), pad->get (pulse::DrumPad::pEnd)));
            const bool rev = pad->getInt (pulse::DrumPad::pReverse) > 0;

            g.setColour (Theme::orange.withAlpha (0.85f));
            const float mid = r.getCentreY(), h = r.getHeight() * 0.44f;
            auto& ov = data.overview;
            for (int x = 0; x < (int) r.getWidth(); ++x)
            {
                const int idx = jmin ((int) ov.size() - 1, x * (int) ov.size() / jmax (1, (int) r.getWidth()));
                auto [lo, hi] = ov[(size_t) (rev ? (int) ov.size() - 1 - idx : idx)];
                g.drawVerticalLine (x, mid - hi * h, mid - lo * h + 1.0f);
            }
            g.setColour (Colours::black.withAlpha (0.5f));
            g.fillRect (r.withRight (s));
            g.fillRect (r.withLeft (e));
            g.setColour (Theme::green);  g.fillRect (s - 1.0f, 0.0f, 2.0f, r.getHeight());
            g.setColour (Theme::red);    g.fillRect (e - 1.0f, 0.0f, 2.0f, r.getHeight());

            if (const float ph = pad->playhead.load(); ph >= 0.0f)
            {
                g.setColour (Colours::white);
                g.drawVerticalLine ((int) xFor (ph), 0.0f, r.getHeight());
            }
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f));
            g.drawText (data.file.getFileName() + (layers->size() > 1 ? "  (layer " + String (li + 1) + ")" : String())
                          + (rev ? "  reversed" : ""), r.reduced (6, 3), Justification::topLeft);
        }

        float xFor (float v) const { return v * (float) getWidth(); }

        void mouseDown (const MouseEvent& e) override
        {
            const float s = xFor (pad->get (pulse::DrumPad::pStart)), en = xFor (pad->get (pulse::DrumPad::pEnd));
            dragging = std::abs (e.position.x - s) <= std::abs (e.position.x - en) ? pulse::DrumPad::pStart : pulse::DrumPad::pEnd;
            owner.begin ("Pad start / end");
            mouseDrag (e);
        }
        void mouseDrag (const MouseEvent& e) override
        {
            pad->set (dragging, jlimit (0.0f, 1.0f, e.position.x / (float) getWidth()));
            repaint();
        }

        DrumRackCard& owner;
        pulse::DrumPad* pad;
        te::Plugin::Ptr padHolder { pad };
        int dragging = pulse::DrumPad::pStart;
    };

    //==============================================================================
    struct LayerRow : public Component
    {
        LayerRow (DrumRackCard& o, pulse::DrumPad& p, int i) : owner (o), pad (&p), index (i)
        {
            name.setText (String (i + 1) + ". " + p.layerFile (i).getFileNameWithoutExtension(), dontSendNotification);
            name.setFont (FontOptions (11.5f));
            name.setTooltip (p.layerFile (i).getFullPathName());
            auto v = p.layerVelocity (i);
            vel.setSliderStyle (Slider::TwoValueHorizontal);
            vel.setRange (1.0, 127.0, 1.0);
            vel.setMinAndMaxValues (v.getStart(), v.getEnd(), dontSendNotification);
            vel.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            vel.setTooltip ("Velocity range of this layer: " + String (v.getStart()) + " - " + String (v.getEnd()));
            vel.onDragStart = [this] { owner.begin ("Layer velocity"); };
            vel.onValueChange = [this]
            {
                pad->setLayerVelocity (index, (int) vel.getMinValue(), (int) vel.getMaxValue());
                range.setText (String ((int) vel.getMinValue()) + "-" + String ((int) vel.getMaxValue()), dontSendNotification);
            };
            range.setText (String (v.getStart()) + "-" + String (v.getEnd()), dontSendNotification);
            range.setFont (FontOptions (10.5f));
            range.setColour (Label::textColourId, Theme::muted);
            removeBtn.onClick = [this] { owner.begin ("Remove layer"); pad->removeLayer (index); owner.refreshSoon(); };
            for (auto* c : std::initializer_list<Component*> { &name, &vel, &range, &removeBtn }) addAndMakeVisible (c);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            removeBtn.setBounds (r.removeFromRight (18).reduced (0, 1));
            range.setBounds (r.removeFromRight (44));
            vel.setBounds (r.removeFromRight (jmin (110, r.getWidth() / 2)));
            name.setBounds (r);
        }
        DrumRackCard& owner;
        pulse::DrumPad* pad;
        te::Plugin::Ptr holder { pad };
        int index;
        Label name, range;
        Slider vel;
        IconButton removeBtn { Icons::Type::close, "Remove this layer" };
    };

    //==============================================================================
    struct FxRow : public Component
    {
        FxRow (DrumRackCard& o, te::Plugin::Ptr p) : owner (o), plugin (p)
        {
            name.setText (p->getName(), dontSendNotification);
            name.setFont (FontOptions (12.0f));
            power.setClickingTogglesState (true);
            power.setToggleState (p->isEnabled(), dontSendNotification);
            power.onClick = [this] { plugin->setEnabled (power.getToggleState()); };
            editBtn.onClick = [this] { plugin->showWindowExplicitly(); };
            leftBtn.onClick = [this] { owner.begin ("Move effect"); DrumRack::moveEffect (*owner.rack, *plugin, -1); owner.refreshSoon(); };
            rightBtn.onClick = [this] { owner.begin ("Move effect"); DrumRack::moveEffect (*owner.rack, *plugin, 1); owner.refreshSoon(); };
            removeBtn.onClick = [this] { owner.begin ("Remove effect"); DrumRack::removeEffect (*owner.rack, *plugin); owner.refreshSoon(); };
            for (auto* c : std::initializer_list<Component*> { &power, &name, &editBtn, &leftBtn, &rightBtn, &removeBtn }) addAndMakeVisible (c);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            power.setBounds (r.removeFromLeft (20).reduced (1));
            removeBtn.setBounds (r.removeFromRight (20));
            rightBtn.setBounds (r.removeFromRight (20));
            leftBtn.setBounds (r.removeFromRight (20));
            editBtn.setBounds (r.removeFromRight (40).reduced (1));
            name.setBounds (r);
        }
        DrumRackCard& owner;
        te::Plugin::Ptr plugin;
        Label name;
        TextButton editBtn { "Edit" };
        IconButton power { Icons::Type::power, "Effect on / off", Theme::green }, leftBtn { Icons::Type::chevronUp, "Earlier in the chain" },
                   rightBtn { Icons::Type::chevronDown, "Later in the chain" }, removeBtn { Icons::Type::close, "Remove this effect" };
    };

    //==============================================================================
    /** One pad (or return) in the Mixer page. */
    struct MixerRow : public Component, private Timer
    {
        MixerRow (DrumRackCard& o, int s) : owner (o), slot (s)
        {
            auto out = DrumRack::output (*o.rack, s);
            outHolder = out;
            auto src = DrumRack::source (*o.rack, s);
            name.setText (DrumRack::isReturnSlot (s) ? String (s == DrumRack::returnA ? "Return A" : "Return B")
                                                      : (src != nullptr ? src->displayName() : DrumRack::defaultPadLabel (s)), dontSendNotification);
            name.setFont (FontOptions (11.5f, Font::bold));
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);
            if (out == nullptr) return;
            for (int p : { pulse::PadOut::pVol, pulse::PadOut::pPan, pulse::PadOut::pSendA, pulse::PadOut::pSendB })
            {
                if (DrumRack::isReturnSlot (s) && p >= pulse::PadOut::pSendA) continue;
                addAndMakeVisible (bars.add (new ParamBar (*out->param (p))));
            }
            auto toggle = [this, out] (TextButton& b, int p, Colour c)
            {
                b.setClickingTogglesState (true);
                b.setColour (TextButton::buttonOnColourId, c);
                b.setColour (TextButton::textColourOnId, Colours::black);
                b.setToggleState (out->getInt (p) > 0, dontSendNotification);
                b.onClick = [this, out, p, &b] { owner.begin ("Pad mixer"); out->set (p, b.getToggleState() ? 1.0f : 0.0f); owner.repaintPads(); };
                addAndMakeVisible (b);
            };
            toggle (mute, pulse::PadOut::pMute, Theme::orange);
            if (! DrumRack::isReturnSlot (s)) toggle (solo, pulse::PadOut::pSolo, Theme::yellow);
            startTimerHz (20);
        }

        void mouseDown (const MouseEvent&) override { if (! DrumRack::isReturnSlot (slot)) owner.select (slot); }

        void paint (Graphics& g) override
        {
            if (owner.selected == slot) { g.setColour (Theme::accent.withAlpha (0.12f)); g.fillRect (getLocalBounds()); }
            auto m = meterArea.toFloat();
            g.setColour (Colour (0xff101216));
            g.fillRect (m);
            const float lvl = jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (level, -60.0f) + 60.0f) / 66.0f);
            g.setColour (level > 0.98f ? Theme::red : Theme::green);
            g.fillRect (m.withWidth (m.getWidth() * lvl));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (2, 1);
            name.setBounds (r.removeFromLeft (120));
            mute.setBounds (r.removeFromLeft (22).reduced (1));
            solo.setBounds (r.removeFromLeft (22).reduced (1));
            meterArea = r.removeFromRight (50).reduced (2, 6);
            const int w = bars.isEmpty() ? 0 : r.getWidth() / jmax (4, bars.size());
            for (auto* b : bars) b->setBounds (r.removeFromLeft (w).reduced (2, 1));
        }

        void timerCallback() override
        {
            if (auto out = dynamic_cast<pulse::PadOut*> (outHolder.get()))
            {
                level = jmax (out->readPeak(), level * 0.8f);
                repaint (meterArea);
                mute.setToggleState (out->getInt (pulse::PadOut::pMute) > 0, dontSendNotification);
                solo.setToggleState (out->getInt (pulse::PadOut::pSolo) > 0, dontSendNotification);
            }
        }

        DrumRackCard& owner;
        int slot;
        te::Plugin::Ptr outHolder;
        Label name;
        OwnedArray<ParamBar> bars;
        TextButton mute { "M" }, solo { "S" };
        Rectangle<int> meterArea;
        float level = 0;
    };

    //==============================================================================
    // Page building
    struct PageHolder : public Component {} ;

    /** Adds knobs for some parameters, wrapping into rows of 78 px. Returns the area used. */
    Rectangle<int> addKnobs (pulse::PulsePlugin& p, std::initializer_list<int> ids, Rectangle<int> area)
    {
        constexpr int kw = 62, kh = 76;
        int x = area.getX(), y = area.getY();
        for (int id : ids)
        {
            if (x + kw > area.getRight()) { x = area.getX(); y += kh; }
            auto* k = new ParamKnob (evs, *p.param (id));
            k->setBounds (x, y, kw, kh);
            pageHolder.addAndMakeVisible (pageItems.add (k));
            x += kw;
        }
        return { area.getX(), area.getY(), area.getWidth(), y + kh - area.getY() };
    }

    TextButton* addButton (const String& text, Rectangle<int> b, std::function<void()> fn, const String& tip = {})
    {
        auto* t = new TextButton (text);
        t->setBounds (b);
        t->setTooltip (tip);
        t->onClick = std::move (fn);
        pageHolder.addAndMakeVisible (pageItems.add (t));
        return t;
    }

    Label* addLabel (const String& text, Rectangle<int> b, bool bold = false, Colour c = Theme::muted)
    {
        auto* l = new Label (String(), text);
        l->setFont (FontOptions (bold ? 11.5f : 12.0f, bold ? Font::bold : Font::plain));
        l->setColour (Label::textColourId, c);
        l->setBounds (b);
        pageHolder.addAndMakeVisible (pageItems.add (l));
        return l;
    }

    void addChain (int slot, Rectangle<int> area, const String& addLabelText)
    {
        for (auto p : DrumRack::chain (*rack, slot))
        {
            if (area.getHeight() < 44) { addLabel ("(more effects...)", area.removeFromTop (18)); break; }
            auto* row = new FxRow (*this, p);
            row->setBounds (area.removeFromTop (22));
            pageHolder.addAndMakeVisible (pageItems.add (row));
            area.removeFromTop (2);
        }
        auto* b = addButton (addLabelText, area.removeFromTop (22).removeFromLeft (110), {});
        b->onClick = [this, slot, b, sp = SafePointer<DrumRackCard> (this)]
        {
            PluginMenu::show (evs.edit, b, [this, slot, sp] (te::Plugin::Ptr p)
            {
                if (sp == nullptr || p == nullptr || p->isSynth()) return;
                begin ("Add pad effect");
                DrumRack::addEffect (*rack, slot, p);
                refreshDetail();
            });
        };
    }

    void refreshDetail()
    {
        pageItems.clear();
        shownChain = DrumRack::chain (*rack, selected);
        for (int i = 0; i < tabs.size(); ++i)
            tabs[i]->setToggleState ((int) page == i, dontSendNotification);

        auto src = sel();
        const int note = DrumRack::noteOfPad (*rack, selected);
        padTitle.setText ((src != nullptr && ! src->isEmpty() ? src->displayName() : DrumRack::defaultPadLabel (selected))
                            + "   " + MidiMessage::getMidiNoteName (note, true, true, 3), dontSendNotification);
        padTitle.setJustificationType (Justification::centredRight);

        auto area = pageHolder.getLocalBounds();
        if (area.isEmpty()) return;
        const bool padPage = page == Page::sample || page == Page::shape || page == Page::synth || page == Page::chain;

        if (padPage && src == nullptr)
        {
            addLabel ("Empty pad. Drop samples on it, or:", area.removeFromTop (22));
            auto row = area.removeFromTop (26);
            addButton ("Load sample...", row.removeFromLeft (110).reduced (1), [this] { browseSamples (selected, false); });
            row.removeFromLeft (6);
            auto* synthBtn = addButton ("Synth drum...", row.removeFromLeft (110).reduced (1), {});
            synthBtn->onClick = [this, synthBtn] { showSynthMenu (selected, synthBtn); };
            area.removeFromTop (10);
            addLabel ("Tip: drag a pad onto another to move it (Ctrl+drag copies). Shift+drop adds samples as velocity layers.",
                      area.removeFromTop (20));
            return;
        }

        switch (page)
        {
            case Page::sample:
            {
                auto top = area.removeFromTop (24);
                addButton ("Load...", top.removeFromLeft (70).reduced (1), [this] { browseSamples (selected, false); }, "Replace the pad's sample(s)");
                addButton ("+ Layer...", top.removeFromLeft (80).reduced (1), [this] { browseSamples (selected, true); }, "Add velocity / round-robin layers");
                addButton ("Spread velocity", top.removeFromLeft (110).reduced (1), [this]
                {
                    if (auto s = sel()) { begin ("Spread layer velocities"); s->spreadVelocities(); refreshDetail(); }
                }, "Split 1-127 evenly across the layers (layer 1 softest)");
                addButton ("Clear", top.removeFromLeft (56).reduced (1), [this] { begin ("Clear pad"); DrumRack::clearPad (*rack, selected); refreshDetail(); repaintPads(); });
                area.removeFromTop (4);

                auto knobs = area.removeFromBottom (76);
                addKnobs (*src, { pulse::DrumPad::pLayerMode, pulse::DrumPad::pStart, pulse::DrumPad::pEnd, pulse::DrumPad::pReverse,
                                  pulse::DrumPad::pPlayMode, pulse::DrumPad::pTranspose, pulse::DrumPad::pFine, pulse::DrumPad::pGain }, knobs);
                area.removeFromBottom (4);

                auto list = area.removeFromRight (jmin (300, area.getWidth() / 2));
                auto* wave = new WaveView (*this, *src);
                wave->setBounds (area.withTrimmedRight (8));
                pageHolder.addAndMakeVisible (pageItems.add (wave));
                waveView = wave;

                if (src->numLayers() == 0)
                    addLabel (src->isSynth() ? "This pad is synthesised." : "No samples yet.", list.removeFromTop (20));
                for (int i = 0; i < src->numLayers() && list.getHeight() >= 18; ++i)
                {
                    auto* row = new LayerRow (*this, *src, i);
                    row->setBounds (list.removeFromTop (20));
                    pageHolder.addAndMakeVisible (pageItems.add (row));
                }
                break;
            }
            case Page::shape:
                addKnobs (*src, { pulse::DrumPad::pA, pulse::DrumPad::pD, pulse::DrumPad::pS, pulse::DrumPad::pR,
                                  pulse::DrumPad::pFilterType, pulse::DrumPad::pCutoff, pulse::DrumPad::pReso, pulse::DrumPad::pVelFilter,
                                  pulse::DrumPad::pVelSens, pulse::DrumPad::pGain }, area);
                break;

            case Page::synth:
            {
                auto top = area.removeFromTop (24);
                auto* b = addButton (src->isSynth() ? "Synth: on" : "Use synth", top.removeFromLeft (90).reduced (1), {});
                b->setClickingTogglesState (false);
                b->setColour (TextButton::buttonColourId, src->isSynth() ? Theme::accent.darker (0.3f) : Theme::panel2);
                b->onClick = [this]
                {
                    if (auto s = sel()) { begin ("Pad source"); s->set (pulse::DrumPad::pSource, s->isSynth() ? 0.0f : 1.0f); refreshDetail(); repaintPads(); }
                };
                addButton ("Play", top.removeFromLeft (56).reduced (1), [this] { audition (selected); });
                addLabel (src->isSynth() ? "" : "  (the pad plays its samples until the synth is switched on)", top);
                area.removeFromTop (4);
                addKnobs (*src, { pulse::DrumPad::pSynthType, pulse::DrumPad::pSynthTune, pulse::DrumPad::pSynthDecay, pulse::DrumPad::pSynthTone,
                                  pulse::DrumPad::pSynthSnap, pulse::DrumPad::pSynthDrive, pulse::DrumPad::pA, pulse::DrumPad::pFilterType,
                                  pulse::DrumPad::pCutoff, pulse::DrumPad::pReso }, area);
                break;
            }
            case Page::chain:
            {
                auto left = area.removeFromLeft (jmin (area.getWidth(), 4 * 62 + 8));
                if (auto out = selOut())
                    addKnobs (*out, { pulse::PadOut::pVol, pulse::PadOut::pPan, pulse::PadOut::pSendA, pulse::PadOut::pSendB }, left.removeFromTop (76));
                left.removeFromTop (8);
                addKnobs (*src, { pulse::DrumPad::pNote, pulse::DrumPad::pChoke }, left.removeFromTop (76));
                area = area.removeFromLeft (jmin (area.getWidth(), 380));
                addLabel ("PAD EFFECTS (in order, then the pad mixer)", area.removeFromTop (16), true);
                addChain (selected, area, "+ Pad effect");
                break;
            }
            case Page::mixer:
            {
                auto used = DrumRack::usedPads (*rack);
                auto header = area.removeFromTop (16);
                addLabel (used.isEmpty() ? "No pads yet." : "PAD   M  S        VOLUME / PAN / SEND A / SEND B", header, true);
                struct Content : public Component { OwnedArray<Component> rows; };
                auto* vp = new Viewport();
                auto* content = new Content();
                vp->setViewedComponent (content, true);
                vp->setScrollBarsShown (true, false);
                vp->setBounds (area);
                pageHolder.addAndMakeVisible (pageItems.add (vp));
                const int rowH = 24, w = area.getWidth() - vp->getScrollBarThickness();
                int y = 0;
                for (int s : used)
                {
                    auto* row = content->rows.add (new MixerRow (*this, s));
                    row->setBounds (0, y, w, rowH);
                    content->addAndMakeVisible (row);
                    y += rowH;
                }
                for (int s : { DrumRack::returnA, DrumRack::returnB })
                {
                    auto* row = content->rows.add (new MixerRow (*this, s));
                    row->setBounds (0, y, w, rowH);
                    content->addAndMakeVisible (row);
                    y += rowH;
                }
                content->setSize (w, y);
                break;
            }
            case Page::returns:
            {
                const int half = area.getWidth() / 2;
                for (int r : { DrumRack::returnA, DrumRack::returnB })
                {
                    auto col = area.removeFromLeft (half).reduced (4, 0).withWidth (jmin (half - 8, 360));
                    addLabel (r == DrumRack::returnA ? "RETURN A" : "RETURN B", col.removeFromTop (16), true, Theme::text);
                    if (auto out = DrumRack::output (*rack, r))
                    {
                        auto knobRow = col.removeFromTop (76);
                        addKnobs (*out, { pulse::PadOut::pVol, pulse::PadOut::pPan, pulse::PadOut::pMute }, knobRow);
                    }
                    addChain (r, col, "+ Return effect");
                }
                break;
            }
            case Page::macros:
            {
                DrumRack::ensureMacros (*rack);
                addLabel ("Right-click any knob of this rack's pads or effects > Map to Rack Macro.", area.removeFromTop (18));
                auto ms = DrumRack::macros (*rack);
                int x = area.getX();
                for (int i = 0; i < ms.size(); ++i)
                {
                    auto* k = new ParamKnob (evs, *ms[i]);
                    k->setBounds (x, area.getY() + 2, 70, 80);
                    pageHolder.addAndMakeVisible (pageItems.add (k));
                    StringArray targets;
                    for (auto p : rack->getPlugins())
                        for (auto prm : p->getAutomatableParameters())
                            if (Macros::isMapped (*prm, *ms[i]))
                                targets.add (p->getName() + ": " + prm->getParameterName());
                    k->setTooltip (targets.isEmpty() ? "Not mapped yet" : "Controls: " + targets.joinIntoString (", "));
                    x += 72;
                }
                break;
            }
        }
    }

    //==============================================================================
    // Actions
    void dropFiles (int slot, const Array<File>& files)
    {
        if (files.isEmpty()) return;
        const bool asLayers = ModifierKeys::currentModifiers.isShiftDown();
        begin ("Load drum samples");
        if (asLayers)
        {
            for (auto& f : files) DrumRack::addLayer (*rack, slot, f);
            if (auto s = DrumRack::source (*rack, slot)) s->spreadVelocities();
        }
        else
        {
            // several files: fill consecutive pads
            for (int i = 0; i < files.size() && slot + i < DrumRack::numPads; ++i)
                DrumRack::loadSample (*rack, slot + i, files[i]);
        }
        select (slot);
        repaintPads();
    }

    void dropPad (int from, int to, bool copy)
    {
        if (from == to) return;
        begin (copy ? "Copy pad" : "Move pad");
        if (copy) DrumRack::copyPad (*rack, from, to);
        else      DrumRack::movePad (*rack, from, to);
        select (to);
        repaintPads();
    }

    void dropDevice (int slot, const String& desc)
    {
        if (auto p = ClipImport::createPluginFromDescription (evs.edit, desc))
        {
            begin ("Add pad effect");
            if (auto ds = dynamic_cast<pulse::DrumSynth*> (p.get()))
            {
                ignoreUnused (ds);
                DrumRack::loadSynth (*rack, slot, pulse::DrumVoice::kick);
            }
            else if (! p->isSynth())
                DrumRack::addEffect (*rack, slot, p);
            select (slot);
            repaintPads();
        }
    }

    void browseSamples (int slot, bool asLayer)
    {
        chooser = std::make_shared<FileChooser> (asLayer ? "Add layers" : "Load sample", File::getSpecialLocation (File::userMusicDirectory),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this, slot, asLayer, sp = SafePointer<DrumRackCard> (this)] (const FileChooser& fc)
                              {
                                  if (sp == nullptr || fc.getResults().isEmpty()) return;
                                  begin (asLayer ? "Add layers" : "Load drum sample");
                                  if (asLayer)
                                  {
                                      for (auto& f : fc.getResults()) DrumRack::addLayer (*rack, slot, f);
                                      if (auto s = DrumRack::source (*rack, slot)) s->spreadVelocities();
                                  }
                                  else
                                  {
                                      auto res = fc.getResults();
                                      for (int i = 0; i < res.size() && slot + i < DrumRack::numPads; ++i)
                                          DrumRack::loadSample (*rack, slot + i, res[i]);
                                  }
                                  select (slot);
                                  repaintPads();
                              });
    }

    void showSynthMenu (int slot, Component* target)
    {
        PopupMenu m;
        auto& names = pulse::DrumVoice::typeNames();
        for (int i = 0; i < names.size(); ++i)
            m.addItem (names[i], [this, slot, i]
            {
                begin ("Synth drum");
                DrumRack::loadSynth (*rack, slot, i);
                if (page != Page::synth && page != Page::chain) page = Page::synth;
                select (slot);
                audition (slot);
            });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
    }

    void showPadMenu (int slot, Component* target)
    {
        const bool has = DrumRack::source (*rack, slot) != nullptr;
        PopupMenu m;
        m.addSectionHeader (DrumRack::defaultPadLabel (slot) + " pad (" + MidiMessage::getMidiNoteName (slot, true, true, 3) + ")");
        m.addItem ("Load sample...", [this, slot] { browseSamples (slot, false); });
        m.addItem ("Add layers...", [this, slot] { browseSamples (slot, true); });
        PopupMenu synthSub;
        auto& names = pulse::DrumVoice::typeNames();
        for (int i = 0; i < names.size(); ++i)
            synthSub.addItem (names[i], [this, slot, i] { begin ("Synth drum"); DrumRack::loadSynth (*rack, slot, i); select (slot); audition (slot); });
        m.addSubMenu ("Synth drum", synthSub);
        m.addSeparator();
        m.addItem ("Copy pad", has, false, [this, slot] { clipboardSlot = slot; });
        m.addItem ("Paste pad", clipboardSlot >= 0 && clipboardSlot != slot && DrumRack::source (*rack, clipboardSlot) != nullptr, false,
                   [this, slot] { dropPad (clipboardSlot, slot, true); });
        m.addItem ("Duplicate to next free pad", has, false, [this, slot]
        {
            const int to = DrumRack::nextFreeSlot (*rack, slot + 1);
            if (to >= 0) { dropPad (slot, to, true); if (! inBank (to)) setBank (to / 4 * 4 - 12); }
        });
        if (has)
        {
            auto src = DrumRack::source (*rack, slot);
            PopupMenu choke;
            for (int c = 0; c <= 16; ++c)
                choke.addItem (c == 0 ? String ("None") : "Group " + String (c), true, src->getInt (pulse::DrumPad::pChoke) == c,
                               [this, slot, c] { if (auto s = DrumRack::source (*rack, slot)) { begin ("Choke group"); s->set (pulse::DrumPad::pChoke, (float) c); repaintPads(); } });
            m.addSubMenu ("Choke group", choke);
            m.addItem ("Reset receive note", src->getInt (pulse::DrumPad::pNote) != slot, false,
                       [this, slot] { if (auto s = DrumRack::source (*rack, slot)) { begin ("Receive note"); s->set (pulse::DrumPad::pNote, (float) slot); repaintPads(); } });
            m.addSeparator();
            m.addItem ("Extract to new track", [this, slot]
            {
                auto t = dynamic_cast<te::AudioTrack*> (instance->getOwnerTrack());
                if (t == nullptr) return;
                begin ("Extract pad");
                if (auto nt = DrumRack::extractToTrack (*t, *rack, slot))
                    MessageManager::callAsync ([&sm = evs.selectionManager, ptr = te::Track::Ptr (nt)] { sm.selectOnly (ptr.get()); });
            });
            m.addItem ("Clear pad", [this, slot] { begin ("Clear pad"); DrumRack::clearPad (*rack, slot); refreshDetail(); repaintPads(); });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
    }

    void showKitMenu()
    {
        PopupMenu m;
        m.addItem ("Load synth kit (GM layout)", [this]
        {
            begin ("Synth kit");
            DrumRack::fillSynthKit (*rack);
            setBank (DrumRack::baseNote);
            select (DrumRack::baseNote);
        });
        m.addItem ("Randomise kit (from your sample folders)", [this]
        {
            Array<File> folders;
            if (drumLibraryFolders) folders = drumLibraryFolders();
            MouseCursor::showWaitCursor();
            auto samples = DrumRack::findSamples (folders);
            MouseCursor::hideWaitCursor();
            begin ("Randomise kit");
            Random rng;
            const int got = DrumRack::randomiseKit (*rack, samples, rng);
            setBank (DrumRack::baseNote);
            select (DrumRack::baseNote);
            if (got == 0)
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Randomise kit",
                    "No drum samples were found in your browser folders, so the kit was synthesised with random settings.\n\n"
                    "Add a sample folder in the browser (Places > Add folder) to randomise from your own samples.");
        });
        m.addSeparator();
        m.addItem ("Clear all pads", [this]
        {
            begin ("Clear all pads");
            for (int s : DrumRack::usedPads (*rack)) DrumRack::clearPad (*rack, s);
            refreshDetail();
            repaintPads();
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&kitBtn));
    }

    //==============================================================================
    int64 structureHash()
    {
        int64 h = 17;
        for (auto p : rack->getPlugins())
            h = h * 31 + (int64) p->itemID.getRawID() * 7 + padOf (p);
        return h;
    }
    static int padOf (te::Plugin* p) { return DrumRack::padOf (*p); }

    void timerCallback() override
    {
        for (auto* p : pads)
        {
            auto src = DrumRack::source (*rack, p->slot());
            const int t = src != nullptr ? src->triggerCount.load() : 0;
            if (p->seenTriggers >= 0 && t != p->seenTriggers) p->flash = 1.0f;
            p->seenTriggers = t;
            if (p->flash > 0.0f) { p->flash = jmax (0.0f, p->flash - 0.12f); p->repaint(); }
        }
        if (waveView != nullptr && (page == Page::sample)) waveView->repaint();

        if (++slowTick % 4 == 0)
        {
            const auto h = structureHash();
            if (h != lastStructure)
            {
                lastStructure = h;
                refreshDetail();
                repaintPads();
            }
        }
    }

    EditViewState& evs;
    te::Plugin::Ptr instanceHolder;   // keeps the instance alive while the card exists
    te::RackInstance* instance;
    te::RackType::Ptr rack;
    OwnedArray<Pad> pads;
    OwnedArray<TextButton> tabs;
    PageHolder pageHolder;
    OwnedArray<Component> pageItems;
    Component::SafePointer<Component> waveView;
    Array<te::Plugin*> shownChain;
    Page page = Page::sample;
    int selected = DrumRack::baseNote, clipboardSlot = -1, slowTick = 0;
    int64 lastStructure = 0;
    Label title, padTitle;
    IconButton power { Icons::Type::power, "Drum Rack on / off", Theme::green }, removeBtn { Icons::Type::close, "Remove device" },
               bankUp { Icons::Type::chevronUp, "Show higher pads" }, bankDown { Icons::Type::chevronDown, "Show lower pads" };
    TextButton kitBtn { "Kit" };
    std::shared_ptr<FileChooser> chooser;
};
