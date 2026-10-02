#pragma once

#include "Common.h"

/** Clip envelopes: automation for a device parameter that lives inside a MIDI clip, so it plays
    wherever the clip plays (arrangement or session, looping with the clip).
    Stored in the clip as PULSEENVS > ENV (plugin, param) > POINT (b = beat from clip start, v = 0..1). */
namespace ClipEnvelopes
{
    static inline const Identifier envsID { "PULSEENVS" }, envID { "ENV" }, pointID { "POINT" },
                                   pluginAttr { "plugin" }, paramAttr { "param" }, beatAttr { "b" }, valueAttr { "v" };

    struct Target { String pluginID, paramID, name; bool isValid() const { return paramID.isNotEmpty(); } };

    inline ValueTree findEnv (te::Clip& c, const Target& t)
    {
        for (auto e : c.state.getChildWithName (envsID))
            if (e[pluginAttr].toString() == t.pluginID && e[paramAttr].toString() == t.paramID) return e;
        return {};
    }

    inline ValueTree getOrCreateEnv (te::Clip& c, const Target& t, UndoManager* um)
    {
        if (auto e = findEnv (c, t); e.isValid()) return e;
        auto envs = c.state.getOrCreateChildWithName (envsID, um);
        ValueTree e (envID);
        e.setProperty (pluginAttr, t.pluginID, nullptr);
        e.setProperty (paramAttr, t.paramID, nullptr);
        envs.addChild (e, -1, um);
        return e;
    }

    inline std::vector<std::pair<double, float>> points (const ValueTree& env)
    {
        std::vector<std::pair<double, float>> p;
        for (auto pt : env) p.push_back ({ (double) pt[beatAttr], (float) pt[valueAttr] });
        std::sort (p.begin(), p.end());
        return p;
    }

    /** Adds or moves the point at `beat` (within half a grid step). */
    inline void setPoint (te::Clip& c, const Target& t, double beat, float value, double tolerance, UndoManager* um)
    {
        auto env = getOrCreateEnv (c, t, um);
        for (auto pt : env)
            if (std::abs ((double) pt[beatAttr] - beat) < tolerance) { pt.setProperty (valueAttr, value, um); return; }
        ValueTree pt (pointID);
        pt.setProperty (beatAttr, beat, nullptr);
        pt.setProperty (valueAttr, jlimit (0.0f, 1.0f, value), nullptr);
        env.addChild (pt, -1, um);
    }

    inline void removeNear (te::Clip& c, const Target& t, double beat, double tolerance, UndoManager* um)
    {
        auto env = findEnv (c, t);
        for (int i = env.getNumChildren(); --i >= 0;)
            if (std::abs ((double) env.getChild (i)[beatAttr] - beat) < tolerance) env.removeChild (i, um);
    }

    /** Linear interpolation between points (holds before the first / after the last). */
    inline std::optional<float> valueAt (const ValueTree& env, double beat)
    {
        auto p = points (env);
        if (p.empty()) return {};
        if (beat <= p.front().first) return p.front().second;
        for (size_t i = 1; i < p.size(); ++i)
            if (beat <= p[i].first)
            {
                const double span = jmax (1.0e-9, p[i].first - p[i - 1].first);
                return p[i - 1].second + (float) ((beat - p[i - 1].first) / span) * (p[i].second - p[i - 1].second);
            }
        return p.back().second;
    }

    inline te::AutomatableParameter* resolve (te::AudioTrack& track, const ValueTree& env)
    {
        const auto pid = env[pluginAttr].toString(), param = env[paramAttr].toString();
        for (auto pl : track.pluginList)
            if (pl->itemID.toString() == pid)
                for (auto p : pl->getAutomatableParameters())
                    if (p->paramID == param) return p;
        return nullptr;
    }

    /** Beat inside the clip's content where it's playing now, or nothing if it isn't. */
    inline std::optional<double> playingBeat (te::Clip& c, te::TimePosition now, bool inSlot)
    {
        if (inSlot)
        {
            if (auto lh = c.getLaunchHandle())
                if (lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
                    if (auto played = lh->getPlayedRange())
                    {
                        const double len = c.isLooping() ? c.getLoopLengthBeats().inBeats() : c.getLengthInBeats().inBeats();
                        return std::fmod (played->getLength().inBeats(), jmax (0.25, len));
                    }
            return {};
        }
        if (! c.getEditTimeRange().contains (now)) return {};
        double rel = EngineHelpers::timeToBeat (c.edit, now) - c.getStartBeat().inBeats();
        if (c.isLooping() && c.getLoopLengthBeats().inBeats() > 0.0)
            rel = std::fmod (rel, c.getLoopLengthBeats().inBeats());
        return rel;
    }

    /** Applies clip envelopes while the song plays (about 60 times a second). */
    class Player : private Timer
    {
    public:
        explicit Player (te::Edit& e) : edit (e) { startTimerHz (60); }

        void applyNow()
        {
            auto& tc = edit.getTransport();
            const auto now = tc.getPosition();
            for (auto t : te::getAudioTracks (edit))
            {
                auto apply = [&] (te::Clip& c, bool slot)
                {
                    auto envs = c.state.getChildWithName (envsID);
                    if (! envs.isValid() || envs.getNumChildren() == 0) return;
                    auto beat = playingBeat (c, now, slot);
                    if (! beat) return;
                    for (auto env : envs)
                        if (auto p = resolve (*t, env))
                            if (auto v = valueAt (env, *beat))
                                p->setNormalisedParameter (*v, dontSendNotification);
                };
                if (tc.isPlaying())
                    for (auto c : t->getClips()) apply (*c, false);
                for (auto slot : t->getClipSlotList().getClipSlots())
                    if (auto c = slot->getClip()) apply (*c, true);
            }
        }

    private:
        void timerCallback() override { applyNow(); }
        te::Edit& edit;
    };
}
