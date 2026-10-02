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

    /** Copies every sample the project uses into "<project> Samples" next to it and points the
        project at the copies (so the folder can be zipped and opened anywhere). */
    inline CollectResult collectAll (te::Edit& e)
    {
        CollectResult r;
        r.folder = samplesFolder (e);
        const auto projectDir = editFileOf (e).getParentDirectory();
        std::map<String, File> done;

        auto bring = [&] (const File& f) -> File
        {
            if (f == File() || ! f.existsAsFile())
            {
                if (f.getFullPathName().isNotEmpty()) r.missing.addIfNotAlreadyThere (f.getFullPathName());
                return f;
            }
            if (f.isAChildOf (projectDir)) { ++r.alreadyInProject; return f; }
            auto key = f.getFullPathName().toLowerCase();
            if (auto it = done.find (key); it != done.end()) return it->second;

            r.folder.createDirectory();
            auto dest = r.folder.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false);
            if (! f.copyFileTo (dest)) { r.missing.add (f.getFullPathName()); return f; }
            ++r.copied;
            done[key] = dest;
            return dest;
        };

        for (auto w : allAudioClips (e))
        {
            const auto src = w->getOriginalFile();
            const auto dst = bring (src);
            if (dst != src) w->getSourceFileReference().setToFile (dst, te::SourceFileReference::PathStyle::chooseBest, false);

            auto takes = w->state.getChildWithName (Timeline::pulseTakesID);
            for (auto t : takes)
            {
                File tf (t["file"].toString());
                auto nf = bring (tf);
                if (nf != tf) t.setProperty ("file", nf.getFullPathName(), nullptr);
            }
        }

        for (auto p : allPlugins (e))
        {
            if (auto s = dynamic_cast<pulse::Simpler*> (p))
            {
                File f (s->state[pulse::Simpler::sampleFileID].toString());
                auto nf = bring (f);
                if (nf != f && f != File()) s->setSampleFile (nf);
                for (auto z : s->state)
                    if (z.hasType (pulse::Simpler::zoneType))
                    {
                        File zf (z[pulse::Simpler::zFile].toString());
                        auto nz = bring (zf);
                        if (nz != zf) z.setProperty (pulse::Simpler::zFile, nz.getFullPathName(), nullptr);
                    }
            }
            else if (auto dp = dynamic_cast<pulse::DrumPad*> (p))
            {
                for (auto layer : dp->state)
                    if (layer.hasType (pulse::DrumPad::layerType))
                    {
                        File f (layer[pulse::DrumPad::fileID].toString());
                        auto nf = bring (f);
                        if (nf != f) layer.setProperty (pulse::DrumPad::fileID, nf.getFullPathName(), nullptr);
                    }
            }
            else if (auto sp = dynamic_cast<te::SamplerPlugin*> (p))
            {
                for (int i = 0; i < sp->getNumSounds(); ++i)
                {
                    auto f = sp->getSoundFile (i).getFile();
                    auto nf = bring (f);
                    if (nf != f) sp->setSoundMedia (i, nf.getFullPathName());
                }
                sp->flushPendingUpdates();   // the sampler applies sound changes asynchronously
            }
        }

        te::EditFileOperations (e).save (true, true, false);
        return r;
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
