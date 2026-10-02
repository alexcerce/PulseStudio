#pragma once

#include "Common.h"
#include "ClipImport.h"
#include "AudioToMidi.h"

/** Arrangement workflow helpers: keyboard nudging, clip looping, warp markers,
    MIDI file export and track presets. */
namespace Workflow
{
    //==============================================================================
    // Keyboard editing
    inline double clipStartBeat (te::Clip& c)   { return EngineHelpers::timeToBeat (c.edit, c.getPosition().getStart()); }

    /** Moves the clips by a number of beats (never before the song start). */
    inline void nudgeClips (const te::Clip::Array& clips, double beats)
    {
        if (clips.isEmpty()) return;

        double shift = beats;
        for (auto c : clips)
            shift = jmax (shift, -clipStartBeat (*c));

        for (auto c : clips)
            c->setStart (EngineHelpers::beatToTime (c->edit, clipStartBeat (*c) + shift), false, true);
    }

    /** Moves the clips to the track above (-1) or below (+1). Returns the moved clips. */
    inline te::Clip::Array moveClipsToAdjacentTrack (te::Edit& edit, const te::Clip::Array& clips, int direction)
    {
        auto tracks = EngineHelpers::getUserTracks (edit);
        std::vector<std::pair<te::EditItemID, te::AudioTrack*>> moves;

        for (auto c : clips)
        {
            const int idx = tracks.indexOf (dynamic_cast<te::AudioTrack*> (c->getTrack()));
            if (idx < 0 || ! isPositiveAndBelow (idx + direction, tracks.size()))
                return clips;   // all or nothing
            moves.push_back ({ c->itemID, tracks[idx + direction] });
        }

        te::Clip::Array moved;
        for (auto& [id, target] : moves)
            if (auto c = te::findClipForID (edit, id))
            {
                c->moveTo (*target);
                if (auto again = te::findClipForID (edit, id))
                    moved.add (again);
            }
        return moved;
    }

    //==============================================================================
    // Clip looping
    /** Makes sure an audio clip knows its tempo and length in beats (needed for looping/warping). */
    inline void ensureLoopInfo (te::AudioClipBase& ac)
    {
        auto info = ac.getAudioFile().getInfo();
        auto li = ac.getLoopInfo();
        if (li.getNumerator() <= 0 || li.getDenominator() <= 0)
        {
            li.setNumerator (4);
            li.setDenominator (4);
        }
        if (li.getBpm (info) <= 0.0 || li.getNumBeats() <= 0.0)
            li.setBpm (ac.edit.tempoSequence.getTempo (0)->getBpm(), info);
        ac.setLoopInfo (li);
    }

    inline bool canLoop (te::Clip& c)
    {
        return dynamic_cast<te::MidiClip*> (&c) != nullptr || dynamic_cast<te::WaveAudioClip*> (&c) != nullptr;
    }

    /** Turns looping on (the current content becomes the loop) or off. */
    inline void setLooping (te::Clip& c, bool shouldLoop)
    {
        if (shouldLoop == c.isLooping())
            return;

        if (! shouldLoop)
        {
            c.disableLooping();
            return;
        }

        if (auto ac = dynamic_cast<te::AudioClipBase*> (&c))
            ensureLoopInfo (*ac);

        c.setNumberOfLoops (1);
    }

    /** Sets the loop length in beats (turns looping on first). */
    inline void setLoopLengthBeats (te::Clip& c, double beats)
    {
        setLooping (c, true);
        auto r = c.getLoopRangeBeats();
        if (c.beatBasedLooping())
            c.setLoopRangeBeats ({ r.getStart(), te::BeatDuration::fromBeats (beats) });
        else
        {
            const auto start = c.getLoopRange().getStart();
            const auto len = EngineHelpers::beatToTime (c.edit, clipStartBeat (c) + beats) - c.getPosition().getStart();
            c.setLoopRange ({ start, len });
        }
    }

    /** Sets the clip length (looped clips repeat their loop to fill it). */
    inline void setLengthBeats (te::Clip& c, double beats)
    {
        c.setEnd (EngineHelpers::beatToTime (c.edit, clipStartBeat (c) + beats), true);
    }

    inline double loopLengthBeats (te::Clip& c)
    {
        if (! c.isLooping()) return 0.0;
        if (c.beatBasedLooping()) return c.getLoopLengthBeats().inBeats();
        const auto start = c.getPosition().getStart();
        return EngineHelpers::timeToBeat (c.edit, start + c.getLoopLength()) - EngineHelpers::timeToBeat (c.edit, start);
    }

    //==============================================================================
    // MIDI clip content → edit beats (handles offset and looping)
    struct MidiPlacement
    {
        double clipStart = 0, clipLength = 0, offset = 0;
        bool looped = false;
        double loopStart = 0, loopLength = 0;

        explicit MidiPlacement (te::MidiClip& mc)
        {
            clipStart = mc.getStartBeat().inBeats();
            clipLength = mc.getLengthInBeats().inBeats();
            offset = mc.getOffsetInBeats().inBeats();
            looped = mc.isLooping();
            loopStart = mc.getLoopStartBeats().inBeats();
            loopLength = mc.getLoopLengthBeats().inBeats();
        }

        /** Calls fn (editStartBeat, lengthBeats) for every place a content range plays. */
        template <typename Fn>
        void forEach (double contentBeat, double length, Fn&& fn) const
        {
            const double clipEnd = clipStart + clipLength;

            if (! looped || loopLength <= 0.0)
            {
                const double s = clipStart + contentBeat - offset;
                const double e = jmin (s + length, clipEnd);
                if (s >= clipStart - 1.0e-9 && s < clipEnd)
                    fn (s, e - s);
                return;
            }

            if (contentBeat < loopStart - 1.0e-9 || contentBeat >= loopStart + loopLength)
                return;

            const double rel = contentBeat - loopStart;
            const double len = jmin (length, loopLength - rel);
            // first repetition at or after the clip start
            double p = rel - offset;
            while (p < -1.0e-9) p += loopLength;

            for (; p < clipLength; p += loopLength)
            {
                const double s = clipStart + p;
                fn (s, jmin (s + len, clipEnd) - s);
            }
        }
    };

    //==============================================================================
    // MIDI file export
    inline void addClipToSequence (te::MidiClip& mc, MidiMessageSequence& seq, double ticksPerBeat, int channel)
    {
        MidiPlacement place (mc);
        auto& list = mc.getSequence();

        for (auto n : list.getNotes())
            place.forEach (n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), [&] (double s, double len)
            {
                const auto vel = (uint8) jlimit (1, 127, n->getVelocity());
                seq.addEvent (MidiMessage::noteOn (channel, n->getNoteNumber(), vel), s * ticksPerBeat);
                seq.addEvent (MidiMessage::noteOff (channel, n->getNoteNumber()), (s + jmax (0.01, len)) * ticksPerBeat);
            });

        for (auto cc : list.getControllerEvents())
            place.forEach (cc->getBeatPosition().inBeats(), 0.0, [&] (double s, double)
            {
                const int type = cc->getType(), value = cc->getControllerValue();
                if (type == te::MidiControllerEvent::pitchWheelType)
                    seq.addEvent (MidiMessage::pitchWheel (channel, jlimit (0, 16383, value)), s * ticksPerBeat);
                else if (isPositiveAndBelow (type, 128))
                    seq.addEvent (MidiMessage::controllerEvent (channel, type, jlimit (0, 127, value >> 7)), s * ticksPerBeat);
            });
    }

    /** Writes the given MIDI clips (grouped by track) to a type-1 MIDI file with the song's tempo map. */
    inline bool exportMidi (te::Edit& edit, const Array<te::MidiClip*>& clips, const File& file)
    {
        if (clips.isEmpty()) return false;

        constexpr int ppq = 960;
        MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);

        // tempo track
        {
            MidiMessageSequence tempo;
            auto& ts = edit.tempoSequence;
            for (int i = 0; i < ts.getNumTempos(); ++i)
            {
                auto t = ts.getTempo (i);
                tempo.addEvent (MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / t->getBpm())), t->getStartBeat().inBeats() * ppq);
            }
            auto& sig = ts.getTimeSigAt (te::TimePosition());
            tempo.addEvent (MidiMessage::timeSignatureMetaEvent (sig.numerator.get(), sig.denominator.get()), 0.0);
            auto name = MidiMessage::textMetaEvent (3, edit.getName());
            tempo.addEvent (name, 0.0);
            mf.addTrack (tempo);
        }

        // one MIDI track per Pulse track, in arrangement order
        for (auto t : te::getAudioTracks (edit))
        {
            MidiMessageSequence seq;
            for (auto mc : clips)
                if (mc->getTrack() == t)
                    addClipToSequence (*mc, seq, ppq, 1);

            if (seq.getNumEvents() == 0)
                continue;

            seq.addEvent (MidiMessage::textMetaEvent (3, t->getName()), 0.0);
            seq.sort();
            seq.updateMatchedPairs();
            mf.addTrack (seq);
        }

        file.deleteFile();
        FileOutputStream out (file);
        return out.openedOk() && mf.writeTo (out, 1);
    }

    /** All MIDI clips in the song (excluding clip slots). */
    inline Array<te::MidiClip*> allMidiClips (te::Edit& edit)
    {
        Array<te::MidiClip*> out;
        for (auto t : EngineHelpers::getUserTracks (edit))
            for (auto c : t->getClips())
                if (auto mc = dynamic_cast<te::MidiClip*> (c))
                    out.add (mc);
        return out;
    }

    //==============================================================================
    // Track presets: instrument + effects + mixer settings, saved as *.pulsetrack
    inline File trackPresetFolder()
    {
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio")
                                                                          .getChildFile ("Presets").getChildFile ("Tracks");
        d.createDirectory();
        return d;
    }

    inline Array<File> trackPresets()
    {
        auto files = trackPresetFolder().findChildFiles (File::findFiles, false, "*.pulsetrack");
        files.sort();
        return files;
    }

    inline void stripForPreset (ValueTree& v)
    {
        for (int i = v.getNumChildren(); --i >= 0;)
        {
            auto child = v.getChild (i);
            if (te::Clip::isClipState (child) || child.hasType (te::IDs::AUTOMATIONCURVE)
                || child.hasType (te::IDs::CLIPSLOTS) || child.hasType (te::IDs::INPUTDEVICES))
                v.removeChild (i, nullptr);
            else
                stripForPreset (child);
        }
    }

    inline ValueTree createTrackPreset (te::AudioTrack& t, const String& name)
    {
        ValueTree root ("PULSETRACK");
        root.setProperty ("name", name, nullptr);

        ValueTree racks ("RACKS");
        for (auto p : t.pluginList)
        {
            p->flushPluginStateToValueTree();
            if (auto ri = dynamic_cast<te::RackInstance*> (p))
                if (ri->type != nullptr)
                {
                    for (auto rp : ri->type->getPlugins())
                        rp->flushPluginStateToValueTree();
                    racks.appendChild (ri->type->state.createCopy(), nullptr);
                }
        }

        auto ts = t.state.createCopy();
        ts.removeProperty (te::IDs::frozen, nullptr);
        stripForPreset (ts);
        root.appendChild (ts, nullptr);
        root.appendChild (racks, nullptr);
        return root;
    }

    inline bool isTrackPreset (const String& description)  { return description.startsWith ("plugin:track:"); }
    inline File trackPresetFile (const String& description) { return File (description.fromFirstOccurrenceOf ("plugin:track:", false, false)); }

    inline bool saveTrackPreset (te::AudioTrack& t, const String& name)
    {
        auto f = trackPresetFolder().getChildFile (File::createLegalFileName (name) + ".pulsetrack");
        if (auto xml = createTrackPreset (t, name).createXml())
            return xml->writeTo (f);
        return false;
    }

    /** Creates a new track from a preset tree (placed after the last content track). */
    inline te::AudioTrack* insertTrackPreset (te::Edit& edit, ValueTree root)
    {
        auto trackState = root.getChildWithName (te::IDs::TRACK);
        if (! trackState.isValid())
            return nullptr;

        // remap the whole preset at once so rack references stay consistent
        root = root.createCopy();
        te::EditItemID::remapIDs (root, nullptr, edit);

        for (auto rack : root.getChildWithName ("RACKS"))
            edit.getRackList().addRackTypeFrom (rack.createCopy());

        trackState = root.getChildWithName (te::IDs::TRACK).createCopy();
        auto content = EngineHelpers::getUserTracks (edit);
        auto t = edit.insertTrack (te::TrackInsertPoint (nullptr, content.getLast()), trackState, nullptr);
        auto at = dynamic_cast<te::AudioTrack*> (t.get());
        if (at != nullptr && root.hasProperty ("name"))
            at->setName (root["name"].toString());
        return at;
    }

    inline te::AudioTrack* loadTrackPreset (te::Edit& edit, const File& f)
    {
        if (auto xml = parseXML (f))
            return insertTrackPreset (edit, ValueTree::fromXml (*xml));
        return nullptr;
    }

    //==============================================================================
    // Warp markers (audio clips, used with "Warp to tempo")
    /** Source seconds per clip beat for a warped clip. */
    inline double sourceSecondsPerBeat (te::AudioClipBase& ac)
    {
        auto bpm = ac.getLoopInfo().getBpm (ac.getAudioFile().getInfo());
        return 60.0 / (bpm > 0.0 ? bpm : ac.edit.tempoSequence.getTempo (0)->getBpm());
    }

    /** Edit beat at which a warp time (seconds in the warped source) plays. */
    inline double warpTimeToEditBeat (te::AudioClipBase& ac, te::TimePosition warpTime)
    {
        return ac.getStartBeat().inBeats() + warpTime.inSeconds() / sourceSecondsPerBeat (ac) - ac.getOffsetInBeats().inBeats();
    }

    inline te::TimePosition editBeatToWarpTime (te::AudioClipBase& ac, double beat)
    {
        return te::TimePosition::fromSeconds (jmax (0.0, (beat - ac.getStartBeat().inBeats() + ac.getOffsetInBeats().inBeats()) * sourceSecondsPerBeat (ac)));
    }

    /** Turns warp markers on: the clip follows the project tempo and markers pin audio to beats. */
    inline void enableWarpMarkers (te::AudioClipBase& ac, bool on)
    {
        if (on)
        {
            ensureLoopInfo (ac);
            if (! ac.getAutoTempo())
                ac.setAutoTempo (true);
        }
        ac.setWarpTime (on);
    }

    inline void addWarpMarkerAtBeat (te::AudioClipBase& ac, double beat)
    {
        auto& wm = ac.getWarpTimeManager();
        auto w = editBeatToWarpTime (ac, beat);
        for (auto m : wm.getMarkers())
            if (std::abs ((m->warpTime - w).inSeconds()) < 0.01)
                return;
        wm.insertMarker (te::WarpMarker (wm.warpTimeToSourceTime (w), w));
    }

    /** Adds a marker at every detected transient (keeps the start/end markers). */
    /** Onsets in the clip's source file (seconds), from Pulse's own detector (cached per file). */
    inline Array<te::TimePosition> sourceTransients (te::AudioClipBase& ac)
    {
        static std::map<String, Array<te::TimePosition>> cache;
        const auto f = ac.getOriginalFile();
        const auto key = f.getFullPathName() + ":" + String (f.getLastModificationTime().toMilliseconds());
        if (auto it = cache.find (key); it != cache.end()) return it->second;

        Array<te::TimePosition> out;
        std::unique_ptr<AudioFormatReader> r (ac.edit.engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (r != nullptr && r->lengthInSamples > 0)
        {
            const int len = (int) jmin<int64> (r->lengthInSamples, (int64) (r->sampleRate * 600.0));
            AudioBuffer<float> b ((int) jmin (2u, r->numChannels), len);
            r->read (&b, 0, len, 0, true, b.getNumChannels() > 1);
            if (b.getNumChannels() > 1) { b.addFrom (0, 0, b, 1, 0, len); b.applyGain (0, 0, len, 0.5f); }
            for (auto& n : AudioToMidi::detectDrums (b.getReadPointer (0), len, r->sampleRate))
                out.add (te::TimePosition::fromSeconds (n.start));
        }
        cache[key] = out;
        return out;
    }

    inline int addWarpMarkersAtTransients (te::AudioClipBase& ac)
    {
        auto& wm = ac.getWarpTimeManager();
        auto [ready, engineTimes] = wm.getTransientTimes();
        auto times = engineTimes;
        if (times.isEmpty())   // the engine's detector found nothing (or hasn't run): use Pulse's
            times = sourceTransients (ac);
        if (times.isEmpty() && ! ready)
            return -1;

        int added = 0;
        for (auto t : times)
        {
            bool exists = false;
            for (auto m : wm.getMarkers())
                exists = exists || std::abs ((m->sourceTime - t).inSeconds()) < 0.03;
            if (! exists)
            {
                wm.insertMarker (te::WarpMarker (t, wm.sourceTimeToWarpTime (t)));
                ++added;
            }
        }
        return added;
    }

    /** Audio quantize: warp markers at the transients are pulled towards the grid
        (strength 1 = exactly on the grid). Returns the number of markers moved, or -1 while the
        transient analysis is still running. */
    inline int quantizeAudio (te::AudioClipBase& ac, double gridBeats, double strength)
    {
        if (gridBeats <= 0) return 0;
        if (! ac.getWarpTime()) enableWarpMarkers (ac, true);
        if (addWarpMarkersAtTransients (ac) < 0) return -1;
        auto& wm = ac.getWarpTimeManager();
        int moved = 0;
        const auto markers = wm.getMarkers();
        for (int i = 1; i < markers.size() - 1; ++i)   // keep the start and end markers
        {
            auto* m = wm.getMarkers()[i];
            const double beat = warpTimeToEditBeat (ac, m->warpTime);
            const double grid = std::round (beat / gridBeats) * gridBeats;
            const double target = beat + (grid - beat) * jlimit (0.0, 1.0, strength);
            if (std::abs (target - beat) < 1.0e-4) continue;
            wm.moveMarker (i, editBeatToWarpTime (ac, target));
            ++moved;
        }
        return moved;
    }
}

namespace Workflow
{
    /** Asks for a .mid destination, then exports the clips (or says why it couldn't). */
    inline void chooseAndExportMidi (te::Edit& edit, Array<te::MidiClip*> clips, const String& defaultName)
    {
        if (clips.isEmpty())
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export MIDI", "There are no MIDI clips to export.");
            return;
        }

        // hold the clips by ID so the chooser can outlive them safely
        Array<te::EditItemID> ids;
        for (auto c : clips) ids.add (c->itemID);

        auto start = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio")
                                                                              .getChildFile (File::createLegalFileName (defaultName) + ".mid");
        auto chooser = std::make_shared<FileChooser> ("Export MIDI file", start, "*.mid");
        auto* engine = &edit.engine;
        auto* editPtr = &edit;
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [chooser, ids, engine, editPtr] (const FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == File() || ! engine->getActiveEdits().getEdits().contains (editPtr))
                return;   // cancelled, or the project was closed meanwhile
            auto& ed = *editPtr;

            Array<te::MidiClip*> found;
            for (auto id : ids)
                if (auto mc = dynamic_cast<te::MidiClip*> (te::findClipForID (ed, id)))
                    found.add (mc);

            const bool ok = exportMidi (ed, found, f.withFileExtension (".mid"));
            AlertWindow::showMessageBoxAsync (ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export MIDI",
                                              ok ? "Exported " + String (found.size()) + " clip(s) to\n" + f.withFileExtension (".mid").getFullPathName()
                                                 : String ("Couldn't write the MIDI file."));
        });
    }
}

//==============================================================================
/** Groove / swing for MIDI clips (uses the engine's groove templates). */
namespace Groove
{
    inline StringArray names (te::Engine& e)   { return e.getGrooveTemplateManager().getTemplateNames(); }

    inline void set (te::MidiClip& mc, const String& name, float strength)
    {
        mc.setGrooveTemplate (name);
        mc.setGrooveStrength (strength);
    }

    /** Moves the notes to where the groove plays them, then clears the clip's groove. */
    inline bool commit (te::MidiClip& mc)
    {
        auto* gt = mc.edit.engine.getGrooveTemplateManager().getTemplateByName (mc.getGrooveTemplate());
        if (gt == nullptr || gt->isEmpty())
            return false;

        auto* um = &mc.edit.getUndoManager();
        const float strength = mc.getGrooveStrength();
        for (auto n : mc.getSequence().getNotes())
        {
            auto grooved = gt->beatsTimeToGroovyTime (n->getStartBeat(), strength);
            n->setStartAndLength (grooved, n->getLengthBeats(), um);
        }
        mc.setGrooveTemplate ({});
        return true;
    }

    /** Timing of hits in edit beats (relative to the clip start) for groove extraction. */
    inline std::vector<double> hitBeats (te::Clip& c)
    {
        std::vector<double> beats;
        if (auto mc = dynamic_cast<te::MidiClip*> (&c))
        {
            for (auto n : mc->getSequence().getNotes()) beats.push_back (n->getStartBeat().inBeats() - mc->getOffsetInBeats().inBeats());
        }
        else if (auto ac = dynamic_cast<te::AudioClipBase*> (&c))
        {
            const double startBeat = EngineHelpers::timeToBeat (c.edit, c.getPosition().getStart());
            const double offset = c.getPosition().getOffset().inSeconds();
            for (auto t : Workflow::sourceTransients (*ac))
            {
                const double editTime = c.getPosition().getStart().inSeconds() + (t.inSeconds() - offset) / jmax (0.01, ac->getSpeedRatio());
                if (editTime >= c.getPosition().getStart().inSeconds() && editTime < c.getPosition().getEnd().inSeconds())
                    beats.push_back (EngineHelpers::timeToBeat (c.edit, te::TimePosition::fromSeconds (editTime)) - startBeat);
            }
        }
        std::sort (beats.begin(), beats.end());
        return beats;
    }

    /** Builds a groove from a clip's timing: for each 16th of a bar, how late (or early) the hits
        land on average. Returns the latenesses (fractions of a 16th; empty if too few hits). */
    inline std::vector<float> latenessesOf (te::Clip& c)
    {
        const auto beats = hitBeats (c);
        std::vector<double> sum (16, 0.0);
        std::vector<int> count (16, 0);
        for (auto b : beats)
        {
            const double sixteenths = b * 4.0;
            const double nearest = std::round (sixteenths);
            const int slot = ((int) nearest % 16 + 16) % 16;
            sum[(size_t) slot] += sixteenths - nearest;
            ++count[(size_t) slot];
        }
        int used = 0;
        std::vector<float> late (16, 0.0f);
        for (int i = 0; i < 16; ++i)
            if (count[(size_t) i] > 0) { late[(size_t) i] = (float) (sum[(size_t) i] / count[(size_t) i]); ++used; }
        if (used < 2) return {};
        return late;
    }

    /** Adds the clip's groove to the groove list (shared with every project). Returns its name. */
    inline String extractGroove (te::Clip& c)
    {
        auto late = latenessesOf (c);
        if (late.empty()) return {};
        te::GrooveTemplate gt;
        gt.setName ("From " + c.getName());
        gt.setNotesPerBeat (4);
        gt.setNumberOfNotes (16);
        for (int i = 0; i < 16; ++i) gt.setLatenessProportion (i, late[(size_t) i], 1.0f);
        auto& gm = c.edit.engine.getGrooveTemplateManager();
        const auto before = gm.getTemplateNames();
        gm.updateTemplate (gm.getNumTemplates(), gt);
        for (auto& n : gm.getTemplateNames()) if (! before.contains (n)) return n;
        return gt.getName();
    }

    inline void addToMenu (PopupMenu& m, te::MidiClip& mc)
    {
        te::Clip::Ptr holder (&mc);
        auto withClip = [holder] (auto fn) { return [holder, fn] { if (auto c = dynamic_cast<te::MidiClip*> (holder.get())) fn (*c); }; };
        const auto current = mc.getGrooveTemplate();
        const float strength = mc.getGrooveStrength();

        PopupMenu g;
        g.addItem ("None", true, current.isEmpty(), withClip ([] (te::MidiClip& c)
        {
            c.edit.getUndoManager().beginNewTransaction ("Groove");
            c.setGrooveTemplate ({});
        }));
        g.addSeparator();
        for (auto& name : names (mc.edit.engine))
            g.addItem (name, true, name == current, withClip ([name, strength] (te::MidiClip& c)
            {
                c.edit.getUndoManager().beginNewTransaction ("Groove");
                set (c, name, strength > 0.01f ? strength : 0.5f);
            }));

        g.addSeparator();
        PopupMenu amounts;
        for (int pct : { 10, 25, 50, 75, 100 })
            amounts.addItem (String (pct) + "%", current.isNotEmpty(), std::abs (strength * 100.0f - (float) pct) < 0.5f,
                             withClip ([pct] (te::MidiClip& c)
            {
                c.edit.getUndoManager().beginNewTransaction ("Groove amount");
                c.setGrooveStrength ((float) pct / 100.0f);
            }));
        g.addSubMenu ("Amount", amounts, current.isNotEmpty());
        g.addItem ("Commit groove to notes", current.isNotEmpty(), false, withClip ([] (te::MidiClip& c)
        {
            c.edit.getUndoManager().beginNewTransaction ("Commit groove");
            commit (c);
        }));

        m.addSubMenu ("Groove" + (current.isNotEmpty() ? " (" + current + ", " + String (roundToInt (strength * 100)) + "%)" : String()), g);
    }
}
