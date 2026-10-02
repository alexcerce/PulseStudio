#pragma once

#include "Common.h"

class MidiLearn;

/** Shared UI state for one Edit: zoom/scroll, snapping, selection and cross-view requests. */
class EditViewState
{
public:
    EditViewState (te::Edit& e, te::SelectionManager& sm) : edit (e), selectionManager (sm) {}

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
    te::AutomatableParameter* getAutomationParam (te::AudioTrack& t)
    {
        auto& av = automation[t.itemID];
        if (av.param == nullptr || av.param->getPlugin() == nullptr || av.param->getPlugin()->getOwnerTrack() != &t)
            if (auto vp = t.getVolumePlugin())
                av.param = vp->volParam;
        return av.param.get();
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

    int laneHeight (te::Track& t) const  { return trackHeight + takeRows (t) * takeRowHeight; }
    int heightFor (te::Track& t) const   { return laneHeight (t) + (isAutomationVisible (t) ? automationHeight : 0); }

    /** Track rows changed height (automation shown/hidden or parameter changed). */
    ChangeBroadcaster layoutChanged;

    /** Groups (folder tracks) whose children are hidden in the arrangement. */
    std::set<te::EditItemID> collapsed;

    /** Push 2 session bank (drawn as a box in the session view while Push is connected). */
    bool pushConnected = false;
    int pushTrackOffset = 0, pushSceneOffset = 0;

    /** Set by the main component: opens a MIDI clip in the piano roll. */
    std::function<void (te::MidiClip*)> openPianoRoll;
    /** Set by the main component: shows the device chain tab. */
    std::function<void()> showDevices;
};
