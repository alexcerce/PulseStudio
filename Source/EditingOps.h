#pragma once

#include "Common.h"
#include "Routing.h"

/** Arrangement editing commands: clipboard, split, duplicate, time-range edits,
    bounce / flatten / freeze and track grouping. */
namespace EditingOps
{
    inline te::Clip::Array selectedClips (te::SelectionManager& sm)
    {
        te::Clip::Array clips;
        for (auto c : sm.getItemsOfType<te::Clip>())
            clips.add (c);
        return clips;
    }

    inline te::TimeRange rangeOf (const te::Clip::Array& clips)
    {
        if (clips.isEmpty()) return {};
        auto r = clips.getFirst()->getEditTimeRange();
        for (auto c : clips) r = r.getUnionWith (c->getEditTimeRange());
        return r;
    }

    //==============================================================================
    // Clipboard
    inline bool copy (te::SelectionManager& sm)
    {
        auto clips = selectedClips (sm);
        if (clips.isEmpty()) return false;

        auto content = std::make_unique<te::Clipboard::Clips>();
        content->addSelectedClips (sm.getSelectedObjects(), rangeOf (clips), te::Clipboard::Clips::AutomationLocked::no);
        te::Clipboard::getInstance()->setContent (std::move (content));
        return true;
    }

    inline void deleteSelected (te::SelectionManager& sm)
    {
        auto clips = selectedClips (sm);
        if (clips.isEmpty()) return;
        sm.deselectAll();
        for (auto c : clips) c->removeFromParent();
    }

    inline void cut (te::SelectionManager& sm)
    {
        if (copy (sm))
            deleteSelected (sm);
    }

    /** Pastes at the playhead, starting on the selected track. */
    inline bool paste (te::Edit& edit, te::SelectionManager& sm)
    {
        auto* clips = te::Clipboard::getInstance()->getContentWithType<te::Clipboard::Clips>();
        if (clips == nullptr) return false;

        te::Track* target = EngineHelpers::getSelectedTrack (sm);
        if (target == nullptr) target = EngineHelpers::getUserTracks (edit).getFirst();
        if (target == nullptr) return false;

        te::EditInsertPoint insertPoint (edit);
        insertPoint.setNextInsertPoint (edit.getTransport().getPosition(), target);
        te::Clipboard::ContentType::EditPastingOptions opts (edit, insertPoint, &sm);
        opts.startTrack = target;
        opts.startTime = edit.getTransport().getPosition();
        return clips->pasteIntoEdit (opts);
    }

    /** Duplicates the selection right after itself. */
    inline void duplicate (te::SelectionManager& sm)
    {
        auto clips = selectedClips (sm);
        if (clips.isEmpty()) return;

        const auto shift = rangeOf (clips).getLength();
        te::SelectableList newOnes;
        for (auto c : clips)
            if (auto copy = te::duplicateClip (*c))
            {
                copy->setStart (c->getPosition().getStart() + shift, false, true);
                newOnes.add (copy.get());
            }
        sm.select (newOnes);
    }

    /** Splits the selected clips (or every clip on the selected track) at the playhead. */
    inline void splitAtPlayhead (te::Edit& edit, te::SelectionManager& sm)
    {
        const auto t = edit.getTransport().getPosition();
        te::SelectableList toSplit;

        for (auto c : selectedClips (sm))
            if (c->getEditTimeRange().contains (t))
                toSplit.add (c);

        if (toSplit.isEmpty())
            if (auto track = EngineHelpers::getSelectedTrack (sm))
                for (auto c : track->getClips())
                    if (c->getEditTimeRange().contains (t))
                        toSplit.add (c);

        if (! toSplit.isEmpty())
            te::splitClips (toSplit, t);
    }

    inline void selectAllClips (te::Edit& edit, te::SelectionManager& sm)
    {
        te::SelectableList all;
        for (auto t : EngineHelpers::getUserTracks (edit))
            for (auto c : t->getClips())
                all.add (c);
        sm.select (all);
    }

    //==============================================================================
    // Time-range edits on the loop range
    inline te::TimeRange loopRange (te::Edit& e) { return e.getTransport().getLoopRange(); }

    inline void deleteTime (te::Edit& e, te::SelectionManager& sm)
    {
        if (! loopRange (e).isEmpty())
            te::deleteRegionOfTracks (e, loopRange (e), false, te::CloseGap::yes, &sm);
    }

    inline void insertTime (te::Edit& e)
    {
        if (! loopRange (e).isEmpty())
            te::insertSpaceIntoEdit (e, loopRange (e));
    }

    /** Copies everything inside the loop range and pastes it right after the range. */
    inline void duplicateLoopRegion (te::Edit& e)
    {
        auto range = loopRange (e);
        if (range.isEmpty()) return;

        for (auto t : EngineHelpers::getUserTracks (e))
            for (auto c : t->getClips())
                if (range.contains (c->getEditTimeRange()))
                    if (auto copy = te::duplicateClip (*c))
                        copy->setStart (c->getPosition().getStart() + range.getLength(), false, true);

        e.getTransport().setLoopRange ({ range.getEnd(), range.getEnd() + range.getLength() });
    }

    //==============================================================================
    // Rendering helpers
    /** "Bounces" next to the project file (falls back to Documents/Pulse Studio/Bounces). */
    inline File bounceFolder (te::Edit& e)
    {
        auto editFile = te::EditFileOperations (e).getEditFile();
        auto base = editFile.getFullPathName().isNotEmpty() && File::isAbsolutePath (editFile.getFullPathName())
                      ? editFile.getParentDirectory()
                      : File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio");
        auto d = base.getChildFile ("Bounces");
        d.createDirectory();
        return d;
    }

    /** Renders one track (with its devices, without master FX) over the given range. */
    inline File renderTrack (te::AudioTrack& track, te::TimeRange range, const String& suffix)
    {
        auto& e = track.edit;
        te::Renderer::Parameters p (e);
        p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { &track });
        p.destFile = bounceFolder (e).getNonexistentChildFile (File::createLegalFileName (track.getName() + " " + suffix), ".wav", false);
        p.audioFormat = e.engine.getAudioFileFormatManager().getWavFormat();
        p.bitDepth = 24;
        p.sampleRateForAudio = 48000.0;
        p.time = range;
        p.endAllowance = te::TimeDuration::fromSeconds (0.0);
        p.usePlugins = true;
        p.useMasterPlugins = false;
        p.checkNodesForAudio = false;
        return te::Renderer::renderToFile ("Bouncing " + track.getName(), p);
    }

    inline te::TimeRange songRange (te::Edit& e)
    {
        return { te::TimePosition(), te::TimePosition() + jmax (e.getLength(), te::TimeDuration::fromSeconds (1.0)) };
    }

    /** Renders the track to audio on a new track below it (original untouched). */
    inline te::AudioTrack* bounceToNewTrack (te::AudioTrack& track, std::optional<te::TimeRange> range = {})
    {
        auto& e = track.edit;
        auto r = range ? *range : songRange (e);
        auto f = renderTrack (track, r, "bounce");
        if (! f.existsAsFile()) return nullptr;

        auto t = e.insertNewAudioTrack (te::TrackInsertPoint (track, false), nullptr);
        if (t == nullptr) return nullptr;
        t->setName (track.getName() + " (bounce)");
        t->setColour (track.getColour());
        te::AudioFile af (e.engine, f);
        t->insertWaveClip (track.getName(), f, { { r.getStart(), te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
        return t.get();
    }

    /** Ableton-style flatten: replaces the track's clips and devices with one rendered audio clip. */
    inline bool flatten (te::AudioTrack& track)
    {
        auto& e = track.edit;
        auto r = songRange (e);
        auto f = renderTrack (track, r, "flatten");
        if (! f.existsAsFile()) return false;

        (void) track.setFrozen (false, te::Track::individualFreeze);

        for (auto c : track.getClips())
            c->removeFromParent();

        for (auto p : track.pluginList.getPlugins())
            if (p != track.getVolumePlugin() && p != track.getLevelMeterPlugin()
                && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr)
                p->deleteFromParent();

        te::AudioFile af (e.engine, f);
        track.insertWaveClip (track.getName(), f, { { r.getStart(), te::TimeDuration::fromSeconds (af.getLength()) }, {} }, false);
        return true;
    }

    inline void toggleFreeze (te::AudioTrack& track)
    {
        const bool frozen = track.isFrozen (te::Track::individualFreeze);
        auto result = track.setFrozen (! frozen, te::Track::individualFreeze);
        if (result.failed())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Freeze", result.getErrorMessage());
    }

    //==============================================================================
    // Grouping (submix folder tracks)
    inline te::FolderTrack* groupTracks (te::Edit& e, juce::Array<te::AudioTrack*> tracks, const String& name)
    {
        if (tracks.isEmpty()) return nullptr;

        auto all = te::getAllTracks (e);
        std::sort (tracks.begin(), tracks.end(), [&all] (auto* a, auto* b) { return all.indexOf (a) < all.indexOf (b); });

        auto first = tracks.getFirst();
        auto folder = e.insertNewFolderTrack (te::TrackInsertPoint (*first, true), nullptr, true);
        if (folder == nullptr) return nullptr;
        folder->setName (name);
        folder->setColour (first->getColour());

        te::Track* preceding = nullptr;
        for (auto t : tracks)
        {
            e.moveTrack (t, te::TrackInsertPoint (folder.get(), preceding));
            preceding = t;
        }
        return folder.get();
    }

    inline void ungroup (te::FolderTrack& folder)
    {
        auto& e = folder.edit;
        te::Track* preceding = &folder;
        for (auto t : folder.getAllSubTracks (false))
        {
            e.moveTrack (t, te::TrackInsertPoint (folder.getParentFolderTrack(), preceding));
            preceding = t;
        }
        e.deleteTrack (&folder);
    }
}
