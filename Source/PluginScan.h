#pragma once

#include "Common.h"

/** Scans for new VST3 plugins in the background (only files not already known), using the
    engine's out-of-process scanner so a crashing plugin can't take Pulse down. */
class BackgroundPluginScan : private Thread
{
public:
    explicit BackgroundPluginScan (te::Engine& e) : Thread ("Plugin scan"), engine (e) {}
    ~BackgroundPluginScan() override { stopThread (10000); }

    /** Starts a scan (does nothing if one is already running). */
    void start()
    {
        if (isThreadRunning()) return;
        progress = 0.0f;
        found = 0;
        startThread (Thread::Priority::low);
    }

    void cancel() { signalThreadShouldExit(); }
    bool isScanning() const { return isThreadRunning(); }
    float getProgress() const { return progress.load(); }

    String getStatus() const
    {
        const ScopedLock sl (lock);
        return "Scanning plugins" + (current.isNotEmpty() ? ": " + current : String());
    }

    /** How many new plugins the last scan added. */
    int getNumFound() const { return found.load(); }

    /** Where VST3s are looked for: the default folders plus any the plugin manager was pointed at. */
    static FileSearchPath searchPath (te::Engine& e, AudioPluginFormat& f)
    {
        auto path = f.getDefaultLocationsToSearch();
        auto saved = e.getPropertyStorage().getPropertiesFile().getValue ("lastPluginScanPath_" + f.getName());
        if (saved.isNotEmpty())
        {
            FileSearchPath extra (saved);
            for (int i = 0; i < extra.getNumPaths(); ++i) path.addIfNotAlreadyThere (extra.getRawString (i));
        }
        path.removeRedundantPaths();
        path.removeNonExistentPaths();
        return path;
    }

private:
    void run() override
    {
        auto& pm = engine.getPluginManager();
        auto& list = pm.knownPluginList;
        const int before = list.getNumTypes();
        for (auto* format : pm.pluginFormatManager.getFormats())
        {
            if (! format->getName().containsIgnoreCase ("VST3")) continue;
            auto pedal = engine.getTemporaryFileManager().getTempFile ("PulseBackgroundScanPedal");
            PluginDirectoryScanner scanner (list, *format, searchPath (engine, *format), true, pedal, false);
            String name;
            while (! threadShouldExit())
            {
                {
                    const ScopedLock sl (lock);
                    current = scanner.getNextPluginFileThatWillBeScanned();
                }
                if (! scanner.scanNextFile (true, name)) break;
                progress = scanner.getProgress();
            }
        }
        found = jmax (0, list.getNumTypes() - before);
        progress = 1.0f;
    }

    te::Engine& engine;
    std::atomic<float> progress { 0.0f };
    std::atomic<int> found { 0 };
    CriticalSection lock;
    String current;
};
