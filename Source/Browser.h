#pragma once

#include "EditViewState.h"
#include "ClipImport.h"
#include "PluginMenu.h"
#include "Presets.h"
#include "Workflow.h"

//==============================================================================
/** Plays audio files from the browser through the current output device.
    With sync on and the transport running, short loops are time-stretched to the
    project tempo and start on the next bar. */
class AudioPreviewer
{
public:
    AudioPreviewer (te::Engine& e, TimeSliceThread& t) : engine (e), thread (t)
    {
        player.setSource (&mixer);
        mixer.addInputSource (&transport, false);
        mixer.addInputSource (&synced, false);
        engine.getDeviceManager().deviceManager.addAudioCallback (&player);
    }

    ~AudioPreviewer()
    {
        ++*genPtr;
        engine.getDeviceManager().deviceManager.removeAudioCallback (&player);
        stop();
        mixer.removeAllInputs();
        player.setSource (nullptr);
    }

    void play (const File& f, te::Edit* syncTo)
    {
        stop();
        const int gen = ++*genPtr;

        if (syncTo != nullptr && syncTo->getTransport().isPlaying() && playSynced (f, *syncTo, gen))
            return;

        auto* reader = engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f);
        if (reader == nullptr)
            return;

        const auto rate = reader->sampleRate;
        source = std::make_unique<AudioFormatReaderSource> (reader, true);
        transport.setSource (source.get(), 32768, &thread, rate);
        transport.setGain (0.8f);
        transport.start();
    }

    void stop()
    {
        ++*genPtr;
        transport.stop();
        transport.setSource (nullptr);
        source.reset();
        synced.clear();
    }

    /** Tempo of a loop: file tag, "120bpm" in the name, or a whole number of bars at the project tempo. */
    static double estimateBpm (te::Engine& engine, const File& f, double lengthSecs, double projectBpm, int beatsPerBar)
    {
        te::AudioFile af (engine, f);
        auto info = af.getInfo();
        if (auto b = info.loopInfo.getBpm (info); b > 40.0 && b < 300.0)
            return b;

        auto name = f.getFileNameWithoutExtension().toLowerCase();
        for (int i = name.indexOf ("bpm"); i >= 0; i = name.indexOf (i + 1, "bpm"))
        {
            int s = i;
            while (s > 0 && (name[s - 1] == ' ' || name[s - 1] == '_' || name[s - 1] == '-')) --s;
            int e = s;
            while (s > 0 && CharacterFunctions::isDigit (name[s - 1])) --s;
            if (auto v = name.substring (s, e).getIntValue(); v >= 50 && v <= 250)
                return v;
        }

        // assume a whole power-of-two number of bars, pick the one closest to the project tempo
        double best = 0.0;
        for (int bars : { 1, 2, 4, 8, 16 })
        {
            auto bpm = bars * beatsPerBar * 60.0 / lengthSecs;
            if (bpm >= 60.0 && bpm <= 200.0 && (best == 0.0 || std::abs (bpm - projectBpm) < std::abs (best - projectBpm)))
                best = bpm;
        }
        return best;
    }

private:
    /** Looping buffer source that waits a number of samples before starting (sample-accurate bar sync). */
    struct SyncedLoopSource : public AudioSource
    {
        void prepareToPlay (int, double) override {}
        void releaseResources() override {}

        void set (AudioBuffer<float>&& b, int64 waitSamples)
        {
            const SpinLock::ScopedLockType sl (lock);
            buffer = std::move (b);
            wait = waitSamples;
            pos = 0;
            active = buffer.getNumSamples() > 0;
        }

        void clear() { const SpinLock::ScopedLockType sl (lock); active = false; }

        void getNextAudioBlock (const AudioSourceChannelInfo& info) override
        {
            info.clearActiveBufferRegion();
            const SpinLock::ScopedLockType sl (lock);
            if (! active) return;

            int start = info.startSample, num = info.numSamples;
            if (wait > 0)
            {
                auto skip = (int) jmin<int64> (wait, num);
                wait -= skip; start += skip; num -= skip;
            }

            const int len = buffer.getNumSamples();
            while (num > 0)
            {
                const int n = jmin (num, len - pos);
                for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                    info.buffer->addFrom (ch, start, buffer, jmin (ch, buffer.getNumChannels() - 1), pos, n, 0.8f);
                pos = (pos + n) % len;
                start += n; num -= n;
            }
        }

        SpinLock lock;
        AudioBuffer<float> buffer;
        int64 wait = 0;
        int pos = 0;
        bool active = false;
    };

    /** Reads, stretches and resamples the loop on a background thread, then starts it on the next bar. */
    bool playSynced (const File& f, te::Edit& edit, int gen)
    {
        std::unique_ptr<AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (reader == nullptr) return false;

        const double fileRate = reader->sampleRate;
        const double lengthSecs = (double) reader->lengthInSamples / fileRate;
        if (lengthSecs > 40.0) return false;   // only loops get synced

        const double projectBpm = edit.tempoSequence.getTempo (0)->getBpm();
        const int bpb = EngineHelpers::beatsPerBar (edit);
        const double fileBpm = estimateBpm (engine, f, lengthSecs, projectBpm, bpb);
        if (fileBpm <= 0.0) return false;

        const double deviceRate = engine.getDeviceManager().getSampleRate();
        auto* raw = reader.release();

        Thread::launch ([this, g = genPtr, raw, gen, fileRate, fileBpm, projectBpm, deviceRate, &edit, bpb]
        {
            std::unique_ptr<AudioFormatReader> r (raw);
            const int numCh = jlimit (1, 2, (int) r->numChannels);
            AudioBuffer<float> src (numCh, (int) r->lengthInSamples);
            r->read (&src, 0, src.getNumSamples(), 0, true, numCh > 1);

            // 1. time-stretch to the project tempo (keeps pitch)
            const float ratio = (float) (fileBpm / projectBpm);
            te::TimeStretcher ts;
            ts.initialise (fileRate, 512, numCh, te::TimeStretcher::defaultMode, {}, false);
            ts.setSpeedAndPitch (ratio, 0.0f);

            AudioBuffer<float> stretched (numCh, (int) (src.getNumSamples() * ratio) + 8192);
            AudioBuffer<float> block (numCh, 512);
            int inPos = 0, outPos = 0;
            auto append = [&] (int got)
            {
                got = jmin (got, stretched.getNumSamples() - outPos);
                for (int ch = 0; ch < numCh; ++ch) stretched.copyFrom (ch, outPos, block, ch, 0, got);
                outPos += got;
            };

            while (inPos < src.getNumSamples() && *g == gen)
            {
                const int need = jmax (1, ts.getFramesNeeded());
                AudioBuffer<float> in (numCh, need);
                in.clear();
                const int n = jmin (need, src.getNumSamples() - inPos);
                for (int ch = 0; ch < numCh; ++ch) in.copyFrom (ch, 0, src, ch, inPos, n);
                inPos += n;
                append (ts.processData (in.getArrayOfReadPointers(), need, block.getArrayOfWritePointers()));
            }
            for (int got; (got = ts.flush (block.getArrayOfWritePointers())) > 0 && *g == gen;)
                append (got);

            // 2. trim to an exact number of beats, 3. resample to the device rate
            const double loopSecs = std::round (src.getNumSamples() / fileRate * fileBpm / 60.0) * 60.0 / projectBpm;
            const int loopLenFile = jlimit (1, outPos, (int) std::round (loopSecs * fileRate));
            const int loopLenDev = (int) std::round (loopSecs * deviceRate);
            AudioBuffer<float> loop (numCh, loopLenDev);
            for (int ch = 0; ch < numCh; ++ch)
            {
                LagrangeInterpolator interp;
                interp.process (fileRate / deviceRate, stretched.getReadPointer (ch), loop.getWritePointer (ch), loopLenDev,
                                loopLenFile, 0);
            }

            MessageManager::callAsync ([this, g, gen, l = std::move (loop), &edit, deviceRate, bpb] () mutable
            {
                if (*g != gen) return;   // stale (or the previewer is gone)
                // samples until the next bar line of the running transport
                auto& tc = edit.getTransport();
                auto& ts2 = edit.tempoSequence;
                auto pos = tc.getPosition();
                auto bb = ts2.toBarsAndBeats (pos);
                auto nextBar = ts2.toTime (te::tempo::BarsAndBeats { bb.bars + 1 });
                auto wait = (int64) ((nextBar - pos).inSeconds() * deviceRate);
                ignoreUnused (bpb);
                synced.set (std::move (l), jmax<int64> (0, wait));
            });
        });
        return true;
    }

    te::Engine& engine;
    TimeSliceThread& thread;
    AudioSourcePlayer player;
    MixerAudioSource mixer;
    AudioTransportSource transport;
    SyncedLoopSource synced;
    std::unique_ptr<AudioFormatReaderSource> source;
    std::shared_ptr<std::atomic<int>> genPtr = std::make_shared<std::atomic<int>> (0);
};

//==============================================================================
/** Draggable list of built-in devices and scanned VST3 plugins. */
class PluginListBox : public Component,
                      private ListBoxModel,
                      private ChangeListener,
                      private Timer
{
public:
    PluginListBox (EditViewState& e) : evs (e)
    {
        list.setModel (this);
        list.setRowHeight (22);
        list.setColour (ListBox::backgroundColourId, Theme::panel);
        addAndMakeVisible (list);
        evs.edit.engine.getPluginManager().knownPluginList.addChangeListener (this);
        refresh();
        startTimer (1500);   // picks up newly saved presets
    }

    ~PluginListBox() override { evs.edit.engine.getPluginManager().knownPluginList.removeChangeListener (this); }

    void resized() override { list.setBounds (getLocalBounds()); }

    void refresh()
    {
        rows.clear();
        rows.push_back ({ "BUILT-IN INSTRUMENTS", {}, true });
        for (auto& b : ClipImport::builtIns()) if (b.synth)   rows.push_back ({ b.name, String ("plugin:builtin:") + b.type, false });
        rows.push_back ({ "BUILT-IN EFFECTS", {}, true });
        for (auto& b : ClipImport::builtIns()) if (! b.synth && ! b.midiFx) rows.push_back ({ b.name, String ("plugin:builtin:") + b.type, false });
        rows.push_back ({ "MIDI EFFECTS", {}, true });
        for (auto& b : ClipImport::builtIns()) if (b.midiFx) rows.push_back ({ b.name, String ("plugin:builtin:") + b.type, false });

        auto types = evs.edit.engine.getPluginManager().knownPluginList.getTypes();
        std::sort (types.begin(), types.end(), [] (auto& a, auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });

        rows.push_back ({ "TRACK PRESETS", {}, true });
        auto trackPresets = Workflow::trackPresets();
        for (auto& f : trackPresets)
            rows.push_back ({ f.getFileNameWithoutExtension(), "plugin:track:" + f.getFullPathName(), false });
        if (trackPresets.isEmpty())
            rows.push_back ({ "(right-click a track header > Save track preset)", {}, false });

        rows.push_back ({ "CHAIN PRESETS", {}, true });
        auto chains = Presets::chainPresets();
        for (auto& f : chains)
            rows.push_back ({ f.getFileNameWithoutExtension(), "plugin:chain:" + f.getFullPathName(), false });
        if (chains.isEmpty())
            rows.push_back ({ "(save one from the device chain header)", {}, false });

        for (bool inst : { true, false })
        {
            rows.push_back ({ inst ? "VST3 INSTRUMENTS" : "VST3 EFFECTS", {}, true });
            bool any = false;
            for (auto& d : types)
                if (d.isInstrument == inst)
                {
                    rows.push_back ({ d.name + "  -  " + d.manufacturerName, "plugin:ext:" + d.createIdentifierString(), false });
                    any = true;
                }
            if (! any)
                rows.push_back ({ "(none - use Plugins... to scan)", {}, false });
        }

        presetSignature = currentPresetSignature();
        list.updateContent();
        list.repaint();
    }

private:
    struct Row { String name, id; bool header; };

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) rows.size()) return;
        auto& r = rows[(size_t) row];

        if (r.header)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.5f, Font::bold));
            g.drawText (r.name, 8, 0, w - 8, h, Justification::bottomLeft);
            return;
        }

        if (selected) g.fillAll (Theme::accent.withAlpha (0.25f));
        g.setColour (r.id.isEmpty() ? Theme::muted : Theme::text);
        g.setFont (FontOptions (13.0f));
        g.drawText (r.name, 14, 0, w - 16, h, Justification::centredLeft, true);
    }

    var getDragSourceDescription (const SparseSet<int>& selected) override
    {
        if (selected.size() == 1)
            if (auto row = selected[0]; row >= 0 && row < (int) rows.size())
                return rows[(size_t) row].id.isNotEmpty() ? var (rows[(size_t) row].id) : var();
        return {};
    }

    // Double-click adds the device to the selected track.
    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].id.isEmpty()) return;

        if (Workflow::isTrackPreset (rows[(size_t) row].id))
        {
            evs.edit.getUndoManager().beginNewTransaction ("Load track preset");
            if (auto t = Workflow::loadTrackPreset (evs.edit, Workflow::trackPresetFile (rows[(size_t) row].id)))
                evs.selectionManager.selectOnly (t);
            return;
        }

        if (auto t = EngineHelpers::getSelectedTrack (evs.selectionManager))
            Presets::applyDescription (t, t->pluginList, rows[(size_t) row].id);
    }

    void changeListenerCallback (ChangeBroadcaster*) override { refresh(); }

    static String currentPresetSignature()
    {
        String sig;
        for (auto& f : Workflow::trackPresets()) sig << f.getFileName() << ";";
        for (auto& f : Presets::chainPresets())  sig << f.getFileName() << ";";
        return sig;
    }

    void timerCallback() override
    {
        if (isShowing() && currentPresetSignature() != presetSignature)
            refresh();
    }

    String presetSignature;

    EditViewState& evs;
    ListBox list;
    std::vector<Row> rows;
};

//==============================================================================
/** Left-hand browser: file places + tree (audio/MIDI, with preview) and the device list. */
class Browser : public Component,
                private FileBrowserListener
{
public:
    Browser (EditViewState& e, PropertiesFile& props)
        : evs (e), settings (props),
          filter ("*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.mid;*.midi", "*", "Audio & MIDI"),
          contents (&filter, thread),
          tree (contents),
          previewer (e.edit.engine, thread),
          plugins (e)
    {
        thread.startThread (Thread::Priority::low);

        for (auto* b : { &filesTab, &devicesTab })
        {
            b->setRadioGroupId (2);
            b->setClickingTogglesState (true);
            b->setColour (TextButton::buttonOnColourId, Colour (0xff2b3a4d));
            addAndMakeVisible (b);
        }
        filesTab.onClick = [this] { showFiles (true); };
        devicesTab.onClick = [this] { showFiles (false); };

        places.onChange = [this] { placeChosen(); };
        addAndMakeVisible (places);

        preview.setToggleState (settings.getBoolValue ("browserPreview", true), dontSendNotification);
        preview.setTooltip ("Play audio files when you click them");
        preview.onClick = [this]
        {
            settings.setValue ("browserPreview", preview.getToggleState());
            if (! preview.getToggleState()) previewer.stop();
        };
        addAndMakeVisible (preview);

        syncBtn.setToggleState (settings.getBoolValue ("browserSync", true), dontSendNotification);
        syncBtn.setTooltip ("While the song plays, preview loops in time with it (stretched to the project tempo, starting on the next bar)");
        syncBtn.onClick = [this] { settings.setValue ("browserSync", syncBtn.getToggleState()); };
        addAndMakeVisible (syncBtn);

        tree.setDragAndDropDescription ("PulseFiles");
        tree.setColour (TreeView::backgroundColourId, Theme::panel);
        tree.setColour (FileTreeComponent::textColourId, Theme::text);
        tree.addListener (this);
        tree.setMultiSelectEnabled (true);
        addAndMakeVisible (tree);
        addChildComponent (plugins);

        rebuildPlaces();
        showFiles (true);
    }

    ~Browser() override
    {
        tree.removeListener (this);
        previewer.stop();
        thread.stopThread (2000);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::line);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.5f));
        if (filesTab.getToggleState())
            g.drawText ("Drag files onto a track, an empty area or a clip slot", 8, getHeight() - 20, getWidth() - 12, 16, Justification::centredLeft);
        else
            g.drawText ("Drag onto a track or double-click to add", 8, getHeight() - 20, getWidth() - 12, 16, Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedRight (1).reduced (6);
        auto tabs = r.removeFromTop (26);
        filesTab.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2).reduced (1, 0));
        devicesTab.setBounds (tabs.reduced (1, 0));
        r.removeFromTop (6);
        r.removeFromBottom (18);

        auto top = r.removeFromTop (26);
        syncBtn.setBounds (top.removeFromRight (56));
        preview.setBounds (top.removeFromRight (74));
        top.removeFromRight (4);
        places.setBounds (top);
        r.removeFromTop (4);
        tree.setBounds (r);
        plugins.setBounds (r.withTop (tabs.getBottom() + 6));
    }

private:
    void showFiles (bool files)
    {
        filesTab.setToggleState (files, dontSendNotification);
        devicesTab.setToggleState (! files, dontSendNotification);
        tree.setVisible (files);
        places.setVisible (files);
        preview.setVisible (files);
        syncBtn.setVisible (files);
        plugins.setVisible (! files);
        if (! files) previewer.stop();
        resized();
        repaint();
    }

    Array<File> placeFolders()
    {
        Array<File> folders {
            File::getSpecialLocation (File::userMusicDirectory),
            File::getSpecialLocation (File::userDesktopDirectory),
            File::getSpecialLocation (File::userDocumentsDirectory),
            File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Downloads"),
            File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Pulse Studio")
        };

        for (auto& p : StringArray::fromLines (settings.getValue ("browserFolders")))
            if (File (p).isDirectory())
                folders.addIfNotAlreadyThere (File (p));

        return folders;
    }

    void rebuildPlaces()
    {
        places.clear (dontSendNotification);
        int id = 1;
        for (auto& f : placeFolders())
            places.addItem (f.getFileName(), id++);

        places.addSeparator();
        places.addItem ("Add folder...", 1000);

        auto last = settings.getIntValue ("browserPlace", 1);
        places.setSelectedId (jlimit (1, id - 1, last), dontSendNotification);
        placeChosen();
    }

    void placeChosen()
    {
        if (places.getSelectedId() == 1000)
        {
            chooser = std::make_shared<FileChooser> ("Add a folder to the browser");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                  [this] (const FileChooser& fc)
                                  {
                                      auto d = fc.getResult();
                                      if (d.isDirectory())
                                      {
                                          auto list = StringArray::fromLines (settings.getValue ("browserFolders"));
                                          list.addIfNotAlreadyThere (d.getFullPathName());
                                          list.removeEmptyStrings();
                                          settings.setValue ("browserFolders", list.joinIntoString ("\n"));
                                          settings.setValue ("browserPlace", placeFolders().indexOf (d) + 1);
                                      }
                                      rebuildPlaces();
                                  });
            return;
        }

        auto folders = placeFolders();
        auto idx = places.getSelectedId() - 1;
        if (idx >= 0 && idx < folders.size())
        {
            settings.setValue ("browserPlace", places.getSelectedId());
            contents.setDirectory (folders[idx], true, true);
        }
    }

    // FileBrowserListener
    void selectionChanged() override
    {
        auto f = tree.getSelectedFile();
        if (preview.getToggleState() && f.existsAsFile() && ClipImport::isAudioFile (f))
            previewer.play (f, syncBtn.getToggleState() ? &evs.edit : nullptr);
        else
            previewer.stop();
    }

    void fileClicked (const File&, const MouseEvent&) override {}
    void fileDoubleClicked (const File&) override {}
    void browserRootChanged (const File&) override {}

    EditViewState& evs;
    PropertiesFile& settings;
    TimeSliceThread thread { "Browser" };
    WildcardFileFilter filter;
    DirectoryContentsList contents;
    FileTreeComponent tree;
    AudioPreviewer previewer;
    PluginListBox plugins;
    TextButton filesTab { "Files" }, devicesTab { "Devices" };
    ToggleButton preview { "Preview" }, syncBtn { "Sync" };
    ComboBox places;
    std::shared_ptr<FileChooser> chooser;
};
