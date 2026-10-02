#pragma once

#include "Common.h"
#include "Sharing.h"
#include "SampleEdit.h"
#include "CrashGuard.h"

/** Project zips, crash report bundles and the (opt-in) update check. */
namespace ShareTools
{
    //==============================================================================
    /** Collects every sample into the project folder, saves, and zips the project file with its
        samples and edited audio. Returns an error message (empty on success). */
    inline String exportZip (te::Edit& e, const File& zipFile)
    {
        Sharing::collectAll (e);
        te::EditFileOperations (e).save (true, true, false);
        const auto editFile = Sharing::editFileOf (e);
        if (! editFile.existsAsFile()) return "Save the project first.";

        ZipFile::Builder zip;
        const auto base = editFile.getParentDirectory();
        zip.addFile (editFile, 9, editFile.getFileName());
        for (auto dir : { Sharing::samplesFolder (e), SampleEdit::editsFolder (e) })
            if (dir.isDirectory())
                for (auto& f : dir.findChildFiles (File::findFiles, true))
                    zip.addFile (f, 6, f.getRelativePathFrom (base).replaceCharacter ('\\', '/'));
        zipFile.deleteFile();
        FileOutputStream out (zipFile);
        if (! out.openedOk()) return "Couldn't write " + zipFile.getFullPathName();
        double progress = 0;
        if (! zip.writeToStream (out, &progress)) return "Couldn't write the zip.";
        return {};
    }

    /** Unpacks a project zip into a new folder inside `projectsFolder`; returns the project file. */
    inline File importZip (const File& zipFile, const File& projectsFolder)
    {
        ZipFile zip (zipFile);
        if (zip.getNumEntries() == 0) return {};
        auto dest = projectsFolder.getNonexistentChildFile (zipFile.getFileNameWithoutExtension(), {}, false);
        dest.createDirectory();
        if (zip.uncompressTo (dest, false).failed()) return {};
        auto edits = dest.findChildFiles (File::findFiles, false, "*.tracktionedit");
        return edits.isEmpty() ? File() : edits.getFirst();
    }

    //==============================================================================
    /** Zips the crash reports (and the disabled-plugin list) with the app version and system
        details, for sending to whoever looks after Pulse. Returns the zip (or an empty File). */
    inline File bundleCrashReports (const File& destFolder, const String& appVersion)
    {
        auto reports = CrashGuard::crashFolder().findChildFiles (File::findFiles, false, "*.txt");
        auto info = File::createTempFile (".txt");
        info.replaceWithText ("Pulse Studio " + appVersion + "\n" + SystemStats::getOperatingSystemName() + "\n"
                              + SystemStats::getCpuModel() + ", " + String (SystemStats::getNumCpus()) + " cores, "
                              + String (SystemStats::getMemorySizeInMegabytes()) + " MB\n");
        ZipFile::Builder zip;
        zip.addFile (info, 9, "system.txt");
        for (auto& f : reports) zip.addFile (f, 9, "crashes/" + f.getFileName());
        if (CrashGuard::disabledFile().existsAsFile()) zip.addFile (CrashGuard::disabledFile(), 9, "disabled-plugins.txt");
        auto out = destFolder.getNonexistentChildFile ("Pulse Studio crash reports " + Time::getCurrentTime().formatted ("%Y-%m-%d"), ".zip", false);
        FileOutputStream os (out);
        const bool ok = os.openedOk() && zip.writeToStream (os, nullptr);
        info.deleteFile();
        return ok ? out : File();
    }

    inline int numCrashReports() { return CrashGuard::crashFolder().findChildFiles (File::findFiles, false, "*.txt").size(); }

    //==============================================================================
    /** "1.2.10" > "1.2.9". */
    inline bool isNewer (const String& candidate, const String& current)
    {
        auto a = StringArray::fromTokens (candidate.trim(), ".", {}), b = StringArray::fromTokens (current.trim(), ".", {});
        for (int i = 0; i < jmax (a.size(), b.size()); ++i)
        {
            const int x = a[i].getIntValue(), y = b[i].getIntValue();
            if (x != y) return x > y;
        }
        return false;
    }

    struct UpdateInfo { String version, url, notes; };

    /** Reads {"version": "...", "url": "...", "notes": "..."} (checked only when the user has set an
        update address; nothing is downloaded). */
    inline std::optional<UpdateInfo> parseUpdateJson (const String& json)
    {
        auto v = JSON::parse (json);
        if (! v.isObject() || ! v.hasProperty ("version")) return std::nullopt;
        return UpdateInfo { v["version"].toString(), v["url"].toString(), v["notes"].toString() };
    }

    /** Fetches the update description in the background and calls back on the message thread. */
    inline void checkForUpdate (const String& address, std::function<void (std::optional<UpdateInfo>)> done)
    {
        if (address.trim().isEmpty()) { done (std::nullopt); return; }
        Thread::launch ([address, done]
        {
            std::optional<UpdateInfo> result;
            if (auto in = URL (address).createInputStream (URL::InputStreamOptions (URL::ParameterHandling::inAddress).withConnectionTimeoutMs (8000)))
                result = parseUpdateJson (in->readEntireStreamAsString());
            MessageManager::callAsync ([done, result] { done (result); });
        });
    }
}
