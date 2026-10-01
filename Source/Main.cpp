#include "Common.h"
#include "PulseLookAndFeel.h"
#include "PluginWindow.h"
#include "MainComponent.h"
#include "SelfTest.h"

//==============================================================================
/** Plugin scans run in a child process, so a crashing plugin can't take the app down. */
struct PulseEngineBehaviour : public te::EngineBehaviour
{
    bool canScanPluginsOutOfProcess() override { return true; }
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

        auto args = StringArray::fromTokens (getCommandLineParameters(), true);

        // --selftest report.txt : exercise the features on a scratch project, then exit
        if (auto i = args.indexOf ("--selftest"); i >= 0)
        {
            engine = std::make_unique<te::Engine> ("Pulse Studio Test", std::make_unique<PulseUIBehaviour>(), std::make_unique<PulseEngineBehaviour>());
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
        engine = std::make_unique<te::Engine> (profile, std::make_unique<PulseUIBehaviour>(), std::make_unique<PulseEngineBehaviour>());
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
            setVisible (true);
            getContentComponent()->grabKeyboardFocus();
        }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<PulseLookAndFeel> lookAndFeel;
    std::unique_ptr<TooltipWindow> tooltips;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (PulseStudioApplication)
