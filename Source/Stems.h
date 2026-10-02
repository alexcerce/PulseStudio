#pragma once

#include "Common.h"
#include "ClipImport.h"
#include "EditingOps.h"

/** Stem separation with Demucs (Meta's open-source model), run as an external Python process.
    Pulse keeps its own Python environment in Documents/Pulse Studio/Tools/stems-env so the
    system Python is left alone. */
namespace Stems
{
    inline File toolsFolder()  { return File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio").getChildFile ("Tools"); }
    inline File envFolder()    { return toolsFolder().getChildFile ("stems-env"); }
    inline File envPython()    { return envFolder().getChildFile ("Scripts").getChildFile ("python.exe"); }

    inline bool isInstalled()
    {
        return envPython().existsAsFile()
            && envFolder().getChildFile ("Lib").getChildFile ("site-packages").getChildFile ("demucs").isDirectory();
    }

    inline const StringArray& stemNames() { static const StringArray s { "drums", "bass", "other", "vocals" }; return s; }

    /** Runs a command, streaming its output to `onLine`. Returns the exit code (-1 = couldn't start / cancelled). */
    inline int run (const StringArray& cmd, std::function<bool (const String& chunk)> onOutput, int timeoutMs = 3600000)
    {
        ChildProcess p;
        if (! p.start (cmd, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
            return -1;

        const auto start = Time::getMillisecondCounter();
        char buf[4096];
        for (;;)
        {
            const int got = p.readProcessOutput (buf, sizeof (buf) - 1);
            if (got > 0)
            {
                buf[got] = 0;
                if (onOutput && ! onOutput (String::fromUTF8 (buf, got))) { p.kill(); return -1; }
                continue;
            }
            if (! p.isRunning()) break;
            if (Time::getMillisecondCounter() - start > (uint32) timeoutMs) { p.kill(); return -1; }
            Thread::sleep (50);
        }
        return (int) p.getExitCode();
    }

    /** A system Python 3.9-3.12 to build the environment with (py launcher first). */
    inline StringArray findSystemPython()
    {
        for (auto cmd : { StringArray { "py", "-3.12" }, StringArray { "py", "-3.11" }, StringArray { "py", "-3.10" }, StringArray { "python" } })
        {
            String out;
            auto c = cmd; c.add ("--version");
            if (run (c, [&out] (const String& s) { out << s; return true; }, 20000) == 0 && out.contains ("Python 3."))
            {
                const int minor = out.fromFirstOccurrenceOf ("Python 3.", false, false).getIntValue();
                if (minor >= 9 && minor <= 12)
                    return cmd;
            }
        }
        return {};
    }

    //==============================================================================
    /** Creates the environment and installs PyTorch (CPU) + Demucs. ~1.5 GB download. */
    class SetupJob : public ThreadWithProgressWindow
    {
    public:
        SetupJob() : ThreadWithProgressWindow ("Setting up stem separation", true, true) {}

        void run() override
        {
            auto python = findSystemPython();
            if (python.isEmpty()) { error = "Python 3.9 - 3.12 wasn't found. Install it from python.org, then try again."; return; }

            toolsFolder().createDirectory();
            auto step = [this] (const String& what, StringArray cmd, double p0, double p1) -> bool
            {
                setStatusMessage (what);
                setProgress (p0);
                log << "> " << cmd.joinIntoString (" ") << "\n";
                const int code = Stems::run (cmd, [this, p0, p1] (const String& s)
                {
                    log << s;
                    setProgress (jmin (p1, getProgressGuess() + 0.002));
                    return ! threadShouldExit();
                });
                if (code != 0 && error.isEmpty())
                    error = threadShouldExit() ? String ("Cancelled.") : what + " failed (code " + String (code) + ").";
                return code == 0;
            };

            auto py = envPython().getFullPathName();
            auto cmd = python; cmd.addArray ({ "-m", "venv", envFolder().getFullPathName() });
            if (! step ("Creating the Python environment...", cmd, 0.0, 0.05)) return;
            if (! step ("Updating pip...", { py, "-m", "pip", "install", "--upgrade", "pip" }, 0.05, 0.1)) return;
            if (! step ("Installing PyTorch (CPU, ~200 MB)...", { py, "-m", "pip", "install", "torch==2.5.1", "torchaudio==2.5.1",
                                                                  "--index-url", "https://download.pytorch.org/whl/cpu" }, 0.1, 0.7)) return;
            step ("Installing Demucs...", { py, "-m", "pip", "install", "demucs==4.0.1", "soundfile" }, 0.7, 1.0);
        }

        double getProgressGuess() const { return progressGuess; }
        void setProgress (double p) { progressGuess = p; ThreadWithProgressWindow::setProgress (p); }

        String error, log;

    private:
        double progressGuess = 0.0;
    };

    //==============================================================================
    /** Runs Demucs on a file. Stems end up in outDir/htdemucs/<stem>.wav.
        `progress` gets 0..1 and returns false to cancel. */
    inline Result separate (const File& input, const File& outDir, std::function<bool (float)> progress, String* logOut = nullptr)
    {
        if (! isInstalled())
            return Result::fail ("Stem separation isn't set up yet.");

        outDir.createDirectory();

        // Demucs 4.0.1 crashes on mono files: hand it a stereo copy
        File source = input;
        {
            AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (input));
            if (r == nullptr)
                return Result::fail ("Couldn't read " + input.getFileName());
            if (r->numChannels == 1)
            {
                source = outDir.getChildFile ("_stereo_input.wav");
                source.deleteFile();
                WavAudioFormat wav;
                std::unique_ptr<FileOutputStream> out (source.createOutputStream());
                std::unique_ptr<AudioFormatWriter> w (out != nullptr ? wav.createWriterFor (out.get(), r->sampleRate, 2, 24, {}, 0) : nullptr);
                if (w == nullptr)
                    return Result::fail ("Couldn't write a temporary file in " + outDir.getFullPathName());
                out.release();
                AudioBuffer<float> buf (2, 65536);
                for (int64 pos = 0; pos < r->lengthInSamples; pos += buf.getNumSamples())
                {
                    const int n = (int) jmin<int64> (buf.getNumSamples(), r->lengthInSamples - pos);
                    r->read (&buf, 0, n, pos, true, false);
                    buf.copyFrom (1, 0, buf, 0, 0, n);
                    w->writeFromAudioSampleBuffer (buf, 0, n);
                }
            }
        }

        StringArray cmd { envPython().getFullPathName(), "-m", "demucs", "-n", "htdemucs", "-d", "cpu", "--int24",
                          "--filename", "{stem}.{ext}", "-o", outDir.getFullPathName(), source.getFullPathName() };
        String log;
        const int code = run (cmd, [&] (const String& s)
        {
            log << s;
            // tqdm prints " 42%|####   |" - take the last percentage we've seen
            auto lastPct = s.lastIndexOfChar ('%');
            if (lastPct > 0 && progress)
            {
                int i = lastPct;
                while (i > 0 && CharacterFunctions::isDigit (s[i - 1])) --i;
                if (i < lastPct)
                    return progress (s.substring (i, lastPct).getIntValue() / 100.0f);
            }
            return progress ? progress (-1.0f) : true;
        });

        if (logOut != nullptr) *logOut = log;
        if (code != 0)
            return Result::fail (code < 0 ? "Cancelled." : "Demucs failed:\n" + log.getLastCharacters (600));

        for (auto& s : stemNames())
            if (! outDir.getChildFile ("htdemucs").getChildFile (s + ".wav").existsAsFile())
                return Result::fail ("Demucs didn't write the " + s + " stem.");
        return Result::ok();
    }

    /** Folder for a clip's stems (next to the project's bounces). */
    inline File stemsFolderFor (te::WaveAudioClip& clip)
    {
        return EditingOps::bounceFolder (clip.edit).getChildFile ("Stems")
                   .getNonexistentChildFile (File::createLegalFileName (clip.getName()), {}, false);
    }

    /** Puts four stem tracks (in a group) under the clip's track, lined up with the clip,
        and mutes the original clip. */
    inline te::FolderTrack* importStems (te::WaveAudioClip& clip, const File& stemDir)
    {
        auto& edit = clip.edit;
        auto folder = edit.insertNewFolderTrack (te::TrackInsertPoint (*clip.getTrack(), false), nullptr, true);
        if (folder == nullptr) return nullptr;
        folder->setName (clip.getName() + " stems");

        const Colour cols[] = { Colour (0xffe0604a), Colour (0xffa77be8), Colour (0xff4fa8e0), Colour (0xff5fcf7a) };
        te::Track* after = nullptr;
        for (int i = 0; i < stemNames().size(); ++i)
        {
            auto f = stemDir.getChildFile (stemNames()[i] + ".wav");
            auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (folder.get(), after), nullptr);
            if (t == nullptr) continue;
            after = t.get();
            t->setName (stemNames()[i].substring (0, 1).toUpperCase() + stemNames()[i].substring (1));
            t->setColour (cols[i]);
            if (auto c = te::insertWaveClip (*t, t->getName(), f, { clip.getEditTimeRange(), clip.getPosition().getOffset() }, te::DeleteExistingClips::no))
            {
                c->setGainDB (clip.getGainDB());
                c->setFadeIn (clip.getFadeIn());
                c->setFadeOut (clip.getFadeOut());
            }
        }
        clip.setMuted (true);
        return folder.get();
    }

    //==============================================================================
    class SeparateJob : public ThreadWithProgressWindow
    {
    public:
        SeparateJob (File in, File out) : ThreadWithProgressWindow ("Separating stems (Demucs)", true, true), input (in), output (out) {}

        void run() override
        {
            setStatusMessage ("Loading the model (the first run downloads it, ~80 MB)...");
            result = separate (input, output, [this] (float p)
            {
                if (p >= 0.0f) { setProgress (p); setStatusMessage ("Separating drums, bass, vocals and other... " + String (roundToInt (p * 100)) + "%"); }
                return ! threadShouldExit();
            });
        }

        File input, output;
        Result result = Result::fail ("Not run");
    };

    /** The whole flow from the UI: set up if needed, separate, import. */
    inline void separateClipInteractive (te::WaveAudioClip& clip)
    {
        if (! isInstalled())
        {
            const bool go = AlertWindow::showOkCancelBox (MessageBoxIconType::QuestionIcon, "Stem separation",
                "Stem separation uses Demucs, an open-source AI model, which needs a one-time setup:\n"
                "a private Python environment with PyTorch (CPU) and Demucs, about 1.5 GB, in\n"
                + envFolder().getFullPathName() + "\n\nSet it up now?", "Set up", "Cancel");
            if (! go) return;

            SetupJob setup;
            setup.runThread();
            if (! isInstalled())
            {
                AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Stem separation",
                                                  (setup.error.isNotEmpty() ? setup.error : String ("Setup didn't finish.")) + "\n\n" + setup.log.getLastCharacters (500));
                return;
            }
        }

        te::Clip::Ptr hold (&clip);
        auto outDir = stemsFolderFor (clip);
        SeparateJob job (clip.getOriginalFile(), outDir);
        job.runThread();

        auto* wac = dynamic_cast<te::WaveAudioClip*> (hold.get());
        if (! job.result.wasOk() || wac == nullptr || wac->getTrack() == nullptr)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Stem separation", job.result.getErrorMessage());
            return;
        }

        wac->edit.getUndoManager().beginNewTransaction ("Separate stems");
        importStems (*wac, outDir.getChildFile ("htdemucs"));
    }
}
