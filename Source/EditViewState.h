#pragma once

#include "Common.h"

class MidiLearn;

/** Shared UI state for one Edit: zoom/scroll, snapping, selection and cross-view requests. */
class EditViewState
{
public:
    EditViewState (te::Edit& e, te::SelectionManager& sm) : edit (e), selectionManager (sm) { edit.state.addListener (&counter); }
    ~EditViewState()                                                                         { edit.state.removeListener (&counter); }

    /** A number that is different whenever the project or the view of it has changed. Views that used to
        redraw several times a second "just in case" compare this instead (changedSince) and stay idle. */
    double signature() const
    {
        return counter.count * 1.0 + viewStartBeat * 3.17 + pixelsPerBeat * 7.31 + trackHeight * 0.13 + (edit.getTransport().isPlaying() ? 0.5 : 0.0);
    }
    /** True (once) when the signature plus `extra` (anything else the view shows) differs from `last`. */
    bool changedSince (double& last, double extra = 0.0) const
    {
        const double now = signature() + extra;
        if (now == last) return false;
        last = now;
        return true;
    }

    struct ChangeCounter : public ValueTree::Listener
    {
        int64 count = 0;
        void valueTreePropertyChanged (ValueTree&, const Identifier&) override { ++count; }
        void valueTreeChildAdded (ValueTree&, ValueTree&) override             { ++count; }
        void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override      { ++count; }
        void valueTreeChildOrderChanged (ValueTree&, int, int) override        { ++count; }
        void valueTreeParentChanged (ValueTree&) override                      { ++count; }
    } counter;

    te::Edit& edit;
    te::SelectionManager& selectionManager;
    MidiLearn* midiLearn = nullptr;   // owned by MainComponent

    double pixelsPerBeat = 28.0;
    double viewStartBeat = 0.0;
    double snapBeats = 1.0;           // 0 = off
    int trackHeight = 74;

    float beatToX (double beat) const          { return (float) ((beat - viewStartBeat) * pixelsPerBeat); }
    double xToBeat (float x) const             { return viewStartBeat + (double) x / pixelsPerBeat; }
    float timeToX (te::TimePosition t) const   { return beatToX (EngineHelpers::timeToBeat (edit, t)); }
    te::TimePosition xToTime (float x) const   { return EngineHelpers::beatToTime (edit, jmax (0.0, xToBeat (x))); }

    double snap (double beat) const            { return snapBeats > 0.0 ? std::round (beat / snapBeats) * snapBeats : beat; }
    double snapFloor (double beat) const       { return snapBeats > 0.0 ? std::floor (beat / snapBeats + 1.0e-9) * snapBeats : beat; }

    void zoom (double factor, float anchorX)
    {
        auto b = xToBeat (anchorX);
        pixelsPerBeat = jlimit (2.0, 600.0, pixelsPerBeat * factor);
        viewStartBeat = jmax (0.0, b - (double) anchorX / pixelsPerBeat);
        viewChanged.sendChangeMessage();
    }

    void scrollBy (double beats)
    {
        viewStartBeat = jmax (0.0, viewStartBeat + beats);
        viewChanged.sendChangeMessage();
    }

    /** Notifies the arrangement/ruler to repaint after zoom/scroll. */
    ChangeBroadcaster viewChanged;

    //==============================================================================
    // Automation lanes (one optional lane per track, showing one parameter)
    struct AutomationView
    {
        bool visible = false;
        te::AutomatableParameter::Ptr param;
    };

    static constexpr int automationHeight = 84;
    std::map<te::EditItemID, AutomationView> automation;

    bool isAutomationVisible (te::Track& t) const
    {
        auto it = automation.find (t.itemID);
        return it != automation.end() && it->second.visible;
    }

    void setAutomationVisible (te::Track& t, bool v)
    {
        automation[t.itemID].visible = v;
        layoutChanged.sendChangeMessage();
    }

    /** The parameter shown in a track's lane (defaults to the track's volume). */
    te::AutomatableParameter* getAutomationParam (te::Track& t)
    {
        auto& av = automation[t.itemID];
        if (av.param == nullptr || av.param->getPlugin() == nullptr || ! pluginsOf (t).contains (av.param->getPlugin()))
            if (auto vp = volumeOf (t))
                av.param = vp->volParam;
        return av.param.get();
    }

    /** The devices whose parameters a track's automation lane can show (the master's devices
        and fader for the master track). */
    static juce::Array<te::Plugin*> pluginsOf (te::Track& t)
    {
        juce::Array<te::Plugin*> out;
        if (dynamic_cast<te::MasterTrack*> (&t) != nullptr)
        {
            if (auto vp = t.edit.getMasterVolumePlugin()) out.add (vp.get());
            for (auto p : t.edit.getMasterPluginList()) out.add (p);
        }
        else
            for (auto p : t.pluginList) out.add (p);
        return out;
    }

    /** A track's fader (the master's for the master track). */
    static te::VolumeAndPanPlugin* volumeOf (te::Track& t)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (&t)) return at->getVolumePlugin();
        if (dynamic_cast<te::MasterTrack*> (&t) != nullptr) return t.edit.getMasterVolumePlugin().get();
        return nullptr;
    }

    void setAutomationParam (te::Track& t, te::AutomatableParameter* p)
    {
        automation[t.itemID].param = p;
        layoutChanged.sendChangeMessage();
    }

    /** Tracks showing their take lanes (for comping). */
    std::set<te::EditItemID> takeLanes;
    static constexpr int takeRowHeight = 22;

    int takeRows (te::Track& t) const
    {
        if (takeLanes.count (t.itemID) == 0) return 0;
        int n = 0;
        if (auto ct = dynamic_cast<te::ClipTrack*> (&t))
            for (auto c : ct->getClips())
            {
                if (dynamic_cast<te::WaveAudioClip*> (c) != nullptr) n = jmax (n, c->state.getChildWithName ("PULSETAKES").getNumChildren());
                else if (c->hasAnyTakes())                           n = jmax (n, c->getTakeDescriptions().size());
            }
        return n;
    }

    static inline const Identifier laneHeightID { "pulseLaneHeight" };
    static constexpr int minLaneHeight = 34, maxLaneHeight = 320;
    /** A track's own height (drag the bottom edge of its header), or the default. */
    int baseHeight (te::Track& t) const
    {
        const int h = (int) t.state.getProperty (laneHeightID, 0);
        return h > 0 ? jlimit (minLaneHeight, maxLaneHeight, h) : trackHeight;
    }
    void setBaseHeight (te::Track& t, int h)
    {
        t.state.setProperty (laneHeightID, jlimit (minLaneHeight, maxLaneHeight, h), nullptr);
        heightsChanged.sendChangeMessage();
    }
    int laneHeight (te::Track& t) const  { return baseHeight (t) + takeRows (t) * takeRowHeight; }
    int heightFor (te::Track& t) const   { return laneHeight (t) + (isAutomationVisible (t) ? automationHeight : 0); }

    /** Track rows changed height (automation shown/hidden or parameter changed). */
    ChangeBroadcaster layoutChanged, heightsChanged;

    /** Pencil mode: drag in automation lanes to draw (snapped to the grid). */
    bool drawMode = false;

    /** Groups (folder tracks) whose children are hidden in the arrangement. */
    std::set<te::EditItemID> collapsed;

    /** Push 2 session bank (drawn as a box in the session view while Push is connected). */
    bool pushConnected = false;
    int pushTrackOffset = 0, pushSceneOffset = 0;

    /** Set by the main component: opens a MIDI clip in the piano roll. */
    std::function<void (te::MidiClip*)> openPianoRoll;
    /** Shows a track's devices (selects it and opens the Devices panel; also for return tracks). */
    std::function<void (te::Track*)> showDevices;   // nullptr = the master
    /** Opens a device's editor (big Pulse editor, sandbox window or the plugin's own window). */
    std::function<void (te::Plugin&)> openDeviceEditor;
    /** True when the Devices panel shows this track's chain (nullptr = the master's). */
    std::function<bool (te::Track*)> isShowingDevicesOf;
};
