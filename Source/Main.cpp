#include "Common.h"
#include "PulseLookAndFeel.h"
#include "PluginWindow.h"
#include "MainComponent.h"
#include "SelfTest.h"
#include "CrashGuard.h"
#include "Sandbox.h"

//==============================================================================
/** Plugin scans run in a child process, so a crashing plugin can't take the app down. */
struct PulseEngineBehaviour : public te::EngineBehaviour
{
    bool canScanPluginsOutOfProcess() override { return true; }

    /** The engine allows only 4 devices on the master and 16 on a track unless told otherwise. The master already holds
        its meter and its loudness readout, which left room for two: not enough for a mastering chain. */
    te::EditLimits getEditLimits() override
    {
        te::EditLimits l;
        l.maxNumMasterPlugins = 24;
        l.maxPluginsOnTrack = 48;
        l.maxPluginsOnClip = 8;
        return l;
    }

    // Audio worker threads: one per physical core is plenty (the engine's default is every logical core but one,
    // and each of them wakes for every block of audio even when the project is doing nothing).
    int getNumberOfCPUsToUseForAudio() override
    {
        const int logical = jmax (1, SystemStats::getNumCpus()), physical = jmax (1, SystemStats::getNumPhysicalCpus());
        return jlimit (2, 8, jmin (logical, physical));
    }

    // the app opens the audio device itself (timed, see StartupTimer); tests keep the default
    bool openDevicesInEngine = true;
    bool autoInitialiseDeviceManager() override { return openDevicesInEngine; }

    // Safe mode loads no plugins; disabled (crashing) plugins are skipped. Their settings stay in the project.
    bool shouldLoadPlugin (te::ExternalPlugin& p) override
    {
        if (CrashGuard::safeMode() || CrashGuard::isDisabled (p.desc.fileOrIdentifier))
            return false;
        CrashGuard::beginLoading (p.desc.fileOrIdentifier, p.desc.name);
        return true;
    }

    void doAdditionalInitialisation (te::ExternalPlugin&) override  { CrashGuard::endLoading(); }
    bool isPluginDisabled (const String& id) override               { return CrashGuard::isDisabled (id); }
    void setPluginDisabled (const String& id, bool d) override       { CrashGuard::setDisabled (id, d); }
};

//==============================================================================
/** Processor time this process has used so far (for --idle-cpu). */
#if JUCE_WINDOWS
extern "C" { struct PulseFileTime { unsigned long lo, hi; };
             __declspec (dllimport) void* __stdcall GetCurrentProcess();
             __declspec (dllimport) int __stdcall GetProcessTimes (void*, PulseFileTime*, PulseFileTime*, PulseFileTime*, PulseFileTime*);
             __declspec (dllimport) void* __stdcall GetCurrentThread();
             __declspec (dllimport) int __stdcall GetThreadTimes (void*, PulseFileTime*, PulseFileTime*, PulseFileTime*, PulseFileTime*);
             struct PulseMemCounters { unsigned long cb, pageFaults; size_t peakWorkingSet, workingSet, quotaPeakPaged, quotaPaged, quotaPeakNonPaged, quotaNonPaged, pagefile, peakPagefile; };
             __declspec (dllimport) int __stdcall K32GetProcessMemoryInfo (void*, PulseMemCounters*, unsigned long); }
#endif
namespace CrashGuardWin { std::map<unsigned long, std::pair<juce::String, double>> threadTimes(); }

namespace ProcessCpu
{
    inline double seconds()
    {
       #if JUCE_WINDOWS
        PulseFileTime created, exited, kernel, user;
        if (GetProcessTimes (GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) return 0.0;
        auto toSeconds = [] (PulseFileTime t) { return (((unsigned long long) t.hi << 32) | t.lo) * 1.0e-7; };
        return toSeconds (kernel) + toSeconds (user);
       #else
        return 0.0;
       #endif
    }

    /** Memory the process has committed (private bytes), in megabytes. */
    inline double privateMegabytes()
    {
       #if JUCE_WINDOWS
        PulseMemCounters c {};
        c.cb = sizeof (c);
        if (K32GetProcessMemoryInfo (GetCurrentProcess(), &c, sizeof (c)) == 0) return 0.0;
        return (double) c.pagefile / (1024.0 * 1024.0);
       #else
        return 0.0;
       #endif
    }

    /** The same for the calling thread only (call it on the message thread for the cost of the user interface). */
    inline double thisThreadSeconds()
    {
       #if JUCE_WINDOWS
        PulseFileTime created, exited, kernel, user;
        if (GetThreadTimes (GetCurrentThread(), &created, &exited, &kernel, &user) == 0) return 0.0;
        auto toSeconds = [] (PulseFileTime t) { return (((unsigned long long) t.hi << 32) | t.lo) * 1.0e-7; };
        return toSeconds (kernel) + toSeconds (user);
       #else
        return 0.0;
       #endif
    }
}

//==============================================================================
class PulseStudioApplication : public JUCEApplication
{
public:
    const String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    // One Pulse Studio at a time: a project double-clicked while it runs opens in the running one. Helper
    // processes (plugin scanner, plugin sandbox) and runs with options (--dev-profile, --selftest ...) are separate.
    bool moreThanOneInstanceAllowed() override       { return About::wantsOwnInstance (getCommandLineParameters()); }

    void anotherInstanceStarted (const String& commandLine) override
    {
        if (mainWindow == nullptr) return;
        mainWindow->setMinimised (false);
        mainWindow->toFront (true);
        if (auto f = About::projectFromCommandLine (StringArray::fromTokens (commandLine, true)); f.existsAsFile())
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->openProjectFile (f);
    }

    void initialise (const String& commandLine) override
    {
        // When launched as the plugin-scanner child process, do nothing else.
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        // When launched as a plugin sandbox helper, just host that plugin.
        if (sandbox::startWorkerIfRequested (commandLine))
            return;

        auto args = StringArray::fromTokens (getCommandLineParameters(), true);

        // --log <file> : everything the engine, JUCE and the app report (in a debug build that includes failed
        // assertions and leaked objects) goes to this file; used when running the tests under a debug build
        if (auto li = args.indexOf ("--log"); li >= 0 && li + 1 < args.size())
        {
            auto f = File::getCurrentWorkingDirectory().getChildFile (args[li + 1].unquoted());
            f.deleteFile();
            // never freed on purpose: objects found leaked are reported after the app object itself has gone
            Logger::setCurrentLogger (new FileLogger (f, "Pulse Studio log", 64 * 1024 * 1024));
        }
        StartupTimer::origin();

        // --crash-test : used by the self-test to check the crash handler (crashes "inside" a fake plugin load)
        if (args.contains ("--crash-test"))
        {
            CrashGuard::install ("Pulse Studio Test");
            CrashGuard::beginLoading ("C:/Fake Plugins/Crashy.vst3", "Crashy");
            volatile int* p = nullptr;
            *p = 42;
        }

        // --device-shots <dir> : pictures of the graph-first device editors, then exit
        if (auto i = args.indexOf ("--device-shots"); i >= 0 && i + 1 < args.size())
        {
            engine = std::make_unique<te::Engine> ("Pulse Studio Test", std::make_unique<PulseUIBehaviour>(), std::make_unique<PulseEngineBehaviour>());
            pulse::registerDevices (*engine);
            setApplicationReturnValue (SelfTest::deviceShots (*engine, File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted())));
            quit();
            return;
        }

        // --selftest report.txt : exercise the features on a scratch project, then exit
        if (auto i = args.indexOf ("--selftest"); i >= 0)
        {
            CrashGuard::install ("Pulse Studio Test");
            engine = std::make_unique<te::Engine> ("Pulse Studio Test", std::make_unique<PulseUIBehaviour>(), std::make_unique<PulseEngineBehaviour>());
            pulse::registerDevices (*engine);
            auto report = File::getCurrentWorkingDirectory().getChildFile (i + 1 < args.size() ? args[i + 1].unquoted() : "selftest.txt");
            setApplicationReturnValue (SelfTest::run (*engine, report));
            quit();
            return;
        }

        // --dev-profile: separate settings + projects folder (used for testing new builds)
        const String profile = args.contains ("--dev-profile") ? "Pulse Studio Dev" : "Pulse Studio";

        lookAndFeel = std::make_unique<PulseLookAndFeel>();
        LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        tooltips = std::make_unique<TooltipWindow> (nullptr, 600);

        // crash reports + safe mode (--safe-mode, or hold Shift while starting)
        CrashGuard::install (profile);
        const bool safeRequested = args.contains ("--safe-mode") || ModifierKeys::getCurrentModifiersRealtime().isShiftDown();
        CrashGuard::safeMode() = CrashGuard::askAfterCrash (safeRequested);

        // a splash right away: opening the audio interface can take a couple of seconds
        if (! args.contains ("--snapshots") && ! args.contains ("--render"))
        {
            splash = std::make_unique<Splash>();
            splash->setStatus ("Starting the audio engine...");
            StartupTimer::status() = [this] (const String& s) { if (splash != nullptr) splash->setStatus (s); };
        }
        StartupTimer::mark ("look and feel, crash guard");
        auto behaviour = std::make_unique<PulseEngineBehaviour>();
        behaviour->openDevicesInEngine = false;
        // idle worker threads sleep on a semaphore after a short spin (the default keeps yielding, which costs processor all the time)
        te::EditPlaybackContext::setThreadPoolStrategy ((int) te::graph::ThreadPoolStrategy::lightweightSemaphore);
        engine = std::make_unique<te::Engine> (profile, std::make_unique<PulseUIBehaviour>(), std::move (behaviour));
        StartupTimer::mark ("engine (no devices)");
        if (splash != nullptr) splash->setStatus ("Opening the audio device (this can take a moment)...");
        engine->getDeviceManager().initialise (engine->getEngineBehaviour().shouldOpenAudioInputByDefault() ? te::DeviceManager::defaultNumChannelsToOpen : 0,
                                               te::DeviceManager::defaultNumChannelsToOpen);
        StartupTimer::mark ("audio / MIDI devices opened");
        pulse::registerDevices (*engine);
        StartupTimer::mark ("devices registered");
        if (splash != nullptr) splash->setStatus ("Loading the project...");
        Theme::apply (Theme::indexOf (engine->getPropertyStorage().getPropertiesFile().getValue ("theme", "Pulse Dark")));
        lookAndFeel->applyTheme();
        mainWindow = std::make_unique<MainWindow> (*engine);
        StartupTimer::mark ("main window shown");
        MessageManager::callAsync ([this] { StartupTimer::mark ("first message loop"); StartupTimer::save(); StartupTimer::status() = nullptr; splash = nullptr; });

        // Pulse Studio.exe --snapshots <dir> : screenshots of the main views (for UI work), then exit
        if (auto i = args.indexOf ("--snapshots"); i >= 0 && i + 1 < args.size())
        {
            struct Runner : public Timer
            {
                Runner (MainComponent& m, File d) : mc (m), dir (d) { startTimer (1500); }
                void timerCallback() override
                {
                    if (step > 0) mc.saveSnapshot (dir.getChildFile ("view_" + String (step - 1) + ".png"));
                    if (step >= 12) { stopTimer(); JUCEApplication::quit(); return; }
                    mc.prepareSnapshot (step++);
                }
                MainComponent& mc;
                File dir;
                int step = 0;
            };
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                mainWindow->setBounds (40, 40, 1600, 940);
                auto dir = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
                mc->startSnapshotDemo (dir);
                snapshotRunner = std::make_unique<Runner> (*mc, dir);
            }
        }

        // Pulse Studio.exe --idle-cpu <file> : how much processor the app uses doing nothing, in three states, then exit
        if (auto i = args.indexOf ("--idle-cpu"); i >= 0 && i + 1 < args.size())
        {
            struct Runner : public Timer
            {
                Runner (MainComponent& m, File f) : mc (m), out (f) { startTimer (12000); }   // let the start-up work finish first
                void timerCallback() override
                {
                    const double cpu = ProcessCpu::seconds(), ui = ProcessCpu::thisThreadSeconds(), wall = Time::getMillisecondCounterHiRes() * 0.001;
                    if (step > 0)
                    {
                        lines.add (names[step - 1] + ": " + String (100.0 * (cpu - lastCpu) / jmax (0.001, wall - lastWall), 1) + "% of one core in all, "
                                   + String (100.0 * (ui - lastUi) / jmax (0.001, wall - lastWall), 1) + "% for the user interface");
                        // which threads did the work (busiest first)
                        std::vector<std::pair<double, String>> busy;
                        for (auto& [id, now] : CrashGuardWin::threadTimes())
                        {
                            const auto before = lastThreads.find (id);
                            const double used = now.second - (before != lastThreads.end() ? before->second.second : 0.0);
                            if (used > 0.0005 * (wall - lastWall)) busy.push_back ({ used, now.first.isNotEmpty() ? now.first : "thread " + String ((int) id) });
                        }
                        std::sort (busy.begin(), busy.end(), [] (auto& a, auto& b) { return a.first > b.first; });
                        String detail = "      ";
                        for (size_t k = 0; k < jmin<size_t> (8, busy.size()); ++k)
                            detail << busy[k].second << " " << String (100.0 * busy[k].first / jmax (0.001, wall - lastWall), 1) << "%   ";
                        lines.add (detail + "(" + String ((int) busy.size()) + " threads busy)");
                    }
                    if (step >= 4) { stopTimer(); out.replaceWithText (lines.joinIntoString ("\n") + "\n"); JUCEApplication::quit(); return; }
                    if (step < 3) mc.stopForMeasurement();        // (the demo starts playing by itself)
                    if (step == 0) mc.prepareSnapshot (0);        // stopped, device chain showing
                    if (step == 1) mc.prepareSnapshot (1);        // stopped, mixer showing
                    if (step == 2) mc.prepareSnapshot (0);        // devices again (shows whether the cost depends on the view or on time)
                    if (step == 3) mc.playForMeasurement();       // playing
                    ++step;
                    lastCpu = ProcessCpu::seconds();
                    lastUi = ProcessCpu::thisThreadSeconds();
                    lastThreads = CrashGuardWin::threadTimes();
                    lastWall = Time::getMillisecondCounterHiRes() * 0.001;
                    startTimer (10000);
                }
                MainComponent& mc;
                File out;
                StringArray lines, names { "stopped, devices showing", "stopped, mixer showing", "stopped, devices showing again", "playing" };
                int step = 0;
                double lastCpu = 0, lastUi = 0, lastWall = 0;
                std::map<unsigned long, std::pair<String, double>> lastThreads;
            };
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                mainWindow->setBounds (40, 40, 1600, 940);
                auto file = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
                mc->startSnapshotDemo (file.getParentDirectory());
                if (auto bigAt = args.indexOf ("--big"); bigAt >= 0) mc->makeBigProjectForTest (jlimit (8, 120, args[bigAt + 1].getIntValue()));
                idleRunner = std::make_unique<Runner> (*mc, file);
            }
        }

        // Pulse Studio.exe --command-fuzz <file> : runs every unattended command many times in a random order on the
        // demo project (with different things selected), writing each one to the file first so a crash names its cause
        if (auto i = args.indexOf ("--command-fuzz"); i >= 0 && i + 1 < args.size())
        {
            struct Runner : public Timer
            {
                Runner (MainComponent& m, File f, int64 seed) : mc (m), out (f), ids (m.commandsForTest()), rng (seed)
                {
                    out.replaceWithText ("commands: " + ids.joinIntoString (" ") + "\n");
                    startTimer (4000);
                }
                void timerCallback() override
                {
                    startTimer (120);
                    if (done >= rounds * ids.size())
                    {
                        stopTimer();
                        out.appendText ("DONE " + String (done) + " commands, " + String (mc.numTracksForTest()) + " tracks at the end\n");
                        JUCEApplication::getInstance()->setApplicationReturnValue (0);
                        JUCEApplication::quit();
                        return;
                    }
                    if (done % 3 == 0) mc.selectSomethingForTest (rng);
                    const auto id = ids[rng.nextInt (ids.size())];
                    out.appendText (String (done) + " " + id + "\n");
                    mc.runCommandForTest (id);
                    ++done;
                }
                MainComponent& mc;
                File out;
                StringArray ids;
                Random rng;
                int done = 0;
                const int rounds = 12;
            };
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                setApplicationReturnValue (1);   // stays 1 unless the run reaches its end
                mainWindow->setBounds (40, 40, 1600, 940);
                auto file = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
                file.getParentDirectory().createDirectory();
                mc->startSnapshotDemo (file.getParentDirectory());
                if (auto bigAt = args.indexOf ("--big"); bigAt >= 0) mc->makeBigProjectForTest (jlimit (8, 120, args[bigAt + 1].getIntValue()));
                const auto seedAt = args.indexOf ("--seed");   // optional: --seed <number> for a different order
                idleRunner = std::make_unique<Runner> (*mc, file, seedAt >= 0 ? args[seedAt + 1].getLargeIntValue() : 20261005);
            }
        }

        // Pulse Studio.exe --monkey <file> [--seed n] [--events n] : random clicks, drags, wheel turns and key presses on the
        // main window (demo project), each written to the file first so a crash names the last thing done
        if (auto i = args.indexOf ("--monkey"); i >= 0 && i + 1 < args.size())
        {
            struct Runner : public Timer
            {
                Runner (DocumentWindow& w, MainComponent& m, File f, int64 seed, int numEvents) : window (w), mc (m), out (f), rng (seed), total (numEvents)
                {
                    out.replaceWithText ("monkey run, seed " + String (seed) + ", " + String (numEvents) + " events\n");
                    startTimer (5000);
                }
                /** The window the next events go to: a device window for a while after one was opened, else the main one. */
                Component& target() { return deviceWindow != nullptr && deviceEvents > 0 ? *deviceWindow : static_cast<Component&> (window); }
                juce::Point<float> anywhere()
                {
                    if (&target() != &window) return { rng.nextFloat() * (float) target().getWidth(), rng.nextFloat() * (float) target().getHeight() };
                    // anywhere in the window, top bar included, except a few buttons that reach outside the test project
                    for (int attempt = 0; attempt < 20; ++attempt)
                    {
                        const juce::Point<float> p (rng.nextFloat() * (float) window.getWidth(), rng.nextFloat() * (float) window.getHeight());
                        if (mc.safeForRandomInput (mc.getLocalPoint (&window, p.toInt()))) return p;
                    }
                    return { rng.nextFloat() * (float) window.getWidth(), 64.0f + rng.nextFloat() * (float) (window.getHeight() - 70) };
                }
                void mouse (juce::Point<float> p, ModifierKeys mods)
                {
                    if (auto* peer = target().getPeer())
                        peer->handleMouseEvent (MouseInputSource::InputSourceType::mouse, p, mods, MouseInputSource::defaultPressure, MouseInputSource::defaultOrientation, Time::currentTimeMillis());
                }
                void tidy()
                {
                    // menus, dialogs, call-out boxes and device windows opened along the way are closed again
                    PopupMenu::dismissAllActiveMenus();
                    for (int guard = 0; guard < 8; ++guard)
                        if (auto* m = Component::getCurrentlyModalComponent()) m->exitModalState (0); else break;
                    const bool keepDevice = deviceWindow != nullptr && deviceEvents > 0;
                    if (! keepDevice) DeviceEditors::closeAll();
                    for (int w = TopLevelWindow::getNumTopLevelWindows(); --w >= 0;)
                        if (auto* tw = TopLevelWindow::getTopLevelWindow (w); tw != nullptr && tw != &window && ! (keepDevice && tw == deviceWindow.getComponent()))
                        {
                            if (auto* dw = dynamic_cast<DialogWindow*> (tw)) dw->closeButtonPressed();
                            else if (auto* doc = dynamic_cast<DocumentWindow*> (tw)) doc->closeButtonPressed();
                        }
                }
                void timerCallback() override
                {
                    startTimer (20);
                    if (done >= total)
                    {
                        stopTimer();
                        tidy();
                        out.appendText ("DONE " + String (done) + " events\n");
                        JUCEApplication::getInstance()->setApplicationReturnValue (0);
                        JUCEApplication::quit();
                        return;
                    }
                    if (deviceEvents > 0) --deviceEvents;
                    if (done % 300 == 20)    // a device window every so often, and the next 120 events go to it
                    {
                        tidy();
                        deviceWindow = mc.openDeviceWindowForTest (rng);
                        deviceEvents = deviceWindow != nullptr ? 120 : 0;
                        out.appendText (String (done) + " device window " + (deviceWindow != nullptr ? deviceWindow->getName() : String ("(none)")) + newLine);
                        ++done;
                        return;
                    }
                    if (done % 300 == 150)   // another view every so often: session, mixer, piano roll, other tracks
                    {
                        const int view = rng.nextInt (6);
                        out.appendText (String (done) + " view " + String (view) + newLine);
                        tidy();
                        mc.prepareSnapshot (view);
                        ++done;
                        return;
                    }
                    if (done % 25 == 24) { out.appendText (String (done) + " tidy\n"); tidy(); ++done; return; }
                    const int kind = rng.nextInt (100);
                    const auto p = anywhere();
                    const auto none = ModifierKeys();
                    if (kind < 45)
                    {
                        const bool right = rng.nextInt (8) == 0, shift = rng.nextInt (10) == 0, ctrl = rng.nextInt (12) == 0;
                        int flags = right ? ModifierKeys::rightButtonModifier : ModifierKeys::leftButtonModifier;
                        if (shift) flags |= ModifierKeys::shiftModifier;
                        if (ctrl) flags |= ModifierKeys::ctrlModifier;
                        out.appendText (String (done) + (right ? " right-click " : " click ") + p.toString() + "\n");
                        mouse (p, none); mouse (p, ModifierKeys (flags)); mouse (p, none);
                        if (rng.nextInt (6) == 0) { mouse (p, ModifierKeys (flags)); mouse (p, none); }   // a double-click now and then
                    }
                    else if (kind < 75)
                    {
                        const auto q = rng.nextInt (3) == 0 ? anywhere() : p + juce::Point<float> (rng.nextFloat() * 240.0f - 120.0f, rng.nextFloat() * 160.0f - 80.0f);
                        const ModifierKeys held (ModifierKeys::leftButtonModifier | (rng.nextInt (8) == 0 ? ModifierKeys::altModifier : 0));
                        out.appendText (String (done) + " drag " + p.toString() + " -> " + q.toString() + "\n");
                        mouse (p, none); mouse (p, held);
                        for (int s = 1; s <= 4; ++s) mouse (p + (q - p) * (s / 4.0f), held);
                        mouse (q, none);
                    }
                    else if (kind < 88)
                    {
                        MouseWheelDetails wd;
                        wd.deltaX = rng.nextInt (5) == 0 ? rng.nextFloat() - 0.5f : 0.0f;
                        wd.deltaY = rng.nextFloat() - 0.5f;
                        wd.isReversed = false; wd.isSmooth = false; wd.isInertial = false;
                        out.appendText (String (done) + " wheel " + p.toString() + "\n");
                        mouse (p, none);
                        if (auto* peer = target().getPeer()) peer->handleMouseWheel (MouseInputSource::InputSourceType::mouse, p, Time::currentTimeMillis(), wd);
                    }
                    else
                    {
                        const KeyPress keys[] = { KeyPress (KeyPress::spaceKey), KeyPress (KeyPress::deleteKey), KeyPress (KeyPress::escapeKey), KeyPress (KeyPress::tabKey),
                                                  KeyPress (KeyPress::leftKey), KeyPress (KeyPress::rightKey), KeyPress (KeyPress::upKey), KeyPress (KeyPress::downKey),
                                                  KeyPress ('z', ModifierKeys::ctrlModifier, 0), KeyPress ('y', ModifierKeys::ctrlModifier, 0), KeyPress ('d', ModifierKeys::ctrlModifier, 0),
                                                  KeyPress ('a', ModifierKeys::ctrlModifier, 0), KeyPress ('c', ModifierKeys::ctrlModifier, 0), KeyPress ('v', ModifierKeys::ctrlModifier, 0),
                                                  KeyPress ('e', ModifierKeys::ctrlModifier, 0), KeyPress ('b'), KeyPress ('q'), KeyPress ('l'), KeyPress ('m'), KeyPress (KeyPress::homeKey) };
                        const auto k = keys[rng.nextInt ((int) std::size (keys))];
                        out.appendText (String (done) + " key " + k.getTextDescription() + "\n");
                        if (auto* peer = target().getPeer()) peer->handleKeyPress (k);
                    }
                    ++done;
                }
                DocumentWindow& window;
                MainComponent& mc;
                Component::SafePointer<Component> deviceWindow;
                int deviceEvents = 0;
                File out;
                Random rng;
                int done = 0, total = 2000;
            };
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                setApplicationReturnValue (1);   // stays 1 unless the run reaches its end
                mainWindow->setBounds (40, 40, 1600, 940);
                auto file = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
                file.getParentDirectory().createDirectory();
                mc->startSnapshotDemo (file.getParentDirectory());
                if (auto bigAt = args.indexOf ("--big"); bigAt >= 0) mc->makeBigProjectForTest (jlimit (8, 120, args[bigAt + 1].getIntValue()));
                const auto seedAt = args.indexOf ("--seed"), eventsAt = args.indexOf ("--events");
                idleRunner = std::make_unique<Runner> (*mainWindow, *mc, file, seedAt >= 0 ? args[seedAt + 1].getLargeIntValue() : 1,
                                                       eventsAt >= 0 ? args[eventsAt + 1].getIntValue() : 2000);
            }
        }

        // Pulse Studio.exe --soak <file> [--minutes n] : plays the demo song round and round, switching views, and writes
        // the memory in use every half minute (a leak shows as a figure that keeps climbing)
        if (auto i = args.indexOf ("--soak"); i >= 0 && i + 1 < args.size())
        {
            struct Runner : public Timer
            {
                Runner (MainComponent& m, File f, int mins) : mc (m), out (f), samples (mins * 2)
                {
                    out.replaceWithText ("soak for " + String (mins) + " minutes" + newLine);
                    startTimer (15000);
                }
                void timerCallback() override
                {
                    startTimer (30000);
                    const double mb = ProcessCpu::privateMegabytes();
                    readings.add (mb);
                    out.appendText (String (readings.size() * 0.5, 1) + " min: " + String (mb, 1) + " MB" + newLine);
                    if (readings.size() > samples)
                    {
                        stopTimer();
                        // judged over the last third of the run: start-up work, caches and the first full backup
                        // (about five minutes in, a one-off of some tens of MB) are behind it by then
                        const int from = readings.size() - 1 - jmax (2, readings.size() / 3);
                        const double growth = readings.getLast() - readings[from];
                        out.appendText ("GROWTH over the last " + String ((readings.size() - 1 - from) * 0.5, 1) + " minutes: " + String (growth, 1) + " MB" + newLine + "DONE" + newLine);
                        JUCEApplication::getInstance()->setApplicationReturnValue (growth < 15.0 ? 0 : 2);
                        JUCEApplication::quit();
                        return;
                    }
                    if (readings.size() == 3) out.appendText ("backup steps: " + mc.backupStepsForTest ([] { return ProcessCpu::privateMegabytes(); }) + newLine);
                    mc.prepareSnapshot (readings.size() % 6);   // devices, mixer, session, piano roll ...
                    mc.playForMeasurement();
                }
                MainComponent& mc;
                File out;
                int samples;
                Array<double> readings;
            };
            if (auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
            {
                setApplicationReturnValue (1);
                mainWindow->setBounds (40, 40, 1600, 940);
                auto file = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
                file.getParentDirectory().createDirectory();
                mc->startSnapshotDemo (file.getParentDirectory());
                if (auto bigAt = args.indexOf ("--big"); bigAt >= 0) mc->makeBigProjectForTest (jlimit (8, 120, args[bigAt + 1].getIntValue()));
                const auto minsAt = args.indexOf ("--minutes");
                idleRunner = std::make_unique<Runner> (*mc, file, minsAt >= 0 ? jmax (2, args[minsAt + 1].getIntValue()) : 6);
            }
        }

        // Pulse Studio.exe --render out.wav : bounce the last project and exit
        if (auto i = args.indexOf ("--render"); i >= 0 && i + 1 < args.size())
        {
            auto* mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent());
            auto out = File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted());
            setApplicationReturnValue (mc != nullptr && mc->renderTo (out) ? 0 : 1);
            quit();
        }
    }

    void shutdown() override
    {
        snapshotRunner = nullptr;
        mainWindow = nullptr;
        engine = nullptr;
        tooltips = nullptr;
        if (lookAndFeel != nullptr)
            LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public DocumentWindow
    {
    public:
        explicit MainWindow (te::Engine& engine)
            : DocumentWindow ("Pulse Studio", Theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine), true);
            setResizable (true, false);
            setResizeLimits (1000, 650, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            auto saved = engine.getPropertyStorage().getPropertiesFile().getValue ("windowState");
            if (saved.isNotEmpty()) restoreWindowStateFromString (saved);
            setVisible (true);
            getContentComponent()->grabKeyboardFocus();
        }

        ~MainWindow() override
        {
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                mc->getEngine().getPropertyStorage().getPropertiesFile().setValue ("windowState", getWindowStateAsString());
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<PulseLookAndFeel> lookAndFeel;
    std::unique_ptr<TooltipWindow> tooltips;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<Timer> idleRunner;
    std::unique_ptr<Timer> snapshotRunner;

    /** Shown while the engine and audio device start. */
    struct Splash : public Component
    {
        Splash()
        {
            setOpaque (true);
            setSize (440, 200);
            addToDesktop (ComponentPeer::windowAppearsOnTaskbar);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            toFront (false);
        }
        void setStatus (const String& s)
        {
            status = s;
            repaint();
            if (auto* peer = getPeer()) peer->performAnyPendingRepaintsNow();
            MessageManager::getInstance()->runDispatchLoopUntil (15);   // let Windows show it before the next slow step
        }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setGradientFill (ColourGradient (Colour (0xff1c2027), 0.0f, 0.0f, Colour (0xff111317), 0.0f, r.getBottom(), false));
            g.fillRect (r);
            g.setColour (Theme::accent);
            g.fillRect (r.removeFromBottom (3.0f));
            // pulse mark
            Path wave;
            const float cx = 40.0f, cy = 78.0f;
            wave.startNewSubPath (cx, cy);
            wave.lineTo (cx + 14, cy); wave.lineTo (cx + 22, cy - 22); wave.lineTo (cx + 32, cy + 20); wave.lineTo (cx + 40, cy - 8); wave.lineTo (cx + 46, cy); wave.lineTo (cx + 60, cy);
            g.setColour (Theme::accent);
            g.strokePath (wave, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (Theme::text);
            g.setFont (FontOptions (30.0f, Font::bold));
            g.drawText ("Pulse Studio", 112, 54, 300, 40, Justification::centredLeft);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.0f));
            g.drawText ("Version " + JUCEApplication::getInstance()->getApplicationVersion(), 114, 92, 300, 18, Justification::centredLeft);
            g.setFont (FontOptions (13.0f));
            g.drawText (status, 40, 150, 380, 20, Justification::centredLeft);
        }
        String status;
    };
    std::unique_ptr<Splash> splash;
};

START_JUCE_APPLICATION (PulseStudioApplication)
