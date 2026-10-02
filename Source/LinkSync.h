#pragma once

#include "Common.h"

/** Connects the Edit to an Ableton Link session: shared tempo, bar phase and (optionally)
    start/stop. The engine does the phase chasing; this answers its requests. */
class LinkSync : private te::AbletonLink::Listener,
                 private Timer
{
public:
    explicit LinkSync (te::Edit& e) : edit (e), link (e.getAbletonLink())
    {
        link.addListener (this);
        link.setTempoConstraint ({ 40.0, 250.0 });
        lastTempo = currentTempo();
        bpm.store (lastTempo);
        startTimerHz (10);
    }

    ~LinkSync() override
    {
        link.removeListener (this);
        link.setEnabled (false);
    }

    void setEnabled (bool on)
    {
        link.setEnabled (on);
        link.enableStartStopSync (on);
        if (on) link.requestTempoChange (currentTempo());
    }

    bool isEnabled() const       { return link.isEnabled(); }
    size_t getNumPeers() const   { return link.getNumPeers(); }

    String getStatus() const
    {
        if (! link.isEnabled()) return {};
        const auto peers = (int) link.getNumPeers();
        return peers == 0 ? String ("Link: no peers yet") : "Link: " + String (peers) + (peers == 1 ? " peer" : " peers");
    }

    /** Applies a tempo coming from Link without filling the undo history. */
    static void setTempoQuietly (te::Edit& e, double newBpm)
    {
        if (auto t = e.tempoSequence.getTempo (0))
            t->state.setProperty (te::IDs::bpm, jlimit (te::TempoSetting::minBPM, te::TempoSetting::maxBPM, newBpm), nullptr);
    }

private:
    double currentTempo() const { return edit.tempoSequence.getTempo (0)->getBpm(); }

    // Link -> Pulse ------------------------------------------------------------------
    void linkRequestedTempoChange (double newBpm) override
    {
        setTempoQuietly (edit, newBpm);
        lastTempo = currentTempo();
        bpm.store (lastTempo);
    }

    void linkRequestedStartStopChange (bool shouldPlay) override
    {
        auto& tc = edit.getTransport();
        if (shouldPlay && ! tc.isPlaying())      tc.play (false);
        else if (! shouldPlay && tc.isPlaying()) tc.stop (false, false);
    }

    void linkConnectionChanged() override {}

    // [[ audio thread ]] a large jump to get back in phase
    void linkRequestedPositionChange (te::BeatDuration adjustment) override
    {
        if (auto ctx = edit.getTransport().getCurrentPlaybackContext())
        {
            const double seconds = adjustment.inBeats() * 60.0 / jmax (1.0, bpm.load());
            ctx->postPosition (ctx->getAudibleTimelineTime() + te::TimeDuration::fromSeconds (seconds));
        }
    }

    // Pulse -> Link ------------------------------------------------------------------
    void timerCallback() override
    {
        const double now = currentTempo();
        bpm.store (now);
        if (link.isEnabled() && std::abs (now - lastTempo) > 0.001)
            link.requestTempoChange (now);
        lastTempo = now;

        auto& tc = edit.getTransport();
        if (link.isEnabled() && link.isStartStopSyncEnabled() && tc.isPlaying() != wasPlaying)
            link.requestStartStopChange (tc.isPlaying());
        wasPlaying = tc.isPlaying();
    }

    te::Edit& edit;
    te::AbletonLink& link;
    double lastTempo = 120.0;
    std::atomic<double> bpm { 120.0 };
    bool wasPlaying = false;
};
