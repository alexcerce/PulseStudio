#pragma once

#include "Common.h"
#include "EditingOps.h"

/** What to do when the processor cannot keep up: notice it (a few seconds of overload in a row, not one spike),
    find the track whose devices cost the most, and freeze it: either when asked, or by itself the next time
    playback stops (never in the middle of the music, because freezing has to play the track into a file). */
namespace LoadGuard
{
    enum Mode { ask, automatic, off };
    inline Mode mode (te::Engine& e)              { return (Mode) jlimit (0, 2, e.getPropertyStorage().getPropertiesFile().getIntValue ("overloadMode", ask)); }
    inline void setMode (te::Engine& e, Mode m)   { e.getPropertyStorage().getPropertiesFile().setValue ("overloadMode", (int) m); }

    /** The share of the processor a track's devices are using (0..1). */
    inline double cpuOf (te::AudioTrack& t)
    {
        double cpu = 0.0;
        for (auto p : t.pluginList) cpu += p->getCpuUsage();
        return cpu;
    }

    /** A track that can be frozen now: it has something on it, it is not frozen already, and nobody is about to
        record onto it. */
    inline bool canFreeze (te::AudioTrack& t)
    {
        return ! t.isFrozen (te::Track::anyFreeze) && ! t.getClips().isEmpty() && ! EngineHelpers::isTrackArmed (t);
    }

    struct Heavy { te::AudioTrack* track = nullptr; double cpu = 0.0; };

    /** The track that freezing would help the most. */
    inline Heavy heaviest (te::Edit& e, std::function<double (te::AudioTrack&)> cost = cpuOf)
    {
        Heavy h;
        for (auto t : te::getAudioTracks (e))
            if (canFreeze (*t))
                if (const double c = cost (*t); c > h.cpu) h = { t, c };
        return h;
    }

    /** Freezes the heaviest track. Returns its name (empty when there was nothing worth freezing, or it failed). */
    inline String freezeHeaviest (te::Edit& e, std::function<double (te::AudioTrack&)> cost = cpuOf)
    {
        auto h = heaviest (e, cost);
        if (h.track == nullptr) return {};
        const auto name = h.track->getName();
        return h.track->setFrozen (true, te::Track::individualFreeze).wasOk() ? name : String();
    }

    /** Watches the load: overloaded means over `high` for `after` seconds in a row. A dip that stays above `low` does
        not start the count again; one below it does. */
    class Monitor
    {
    public:
        static constexpr double high = 0.85, low = 0.7, after = 3.0;

        void tick (double load, double seconds)
        {
            if (load >= high)      over += seconds;
            else if (load < low)   over = 0.0;
            if (over >= after && ! wasOver) asked = true;   // once for each time it happens
            wasOver = over >= after;
        }

        bool overloaded() const { return over >= after; }
        /** True once after each overload, until taken: there is something to do about it. */
        bool takeRequest()      { const bool a = asked; asked = false; return a; }
        bool hasRequest() const { return asked; }

    private:
        double over = 0.0;
        bool wasOver = false, asked = false;
    };
}
