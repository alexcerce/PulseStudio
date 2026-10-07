#pragma once

#include "Common.h"

/** MIDI Learn: map any controller (CC) to any automatable parameter.
    Mappings are stored inside the Edit, so they travel with the project.

    Pages: a controller has only so many knobs. A mapping belongs either to "Always" (it works whatever page is
    chosen: this is what every mapping is until pages are used) or to one of eight pages, of which one is chosen at
    a time. The same eight knobs can then play the filter on page 1, the sends on page 2, the drum rack on page 3.
    The page is chosen in the mappings window, from the command palette, or from two buttons of the controller
    itself that have been learned as "next page" and "previous page". */
class MidiLearn : public ChangeBroadcaster,
                  private te::PhysicalMidiInputDevice::Listener,
                  private AsyncUpdater
{
public:
    static constexpr int numPages = 8;   // pages 1..8; page 0 is "Always"
    enum class Target { parameter, nextPage, previousPage, trackMute };

    explicit MidiLearn (te::Edit& e) : edit (e)
    {
        load();
        attachToInputs();
    }

    ~MidiLearn() override
    {
        cancelPendingUpdate();
        for (auto& d : attached)
            if (auto p = d.lock())
                if (auto phys = dynamic_cast<te::PhysicalMidiInputDevice*> (p.get()))
                    phys->removeListener (this);
    }

    /** Push 2 is handled by its own controller code, so it's skipped here. */
    static bool isPushPort (const String& name) { return name.containsIgnoreCase ("Push 2"); }

    void attachToInputs()
    {
        for (auto& d : edit.engine.getDeviceManager().getMidiInDevices())
            if (auto phys = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()))
                if (! isPushPort (phys->getName()))
                {
                    bool already = false;
                    for (auto& a : attached) if (a.lock() == d) already = true;
                    if (! already)
                    {
                        phys->addListener (this);
                        attached.push_back (d);
                    }
                }
    }

    //==============================================================================
    /** Waits for a controller to be moved, and maps it to this parameter on the page chosen now. */
    void startLearning (te::AutomatableParameter& p)
    {
        learning = &p;
        learnTarget = Target::parameter;
        sendChangeMessage();
    }

    /** Waits for a button of the controller, and makes it step to the next (or the previous) page. */
    void startLearningPageButton (bool next)
    {
        learning = nullptr;
        learnTarget = next ? Target::nextPage : Target::previousPage;
        sendChangeMessage();
    }

    /** Waits for a button of the controller, and makes it mute and unmute this track (on the page chosen now). */
    void startLearningMute (te::Track& t)
    {
        learning = nullptr;
        learnTrack = t.itemID;
        learnTarget = Target::trackMute;
        sendChangeMessage();
    }

    bool isMuteMapped (te::Track& t) const
    {
        for (auto& m : mappings) if (m.param == nullptr && m.muteTrack == t.itemID && active (m)) return true;
        return false;
    }
    String muteMappingText (te::Track& t) const
    {
        StringArray all;
        for (auto& m : mappings)
            if (m.param == nullptr && m.muteTrack == t.itemID)
                all.add ("CC " + String (m.cc) + " (ch " + String (m.channel) + ")" + (m.page > 0 ? " on " + pageName (m.page) : String()));
        return all.joinIntoString (", ");
    }
    void removeMuteMapping (te::Track& t)
    {
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (auto& m) { return m.param == nullptr && m.muteTrack == t.itemID && active (m); }), mappings.end());
        save();
        sendChangeMessage();
    }

    void cancelLearning()                  { learning = nullptr; learnTarget = Target::parameter; sendChangeMessage(); }
    bool isLearning() const                { return learning != nullptr || learnTarget != Target::parameter; }
    String getLearningName() const
    {
        if (learnTarget == Target::nextPage)     return "the \"next page\" button";
        if (learnTarget == Target::previousPage) return "the \"previous page\" button";
        if (learnTarget == Target::trackMute)
        {
            auto t = te::findTrackForID (edit, learnTrack);
            return "the mute of " + (t != nullptr ? t->getName() : String ("a track")) + (page > 0 ? "  (" + pageName (page) + ")" : String());
        }
        return learning != nullptr ? describe (*learning) + (page > 0 ? "  (" + pageName (page) + ")" : String()) : String();
    }

    /** True when the parameter answers to a controller with the page as it is now. */
    bool isMapped (te::AutomatableParameter& p) const
    {
        for (auto& m : mappings) if (m.param.get() == &p && active (m)) return true;
        return false;
    }

    String mappingText (te::AutomatableParameter& p) const
    {
        StringArray all;
        for (auto& m : mappings)
            if (m.param.get() == &p)
                all.add ("CC " + String (m.cc) + " (ch " + String (m.channel) + ")" + (m.page > 0 ? " on " + pageName (m.page) : String()));
        return all.joinIntoString (", ");
    }

    /** Takes away what maps this parameter with the page as it is now (its mappings on other pages stay). */
    void removeMapping (te::AutomatableParameter& p)
    {
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (auto& m) { return m.param.get() == &p && active (m); }), mappings.end());
        save();
        sendChangeMessage();
    }

    //==============================================================================
    // Pages
    int getPage() const { return page; }

    void setPage (int p)
    {
        p = jlimit (0, numPages, p);
        if (p == page) return;
        page = p;
        pageChangedAt = Time::getMillisecondCounter();
        save();
        sendChangeMessage();
    }

    String pageName (int p) const
    {
        if (p <= 0) return "Always";
        return pageNames[p].isNotEmpty() ? pageNames[p] : "Page " + String (p);
    }

    void setPageName (int p, const String& name)
    {
        if (p < 1 || p > numPages) return;
        pageNames.set (p, name.trim() == "Page " + String (p) ? String() : name.trim());
        save();
        sendChangeMessage();
    }

    int numMappingsOn (int p) const
    {
        int n = 0;
        for (auto& m : mappings) if (m.page == p) ++n;
        return n;
    }

    /** Steps to the next page that has something mapped on it (round and round; nothing happens when none has). */
    void stepPage (bool forwards)
    {
        const int base = page == 0 ? (forwards ? 0 : 1) : page;   // from "Always": forwards starts at page 1, backwards at the last
        for (int i = 1; i <= numPages; ++i)
        {
            const int p = ((base - 1 + (forwards ? i : -i)) % numPages + numPages) % numPages + 1;
            if (numMappingsOn (p) > 0) { setPage (p); return; }
        }
    }

    /** True for a moment after the page has changed (the window says which page it is now). */
    bool pageJustChanged() const { return pageChangedAt != 0 && Time::getMillisecondCounter() - pageChangedAt < 400; }

    /** The buttons that step the page, as text ("CC 14 (ch 1)", or empty). */
    String pageButtonText (bool next) const
    {
        auto& b = next ? nextButton : prevButton;
        return b.cc >= 0 ? "CC " + String (b.cc) + " (ch " + String (b.channel) + ")" : String();
    }
    void clearPageButton (bool next) { (next ? nextButton : prevButton) = {}; save(); sendChangeMessage(); }

    //==============================================================================
    /** One mapping, for the mappings window. */
    struct Row { int index = 0, channel = 1, cc = 0, page = 0; String target; };

    std::vector<Row> rows (int onPage) const
    {
        std::vector<Row> out;
        for (int i = 0; i < (int) mappings.size(); ++i)
        {
            auto& m = mappings[(size_t) i];
            if (m.page != onPage) continue;
            if (m.param != nullptr) out.push_back ({ i, m.channel, m.cc, m.page, describe (*m.param) });
            else if (auto t = te::findTrackForID (edit, m.muteTrack)) out.push_back ({ i, m.channel, m.cc, m.page, t->getName() + " / Mute (a button)" });
        }
        return out;
    }

    void removeAt (int index)
    {
        if (! isPositiveAndBelow (index, (int) mappings.size())) return;
        mappings.erase (mappings.begin() + index);
        save();
        sendChangeMessage();
    }

    /** Moves a mapping to another page (a mapping of the same controller already there gives way). */
    void moveToPage (int index, int toPage)
    {
        if (! isPositiveAndBelow (index, (int) mappings.size())) return;
        auto moved = mappings[(size_t) index];
        moved.page = jlimit (0, numPages, toPage);
        mappings.erase (mappings.begin() + index);
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (auto& m) { return m.cc == moved.cc && m.channel == moved.channel && m.page == moved.page; }), mappings.end());
        mappings.push_back (moved);
        save();
        sendChangeMessage();
    }

    //==============================================================================
    /** Feeds a controller message synchronously (used by the self-test). */
    void processControllerForTest (int channel, int cc, int value)
    {
        handleIncomingMidiMessage (MidiMessage::controllerEvent (channel, cc, value));
        handleUpdateNowIfNeeded();
    }

    /** Adds the standard "MIDI Learn" items to a parameter's context menu. */
    void addToMenu (PopupMenu& m, te::AutomatableParameter& p)
    {
        m.addItem (page > 0 ? "MIDI Learn (on " + pageName (page) + ")..." : String ("MIDI Learn..."), [this, ptr = te::AutomatableParameter::Ptr (&p)] { startLearning (*ptr); });
        if (isMapped (p))
            m.addItem ("Remove MIDI mapping (" + mappingText (p) + ")", [this, ptr = te::AutomatableParameter::Ptr (&p)] { removeMapping (*ptr); });
        if (onShowMappings) m.addItem ("Controller mappings and pages...", [this] { if (onShowMappings) onShowMappings(); });
    }

    /** Opens the mappings window (set by the main window). */
    std::function<void()> onShowMappings;

    static String describe (te::AutomatableParameter& p)
    {
        auto name = p.getParameterName();
        if (auto pl = p.getPlugin())
        {
            name = pl->getName() + ": " + name;
            if (auto t = pl->getOwnerTrack()) name = t->getName() + " / " + name;
        }
        return name;
    }

private:
    struct Mapping
    {
        int channel = 1, cc = 0;
        te::AutomatableParameter::Ptr param;
        int page = 0;   // 0 = always
        te::EditItemID muteTrack;   // with no parameter: a button that mutes and unmutes this track
        int last = 0;
    };

    struct PageButton { int channel = 1, cc = -1, last = 0; };
    struct Event { int channel, cc, value; };

    bool active (const Mapping& m) const { return m.page == 0 || m.page == page; }

    // MIDI thread: just queue the CC
    void handleIncomingMidiMessage (const MidiMessage& m) override
    {
        if (! m.isController())
            return;

        {
            const SpinLock::ScopedLockType sl (queueLock);
            queue.push_back ({ m.getChannel(), m.getControllerNumber(), m.getControllerValue() });
        }
        triggerAsyncUpdate();
    }

    // Message thread: learn or apply
    void handleAsyncUpdate() override
    {
        std::vector<Event> events;
        {
            const SpinLock::ScopedLockType sl (queueLock);
            events.swap (queue);
        }

        for (auto& ev : events)
        {
            if (learnTarget == Target::trackMute)
            {
                // on a page, a controller does one thing and a track's mute has one button
                mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                                [&] (auto& m) { return m.page == page && ((m.param == nullptr && m.muteTrack == learnTrack) || (m.cc == ev.cc && m.channel == ev.channel)); }),
                                mappings.end());
                Mapping m;
                m.channel = ev.channel; m.cc = ev.cc; m.page = page; m.muteTrack = learnTrack; m.last = ev.value;
                mappings.push_back (m);
                learnTarget = Target::parameter;
                save();
                sendChangeMessage();
                continue;
            }

            if (learnTarget != Target::parameter)
            {
                // a button for stepping the page: it must not be a knob of some mapping as well
                auto& b = learnTarget == Target::nextPage ? nextButton : prevButton;
                b = { ev.channel, ev.cc, ev.value };
                mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&] (auto& m) { return m.cc == ev.cc && m.channel == ev.channel; }), mappings.end());
                learnTarget = Target::parameter;
                save();
                sendChangeMessage();
                continue;
            }

            if (learning != nullptr)
            {
                // on a page, a controller does one thing and a parameter has one controller
                mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                                [&] (auto& m) { return m.page == page && (m.param == learning || (m.cc == ev.cc && m.channel == ev.channel)); }),
                                mappings.end());
                mappings.push_back ({ ev.channel, ev.cc, learning, page });
                learning = nullptr;
                save();
                sendChangeMessage();
                continue;
            }

            // the page buttons act when pressed (the value goes from the lower half to the upper)
            bool wasButton = false;
            for (auto* b : { &nextButton, &prevButton })
                if (b->cc == ev.cc && b->channel == ev.channel)
                {
                    if (ev.value >= 64 && b->last < 64) stepPage (b == &nextButton);
                    b->last = ev.value;
                    wasButton = true;
                }
            if (wasButton) continue;

            for (auto& m : mappings)
            {
                if (m.cc != ev.cc || m.channel != ev.channel || ! active (m)) continue;
                if (m.param != nullptr)
                    m.param->setNormalisedParameter ((float) ev.value / 127.0f, sendNotificationSync);
                else
                {
                    // a mute acts when its button is pressed (the value goes from the lower half to the upper)
                    if (ev.value >= 64 && m.last < 64)
                        if (auto t = te::findTrackForID (edit, m.muteTrack)) EngineHelpers::toggleMute (*t);
                    m.last = ev.value;
                }
            }
        }
    }

    //==============================================================================
    // Persistence: <PULSEMIDIMAP page next.. prev.. name1..><MAP channel cc plugin param page/></PULSEMIDIMAP>
    void save()
    {
        auto parent = edit.state.getOrCreateChildWithName ("PULSEMIDIMAP", nullptr);
        parent.removeAllChildren (nullptr);
        parent.removeAllProperties (nullptr);
        if (page != 0) parent.setProperty ("page", page, nullptr);
        if (nextButton.cc >= 0) { parent.setProperty ("nextCc", nextButton.cc, nullptr); parent.setProperty ("nextChannel", nextButton.channel, nullptr); }
        if (prevButton.cc >= 0) { parent.setProperty ("prevCc", prevButton.cc, nullptr); parent.setProperty ("prevChannel", prevButton.channel, nullptr); }
        for (int p = 1; p <= numPages; ++p)
            if (pageNames[p].isNotEmpty()) parent.setProperty (Identifier ("name" + String (p)), pageNames[p], nullptr);
        for (auto& m : mappings)
        {
            if (m.param == nullptr)
            {
                if (! m.muteTrack.isValid()) continue;
                ValueTree v ("MAP");
                v.setProperty ("channel", m.channel, nullptr);
                v.setProperty ("cc", m.cc, nullptr);
                v.setProperty ("muteTrack", m.muteTrack.toString(), nullptr);
                if (m.page != 0) v.setProperty ("page", m.page, nullptr);
                parent.appendChild (v, nullptr);
                continue;
            }
            if (auto pl = m.param->getPlugin())
            {
                ValueTree v ("MAP");
                v.setProperty ("channel", m.channel, nullptr);
                v.setProperty ("cc", m.cc, nullptr);
                v.setProperty ("plugin", pl->itemID.toString(), nullptr);
                v.setProperty ("param", m.param->paramID, nullptr);
                if (m.page != 0) v.setProperty ("page", m.page, nullptr);
                parent.appendChild (v, nullptr);
            }
        }
    }

    void load()
    {
        mappings.clear();
        pageNames.clear();
        for (int p = 0; p <= numPages; ++p) pageNames.add ({});
        auto parent = edit.state.getChildWithName ("PULSEMIDIMAP");
        page = jlimit (0, numPages, (int) parent.getProperty ("page", 0));
        nextButton = { (int) parent.getProperty ("nextChannel", 1), (int) parent.getProperty ("nextCc", -1), 0 };
        prevButton = { (int) parent.getProperty ("prevChannel", 1), (int) parent.getProperty ("prevCc", -1), 0 };
        for (int p = 1; p <= numPages; ++p) pageNames.set (p, parent.getProperty (Identifier ("name" + String (p))).toString());
        for (auto v : parent)
            if (v.hasProperty ("muteTrack"))
            {
                Mapping m;
                m.channel = v["channel"]; m.cc = v["cc"]; m.page = jlimit (0, numPages, (int) v.getProperty ("page", 0));
                m.muteTrack = te::EditItemID::fromVar (v["muteTrack"]);
                if (te::findTrackForID (edit, m.muteTrack) != nullptr) mappings.push_back (m);
            }
            else if (auto pl = te::findPluginForID (edit, te::EditItemID::fromVar (v["plugin"])))
                if (auto p = pl->getAutomatableParameterByID (v["param"].toString()))
                    mappings.push_back ({ (int) v["channel"], (int) v["cc"], p, jlimit (0, numPages, (int) v.getProperty ("page", 0)) });
    }

    te::Edit& edit;
    std::vector<Mapping> mappings;
    te::AutomatableParameter::Ptr learning;
    te::EditItemID learnTrack;
    Target learnTarget = Target::parameter;
    int page = 0;
    uint32 pageChangedAt = 0;
    StringArray pageNames;
    PageButton nextButton, prevButton;
    std::vector<std::weak_ptr<te::MidiInputDevice>> attached;
    SpinLock queueLock;
    std::vector<Event> queue;
};
