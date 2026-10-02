#pragma once

#include "Common.h"
#include "Routing.h"
#include "EditingOps.h"
#include "BackgroundRender.h"
#include "ExportTools.h"

/** Export settings: format, bit depth, sample rate, normalisation, range and stems. */
class ExportDialog : public Component
{
public:
    explicit ExportDialog (te::Edit& e, std::optional<te::TimeRange> sel = {}) : edit (e), selection (sel)
    {
        auto addRow = [this] (Label& l, ComboBox& c, const String& name, StringArray items, int sel)
        {
            l.setText (name, dontSendNotification);
            l.setColour (Label::textColourId, Theme::muted);
            c.addItemList (items, 1);
            c.setSelectedId (sel, dontSendNotification);
            addAndMakeVisible (l);
            addAndMakeVisible (c);
        };

        addRow (formatL, format, "Format", { "WAV", "AIFF", "FLAC", "OGG Vorbis", "MP3 320 kbps", "MP3 192 kbps" }, 1);
        addRow (depthL, depth, "Bit depth", { "16-bit", "24-bit", "32-bit float" }, 2);
        addRow (rateL, rate, "Sample rate", { "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
        addRow (normL, normalise, "Normalise", { "Off", "Peak to -0.3 dB", "Loudness -14 LUFS (streaming)", "Loudness -16 LUFS (Apple / podcasts)", "Loudness -9 LUFS (loud)",
                                                  "Streaming ready: -14 LUFS, true peak under -1 dBTP" }, 1);
        addRow (rangeL, range, "Range", { "Whole song", "Loop range" }, 1);
        if (selection.has_value() && ! selection->isEmpty())
        {
            range.addItem ("Selected clips (" + EngineHelpers::formatTime (te::toPosition (selection->getLength())) + ")", 3);
            range.setSelectedId (3, dontSendNotification);
        }

        stems.setButtonText ("Export stems (one file per track and return)");
        masterFx.setButtonText ("Include master bus effects");
        masterFx.setToggleState (true, dontSendNotification);
        addAndMakeVisible (stems);
        addAndMakeVisible (masterFx);

        format.onChange = [this]
        {
            depth.setEnabled (format.getSelectedId() <= 3);
            if (isMp3() && rate.getSelectedId() > 2) rate.setSelectedId (1);   // MP3 is 44.1 or 48 kHz
        };

        exportBtn.setColour (TextButton::buttonColourId, Colour (0xff1c3a55));
        exportBtn.onClick = [this] { chooseDestination(); };
        cancelBtn.onClick = [this] { close(); };
        addAndMakeVisible (exportBtn);
        addAndMakeVisible (cancelBtn);
        addAndMakeVisible (status);
        status.setColour (Label::textColourId, Theme::muted);

        setSize (440, 330);
    }

    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        auto row = [&r] (Label& l, ComboBox& c)
        {
            auto a = r.removeFromTop (28);
            l.setBounds (a.removeFromLeft (110));
            c.setBounds (a);
            r.removeFromTop (8);
        };
        row (formatL, format);
        row (depthL, depth);
        row (rateL, rate);
        row (normL, normalise);
        row (rangeL, range);
        stems.setBounds (r.removeFromTop (24));
        masterFx.setBounds (r.removeFromTop (24));
        auto buttons = r.removeFromBottom (30);
        cancelBtn.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        exportBtn.setBounds (buttons.removeFromRight (110));
        status.setBounds (buttons);
    }

private:
    te::Edit& edit;
    std::optional<te::TimeRange> selection;
    Label formatL, depthL, rateL, normL, rangeL, status;
    ComboBox format, depth, rate, normalise, range;
    ToggleButton stems, masterFx;
    TextButton exportBtn { "Export..." }, cancelBtn { "Cancel" };
    std::shared_ptr<FileChooser> chooser;

    void close()
    {
        if (auto* dw = findParentComponentOfClass<DialogWindow>())
            dw->exitModalState (0);
    }

    String extension() const
    {
        const char* ext[] = { ".wav", ".aif", ".flac", ".ogg", ".mp3", ".mp3" };
        return ext[format.getSelectedId() - 1];
    }

    AudioFormat* audioFormat() const
    {
        auto& fm = edit.engine.getAudioFileFormatManager();
        switch (format.getSelectedId())
        {
            case 2:  return fm.getAiffFormat();
            case 3:  return fm.getFlacFormat();
            case 4:  return fm.getOggFormat();
            case 5:
            case 6:  return fm.getWavFormat();   // rendered to WAV, then encoded
            default: return fm.getWavFormat();
        }
    }

    te::Renderer::Parameters baseParams() const
    {
        te::Renderer::Parameters p (edit);
        p.audioFormat = audioFormat();
        const int depths[] = { 16, 24, 32 };
        p.bitDepth = format.getSelectedId() == 3 ? jmin (24, depths[depth.getSelectedId() - 1]) : depths[depth.getSelectedId() - 1];
        if (isMp3()) p.bitDepth = 16;
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
        p.sampleRateForAudio = rates[rate.getSelectedId() - 1];
        p.quality = format.getSelectedId() == 4 ? 8 : 0;
        p.ditheringEnabled = p.bitDepth < 32;
        p.useMasterPlugins = masterFx.getToggleState();
        p.canRenderInMono = false;
        p.checkNodesForAudio = false;

        switch (normalise.getSelectedId())
        {
            case 2: p.shouldNormalise = true; p.normaliseToLevelDb = -0.3f; break;
            case 3: p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -14.0f; break;
            case 4: p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -16.0f; break;
            case 5: p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -9.0f; break;
            case 6: p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -14.0f; break;   // + a true-peak pass afterwards
            default: break;
        }

        if (range.getSelectedId() == 3 && selection.has_value())
        {
            p.time = *selection;
        }
        else if (range.getSelectedId() == 2 && ! edit.getTransport().getLoopRange().isEmpty())
        {
            p.time = edit.getTransport().getLoopRange();
        }
        else
        {
            p.time = { te::TimePosition(), te::TimePosition() + edit.getLength() };
            p.endAllowance = te::TimeDuration::fromSeconds (6.0);   // reverb/delay tails, stops at silence
        }
        return p;
    }

    void chooseDestination()
    {
        auto base = edit.editFileRetriever();
        if (stems.getToggleState())
        {
            chooser = std::make_shared<FileChooser> ("Choose a folder for the stems", base.getParentDirectory());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                  [this, sp = SafePointer<ExportDialog> (this)] (const FileChooser& fc)
                                  {
                                      if (sp != nullptr && fc.getResult().isDirectory()) exportStems (fc.getResult());
                                  });
        }
        else
        {
            chooser = std::make_shared<FileChooser> ("Export mixdown", base.withFileExtension (extension()), "*" + extension());
            chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                                  [this, sp = SafePointer<ExportDialog> (this)] (const FileChooser& fc)
                                  {
                                      if (sp != nullptr && fc.getResult() != File()) exportMix (fc.getResult().withFileExtension (extension()));
                                  });
        }
    }

    bool isMp3() const { return format.getSelectedId() >= 5; }
    int mp3Kbps() const { return format.getSelectedId() == 5 ? 320 : 192; }
    bool truePeakPass() const { return normalise.getSelectedId() == 6; }

    /** After rendering: the true-peak pass and the MP3 encoding. Returns an error or "". */
    static String finishFile (te::Engine& eng, const File& rendered, const File& dest, bool limit, int mp3Kbps)
    {
        if (limit) ExportTools::limitTruePeak (eng, rendered, -1.0f);
        if (mp3Kbps > 0)
        {
            const auto err = Mp3EncoderWin::encode (rendered, dest, mp3Kbps);
            rendered.deleteFile();
            return err;
        }
        return {};
    }

    void exportMix (const File& f)
    {
        edit.getTransport().stop (false, false);
        EditingOps::waitForClipProxies (edit);
        auto p = baseParams();
        const auto rendered = isMp3() ? f.getSiblingFile (f.getFileNameWithoutExtension() + " (render).wav") : f;
        p.destFile = rendered;
        BackgroundRender::Job job;
        job.name = "Exporting " + f.getFileName();
        job.renders.push_back (p);
        const bool limit = truePeakPass();
        const int kbps = isMp3() ? mp3Kbps() : 0;
        job.onDone = [f, rendered, limit, kbps, &eng = edit.engine] (int ok, int, bool cancelled)
        {
            if (cancelled) return;
            const auto measureFirst = kbps > 0 && ok > 0;   // measure the WAV before it becomes MP3
            pulse::LoudnessAnalyser::Result before;
            String err;
            if (ok > 0)
            {
                if (limit) ExportTools::limitTruePeak (eng, rendered, -1.0f);
                if (measureFirst) before = pulse::LoudnessAnalyser::measureFile (eng, rendered);
                if (kbps > 0) err = finishFile (eng, rendered, f, false, kbps);
            }
            if (err.isNotEmpty()) ok = 0;
            String msg = ok > 0 ? "Exported " + f.getFileName() : "Export failed." + (err.isNotEmpty() ? "\n" + err : String());
            if (ok > 0)
            {
                const auto m = measureFirst ? before : pulse::LoudnessAnalyser::measureFile (eng, f);
                msg << "\n\nLoudness " << String (m.integrated, 1) << " LUFS, true peak " << String (m.truePeakDb, 1) << " dBTP";
                if (m.truePeakDb > -1.0f) msg << "\nThe true peak is above -1 dBTP: streaming services may add distortion when they encode it. Try the master limiter.";
            }
            AlertWindow::showMessageBoxAsync (ok > 0 ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export", msg);
            if (ok > 0) f.revealToUser();
        };
        startJob (std::move (job));
    }

    void startJob (BackgroundRender::Job job)
    {
        if (! BackgroundRender::instance().start (std::move (job)))
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Export", "Another export is still running.");
            return;
        }
        close();   // carries on in the background (progress in the transport bar)
    }

    void exportStems (const File& folder)
    {
        edit.getTransport().stop (false, false);
        EditingOps::waitForClipProxies (edit);
        int ok = 0, total = 0;

        auto tracks = EngineHelpers::getUserTracks (edit);
        tracks.addArray (Routing::getReturnTracks (edit));
        ignoreUnused (ok);

        BackgroundRender::Job job;
        job.name = "Exporting stems";
        for (auto t : tracks)
        {
            ++total;
            auto p = baseParams();
            p.useMasterPlugins = false;
            p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { t });
            p.destFile = folder.getChildFile (File::createLegalFileName (String (total).paddedLeft ('0', 2) + " " + t->getName()) + (isMp3() ? String (".wav") : extension()));
            job.renders.push_back (p);
        }
        const bool limit = truePeakPass();
        const int kbps = isMp3() ? mp3Kbps() : 0;
        std::vector<std::pair<File, File>> files;
        for (auto& r : job.renders) files.push_back ({ r.destFile, r.destFile.withFileExtension (".mp3") });
        job.onDone = [folder, files, limit, kbps, &eng = edit.engine] (int done, int count, bool cancelled)
        {
            if (cancelled) return;
            for (auto& [wav, mp3] : files)
                if (wav.existsAsFile()) finishFile (eng, wav, mp3, limit, kbps);
            AlertWindow::showMessageBoxAsync (done == count ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export",
                                              String (done) + " of " + String (count) + " stems exported to " + folder.getFileName());
            if (done > 0) folder.revealToUser();
        };
        startJob (std::move (job));
    }

    void finish (bool ok, const String& msg)
    {
        AlertWindow::showMessageBoxAsync (ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export", msg);
        close();
    }
};

/** Opens the export dialog. A selection range adds a "Selected clips" option (and picks it). */
inline void showExportDialog (te::Edit& edit, std::optional<te::TimeRange> selection = {})
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Export";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.content.setOwned (new ExportDialog (edit, selection));
    o.launchAsync();
}
