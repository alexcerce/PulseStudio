#pragma once

#include "PulseSynths.h"
#include "EditingOps.h"

/** Mastering: a ready chain for the master (EQ, gentle multiband, a touch of exciter, a true-peak limiter), and an
    assistant that measures the song and sets the limiter so that it comes out at a chosen loudness. */
namespace Mastering
{
    /** Where a device goes on the master: before its meter (and before the loudness readout). */
    inline int insertIndex (te::PluginList& list)
    {
        int index = list.size();
        if (auto meter = list.findFirstPluginOfType<te::LevelMeterPlugin>()) index = jmin (index, list.indexOf (meter));
        if (auto probe = list.findFirstPluginOfType<pulse::LoudnessProbe>()) index = jmin (index, list.indexOf (probe));
        if (auto ref = list.findFirstPluginOfType<pulse::Reference>()) index = jmin (index, list.indexOf (ref));   // the reference song is not mastered
        return index;
    }

    /** The reference-song device of the master (made, when asked, after everything that shapes the mix and before
        the meters, so that they show whichever of the two is heard). */
    inline pulse::Reference* reference (te::Edit& e, bool create)
    {
        auto& list = e.getMasterPluginList();
        if (auto r = list.findFirstPluginOfType<pulse::Reference>()) return r;
        if (! create) return nullptr;
        auto p = e.getPluginCache().createNewPlugin (pulse::Reference::xmlTypeName, {});
        if (p == nullptr) return nullptr;
        list.insertPlugin (p, insertIndex (list), nullptr);
        return dynamic_cast<pulse::Reference*> (p.get());
    }

    /** Switches between the mix and the reference song. Returns true when the reference is now heard. */
    inline bool toggleReference (te::Edit& e)
    {
        auto* r = reference (e, true);
        if (r == nullptr) return false;
        const bool now = r->getInt (pulse::Reference::pListen) == 0;
        r->set (pulse::Reference::pListen, now ? 1.0f : 0.0f);
        return now;
    }

    /** Puts the mastering chain on the master, in order, leaving alone any of its devices that are already there.
        Returns its limiter. The settings are gentle: nothing is meant to be heard until you turn something up. */
    inline pulse::Limiter* addChain (te::Edit& e)
    {
        auto& list = e.getMasterPluginList();
        auto ensure = [&] (const char* type, std::function<void (pulse::PulsePlugin&)> setup) -> te::Plugin*
        {
            for (auto p : list) if (p->getPluginType() == type) return p;
            auto p = e.getPluginCache().createNewPlugin (type, {});
            if (p == nullptr) return nullptr;
            if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get()); pp != nullptr && setup) setup (*pp);
            list.insertPlugin (p, insertIndex (list), nullptr);
            return p.get();
        };
        using EQ = pulse::PrismEQ;
        using MB = pulse::MultibandDynamics;
        using EX = pulse::Exciter;
        using LM = pulse::Limiter;
        ensure (EQ::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            // only a low cut under what speakers can play; the other bands are there, flat, ready to be dragged
            p.set (EQ::idx (0, EQ::pOn), 1.0f); p.set (EQ::idx (0, EQ::pType), (float) EQ::lowCut); p.set (EQ::idx (0, EQ::pFreq), 25.0f);
        });
        ensure (MB::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            auto& m = dynamic_cast<MB&> (p);
            m.set (m.g (MB::gBands), 1.0f);   // three bands
            for (int b = 0; b < 3; ++b) { m.set (MB::idx (b, MB::bThreshold), -18.0f); m.set (MB::idx (b, MB::bRatio), 1.6f); m.set (MB::idx (b, MB::bAttack), 30.0f); m.set (MB::idx (b, MB::bRelease), 200.0f); }
        });
        ensure (EX::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (EX::pFreq, 7000.0f); p.set (EX::pDrive, 8.0f); p.set (EX::pAmount, 0.06f); });
        auto* lim = ensure (LM::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (LM::pCeiling, -1.0f); p.set (LM::pTruePeak, 1.0f); p.set (LM::pGain, 0.0f); });
        return dynamic_cast<pulse::Limiter*> (lim);
    }

    /** The whole song as it comes out of the master, measured (it is rendered to a temporary file for this). */
    inline pulse::LoudnessAnalyser::Result measure (te::Edit& e)
    {
        auto f = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("pulse-master-measure", ".wav", false);
        pulse::LoudnessAnalyser::Result r;
        e.getTransport().ensureContextAllocated();   // (a project that has never been played has nothing to render through yet)
        EditingOps::waitForClipProxies (e);
        EditingOps::renderDetached (e, f);
        if (f.existsAsFile()) r = pulse::LoudnessAnalyser::measureFile (e.engine, f);
        f.deleteFile();
        return r;
    }

    /** The limiter gain that would bring a song measured at `nowLufs` (with the gain at `gainNow`) to `target`. */
    inline float gainFor (double nowLufs, double target, float gainNow, float mostGain = 24.0f)
    {
        return jlimit (0.0f, mostGain, gainNow + (float) (target - nowLufs));
    }

    struct Outcome
    {
        bool ok = false;
        double before = -200.0, after = -200.0, target = -14.0;
        float peakBefore = -200.0f, peakAfter = -200.0f, limiterGain = 0.0f;
        String text() const
        {
            if (! ok) return "The song could not be measured. It needs something in it, and the project has to be saved once (the measurement plays the song into a file).";
            String s = "Before: " + String (before, 1) + " LUFS, true peak " + String (peakBefore, 1) + " dBTP\nAfter:  " + String (after, 1) + " LUFS, true peak " + String (peakAfter, 1) + " dBTP\n\n"
                       "The limiter's gain is now " + String (limiterGain, 1) + " dB";
            if (after < target - 1.0) s << ", which is as far as it goes: the mix itself is quiet. Bring the tracks up, or accept a quieter master.";
            else if (before > target + 0.5 && limiterGain < 0.05f) s << ". The song was already louder than the target: turn the master fader down by " + String (before - target, 1) + " dB if it must match exactly.";
            else s << ".";
            return s;
        }
    };

    /** The assistant: makes sure the chain is there, measures the song, and sets the limiter's gain so that it comes out
        at `targetLufs` (-14 is what streaming services play at). It measures up to three times. */
    inline Outcome assist (te::Edit& e, double targetLufs = -14.0)
    {
        Outcome o;
        o.target = targetLufs;
        auto* lim = addChain (e);
        if (lim == nullptr) return o;
        using LM = pulse::Limiter;
        lim->set (LM::pGain, 0.0f);
        auto m = measure (e);
        if (m.integrated < -100.0) return o;
        o.before = m.integrated; o.peakBefore = m.truePeakDb;
        float gain = gainFor (m.integrated, targetLufs, 0.0f);
        for (int pass = 0; pass < 2 && gain > 0.05f; ++pass)
        {
            lim->set (LM::pGain, gain);
            m = measure (e);
            if (m.integrated < -100.0) break;
            // the limiter holds the peaks down, so a second look usually asks for a little more (the last look is only a look)
            if (std::abs (targetLufs - m.integrated) < 0.3 || gain >= 23.9f || pass == 1) break;
            gain = gainFor (m.integrated, targetLufs, gain);
        }
        lim->set (LM::pGain, gain);
        o.after = m.integrated; o.peakAfter = m.truePeakDb; o.limiterGain = gain;
        o.ok = true;
        return o;
    }
}
