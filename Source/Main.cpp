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
class PulseStudioApplication : public JUCEApplication
{
public:
    const String getApplicationName() override       { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override    { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override       { return true; }   // the plugin scanner is a second instance

    void initialise (const String& commandLine) override
    {
        // When launched as the plugin-scanner child process, do nothing else.
        if (te::PluginManager::startChildProcessPluginScan (commandLine))
            return;

        // When launched as a plugin sandbox helper, just host that plugin.
        if (sandbox::startWorkerIfRequested (commandLine))
            return;

        auto args = StringArray::fromTokens (getCommandLineParameters(), true);
        StartupTimer::origin();

        // --crash-test : used by the self-test to check the crash handler (crashes "inside" a fake plugin load)
        if (args.contains ("--crash-test"))
        {
            CrashGuard::install ("Pulse Studio Test");
            CrashGuard::beginLoading ("C:/Fake Plugins/Crashy.vst3", "Crashy");
            volatile int* p = nullptr;
            *p = 42;
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
        }
        StartupTimer::mark ("look and feel, crash guard");
        auto behaviour = std::make_unique<PulseEngineBehaviour>();
        behaviour->openDevicesInEngine = false;
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
        MessageManager::callAsync ([this] { StartupTimer::mark ("first message loop"); StartupTimer::save(); splash = nullptr; });

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
