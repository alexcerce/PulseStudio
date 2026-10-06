#pragma once

#include "Common.h"
#include "PulseSynths.h"
#include "Timeline.h"

/** Recently opened projects (newest first, no duplicates). */
namespace RecentProjects
{
    inline StringArray push (StringArray list, const File& f, int maxItems = 12)
    {
        list.removeString (f.getFullPathName(), true);
        list.insert (0, f.getFullPathName());
        list.removeEmptyStrings();
        while (list.size() > maxItems) list.remove (list.size() - 1);
        return list;
    }
}

/** Moving projects between machines: Collect All and Save, and project templates. */
namespace Sharing
{
    inline File editFileOf (te::Edit& e) { return te::EditFileOperations (e).getEditFile(); }

    inline File samplesFolder (te::Edit& e)
    {
        auto f = editFileOf (e);
        return f.getParentDirectory().getChildFile (f.getFileNameWithoutExtension() + " Samples");
    }

    /** Every audio clip in the song, including session clip slots. */
    inline Array<te::WaveAudioClip*> allAudioClips (te::Edit& e)
    {
        Array<te::WaveAudioClip*> out;
        for (auto t : te::getAudioTracks (e))
        {
            for (auto c : t->getClips())
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c)) out.add (w);
            for (auto slot : t->getClipSlotList().getClipSlots())
                if (auto w = dynamic_cast<te::WaveAudioClip*> (slot->getClip())) out.add (w);
        }
        return out;
    }

    /** Every plugin, including the ones inside racks (Drum Rack pads). */
    inline Array<te::Plugin*> allPlugins (te::Edit& e)
    {
        Array<te::Plugin*> out;
        for (auto p : te::getAllPlugins (e, false)) out.addIfNotAlreadyThere (p);
        for (auto rt : e.getRackList().getTypes())
            for (auto p : rt->getPlugins()) out.addIfNotAlreadyThere (p);
        return out;
    }

    struct CollectResult
    {
        int copied = 0, alreadyInProject = 0;
        StringArray missing;
        File folder;

        String summary() const
        {
            String s;
            s << copied << " file(s) copied into\n" << folder.getFullPathName() << "\n"
              << alreadyInProject << " file(s) were already in the project folder.";
            if (! missing.isEmpty())
                s << "\n\nMissing (couldn't be copied):\n" << missing.joinIntoString ("\n");
            return s;
        }
    };

    /** Goes through every file the project points at: audio clips and their takes, the samples of Simpler, the
        Drum Rack's pads and the sampler, a Pulse Space's own impulse response, the reference song, the video.
        `visit` is given each file and returns the file to point at from now on (the same one to leave it be). */
    inline void forEachFile (te::Edit& e, const std::function<File (const File&)>& visit)
    {
        for (auto w : allAudioClips (e))
        {
            const auto src = w->getOriginalFile();
            const auto dst = visit (src);
            if (dst != src) w->getSourceFileReference().setToFile (dst, te::SourceFileReference::PathStyle::chooseBest, false);

            auto takes = w->state.getChildWithName (Timeline::pulseTakesID);
            for (auto t : takes)
            {
                File tf (t["file"].toString());
                auto nf = visit (tf);
                if (nf != tf) t.setProperty ("file", nf.getFullPathName(), nullptr);
            }
        }

        for (auto p : allPlugins (e))
        {
            if (auto s = dynamic_cast<pulse::Simpler*> (p))
            {
                File f (s->state[pulse::Simpler::sampleFileID].toString());
                auto nf = visit (f);
                if (nf != f && f != File()) s->setSampleFile (nf);
                for (auto z : s->state)
                    if (z.hasType (pulse::Simpler::zoneType))
                    {
                        File zf (z[pulse::Simpler::zFile].toString());
                        auto nz = visit (zf);
                        if (nz != zf) z.setProperty (pulse::Simpler::zFile, nz.getFullPathName(), nullptr);
                    }
            }
            else if (auto dp = dynamic_cast<pulse::DrumPad*> (p))
            {
                for (auto layer : dp->state)
                    if (layer.hasType (pulse::DrumPad::layerType))
                    {
                        File f (layer[pulse::DrumPad::fileID].toString());
                        auto nf = visit (f);
                        if (nf != f) layer.setProperty (pulse::DrumPad::fileID, nf.getFullPathName(), nullptr);
                    }
            }
            else if (auto sp = dynamic_cast<te::SamplerPlugin*> (p))
            {
                for (int i = 0; i < sp->getNumSounds(); ++i)
                {
                    auto f = sp->getSoundFile (i).getFile();
                    auto nf = visit (f);
                    if (nf != f) sp->setSoundMedia (i, nf.getFullPathName());
                }
                sp->flushPendingUpdates();   // the sampler applies sound changes asynchronously
            }
            else if (auto space = dynamic_cast<pulse::ConvolutionReverb*> (p))
            {
                const auto f = space->getFile();
                if (f != File()) if (auto nf = visit (f); nf != f) space->state.setProperty ("irFile", nf.getFullPathName(), nullptr);
            }
            else if (auto song = dynamic_cast<pulse::Reference*> (p))
            {
                const auto f = song->getFile();
                if (f != File()) if (auto nf = visit (f); nf != f) song->state.setProperty ("referenceFile", nf.getFullPathName(), nullptr);
            }
        }

        if (auto video = e.state.getChildWithName ("PULSEVIDEO"); video.isValid() && video["file"].toString().isNotEmpty())
        {
            File f (video["file"].toString());
            if (auto nf = visit (f); nf != f) video.setProperty ("file", nf.getFullPathName(), nullptr);
        }
    }

    //==============================================================================
    // Missing files
    /** Every file the project points at that is not there (each once). */
    inline StringArray missingFiles (te::Edit& e)
    {
        StringArray missing;
        forEachFile (e, [&] (const File& f)
        {
            if (f != File() && f.getFullPathName().isNotEmpty() && ! f.existsAsFile()) missing.addIfNotAlreadyThere (f.getFullPathName());
            return f;
        });
        return missing;
    }

    struct Relinked
    {
        int found = 0;
        StringArray stillMissing;
        String summary() const
        {
            String s = found == 0 ? String ("None of the missing files is in that folder.") : String (found) + (found == 1 ? " file was found, and the project now uses it." : " files were found, and the project now uses them.");
            if (! stillMissing.isEmpty()) s << "\n\nStill missing:\n" << stillMissing.joinIntoString ("\n");
            return s;
        }
    };

    /** Looks in a folder and everything under it for the missing files, by name, and points the project at the ones
        found. When a name is there more than once, the one whose folders are most like the old path's is taken. */
    inline Relinked relink (te::Edit& e, const File& folder)
    {
        Relinked r;
        const auto missing = missingFiles (e);
        if (missing.isEmpty() || ! folder.isDirectory()) { r.stillMissing = missing; return r; }
        StringArray wantedNames;
        for (auto& m : missing) wantedNames.addIfNotAlreadyThere (File (m).getFileName().toLowerCase());
        std::map<String, Array<File>> byName;
        int looked = 0;
        for (const auto& entry : RangedDirectoryIterator (folder, true, "*", File::findFiles))
        {
            if (++looked > 400000) break;   // (a whole disk was chosen: enough)
            const auto name = entry.getFile().getFileName().toLowerCase();
            if (wantedNames.contains (name)) byName[name].add (entry.getFile());
        }
        auto likeness = [] (const File& a, const File& b)   // how many folders the two paths share, counted from the file upwards
        {
            int n = 0;
            for (auto pa = a.getParentDirectory(), pb = b.getParentDirectory(); pa.getFileName().isNotEmpty() && pa.getFileName().equalsIgnoreCase (pb.getFileName()); pa = pa.getParentDirectory(), pb = pb.getParentDirectory()) ++n;
            return n;
        };
        StringArray done;
        forEachFile (e, [&] (const File& f)
        {
            if (f == File() || f.getFullPathName().isEmpty() || f.existsAsFile()) return f;
            auto it = byName.find (f.getFileName().toLowerCase());
            if (it == byName.end() || it->second.isEmpty()) return f;
            File best = it->second.getFirst();
            for (auto& c : it->second) if (likeness (c, f) > likeness (best, f)) best = c;
            done.addIfNotAlreadyThere (f.getFullPathName());
            return best;
        });
        r.found = done.size();
        r.stillMissing = missingFiles (e);
        return r;
    }

    /** Copies every sample the project uses into "<project> Samples" next to it and points the
        project at the copies (so the folder can be zipped and opened anywhere). */
    /** What collecting will do, worked out without doing any of it: which files are outside the project's folder
        and where each will be copied to. */
    struct CollectPlan
    {
        CollectResult result;
        std::vector<std::pair<File, File>> copies;   // from, to
        std::map<String, File> placed;                // (old path, lower case) -> where it is now
        int64 bytes = 0;
    };

    inline CollectPlan planCollect (te::Edit& e)
    {
        CollectPlan plan;
        plan.result.folder = samplesFolder (e);
        const auto projectDir = editFileOf (e).getParentDirectory();
        StringArray taken;
        forEachFile (e, [&] (const File& f)
        {
            if (f == File() || ! f.existsAsFile())
            {
                if (f.getFullPathName().isNotEmpty()) plan.result.missing.addIfNotAlreadyThere (f.getFullPathName());
                return f;
            }
            if (f.isAChildOf (projectDir)) { ++plan.result.alreadyInProject; return f; }
            const auto key = f.getFullPathName().toLowerCase();
            if (plan.placed.count (key) != 0) return f;
            // a name of its own in the samples folder (two files of the same name from different places both get one)
            auto dest = plan.result.folder.getChildFile (f.getFileName());
            for (int n = 2; dest.existsAsFile() || taken.contains (dest.getFullPathName().toLowerCase()); ++n)
                dest = plan.result.folder.getChildFile (f.getFileNameWithoutExtension() + " " + String (n) + f.getFileExtension());
            taken.add (dest.getFullPathName().toLowerCase());
            plan.placed[key] = dest;
            plan.copies.push_back ({ f, dest });
            plan.bytes += f.getSize();
            return f;
        });
        return plan;
    }

    /** The copying: files only, nothing of the project, so it can run on any thread. `progress` is told how far it is
        (0..1) and stops it by returning false. A file that cannot be copied is noted, and the project keeps the old one. */
    inline void copyPlanned (CollectPlan& plan, const std::function<bool (float)>& progress = {})
    {
        if (! plan.copies.empty()) plan.result.folder.createDirectory();
        int64 doneBytes = 0;
        for (auto& [from, to] : plan.copies)
        {
            if (progress && ! progress (plan.bytes > 0 ? (float) ((double) doneBytes / (double) plan.bytes) : 0.0f)) { plan.placed.erase (from.getFullPathName().toLowerCase()); continue; }
            if (from.copyFileTo (to)) ++plan.result.copied;
            else { plan.result.missing.add (from.getFullPathName()); plan.placed.erase (from.getFullPathName().toLowerCase()); }
            doneBytes += from.getSize();
        }
    }

    /** Points the project at the copies, and saves it. */
    inline CollectResult finishCollect (te::Edit& e, CollectPlan& plan)
    {
        forEachFile (e, [&] (const File& f)
        {
            auto it = plan.placed.find (f.getFullPathName().toLowerCase());
            return it != plan.placed.end() && it->second.existsAsFile() ? it->second : f;
        });
        te::EditFileOperations (e).save (true, true, false);
        return plan.result;
    }

    inline CollectResult collectAll (te::Edit& e)
    {
        auto plan = planCollect (e);
        copyPlanned (plan);
        return finishCollect (e, plan);
    }

    //==============================================================================
    // Templates
    inline File templatesFolder()
    {
        auto d = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio").getChildFile ("Templates");
        d.createDirectory();
        return d;
    }

    inline Array<File> templates()
    {
        auto files = templatesFolder().findChildFiles (File::findFiles, false, "*.tracktionedit");
        files.sort();
        return files;
    }

    /** Saves the project as a template. File references are made absolute so the template
        works wherever new projects are created. */
    inline bool saveTemplate (te::Edit& e, const String& name)
    {
        e.flushState();
        auto copy = e.state.createCopy();

        std::function<void (ValueTree)> absolutise = [&] (ValueTree v)
        {
            if (te::Clip::isClipState (v) && v.hasProperty (te::IDs::source))
            {
                auto f = te::SourceFileReference::findFileFromString (e, v[te::IDs::source].toString());
                if (f.existsAsFile()) v.setProperty (te::IDs::source, f.getFullPathName(), nullptr);
            }
            for (auto c : v) absolutise (c);
        };
        absolutise (copy);
        copy.removeChild (copy.getChildWithName ("PULSEVIEW"), nullptr);

        auto f = templatesFolder().getChildFile (File::createLegalFileName (name) + ".tracktionedit");
        if (auto xml = copy.createXml())
            return xml->writeTo (f);
        return false;
    }

    inline String defaultTemplateName (PropertiesFile& settings) { return settings.getValue ("defaultTemplate"); }
}

//==============================================================================
/** Backup history: timestamped copies of the project in "<project> Backups" (newest 20 kept). */
namespace Backups
{
    constexpr int keep = 20;

    inline File folderFor (const File& editFile)
    {
        return editFile.getParentDirectory().getChildFile (editFile.getFileNameWithoutExtension() + " Backups");
    }

    /** Backups of a project, newest first. */
    inline Array<File> list (const File& editFile)
    {
        auto files = folderFor (editFile).findChildFiles (File::findFiles, false, "*.tracktionedit");
        std::sort (files.begin(), files.end(), [] (const File& a, const File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
        return files;
    }

    /** Writes a backup now and drops the oldest beyond `keep`. */
    inline File write (te::Edit& e)
    {
        const auto editFile = te::EditFileOperations (e).getEditFile();
        if (editFile == File()) return {};
        auto dir = folderFor (editFile);
        dir.createDirectory();
        auto f = dir.getNonexistentChildFile (editFile.getFileNameWithoutExtension() + " " + Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"),
                                              ".tracktionedit", false);
        if (! te::EditFileOperations (e).writeToFile (f, false)) return {};
        auto all = list (editFile);
        for (int i = keep; i < all.size(); ++i) all[i].deleteFile();
        return f;
    }

    /** Copies a backup next to the project as a new project file (the original stays as it is). */
    inline File restoreAsCopy (const File& backup, const File& editFile)
    {
        auto dest = editFile.getParentDirectory().getNonexistentChildFile (editFile.getFileNameWithoutExtension() + " (restored "
                                                                            + backup.getLastModificationTime().formatted ("%H-%M") + ")",
                                                                            ".tracktionedit", false);
        return backup.copyFileTo (dest) ? dest : File();
    }
}
