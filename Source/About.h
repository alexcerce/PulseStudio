#pragma once

/** The About box: version, what the app is built on, and where the licences and logs are. */
namespace About
{
    /** Shown next to the version number while 1.0 is being prepared (empty for a final release). */
    inline String stage() { return "1.0 release candidate 1"; }

    inline String versionLine()
    {
        auto v = "Version " + JUCEApplication::getInstance()->getApplicationVersion();
        if (stage().isNotEmpty()) v << "  (" << stage() << ")";
        return v;
    }

    /** The folder of the installed files (the notices and the changelog sit next to the program). */
    inline File installFolder() { return File::getSpecialLocation (File::currentExecutableFile).getParentDirectory(); }
    inline File noticesFile()   { return installFolder().getChildFile ("THIRD-PARTY-NOTICES.md"); }
    inline File changelogFile() { return installFolder().getChildFile ("CHANGELOG.md"); }

    inline String creditsText()
    {
        return "A music studio for Windows: arrangement and session views, instruments, effects, recording and export.\n\n"
               "Built with JUCE 8, Tracktion Engine, Ableton Link, the ARA SDK, the VST3 SDK and libusb. "
               "Each stays under its own licence (see Third-party licences).\n\n"
               "VST is a registered trademark of Steinberg Media Technologies GmbH.\n"
               "64-bit build of " + String (__DATE__) + ".";
    }

    inline String licenceSummary()
    {
        return "JUCE 8: AGPLv3 or commercial\n"
               "Tracktion Engine: GPLv3 or commercial\n"
               "Ableton Link: GPLv2 or later, or proprietary\n"
               "ARA SDK: Apache 2.0\n"
               "VST3 SDK (through JUCE): GPLv3 or Steinberg VST3 licence\n"
               "libusb: LGPL 2.1 or later\n\n"
               "The full notices are in THIRD-PARTY-NOTICES.md, installed next to the program.";
    }

    inline void show (Component* parent)
    {
        auto* w = new AlertWindow ("Pulse Studio", versionLine() + "\n\n" + creditsText(), MessageBoxIconType::NoIcon, parent);
        w->addButton ("Third-party licences", 1);
        w->addButton ("What's new", 2);
        w->addButton ("Close", 0, KeyPress (KeyPress::escapeKey), KeyPress (KeyPress::returnKey));
        w->enterModalState (true, ModalCallbackFunction::create ([parent] (int r)
        {
            if (r == 1)
            {
                if (noticesFile().existsAsFile()) noticesFile().startAsProcess();
                else AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Third-party licences", licenceSummary(), "OK", parent);
            }
            if (r == 2)
            {
                if (changelogFile().existsAsFile()) changelogFile().startAsProcess();
                else AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "What's new", "The changelog is installed next to the program (CHANGELOG.md).", "OK", parent);
            }
        }), true);
    }

    /** True if a file reads as a project (so a damaged or foreign file is never opened and then saved over). */
    inline bool isProjectFile (const File& f)
    {
        if (! f.existsAsFile() || f.getSize() < 8) return false;
        if (auto xml = parseXML (f)) return xml->hasTagName ("EDIT");
        return false;
    }

    /** A project file named on the command line (double-clicked in Explorer), if there is one. */
    inline File projectFromCommandLine (const StringArray& args)
    {
        for (auto& a : args)
        {
            if (a.startsWith ("--")) continue;
            const File f (File::getCurrentWorkingDirectory().getChildFile (a.unquoted()));
            if (f.existsAsFile() && f.hasFileExtension ("tracktionedit")) return f;
        }
        return {};
    }
}
