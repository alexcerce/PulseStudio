#pragma once

#include "EditViewState.h"

/** VCA faders: a group fader that turns its member tracks up and down together (each keeps its own balance within
    the group), without their sound being mixed into a group first. Kept in the project. */
namespace Vca
{
    inline const Identifier listID { "PULSEVCAS" }, vcaID { "VCA" }, nameID { "name" }, levelID { "level" }, mutedID { "muted" }, membersID { "members" };
    constexpr float lowest = -60.0f, highest = 6.0f;

    struct Group
    {
        String name;
        float level = 0.0f;   // dB the group has been moved by
        bool muted = false;
        StringArray members;  // track ids
        int index = -1;
    };

    inline Colour colourOf (int index)
    {
        const uint32 c[] = { 0xffffd43b, 0xff63e6be, 0xffda77f2, 0xffff922b, 0xff74c0fc, 0xffff8787 };
        return Colour (c[((index % 6) + 6) % 6]);
    }

    inline std::vector<Group> all (te::Edit& e)
    {
        std::vector<Group> out;
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            auto v = list.getChild (i);
            Group g { v[nameID].toString(), (float) v.getProperty (levelID, 0.0), (bool) v.getProperty (mutedID, false), {}, i };
            g.members.addTokens (v[membersID].toString(), ",", {});
            g.members.removeEmptyStrings();
            out.push_back (g);
        }
        return out;
    }

    inline ValueTree node (te::Edit& e, int index) { return e.state.getChildWithName (listID).getChild (index); }
    inline String idOf (te::Track& t) { return t.itemID.toString(); }

    /** The group a track belongs to (-1 = none). A track is in one group at most. */
    inline int groupOf (te::Edit& e, te::Track& t)
    {
        for (auto& g : all (e)) if (g.members.contains (idOf (t))) return g.index;
        return -1;
    }

    inline std::vector<te::AudioTrack*> tracksOf (te::Edit& e, int index)
    {
        std::vector<te::AudioTrack*> out;
        const auto groups = all (e);
        if (index < 0 || index >= (int) groups.size()) return out;
        for (auto t : te::getAudioTracks (e)) if (groups[(size_t) index].members.contains (idOf (*t))) out.push_back (t);
        return out;
    }

    /** A new, empty group. Returns its index. */
    inline int add (te::Edit& e, const String& name = {})
    {
        auto* um = &e.getUndoManager();
        auto list = e.state.getOrCreateChildWithName (listID, um);
        ValueTree v (vcaID);
        v.setProperty (nameID, name.isNotEmpty() ? name : "VCA " + String (list.getNumChildren() + 1), nullptr);
        v.setProperty (levelID, 0.0, nullptr);
        list.appendChild (v, um);
        return list.getNumChildren() - 1;
    }

    /** Takes a track out of whatever group it is in (its fader stays where it is). */
    inline void leave (te::Edit& e, te::Track& t)
    {
        auto list = e.state.getChildWithName (listID);
        for (int i = 0; i < list.getNumChildren(); ++i)
        {
            auto v = list.getChild (i);
            StringArray m;
            m.addTokens (v[membersID].toString(), ",", {});
            if (m.contains (idOf (t))) { m.removeString (idOf (t)); v.setProperty (membersID, m.joinIntoString (","), &e.getUndoManager()); }
        }
    }

    /** Puts a track into a group (and out of any other). */
    inline void join (te::Edit& e, int index, te::Track& t)
    {
        leave (e, t);
        auto v = node (e, index);
        if (! v.isValid()) return;
        StringArray m;
        m.addTokens (v[membersID].toString(), ",", {});
        m.removeEmptyStrings();
        m.add (idOf (t));
        v.setProperty (membersID, m.joinIntoString (","), &e.getUndoManager());
    }

    /** Moves the group's fader to a level (dB): every member's fader moves by the same amount. A member that reaches the
        end of its own fader stays there, and the group's level only moves as far as every member could follow, so the
        balance between the members is never lost. */
    inline void setLevel (te::Edit& e, int index, float db)
    {
        auto v = node (e, index);
        if (! v.isValid()) return;
        const float old = (float) v.getProperty (levelID, 0.0);
        float delta = jlimit (lowest, highest, db) - old;
        for (auto t : tracksOf (e, index))
            if (auto vp = t->getVolumePlugin())
                delta = delta > 0 ? jmin (delta, highest - vp->getVolumeDb()) : jmax (delta, lowest - vp->getVolumeDb());
        if (std::abs (delta) < 1.0e-4f) return;
        for (auto t : tracksOf (e, index))
            if (auto vp = t->getVolumePlugin()) vp->setVolumeDb (vp->getVolumeDb() + delta);
        v.setProperty (levelID, old + delta, &e.getUndoManager());
    }

    /** Mutes or un-mutes every member. */
    inline void setMuted (te::Edit& e, int index, bool muted)
    {
        auto v = node (e, index);
        if (! v.isValid()) return;
        for (auto t : tracksOf (e, index)) t->setMute (muted);
        v.setProperty (mutedID, muted, &e.getUndoManager());
    }

    inline void rename (te::Edit& e, int index, const String& name) { node (e, index).setProperty (nameID, name, &e.getUndoManager()); }
    /** Removes the group; its members keep their faders where they are. */
    inline void remove (te::Edit& e, int index) { e.state.getChildWithName (listID).removeChild (index, &e.getUndoManager()); }

    //==============================================================================
    /** A group's strip in the mixer: its name, its fader, mute, and how many tracks it holds. Right-click for rename and remove. */
    class Strip : public Component,
                  private Timer
    {
    public:
        Strip (EditViewState& e, int groupIndex) : evs (e), index (groupIndex)
        {
            fader.setSliderStyle (Slider::LinearVertical);
            fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            fader.setRange (lowest, highest, 0.1);
            fader.setSkewFactorFromMidPoint (-10.0);
            fader.setDoubleClickReturnValue (true, 0.0);
            fader.setTooltip ("VCA fader: moves the faders of the tracks in this group together");
            fader.onDragStart = [this] { evs.edit.getUndoManager().beginNewTransaction ("VCA fader"); };
            fader.onValueChange = [this] { Vca::setLevel (evs.edit, index, (float) fader.getValue()); };
            addAndMakeVisible (fader);
            muteBtn.setClickingTogglesState (true);
            muteBtn.setColour (TextButton::buttonOnColourId, Theme::orange);
            muteBtn.setTooltip ("Mute every track in this group");
            muteBtn.onClick = [this] { evs.edit.getUndoManager().beginNewTransaction ("VCA mute"); Vca::setMuted (evs.edit, index, muteBtn.getToggleState()); };
            addAndMakeVisible (muteBtn);
            startTimerHz (10);
            timerCallback();
        }

        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (Pro::shade (0.35f));
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f), 7.0f);
            g.setGradientFill (ColourGradient (Pro::cardTop(), 0.0f, r.getY(), Pro::cardBot(), 0.0f, r.getY() + 120.0f, false));
            g.fillRoundedRectangle (r, 7.0f);
            const auto col = colourOf (index);
            g.setColour (col);
            g.fillRoundedRectangle (r.withHeight (24.0f), 7.0f);
            g.setColour (Colour (0xff111317));
            g.setFont (FontOptions (12.5f, Font::bold));
            g.drawText (name, r.withHeight (24.0f).reduced (4.0f, 0.0f), Justification::centred, true);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f));
            g.drawText ("VCA", r.withTrimmedTop (28.0f).withHeight (14.0f), Justification::centred);
            g.drawText (String (count) + (count == 1 ? " track" : " tracks"), r.withTrimmedTop (42.0f).withHeight (14.0f), Justification::centred);
            g.setColour (Theme::text);
            g.setFont (FontOptions (12.0f));
            g.drawText ((level > 0.05f ? "+" : "") + String (level, 1) + " dB", Rectangle<float> (r.getX(), (float) muteBtn.getY() - 22.0f, r.getWidth(), 16.0f), Justification::centred);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            r.removeFromTop (56);
            muteBtn.setBounds (r.removeFromBottom (26).withSizeKeepingCentre (jmin (60, r.getWidth()), 26));
            r.removeFromBottom (24);
            fader.setBounds (r.withSizeKeepingCentre (30, r.getHeight()));
        }

        void mouseDown (const MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            PopupMenu m;
            auto& edit = evs.edit;
            const int i = index;
            m.addItem ("Rename...", [this] { renameDialog(); });
            m.addItem ("Remove this VCA group (the tracks keep their faders)", [&edit, i] { edit.getUndoManager().beginNewTransaction ("Remove VCA"); Vca::remove (edit, i); });
            SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this));
        }

    private:
        void renameDialog()
        {
            auto* w = new AlertWindow ("Rename VCA group", {}, MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", name);
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            w->enterModalState (true, ModalCallbackFunction::create ([this, w, sp = SafePointer<Strip> (this)] (int r)
            {
                const auto n = w->getTextEditorContents ("name").trim();
                if (sp != nullptr && r == 1 && n.isNotEmpty()) { evs.edit.getUndoManager().beginNewTransaction ("Rename VCA"); Vca::rename (evs.edit, index, n); }
            }), true);
        }

        void timerCallback() override
        {
            const auto groups = all (evs.edit);
            if (index >= (int) groups.size()) return;
            auto& g = groups[(size_t) index];
            if (g.name != name || std::abs (g.level - level) > 0.01f || (int) g.members.size() != count || g.muted != muteBtn.getToggleState())
            {
                name = g.name; level = g.level; count = g.members.size();
                if (! fader.isMouseButtonDown()) fader.setValue (level, dontSendNotification);
                muteBtn.setToggleState (g.muted, dontSendNotification);
                repaint();
            }
        }

        EditViewState& evs;
        int index, count = 0;
        String name;
        float level = 0.0f;
        Slider fader;
        TextButton muteBtn { "M" };
    };
}
