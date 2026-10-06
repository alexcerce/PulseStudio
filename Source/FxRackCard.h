#pragma once

// Included from DeviceChain.h (uses CardBase, IconButton, PluginMenu).

/** The card of an Effect Rack: its chains on the left (name, level, mute, listen alone), and the effects of the
    chain chosen on the right. */
class FxRackCard : public CardBase,
                   private Timer
{
public:
    FxRackCard (EditViewState& e, te::RackInstance& inst) : evs (e), instanceHolder (&inst), instance (&inst), rack (inst.type)
    {
        title.setText ("Effect Rack", dontSendNotification);
        title.setFont (FontOptions (13.5f, Font::bold));
        title.setInterceptsMouseClicks (false, false);
        dragPlugin = instanceHolder;
        addAndMakeVisible (title);
        power.setClickingTogglesState (true);
        power.setToggleState (instance->isEnabled(), dontSendNotification);
        power.onClick = [this] { instance->setEnabled (power.getToggleState()); repaint(); };
        removeBtn.onClick = [this] { begin ("Remove rack"); instance->deleteFromParent(); };
        addChainBtn.setTooltip ("A new chain beside the others: the sound goes through all of them at once, and they are added together");
        addChainBtn.onClick = [this] { begin ("Add chain"); selected = FxRack::addChain (*rack); refreshSoon(); };
        addFxBtn.setTooltip ("Add an effect to the end of this chain");
        addFxBtn.onClick = [this]
        {
            PluginMenu::show (evs.edit, &addFxBtn, [this, sp = SafePointer<FxRackCard> (this)] (te::Plugin::Ptr p)
            {
                if (sp == nullptr || p == nullptr || p->isSynth()) return;
                begin ("Add effect to chain");
                FxRack::addEffect (*rack, selected, p);
                rebuild();
            });
        };
        hint.setFont (FontOptions (12.0f));
        hint.setColour (Label::textColourId, Theme::muted);
        hint.setJustificationType (Justification::topLeft);
        for (auto* c : std::initializer_list<Component*> { &power, &removeBtn, &addChainBtn, &addFxBtn, &hint }) addAndMakeVisible (c);
        const auto all = FxRack::chains (*rack);
        selected = all.empty() ? -1 : all.front().id;
        setSize (590, 0);
        rebuild();
        startTimerHz (12);
    }

    void paint (Graphics& g) override
    {
        paintCard (g, Theme::accent, instance->isEnabled());
        // the two halves: chains, and the chain chosen
        g.setColour (Theme::ink.withAlpha (0.07f));
        g.drawVerticalLine (divider, (float) headerH + 4.0f, (float) getHeight() - 8.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0);
        auto head = r.removeFromTop (headerH).reduced (0, 6);
        power.setBounds (head.removeFromLeft (22));
        head.removeFromLeft (6);
        removeBtn.setBounds (head.removeFromRight (22));
        head.removeFromRight (6);
        addChainBtn.setBounds (head.removeFromRight (74));
        title.setBounds (head.removeFromLeft (110));
        r.removeFromTop (6);
        r.removeFromBottom (8);
        auto left = r.removeFromLeft (290);
        divider = left.getRight() + 6;
        r.removeFromLeft (12);
        for (auto* row : chainRows) { row->setBounds (left.removeFromTop (28)); left.removeFromTop (2); }
        for (auto* row : fxRows) { row->setBounds (r.removeFromTop (22)); r.removeFromTop (2); }
        addFxBtn.setBounds (r.removeFromTop (22).removeFromLeft (96));
        r.removeFromTop (6);
        hint.setBounds (r);
    }

    //==============================================================================
    // (for the self-test)
    int numChainRows() const   { return chainRows.size(); }
    int numFxRows() const      { return fxRows.size(); }
    int selectedChain() const  { return selected; }
    void selectChain (int id)  { selected = id; rebuild(); }

private:
    void begin (const String& what) { evs.edit.getUndoManager().beginNewTransaction (what); }
    void refreshSoon() { MessageManager::callAsync ([sp = SafePointer<FxRackCard> (this)] { if (sp != nullptr) sp->rebuild(); }); }

    /** One chain: click to choose it; its level, mute, listen-alone, and remove. */
    struct ChainRow : public Component, public SettableTooltipClient
    {
        ChainRow (FxRackCard& o, FxRack::Chain c) : owner (o), chain (c), out (FxRack::output (*o.rack, c.id))
        {
            name.setText (c.name, dontSendNotification);
            name.setFont (FontOptions (12.5f, Font::bold));
            name.setInterceptsMouseClicks (false, false);
            name.setEditable (false, true);
            name.onTextChange = [this] { owner.begin ("Rename chain"); FxRack::renameChain (*owner.rack, chain.id, name.getText()); };
            addAndMakeVisible (name);
            if (auto o = output())
            {
                vol = std::make_unique<ParamBar> (*o->param (pulse::ChainOut::pVol));
                vol->setTooltip ("The chain's level (double-click: 0 dB)");
                addAndMakeVisible (*vol);
            }
            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (true);
                addAndMakeVisible (b);
            }
            mute.setColour (TextButton::buttonOnColourId, Theme::orange);
            solo.setColour (TextButton::buttonOnColourId, Theme::yellow);
            mute.setTooltip ("Mute this chain");
            solo.setTooltip ("Listen to this chain alone");
            mute.onClick = [this] { owner.begin ("Mute chain"); if (auto o = output()) o->set (pulse::ChainOut::pMute, mute.getToggleState() ? 1.0f : 0.0f); };
            solo.onClick = [this] { FxRack::setSolo (*owner.rack, solo.getToggleState() ? chain.id : -1); owner.syncRows(); };
            removeBtn.onClick = [this] { owner.begin ("Remove chain"); owner.removeChain (chain.id); };
            addAndMakeVisible (removeBtn);
            setTooltip ("Click to see this chain's effects. Double-click its name to rename it.");
            sync();
        }

        pulse::ChainOut* output() const { return FxRack::output (*owner.rack, chain.id); }

        void sync()
        {
            auto o = output();
            if (o == nullptr) return;
            mute.setToggleState (o->getInt (pulse::ChainOut::pMute) > 0, dontSendNotification);
            solo.setToggleState (o->soloed.load(), dontSendNotification);
            const float l = jlimit (0.0f, 1.0f, 1.0f + o->levelDb.load() / 60.0f);
            if (std::abs (l - level) > 0.005f) { level = l; repaint(); }
        }

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const bool sel = owner.selected == chain.id;
            g.setColour (sel ? Theme::accent.withAlpha (0.18f) : Pro::shade (0.25f));
            g.fillRoundedRectangle (r, 5.0f);
            if (sel) { g.setColour (Theme::accent); g.fillRoundedRectangle (r.withWidth (3.0f), 1.5f); }
            // how loud the chain is, along the bottom
            g.setColour (Theme::green.withAlpha (0.7f));
            g.fillRect (Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 2.5f, (r.getWidth() - 12.0f) * level, 1.5f));
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 3);
            removeBtn.setBounds (r.removeFromRight (20));
            r.removeFromRight (3);
            solo.setBounds (r.removeFromRight (22));
            r.removeFromRight (2);
            mute.setBounds (r.removeFromRight (22));
            r.removeFromRight (5);
            if (vol != nullptr) vol->setBounds (r.removeFromRight (104).reduced (0, 1));
            r.removeFromRight (5);
            name.setBounds (r);
        }

        void mouseDown (const MouseEvent&) override { owner.selectChain (chain.id); }
        void mouseDoubleClick (const MouseEvent&) override { name.showEditor(); }

        FxRackCard& owner;
        FxRack::Chain chain;
        te::Plugin::Ptr out;
        Label name;
        std::unique_ptr<ParamBar> vol;
        TextButton mute { "M" }, solo { "S" };
        IconButton removeBtn { Icons::Type::close, "Remove this chain and its effects" };
        float level = 0.0f;
    };

    /** One effect of the chain chosen. */
    struct FxRow : public Component
    {
        FxRow (FxRackCard& o, te::Plugin::Ptr p) : owner (o), plugin (p)
        {
            name.setText (p->getName(), dontSendNotification);
            name.setFont (FontOptions (12.5f));
            power.setClickingTogglesState (true);
            power.setToggleState (p->isEnabled(), dontSendNotification);
            power.onClick = [this] { plugin->setEnabled (power.getToggleState()); };
            editBtn.setTooltip ("Open this effect's window");
            editBtn.onClick = [this] { plugin->showWindowExplicitly(); };
            upBtn.onClick = [this] { owner.begin ("Move effect"); FxRack::moveEffect (*owner.rack, *plugin, -1); owner.refreshSoon(); };
            downBtn.onClick = [this] { owner.begin ("Move effect"); FxRack::moveEffect (*owner.rack, *plugin, 1); owner.refreshSoon(); };
            removeBtn.onClick = [this] { owner.begin ("Remove effect"); FxRack::removeEffect (*owner.rack, *plugin); owner.refreshSoon(); };
            for (auto* c : std::initializer_list<Component*> { &power, &name, &editBtn, &upBtn, &downBtn, &removeBtn }) addAndMakeVisible (c);
        }
        void paint (Graphics& g) override
        {
            g.setColour (Pro::shade (0.25f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (3, 1);
            power.setBounds (r.removeFromLeft (20).reduced (1));
            removeBtn.setBounds (r.removeFromRight (20));
            downBtn.setBounds (r.removeFromRight (20));
            upBtn.setBounds (r.removeFromRight (20));
            editBtn.setBounds (r.removeFromRight (44).reduced (1));
            r.removeFromLeft (4);
            name.setBounds (r);
        }
        FxRackCard& owner;
        te::Plugin::Ptr plugin;
        Label name;
        TextButton editBtn { "Edit" };
        IconButton power { Icons::Type::power, "Effect on / off", Theme::green }, upBtn { Icons::Type::chevronUp, "Earlier in the chain" },
                   downBtn { Icons::Type::chevronDown, "Later in the chain" }, removeBtn { Icons::Type::close, "Remove this effect" };
    };

    void removeChain (int id)
    {
        if (FxRack::removeChain (*rack, id) && selected == id)
        {
            const auto all = FxRack::chains (*rack);
            selected = all.empty() ? -1 : all.front().id;
        }
        refreshSoon();
    }

    void syncRows() { for (auto* r : chainRows) r->sync(); }

    String structure() const
    {
        String s;
        for (auto& c : FxRack::chains (*rack))
        {
            s << c.id << ":" << c.name << "[";
            for (auto p : FxRack::effects (*rack, c.id)) s << p->itemID.toString() << (p->isEnabled() ? "+" : "-") << ",";
            s << "]";
        }
        return s;
    }

    void rebuild()
    {
        chainRows.clear();
        fxRows.clear();
        const auto all = FxRack::chains (*rack);
        if (std::none_of (all.begin(), all.end(), [this] (auto& c) { return c.id == selected; })) selected = all.empty() ? -1 : all.front().id;
        for (auto& c : all) addAndMakeVisible (chainRows.add (new ChainRow (*this, c)));
        const auto fx = FxRack::effects (*rack, selected);
        for (auto p : fx) addAndMakeVisible (fxRows.add (new FxRow (*this, p)));
        hint.setText (fx.isEmpty() ? (all.size() < 2 ? "This chain is empty: the sound goes through untouched.\nAdd an effect here, then \"+ Chain\" for a second path beside it."
                                                    : "An empty chain is the sound itself, untouched:\nthe dry sound beside the other chains.")
                                   : String(), dontSendNotification);
        built = structure();
        // as tall as its chains and effects need (the chain's own height when that is more)
        resized();
        repaint();
    }

    void timerCallback() override
    {
        if (structure() != built) rebuild();
        syncRows();
        power.setToggleState (instance->isEnabled(), dontSendNotification);
    }

    EditViewState& evs;
    te::Plugin::Ptr instanceHolder;
    te::RackInstance* instance;
    te::RackType::Ptr rack;
    Label title, hint;
    IconButton power { Icons::Type::power, "Rack on / off", Theme::green }, removeBtn { Icons::Type::close, "Remove the rack and everything in it" };
    TextButton addChainBtn { "+ Chain" }, addFxBtn { "+ Effect" };
    OwnedArray<ChainRow> chainRows;
    OwnedArray<FxRow> fxRows;
    int selected = -1, divider = 280;
    String built;
};
