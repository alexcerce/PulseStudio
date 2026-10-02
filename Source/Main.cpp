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
        engine = std::make_unique<te::Engine> (profile, std::make_unique<PulseUIBehaviour>(), std::make_unique<PulseEngineBehaviour>());
        pulse::registerDevices (*engine);
        mainWindow = std::make_unique<MainWindow> (*engine);

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
};

START_JUCE_APPLICATION (PulseStudioApplication)
