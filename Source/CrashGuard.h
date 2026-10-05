#pragma once

#include <csignal>
#include <cstdlib>
#include <exception>

#include "Common.h"

namespace CrashGuardWin { void describe (void* exceptionPointers, String& modulePath, String& description);
                          void installDebugCheckHook (void (*onFail) (const char*)); }

/** Crash reporting, plugin blocklist and safe mode.
    - A crash handler writes a log (with a stack trace) and notes which plugin it happened in:
      the module that faulted, or the plugin that was being loaded at the time.
    - On the next launch the app offers to disable that plugin or start in safe mode.
    - Disabled plugins (and every plugin, in safe mode) are not loaded; their settings stay in the project. */
namespace CrashGuard
{
    inline File& rootFolder() { static File f; return f; }
    inline File crashFolder()  { auto d = rootFolder().getChildFile ("Crashes"); d.createDirectory(); return d; }
    inline File markerFile()   { return rootFolder().getChildFile ("last-crash.xml"); }
    inline File loadingFile()  { return rootFolder().getChildFile ("loading-plugin.txt"); }
    inline File disabledFile() { return rootFolder().getChildFile ("disabled-plugins.txt"); }

    inline bool& safeMode() { static bool b = false; return b; }

    //==============================================================================
    // Disabled plugins (by plugin file path / identifier)
    inline StringArray disabledPlugins()
    {
        StringArray s;
        s.addLines (disabledFile().loadFileAsString());
        s.removeEmptyStrings();
        return s;
    }

    inline void setDisabled (const String& fileOrIdentifier, bool disabled)
    {
        auto s = disabledPlugins();
        s.removeString (fileOrIdentifier, true);
        if (disabled) s.add (fileOrIdentifier);
        disabledFile().replaceWithText (s.joinIntoString ("\n"));
    }

    inline bool isDisabled (const String& fileOrIdentifier)
    {
        for (auto& d : disabledPlugins())
            if (fileOrIdentifier.equalsIgnoreCase (d) || fileOrIdentifier.startsWithIgnoreCase (d) || d.startsWithIgnoreCase (fileOrIdentifier))
                return true;
        return false;
    }

    //==============================================================================
    // Plugin loading markers (a crash while a marker is set is blamed on that plugin)
    inline void beginLoading (const String& fileOrIdentifier, const String& name)
    {
        loadingFile().replaceWithText (fileOrIdentifier + "\n" + name);
    }

    inline void endLoading() { loadingFile().deleteFile(); }

    //==============================================================================
    /** Is this module path a plugin binary (rather than our exe or a system DLL)? */
    inline bool looksLikePlugin (const String& modulePath)
    {
        const auto p = modulePath.toLowerCase();
        return p.contains (".vst3") || (p.endsWith (".dll") && ! p.contains ("\\windows\\") && ! p.contains ("\\system32\\"));
    }

    /** The plugin's bundle path from a module path ("...\X.vst3\Contents\x86_64-win\X.vst3" -> "...\X.vst3"). */
    inline String pluginPathFromModule (const String& modulePath)
    {
        auto lower = modulePath.toLowerCase();
        auto idx = lower.indexOf (".vst3");
        return idx >= 0 ? modulePath.substring (0, idx + 5) : modulePath;
    }

    inline void writeCrashReport (void* exceptionPointers, const String& what = {})
    {
        String modulePath, description;
        CrashGuardWin::describe (exceptionPointers, modulePath, description);
        if (what.isNotEmpty()) description = what;

        String plugin, pluginName;
        if (looksLikePlugin (modulePath))
        {
            plugin = pluginPathFromModule (modulePath);
            pluginName = File (plugin).getFileNameWithoutExtension();
        }
        else if (loadingFile().existsAsFile())   // crashed while loading a plugin
        {
            StringArray lines;
            lines.addLines (loadingFile().loadFileAsString());
            plugin = lines[0];
            pluginName = lines[1];
        }

        const auto now = Time::getCurrentTime();
        auto log = crashFolder().getChildFile ("crash-" + now.formatted ("%Y-%m-%d_%H-%M-%S") + ".txt");
        String text;
        text << "Pulse Studio " << JUCEApplication::getInstance()->getApplicationVersion() << " crashed at " << now.toString (true, true) << "\n"
             << description << "\n"
             << "Module: " << (modulePath.isNotEmpty() ? modulePath : String ("(unknown)")) << "\n"
             << "Plugin: " << (plugin.isNotEmpty() ? plugin : String ("(none identified)")) << "\n\n"
             << SystemStats::getStackBacktrace();
        log.replaceWithText (text);

        XmlElement x ("CRASH");
        x.setAttribute ("time", now.toString (true, true));
        x.setAttribute ("plugin", plugin);
        x.setAttribute ("pluginName", pluginName);
        x.setAttribute ("description", description);
        x.setAttribute ("log", log.getFullPathName());
        x.writeTo (markerFile());
    }

    inline void handler (void* exceptionPointers)
    {
        static std::atomic<bool> inHandler { false };
        if (inHandler.exchange (true)) return;
        writeCrashReport (exceptionPointers);
    }

    inline void install (const String& profileName)
    {
        rootFolder() = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile (profileName);
        rootFolder().createDirectory();
        SystemStats::setApplicationCrashHandler (handler);
        // The program can also stop itself without a hardware fault: abort() (a failed check in the C++ runtime
        // or a library) and std::terminate (an error nobody caught). Those get a report with a stack trace too.
        std::signal (SIGABRT, [] (int)
        {
            static std::atomic<bool> once { false };
            if (! once.exchange (true)) writeCrashReport (nullptr, "The program stopped itself (abort)");
            std::_Exit (3);
        });
        CrashGuardWin::installDebugCheckHook ([] (const char* message)
        {
            static std::atomic<bool> once { false };
            if (! once.exchange (true)) writeCrashReport (nullptr, "A check in the debug build failed: " + String (message).trim());
            std::_Exit (3);
        });
        std::set_terminate ([]
        {
            static std::atomic<bool> once { false };
            if (! once.exchange (true)) writeCrashReport (nullptr, "An error nobody handled (terminate)");
            std::_Exit (3);
        });
    }

    //==============================================================================
    struct Report
    {
        String time, plugin, pluginName, description;
        File log;
    };

    /** The last crash (if any), clearing the marker. */
    inline std::optional<Report> takeLastCrash()
    {
        auto f = markerFile();
        if (! f.existsAsFile()) return {};
        std::optional<Report> r;
        if (auto x = parseXML (f))
            r = Report { x->getStringAttribute ("time"), x->getStringAttribute ("plugin"), x->getStringAttribute ("pluginName"),
                         x->getStringAttribute ("description"), File (x->getStringAttribute ("log")) };
        f.deleteFile();
        endLoading();
        return r;
    }

    /** Startup: if the last run crashed, ask what to do. Returns true for safe mode. */
    inline bool askAfterCrash (bool safeModeRequested)
    {
        auto crash = takeLastCrash();
        if (! crash) return safeModeRequested;

        const bool hasPlugin = crash->plugin.isNotEmpty();
        String msg;
        msg << "Pulse Studio crashed last time (" << crash->time << ").\n\n";
        if (hasPlugin) msg << "It happened inside the plugin \"" << crash->pluginName << "\".\n\n";
        msg << "A crash log was saved to:\n" << crash->log.getFullPathName();

        if (hasPlugin)
        {
            const int r = AlertWindow::showYesNoCancelBox (MessageBoxIconType::WarningIcon, "Pulse Studio crashed", msg,
                                                           "Disable " + crash->pluginName, "Start in safe mode", "Continue normally");
            if (r == 1) { setDisabled (crash->plugin, true); return safeModeRequested; }
            return r == 2 || safeModeRequested;
        }

        return AlertWindow::showOkCancelBox (MessageBoxIconType::WarningIcon, "Pulse Studio crashed", msg + "\n\nStart in safe mode (no plugins loaded)?",
                                             "Start in safe mode", "Continue normally")
               || safeModeRequested;
    }
}
