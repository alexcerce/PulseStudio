#pragma once

#include "Common.h"
#include "Routing.h"

/** Export settings: format, bit depth, sample rate, normalisation, range and stems. */
class ExportDialog : public Component
{
public:
    explicit ExportDialog (te::Edit& e) : edit (e)
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

        addRow (formatL, format, "Format", { "WAV", "AIFF", "FLAC", "OGG Vorbis" }, 1);
        addRow (depthL, depth, "Bit depth", { "16-bit", "24-bit", "32-bit float" }, 2);
        addRow (rateL, rate, "Sample rate", { "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
        addRow (normL, normalise, "Normalise", { "Off", "Peak to -0.3 dB", "Loudness -14 LUFS (streaming)", "Loudness -9 LUFS (loud)" }, 1);
        addRow (rangeL, range, "Range", { "Whole song", "Loop range" }, 1);

        stems.setButtonText ("Export stems (one file per track and return)");
        masterFx.setButtonText ("Include master bus effects");
        masterFx.setToggleState (true, dontSendNotification);
        addAndMakeVisible (stems);
        addAndMakeVisible (masterFx);

        format.onChange = [this] { depth.setEnabled (format.getSelectedId() <= 3); };

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
        const char* ext[] = { ".wav", ".aif", ".flac", ".ogg" };
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
            default: return fm.getWavFormat();
        }
    }

    te::Renderer::Parameters baseParams() const
    {
        te::Renderer::Parameters p (edit);
        p.audioFormat = audioFormat();
        const int depths[] = { 16, 24, 32 };
        p.bitDepth = format.getSelectedId() == 3 ? jmin (24, depths[depth.getSelectedId() - 1]) : depths[depth.getSelectedId() - 1];
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
            case 4: p.shouldNormaliseByLUFS = true; p.normaliseToLevelDb = -9.0f; break;
            default: break;
        }

        if (range.getSelectedId() == 2 && ! edit.getTransport().getLoopRange().isEmpty())
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

    void exportMix (const File& f)
    {
        edit.getTransport().stop (false, false);
        auto p = baseParams();
        p.destFile = f;
        auto result = te::Renderer::renderToFile ("Exporting mixdown", p);
        finish (result.existsAsFile(), result.existsAsFile() ? "Exported " + f.getFileName() : "Export failed.");
    }

    void exportStems (const File& folder)
    {
        edit.getTransport().stop (false, false);
        int ok = 0, total = 0;

        auto tracks = EngineHelpers::getUserTracks (edit);
        tracks.addArray (Routing::getReturnTracks (edit));

        for (auto t : tracks)
        {
            ++total;
            auto p = baseParams();
            p.useMasterPlugins = false;
            p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { t });
            p.destFile = folder.getChildFile (File::createLegalFileName (String (total).paddedLeft ('0', 2) + " " + t->getName()) + extension());
            if (te::Renderer::renderToFile ("Exporting " + t->getName(), p).existsAsFile())
                ++ok;
        }

        finish (ok == total, String (ok) + " of " + String (total) + " stems exported to " + folder.getFileName());
        if (ok > 0) folder.revealToUser();
    }

    void finish (bool ok, const String& msg)
    {
        AlertWindow::showMessageBoxAsync (ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Export", msg);
        close();
    }
};

inline void showExportDialog (te::Edit& edit)
{
    DialogWindow::LaunchOptions o;
    o.dialogTitle = "Export";
    o.dialogBackgroundColour = Theme::panel;
    o.useNativeTitleBar = true;
    o.content.setOwned (new ExportDialog (edit));
    o.launchAsync();
}
