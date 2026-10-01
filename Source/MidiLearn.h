#pragma once

#include "Common.h"

/** MIDI Learn: map any controller (CC) to any automatable parameter.
    Mappings are stored inside the Edit, so they travel with the project. */
class MidiLearn : public ChangeBroadcaster,
                  private te::PhysicalMidiInputDevice::Listener,
                  private AsyncUpdater
{
public:
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
    void startLearning (te::AutomatableParameter& p)
    {
        learning = &p;
        sendChangeMessage();
    }

    void cancelLearning()                  { learning = nullptr; sendChangeMessage(); }
    bool isLearning() const                { return learning != nullptr; }
    String getLearningName() const         { return learning != nullptr ? describe (*learning) : String(); }

    bool isMapped (te::AutomatableParameter& p) const
    {
        for (auto& m : mappings) if (m.param.get() == &p) return true;
        return false;
    }

    String mappingText (te::AutomatableParameter& p) const
    {
        for (auto& m : mappings)
            if (m.param.get() == &p)
                return "CC " + String (m.cc) + " (ch " + String (m.channel) + ")";
        return {};
    }

    void removeMapping (te::AutomatableParameter& p)
    {
        mappings.erase (std::remove_if (mappings.begin(), mappings.end(), [&p] (auto& m) { return m.param.get() == &p; }), mappings.end());
        save();
        sendChangeMessage();
    }

    /** Feeds a controller message synchronously (used by the self-test). */
    void processControllerForTest (int channel, int cc, int value)
    {
        handleIncomingMidiMessage (MidiMessage::controllerEvent (channel, cc, value));
        handleUpdateNowIfNeeded();
    }

    /** Adds the standard "MIDI Learn" items to a parameter's context menu. */
    void addToMenu (PopupMenu& m, te::AutomatableParameter& p)
    {
        m.addItem ("MIDI Learn...", [this, ptr = te::AutomatableParameter::Ptr (&p)] { startLearning (*ptr); });
        if (isMapped (p))
            m.addItem ("Remove MIDI mapping (" + mappingText (p) + ")", [this, ptr = te::AutomatableParameter::Ptr (&p)] { removeMapping (*ptr); });
    }

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
    };

    struct Event { int channel, cc, value; };

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
            if (learning != nullptr)
            {
                mappings.erase (std::remove_if (mappings.begin(), mappings.end(),
                                                [&] (auto& m) { return m.param == learning || (m.cc == ev.cc && m.channel == ev.channel); }),
                                mappings.end());
                mappings.push_back ({ ev.channel, ev.cc, learning });
                learning = nullptr;
                save();
                sendChangeMessage();
                continue;
            }

            for (auto& m : mappings)
                if (m.cc == ev.cc && m.channel == ev.channel && m.param != nullptr)
                    m.param->setNormalisedParameter ((float) ev.value / 127.0f, sendNotificationSync);
        }
    }

    //==============================================================================
    // Persistence: <PULSEMIDIMAP><MAP channel cc plugin param/></PULSEMIDIMAP>
    void save()
    {
        auto parent = edit.state.getOrCreateChildWithName ("PULSEMIDIMAP", nullptr);
        parent.removeAllChildren (nullptr);
        for (auto& m : mappings)
            if (auto pl = m.param->getPlugin())
            {
                ValueTree v ("MAP");
                v.setProperty ("channel", m.channel, nullptr);
                v.setProperty ("cc", m.cc, nullptr);
                v.setProperty ("plugin", pl->itemID.toString(), nullptr);
                v.setProperty ("param", m.param->paramID, nullptr);
                parent.appendChild (v, nullptr);
            }
    }

    void load()
    {
        mappings.clear();
        auto parent = edit.state.getChildWithName ("PULSEMIDIMAP");
        for (auto v : parent)
            if (auto pl = te::findPluginForID (edit, te::EditItemID::fromVar (v["plugin"])))
                if (auto p = pl->getAutomatableParameterByID (v["param"].toString()))
                    mappings.push_back ({ (int) v["channel"], (int) v["cc"], p });
    }

    te::Edit& edit;
    std::vector<Mapping> mappings;
    te::AutomatableParameter::Ptr learning;
    std::vector<std::weak_ptr<te::MidiInputDevice>> attached;
    SpinLock queueLock;
    std::vector<Event> queue;
};
