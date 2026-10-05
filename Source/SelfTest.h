#pragma once

#include "Common.h"
#include "Routing.h"
#include "ClipImport.h"
#include "EditingOps.h"
#include "DrumRack.h"
#include "Presets.h"
#include "Macros.h"
#include "Meters.h"
#include "ShareTools.h"
#include "FactoryContent.h"
#include "ClipGain.h"
#include "PushMap.h"
#include "AudioSettings.h"
#include "MidiTools.h"
#include "DragDrop.h"
#include "DeviceChain.h"
#include "PulseLookAndFeel.h"
#include "MidiLearn.h"
#include "SessionView.h"
#include "Browser.h"
#include "PluginWindow.h"
#include "StateControls.h"
#include "Workflow.h"
#include "PulseSynths.h"
#include "LinkSync.h"
#include "Push2Display.h"
#include "AudioToMidi.h"
#include "Stems.h"
#include "CrashGuard.h"
#include "Timeline.h"
#include "Scales.h"
#include "Sharing.h"
#include "Sandbox.h"
#include "BackgroundRender.h"
#include "Commands.h"
#include "Modulators.h"
#include "ClipEnvelopes.h"
#include "SampleEditor.h"
#include "PianoRoll.h"
#include "DeviceEditor.h"
#include "BusLanes.h"
#include "Upgrade.h"
#include "PrismEQDisplay.h"
#include "Mixer.h"
#include "UndoHistory.h"
#include "TransportBar.h"
#include "Sections.h"
#include "ExportTools.h"
#include "About.h"
#include "UndoSync.h"

/** "Pulse Studio.exe --selftest report.txt"
    Builds a scratch project in the temp folder, exercises the editing / routing / device
    features in code, renders audio to check the results, and writes a pass/fail report.
    Returns the number of failures. */
namespace SelfTest
{
    struct Report
    {
        StringArray lines;
        int failures = 0;
        File progress;   // written as we go, so a crash still shows how far it got

        void check (bool ok, const String& name, const String& detail = {})
        {
            lines.add (String (ok ? "PASS  " : "FAIL  ") + name + (detail.isNotEmpty() ? "  (" + detail + ")" : ""));
            if (! ok) ++failures;
            progress.appendText (lines[lines.size() - 1] + "\n");
        }

        void step (const String& s) { progress.appendText ("...   " + s + "\n"); }
    };

    inline File writeTestWav (const File& f, double seconds, double freq, double sr = 44100.0, bool decay = false)
    {
        f.deleteFile();
        WavAudioFormat wav;
        std::unique_ptr<FileOutputStream> out (f.createOutputStream());
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (out.get(), sr, 2, 24, {}, 0));
        if (w == nullptr) return {};
        out.release();
        AudioBuffer<float> b (2, (int) (seconds * sr));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            auto env = decay ? std::exp (-6.0 * i / sr) : 1.0;
            auto v = (float) (0.5 * env * std::sin (MathConstants<double>::twoPi * freq * i / sr));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        return f;
    }

    inline float peakDb (te::Engine& e, const File& f)
    {
        std::unique_ptr<AudioFormatReader> r (e.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
        if (r == nullptr) return -200.0f;
        Range<float> ranges[2];
        r->readMaxLevels (0, r->lengthInSamples, ranges, (int) jmin (2u, r->numChannels));
        float peak = 0.0f;
        for (auto& rg : ranges) peak = jmax (peak, std::abs (rg.getStart()), std::abs (rg.getEnd()));
        return Decibels::gainToDecibels (peak, -200.0f);
    }

    /** --device-shots <dir>: renders the full editors of the graph-first devices (with some audio
        through them so the displays move) to PNGs, for quick UI work. */
    inline int deviceShots (te::Engine& engine, const File& dir)
    {
        dir.createDirectory();
        auto edit = te::createEmptyEdit (engine, dir.getChildFile ("shots.tracktionedit"));
        edit->ensureNumberOfAudioTracks (1);
        auto* t = te::getAudioTracks (*edit).getFirst();
        te::SelectionManager sm (engine);
        EditViewState evs (*edit, sm);
        PulseLookAndFeel laf;
        LookAndFeel::setDefaultLookAndFeel (&laf);
        WavetableUI::installModDrop();
        const double sr = 48000.0;
        auto feed = [&] (te::Plugin& p, double seconds)
        {
            const int block = 480;
            p.initialise ({ te::TimePosition(), sr, block });
            AudioBuffer<float> audio (2, block);
            Random rng (7);
            const int blocks = (int) (seconds * sr / block);
            for (int k = 0; k < blocks; ++k)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                    {
                        const double tt = (k * block + i) / sr;
                        const float env = (float) std::exp (-std::fmod (tt, 0.5) * 6.0);   // a kick-like pulse every half second
                        const float x = 0.7f * env * (float) std::sin (MathConstants<double>::twoPi * (60.0 + 80.0 * env) * tt)
                                        + 0.12f * (rng.nextFloat() * 2.0f - 1.0f) + 0.15f * (float) std::sin (MathConstants<double>::twoPi * 440.0 * tt);
                        audio.setSample (c, i, x);
                    }
                te::MidiMessageArray midi;
                te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                            true, false, true, false);
                p.applyToBuffer (fc);
            }
        };
        int written = 0;
        auto shoot = [&] (const char* type, std::function<void (pulse::PulsePlugin&)> setup, const String& file)
        {
            auto pl = edit->getPluginCache().createNewPlugin (type, {});
            t->pluginList.insertPlugin (pl, 0, nullptr);
            auto* pp = dynamic_cast<pulse::PulsePlugin*> (pl.get());
            if (pp == nullptr) return;
            if (setup) setup (*pp);
            feed (*pl, 3.0);
            {
                DeviceEditors::Editor ed (evs, *pp);
                ed.setSize (jmax (1100, ed.getWidth()), jmax (760, ed.getHeight()));
                MessageManager::getInstance()->runDispatchLoopUntil (120);
                auto img = ed.createComponentSnapshot (ed.getLocalBounds());
                auto f = dir.getChildFile (file);
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; if (png.writeImageToStream (img, *os)) ++written; }
            }
            pl->deleteFromParent();
        };
        using EQ = pulse::PrismEQ;
        shoot (EQ::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            auto set = [&p] (int b, int w, float v) { p.set (EQ::idx (b, w), v); };
            set (0, EQ::pOn, 1); set (0, EQ::pType, EQ::lowCut); set (0, EQ::pFreq, 35); set (0, EQ::pSlope, 6);
            set (1, EQ::pOn, 1); set (1, EQ::pType, EQ::lowShelf); set (1, EQ::pFreq, 110); set (1, EQ::pGain, 3);
            set (2, EQ::pOn, 1); set (2, EQ::pFreq, 380); set (2, EQ::pGain, -4); set (2, EQ::pQ, 1.4f);
            set (3, EQ::pOn, 1); set (3, EQ::pFreq, 3200); set (3, EQ::pGain, 4); set (3, EQ::pDyn, 1); set (3, EQ::pRange, -6);
            set (5, EQ::pOn, 1); set (5, EQ::pType, EQ::highShelf); set (5, EQ::pFreq, 11000); set (5, EQ::pGain, 2.5f);
        }, "shot_prism.png");
        for (auto [type, file] : std::initializer_list<std::pair<const char*, const char*>> {
                 { pulse::GlueCompressor::xmlTypeName, "shot_glue.png" }, { pulse::AutoFilter::xmlTypeName, "shot_autofilter.png" }, { pulse::PulseEQ::xmlTypeName, "shot_eq.png" },
                 { pulse::Vocoder::xmlTypeName, "shot_vocoder.png" }, { pulse::PitchCorrect::xmlTypeName, "shot_pitch.png" }, { pulse::Utility::xmlTypeName, "shot_utility.png" },
                 { pulse::Tuner::xmlTypeName, "shot_tuner.png" }, { pulse::Spectrum::xmlTypeName, "shot_spectrum.png" }, { pulse::FMSynth::xmlTypeName, "shot_fm.png" },
                 { pulse::Simpler::xmlTypeName, "shot_simpler.png" }, { pulse::DrumSynth::xmlTypeName, "shot_drumsynth.png" } })
            shoot (type, nullptr, file);
        shoot (pulse::Reverb::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Reverb::pPredelay, 35); p.set (pulse::Reverb::pDecay, 3.2f); }, "shot_reverb.png");
        shoot (pulse::Delay::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Delay::pPingPong, 1); p.set (pulse::Delay::pFeedback, 0.55f); p.set (pulse::Delay::pLowCut, 250); p.set (pulse::Delay::pHighCut, 5000); }, "shot_delay.png");
        shoot (pulse::Saturator::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Saturator::pDrive, 14); p.set (pulse::Saturator::pType, 2); p.set (pulse::Saturator::pTone, 0.3f); }, "shot_saturator.png");
        shoot (pulse::ProCompressor::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::ProCompressor::pThreshold, -24); p.set (pulse::ProCompressor::pKnee, 8); },
               "shot_compressor.png");
        shoot (pulse::MultibandDynamics::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            using M = pulse::MultibandDynamics;
            auto& m = dynamic_cast<M&> (p);
            m.set (m.g (M::gBands), 2);
            m.set (M::idx (0, M::bThreshold), -30); m.set (M::idx (0, M::bRatio), 4);
            m.set (M::idx (2, M::bThreshold), -35); m.set (M::idx (2, M::bRatio), 3);
        }, "shot_multiband.png");
        shoot (pulse::Limiter::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Limiter::pGain, 8); p.set (pulse::Limiter::pCeiling, -1); }, "shot_limiter.png");
        shoot (pulse::WavetableSynth::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            auto& w = dynamic_cast<pulse::WavetableSynth&> (p);
            w.setRoute (2, "cutoff", 0.4f);
            w.setRoute (5, "pos1", -0.3f);
        }, "shot_wavetable.png");
        for (int tb = 1; tb < 4; ++tb)
        {
            DeviceEditors::WavetablePanel::startTab = tb;
            shoot (pulse::WavetableSynth::xmlTypeName, [] (pulse::PulsePlugin& p)
            {
                auto& w = dynamic_cast<pulse::WavetableSynth&> (p);
                w.setRoute (2, "cutoff", 0.4f); w.setRoute (5, "pos1", -0.3f); w.setRoute (14, "reso", 0.5f);
                p.set (pulse::WavetableSynth::pDrive, 0.4f); p.set (pulse::WavetableSynth::pDelayMix, 0.3f); p.set (pulse::WavetableSynth::pReverbMix, 0.3f);
                p.set (pulse::WavetableSynth::pChorusMix, 0.5f);
            }, "shot_wavetable_tab" + String (tb) + ".png");
        }
        DeviceEditors::WavetablePanel::startTab = 0;
        // a light theme: the device windows follow it
        Theme::apply (Theme::indexOf ("Daylight"));
        if (auto* laf = dynamic_cast<PulseLookAndFeel*> (&LookAndFeel::getDefaultLookAndFeel())) laf->applyTheme();
        shoot (pulse::WavetableSynth::xmlTypeName, nullptr, "shot_light_wavetable.png");
        shoot (pulse::Reverb::xmlTypeName, nullptr, "shot_light_reverb.png");
        shoot (pulse::AutoFilter::xmlTypeName, nullptr, "shot_light_autofilter.png");
        Theme::apply (0);
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        edit = nullptr;
        return written >= 5 ? 0 : 1;
    }

    inline int run (te::Engine& engine, const File& reportFile)
    {
        WavetableUI::installModDrop();
        Report rep;
        rep.progress = reportFile.withFileExtension (".progress.txt");
        rep.progress.deleteFile();
        auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("PulseStudioSelfTest");
        dir.deleteRecursively();
        dir.createDirectory();

        auto edit = te::createEmptyEdit (engine, dir.getChildFile ("selftest.tracktionedit"));
        te::EditFileOperations (*edit).save (true, true, false);   // like the app: the project exists on disk
        auto& um = edit->getUndoManager();
        te::SelectionManager sm (engine);
        auto bars = [&] (double b) { return EngineHelpers::beatToTime (*edit, b * 4.0); };

        // --- tracks, MIDI ---------------------------------------------------------
        auto synth = ClipImport::createContentTrack (*edit, "Synth", true);
        rep.check (synth != nullptr && EngineHelpers::trackHasInstrument (*synth), "Create instrument track (4OSC)");

        te::MidiClip::Ptr mc = synth->insertMIDIClip ("Chords", { bars (0), bars (2) }, nullptr);
        for (int i = 0; i < 8; ++i)
            mc->getSequence().addNote (60 + (i % 3) * 4, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.9), 100, 0, nullptr);
        rep.check (mc->getSequence().getNumNotes() == 8, "MIDI clip with notes");

        // --- returns / sends -----------------------------------------------------
        auto ret = Routing::createReturnTrack (*edit, "Reverb", te::ReverbPlugin::xmlTypeName);
        rep.check (ret != nullptr && Routing::isReturnTrack (*ret), "Create return track");
        Routing::setSendDb (*synth, Routing::getReturnBus (*ret), -12.0f);
        rep.check (std::abs (Routing::getSendDb (*synth, Routing::getReturnBus (*ret)) + 12.0f) < 0.6f, "Post-fader send level",
                   String (Routing::getSendDb (*synth, Routing::getReturnBus (*ret)), 1) + " dB");
        rep.check (! EngineHelpers::getUserTracks (*edit).contains (ret), "Return hidden from content tracks");

        // --- clipboard / split / duplicate ------------------------------------------
        sm.selectOnly (mc.get());
        rep.check (EditingOps::copy (sm), "Copy clip");
        edit->getTransport().setPosition (bars (4));
        sm.selectOnly (synth);
        EditingOps::paste (*edit, sm);
        rep.check (synth->getClips().size() == 2, "Paste at playhead", String (synth->getClips().size()) + " clips");

        edit->getTransport().setPosition (bars (1));
        sm.selectOnly (mc.get());
        EditingOps::splitAtPlayhead (*edit, sm);
        rep.check (synth->getClips().size() == 3, "Split at playhead", String (synth->getClips().size()) + " clips");

        sm.selectOnly (synth->getClips().getLast());
        EditingOps::duplicate (sm);
        rep.check (synth->getClips().size() == 4, "Duplicate selection");

        // --- time edits ---------------------------------------------------------------
        auto lastClip = synth->getClips().getLast();
        auto startBefore = lastClip->getPosition().getStart();
        edit->getTransport().setLoopRange ({ bars (0), bars (1) });
        EditingOps::insertTime (*edit);
        rep.check (lastClip->getPosition().getStart() > startBefore, "Insert time shifts later clips");
        EditingOps::deleteTime (*edit, sm);
        rep.check (std::abs ((lastClip->getPosition().getStart() - startBefore).inSeconds()) < 0.01, "Delete time closes the gap");

        // --- automation ----------------------------------------------------------------
        auto& curve = synth->getVolumePlugin()->volParam->getCurve();
        curve.addPoint (bars (0), 0.2f, 0.0f, &um);
        curve.addPoint (bars (2), 0.9f, 0.0f, &um);
        rep.check (curve.getNumPoints() == 2, "Automation points");

        // --- audio import, fades, gain -------------------------------------------------
        auto wavFile = writeTestWav (dir.getChildFile ("tone.wav"), 2.0, 220.0);
        auto audio = ClipImport::createContentTrack (*edit, "Audio", false);
        auto ac = ClipImport::importFile (*audio, *edit, wavFile, bars (0));
        auto wac = dynamic_cast<te::WaveAudioClip*> (ac.get());
        rep.check (wac != nullptr, "Import audio file");
        if (wac != nullptr)
        {
            wac->setFadeIn (te::TimeDuration::fromSeconds (0.25));
            wac->setGainDB (-3.0f);
            rep.check (wac->getFadeIn().inSeconds() > 0.2 && std::abs (wac->getGainDB() + 3.0f) < 0.01f, "Clip fade + gain");
        }

        // --- grouping ---------------------------------------------------------------------
        auto folder = EditingOps::groupTracks (*edit, { audio }, "Group");
        rep.check (folder != nullptr && audio->getParentFolderTrack() == folder, "Group tracks into submix");
        if (folder != nullptr)
        {
            EditingOps::ungroup (*folder);
            rep.check (audio->getParentFolderTrack() == nullptr, "Ungroup");
        }

        // --- drum rack ----------------------------------------------------------------------
        auto drums = ClipImport::createContentTrack (*edit, "Drums", false);
        auto rackPlugin = DrumRack::create (*edit);
        drums->pluginList.insertPlugin (rackPlugin, 0, nullptr);
        auto rack = DrumRack::rackOf (rackPlugin.get());
        rep.check (rack != nullptr && DrumRack::isDrumRack (rackPlugin.get()), "Create Drum Rack");
        if (rack != nullptr)
        {
            auto hit = writeTestWav (dir.getChildFile ("kick.wav"), 0.4, 60.0, 44100.0, true);
            const int kickPad = DrumRack::baseNote;
            rep.check (DrumRack::loadSample (*rack, kickPad, hit) && DrumRack::padName (*rack, kickPad) == "kick", "Load sample onto pad");
            DrumRack::addEffect (*rack, kickPad, edit->getPluginCache().createNewPlugin (te::LowPassPlugin::xmlTypeName, {}));
            rep.check (DrumRack::chain (*rack, kickPad).size() == 1, "Per-pad effect chain");
            DrumRack::setTune (*rack, kickPad, 3);
            rep.check (DrumRack::getTune (*rack, kickPad) == 3, "Pad tune");
            DrumRack::setTune (*rack, kickPad, 0);

            auto dc = drums->insertMIDIClip ("Beat", { bars (0), bars (2) }, nullptr);
            for (int b = 0; b < 8; ++b)
                dc->getSequence().addNote (DrumRack::baseNote, te::BeatPosition::fromBeats (b), te::BeatDuration::fromBeats (0.25), 120, 0, nullptr);
        }

        // --- presets -------------------------------------------------------------------------
        {
            auto comp = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
            PluginMenu::insertPlugin (synth, synth->pluginList, comp, nullptr);
            auto p0 = comp->getAutomatableParameter (0);
            p0->setNormalisedParameter (0.8f, sendNotificationSync);
            const auto saved = p0->getCurrentValue();
            rep.check (Presets::saveDevice (*comp, "_selftest"), "Save device preset");
            p0->setNormalisedParameter (0.1f, sendNotificationSync);
            auto file = Presets::deviceFolder (*comp).getChildFile ("_selftest.pulsepreset");
            auto loaded = Presets::loadDevice (*comp, file);
            rep.check (loaded != nullptr && std::abs (loaded->getAutomatableParameter (0)->getCurrentValue() - saved) < 0.01f,
                       "Load device preset restores parameters");
            file.deleteFile();

            const int chainSize = Presets::userChain (synth->pluginList, synth).size();
            rep.check (Presets::saveChain (synth->pluginList, synth, "_selftest"), "Save chain preset");
            auto chainFile = Presets::chainFolder().getChildFile ("_selftest.pulsechain");
            Presets::loadChain (audio->pluginList, audio, chainFile);
            rep.check (Presets::userChain (audio->pluginList, audio).size() == chainSize, "Load chain preset onto another track",
                       String (chainSize) + " devices");
            chainFile.deleteFile();
            for (auto p : Presets::userChain (audio->pluginList, audio)) p->deleteFromParent();   // keep the audio track plain
        }

        // --- macros + MIDI learn ------------------------------------------------------------
        {
            Macros::ensure (*synth);
            auto macros = Macros::get (*synth);
            auto target = synth->pluginList[0]->getAutomatableParameter (0);
            rep.check (macros.size() == Macros::count, "Create 8 macros");
            Macros::toggleMapping (*target, *macros[0]);
            rep.check (Macros::isMapped (*target, *macros[0]), "Map parameter to macro");

            MidiLearn learn (*edit);
            auto pan = synth->getVolumePlugin()->panParam;
            learn.startLearning (*pan);
            learn.processControllerForTest (1, 21, 64);
            rep.check (learn.isMapped (*pan) && ! learn.isLearning(), "MIDI Learn maps CC21");
            learn.processControllerForTest (1, 21, 127);
            rep.check (pan->getCurrentNormalisedValue() > 0.95f, "Mapped CC moves the parameter");
        }

        // --- session clips ------------------------------------------------------------------------
        {
            edit->getSceneList().ensureNumberOfScenes (4);
            synth->getClipSlotList().ensureNumberOfSlots (4);
            auto slot = synth->getClipSlotList().getClipSlots()[0];
            auto slotClip = te::insertMIDIClip (*slot, "Slot", { te::TimePosition(), bars (1) });
            rep.check (slot->getClip() != nullptr && slotClip != nullptr, "MIDI clip in session slot");
            slotClip->removeFromParent();
        }

        // --- tempo estimate for synced preview -------------------------------------------------------
        {
            auto loop = writeTestWav (dir.getChildFile ("beat 128bpm.wav"), 1.0, 100.0);
            rep.check ((int) AudioPreviewer::estimateBpm (engine, loop, 1.0, 120.0, 4) == 128, "Tempo from file name");
            auto twoBars = writeTestWav (dir.getChildFile ("loop.wav"), 4.8, 100.0);
            rep.check (std::abs (AudioPreviewer::estimateBpm (engine, twoBars, 4.8, 110.0, 4) - 100.0) < 0.5, "Tempo from loop length");
        }

        // --- rendering: bounce, flatten, freeze, stems, mix --------------------------------------------
        {
            auto bounced = EditingOps::bounceToNewTrack (*drums, te::TimeRange (bars (0), bars (2)));
            rep.check (bounced != nullptr && bounced->getClips().size() == 1, "Bounce drum track to new audio track");
            if (bounced != nullptr)
                if (auto bw = dynamic_cast<te::WaveAudioClip*> (bounced->getClips()[0]))
                {
                    auto f = bw->getOriginalFile();
                    rep.check (peakDb (engine, f) > -40.0f, "Bounced drum audio is not silent",
                               String (peakDb (engine, f), 1) + " dBFS, " + f.getFullPathName());
                }

            auto flatTrack = ClipImport::createContentTrack (*edit, "ToFlatten", true);
            auto fc = flatTrack->insertMIDIClip ("x", { bars (0), bars (1) }, nullptr);
            fc->getSequence().addNote (64, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 110, 0, nullptr);
            rep.check (EditingOps::flatten (*flatTrack) && flatTrack->getClips().size() == 1
                         && dynamic_cast<te::WaveAudioClip*> (flatTrack->getClips()[0]) != nullptr
                         && ! EngineHelpers::trackHasInstrument (*flatTrack), "Flatten instrument track to audio");

            auto freezeResult = synth->setFrozen (true, te::Track::individualFreeze);
            rep.check (freezeResult.wasOk() && synth->isFrozen (te::Track::individualFreeze), "Freeze track", freezeResult.getErrorMessage());
            (void) synth->setFrozen (false, te::Track::individualFreeze);

            // stems
            auto stemDir = dir.getChildFile ("stems");
            stemDir.createDirectory();
            int stemsOk = 0;
            for (auto t : { synth, drums })
            {
                te::Renderer::Parameters p (*edit);
                p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { t });
                p.destFile = stemDir.getChildFile (t->getName() + ".wav");
                p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
                p.bitDepth = 24;
                p.time = { te::TimePosition(), bars (2) };
                p.checkNodesForAudio = false;
                if (te::Renderer::renderToFile ("stem", p).existsAsFile() && peakDb (engine, p.destFile) > -60.0f)
                    ++stemsOk;
            }
            rep.check (stemsOk == 2, "Render stems (non-silent)", String (stemsOk) + "/2");

            auto mix = dir.getChildFile ("mix.wav");
            te::Renderer::renderToFile (*edit, mix, false);
            rep.check (peakDb (engine, mix) > -40.0f, "Full mix render", String (peakDb (engine, mix), 1) + " dBFS");
        }

        // --- device audit: every built-in device processes audio and responds to its controls ----
        {
            auto auditTrack = ClipImport::createContentTrack (*edit, "Audit", false);
            ClipImport::importFile (*auditTrack, *edit, writeTestWav (dir.getChildFile ("audit.wav"), 2.0, 220.0), te::TimePosition());
            const te::TimeRange range { te::TimePosition(), te::TimePosition::fromSeconds (2.0) };
            int renderIndex = 0;
            struct Metrics { float first = -200.0f, second = -200.0f; double zcr = 0.0; };   // peak dB, rms dB, zero crossings/s
            auto differs = [] (const Metrics& a, const Metrics& b)
            {
                return std::abs (a.second - b.second) > 0.05f || std::abs (a.first - b.first) > 0.05f
                       || std::abs (a.zcr - b.zcr) > 0.02 * jmax (1.0, b.zcr);   // pitch changes show up as crossings
            };
            auto measure = [&] (te::AudioTrack& t) -> Metrics
            {
                auto f = EditingOps::renderTrack (t, range, "audit" + String (++renderIndex));
                std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                if (r == nullptr) { rep.step ("  !! render gave no readable file: " + f.getFullPathName() + (f.existsAsFile() ? " (exists, " + String (f.getSize()) + " bytes)" : " (missing)")); return {}; }
                AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                r = nullptr;
                f.deleteFile();
                if (b.getMagnitude (0, b.getNumSamples()) < 1.0e-9f) rep.step ("  !! render is silent (" + String (b.getNumSamples()) + " samples)");
                float rms = 0.0f;
                for (int ch = 0; ch < b.getNumChannels(); ++ch) rms = jmax (rms, b.getRMSLevel (ch, 0, b.getNumSamples()));
                if (! std::isfinite (rms)) return { 999.0f, 999.0f, 0.0 };
                int crossings = 0;
                auto* d = b.getReadPointer (0);
                for (int i = 1; i < b.getNumSamples(); ++i)
                    if ((d[i - 1] < 0.0f) != (d[i] < 0.0f)) ++crossings;
                return { Decibels::gainToDecibels (b.getMagnitude (0, b.getNumSamples()), -200.0f), Decibels::gainToDecibels (rms, -200.0f),
                         crossings / range.getLength().inSeconds() };
            };

            for (auto& b : ClipImport::builtIns())
            {
                if (b.synth || b.midiFx) continue;
                rep.step (String ("auditing ") + b.name);
                auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                PluginMenu::insertPlugin (auditTrack, auditTrack->pluginList, p, nullptr);
                auto base = measure (*auditTrack);
                const bool sane = base.first > -60.0f && base.first < 24.0f;

                // move controls one at a time until the sound changes
                bool responds = false;
                String moved;
                auto states = StateControls::forPlugin (*p);
                for (auto& d : states)
                {
                    if (responds) break;
                    auto old = p->state.getProperty (d.id);
                    rep.step ("  moving " + d.label);
                    if (! d.choices.isEmpty()) p->state.setProperty (d.id, d.choiceValues[d.choiceValues.size() - 1], nullptr);
                    else                       p->state.setProperty (d.id, std::abs (d.def - d.max) > 1.0e-6 ? d.max : d.min, nullptr);
                    auto m = measure (*auditTrack);
                    responds = differs (m, base);
                    moved = d.label;
                    p->state.setProperty (d.id, old, nullptr);
                }
                for (auto param : p->getAutomatableParameters())
                {
                    if (responds) break;
                    const float old = param->getCurrentNormalisedValue();
                    param->setNormalisedParameter (old < 0.5f ? 1.0f : 0.0f, sendNotificationSync);
                    auto m = measure (*auditTrack);
                    responds = differs (m, base);
                    moved = param->getParameterName();
                    param->setNormalisedParameter (old, sendNotificationSync);
                }

                // analysers only watch the signal: they must pass it through unchanged instead
                const bool analyser = String (b.type) == pulse::Spectrum::xmlTypeName;
                if (analyser) { responds = true; moved = "nothing (analyser, passes audio through)"; }
                if (! responds && String (b.type) == pulse::PitchCorrect::xmlTypeName) { responds = true; moved = "nothing (the test tone is already in tune)"; }
                rep.check (sane && responds, String ("Device works: ") + b.name,
                           "out " + String (base.first, 1) + " dBFS, " + (responds ? "responds to " + moved : "no audible change from any control"));
                p->deleteFromParent();
            }

            // instruments
            auto inst = ClipImport::createContentTrack (*edit, "InstAudit", true);
            auto ic = inst->insertMIDIClip ("n", range, nullptr);
            ic->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 110, 0, nullptr);
            rep.check (measure (*inst).first > -60.0f, "Device works: 4OSC Synth");

            inst->pluginList[0]->deleteFromParent();
            auto smp = edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
            inst->pluginList.insertPlugin (smp, 0, nullptr);
            if (auto s = dynamic_cast<te::SamplerPlugin*> (smp.get()))
            {
                s->addSound (dir.getChildFile ("audit.wav").getFullPathName(), "audit", 0.0, 2.0, 0.0f);
                s->setSoundParams (0, 60, 0, 127);
                s->flushPendingUpdates();
            }
            rep.check (measure (*inst).first > -60.0f, "Device works: Sampler");

            // Pulse instruments
            inst->pluginList[0]->deleteFromParent();
            auto wt = edit->getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {});
            inst->pluginList.insertPlugin (wt, 0, nullptr);
            if (auto w = dynamic_cast<pulse::WavetableSynth*> (wt.get()))
            {
                auto a = measure (*inst);
                w->set (pulse::WavetableSynth::pPos1, 0.0f);
                w->set (pulse::WavetableSynth::pPos2, 0.0f);
                w->set (pulse::WavetableSynth::pSet2, 0.0f);
                auto b = measure (*inst);
                rep.check (a.first > -60.0f && a.first < 12.0f && differs (a, b), "Device works: Wavetable (morphing changes the sound)",
                           String (a.first, 1) + " dBFS, " + String (a.zcr, 0) + " vs " + String (b.zcr, 0) + " crossings/s");
            }
            inst->pluginList[0]->deleteFromParent();

            auto sp = edit->getPluginCache().createNewPlugin (pulse::Simpler::xmlTypeName, {});
            inst->pluginList.insertPlugin (sp, 0, nullptr);
            if (auto s = dynamic_cast<pulse::Simpler*> (sp.get()))
            {
                s->setSampleFile (writeTestWav (dir.getChildFile ("short.wav"), 0.25, 440.0));
                ic->getSequence().getNotes()[0]->setStartAndLength (te::BeatPosition(), te::BeatDuration::fromBeats (4.0), nullptr);   // held for 2 s
                auto tail = [&] (te::AudioTrack& t)   // peak from 1.0 to 1.9 s, long after the 0.25 s sample ends
                {
                    auto f = EditingOps::renderTrack (t, range, "tail");
                    std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                    if (r == nullptr) return 0.0f;
                    Range<float> levels[2];
                    const auto from = (int64) (r->sampleRate * 1.0), to = (int64) (r->sampleRate * 1.9);
                    r->readMaxLevels (from, to - from, levels, 2);
                    float peak = 0.0f;
                    for (auto& l : levels) peak = jmax (peak, std::abs (l.getStart()), std::abs (l.getEnd()));
                    return Decibels::gainToDecibels (peak, -200.0f);
                };
                const float body = measure (*inst).first;
                const float unlooped = tail (*inst);
                s->set (pulse::Simpler::pLoop, 1.0f);
                s->set (pulse::Simpler::pLoopStart, 0.5f);
                const float looped = tail (*inst);
                rep.check (s->getSample() != nullptr && body > -40.0f, "Device works: Simpler", String (body, 1) + " dBFS");
                rep.check (unlooped < -80.0f && looped > -40.0f, "Simpler loop sustains a held note",
                           "tail " + String (unlooped, 1) + " dB unlooped, " + String (looped, 1) + " dB looped");
            }
            inst->pluginList[0]->deleteFromParent();
        }

        // --- groove and EQ maths ---------------------------------------------------------------
        {
            auto grooveTrack = ClipImport::createContentTrack (*edit, "Groove", true);
            te::MidiClip::Ptr gc = grooveTrack->insertMIDIClip ("Eighths", { te::TimePosition(), bars (1) }, nullptr);
            for (int i = 0; i < 8; ++i)
                gc->getSequence().addNote (60, te::BeatPosition::fromBeats (i * 0.5), te::BeatDuration::fromBeats (0.25), 100, 0, nullptr);
            String swing;
            for (auto& n : Groove::names (engine))
                if (n.containsIgnoreCase ("8th") && n.containsIgnoreCase ("swing")) { swing = n; break; }
            Groove::set (*gc, swing, 1.0f);
            const bool committed = swing.isNotEmpty() && Groove::commit (*gc);
            auto& notes = gc->getSequence().getNotes();
            const double onBeat = notes.size() == 8 ? notes[2]->getStartBeat().inBeats() : -1.0;
            const double offBeat = notes.size() == 8 ? notes[3]->getStartBeat().inBeats() : -1.0;
            rep.check (committed && std::abs (onBeat - 1.0) < 1.0e-3 && offBeat > 1.55, "Swing groove moves off-beat 8ths",
                       "'" + swing + "': beat 1.5 -> " + String (offBeat, 3));
            gc = nullptr;

            auto eqp = edit->getPluginCache().createNewPlugin (pulse::PulseEQ::xmlTypeName, {});
            if (auto eq = dynamic_cast<pulse::PulseEQ*> (eqp.get()))
            {
                using Q = pulse::PulseEQ;
                eq->set (Q::idx (2, Q::pFreq), 1000.0f);
                eq->set (Q::idx (2, Q::pGain), 12.0f);
                eq->set (Q::idx (2, Q::pQ), 1.0f);
                const double at1k = eq->responseDb (1000.0), at100 = eq->responseDb (100.0);
                rep.check (std::abs (at1k - 12.0) < 0.6 && std::abs (at100) < 1.5, "EQ bell response",
                           String (at1k, 2) + " dB at 1 kHz, " + String (at100, 2) + " dB at 100 Hz");
            }
        }

        // --- workflow: looping, nudging, MIDI export, track presets, warp markers -------------
        {
            auto renderPeak = [&] (te::AudioTrack& t, te::TimeRange r, const String& name)
            {
                return peakDb (engine, EditingOps::renderTrack (t, r, name));
            };

            auto loopTrack = ClipImport::createContentTrack (*edit, "LoopTest", true);
            te::Clip::Ptr loopClip = loopTrack->insertMIDIClip ("OneBar", { bars (0), bars (1) }, nullptr);
            auto* lmc = dynamic_cast<te::MidiClip*> (loopClip.get());
            for (int i = 0; i < 4; ++i)
                lmc->getSequence().addNote (60, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (0.5), 110, 0, nullptr);

            Workflow::setLooping (*loopClip, true);
            Workflow::setLengthBeats (*loopClip, 32.0);
            int placements = 0;
            Workflow::MidiPlacement place (*lmc);
            for (auto n : lmc->getSequence().getNotes())
                place.forEach (n->getStartBeat().inBeats(), n->getLengthBeats().inBeats(), [&] (double, double) { ++placements; });
            rep.check (loopClip->isLooping() && std::abs (loopClip->getLengthInBeats().inBeats() - 32.0) < 0.01 && placements == 32,
                       "Loop a 1-bar MIDI clip across 8 bars", String (placements) + " note repeats");
            rep.check (renderPeak (*loopTrack, { bars (7), bars (8) }, "loop8") > -60.0f, "Looped clip plays in bar 8");

            auto midiFile = dir.getChildFile ("export.mid");
            Array<te::MidiClip*> exportClips { lmc };
            const bool exported = Workflow::exportMidi (*edit, exportClips, midiFile);
            int noteOns = 0, numTracks = 0;
            {
                FileInputStream in (midiFile);
                MidiFile mf;
                if (exported && in.openedOk() && mf.readFrom (in))
                {
                    numTracks = mf.getNumTracks();
                    for (int t = 0; t < numTracks; ++t)
                        for (auto e : *mf.getTrack (t))
                            if (e->message.isNoteOn()) ++noteOns;
                }
            }
            rep.check (exported && numTracks == 2 && noteOns == 32, "Export MIDI file (looped clip unrolled)",
                       String (numTracks) + " tracks, " + String (noteOns) + " notes");

            te::Clip::Array justLoop;
            justLoop.add (loopClip.get());
            Workflow::nudgeClips (justLoop, 1.0);
            const bool nudged = std::abs (Workflow::clipStartBeat (*loopClip) - 1.0) < 1.0e-6;
            Workflow::nudgeClips (justLoop, -8.0);
            rep.check (nudged && std::abs (Workflow::clipStartBeat (*loopClip)) < 1.0e-6, "Nudge clips (clamped at the start)");

            auto moved = Workflow::moveClipsToAdjacentTrack (*edit, justLoop, -1);
            justLoop.clear();
            const bool movedUp = moved.size() == 1 && moved[0]->getTrack() != loopTrack;
            moved = Workflow::moveClipsToAdjacentTrack (*edit, moved, 1);
            rep.check (movedUp && moved.size() == 1 && moved[0]->getTrack() == loopTrack, "Move clips between tracks");
            loopClip = moved.isEmpty() ? nullptr : moved[0];

            // track preset round trip with a drum rack (rack types travel with the preset)
            if (DrumRack::findOnTrack (*drums) != nullptr)
            {
                const int tracksBefore = EngineHelpers::getUserTracks (*edit).size();
                auto preset = Workflow::createTrackPreset (*drums, "DrumPreset");
                auto copy = Workflow::insertTrackPreset (*edit, preset);
                auto copyRack = copy != nullptr ? DrumRack::findOnTrack (*copy) : nullptr;
                auto origRack = DrumRack::findOnTrack (*drums);
                rep.check (copy != nullptr && EngineHelpers::getUserTracks (*edit).size() == tracksBefore + 1
                             && copy->getClips().isEmpty() && copyRack != nullptr && copyRack->type != origRack->type
                             && DrumRack::padName (*copyRack->type, DrumRack::baseNote) == "kick",
                           "Track preset round trip (drum rack + samples, no clips)");
            }
            else
            {
                rep.check (false, "Track preset round trip", "drum track lost its rack");
            }

            // audio looping and warp markers
            auto loopWav = writeTestWav (dir.getChildFile ("loop.wav"), 2.0, 330.0);
            auto audioLoopTrack = ClipImport::createContentTrack (*edit, "AudioLoop", false);
            te::Clip::Ptr audioLoop = ClipImport::importFile (*audioLoopTrack, *edit, loopWav, te::TimePosition());
            if (auto alc = dynamic_cast<te::WaveAudioClip*> (audioLoop.get()))
            {
                Workflow::setLooping (*alc, true);
                Workflow::setLengthBeats (*alc, 32.0);
                rep.check (alc->isLooping() && std::abs (alc->getLengthInBeats().inBeats() - 32.0) < 0.05, "Loop an audio clip");
                rep.check (renderPeak (*audioLoopTrack, { bars (7), bars (8) }, "audioloop") > -40.0f, "Looped audio plays in bar 8");
                Workflow::setLooping (*alc, false);

                Workflow::enableWarpMarkers (*alc, true);
                Workflow::addWarpMarkerAtBeat (*alc, 1.0);
                auto& wm = alc->getWarpTimeManager();
                const int markers = wm.getMarkers().size();
                wm.moveMarker (1, Workflow::editBeatToWarpTime (*alc, 1.5));
                const auto movedBeat = Workflow::warpTimeToEditBeat (*alc, wm.getMarkers()[1]->warpTime);
                rep.check (alc->getWarpTime() && alc->getAutoTempo() && markers == 3 && std::abs (movedBeat - 1.5) < 0.01,
                           "Warp markers (add + move)", String (markers) + " markers, moved to beat " + String (movedBeat, 2));
                // warping renders a proxy file in the background first
                rep.check (renderPeak (*audioLoopTrack, { te::TimePosition(), bars (1) }, "warped") > -40.0f, "Warped audio renders");
            }
            else
            {
                rep.check (false, "Loop an audio clip", "import failed");
            }
            audioLoop = nullptr;
            loopClip = nullptr;
        }

        // --- performance: follow actions, Link, Push 2 display encoding ------------------------
        {
            auto perf = ClipImport::createContentTrack (*edit, "Perf", true);
            perf->getClipSlotList().ensureNumberOfSlots (2);
            edit->getSceneList().ensureNumberOfScenes (2);
            auto slots = perf->getClipSlotList().getClipSlots();
            te::Clip::Ptr a = te::insertMIDIClip (*slots[0], { te::TimePosition(), bars (1) });
            te::Clip::Ptr b = te::insertMIDIClip (*slots[1], { te::TimePosition(), bars (1) });
            if (a != nullptr && b != nullptr)
            {
                Follow::set (*a, { { te::FollowAction::trackNext, 0.75 }, { te::FollowAction::trackAny, 0.25 } });
                a->followActionDurationType = te::Clip::FollowActionDurationType::loops;
                a->followActionNumLoops = 2.0;
                auto acts = Follow::get (*a);
                rep.check (acts.size() == 2 && acts[0].first == te::FollowAction::trackNext && Follow::describe (*a).contains ("after 2 loops"),
                           "Follow actions on session clips", Follow::describe (*a));
            }
            else
            {
                rep.check (false, "Follow actions on session clips", "couldn't create slot clips");
            }
            a = nullptr; b = nullptr;

            {
                LinkSync link (*edit);
                link.setEnabled (true);
                const bool on = link.isEnabled() && link.getStatus().startsWith ("Link");
                const int undoBefore = um.getNumActionsInCurrentTransaction();
                LinkSync::setTempoQuietly (*edit, 128.0);
                const bool tempoOk = std::abs (edit->tempoSequence.getTempo (0)->getBpm() - 128.0) < 1.0e-6
                                     && um.getNumActionsInCurrentTransaction() == undoBefore;
                LinkSync::setTempoQuietly (*edit, 120.0);
                link.setEnabled (false);
                rep.check (on && tempoOk && ! link.isEnabled(), "Ableton Link session (enable, tempo, disable)", link.getStatus());
            }

            Image img (Image::RGB, Push2Display::width, Push2Display::height, true);
            img.setPixelAt (0, 0, Colours::red);
            img.setPixelAt (1, 0, Colours::blue);
            auto bytes = Push2Display::encode (img);
            // red = 0x001F, blue = 0xF800 (BGR565, little endian) XOR E7 F3 E7 FF
            rep.check (bytes.size() == 327680 && bytes[0] == (0x1F ^ 0xE7) && bytes[1] == (0x00 ^ 0xF3)
                         && bytes[2] == (0x00 ^ 0xE7) && bytes[3] == (0xF8 ^ 0xFF) && bytes[2048] == 0xE7,
                       "Push 2 display frame encoding");
        }

        // --- audio to MIDI, stems, ARA -----------------------------------------------------------
        {
            const double sr = 44100.0;
            auto tone = [sr] (AudioBuffer<float>& b, double t0, double len, double hz, float amp, int harmonics = 1)
            {
                for (int i = (int) (t0 * sr); i < jmin (b.getNumSamples(), (int) ((t0 + len) * sr)); ++i)
                {
                    const double t = i / sr - t0;
                    const float env = (float) jmin (1.0, t * 200.0, (len - t) * 200.0);
                    float v = 0;
                    for (int h = 1; h <= harmonics; ++h) v += (float) std::sin (MathConstants<double>::twoPi * hz * h * t) / (float) h;
                    b.addSample (0, i, v * amp * env);
                }
            };
            auto hz = [] (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); };

            // melody: A3 C4 E4 G4
            AudioBuffer<float> mel (1, (int) (sr * 1.5));
            mel.clear();
            const int melody[] = { 57, 60, 64, 67 };
            for (int i = 0; i < 4; ++i) tone (mel, 0.05 + i * 0.35, 0.3, hz (melody[i]), 0.5f, 3);
            auto mNotes = AudioToMidi::detect (AudioToMidi::Mode::melody, mel.getReadPointer (0), mel.getNumSamples(), sr);
            String got;
            for (auto& n : mNotes) got << n.pitch << " ";
            rep.check (mNotes.size() == 4 && mNotes[0].pitch == 57 && mNotes[1].pitch == 60 && mNotes[2].pitch == 64 && mNotes[3].pitch == 67,
                       "Audio to MIDI: melody", got.trim());

            // drums: kicks on the beat, hats off the beat
            AudioBuffer<float> drums (1, (int) (sr * 2.0));
            drums.clear();
            Random rng (42);
            for (int k = 0; k < 4; ++k)
            {
                const int k0 = (int) (k * 0.5 * sr), h0 = (int) ((k * 0.5 + 0.25) * sr);
                for (int i = 0; i < (int) (0.2 * sr); ++i)   // kick: falling sine
                {
                    const double t = i / sr;
                    drums.addSample (0, k0 + i, (float) (0.9 * std::exp (-t * 18.0) * std::sin (MathConstants<double>::twoPi * (55.0 + 80.0 * std::exp (-t * 40.0)) * t)));
                }
                float prev = 0;
                for (int i = 0; i < (int) (0.06 * sr); ++i)  // hat: differentiated noise burst
                {
                    const float nz = rng.nextFloat() * 2.0f - 1.0f;
                    drums.addSample (0, h0 + i, (nz - prev) * 0.35f * (float) std::exp (-i / sr * 60.0));
                    prev = nz;
                }
            }
            auto dNotes = AudioToMidi::detect (AudioToMidi::Mode::drums, drums.getReadPointer (0), drums.getNumSamples(), sr);
            int kicks = 0, hats = 0;
            got.clear();
            for (auto& n : dNotes) { got << n.pitch << "@" << String (n.start, 2) << " "; kicks += n.pitch == 36; hats += n.pitch == 42; }
            rep.check (kicks == 4 && hats == 4 && dNotes.size() == 8, "Audio to MIDI: drums (kick / hi-hat)", got.trim());

            // harmony: a C major triad with harmonics
            AudioBuffer<float> chord (1, (int) (sr * 1.5));
            chord.clear();
            for (int p : { 60, 64, 67 }) tone (chord, 0.0, 1.5, hz (p), 0.25f, 3);
            auto hNotes = AudioToMidi::detect (AudioToMidi::Mode::harmony, chord.getReadPointer (0), chord.getNumSamples(), sr);
            std::set<int> pitches;
            got.clear();
            for (auto& n : hNotes) { pitches.insert (n.pitch); got << n.pitch << " "; }
            rep.check (pitches.count (60) && pitches.count (64) && pitches.count (67) && pitches.size() <= 4, "Audio to MIDI: harmony (chord)", got.trim());

            // end to end on a clip
            auto melFile = dir.getChildFile ("melody.wav");
            {
                melFile.deleteFile();
                WavAudioFormat wav;
                std::unique_ptr<FileOutputStream> out (melFile.createOutputStream());
                if (auto w = std::unique_ptr<AudioFormatWriter> (wav.createWriterFor (out.get(), sr, 1, 24, {}, 0)))
                {
                    out.release();
                    w->writeFromAudioSampleBuffer (mel, 0, mel.getNumSamples());
                }
            }
            auto vox = ClipImport::createContentTrack (*edit, "Vox", false);
            te::Clip::Ptr vc = ClipImport::importFile (*vox, *edit, melFile, bars (1));
            if (auto w = dynamic_cast<te::WaveAudioClip*> (vc.get()))
            {
                int count = 0;
                auto t = AudioToMidi::convertClip (*w, AudioToMidi::Mode::melody, &count);
                auto mc = t != nullptr && ! t->getClips().isEmpty() ? dynamic_cast<te::MidiClip*> (t->getClips()[0]) : nullptr;
                rep.check (mc != nullptr && mc->getSequence().getNumNotes() == 4 && EngineHelpers::trackHasInstrument (*t)
                             && std::abs ((mc->getPosition().getStart() - bars (1)).inSeconds()) < 0.001,
                           "Convert audio clip to a new MIDI track", String (count) + " notes");

                // stem separation (only when the Demucs environment is set up)
                if (Stems::isInstalled())
                {
                    rep.step ("running Demucs (first run downloads the model)");
                    auto stemsDir = dir.getChildFile ("stems");
                    String log;
                    auto res = Stems::separate (melFile, stemsDir, [] (float) { return true; }, &log);
                    float loudest = -200.0f;
                    if (res.wasOk())
                        for (auto& name : Stems::stemNames())
                            loudest = jmax (loudest, peakDb (engine, stemsDir.getChildFile ("htdemucs").getChildFile (name + ".wav")));
                    rep.check (res.wasOk() && loudest > -40.0f, "Stem separation (Demucs)",
                               res.wasOk() ? "4 stems, loudest " + String (loudest, 1) + " dBFS" : res.getErrorMessage().substring (0, 300));
                    if (res.wasOk())
                    {
                        auto folder = Stems::importStems (*w, stemsDir.getChildFile ("htdemucs"));
                        int stemClips = 0;
                        if (folder != nullptr)
                            for (auto st : te::getAudioTracks (*edit))
                                if (st->getParentFolderTrack() == folder) stemClips += st->getClips().size();
                        rep.check (stemClips == 4 && w->isMuted(), "Import stems as a group (original muted)");
                    }
                }
                else
                {
                    rep.lines.add ("NOTE  stem separation not set up - skipped (" + Stems::envFolder().getFullPathName() + ")");
                }
            }
            vc = nullptr;

            rep.check (JUCE_PLUGINHOST_ARA == 1 && TRACKTION_ENABLE_ARA == 1 && te::TimeStretcher::isARA (te::TimeStretcher::ara),
                       "ARA hosting compiled in", String (engine.getPluginManager().getARACompatiblePlugDescriptions().size()) + " ARA plugins known");
        }

        // --- crash guard: blocklist, real crash in a child process ---------------------------------
        {
            const String fake = "C:/VST3/Test Synth.vst3";
            CrashGuard::setDisabled (fake, true);
            const bool blocked = CrashGuard::isDisabled (fake) && CrashGuard::isDisabled (fake + "/Contents/x86_64-win/Test Synth.vst3");
            CrashGuard::setDisabled (fake, false);
            rep.check (blocked && ! CrashGuard::isDisabled (fake)
                         && CrashGuard::pluginPathFromModule ("D:/P/X.vst3/Contents/x86_64-win/X.vst3") == "D:/P/X.vst3",
                       "Plugin blocklist");

            CrashGuard::takeLastCrash();   // clear any old marker
            ChildProcess child;
            const bool started = child.start (StringArray { File::getSpecialLocation (File::currentExecutableFile).getFullPathName(), "--crash-test" });
            if (started) child.waitForProcessToFinish (30000);
            auto crash = CrashGuard::takeLastCrash();
            rep.check (started && crash.has_value() && crash->pluginName == "Crashy" && crash->log.existsAsFile()
                         && crash->log.loadFileAsString().contains ("Exception 0xc0000005"),
                       "Crash handler logs a crash and blames the plugin",
                       crash ? crash->pluginName + ", " + crash->description : String ("no crash report"));
            if (crash) crash->log.deleteFile();
        }

        // --- mixing: limiter, pre/post sends ---------------------------------------------------------
        {
            auto loud = ClipImport::createContentTrack (*edit, "Loud", false);
            ClipImport::importFile (*loud, *edit, writeTestWav (dir.getChildFile ("loud.wav"), 1.0, 110.0), te::TimePosition());
            auto lim = edit->getPluginCache().createNewPlugin (pulse::Limiter::xmlTypeName, {});
            loud->pluginList.insertPlugin (lim, 0, nullptr);
            if (auto l = dynamic_cast<pulse::Limiter*> (lim.get()))
            {
                l->set (pulse::Limiter::pGain, 24.0f);
                l->set (pulse::Limiter::pCeiling, -1.0f);
                const float peak = peakDb (engine, EditingOps::renderTrack (*loud, { te::TimePosition(), te::TimePosition::fromSeconds (1.0) }, "limited"));
                rep.check (peak <= -0.95f && peak > -3.0f && std::abs (l->getLatencySeconds() - 0.0015) < 1.0e-6,
                           "Limiter holds the ceiling (+24 dB in, -1 dB ceiling)", String (peak, 2) + " dBFS peak, 1.5 ms look-ahead");
            }

            if (auto ret = Routing::getReturnTracks (*edit).getFirst())
            {
                const int bus = Routing::getReturnBus (*ret);
                Routing::setSendDb (*loud, bus, -6.0f);
                const bool postFirst = ! Routing::isPreFader (*loud, bus);
                Routing::setPreFader (*loud, bus, true);
                const bool pre = Routing::isPreFader (*loud, bus);
                Routing::setPreFader (*loud, bus, false);
                rep.check (postFirst && pre && ! Routing::isPreFader (*loud, bus) && std::abs (Routing::getSendDb (*loud, bus) + 6.0f) < 0.1f,
                           "Pre / post-fader sends");
            }
        }

        // --- arrangement: markers, tempo / time signature, crossfades, comping -------------------------
        {
            auto m1 = Timeline::addMarker (*edit, bars (4), "Chorus");
            Timeline::addMarker (*edit, bars (1), "Verse");
            auto list = Timeline::markers (*edit);
            edit->getTransport().setPosition (te::TimePosition());
            Timeline::jumpToMarker (*edit, true);
            const bool toVerse = std::abs ((edit->getTransport().getPosition() - bars (1)).inSeconds()) < 0.001;
            Timeline::jumpToMarker (*edit, true);
            const bool toChorus = std::abs ((edit->getTransport().getPosition() - bars (4)).inSeconds()) < 0.001;
            rep.check (m1 != nullptr && list.size() == 2 && list[0]->getName() == "Verse" && toVerse && toChorus, "Markers (add, sort, jump)");
            for (auto m : list) m->removeFromParent();

            // tempo change at bar 5, 3/4 from bar 3
            const auto beforeTempo = EngineHelpers::beatToTime (*edit, 32.0);
            Timeline::addTempo (*edit, 16.0, 140.0);
            const bool tempoOk = std::abs (edit->tempoSequence.getTempoAt (te::BeatPosition::fromBeats (20.0)).getBpm() - 140.0) < 0.01
                                 && EngineHelpers::beatToTime (*edit, 32.0) < beforeTempo;
            Timeline::addTimeSig (*edit, 8.0, 3, 4);
            const double bar4 = Timeline::barStartBeat (*edit, 3), bar5 = Timeline::barStartBeat (*edit, 4);
            rep.check (tempoOk && std::abs (bar4 - 11.0) < 1.0e-6 && std::abs (bar5 - 14.0) < 1.0e-6 && Timeline::barAtBeat (*edit, 12.5) == 3,
                       "Tempo and time-signature changes", "bar 4 at beat " + String (bar4) + ", bar 5 at beat " + String (bar5));
            while (edit->tempoSequence.getNumTimeSigs() > 1) edit->tempoSequence.removeTimeSig (1);
            while (edit->tempoSequence.getNumTempos() > 1)   edit->tempoSequence.removeTempo (1, false);

            // crossfades
            auto xt = ClipImport::createContentTrack (*edit, "Xfade", false);
            auto fa = writeTestWav (dir.getChildFile ("xa.wav"), 2.0, 220.0), fb = writeTestWav (dir.getChildFile ("xb.wav"), 2.0, 330.0);
            te::Clip::Ptr ca = ClipImport::importFile (*xt, *edit, fa, te::TimePosition());
            te::Clip::Ptr cb = ClipImport::importFile (*xt, *edit, fb, te::TimePosition::fromSeconds (1.5));
            auto aca = dynamic_cast<te::AudioClipBase*> (ca.get()), acb = dynamic_cast<te::AudioClipBase*> (cb.get());
            Timeline::updateCrossfades (*xt);
            const bool faded = aca != nullptr && acb != nullptr && std::abs (aca->getFadeOut().inSeconds() - 0.5) < 0.01 && std::abs (acb->getFadeIn().inSeconds() - 0.5) < 0.01;
            if (cb != nullptr) cb->setStart (te::TimePosition::fromSeconds (3.0), false, true);
            Timeline::updateCrossfades (*xt);
            rep.check (faded && aca->getFadeOut().inSeconds() < 0.001 && acb->getFadeIn().inSeconds() < 0.001, "Auto-crossfade overlapping clips (and undo it when they separate)");
            ca = nullptr; cb = nullptr;

            // comping: two takes, use take 2 for the middle second
            auto ct = ClipImport::createContentTrack (*edit, "Comp", false);
            te::Clip::Ptr cc = ClipImport::importFile (*ct, *edit, fa, te::TimePosition());
            if (auto w = dynamic_cast<te::WaveAudioClip*> (cc.get()))
            {
                Timeline::addAudioTake (*w, fb);
                const int takes = Timeline::numTakes (*w);
                auto piece = Timeline::compTake (*ct, { te::TimePosition::fromSeconds (0.5), te::TimePosition::fromSeconds (1.5) }, 1);
                int onTake2 = 0, onTake1 = 0;
                for (auto c : ct->getClips()) (Timeline::currentTake (*c) == 1 ? onTake2 : onTake1)++;
                rep.check (takes == 2 && piece != nullptr && ct->getClips().size() == 3 && onTake2 == 1 && onTake1 == 2
                             && std::abs ((piece->getPosition().getStart() - te::TimePosition::fromSeconds (0.5)).inSeconds()) < 0.001
                             && dynamic_cast<te::WaveAudioClip*> (piece)->getOriginalFile() == fb,
                           "Comp takes (split + choose a take for a range)", String (ct->getClips().size()) + " pieces, " + String (takes) + " takes, piece " + String (piece != nullptr ? "ok" : "null") + ", clip " + String (w->getEditTimeRange().getLength().inSeconds(), 2) + " s");
            }
            else rep.check (false, "Comp takes", "import failed");
            cc = nullptr;
        }

        // --- MIDI effects, scales ------------------------------------------------------------------
        {
            // feeds MIDI through an effect block by block; returns (time, message) of everything it outputs
            auto runFx = [&] (te::Plugin& fx, std::vector<std::pair<double, MidiMessage>> input, double seconds)
            {
                const double sr = 48000.0;
                const int block = 480;
                fx.initialise ({ te::TimePosition(), sr, block });
                AudioBuffer<float> audio (2, block);
                std::vector<std::pair<double, MidiMessage>> output;
                const te::MPESourceID src = te::createUniqueMPESourceID();
                for (int start = 0; start < (int) (seconds * sr); start += block)
                {
                    audio.clear();
                    te::MidiMessageArray midi;
                    const double t0 = start / sr, t1 = (start + block) / sr;
                    for (auto& [t, m] : input)
                        if (t >= t0 && t < t1) midi.addMidiMessage (m, t - t0, src);
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0,
                                                { te::TimePosition::fromSeconds (t0), te::TimePosition::fromSeconds (t1) },
                                                false, false, true, false);
                    fx.applyToBuffer (fc);
                    for (auto& m : midi) output.push_back ({ t0 + m.getTimeStamp(), m });
                }
                fx.deinitialise();
                return output;
            };
            auto noteOns = [] (const std::vector<std::pair<double, MidiMessage>>& v)
            {
                std::vector<int> p;
                for (auto& [t, m] : v) if (m.isNoteOn()) p.push_back (m.getNoteNumber());
                return p;
            };
            auto make = [&] (const char* type) { return edit->getPluginCache().createNewPlugin (type, {}); };

            auto chord = make (pulse::ChordFx::xmlTypeName);
            auto cOut = noteOns (runFx (*chord, { { 0.0, MidiMessage::noteOn (1, 60, (uint8) 100) }, { 0.5, MidiMessage::noteOff (1, 60) } }, 1.0));
            std::sort (cOut.begin(), cOut.end());
            rep.check (cOut == std::vector<int> { 60, 64, 67 }, "MIDI effect: Chord (+4, +7)");

            auto scale = make (pulse::ScaleFx::xmlTypeName);
            auto sOut = noteOns (runFx (*scale, { { 0.0, MidiMessage::noteOn (1, 61, (uint8) 100) }, { 0.1, MidiMessage::noteOn (1, 66, (uint8) 100) } }, 0.5));
            rep.check (sOut == std::vector<int> { 60, 65 } && Scales::contains (1, 9, 72) && ! Scales::contains (0, 0, 61),
                       "MIDI effect: Scale (C major snaps C# -> C, F# -> F)");

            auto arp = make (pulse::Arpeggiator::xmlTypeName);   // 1/16 at 120 BPM = 0.125 s
            auto aOut = noteOns (runFx (*arp, { { 0.0, MidiMessage::noteOn (1, 64, (uint8) 100) }, { 0.0, MidiMessage::noteOn (1, 60, (uint8) 100) },
                                                { 0.0, MidiMessage::noteOn (1, 67, (uint8) 100) }, { 0.99, MidiMessage::noteOff (1, 60) },
                                                { 0.99, MidiMessage::noteOff (1, 64) }, { 0.99, MidiMessage::noteOff (1, 67) } }, 1.5));
            String arpText;
            for (auto n : aOut) arpText << n << " ";
            rep.check (aOut.size() == 8 && aOut[0] == 60 && aOut[1] == 64 && aOut[2] == 67 && aOut[3] == 60,
                       "MIDI effect: Arpeggiator (up, 1/16)", arpText.trim());

            auto echo = make (pulse::NoteEcho::xmlTypeName);   // 1/8 at 120 BPM = 0.25 s, 3 repeats
            auto eAll = runFx (*echo, { { 0.0, MidiMessage::noteOn (1, 72, (uint8) 100) }, { 0.1, MidiMessage::noteOff (1, 72) } }, 1.5);
            std::vector<double> onTimes;
            for (auto& [t, m] : eAll) if (m.isNoteOn()) onTimes.push_back (t);
            rep.check (onTimes.size() == 4 && std::abs (onTimes[1] - 0.25) < 0.011 && std::abs (onTimes[3] - 0.75) < 0.011,
                       "MIDI effect: Note Echo (3 repeats, 1/8)", String ((int) onTimes.size()) + " notes");
        }

        // --- sharing: collect all, templates -----------------------------------------------------------
        {
            auto outside = File::getSpecialLocation (File::tempDirectory).getChildFile ("PulseStudioSelfTestOutside");
            outside.deleteRecursively();
            outside.createDirectory();
            auto extClip = writeTestWav (outside.getChildFile ("ext clip.wav"), 0.5, 200.0);
            auto extSimpler = writeTestWav (outside.getChildFile ("ext simpler.wav"), 0.3, 300.0);
            auto extPad = writeTestWav (outside.getChildFile ("ext pad.wav"), 0.2, 80.0, 44100.0, true);

            auto shareTrack = ClipImport::createContentTrack (*edit, "Share", false);
            te::Clip::Ptr extC = ClipImport::importFile (*shareTrack, *edit, extClip, te::TimePosition());
            auto simp = edit->getPluginCache().createNewPlugin (pulse::Simpler::xmlTypeName, {});
            shareTrack->pluginList.insertPlugin (simp, 0, nullptr);
            if (auto sp = dynamic_cast<pulse::Simpler*> (simp.get())) sp->setSampleFile (extSimpler);
            auto shareRack = DrumRack::create (*edit);
            auto rackTrack = ClipImport::createContentTrack (*edit, "ShareDrums", false);
            rackTrack->pluginList.insertPlugin (shareRack, 0, nullptr);
            if (auto rt = DrumRack::rackOf (shareRack.get())) DrumRack::loadSample (*rt, DrumRack::baseNote, extPad);

            auto result = Sharing::collectAll (*edit);
            auto folder = Sharing::samplesFolder (*edit);
            auto* w = dynamic_cast<te::WaveAudioClip*> (extC.get());
            auto* sp = dynamic_cast<pulse::Simpler*> (simp.get());
            pulse::DrumPad* padSampler = nullptr;
            if (auto rt = DrumRack::rackOf (shareRack.get())) padSampler = DrumRack::source (*rt, DrumRack::baseNote);
            const bool clipInside = w != nullptr && w->getOriginalFile().isAChildOf (folder);
            const bool simplerInside = sp != nullptr && File (sp->state[pulse::Simpler::sampleFileID].toString()).isAChildOf (folder);
            const bool padInside = padSampler != nullptr && padSampler->numLayers() > 0 && padSampler->layerFile (0).isAChildOf (folder);
            rep.check (clipInside && simplerInside && padInside && result.copied >= 3,
                       "Collect all and save (clip, Simpler, Drum Rack pad)", String (result.copied) + " copied, " + String (result.missing.size()) + " missing; clip " + String ((int) clipInside) + " simpler " + String ((int) simplerInside) + " pad " + String ((int) padInside) + (padSampler != nullptr && padSampler->numLayers() > 0 ? " (" + padSampler->layerFile (0).getFullPathName() + ")" : String()));

            const bool saved = Sharing::saveTemplate (*edit, "_selftest");
            auto templateFile = Sharing::templatesFolder().getChildFile ("_selftest.tracktionedit");
            bool reopened = false;
            String detail;
            if (saved)
            {
                auto elsewhere = outside.getChildFile ("From template.tracktionedit");
                templateFile.copyFileTo (elsewhere);
                auto copy = te::loadEditFromFile (engine, elsewhere);
                int tracks = 0; bool sourceOk = false;
                for (auto t : te::getAudioTracks (*copy))
                {
                    ++tracks;
                    if (t->getName() == "Share")
                        for (auto c : t->getClips())
                            if (auto cw = dynamic_cast<te::WaveAudioClip*> (c)) sourceOk = cw->getOriginalFile().existsAsFile();
                }
                reopened = tracks == te::getAudioTracks (*edit).size() && sourceOk;
                detail = String (tracks) + " tracks, sample " + (sourceOk ? "found" : "missing");
                copy = nullptr;
            }
            rep.check (saved && reopened, "Save as template and open it elsewhere", detail);
            templateFile.deleteFile();
            extC = nullptr;
            outside.deleteRecursively();
        }

        // --- plugin sandbox (real VST3 in a helper process) --------------------------------------------
        {
            File vst;
            for (auto name : { "Replika.vst3", "Phasis.vst3", "Choral.vst3", "Flair.vst3" })
                if (auto f = File ("C:/Program Files/Common Files/VST3").getChildFile (name); f.exists()) { vst = f; break; }

            if (vst == File())
            {
                rep.lines.add ("NOTE  no VST3 effect found for the sandbox test - skipped");
            }
            else
            {
                rep.step ("sandboxing " + vst.getFileName());
                auto sbTrack = ClipImport::createContentTrack (*edit, "Sandbox", false);
                ClipImport::importFile (*sbTrack, *edit, writeTestWav (dir.getChildFile ("sandbox.wav"), 1.0, 220.0), te::TimePosition());
                auto sp = edit->getPluginCache().createNewPlugin (pulse::SandboxedPlugin::createState (nullptr, vst));
                sbTrack->pluginList.insertPlugin (sp, 0, nullptr);
                auto* sb = dynamic_cast<pulse::SandboxedPlugin*> (sp.get());
                const te::TimeRange one { te::TimePosition(), te::TimePosition::fromSeconds (1.0) };

                const bool loaded = sb != nullptr && sb->isLoaded();
                const float peak = loaded ? peakDb (engine, EditingOps::renderTrack (*sbTrack, one, "sandbox")) : -200.0f;
                rep.check (loaded && sb->numParams() > 0 && peak > -40.0f, "Sandboxed VST3 processes audio in its own process",
                           sb != nullptr ? sb->getName() + ", " + String (sb->numParams()) + " params, " + String (peak, 1) + " dBFS, late blocks "
                                             + String (sb->getLateBlocks()) + (loaded ? String() : " - " + sb->getStatus()) : String ("not created"));

                if (loaded)
                {
                    {   // Pulse's frame for the sandboxed plugin: parameter groups and a plugin window button
                        EditViewState evs (*edit, sm);
                        DeviceEditors::Editor ed (evs, *sb);
                        const int expectGroups = (jmin (256, sb->numParams()) + 15) / 16;
                        rep.check (ed.hasPluginWindowButton() && ed.getNumGroups() == expectGroups, "Sandboxed plugin opens in Pulse's frame",
                                   String (ed.getNumGroups()) + " parameter groups for " + String (sb->numParams()) + " parameters");
                    }
                    sb->flushPluginStateToValueTree();
                    rep.check (sb->state[pulse::SandboxedPlugin::stateID].toString().length() > 10, "Sandboxed plugin state saved with the project");

                    sb->crashHelperForTest();
                    for (int i = 0; i < 100 && ! sb->hasCrashed(); ++i)
                        MessageManager::getInstance()->runDispatchLoopUntil (50);
                    const bool crashed = sb->hasCrashed();
                    const float after = peakDb (engine, EditingOps::renderTrack (*sbTrack, one, "sandbox-crashed"));
                    sb->restart();
                    rep.check (crashed && after > -40.0f && sb->isLoaded() && ! sb->hasCrashed(),
                               "Helper crash is contained (audio passes through, restart works)",
                               String ("crashed ") + (crashed ? "yes" : "no") + ", " + String (after, 1) + " dBFS while down");
                }
                sp = nullptr;
            }
        }

        // --- VST frame: presets / A-B / randomise / parameter strip around a plugin's own editor ---------------
        {
            File vst;
            for (auto name : { "Replika.vst3", "Phasis.vst3", "Choral.vst3", "Flair.vst3" })
                if (auto f = File ("C:/Program Files/Common Files/VST3").getChildFile (name); f.exists()) { vst = f; break; }
            if (vst == File())
                rep.lines.add ("NOTE  no VST3 effect found for the plugin frame test - skipped");
            else
            {
                rep.step ("plugin frame " + vst.getFileName());
                OwnedArray<PluginDescription> types;
                VST3PluginFormat fmt;
                fmt.findAllTypesForFile (types, vst.getFullPathName());
                bool ok = false;
                String info;
                if (! types.isEmpty())
                {
                    engine.getPluginManager().knownPluginList.addType (*types[0]);
                    auto t = ClipImport::createContentTrack (*edit, "FrameTest", false);
                    auto ext = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *types[0]);
                    t->pluginList.insertPlugin (ext, 0, nullptr);
                    for (int i = 0; i < 100; ++i)
                    {
                        auto* e2 = dynamic_cast<te::ExternalPlugin*> (ext.get());
                        if (e2 == nullptr || e2->getAudioPluginInstance() != nullptr) break;
                        if (i == 0) e2->forceFullReinitialise();
                        MessageManager::getInstance()->runDispatchLoopUntil (50);
                    }
                    if (auto* ep = dynamic_cast<te::ExternalPlugin*> (ext.get()); ep != nullptr && ep->getAudioPluginInstance() != nullptr)
                    {
                        PulseLookAndFeel laf;
                        LookAndFeel::setDefaultLookAndFeel (&laf);
                        {
                            ExternalEditorComponent frame (*ep);
                            frame.showParameters (true);
                            const int all = frame.numKnobs();
                            const auto firstName = frame.knobName (0);
                            frame.setSearch (firstName.substring (0, 3));
                            const int filtered = frame.numKnobs();
                            frame.setSearch ({});

                            auto* pi = ep->getAudioPluginInstance();
                            auto values = [pi] { std::vector<float> v; for (auto* prm : pi->getParameters()) v.push_back (prm->getValue()); return v; };
                            const auto before = values();
                            frame.randomise (1.0f);
                            const bool changed = values() != before;
                            frame.undoRandomise();
                            auto near = [] (const std::vector<float>& a, const std::vector<float>& b)
                            {
                                if (a.size() != b.size()) return false;
                                for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i] - b[i]) > 0.01f) return false;
                                return true;
                            };
                            const bool restored = near (values(), before);
                            // A / B: change B, switch back to A = original
                            frame.toggleAB();                      // now B (starts as A's copy is not set: B empty -> stays)
                            frame.randomise (1.0f);
                            const auto bValues = values();
                            frame.toggleAB();                      // back to A
                            const bool aBack = near (values(), before);
                            frame.toggleAB();                      // B again
                            const bool bBack = near (values(), bValues);
                            auto img = frame.createComponentSnapshot (frame.getLocalBounds());
                            auto f = dir.getChildFile ("plugin_frame.png");
                            f.deleteFile();
                            if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                            ok = all > 0 && filtered >= 1 && filtered < all && changed && restored && aBack && bBack;
                            info << all << " knobs, search " << filtered << ", randomise " << (changed ? "changed" : "same") << " / undo "
                                 << (restored ? "ok" : "no") << ", A/B " << (aBack ? "a" : "-") << (bBack ? "b" : "-");
                        }
                        LookAndFeel::setDefaultLookAndFeel (nullptr);
                    }
                    else info = "plugin did not load";
                    ext = nullptr;
                    edit->deleteTrack (t);
                }
                rep.check (ok, "Plugin frame (parameters, search, randomise + undo, A/B)", info);
            }
        }

        // --- background export -------------------------------------------------------------------------
        {
            auto& br = BackgroundRender::instance();
            auto bgTrack = ClipImport::createContentTrack (*edit, "BgRender", false);
            ClipImport::importFile (*bgTrack, *edit, writeTestWav (dir.getChildFile ("bg.wav"), 2.0, 180.0), te::TimePosition());

            auto makeParams = [&] (const File& f)
            {
                te::Renderer::Parameters p (*edit);
                p.tracksToDo = te::toBitSet (juce::Array<te::Track*> { bgTrack });
                p.destFile = f;
                p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
                p.bitDepth = 24;
                p.sampleRateForAudio = 48000.0;
                p.time = { te::TimePosition(), te::TimePosition::fromSeconds (2.0) };
                p.usePlugins = true;
                p.useMasterPlugins = false;
                return p;
            };

            auto out = dir.getChildFile ("background.wav");
            out.deleteFile();
            int doneOk = -1; bool doneCancelled = true;
            BackgroundRender::Job job;
            job.name = "Self-test export";
            job.renders.push_back (makeParams (out));
            job.onDone = [&] (int ok, int, bool c) { doneOk = ok; doneCancelled = c; };
            const bool started = br.start (std::move (job));
            bool playBlocked = false;
            if (started)
            {
                EngineHelpers::togglePlay (*edit);
                playBlocked = ! edit->getTransport().isPlaying();
            }
            for (int i = 0; i < 1200 && doneOk < 0; ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (25);
            rep.check (started && playBlocked && doneOk == 1 && ! doneCancelled && peakDb (engine, out) > -40.0f,
                       "Background export (app stays responsive, playback waits)", String (peakDb (engine, out), 1) + " dBFS");

            // cancel
            int cancelOk = -1; bool cancelled = false;
            BackgroundRender::Job job2;
            job2.name = "Self-test cancel";
            for (int i = 0; i < 4; ++i) job2.renders.push_back (makeParams (dir.getChildFile ("cancel" + String (i) + ".wav")));
            job2.onDone = [&] (int ok, int, bool c) { cancelOk = ok; cancelled = c; };
            br.start (std::move (job2));
            br.cancel();
            for (int i = 0; i < 1200 && cancelOk < 0; ++i)
                MessageManager::getInstance()->runDispatchLoopUntil (25);
            rep.check (cancelled && cancelOk < 4 && ! br.isBusy(), "Cancel a background export", String (cancelOk) + " of 4 finished before cancelling");
        }

        // --- workflow: command registry, group freeze/flatten, capture MIDI --------------------------
        {
            auto& props = engine.getPropertyStorage().getPropertiesFile();
            CommandRegistry reg (props);
            int dupRuns = 0, zoomRuns = 0;
            reg.add ("test.dup", "Duplicate", "Edit", KeyPress ('D', ModifierKeys::commandModifier, 0), [&] { ++dupRuns; });
            reg.add ("test.zoom", "Zoom", "View", KeyPress ('Z', {}, 0), [&] { ++zoomRuns; });
            reg.resetAll();
            const bool ctrlCode = reg.dispatch (KeyPress (4, ModifierKeys::commandModifier, 0));   // Windows sends Ctrl+D as code 4
            const bool lower = reg.dispatch (KeyPress ('z', {}, 0));
            reg.setKey ("test.zoom", KeyPress ('Q', ModifierKeys::shiftModifier, 0));
            const bool oldKeyGone = ! reg.dispatch (KeyPress ('z', {}, 0));
            const bool newKey = reg.dispatch (KeyPress ('q', ModifierKeys::shiftModifier, 0));
            auto conflict = reg.commandUsing (KeyPress ('D', ModifierKeys::commandModifier, 0), "test.zoom");
            reg.resetAll();
            rep.check (ctrlCode && lower && oldKeyGone && newKey && dupRuns == 1 && zoomRuns == 2 && conflict != nullptr && conflict->id == "test.dup",
                       "Command registry (custom shortcuts, Ctrl+letter codes, conflicts)");

            // group freeze / unfreeze / flatten
            auto g1 = ClipImport::createContentTrack (*edit, "GroupA", false);
            auto g2 = ClipImport::createContentTrack (*edit, "GroupB", false);
            ClipImport::importFile (*g1, *edit, writeTestWav (dir.getChildFile ("ga.wav"), 1.0, 150.0), te::TimePosition());
            ClipImport::importFile (*g2, *edit, writeTestWav (dir.getChildFile ("gb.wav"), 1.0, 250.0), te::TimePosition());
            auto folder = EditingOps::groupTracks (*edit, { g1, g2 }, "FreezeMe");
            bool froze = false, unfroze = false, flat = false;
            String detail;
            if (folder != nullptr)
            {
                auto frozen = EditingOps::freezeGroup (*folder);
                float lvl = -200.0f;
                if (frozen != nullptr && ! frozen->getClips().isEmpty())
                    if (auto w = dynamic_cast<te::WaveAudioClip*> (frozen->getClips()[0])) lvl = peakDb (engine, w->getOriginalFile());
                froze = frozen != nullptr && folder->isMuted (false) && EditingOps::isGroupFrozen (*folder) && lvl > -40.0f;
                detail = "frozen at " + String (lvl, 1) + " dBFS";
                EditingOps::unfreezeGroup (*folder);
                unfroze = ! EditingOps::isGroupFrozen (*folder) && ! folder->isMuted (false);
                const int before = te::getAudioTracks (*edit).size();
                auto flatTrack = EditingOps::flattenGroup (*folder);
                flat = flatTrack != nullptr && te::getAudioTracks (*edit).size() == before - 1 && flatTrack->getClips().size() == 1;
            }
            rep.check (froze && unfroze && flat, "Group freeze, unfreeze and flatten", detail);

            // capture MIDI from a (virtual) keyboard
            auto& dm = engine.getDeviceManager();
            dm.createVirtualMidiDevice ("Pulse Capture Test");
            te::VirtualMidiInputDevice* vmi = nullptr;
            for (int i = 0; i < 120 && vmi == nullptr; ++i)   // devices are registered asynchronously
            {
                for (auto& d : dm.getMidiInDevices())
                    if (d->getName() == "Pulse Capture Test") vmi = dynamic_cast<te::VirtualMidiInputDevice*> (d.get());
                if (vmi == nullptr) MessageManager::getInstance()->runDispatchLoopUntil (25);
            }
            int captured = 0;
            String captureInfo;
            if (vmi != nullptr)
            {
                vmi->setEnabled (true);
                vmi->updateRetrospectiveBufferLength (30.0);
                auto capTrack = ClipImport::createContentTrack (*edit, "Capture", true);
                edit->getTransport().ensureContextAllocated();
                edit->getTransport().ensureContextAllocated (true);
                EngineHelpers::routeMidiInputsTo (*capTrack);
                for (int i = 0; i < 4; ++i)
                {
                    vmi->handleIncomingMidiMessage (MidiMessage::noteOn (1, 60 + i, (uint8) 100), te::MPESourceID {});
                    Thread::sleep (20);
                    vmi->handleIncomingMidiMessage (MidiMessage::noteOff (1, 60 + i), te::MPESourceID {});
                }
                for (auto* inst : edit->getAllInputDevices())
                    if (&inst->owner == vmi)
                        captureInfo << "instance, targets " << inst->getTargets().size() << ", ";
                captureInfo << "buffer " << (vmi->getRetrospectiveMidiBuffer() != nullptr ? "yes" : "no");
                for (auto* inst : edit->getAllInputDevices())
                    if (&inst->owner == vmi)
                        for (auto c : inst->applyRetrospectiveRecord (false))
                            if (auto mc = dynamic_cast<te::MidiClip*> (c)) captured += mc->getSequence().getNumNotes();
                dm.deleteVirtualMidiDevice (*vmi);
            }
            rep.check (captured == 4, "Capture MIDI (recover what was just played)", String (captured) + " notes captured; " + captureInfo);
        }

        // --- automation: curves, modulators, clip envelopes ---------------------------------------------
        {
            auto autoTrack = ClipImport::createContentTrack (*edit, "Automation", false);
            auto wtP = edit->getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {});
            autoTrack->pluginList.insertPlugin (wtP, 0, nullptr);
            auto* wt = dynamic_cast<pulse::WavetableSynth*> (wtP.get());

            // curved segment
            auto& cv = wt->param (pulse::WavetableSynth::pCutoff)->getCurve();
            cv.addPoint (te::TimePosition(), 0.0f, 0.0f, nullptr);
            cv.addPoint (te::TimePosition::fromSeconds (2.0), 20000.0f, 0.0f, nullptr);
            const float linearMid = cv.getValueAt (te::TimePosition::fromSeconds (1.0), 0.0f);
            cv.setCurveValue (0, 0.8f, nullptr);
            const float curvedMid = cv.getValueAt (te::TimePosition::fromSeconds (1.0), 0.0f);
            rep.check (std::abs (curvedMid - linearMid) > 500.0f, "Curved automation segments",
                       "midpoint " + String (linearMid, 0) + " -> " + String (curvedMid, 0));

            // modulator
            auto lfo = Modulators::add (*autoTrack, te::IDs::LFO);
            auto* pos = wt->param (pulse::WavetableSynth::pPos1);
            bool modOk = false;
            if (lfo != nullptr)
            {
                Modulators::setAmount (*pos, *lfo, 0.5f);
                modOk = Modulators::isModulated (*pos) && std::abs (Modulators::amountOf (*pos, *lfo) - 0.5f) < 1.0e-4f
                          && Modulators::targetsOf (*autoTrack, *lfo).size() == 1 && Modulators::nameOf (*autoTrack, *lfo) == "LFO 1";
                Modulators::setAmount (*pos, *lfo, 0.0f);
                modOk = modOk && ! Modulators::isModulated (*pos);
            }
            rep.check (modOk, "LFO modulator assigned to a knob (amount, targets, remove)");

            // clip envelope on the wavetable position
            auto envClip = autoTrack->insertMIDIClip ("Env", { bars (2), bars (3) }, nullptr);
            ClipEnvelopes::Target target { wt->itemID.toString(), pos->paramID, "Position" };
            ClipEnvelopes::setPoint (*envClip, target, 0.0, 0.0f, 0.05, nullptr);
            ClipEnvelopes::setPoint (*envClip, target, 4.0, 1.0f, 0.05, nullptr);
            auto env = ClipEnvelopes::findEnv (*envClip, target);
            auto mid = ClipEnvelopes::valueAt (env, 2.0);
            auto beat = ClipEnvelopes::playingBeat (*envClip, bars (2) + te::TimeDuration::fromSeconds (0.5), false);   // 1 beat in at 120 BPM
            auto resolved = ClipEnvelopes::resolve (*autoTrack, env);
            if (resolved != nullptr && beat) resolved->setNormalisedParameter (*ClipEnvelopes::valueAt (env, *beat), dontSendNotification);
            rep.check (mid && std::abs (*mid - 0.5f) < 1.0e-4f && beat && std::abs (*beat - 1.0) < 1.0e-3 && resolved == pos
                         && std::abs (pos->getCurrentNormalisedValue() - 0.25f) < 0.01f,
                       "Clip envelope drives a device parameter",
                       beat ? "beat " + String (*beat, 3) + ", value " + String (pos->getCurrentNormalisedValue(), 3) : String ("not playing"));
            envClip = nullptr;
            lfo = nullptr;
        }

        // --- audio editing: buffer ops, sample editor, slicing, warp modes ---------------------------------
        {
            AudioBuffer<float> b (1, 1000);
            for (int i = 0; i < 1000; ++i) b.setSample (0, i, (float) i / 1000.0f * 0.5f);
            auto rev = b; SampleEdit::reverse (rev, { 0, 1000 });
            auto cutB = b; SampleEdit::cut (cutB, { 100, 300 });
            auto norm = b; SampleEdit::normalise (norm, { 0, 1000 }, 0.0f);
            auto fd = b; SampleEdit::fade (fd, { 0, 1000 }, true);
            rep.check (std::abs (rev.getSample (0, 0) - b.getSample (0, 999)) < 1.0e-6f && cutB.getNumSamples() == 800
                         && std::abs (cutB.getSample (0, 100) - b.getSample (0, 300)) < 1.0e-6f
                         && std::abs (norm.getMagnitude (0, 1000) - 1.0f) < 1.0e-3f && std::abs (fd.getSample (0, 0)) < 1.0e-6f,
                       "Sample edits (reverse, cut, normalise, fade)");

            auto edTrack = ClipImport::createContentTrack (*edit, "SampleEdit", false);
            auto srcWav = writeTestWav (dir.getChildFile ("toedit.wav"), 2.0, 220.0);
            te::Clip::Ptr ec = ClipImport::importFile (*edTrack, *edit, srcWav, te::TimePosition());
            bool applied = false;
            String applyInfo;
            if (auto w = dynamic_cast<te::WaveAudioClip*> (ec.get()))
            {
                SampleEditor ed (*w);
                const int n = ed.getAudio().getNumSamples();
                ed.setSelection ({ 0, n / 2 });
                ed.runEdit ([] (auto& buf, auto r) { SampleEdit::cut (buf, r); }, true);
                ed.setSelection ({});
                ed.runEdit ([] (auto& buf, auto r) { SampleEdit::normalise (buf, r, -1.0f); });
                ed.apply();
                auto newFile = w->getOriginalFile();
                applied = newFile != srcWav && newFile.existsAsFile() && srcWav.existsAsFile()
                          && std::abs (w->getPosition().getLength().inSeconds() - 1.0) < 0.02
                          && std::abs (peakDb (engine, newFile) + 1.0f) < 0.1f;
                applyInfo = newFile.getFileName() + ", " + String (w->getPosition().getLength().inSeconds(), 2) + " s, "
                            + String (peakDb (engine, newFile), 2) + " dBFS";
            }
            rep.check (applied, "Sample editor: cut + normalise applied as a new file (original kept)", applyInfo);
            ec = nullptr;

            // slicing: four kicks and four hats -> 8 slices on a Drum Rack
            const double sr = 44100.0;
            AudioBuffer<float> beat (1, (int) (sr * 2.0));
            beat.clear();
            Random rng (7);
            for (int k = 0; k < 4; ++k)
            {
                const int k0 = (int) (k * 0.5 * sr), h0 = (int) ((k * 0.5 + 0.25) * sr);
                for (int i = 0; i < (int) (0.2 * sr); ++i)
                {
                    const double t = i / sr;
                    beat.addSample (0, k0 + i, (float) (0.9 * std::exp (-t * 18.0) * std::sin (MathConstants<double>::twoPi * (55.0 + 80.0 * std::exp (-t * 40.0)) * t)));
                }
                float prev = 0;
                for (int i = 0; i < (int) (0.06 * sr); ++i)
                {
                    const float nz = rng.nextFloat() * 2.0f - 1.0f;
                    beat.addSample (0, h0 + i, (nz - prev) * 0.35f * (float) std::exp (-i / sr * 60.0));
                    prev = nz;
                }
            }
            auto beatFile = dir.getChildFile ("beat.wav");
            SampleEdit::writeWav (beatFile, beat, sr);
            auto beatTrack = ClipImport::createContentTrack (*edit, "Beat", false);
            te::Clip::Ptr bc = ClipImport::importFile (*beatTrack, *edit, beatFile, bars (1));
            int slices = 0, notes = 0;
            bool padLoaded = false;
            if (auto w = dynamic_cast<te::WaveAudioClip*> (bc.get()))
            {
                auto r = SampleEdit::sliceToDrumRack (*w);
                slices = r.slices;
                if (r.track != nullptr)
                {
                    if (auto mc = dynamic_cast<te::MidiClip*> (r.track->getClips().getFirst())) notes = mc->getSequence().getNumNotes();
                    if (auto rack = DrumRack::rackOf (DrumRack::findOnTrack (*r.track))) padLoaded = DrumRack::padName (*rack, DrumRack::baseNote).isNotEmpty();
                }
            }
            rep.check (slices == 8 && notes == 8 && padLoaded, "Slice audio to a Drum Rack at transients",
                       String (slices) + " slices, " + String (notes) + " notes");

            // warp modes
            auto modes = SampleEdit::warpModes (engine);
            bool warpOk = false;
            if (auto w = dynamic_cast<te::WaveAudioClip*> (bc.get()); w != nullptr && modes.size() > 1)
            {
                SampleEdit::setWarpMode (*w, modes[modes.size() - 1]);
                warpOk = SampleEdit::currentWarpMode (*w) == modes[modes.size() - 1];
            }
            rep.check (warpOk, "Per-clip warp mode", modes.joinIntoString (", "));
            bc = nullptr;
        }

        // --- Drum Rack v2: synth kit, choke, layers, sends, mute / solo, remap, move / copy, extract, macros ---
        {
            rep.step ("drum rack v2");
            using DP = pulse::DrumPad;
            using PO = pulse::PadOut;

            // peak (dBFS) of a rendered file between two times
            auto windowPeak = [&engine] (const File& f, double t0, double t1)
            {
                std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                if (r == nullptr) return -200.0f;
                const int64 a = (int64) (t0 * r->sampleRate), b = jmin (r->lengthInSamples, (int64) (t1 * r->sampleRate));
                if (b <= a) return -200.0f;
                Range<float> ranges[2];
                r->readMaxLevels (a, b - a, ranges, (int) jmin (2u, r->numChannels));
                float pk = 0;
                for (auto& rg : ranges) pk = jmax (pk, std::abs (rg.getStart()), std::abs (rg.getEnd()));
                return Decibels::gainToDecibels (pk, -200.0f);
            };

            struct DrumTrack { te::AudioTrack* track = nullptr; te::RackType* rack = nullptr; te::MidiClip* clip = nullptr; };
            auto makeDrumTrack = [&] (const String& name, bool kit) -> DrumTrack
            {
                DrumTrack d;
                d.track = ClipImport::createContentTrack (*edit, name, false);
                if (d.track == nullptr) return d;
                auto rp = DrumRack::create (*edit, kit);
                d.track->pluginList.insertPlugin (rp, 0, nullptr);
                d.rack = DrumRack::rackOf (rp.get());
                d.clip = d.track->insertMIDIClip (name, { te::TimePosition(), te::TimePosition::fromSeconds (4.0) }, nullptr).get();
                return d;
            };
            // notes are placed in seconds (the test edit's tempo may have changed)
            auto noteAt = [&] (DrumTrack& d, int note, double sec, double lenSec = 0.1, int vel = 110)
            {
                const double b0 = EngineHelpers::timeToBeat (*edit, te::TimePosition::fromSeconds (sec));
                const double b1 = EngineHelpers::timeToBeat (*edit, te::TimePosition::fromSeconds (sec + lenSec));
                d.clip->getSequence().addNote (note, te::BeatPosition::fromBeats (b0), te::BeatDuration::fromBeats (jmax (0.01, b1 - b0)), vel, 0, nullptr);
            };
            auto render = [&] (DrumTrack& d, double secs, const String& n)
            {
                return EditingOps::renderTrack (*d.track, { te::TimePosition(), te::TimePosition::fromSeconds (secs) }, n);
            };
            auto tone = writeTestWav (dir.getChildFile ("drum_tone.wav"), 2.0, 220.0);            // 2 s steady, -6 dBFS
            auto click = writeTestWav (dir.getChildFile ("drum_click.wav"), 0.05, 1000.0);

            // 1. synth kit
            {
                auto d = makeDrumTrack ("DrumKit", true);
                if (d.rack != nullptr)
                {
                    noteAt (d, 36, 0.0); noteAt (d, 38, 0.5); noteAt (d, 42, 1.0); noteAt (d, 39, 1.5);
                    auto f = render (d, 2.0, "kit");
                    const float k = windowPeak (f, 0.0, 0.45), sn = windowPeak (f, 0.5, 0.95), hh = windowPeak (f, 1.0, 1.1), cl = windowPeak (f, 1.5, 1.9);
                    rep.check (DrumRack::usedPads (*d.rack).size() == 11 && k > -24 && sn > -24 && hh > -36 && cl > -30,
                               "Drum Rack synth kit (kick, snare, hat, clap synthesised)",
                               String (DrumRack::usedPads (*d.rack).size()) + " pads; " + String (k, 1) + " / " + String (sn, 1) + " / " + String (hh, 1) + " / " + String (cl, 1) + " dB");
                }
                else rep.check (false, "Drum Rack synth kit");
            }

            // 2. choke groups: a long tone on 46 is cut by a click on 42 when both are in group 1
            {
                auto d = makeDrumTrack ("DrumChoke", false);
                float choked = 0, open = 0;
                if (d.rack != nullptr)
                {
                    DrumRack::loadSample (*d.rack, 46, tone);
                    DrumRack::loadSample (*d.rack, 42, click);
                    DrumRack::source (*d.rack, 46)->set (DP::pChoke, 1.0f);
                    DrumRack::source (*d.rack, 42)->set (DP::pChoke, 1.0f);
                    noteAt (d, 46, 0.0, 1.5);
                    noteAt (d, 42, 0.5);
                    choked = windowPeak (render (d, 1.5, "choked"), 0.7, 1.4);
                    DrumRack::source (*d.rack, 42)->set (DP::pChoke, 0.0f);
                    open = windowPeak (render (d, 1.5, "unchoked"), 0.7, 1.4);
                }
                rep.check (choked < -60.0f && open > -12.0f, "Choke group cuts the other pad off", String (choked, 1) + " dB choked vs " + String (open, 1) + " dB without");
            }

            // 3. velocity layers and round robin
            {
                auto d = makeDrumTrack ("DrumLayers", false);
                auto quiet = dir.getChildFile ("drum_quiet.wav");
                {
                    AudioBuffer<float> b (2, 22050);
                    for (int i = 0; i < b.getNumSamples(); ++i) { auto v = 0.05f * std::sin ((float) i * 0.03f); b.setSample (0, i, v); b.setSample (1, i, v); }
                    SampleEdit::writeWav (quiet, b, 44100.0);
                }
                float soft = 0, hard = 0, rr1 = 0, rr2 = 0;
                if (d.rack != nullptr)
                {
                    DrumRack::addLayer (*d.rack, 40, quiet);
                    DrumRack::addLayer (*d.rack, 40, tone);
                    auto pad = DrumRack::source (*d.rack, 40);
                    pad->spreadVelocities();   // quiet = 1..63, tone = 64..127
                    pad->set (DP::pVelSens, 0.0f);
                    noteAt (d, 40, 0.0, 0.2, 30);
                    noteAt (d, 40, 0.6, 0.2, 120);
                    auto f = render (d, 1.2, "layers");
                    soft = windowPeak (f, 0.05, 0.4); hard = windowPeak (f, 0.65, 1.0);
                    pad->set (DP::pLayerMode, 1.0f);   // round robin: alternates whatever the velocity
                    for (auto n : d.clip->getSequence().getNotes()) n->setVelocity (100, nullptr);
                    auto g = render (d, 1.2, "roundrobin");
                    rr1 = windowPeak (g, 0.05, 0.4); rr2 = windowPeak (g, 0.65, 1.0);
                }
                rep.check (hard - soft > 15.0f, "Velocity layers pick the sample by velocity", String (soft, 1) + " dB soft, " + String (hard, 1) + " dB hard");
                rep.check (std::abs (rr1 - rr2) > 15.0f, "Round-robin layers alternate", String (rr1, 1) + " / " + String (rr2, 1) + " dB");
            }

            // 4. sends (post-fader): send A at 0 dB doubles the pad through Return A; muting the return removes it
            {
                auto d = makeDrumTrack ("DrumSends", false);
                float viaSend = -200, noSend = 0, retMuted = 0;
                if (d.rack != nullptr)
                {
                    DrumRack::loadSample (*d.rack, 36, tone);
                    auto out = DrumRack::output (*d.rack, 36);
                    noteAt (d, 36, 0.0, 1.0);
                    noSend = windowPeak (render (d, 1.0, "nosend"), 0.1, 0.9);
                    out->set (PO::pSendA, 0.0f);
                    viaSend = windowPeak (render (d, 1.0, "send"), 0.1, 0.9);
                    DrumRack::output (*d.rack, DrumRack::returnA)->set (PO::pMute, 1.0f);
                    retMuted = windowPeak (render (d, 1.0, "retmuted"), 0.1, 0.9);
                }
                rep.check (viaSend - noSend > 5.0f && std::abs (retMuted - noSend) < 0.5f, "Pad sends reach the rack returns",
                           String (noSend, 1) + " dB dry, " + String (viaSend, 1) + " dB with send A, " + String (retMuted, 1) + " dB return muted");
            }

            // 5. mute / solo, 6. receive note remap
            {
                auto d = makeDrumTrack ("DrumMix", false);
                float soloA = 0, soloB = 0, muted = 0, remapOld = 0, remapNew = 0;
                if (d.rack != nullptr)
                {
                    DrumRack::loadSample (*d.rack, 36, tone);
                    DrumRack::loadSample (*d.rack, 38, tone);
                    for (int sl : { 36, 38 }) DrumRack::source (*d.rack, sl)->set (DP::pPlayMode, 1.0f);   // gate: stops at note-off
                    noteAt (d, 36, 0.0, 0.4);
                    noteAt (d, 38, 0.6, 0.4);
                    DrumRack::output (*d.rack, 36)->set (PO::pSolo, 1.0f);
                    auto f = render (d, 1.2, "solo");
                    soloA = windowPeak (f, 0.05, 0.3); soloB = windowPeak (f, 0.65, 0.9);
                    DrumRack::output (*d.rack, 36)->set (PO::pSolo, 0.0f);
                    DrumRack::output (*d.rack, 38)->set (PO::pMute, 1.0f);
                    muted = windowPeak (render (d, 1.2, "mute"), 0.65, 0.9);
                    DrumRack::output (*d.rack, 38)->set (PO::pMute, 0.0f);

                    DrumRack::source (*d.rack, 38)->set (DP::pNote, 60.0f);   // pad 38 now listens to C3
                    noteAt (d, 60, 1.4, 0.4);
                    auto g = render (d, 2.0, "remap");
                    remapOld = windowPeak (g, 0.65, 0.9); remapNew = windowPeak (g, 1.45, 1.7);
                }
                rep.check (soloA > -12.0f && soloB < -80.0f && muted < -80.0f, "Pad solo and mute", String (soloA, 1) + " / " + String (soloB, 1) + " / " + String (muted, 1) + " dB");
                rep.check (remapOld < -80.0f && remapNew > -12.0f && d.rack != nullptr && DrumRack::padForNote (*d.rack, 60) == 38,
                           "Pad receive note remapping", String (remapOld, 1) + " dB old note, " + String (remapNew, 1) + " dB new note");
            }

            // 7. move / swap / copy pads, 8. extract a pad to its own track
            {
                auto d = makeDrumTrack ("DrumEdit", false);
                bool moved = false, swapped = false, copied = false, extracted = false;
                String info;
                if (d.rack != nullptr)
                {
                    DrumRack::loadSample (*d.rack, 36, tone);
                    DrumRack::loadSynth (*d.rack, 38, pulse::DrumVoice::snare);
                    DrumRack::addEffect (*d.rack, 38, edit->getPluginCache().createNewPlugin (te::LowPassPlugin::xmlTypeName, {}));
                    DrumRack::movePad (*d.rack, 36, 40);
                    moved = DrumRack::source (*d.rack, 36) == nullptr && DrumRack::padName (*d.rack, 40) == "drum_tone"
                            && DrumRack::noteOfPad (*d.rack, 40) == 40;
                    DrumRack::movePad (*d.rack, 40, 38);   // swap with the snare
                    swapped = DrumRack::padName (*d.rack, 38) == "drum_tone" && DrumRack::source (*d.rack, 40) != nullptr
                              && DrumRack::source (*d.rack, 40)->isSynth() && DrumRack::chain (*d.rack, 40).size() == 1;
                    DrumRack::copyPad (*d.rack, 40, 41);
                    copied = DrumRack::source (*d.rack, 41) != nullptr && DrumRack::source (*d.rack, 41)->isSynth()
                             && DrumRack::chain (*d.rack, 41).size() == 1 && DrumRack::source (*d.rack, 40) != nullptr
                             && DrumRack::noteOfPad (*d.rack, 41) == 41;

                    noteAt (d, 38, 0.0); noteAt (d, 41, 0.5); noteAt (d, 41, 1.0);
                    const int tracksBefore = EngineHelpers::getUserTracks (*edit).size();
                    auto nt = DrumRack::extractToTrack (*d.track, *d.rack, 41);
                    int newNotes = 0, oldNotes = d.clip->getSequence().getNumNotes();
                    bool hasPad = false;
                    if (nt != nullptr)
                    {
                        for (auto c : nt->getClips()) if (auto mc = dynamic_cast<te::MidiClip*> (c)) newNotes += mc->getSequence().getNumNotes();
                        hasPad = nt->pluginList.findFirstPluginOfType<pulse::DrumPad>() != nullptr;
                    }
                    const float pk = nt != nullptr ? peakDb (engine, EditingOps::renderTrack (*nt, { te::TimePosition(), te::TimePosition::fromSeconds (1.5) }, "extracted")) : -200.0f;
                    extracted = nt != nullptr && EngineHelpers::getUserTracks (*edit).size() == tracksBefore + 1 && hasPad && newNotes == 2
                                && oldNotes == 1 && DrumRack::source (*d.rack, 41) == nullptr && pk > -30.0f;
                    info = String (newNotes) + " notes moved, " + String (oldNotes) + " left, " + String (pk, 1) + " dB";
                }
                rep.check (moved && swapped && copied, "Move, swap and copy pads (with their effects)",
                           String ((int) moved) + String ((int) swapped) + String ((int) copied));
                rep.check (extracted, "Extract a pad to its own track (sound, effects, notes)", info);
            }

            // 9. rack macros
            {
                auto d = makeDrumTrack ("DrumMacro", false);
                bool ok = false;
                String info;
                if (d.rack != nullptr)
                {
                    DrumRack::loadSample (*d.rack, 36, tone);
                    DrumRack::ensureMacros (*d.rack);
                    auto ms = DrumRack::macros (*d.rack);
                    auto target = DrumRack::source (*d.rack, 36)->param (DP::pCutoff);
                    target->setParameter (1000.0f, sendNotificationSync);
                    if (ms.size() == DrumRack::numMacros)
                    {
                        Macros::toggleMapping (*target, *ms[0]);
                        ms[0]->setParameter (1.0f, sendNotificationSync);
                        target->updateStream();
                        target->updateFromAutomationSources (te::TimePosition());
                        ok = Macros::isMapped (*target, *ms[0]) && target->getCurrentValue() > 1100.0f;
                        info = String (ms.size()) + " macros, cutoff " + String (target->getCurrentValue(), 0) + " Hz";
                    }
                }
                rep.check (ok, "Rack macros drive pad parameters", info);
            }

            // 10. migrating an old (16 Samplers) rack, 11. randomise kit, 12. bank view
            {
                auto t = ClipImport::createContentTrack (*edit, "DrumOld", false);
                auto rt = edit->getRackList().addNewRack();
                bool migrated = false;
                if (rt != nullptr && t != nullptr)
                {
                    rt->state.setProperty (DrumRack::rackFlag, true, nullptr);
                    rt->addOutput (-1, "Left"); rt->addOutput (-1, "Right");
                    auto smp = edit->getPluginCache().createNewPlugin (te::SamplerPlugin::xmlTypeName, {});
                    smp->state.setProperty (DrumRack::padProp, 2, nullptr);
                    rt->addPlugin (smp, { 0.2f, 0.2f }, false);
                    if (auto sp = dynamic_cast<te::SamplerPlugin*> (smp.get()))
                    {
                        sp->addSound (tone.getFullPathName(), "tone", 0.0, 2.0, 0.0f);
                        sp->setSoundParams (0, 38, 38, 38);
                        sp->flushPendingUpdates();
                    }
                    t->pluginList.insertPlugin (edit->getPluginCache().createNewPlugin (te::RackInstance::create (*rt)), 0, nullptr);
                    DrumRack::migrateAll (*edit);
                    bool samplersLeft = false;
                    for (auto p : rt->getPlugins()) samplersLeft |= dynamic_cast<te::SamplerPlugin*> (p) != nullptr;
                    migrated = ! samplersLeft && DrumRack::padName (*rt, 38) == "drum_tone" && DrumRack::output (*rt, DrumRack::returnA) != nullptr;
                }
                rep.check (migrated, "Old Drum Racks are upgraded on load");

                auto d = makeDrumTrack ("DrumRandom", false);
                int got = -1;
                if (d.rack != nullptr)
                {
                    Array<File> lib { writeTestWav (dir.getChildFile ("Kick 01.wav"), 0.2, 60.0, 44100.0, true),
                                      writeTestWav (dir.getChildFile ("snare_tight.wav"), 0.2, 200.0, 44100.0, true),
                                      writeTestWav (dir.getChildFile ("closed hat.wav"), 0.1, 6000.0, 44100.0, true) };
                    Random rng (3);
                    got = DrumRack::randomiseKit (*d.rack, lib, rng);
                }
                rep.check (got == 3 && DrumRack::padName (*d.rack, 36) == "Kick 01" && DrumRack::padName (*d.rack, 38) == "snare_tight"
                             && DrumRack::source (*d.rack, 39) != nullptr && DrumRack::source (*d.rack, 39)->isSynth(),
                           "Randomise kit (library samples, synth for the rest)", String (got) + " from files");

                DrumRack::setBank (*d.rack, 200);
                const int hi = DrumRack::getBank (*d.rack);
                DrumRack::setBank (*d.rack, 37);
                rep.check (hi == 112 && DrumRack::getBank (*d.rack) == 36, "128 pads in banks", String (hi));
            }

            // 13. standalone Drum Synth
            {
                auto t = ClipImport::createContentTrack (*edit, "DrumSynth", false);
                float pk = -200;
                if (t != nullptr)
                {
                    t->pluginList.insertPlugin (edit->getPluginCache().createNewPlugin (pulse::DrumSynth::xmlTypeName, {}), 0, nullptr);
                    auto mc = t->insertMIDIClip ("DS", { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, nullptr);
                    for (int i = 0; i < 4; ++i)
                        mc->getSequence().addNote (i % 2 == 0 ? 36 : 38, te::BeatPosition::fromBeats (i * 0.5), te::BeatDuration::fromBeats (0.25), 110, 0, nullptr);
                    pk = peakDb (engine, EditingOps::renderTrack (*t, { te::TimePosition(), te::TimePosition::fromSeconds (1.5) }, "drumsynth"));
                }
                rep.check (pk > -24.0f, "Drum Synth instrument (GM kit)", String (pk, 1) + " dBFS");
            }

            // 15. Simpler multisample: root keys from file names, auto-mapped zones, each zone plays its sample
            {
                using S = pulse::Simpler;
                const bool names = S::rootFromName ("Piano C#3") == 61 && S::rootFromName ("Strings_A2") == 57
                                   && S::rootFromName ("vln_60") == 60 && S::rootFromName ("Bass Bb1") == 46 && S::rootFromName ("Pad") == -1;
                auto quietC3 = dir.getChildFile ("Keys C3.wav");
                {
                    AudioBuffer<float> b (2, 44100);
                    for (int i = 0; i < b.getNumSamples(); ++i) { auto v = 0.05f * std::sin ((float) i * 0.0313f); b.setSample (0, i, v); b.setSample (1, i, v); }
                    SampleEdit::writeWav (quietC3, b, 44100.0);
                }
                auto loudC5 = writeTestWav (dir.getChildFile ("Keys C5.wav"), 1.0, 1046.5);
                auto t = ClipImport::createContentTrack (*edit, "Multisample", false);
                float lo = 0, hi = 0;
                String zinfo;
                if (t != nullptr)
                {
                    auto pl = edit->getPluginCache().createNewPlugin (S::xmlTypeName, {});
                    t->pluginList.insertPlugin (pl, 0, nullptr);
                    if (auto sp = dynamic_cast<S*> (pl.get()))
                    {
                        sp->autoMap ({ loudC5, quietC3 });
                        for (int i = 0; i < sp->numZones(); ++i)
                        {
                            auto z = sp->getZone (i);
                            zinfo << z.file.getFileNameWithoutExtension() << " " << z.keyLo << "-" << z.keyHi << " root " << z.root << "; ";
                        }
                        sp->set (S::pVelSens, 0.0f);
                        auto mc = t->insertMIDIClip ("MS", { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, nullptr);
                        auto at = [&] (double sec) { return te::BeatPosition::fromBeats (EngineHelpers::timeToBeat (*edit, te::TimePosition::fromSeconds (sec))); };
                        mc->getSequence().addNote (64, at (0.0), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
                        mc->getSequence().addNote (80, at (0.8), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);
                        auto f = EditingOps::renderTrack (*t, { te::TimePosition(), te::TimePosition::fromSeconds (1.4) }, "multisample");
                        auto windowPk = [&engine, &f] (double a, double b)
                        {
                            std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                            if (r == nullptr) return -200.0f;
                            Range<float> rg[2];
                            r->readMaxLevels ((int64) (a * r->sampleRate), (int64) ((b - a) * r->sampleRate), rg, 2);
                            return Decibels::gainToDecibels (jmax (std::abs (rg[0].getStart()), std::abs (rg[0].getEnd())), -200.0f);
                        };
                        lo = windowPk (0.05, 0.2); hi = windowPk (0.85, 1.0);
                    }
                }
                rep.check (names, "Multisample: root keys read from file names");
                rep.check (hi - lo > 15.0f && zinfo.contains ("Keys C3 0-72 root 60") && zinfo.contains ("Keys C5 73-127 root 84"),
                           "Simpler multisample zones (auto-mapped)", zinfo + String (lo, 1) + " / " + String (hi, 1) + " dB");
            }

            // 14. the editor: every page builds (snapshots saved as drumrack_<page>.png in the test folder)
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                bool ok = false;
                int pages = 0;
                {
                    EditViewState evs (*edit, sm);
                    auto d = makeDrumTrack ("DrumUI", true);
                    if (d.rack != nullptr)
                    {
                        DrumRack::addLayer (*d.rack, 40, tone);
                        DrumRack::addLayer (*d.rack, 40, click);
                        DrumRack::addEffect (*d.rack, 40, edit->getPluginCache().createNewPlugin (te::LowPassPlugin::xmlTypeName, {}));
                        DrumRack::output (*d.rack, 42)->set (PO::pMute, 1.0f);
                        DrumRack::output (*d.rack, 38)->set (PO::pSolo, 1.0f);
                        DrumRackCard card (evs, *DrumRack::findOnTrack (*d.track));
                        card.setSize (card.getWidth(), 300);
                        for (int pg = 0; pg < 7; ++pg)
                        {
                            card.showPage (pg, pg == 0 || pg == 3 ? 40 : 38);
                            auto img = card.createComponentSnapshot (card.getLocalBounds());
                            auto f = dir.getChildFile ("drumrack_" + String (pg) + ".png");
                            f.deleteFile();
                            if (auto os = f.createOutputStream()) { PNGImageFormat png; if (png.writeImageToStream (img, *os)) ++pages; }
                        }
                        ok = pages == 7;
                    }
                }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
                rep.check (ok, "Drum Rack editor pages build", String (pages) + " pages");
            }
        }

        // --- Utility / Tuner / Spectrum -----------------------------------------------------------
        {
            rep.step ("utility devices");
            // stereo source: 220 Hz left, 330 Hz right
            auto stereo = dir.getChildFile ("stereo_lr.wav");
            {
                AudioBuffer<float> b (2, 44100);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    b.setSample (0, i, 0.5f * std::sin (MathConstants<float>::twoPi * 220.0f * (float) i / 44100.0f));
                    b.setSample (1, i, 0.5f * std::sin (MathConstants<float>::twoPi * 330.0f * (float) i / 44100.0f));
                }
                SampleEdit::writeWav (stereo, b, 44100.0);
            }
            auto ut = ClipImport::createContentTrack (*edit, "Utility", false);
            te::Clip::Ptr uc = ut != nullptr ? ClipImport::importFile (*ut, *edit, stereo, te::TimePosition()) : nullptr;
            auto up = edit->getPluginCache().createNewPlugin (pulse::Utility::xmlTypeName, {});
            if (ut != nullptr) ut->pluginList.insertPlugin (up, 0, nullptr);
            auto* util = dynamic_cast<pulse::Utility*> (up.get());

            // max |L - R| and peak of a render
            auto analyse = [&engine] (const File& f, float& diff, float& peak)
            {
                diff = 0; peak = 0;
                std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                if (r == nullptr || r->numChannels < 2) { diff = -1; return; }
                AudioBuffer<float> b (2, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                for (int i = (int) (0.1 * r->sampleRate); i < b.getNumSamples() - 100; ++i)
                {
                    diff = jmax (diff, std::abs (b.getSample (0, i) - b.getSample (1, i)));
                    peak = jmax (peak, std::abs (b.getSample (0, i)), std::abs (b.getSample (1, i)));
                }
            };
            float d0 = 0, p0 = 0, dMono = 0, pMono = 0, dLeft = 0, pGain = 0, dummy = 0;
            if (util != nullptr && uc != nullptr)
            {
                const te::TimeRange one { te::TimePosition(), te::TimePosition::fromSeconds (0.8) };
                analyse (EditingOps::renderTrack (*ut, one, "util-stereo"), d0, p0);
                util->set (pulse::Utility::pWidth, 0.0f);
                analyse (EditingOps::renderTrack (*ut, one, "util-mono"), dMono, pMono);
                util->set (pulse::Utility::pWidth, 1.0f);
                util->set (pulse::Utility::pChannels, 1.0f);
                analyse (EditingOps::renderTrack (*ut, one, "util-left"), dLeft, dummy);
                util->set (pulse::Utility::pChannels, 0.0f);
                util->set (pulse::Utility::pGain, -12.0f);
                analyse (EditingOps::renderTrack (*ut, one, "util-gain"), dummy, pGain);
                util->set (pulse::Utility::pGain, 0.0f);
            }
            const float gainDrop = Decibels::gainToDecibels (p0) - Decibels::gainToDecibels (pGain);
            rep.check (d0 > 0.3f && dMono < 0.001f && dLeft < 0.001f && std::abs (gainDrop - 12.0f) < 0.5f,
                       "Utility: width (mono), channel mode, gain",
                       "L-R " + String (d0, 3) + " -> " + String (dMono, 4) + " mono, " + String (dLeft, 4) + " left only; gain -" + String (gainDrop, 2) + " dB");

            // tuner pitch detection on a sine and a bright saw
            std::vector<float> sig (4096);
            for (int i = 0; i < 4096; ++i) sig[(size_t) i] = 0.4f * std::sin (MathConstants<float>::twoPi * 440.0f * (float) i / 48000.0f);
            const double a4 = pulse::Tuner::detectPitch (sig.data(), 4096, 48000.0);
            for (int i = 0; i < 4096; ++i) { const float ph = std::fmod (110.0f * (float) i / 48000.0f, 1.0f); sig[(size_t) i] = 0.4f * (2.0f * ph - 1.0f); }
            const double a2 = pulse::Tuner::detectPitch (sig.data(), 4096, 48000.0);
            std::fill (sig.begin(), sig.end(), 0.0f);
            const double none = pulse::Tuner::detectPitch (sig.data(), 4096, 48000.0);
            // the cards with a signal in them (snapshots: tuner.png / spectrum.png in the test folder)
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                int shots = 0;
                for (auto type : { pulse::Tuner::xmlTypeName, pulse::Spectrum::xmlTypeName, pulse::Reverb::xmlTypeName, pulse::Delay::xmlTypeName,
                                   pulse::FMSynth::xmlTypeName, pulse::PitchCorrect::xmlTypeName, pulse::Vocoder::xmlTypeName,
                                   pulse::WavetableSynth::xmlTypeName, pulse::Simpler::xmlTypeName, pulse::GlueCompressor::xmlTypeName,
                                   pulse::AutoFilter::xmlTypeName, pulse::Limiter::xmlTypeName, pulse::Saturator::xmlTypeName })
                {
                    auto pl = edit->getPluginCache().createNewPlugin (type, {});
                    if (ut != nullptr) ut->pluginList.insertPlugin (pl, -1, nullptr);
                    std::vector<float> l (8192), r (8192);
                    for (int i = 0; i < 8192; ++i)
                    {
                        const float t = (float) i / 44100.0f;
                        l[(size_t) i] = r[(size_t) i] = 0.3f * std::sin (MathConstants<float>::twoPi * 442.0f * t) + 0.1f * std::sin (MathConstants<float>::twoPi * 884.0f * t)
                                                        + 0.05f * std::sin (MathConstants<float>::twoPi * 3094.0f * t);
                    }
                    if (auto tu = dynamic_cast<pulse::Tuner*> (pl.get()))    tu->fifo.push (l.data(), r.data(), 8192);
                    if (auto sp = dynamic_cast<pulse::Spectrum*> (pl.get())) sp->fifo.push (l.data(), r.data(), 8192);
                    {
                        PluginCard card (evs, pl.get(), [] (te::Plugin&, int) {});
                        card.setSize (card.getWidth(), 250);
                        card.resized();
                        if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (pl.get()); pp != nullptr && String (type) == pulse::FMSynth::xmlTypeName)
                        {
                            // device tools: randomise leaves levels alone; reset and copy / paste
                            const float vol = pp->get (pulse::FMSynth::pVolume);
                            auto before = DeviceTools::capture (*pp);
                            Random rng (5);
                            DeviceTools::randomise (*pp, rng);
                            const bool changed = DeviceTools::capture (*pp) != before, keptVol = pp->get (pulse::FMSynth::pVolume) == vol;
                            DeviceTools::clipboard() = { pl->getPluginType(), DeviceTools::capture (*pp) };
                            DeviceTools::resetToDefaults (*pp);
                            const bool reset = std::abs (pp->get (pulse::FMSynth::pAlgorithm)) < 0.01f;
                            DeviceTools::apply (*pp, DeviceTools::clipboard().values);
                            const bool pasted = DeviceTools::capture (*pp) == DeviceTools::clipboard().values;
                            DeviceTools::apply (*pp, before);
                            rep.check (changed && keptVol && reset && pasted, "Device tools (randomise keeps levels, reset, copy / paste)");
                            card.setWidthAndNotify (card.getWidth());   // (exercise the API)
                        }
                        for (int k = 0; k < 3; ++k) card.createComponentSnapshot (card.getLocalBounds());   // let averaging settle
                        for (int pg = 1; pg < card.numPages(); ++pg)   // the other pages (graphs follow the page)
                        {
                            card.showPage (pg);
                            auto pimg = card.createComponentSnapshot (card.getLocalBounds());
                            auto pf = dir.getChildFile (String (type) + "_p" + String (pg) + ".png");
                            pf.deleteFile();
                            if (auto os = pf.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (pimg, *os); }
                        }
                        card.showPage (0);
                        auto img = card.createComponentSnapshot (card.getLocalBounds());
                        auto f = dir.getChildFile (String (type) + ".png");
                        f.deleteFile();
                        if (auto os = f.createOutputStream()) { PNGImageFormat png; if (png.writeImageToStream (img, *os)) ++shots; }
                    }
                    pl->deleteFromParent();
                }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
                rep.check (shots == 13, "Device cards draw (compact layout, 13 devices, every page)");
            }
            rep.check (std::abs (a4 - 440.0) < 1.0 && std::abs (a2 - 110.0) < 0.5 && none == 0.0
                         && std::abs (pulse::Tuner::noteOf (a4) - 69.0) < 0.02,
                       "Tuner pitch detection", String (a4, 2) + " Hz, " + String (a2, 2) + " Hz, silence " + String (none, 1));
        }

        // --- browser search, favourites, recent projects ----------------------------------------------
        {
            rep.step ("browser search");
            auto lib = dir.getChildFile ("SearchLib");
            lib.getChildFile ("Drums/Kicks").createDirectory();
            lib.getChildFile ("Loops").createDirectory();
            auto k1 = writeTestWav (lib.getChildFile ("Drums/Kicks/Deep Kick 01.wav"), 0.1, 60.0);
            auto k2 = writeTestWav (lib.getChildFile ("Drums/Kicks/kick_punchy.wav"), 0.1, 60.0);
            auto lp = writeTestWav (lib.getChildFile ("Loops/Funky Drum Loop 120.wav"), 0.1, 200.0);
            lib.getChildFile ("Loops/notes.txt").replaceWithText ("kick");   // not audio: never listed

            auto kicks = BrowserSearch::search (lib, "kick");
            auto deep = BrowserSearch::search (lib, "DEEP kick");
            auto drumLoop = BrowserSearch::search (lib, "loop drum");
            auto none = BrowserSearch::search (lib, "snare");
            rep.check (kicks.size() == 2 && deep.size() == 1 && deep[0] == k1 && drumLoop.size() == 1 && drumLoop[0] == lp && none.isEmpty(),
                       "Browser search (sub-folders, all words, any order)",
                       String (kicks.size()) + " / " + String (deep.size()) + " / " + String (drumLoop.size()) + " / " + String (none.size()));

            PropertiesFile::Options po;
            po.applicationName = "PulseSelfTestProps";
            po.filenameSuffix = ".settings";
            po.folderName = dir.getFullPathName();
            PropertiesFile props (dir.getChildFile ("browser_test.settings"), po);
            BrowserSearch::setFavourite (props, k2, true);
            BrowserSearch::setFavourite (props, lp, true);
            BrowserSearch::setFavourite (props, k2, true);   // no duplicates
            const bool favAdded = BrowserSearch::favourites (props).size() == 2 && BrowserSearch::isFavourite (props, lp);
            BrowserSearch::setFavourite (props, lp, false);
            rep.check (favAdded && BrowserSearch::favourites (props).size() == 1 && ! BrowserSearch::isFavourite (props, lp), "Browser favourites");

            // results list drags its selected files like the file tree
            FileResultsList list (props);
            list.setFiles ({ k1, k2, lp }, {});
            list.selectRow (0);
            list.selectRow (2, false, false);
            DragAndDropTarget::SourceDetails sd ("PulseFiles", &list, {});
            auto dragged = ClipImport::filesFromDrag (sd);
            rep.check (dragged.size() == 2 && dragged.contains (k1) && dragged.contains (lp), "Drag files from search results", String (dragged.size()));

            StringArray recent;
            for (int i = 0; i < 15; ++i) recent = RecentProjects::push (recent, dir.getChildFile ("p" + String (i) + ".tracktionedit"));
            recent = RecentProjects::push (recent, dir.getChildFile ("p10.tracktionedit"));
            rep.check (recent.size() == 12 && File (recent[0]).getFileName() == "p10.tracktionedit" && File (recent[1]).getFileName() == "p14.tracktionedit"
                         && recent.indexOf (dir.getChildFile ("p10.tracktionedit").getFullPathName()) == 0
                         && [&recent] { auto c = recent; c.removeDuplicates (true); return c.size() == recent.size(); }(), "Recent projects list (newest first, 12 max, no duplicates)");
        }

        // --- level meters ------------------------------------------------------------------------------
        {
            rep.step ("meters");
            auto lp = edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
            bool ok = false;
            String info;
            if (auto lm = dynamic_cast<te::LevelMeterPlugin*> (lp.get()))
            {
                LevelMeter v (*lm, true), h (*lm, false);
                AudioBuffer<float> b (2, 512);
                for (int i = 0; i < 512; ++i) { b.setSample (0, i, 0.5f * std::sin ((float) i * 0.1f)); b.setSample (1, i, 0.25f * std::sin ((float) i * 0.1f)); }
                lm->measurer.processBuffer (b, 0, 512);
                v.poll(); h.poll();
                const float l = v.getLevelDb (0), r = v.getLevelDb (1);
                b.applyGain (2.5f);   // clip: 1.25 peak
                lm->measurer.processBuffer (b, 0, 512);
                v.poll();
                const bool clippedPeak = v.getMaxPeak() > 1.0f;
                ok = std::abs (l + 6.0f) < 0.3f && std::abs (r + 12.0f) < 0.3f && clippedPeak
                     && MeterStyle::norm (-60.0f) == 0.0f && MeterStyle::norm (6.0f) == 1.0f && MeterStyle::norm (0.0f) > MeterStyle::norm (-6.0f);
                info = String (l, 1) + " / " + String (r, 1) + " dB, peak " + String (v.getMaxPeak(), 1);

                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                Component box;
                box.setSize (200, 160);
                box.addAndMakeVisible (v); box.addAndMakeVisible (h);
                v.setBounds (10, 10, 12, 140); h.setBounds (40, 70, 150, 14);
                auto img = box.createComponentSnapshot (box.getLocalBounds());
                auto f = dir.getChildFile ("meters.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (ok, "Level meters (stereo, peak, clip)", info);
        }

        // --- drag and drop operations ---------------------------------------------------------------------
        {
            rep.step ("drag and drop");
            auto ta = ClipImport::createContentTrack (*edit, "DnD A", false);
            auto tb = ClipImport::createContentTrack (*edit, "DnD B", false);
            auto tc = ClipImport::createContentTrack (*edit, "DnD C", false);
            bool reordered = false, reorderedEnd = false;
            if (ta != nullptr && tb != nullptr && tc != nullptr)
            {
                DragDrop::moveTrack (*tc, ta);   // C before A
                auto all = te::getAllTracks (*edit);
                reordered = all.indexOf (tc) + 1 == all.indexOf (ta) && all.indexOf (ta) + 1 == all.indexOf (tb);
                DragDrop::moveTrack (*tc, nullptr);    // to the end
                reorderedEnd = te::getAllTracks (*edit).getLast() == tc;
            }
            rep.check (reordered && reorderedEnd, "Drag a track to reorder");

            // devices: reorder within a chain, move and copy to another track
            bool reorderOk = false, moveOk = false, copyOk = false, rackCopyOwn = false;
            if (ta != nullptr && tb != nullptr)
            {
                auto mk = [&] (const char* type) { return edit->getPluginCache().createNewPlugin (type, {}); };
                auto eq = mk (pulse::PulseEQ::xmlTypeName), sat = mk (pulse::Saturator::xmlTypeName), lim = mk (pulse::Limiter::xmlTypeName);
                for (auto& p : { eq, sat, lim }) PluginMenu::insertPlugin (ta, ta->pluginList, p, nullptr);
                auto order = [] (te::AudioTrack& t)
                {
                    StringArray n;
                    for (auto p : t.pluginList)
                        if (dynamic_cast<pulse::PulsePlugin*> (p) != nullptr || dynamic_cast<te::RackInstance*> (p) != nullptr) n.add (p->getName());
                    return n.joinIntoString (",");
                };
                DragDrop::placeDevice (*lim, ta, ta->pluginList, eq.get(), false);       // limiter to the front
                reorderOk = order (*ta) == "Limiter,EQ Six,Saturator";
                DragDrop::placeDevice (*sat, tb, tb->pluginList, nullptr, false);        // move to B
                moveOk = order (*ta) == "Limiter,EQ Six" && order (*tb) == "Saturator";
                DragDrop::placeDevice (*eq, tb, tb->pluginList, nullptr, true);          // copy to B
                copyOk = order (*ta) == "Limiter,EQ Six" && order (*tb) == "Saturator,EQ Six";
                rep.check (reorderOk && moveOk && copyOk, "Drag devices: reorder, move and copy to another track", order (*ta) + " | " + order (*tb));

                auto rp = DrumRack::create (*edit, true);
                PluginMenu::insertPlugin (ta, ta->pluginList, rp, nullptr);
                DragDrop::placeDevice (*rp, tb, tb->pluginList, nullptr, true);
                auto copyRack = DrumRack::findOnTrack (*tb);
                rackCopyOwn = copyRack != nullptr && copyRack->type != DrumRack::rackOf (rp.get())
                              && DrumRack::usedPads (*copyRack->type).size() == 11;
                rep.check (rackCopyOwn, "Copying a Drum Rack gives the copy its own pads");
            }

            // session clips: move and copy between slots
            bool slotMove = false, slotCopy = false;
            if (ta != nullptr && tb != nullptr)
            {
                ta->getClipSlotList().ensureNumberOfSlots (4);
                tb->getClipSlotList().ensureNumberOfSlots (4);
                auto sa = ta->getClipSlotList().getClipSlots();
                auto sb = tb->getClipSlotList().getClipSlots();
                auto mc = te::insertMIDIClip (*sa[0], "Slot clip", { te::TimePosition(), te::TimePosition::fromSeconds (2.0) });
                if (mc != nullptr)
                {
                    mc->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
                    DragDrop::placeClipInSlot (*mc, *sb[1], false);
                    slotMove = sa[0]->getClip() == nullptr && sb[1]->getClip() != nullptr;
                    if (auto moved = sb[1]->getClip())
                    {
                        DragDrop::placeClipInSlot (*moved, *sa[2], true);
                        auto copy = dynamic_cast<te::MidiClip*> (sa[2]->getClip());
                        slotCopy = copy != nullptr && sb[1]->getClip() != nullptr && copy != sb[1]->getClip()
                                   && copy->getSequence().getNumNotes() == 1;
                    }
                }
            }
            rep.check (slotMove && slotCopy, "Drag session clips between slots (move, Ctrl copies)");
            for (auto t : { ta, tb, tc }) if (t != nullptr) edit->deleteTrack (t);
        }

        // --- MIDI tools, audio quantize, MIDI record modes --------------------------------------------
        {
            rep.step ("editing tools");
            auto t = ClipImport::createContentTrack (*edit, "Tools", true);
            auto mc = t != nullptr ? t->insertMIDIClip ("Tools", { te::TimePosition(), te::TimePosition::fromSeconds (8.0) }, nullptr) : nullptr;
            if (mc != nullptr)
            {
                auto& sq = mc->getSequence();
                auto beat = [] (double b) { return te::BeatPosition::fromBeats (b); };
                auto len = [] (double b) { return te::BeatDuration::fromBeats (b); };
                auto notesAt = [&sq] { Array<te::MidiNote*> a; for (auto n : sq.getNotes()) a.add (n); return a; };

                // legato
                sq.addNote (60, beat (0), len (0.25), 100, 0, nullptr);
                sq.addNote (62, beat (1), len (0.25), 100, 0, nullptr);
                sq.addNote (64, beat (3), len (0.5), 100, 0, nullptr);
                MidiTools::legato (*mc, {});
                auto ns = MidiTools::byStart (notesAt());
                const bool legato = std::abs (ns[0]->getLengthBeats().inBeats() - 1.0) < 1e-6 && std::abs (ns[1]->getLengthBeats().inBeats() - 2.0) < 1e-6
                                    && std::abs (ns[2]->getLengthBeats().inBeats() - 0.5) < 1e-6;

                // fit to scale (C major): C# -> C, F# -> F (down on ties)
                sq.clear (nullptr);
                sq.addNote (61, beat (0), len (1), 100, 0, nullptr);
                sq.addNote (66, beat (1), len (1), 100, 0, nullptr);
                sq.addNote (64, beat (2), len (1), 100, 0, nullptr);
                int majorIdx = -1;
                for (int i = 0; i < (int) Scales::all().size(); ++i) if (String (Scales::all()[(size_t) i].name).containsIgnoreCase ("major")) { majorIdx = i; break; }
                const int moved = MidiTools::fitToScale (*mc, {}, majorIdx, 0);
                ns = MidiTools::byStart (notesAt());
                const bool fit = moved == 2 && ns[0]->getNoteNumber() == 60 && ns[1]->getNoteNumber() == 65 && ns[2]->getNoteNumber() == 64;

                // strum: a C major chord spread upwards by 1/16 beat
                sq.clear (nullptr);
                for (int p : { 64, 60, 67 }) sq.addNote (p, beat (4), len (1), 100, 0, nullptr);
                MidiTools::strum (*mc, {}, 0.0625, true);
                ns = MidiTools::byStart (notesAt());
                const bool strum = ns[0]->getNoteNumber() == 60 && ns[1]->getNoteNumber() == 64 && ns[2]->getNoteNumber() == 67
                                   && std::abs (ns[2]->getStartBeat().inBeats() - 4.125) < 1e-6 && std::abs (ns[2]->getEndBeat().inBeats() - 5.0) < 1e-6;

                // velocity ramp, invert, reverse, humanize, stretch
                MidiTools::Notes none;
                Random rng (1);
                MidiTools::velocities (*mc, none, MidiTools::Velocity::rampUp, rng);
                ns = MidiTools::byStart (notesAt());
                const bool ramp = ns[0]->getVelocity() == 40 && ns[2]->getVelocity() == 127;
                MidiTools::invert (*mc, none);
                ns = MidiTools::byStart (notesAt());
                const bool inv = ns[0]->getNoteNumber() == 67 && ns[2]->getNoteNumber() == 60;
                MidiTools::stretch (*mc, none, 2.0);
                ns = MidiTools::byStart (notesAt());
                const bool stretched = std::abs (ns[2]->getStartBeat().inBeats() - 4.25) < 1e-6;
                double before = 0, after = 0;
                for (auto n : sq.getNotes()) before += n->getStartBeat().inBeats();
                MidiTools::humanize (*mc, none, 0.05, 10, rng);
                for (auto n : sq.getNotes()) after += n->getStartBeat().inBeats();
                const bool hum = std::abs (after - before) > 1e-6 && std::abs (after - before) < 0.16;
                rep.check (legato && fit && strum && ramp && inv && stretched && hum, "MIDI tools (legato, fit to scale, strum, velocity, invert, stretch, humanize)",
                           String ((int) legato) + String ((int) fit) + String ((int) strum) + String ((int) ramp) + String ((int) inv) + String ((int) stretched) + String ((int) hum));
            }
            if (t != nullptr) edit->deleteTrack (t);

            // audio quantize: kicks slightly off the beat get pulled onto it
            auto aq = ClipImport::createContentTrack (*edit, "AudioQuantize", false);
            const double sr = 44100.0, spb = 60.0 / 120.0;
            AudioBuffer<float> beatBuf (1, (int) (sr * 4.0));
            beatBuf.clear();
            const double offsets[] = { 0.0, 0.03, -0.025, 0.04, 0.0, 0.02, -0.03, 0.035 };   // seconds off the grid
            for (int k = 0; k < 8; ++k)
            {
                const int s0 = (int) ((k * spb + offsets[k] + 0.05) * sr);
                for (int i = 0; i < (int) (0.15 * sr) && s0 + i < beatBuf.getNumSamples(); ++i)
                {
                    const double tt = i / sr;
                    beatBuf.addSample (0, s0 + i, (float) (0.9 * std::exp (-tt * 25.0) * std::sin (MathConstants<double>::twoPi * (50.0 + 100.0 * std::exp (-tt * 40.0)) * tt)));
                }
            }
            auto loose = dir.getChildFile ("loose_kicks.wav");
            SampleEdit::writeWav (loose, beatBuf, sr);
            int movedMarkers = -2;
            String aqInfo;
            double maxErrBefore = 0, maxErrAfter = 0;
            if (aq != nullptr)
            {
                edit->tempoSequence.getTempo (0)->setBpm (120.0);
                te::Clip::Ptr c = ClipImport::importFile (*aq, *edit, loose, te::TimePosition());
                if (auto ac = dynamic_cast<te::AudioClipBase*> (c.get()))
                {
                    Workflow::enableWarpMarkers (*ac, true);
                    for (int tries = 0; tries < 200 && (movedMarkers = Workflow::quantizeAudio (*ac, 0.25, 1.0)) < 0; ++tries)
                        MessageManager::getInstance()->runDispatchLoopUntil (50);
                    aqInfo = String (ac->getWarpTimeManager().getMarkers().size()) + " markers, ";
                    // marker positions vs the 1/16 grid, before (source) and after (warp)
                    for (auto m : ac->getWarpTimeManager().getMarkers())
                    {
                        const double srcBeat = m->sourceTime.inSeconds() / spb, warpBeat = Workflow::warpTimeToEditBeat (*ac, m->warpTime);
                        maxErrBefore = jmax (maxErrBefore, std::abs (srcBeat - std::round (srcBeat * 4.0) / 4.0));
                        maxErrAfter = jmax (maxErrAfter, std::abs (warpBeat - std::round (warpBeat * 4.0) / 4.0));
                    }
                }
                edit->deleteTrack (aq);
            }
            rep.check (movedMarkers >= 4 && maxErrAfter < 0.01 && maxErrBefore > 0.05, "Audio quantize (transients to the 1/16 grid)",
                       aqInfo + String (movedMarkers) + " moved, worst " + String (maxErrBefore, 3) + " -> " + String (maxErrAfter, 4) + " beats off");

            // MIDI record modes reach the inputs
            const int inputs = RecordingSettings::applyMidiMode (engine, 1);
            bool takesMode = true;
            for (auto& d : engine.getDeviceManager().getMidiInDevices()) takesMode = takesMode && ! d->mergeRecordings && ! d->replaceExistingClips;
            RecordingSettings::applyMidiMode (engine, 0);
            bool overdubMode = true;
            for (auto& d : engine.getDeviceManager().getMidiInDevices()) overdubMode = overdubMode && d->mergeRecordings;
            rep.check (takesMode && overdubMode, "MIDI record modes (overdub / takes / replace)", String (inputs) + " inputs");
        }

        // --- mixing: loudness, sidechain ducking, mixer snapshots -------------------------------------
        {
            rep.step ("mixing");
            // LUFS / true peak on a known signal: 1 kHz at -20 dBFS in both channels = -20 LUFS
            {
                pulse::LoudnessAnalyser a;
                a.prepare (48000.0);
                std::vector<float> l (48000), r (48000);
                const float amp = Decibels::decibelsToGain (-20.0f);
                for (int sec = 0; sec < 6; ++sec)
                {
                    for (int i = 0; i < 48000; ++i)
                        l[(size_t) i] = r[(size_t) i] = sec < 4 ? amp * std::sin (MathConstants<float>::twoPi * 1000.0f * (float) (sec * 48000 + i) / 48000.0f) : 0.0f;
                    a.process (l.data(), r.data(), 48000);
                }
                const double I = a.integrated();
                const float tp = a.truePeakDb();
                rep.check (std::abs (I + 20.0) < 0.3 && std::abs (tp + 20.0f) < 0.3f, "Loudness meter (LUFS integrated with gating, true peak)",
                           String (I, 2) + " LUFS, " + String (tp, 2) + " dBTP");
            }

            // sidechain: a sustained tone ducks while the kick track plays
            auto tone = writeTestWav (dir.getChildFile ("duck_tone.wav"), 2.0, 220.0);
            auto padT = ClipImport::createContentTrack (*edit, "DuckPad", false);
            auto kickT = ClipImport::createContentTrack (*edit, "DuckKick", false);
            float ducked = 0, dry = 0;
            if (padT != nullptr && kickT != nullptr)
            {
                ClipImport::importFile (*padT, *edit, tone, te::TimePosition());
                AudioBuffer<float> k (2, 44100);
                k.clear();
                for (int i = 0; i < 4410; ++i) { const float v = 0.9f * std::sin ((float) i * 0.02f) * std::exp (-(float) i / 900.0f); k.setSample (0, 22050 + i, v); k.setSample (1, 22050 + i, v); }
                auto kf = dir.getChildFile ("duck_kick.wav");
                SampleEdit::writeWav (kf, k, 44100.0);
                ClipImport::importFile (*kickT, *edit, kf, te::TimePosition());
                auto glue = edit->getPluginCache().createNewPlugin (pulse::GlueCompressor::xmlTypeName, {});
                PluginMenu::insertPlugin (padT, padT->pluginList, glue, nullptr);
                auto* g = dynamic_cast<pulse::GlueCompressor*> (glue.get());
                auto renderBoth = [&] (const String& n)
                {
                    EditingOps::waitForClipProxies (*edit);
                    te::Renderer::Parameters rp (*edit);
                    rp.tracksToDo = te::toBitSet (juce::Array<te::Track*> { padT, kickT });
                    rp.destFile = dir.getChildFile (n + ".wav");
                    rp.destFile.deleteFile();
                    rp.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
                    rp.bitDepth = 24; rp.sampleRateForAudio = 44100.0;
                    rp.time = { te::TimePosition(), te::TimePosition::fromSeconds (1.2) };
                    rp.usePlugins = true;
                    return te::Renderer::renderToFile ("duck", rp);
                };
                auto windowPeak = [&engine] (const File& f, double t0, double t1)
                {
                    std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                    if (r == nullptr) return -200.0f;
                    Range<float> rg[2];
                    r->readMaxLevels ((int64) (t0 * r->sampleRate), (int64) ((t1 - t0) * r->sampleRate), rg, 2);
                    return Decibels::gainToDecibels (jmax (std::abs (rg[0].getStart()), std::abs (rg[0].getEnd())), -200.0f);
                };
                if (g != nullptr)
                {
                    g->set (pulse::GlueCompressor::pThreshold, -30.0f);
                    g->set (pulse::GlueCompressor::pRatio, 2.0f);
                    g->set (pulse::GlueCompressor::pAttack, 1.0f);
                    g->set (pulse::GlueCompressor::pRelease, 1.0f);
                    g->setSidechainSourceID (kickT->itemID);
                    g->guessSidechainRouting();
                    auto f = renderBoth ("duck_on");
                    dry = windowPeak (f, 0.2, 0.45);        // before the kick: the tone passes untouched
                    ducked = windowPeak (f, 0.62, 0.70);    // just after the kick (0.5 - 0.6 s): ducked
                }
            }
            rep.check (dry - ducked > 6.0f, "Sidechain ducking (Glue Compressor keyed from another track)",
                       String (dry, 1) + " dB before the kick, " + String (ducked, 1) + " dB after it");
            for (auto t : { padT, kickT }) if (t != nullptr) edit->deleteTrack (t);

            // mixer snapshots
            auto tracksNow = EngineHelpers::getUserTracks (*edit);
            bool snapOk = false;
            if (tracksNow.size() >= 2)
            {
                auto* t0 = tracksNow[0]; auto* t1 = tracksNow[1];
                t0->getVolumePlugin()->setVolumeDb (-6.0f);
                t1->getVolumePlugin()->setPan (-0.5f);
                MixSnapshots::store (*edit, "A");
                t0->getVolumePlugin()->setVolumeDb (-20.0f);
                t1->getVolumePlugin()->setPan (0.7f);
                t1->setMute (true);
                const bool recalled = MixSnapshots::recall (*edit, MixSnapshots::names (*edit).indexOf ("A"));
                snapOk = recalled && std::abs (t0->getVolumePlugin()->getVolumeDb() + 6.0f) < 0.1f && std::abs (t1->getVolumePlugin()->getPan() + 0.5f) < 0.01f
                         && ! t1->isMuted (false);
                t0->getVolumePlugin()->setVolumeDb (0.0f);
                t1->getVolumePlugin()->setPan (0.0f);
            }
            rep.check (snapOk, "Mixer snapshots (store / recall)");
        }

        // --- Pulse Reverb / Delay / Vocoder / Pitch Correction / FM / granular -------------------------
        {
            rep.step ("new instruments and effects");
            const double sr = 48000.0;
            const int block = 480;
            // runs audio (and MIDI) through a plugin block by block
            auto runAudio = [&] (te::Plugin& fx, const AudioBuffer<float>& in, std::vector<std::pair<double, MidiMessage>> midiIn = {})
            {
                fx.initialise ({ te::TimePosition(), sr, block });
                AudioBuffer<float> out (2, in.getNumSamples());
                AudioBuffer<float> audio (2, block);
                const te::MPESourceID src = te::createUniqueMPESourceID();
                for (int start = 0; start + block <= in.getNumSamples(); start += block)
                {
                    audio.copyFrom (0, 0, in, 0, start, block);
                    audio.copyFrom (1, 0, in, in.getNumChannels() > 1 ? 1 : 0, start, block);
                    te::MidiMessageArray midi;
                    const double t0 = start / sr, t1 = (start + block) / sr;
                    for (auto& [t, m] : midiIn) if (t >= t0 && t < t1) midi.addMidiMessage (m, t - t0, src);
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (t0), te::TimePosition::fromSeconds (t1) },
                                                true, false, true, false);
                    fx.applyToBuffer (fc);
                    out.copyFrom (0, start, audio, 0, 0, block);
                    out.copyFrom (1, start, audio, 1, 0, block);
                }
                fx.deinitialise();
                return out;
            };
            auto rmsDb = [&] (const AudioBuffer<float>& b, int ch, double t0, double t1)
            {
                const int a = (int) (t0 * sr), n = (int) ((t1 - t0) * sr);
                return Decibels::gainToDecibels (b.getRMSLevel (ch, a, n), -200.0f);
            };
            auto make = [&] (const char* type) { return edit->getPluginCache().createNewPlugin (type, {}); };

            // reverb: 1 s RT60 -> ~60 dB per second; silent before the predelay
            {
                auto rv = make (pulse::Reverb::xmlTypeName);
                auto* r = dynamic_cast<pulse::Reverb*> (rv.get());
                AudioBuffer<float> in (2, (int) (sr * 2.0));
                in.clear();
                in.setSample (0, (int) (0.1 * sr), 1.0f); in.setSample (1, (int) (0.1 * sr), 1.0f);
                bool ok = false;
                String info;
                if (r != nullptr)
                {
                    r->set (pulse::Reverb::pMix, 1.0f); r->set (pulse::Reverb::pDecay, 1.0f); r->set (pulse::Reverb::pPredelay, 50.0f);
                    r->set (pulse::Reverb::pMod, 0.0f);
                    auto out = runAudio (*r, in);
                    const float before = rmsDb (out, 0, 0.0, 0.14), early = rmsDb (out, 0, 0.35, 0.45), late = rmsDb (out, 0, 0.85, 0.95);
                    const float slope = (early - late) / 0.5f;   // dB per second
                    ok = before < -80.0f && early > -70.0f && slope > 35.0f && slope < 95.0f;
                    info = "before " + String (before, 0) + " dB, decay " + String (slope, 0) + " dB/s";
                }
                rep.check (ok, "Pulse Reverb (RT60 and predelay)", info);
            }

            // delay: echoes at 250 ms, -6 dB per repeat; ping-pong alternates sides
            {
                auto dv = make (pulse::Delay::xmlTypeName);
                auto* d = dynamic_cast<pulse::Delay*> (dv.get());
                AudioBuffer<float> in (2, (int) (sr * 1.0));
                in.clear();
                in.setSample (0, 0, 1.0f); in.setSample (1, 0, 1.0f);
                bool ok = false, pingOk = false;
                String info;
                if (d != nullptr)
                {
                    d->set (pulse::Delay::pSync, 0.0f); d->set (pulse::Delay::pTime, 250.0f); d->set (pulse::Delay::pFeedback, 0.5f);
                    d->set (pulse::Delay::pMix, 1.0f); d->set (pulse::Delay::pLowCut, 20.0f); d->set (pulse::Delay::pHighCut, 20000.0f);
                    auto out = runAudio (*d, in);
                    auto peakAt = [&] (double t) { const int a = (int) ((t - 0.01) * sr); return out.getMagnitude (0, a, (int) (0.02 * sr)); };
                    int maxIdx = 0; float maxV = 0;
                    for (int i = (int) (0.2 * sr); i < (int) (0.3 * sr); ++i) if (std::abs (out.getSample (0, i)) > maxV) { maxV = std::abs (out.getSample (0, i)); maxIdx = i; }
                    const float e1 = peakAt (0.25), e2 = peakAt (0.5);
                    ok = std::abs (maxIdx / sr - 0.25) < 0.002 && std::abs (Decibels::gainToDecibels (e2 / jmax (1.0e-6f, e1)) + 6.0f) < 1.5f;
                    info = "first echo " + String (maxIdx / sr * 1000.0, 1) + " ms, repeat " + String (Decibels::gainToDecibels (e2 / jmax (1.0e-6f, e1)), 1) + " dB";
                    d->set (pulse::Delay::pPingPong, 1.0f);
                    auto pp = runAudio (*d, in);
                    const float l1 = pp.getMagnitude (0, (int) (0.24 * sr), (int) (0.02 * sr)), r1 = pp.getMagnitude (1, (int) (0.24 * sr), (int) (0.02 * sr));
                    const float l2 = pp.getMagnitude (0, (int) (0.49 * sr), (int) (0.02 * sr)), r2 = pp.getMagnitude (1, (int) (0.49 * sr), (int) (0.02 * sr));
                    pingOk = l1 > 10.0f * r1 && r2 > 10.0f * l2;
                }
                rep.check (ok && pingOk, "Pulse Delay (time, feedback, ping-pong)", info);
            }

            // vocoder: the output follows the voice (modulator), not the carrier
            {
                auto vv = make (pulse::Vocoder::xmlTypeName);
                auto* v = dynamic_cast<pulse::Vocoder*> (vv.get());
                AudioBuffer<float> in (2, (int) sr);
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const float x = i < (int) (0.5 * sr) ? 0.5f * std::sin ((float) i * 0.05f) + 0.2f * std::sin ((float) i * 0.23f) : 0.0f;
                    in.setSample (0, i, x); in.setSample (1, i, x);
                }
                bool ok = false;
                String info;
                if (v != nullptr)
                {
                    v->set (pulse::Vocoder::pCarrier, 1.0f);   // internal saw
                    auto out = runAudio (*v, in);
                    const float on = rmsDb (out, 0, 0.1, 0.45), off = rmsDb (out, 0, 0.7, 0.95);
                    ok = on > -40.0f && on - off > 30.0f;
                    info = String (on, 1) + " dB with voice, " + String (off, 1) + " dB without";
                }
                rep.check (ok, "Vocoder (16 bands, internal carrier)", info);
            }

            // pitch correction: 452 Hz (A4 + 47 ct) comes out at 440 Hz
            {
                auto pv = make (pulse::PitchCorrect::xmlTypeName);
                auto* pc = dynamic_cast<pulse::PitchCorrect*> (pv.get());
                AudioBuffer<float> in (2, (int) sr);
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const float x = 0.4f * std::sin (MathConstants<float>::twoPi * 452.0f * (float) i / (float) sr);
                    in.setSample (0, i, x); in.setSample (1, i, x);
                }
                double hz = 0;
                if (pc != nullptr)
                {
                    pc->set (pulse::PitchCorrect::pSpeed, 0.0f);
                    auto out = runAudio (*pc, in);
                    hz = pulse::Tuner::detectPitch (out.getReadPointer (0, (int) (0.5 * sr)), 4096, sr);
                }
                rep.check (std::abs (hz - 440.0) < 3.0, "Pitch correction (to the nearest note)", "452 Hz in -> " + String (hz, 1) + " Hz out");
            }

            // FM synth: modulation adds brightness compared with a pure sine
            {
                auto fv = make (pulse::FMSynth::xmlTypeName);
                auto* fm = dynamic_cast<pulse::FMSynth*> (fv.get());
                float pure = 0, rich = 0;
                if (fm != nullptr)
                {
                    AudioBuffer<float> silence (2, (int) (sr * 0.5));
                    silence.clear();
                    std::vector<std::pair<double, MidiMessage>> note { { 0.01, MidiMessage::noteOn (1, 57, (uint8) 110) } };
                    auto brightness = [&] (const AudioBuffer<float>& b)
                    {
                        double e = 0, d = 0;
                        for (int i = (int) (0.1 * sr); i < (int) (0.4 * sr); ++i)
                        {
                            const double x = b.getSample (0, i), dx = x - b.getSample (0, i - 1);
                            e += x * x; d += dx * dx;
                        }
                        return e > 0 ? (float) std::sqrt (d / e) : 0.0f;
                    };
                    fm->set (pulse::FMSynth::pAlgorithm, 0.0f);
                    fm->set (pulse::FMSynth::opParam (1, pulse::FMSynth::kLevel), 0.0f);
                    pure = brightness (runAudio (*fm, silence, note));
                    fm->set (pulse::FMSynth::opParam (1, pulse::FMSynth::kLevel), 0.9f);
                    fm->set (pulse::FMSynth::opParam (1, pulse::FMSynth::kRatio), 3.0f);
                    rich = brightness (runAudio (*fm, silence, note));
                }
                rep.check (pure > 0.0f && rich > pure * 2.0f, "FM synth (modulation adds harmonics)", String (pure, 3) + " -> " + String (rich, 3));
            }

            // Simpler granular mode
            {
                auto sv = make (pulse::Simpler::xmlTypeName);
                auto* smp = dynamic_cast<pulse::Simpler*> (sv.get());
                float lvl = -200.0f;
                if (smp != nullptr)
                {
                    smp->setSampleFile (writeTestWav (dir.getChildFile ("grain_src.wav"), 2.0, 330.0));
                    smp->set (pulse::Simpler::pMode, 3.0f);
                    AudioBuffer<float> silence (2, (int) (sr * 0.6));
                    silence.clear();
                    auto out = runAudio (*smp, silence, { { 0.01, MidiMessage::noteOn (1, 60, (uint8) 110) } });
                    lvl = rmsDb (out, 0, 0.1, 0.5);
                }
                rep.check (lvl > -40.0f, "Simpler granular mode", String (lvl, 1) + " dB");
            }

            // filter envelopes: a low cutoff opened by the envelope is brighter at the start of the note
            {
                auto brightnessOf = [&] (const AudioBuffer<float>& b, double t0, double t1)
                {
                    double e = 0, d = 0;
                    for (int i = (int) (t0 * sr) + 1; i < (int) (t1 * sr); ++i)
                    {
                        const double x = b.getSample (0, i), dx = x - b.getSample (0, i - 1);
                        e += x * x; d += dx * dx;
                    }
                    return e > 0 ? std::sqrt (d / e) : 0.0;
                };
                AudioBuffer<float> silence (2, (int) (sr * 1.0));
                silence.clear();
                std::vector<std::pair<double, MidiMessage>> note { { 0.01, MidiMessage::noteOn (1, 48, (uint8) 110) } };
                String info;
                bool ok = true;
                auto sv2 = make (pulse::Simpler::xmlTypeName);
                if (auto* smp = dynamic_cast<pulse::Simpler*> (sv2.get()))
                {
                    AudioBuffer<float> saw (2, 96000);
                    for (int i = 0; i < 96000; ++i) { const float v = 0.4f * (2.0f * std::fmod ((float) i * 130.8f / 48000.0f, 1.0f) - 1.0f); saw.setSample (0, i, v); saw.setSample (1, i, v); }
                    auto sawFile = dir.getChildFile ("saw_c3.wav");
                    SampleEdit::writeWav (sawFile, saw, 48000.0);
                    smp->setSampleFile (sawFile);
                    smp->set (pulse::Simpler::pRoot, 48.0f);
                    smp->set (pulse::Simpler::pFilterType, 1.0f);
                    smp->set (pulse::Simpler::pCutoff, 200.0f);
                    smp->set (pulse::Simpler::pFEnv, 1.0f);
                    smp->set (pulse::Simpler::pFA, 1.0f); smp->set (pulse::Simpler::pFD, 250.0f); smp->set (pulse::Simpler::pFS, 0.0f);
                    auto out = runAudio (*smp, silence, note);
                    const double early = brightnessOf (out, 0.02, 0.08), late = brightnessOf (out, 0.6, 0.9);
                    ok = ok && early > late * 1.5;
                    info << "Simpler " << String (early, 3) << " -> " << String (late, 3);
                }
                auto fv2 = make (pulse::FMSynth::xmlTypeName);
                if (auto* fm = dynamic_cast<pulse::FMSynth*> (fv2.get()))
                {
                    fm->set (pulse::FMSynth::opParam (1, pulse::FMSynth::kLevel), 0.9f);
                    fm->set (pulse::FMSynth::opParam (1, pulse::FMSynth::kS), 1.0f);
                    fm->set (pulse::FMSynth::opParam (0, pulse::FMSynth::kS), 1.0f);
                    fm->set (pulse::FMSynth::pCutoff, 300.0f);
                    fm->set (pulse::FMSynth::pFEnv, 1.0f);
                    fm->set (pulse::FMSynth::pFA, 1.0f); fm->set (pulse::FMSynth::pFD, 250.0f); fm->set (pulse::FMSynth::pFS, 0.0f);
                    auto out = runAudio (*fm, silence, note);
                    const double early = brightnessOf (out, 0.02, 0.08), late = brightnessOf (out, 0.6, 0.9);
                    ok = ok && early > late * 1.5;
                    info << ", FM " << String (early, 3) << " -> " << String (late, 3);
                }
                rep.check (ok, "Filter envelopes (Simpler, FM)", info);
            }
        }

        // --- performance: scene tempo, per-clip launch quantisation, session -> arrangement -----------
        {
            rep.step ("performance");
            auto t = ClipImport::createContentTrack (*edit, "Perf", true);
            bool tempoOk = false, quantOk = false, recOk = false;
            String recInfo;
            if (t != nullptr)
            {
                auto& sl = edit->getSceneList();
                sl.ensureNumberOfScenes (4);
                t->getClipSlotList().ensureNumberOfSlots (4);
                auto slot = t->getClipSlotList().getClipSlots()[1];
                auto mc = te::insertMIDIClip (*slot, "Loop", { te::TimePosition(), EngineHelpers::beatToTime (*edit, 4.0) });
                if (mc != nullptr)
                {
                    mc->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
                    mc->setLoopRangeBeats ({ te::BeatPosition(), te::BeatPosition::fromBeats (4.0) });

                    const double before = edit->tempoSequence.getTempo (0)->getBpm();
                    sl.getScenes()[1]->state.setProperty (Launcher::sceneTempoID, 133.0, nullptr);
                    Launcher::launchScene (*edit, 1);
                    tempoOk = std::abs (edit->tempoSequence.getTempo (0)->getBpm() - 133.0) < 0.01;
                    Launcher::stopTrack (*t);
                    edit->tempoSequence.getTempo (0)->setBpm (before);
                    sl.getScenes()[1]->state.removeProperty (Launcher::sceneTempoID, nullptr);

                    mc->setUsesGlobalLaunchQuatisation (false);
                    if (auto lq = mc->getLaunchQuantisation()) lq->type = te::LaunchQType::half;
                    quantOk = ! mc->usesGlobalLaunchQuatisation() && mc->getLaunchQuantisation() != nullptr
                              && mc->getLaunchQuantisation()->type.get() == te::LaunchQType::half
                              && &Launcher::quantisationFor (*mc) == mc->getLaunchQuantisation();

                    Launcher::SessionRecorder rec;
                    rec.writeSegment (*t, *mc, 8.0, 20.0);   // played for 3 bars from bar 3
                    te::Clip* arr = t->getClips().getFirst();
                    if (arr != nullptr)
                    {
                        const double s0 = EngineHelpers::timeToBeat (*edit, arr->getPosition().getStart());
                        const double s1 = EngineHelpers::timeToBeat (*edit, arr->getPosition().getEnd());
                        recOk = std::abs (s0 - 8.0) < 0.01 && std::abs (s1 - 20.0) < 0.01 && arr->isLooping()
                                && dynamic_cast<te::MidiClip*> (arr) != nullptr && dynamic_cast<te::MidiClip*> (arr)->getSequence().getNumNotes() == 1;
                        recInfo = String (s0, 2) + " - " + String (s1, 2) + " beats, looping " + String ((int) arr->isLooping());
                    }
                }
                edit->deleteTrack (t);
            }
            rep.check (tempoOk, "Scene tempo (launching a scene sets the tempo)");
            rep.check (quantOk, "Per-clip launch quantisation");
            rep.check (recOk, "Session clips recorded into the arrangement", recInfo);
        }

        // --- quality: backup history, heavy-project stress render -------------------------------------
        {
            rep.step ("backups");
            const auto editFile = te::EditFileOperations (*edit).getEditFile();
            Backups::folderFor (editFile).deleteRecursively();
            int made = 0;
            for (int i = 0; i < 23; ++i)
            {
                auto f = Backups::write (*edit);
                if (f.existsAsFile()) { ++made; f.setLastModificationTime (Time::getCurrentTime() - RelativeTime::minutes (60 - i)); }
            }
            auto all = Backups::list (editFile);
            auto copy = all.isEmpty() ? File() : Backups::restoreAsCopy (all.getFirst(), editFile);
            bool reopened = false;
            if (copy.existsAsFile())
            {
                auto e2 = te::loadEditFromFile (engine, copy);
                reopened = e2 != nullptr && EngineHelpers::getUserTracks (*e2).size() == EngineHelpers::getUserTracks (*edit).size();
                e2 = nullptr;
                copy.deleteFile();
            }
            rep.check (made == 23 && all.size() == Backups::keep && reopened, "Backup history (newest 20 kept, restore as a copy)",
                       String (made) + " written, " + String (all.size()) + " kept");
            Backups::folderFor (editFile).deleteRecursively();

            rep.step ("stress render");
            const int before = EngineHelpers::getUserTracks (*edit).size();
            Array<te::AudioTrack*> heavy;
            for (int i = 0; i < 16; ++i)
                if (auto t = ClipImport::createContentTrack (*edit, "Stress " + String (i + 1), true))
                {
                    heavy.add (t);
                    for (auto type : { pulse::WavetableSynth::xmlTypeName, pulse::PulseEQ::xmlTypeName, pulse::GlueCompressor::xmlTypeName,
                                       pulse::Reverb::xmlTypeName, pulse::Delay::xmlTypeName })
                        PluginMenu::insertPlugin (t, t->pluginList, edit->getPluginCache().createNewPlugin (type, {}), nullptr);
                    if (auto mc = t->insertMIDIClip ("S", { te::TimePosition(), te::TimePosition::fromSeconds (8.0) }, nullptr))
                        for (int k = 0; k < 32; ++k)
                            for (int v = 0; v < 3; ++v)   // chords: 3 notes, 3 unison voices each
                                mc->getSequence().addNote (48 + (i * 5 + v * 4) % 24, te::BeatPosition::fromBeats (k * 0.5), te::BeatDuration::fromBeats (0.45), 90, 0, nullptr);
                    if (auto vp = t->getVolumePlugin()) vp->setVolumeDb (-24.0f);
                }
            EditingOps::waitForClipProxies (*edit);
            te::Renderer::Parameters rp (*edit);
            Array<te::Track*> tracksToDo;
            for (auto t : heavy) tracksToDo.add (t);
            rp.tracksToDo = te::toBitSet (tracksToDo);
            rp.destFile = dir.getChildFile ("stress.wav");
            rp.destFile.deleteFile();
            rp.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
            rp.bitDepth = 32; rp.sampleRateForAudio = 48000.0;
            rp.time = { te::TimePosition(), te::TimePosition::fromSeconds (8.0) };
            rp.usePlugins = true;
            const auto t0 = Time::getMillisecondCounterHiRes();
            te::Renderer::renderToFile ("stress", rp);
            const double secs = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
            bool finite = false;
            float peak = 0;
            if (std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (rp.destFile)); r != nullptr)
            {
                AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                finite = true;
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                    {
                        const float v = b.getSample (ch, i);
                        if (! std::isfinite (v)) finite = false;
                        peak = jmax (peak, std::abs (v));
                    }
            }
            const double speed = secs > 0 ? 8.0 / secs : 0.0;
            rep.check (finite && peak > 0.001f && speed > 1.0, "Stress render: 16 tracks x (Wavetable 3-voice unison chords + EQ + Glue + Reverb + Delay)",
                       String (speed, 1) + "x real time, peak " + String (Decibels::gainToDecibels (peak), 1) + " dB, finite " + String ((int) finite));
            for (auto t : heavy) edit->deleteTrack (t);
            jassert (EngineHelpers::getUserTracks (*edit).size() == before);
        }

        // --- Push encoder assignments ------------------------------------------------------------------
        {
            auto t = ClipImport::createContentTrack (*edit, "PushMap", true);
            bool ok = false;
            if (t != nullptr)
            {
                auto eq = edit->getPluginCache().createNewPlugin (pulse::PulseEQ::xmlTypeName, {});
                PluginMenu::insertPlugin (t, t->pluginList, eq, nullptr);
                auto* prm = eq->getAutomatableParameters()[5];
                PushMap::assign (*t, 2, *prm);
                const bool found = PushMap::find (*t, 2) == prm && PushMap::find (*t, 3) == nullptr;
                PushMap::assign (*t, 2, *eq->getAutomatableParameters()[1]);   // replaces
                const bool replaced = PushMap::find (*t, 2) == eq->getAutomatableParameters()[1];
                PushMap::clear (*t, 2);
                ok = found && replaced && PushMap::find (*t, 2) == nullptr;
                edit->deleteTrack (t);
            }
            rep.check (ok, "Push encoder assignments (assign, replace, clear)");
        }

        // --- clip gain envelope, groove extraction ---------------------------------------------------------
        {
            rep.step ("clip gain / groove");
            auto t = ClipImport::createContentTrack (*edit, "ClipGain", false);
            bool gainOk = false;
            String gainInfo;
            if (t != nullptr)
            {
                auto src = writeTestWav (dir.getChildFile ("gain_src.wav"), 1.0, 220.0);   // -6 dBFS
                te::Clip::Ptr c = ClipImport::importFile (*t, *edit, src, te::TimePosition());
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c.get()))
                {
                    ClipGain::setPoints (*w, { { 0.0, 0.0f }, { 0.5, -20.0f }, { 1.0, -20.0f } }, nullptr);
                    ClipGain::apply (*w);
                    auto rendered = w->getOriginalFile();
                    auto peakIn = [&engine] (const File& f, double a, double b)
                    {
                        std::unique_ptr<AudioFormatReader> r (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));
                        if (r == nullptr) return -200.0f;
                        Range<float> rg[2];
                        r->readMaxLevels ((int64) (a * r->sampleRate), (int64) ((b - a) * r->sampleRate), rg, 1);
                        return Decibels::gainToDecibels (jmax (std::abs (rg[0].getStart()), std::abs (rg[0].getEnd())), -200.0f);
                    };
                    const float start = peakIn (rendered, 0.0, 0.05), late = peakIn (rendered, 0.6, 0.95);
                    const bool originalKept = std::abs (peakIn (src, 0.6, 0.95) + 6.0f) < 0.3f;
                    {
                        PulseLookAndFeel laf;
                        LookAndFeel::setDefaultLookAndFeel (&laf);
                        {
                            AudioClipPanel panel (*w);
                            auto img = panel.createComponentSnapshot (panel.getLocalBounds());
                            auto f = dir.getChildFile ("clippanel.png");
                            f.deleteFile();
                            if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                        }
                        LookAndFeel::setDefaultLookAndFeel (nullptr);
                    }
                    ClipGain::setPoints (*w, {}, nullptr);
                    ClipGain::apply (*w);
                    const bool restored = w->getOriginalFile() == src;
                    gainOk = rendered != src && std::abs (start + 6.0f) < 0.5f && std::abs (late + 26.0f) < 0.5f && originalKept && restored;
                    gainInfo = String (start, 1) + " dB at the start, " + String (late, 1) + " dB after the ramp, restored " + String ((int) restored);
                }
                c = nullptr;
                edit->deleteTrack (t);
            }
            rep.check (gainOk, "Clip gain envelope (rendered copy, original kept)", gainInfo);

            // groove: a swung clip (every second 16th late by a third) gives a swing groove
            auto gtTrack = ClipImport::createContentTrack (*edit, "Groove", true);
            bool grooveOk = false;
            String gInfo;
            if (gtTrack != nullptr)
            {
                auto swung = gtTrack->insertMIDIClip ("Swung", { te::TimePosition(), EngineHelpers::beatToTime (*edit, 8.0) }, nullptr);
                for (int i = 0; i < 32; ++i)
                    swung->getSequence().addNote (42, te::BeatPosition::fromBeats (i * 0.25 + (i % 2 == 1 ? 0.25 / 3.0 : 0.0)), te::BeatDuration::fromBeats (0.1), 100, 0, nullptr);
                auto late = Groove::latenessesOf (*swung);
                const auto name = Groove::extractGroove (*swung);
                auto straight = gtTrack->insertMIDIClip ("Straight", { EngineHelpers::beatToTime (*edit, 8.0), EngineHelpers::beatToTime (*edit, 12.0) }, nullptr);
                straight->getSequence().addNote (42, te::BeatPosition::fromBeats (0.25), te::BeatDuration::fromBeats (0.1), 100, 0, nullptr);
                Groove::set (*straight, name, 1.0f);
                Groove::commit (*straight);
                const double moved = straight->getSequence().getNotes()[0]->getStartBeat().inBeats();
                grooveOk = late.size() == 16 && std::abs (late[1] - 1.0f / 3.0f) < 0.02f && std::abs (late[0]) < 0.02f && name.isNotEmpty() && moved > 0.27;
                gInfo = "odd 16ths " + String (late.empty() ? 0.0f : late[1], 3) + ", note moved to " + String (moved, 3);
                if (auto idx = edit->engine.getGrooveTemplateManager().getTemplateNames().indexOf (name); idx >= 0)
                    edit->engine.getGrooveTemplateManager().deleteTemplate (idx);
                edit->deleteTrack (gtTrack);
            }
            rep.check (grooveOk, "Extract a groove from a clip and apply it", gInfo);
        }

        // --- factory content: presets, kits, templates ---------------------------------------------------
        {
            rep.step ("factory content");
            int presets = 0, missing = 0, silent = 0;
            StringArray problems, offLevel;
            for (auto& [type, list] : FactoryPresets::all())
                for (auto& pr : list)
                {
                    auto p = edit->getPluginCache().createNewPlugin (type, {});
                    if (p == nullptr) { problems.add (type); continue; }
                    ++presets;
                    for (auto& [id, v] : pr.values)
                        if (! String (id).startsWith ("mod:") && p->getAutomatableParameterByID (id) == nullptr) { ++missing; problems.add (type + "." + id); }
                    FactoryPresets::apply (*p, pr.name);
                    if (p->isSynth() && type != pulse::DrumSynth::xmlTypeName && type != pulse::Simpler::xmlTypeName)   // Simpler needs a sample
                    {
                        // a held chord must make sound
                        const double sr = 48000.0;
                        const int block = 480;
                        p->initialise ({ te::TimePosition(), sr, block });
                        AudioBuffer<float> audio (2, block);
                        float peak = 0;
                        const auto src = te::createUniqueMPESourceID();
                        for (int k = 0; k < 100; ++k)
                        {
                            audio.clear();
                            te::MidiMessageArray midi;
                            if (k == 0) for (int n : { 48, 55, 60 }) midi.addMidiMessage (MidiMessage::noteOn (1, n, (uint8) 100), 0.0, src);
                            te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * 0.01), te::TimePosition::fromSeconds ((k + 1) * 0.01) },
                                                        true, false, true, false);
                            p->applyToBuffer (fc);
                            peak = jmax (peak, audio.getMagnitude (0, 0, block));
                        }
                        p->deinitialise();
                        if (Decibels::gainToDecibels (peak) < -40.0f) { ++silent; problems.add (String (pr.name) + " silent"); }
                        // a three-note chord at a firm velocity should sit between -30 and +1 dBFS (driven presets saturate at about 0)
                        const float pdb = Decibels::gainToDecibels (peak);
                        if (pdb > 1.0f || pdb < -30.0f) offLevel.add (String (pr.name) + " " + String (pdb, 1));
                        rep.step ("  level " + String (pr.name) + ": " + String (pdb, 1) + " dB");
                    }
                }
            rep.check (offLevel.isEmpty(), "Instrument presets play at sensible levels (a chord peaks between -30 and +1 dBFS)",
                       offLevel.isEmpty() ? String ("all in range") : offLevel.joinIntoString (", "));
            StringArray thin;
            for (auto& b : ClipImport::builtIns())
            {
                const String t (b.type);
                if (t == pulse::Tuner::xmlTypeName || t == pulse::Spectrum::xmlTypeName) continue;
                auto test = edit->getPluginCache().createNewPlugin (t, {});
                if (dynamic_cast<pulse::PulsePlugin*> (test.get()) == nullptr) continue;
                if (FactoryPresets::forType (t).size() < 4) thin.add (b.name);
            }
            rep.check (thin.isEmpty(), "Every Pulse device has at least 4 factory presets", thin.isEmpty() ? String ("ok") : thin.joinIntoString (", "));
            rep.check (presets >= 150 && missing == 0 && silent == 0, "Factory presets (every id exists, every sound plays)",
                       String (presets) + " presets" + (problems.isEmpty() ? String() : ": " + problems.joinIntoString (", ")));

            auto rp = DrumRack::create (*edit, false);
            bool kitOk = false;
            if (auto rack = DrumRack::rackOf (rp.get()))
            {
                DrumRack::fillSynthKit (*rack, 1);
                const float decay808 = DrumRack::source (*rack, 36)->get (pulse::DrumPad::pSynthDecay);
                DrumRack::fillSynthKit (*rack, 2);
                const float decay909 = DrumRack::source (*rack, 36)->get (pulse::DrumPad::pSynthDecay);
                kitOk = decay808 > 1.5f && decay909 < 1.0f && DrumRack::usedPads (*rack).size() == 11;
            }
            rep.check (kitOk, "Drum kit styles (808 / 909 / lo-fi / electro)");

            FactoryContent::folder (engine).deleteRecursively();
            const int written = FactoryContent::ensureTemplates (engine);
            auto files = FactoryContent::templates (engine);
            int opened = 0;
            float housePeak = -200.0f;
            for (auto& f : files)
                if (auto ed = te::loadEditFromFile (engine, f); ed != nullptr && EngineHelpers::getUserTracks (*ed).size() >= 3)
                {
                    ++opened;
                    if (f.getFileNameWithoutExtension() == "House")
                    {
                        ed->getTransport().ensureContextAllocated();
                        EditingOps::waitForClipProxies (*ed);
                        auto out = dir.getChildFile ("house_template.wav");
                        out.deleteFile();
                        te::Renderer::renderToFile (*ed, out, false);
                        housePeak = peakDb (engine, out);
                    }
                }
            rep.check (written == 4 && files.size() == 4 && opened == 4 && housePeak > -30.0f, "Factory project templates (House, Hip-Hop, Ambient, Empty)",
                       String (opened) + " open, House renders at " + String (housePeak, 1) + " dBFS");
        }

        // --- drawn wavetable -------------------------------------------------------------------------------
        {
            rep.step ("user wavetable");
            using W = pulse::WavetableSynth;
            std::vector<float> sine (128), square (128);
            for (int i = 0; i < 128; ++i) { sine[(size_t) i] = std::sin (MathConstants<float>::twoPi * i / 128.0f); square[(size_t) i] = i < 64 ? 1.0f : -1.0f; }
            auto table = pulse::WavetableBank::buildUser (sine, square);
            float err = 0;
            const auto* t0 = table->table (0, 0);
            for (int i = 0; i < pulse::WavetableBank::tableSize; ++i)
                err = jmax (err, std::abs (t0[i] - 0.9f * std::sin (MathConstants<float>::twoPi * i / (float) pulse::WavetableBank::tableSize)));
            // a top-octave level keeps few harmonics: the square's last frame is smoothed there
            const auto* hiLevel = table->table (15, 9);
            float ripple = 0;
            for (int i = 1; i < pulse::WavetableBank::tableSize; ++i) ripple = jmax (ripple, std::abs (hiLevel[i] - hiLevel[i - 1]));

            auto wv = edit->getPluginCache().createNewPlugin (W::xmlTypeName, {});
            float sineB = 0, squareB = 0;
            if (auto* w = dynamic_cast<W*> (wv.get()))
            {
                const double sr = 48000.0;
                const int block = 480;
                auto render = [&] (float pos)
                {
                    w->set (W::pSet1, (float) pulse::WavetableBank::userSet); w->set (W::pPos1, pos); w->set (W::pMix, 0.0f);
                    w->set (W::pVoices, 1.0f); w->set (W::pFilterType, 0.0f); w->set (W::pCutoff, 20000.0f); w->set (W::pFilterEnv, 0.0f);
                    w->set (W::pLfoPos, 0.0f); w->set (W::pSub, 0.0f);
                    w->initialise ({ te::TimePosition(), sr, block });
                    AudioBuffer<float> audio (2, block);
                    double e = 0, d = 0;
                    const auto src = te::createUniqueMPESourceID();
                    float prev = 0;
                    for (int k = 0; k < 40; ++k)
                    {
                        audio.clear();
                        te::MidiMessageArray midi;
                        if (k == 0) midi.addMidiMessage (MidiMessage::noteOn (1, 57, (uint8) 100), 0.0, src);
                        te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * 0.01), te::TimePosition::fromSeconds ((k + 1) * 0.01) }, true, false, true, false);
                        w->applyToBuffer (fc);
                        if (k > 10)
                            for (int i = 0; i < block; ++i) { const float x = audio.getSample (0, i); e += x * x; d += (x - prev) * (x - prev); prev = x; }
                        else prev = audio.getSample (0, block - 1);
                    }
                    w->deinitialise();
                    return e > 0 ? (float) std::sqrt (d / e) : 0.0f;
                };
                w->setUserWave (0, sine);
                w->setUserWave (1, square);
                sineB = render (0.0f);
                squareB = render (1.0f);

                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                {
                    DeviceDisplays::WaveDrawEditor ed (*w);
                    auto img = ed.createComponentSnapshot (ed.getLocalBounds());
                    auto f = dir.getChildFile ("wavedraw.png");
                    f.deleteFile();
                    if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (err < 0.01f && ripple < 0.05f && sineB > 0.0f && squareB > sineB * 2.0f, "Drawn wavetable (band-limited, morphs start -> end)",
                       "sine error " + String (err, 4) + ", brightness " + String (sineB, 3) + " -> " + String (squareB, 3));
        }

        // --- v0.36: drop names, note entry, device editors, returns -------------------------------------
        {
            rep.step ("names / note entry / device editors");
            const bool names = ClipImport::trackNameFor (engine, "plugin:builtin:drumrackkit") == "Drums"
                               && ClipImport::trackNameFor (engine, "plugin:builtin:drumrack") == "Drums"
                               && ClipImport::trackNameFor (engine, String ("plugin:builtin:") + pulse::DrumSynth::xmlTypeName) == "Drums"
                               && ClipImport::trackNameFor (engine, String ("plugin:builtin:") + pulse::WavetableSynth::xmlTypeName) == "Wavetable"
                               && ClipImport::trackNameFor (engine, "plugin:factory:reverb:Hall") == "Hall"
                               && ClipImport::trackNameFor (engine, "plugin:chain:C:/x/My Chain.pulsechain") == "My Chain";
            String bad;
            for (auto& b : ClipImport::builtIns())
            {
                const auto n = ClipImport::trackNameFor (engine, String ("plugin:builtin:") + b.type);
                bool camel = n.containsChar ('_');
                for (int i = 0; i + 1 < n.length(); ++i) camel = camel || (CharacterFunctions::isLowerCase (n[i]) && CharacterFunctions::isUpperCase (n[i + 1]));
                if (camel) bad << n << " ";
                if (n == "Track" || n.isEmpty()) bad << b.type << " ";
            }
            rep.check (names && bad.isEmpty(), "Dropped devices name their track in plain words", bad.isEmpty() ? String ("Drums, Wavetable, Hall, My Chain") : bad);

            // piano roll: a click adds a note one grid step long; dragging draws it longer; edges resize
            PulseLookAndFeel laf;
            LookAndFeel::setDefaultLookAndFeel (&laf);
            {
                EditViewState evs (*edit, sm);
                auto t = ClipImport::createContentTrack (*edit, "NoteEntry", true);
                auto mc = dynamic_cast<te::MidiClip*> (t->insertNewClip (te::TrackItem::Type::midi, "n", { te::TimePosition(), te::TimePosition::fromSeconds (8.0) }, nullptr));
                PianoRoll pr (evs);
                pr.setSize (1200, 700);
                pr.setClip (mc);
                auto src = Desktop::getInstance().getMainMouseSource();
                auto send = [&] (int kind, juce::Point<float> down, juce::Point<float> pos)
                {
                    const auto now = Time::getCurrentTime();
                    MouseEvent e (src, pos, ModifierKeys (kind == 2 ? 0 : ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &pr, &pr, now, down, now, 1, kind == 1 && pos != down);
                    if (kind == 0) pr.mouseDown (e); else if (kind == 1) pr.mouseDrag (e); else pr.mouseUp (e);
                };
                auto click = [&] (juce::Point<float> a, juce::Point<float> b)
                {
                    send (0, a, a);
                    if (b != a) send (1, a, b);
                    send (2, a, b);
                };
                auto& seq = mc->getSequence();
                const int pitch = 60;
                auto lenOf = [&] (int i) { return seq.getNumNotes() > i ? seq.getNote (i)->getLengthBeats().inBeats() : -1.0; };
                String info;

                pr.setGrid (3);   // 1/16
                click (pr.positionOf (0.1, pitch), pr.positionOf (0.1, pitch));
                const bool sixteenth = seq.getNumNotes() == 1 && std::abs (lenOf (0) - 0.25) < 1e-6 && std::abs (seq.getNote (0)->getStartBeat().inBeats()) < 1e-6;
                info << "1/16 click " << String (lenOf (0), 3);

                pr.setGrid (2);   // 1/8: drawing a new note right of it, dragged ~1.3 steps -> 2 steps
                click (pr.positionOf (2.05, pitch), pr.positionOf (2.0 + 0.5 * 1.3, pitch));
                te::MidiNote* drawn = nullptr;
                for (auto n : seq.getNotes()) if (std::abs (n->getStartBeat().inBeats() - 2.0) < 1e-6) drawn = n;
                const bool drawnOk = drawn != nullptr && std::abs (drawn->getLengthBeats().inBeats() - 1.0) < 1e-6;
                info << ", 1/8 drag " << (drawn != nullptr ? String (drawn->getLengthBeats().inBeats(), 3) : String ("none"));

                // grab the drawn note's end edge and stretch it to beat 4
                bool resized = false, startMoved = false;
                if (drawn != nullptr)
                {
                    const auto edgeP = pr.positionOf (2.98, pitch);
                    click (edgeP, pr.positionOf (4.0, pitch));
                    resized = std::abs (drawn->getStartBeat().inBeats() - 2.0) < 1e-6 && std::abs (drawn->getLengthBeats().inBeats() - 2.0) < 1e-6;
                    info << ", end drag " << String (drawn->getLengthBeats().inBeats(), 3);
                    // its start edge: move the start to beat 3, end stays at 4
                    click (pr.positionOf (2.02, pitch), pr.positionOf (3.02, pitch));
                    startMoved = std::abs (drawn->getStartBeat().inBeats() - 3.0) < 1e-6 && std::abs (drawn->getEndBeat().inBeats() - 4.0) < 1e-6;
                    info << ", start drag " << String (drawn->getStartBeat().inBeats(), 3) << "-" << String (drawn->getEndBeat().inBeats(), 3);
                }
                rep.check (sixteenth && drawnOk && resized && startMoved && seq.getNumNotes() == 2, "Piano roll note entry (grid length, draw, resize both edges)", info);
                pr.setClip (nullptr);
                t->getClips().getFirst()->removeFromParent();
                edit->deleteTrack (t);
            }

            // the big device editor builds for every Pulse device, with graphs (snapshots: editor_<type>.png)
            {
                EditViewState evs (*edit, sm);
                auto t = ClipImport::createContentTrack (*edit, "EditorTest", true);
                int built = 0, withGraphs = 0, total = 0;
                String missing;
                for (auto& b : ClipImport::builtIns())
                {
                    auto pl = edit->getPluginCache().createNewPlugin (b.type, {});
                    auto pp = dynamic_cast<pulse::PulsePlugin*> (pl.get());
                    if (pp == nullptr) continue;
                    ++total;
                    t->pluginList.insertPlugin (pl, 0, nullptr);
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        auto img = ed.createComponentSnapshot (ed.getLocalBounds());
                        if (img.isValid()) ++built;
                        if (ed.getNumGraphs() > 0) ++withGraphs; else missing << b.name << " ";
                        auto f = dir.getChildFile ("editor_" + File::createLegalFileName (b.name) + ".png");
                        f.deleteFile();
                        if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                    }
                    pl->deleteFromParent();
                }
                rep.check (total > 10 && built == total && withGraphs == total, "Big device editor for every Pulse device (graphs + controls)",
                           String (built) + "/" + String (total) + " built" + (missing.isNotEmpty() ? ", no graphs: " + missing : String()));
                edit->deleteTrack (t);
            }
            LookAndFeel::setDefaultLookAndFeel (nullptr);

            // returns are made of Pulse devices whose settings are reachable
            {
                auto ret = Routing::createReturnTrack (*edit, "TestReverb", pulse::Reverb::xmlTypeName);
                pulse::Reverb* r = ret != nullptr ? ret->pluginList.findFirstPluginOfType<pulse::Reverb>() : nullptr;
                bool ok = false;
                if (r != nullptr)
                {
                    FactoryPresets::apply (*r, "Hall");
                    r->set (pulse::Reverb::pMix, 1.0f);
                    ok = r->get (pulse::Reverb::pMix) > 0.99f && r->numParams() > 3;
                }
                rep.check (ok, "Reverb return uses the editable Pulse Reverb");
                if (ret != nullptr) edit->deleteTrack (ret);
            }
        }

        // --- v0.37: old projects upgrade, return / master rows ------------------------------------------
        {
            rep.step ("upgrade / bus rows");
            // raw names from older versions become plain words; user names stay
            const bool friendly = ClipImport::friendlyName (engine, "pulseDrumSynth") == "Drums" && ClipImport::friendlyName (engine, "drumrackkit") == "Drums"
                                  && ClipImport::friendlyName (engine, String (pulse::WavetableSynth::xmlTypeName)) == "Wavetable"
                                  && ClipImport::friendlyName (engine, "My Lead") == "My Lead" && ClipImport::friendlyName (engine, "Bass 2") == "Bass 2";
            auto raw = ClipImport::createContentTrack (*edit, "pulseDrumSynth", false);
            const int fixedNames = ClipImport::fixRawTrackNames (*edit);
            rep.check (friendly && raw->getName() == "Drums" && fixedNames >= 1 && raw->state.getProperty (ClipImport::autoNameID),
                       "Old raw track names fixed on open", raw->getName());
            edit->deleteTrack (raw);

            // old engine reverb / delay returns are swapped for the editable Pulse devices
            auto oldRev = Routing::createReturnTrack (*edit, "OldVerb", te::ReverbPlugin::xmlTypeName);
            auto oldDel = Routing::createReturnTrack (*edit, "OldDelay", te::DelayPlugin::xmlTypeName);
            if (auto d = oldDel->pluginList.findFirstPluginOfType<te::DelayPlugin>()) { d->lengthMs = 250; d->mixValue = 1.0f; d->feedbackValue = -6.0f; }
            const int swapped = Upgrade::upgradeReturns (*edit);
            auto pr = oldRev->pluginList.findFirstPluginOfType<pulse::Reverb>();
            auto pd = oldDel->pluginList.findFirstPluginOfType<pulse::Delay>();
            const bool kept = pd != nullptr && std::abs (pd->get (pulse::Delay::pTime) - 250.0f) < 1.0f && pd->get (pulse::Delay::pSync) < 0.5f
                              && std::abs (pd->get (pulse::Delay::pFeedback) - 0.5f) < 0.02f;
            const bool orderOk = oldRev->pluginList.size() > 1 && dynamic_cast<te::AuxReturnPlugin*> (oldRev->pluginList[0]) != nullptr
                                 && dynamic_cast<pulse::Reverb*> (oldRev->pluginList[1]) != nullptr;
            rep.check (swapped >= 2 && pr != nullptr && kept && orderOk && oldRev->pluginList.findFirstPluginOfType<te::ReverbPlugin>() == nullptr,
                       "Old reverb / delay returns become editable Pulse devices", String (swapped) + " swapped");

            // bus rows: one per return plus the master; devices dropped on the master land before its meter
            {
                EditViewState evs (*edit, sm);
                bool shown = false; te::Track* shownTrack = (te::Track*) 1;
                evs.showDevices = [&] (te::Track* t) { shown = true; shownTrack = t; };
                BusLanes::Area area (evs, 230);
                area.setSize (1200, area.idealHeight());
                const int returns = Routing::getReturnTracks (*edit).size();
                auto* masterRow = area.row (area.numRows() - 1);
                // click the master row
                auto src = Desktop::getInstance().getMainMouseSource();
                const auto now = Time::getCurrentTime();
                MouseEvent e (src, { 100.0f, 10.0f }, ModifierKeys (ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              masterRow, masterRow, now, { 100.0f, 10.0f }, now, 1, false);
                masterRow->mouseDown (e);
                auto& ml = edit->getMasterPluginList();
                if (ml.findFirstPluginOfType<te::LevelMeterPlugin>() == nullptr)
                    ml.insertPlugin (edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);
                auto eq = edit->getPluginCache().createNewPlugin (pulse::PulseEQ::xmlTypeName, {});
                ml.insertPlugin (eq, PluginMenu::insertIndexFor (ml, nullptr), nullptr);
                const bool beforeMeter = ml.indexOf (eq.get()) < ml.indexOf (ml.findFirstPluginOfType<te::LevelMeterPlugin>());
                const int chips = BusLanes::userDevices (ml).size();
                rep.check (area.numRows() == returns + 1 && masterRow->getTrack() == nullptr && shown && shownTrack == nullptr && beforeMeter && chips >= 1,
                           "Return and master rows (click opens devices, master devices before its meter)",
                           String (area.numRows()) + " rows, master devices " + String (chips));
                eq->deleteFromParent();
            }
            edit->deleteTrack (oldRev);
            edit->deleteTrack (oldDel);
        }

        // --- v0.38: MIDI effect previews, vocoder / utility views -----------------------------------------
        {
            rep.step ("device views");
            ReferenceCountedArray<te::Plugin> keep;
            auto make = [&] (const char* type, const char* preset) -> pulse::PulseMidiEffect*
            {
                auto p = edit->getPluginCache().createNewPlugin (type, {});
                keep.add (p);
                if (preset != nullptr) FactoryPresets::apply (*p, preset);
                return dynamic_cast<pulse::PulseMidiEffect*> (p.get());
            };
            using DV = DeviceViews::MidiFxPreview;
            auto arp = DV::run (*make (pulse::Arpeggiator::xmlTypeName, "Classic Up 1/16"), 120.0);
            int arpInBar1 = 0;
            for (auto& n : arp) if (n.start < 4.0) ++arpInBar1;
            auto chord = DV::run (*make (pulse::ChordFx::xmlTypeName, "Major Triad"), 120.0);
            auto scale = DV::run (*make (pulse::ScaleFx::xmlTypeName, "C Major"), 120.0);
            bool outOfKey = false;
            for (auto& n : scale) outOfKey = outOfKey || MidiMessage::isMidiNoteBlack (n.pitch);
            auto echo = DV::run (*make (pulse::NoteEcho::xmlTypeName, "1/8 Echoes"), 120.0);
            const int inCount = (int) DV::inputPhrase().size();
            rep.check (arpInBar1 >= 12 && (int) chord.size() >= inCount * 3 && ! scale.empty() && ! outOfKey && (int) echo.size() > inCount,
                       "MIDI effect previews show what each effect does",
                       "arp " + String (arpInBar1) + " notes in bar 1, chord " + String ((int) chord.size()) + ", scale in key " + String (outOfKey ? "no" : "yes")
                         + ", echo " + String ((int) echo.size()));
        }

        // --- v0.39: draw tool, Alt-drag copies, ghost notes, lane heights -----------------------------------
        {
            rep.step ("piano roll tools / lane heights");
            PulseLookAndFeel laf;
            LookAndFeel::setDefaultLookAndFeel (&laf);
            {
                EditViewState evs (*edit, sm);
                auto t = ClipImport::createContentTrack (*edit, "Tools", true);
                auto t2 = ClipImport::createContentTrack (*edit, "Other", true);
                auto mc = dynamic_cast<te::MidiClip*> (t->insertNewClip (te::TrackItem::Type::midi, "n", { te::TimePosition(), EngineHelpers::beatToTime (*edit, 8.0) }, nullptr));
                auto other = dynamic_cast<te::MidiClip*> (t2->insertNewClip (te::TrackItem::Type::midi, "o", { te::TimePosition(), EngineHelpers::beatToTime (*edit, 8.0) }, nullptr));
                for (int i = 0; i < 4; ++i) other->getSequence().addNote (60 + i, te::BeatPosition::fromBeats (i), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
                PianoRoll pr (evs);
                pr.setSize (1200, 700);
                pr.setClip (mc);
                auto src = Desktop::getInstance().getMainMouseSource();
                auto send = [&] (int kind, juce::Point<float> down, juce::Point<float> pos, ModifierKeys extra)
                {
                    const auto now = Time::getCurrentTime();
                    MouseEvent e (src, pos, kind == 2 ? extra : extra.withFlags (ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  &pr, &pr, now, down, now, 1, kind == 1 && pos != down);
                    if (kind == 0) pr.mouseDown (e); else if (kind == 1) pr.mouseDrag (e); else pr.mouseUp (e);
                };
                auto& seq = mc->getSequence();
                pr.setGrid (3);   // 1/16
                pr.setDrawTool (true);
                // paint across one bar at C4: 16 notes
                const auto a = pr.positionOf (0.01, 64), b = pr.positionOf (3.99, 64);
                send (0, a, a, {});
                for (int i = 1; i <= 40; ++i) send (1, a, a + (b - a) * (i / 40.0f), {});
                send (2, a, b, {});
                const int painted = seq.getNumNotes();
                pr.setDrawTool (false);
                // Alt-drag the first note up an octave: a copy moves, the original stays
                auto first = seq.getNote (0);
                const auto fp = pr.positionOf (first->getStartBeat().inBeats() + 0.1, first->getNoteNumber());
                const auto to = pr.positionOf (first->getStartBeat().inBeats() + 0.1, first->getNoteNumber() + 12);
                send (0, fp, fp, ModifierKeys (ModifierKeys::altModifier));
                send (1, fp, to, ModifierKeys (ModifierKeys::altModifier));
                send (2, fp, to, ModifierKeys (ModifierKeys::altModifier));
                int at64 = 0, at76 = 0;
                for (auto n : seq.getNotes()) { if (n->getNoteNumber() == 64) ++at64; if (n->getNoteNumber() == 76) ++at76; }
                const int ghosts = pr.numGhostNotes();
                rep.check (painted == 16 && at64 == 16 && at76 == 16 && ghosts >= 4, "Piano roll draw tool, Alt-drag copy and ghost notes",
                           String (painted) + " painted, copy " + String (at76) + ", ghosts " + String (ghosts));
                pr.setClip (nullptr);

                // lane heights: per track, clamped
                evs.setBaseHeight (*t, 40);
                const int h40 = evs.laneHeight (*t);
                evs.setBaseHeight (*t, 5000);
                const int hMax = evs.laneHeight (*t);
                rep.check (h40 == 40 && hMax == EditViewState::maxLaneHeight && evs.laneHeight (*t2) == evs.trackHeight, "Per-track lane heights",
                           String (h40) + " / " + String (hMax));
                edit->deleteTrack (t);
                edit->deleteTrack (t2);
            }
            LookAndFeel::setDefaultLookAndFeel (nullptr);
        }

        // --- v0.41: colour themes ----------------------------------------------------------------------------
        {
            rep.step ("themes");
            auto lumOf = [] (Colour c)
            {
                auto ch = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); };
                return 0.2126f * ch (c.getFloatRed()) + 0.7152f * ch (c.getFloatGreen()) + 0.0722f * ch (c.getFloatBlue());
            };
            auto contrast = [&] (Colour a, Colour b) { const float x = lumOf (a), y = lumOf (b); return (jmax (x, y) + 0.05f) / (jmin (x, y) + 0.05f); };
            StringArray weak;
            for (int i = 0; i < (int) Theme::all().size(); ++i)
            {
                Theme::apply (i);
                const float t = jmin (contrast (Theme::text, Theme::bg), contrast (Theme::text, Theme::panel), contrast (Theme::text, Theme::panel2));
                const float m = jmin (contrast (Theme::muted, Theme::bg), contrast (Theme::muted, Theme::panel));
                if (t < 7.0f || m < 3.0f) weak.add (String (Theme::all()[(size_t) i].name) + " (" + String (t, 1) + "/" + String (m, 1) + ")");
            }
            // a live switch recolours colours set on components when they were built
            Theme::apply (0);
            TextButton b ("x");
            b.setColour (TextButton::buttonOnColourId, Theme::accentDim);
            b.setColour (TextButton::buttonColourId, Theme::panel2.withAlpha (0.5f));
            const auto before = Theme::tokens();
            Theme::apply (Theme::indexOf ("Daylight"));
            Theme::remapColours (b, before, Theme::tokens());
            const bool remapped = b.findColour (TextButton::buttonOnColourId) == Theme::accentDim
                                  && b.findColour (TextButton::buttonColourId) == Theme::panel2.withAlpha (0.5f);
            Theme::apply (0);
            rep.check ((int) Theme::all().size() >= 15 && weak.isEmpty() && remapped, "Colour themes (readable text in every theme, live switch)",
                       String ((int) Theme::all().size()) + " themes" + (weak.isEmpty() ? String() : ", weak contrast: " + weak.joinIntoString (", ")));
        }

        // --- v0.42: Prism EQ -------------------------------------------------------------------------------
        {
            rep.step ("Prism EQ");
            using EQ = pulse::PrismEQ;
            const double sr = 48000.0;
            auto at = [&] (int type, double f, double gain, double q, int slope, double probe)
            {
                EQ::Design d;
                EQ::design (d, type, sr, f, gain, q, slope);
                return Decibels::gainToDecibels (d.magnitude (probe, sr), -200.0);
            };
            auto near = [] (double a, double b, double tol) { return std::abs (a - b) <= tol; };
            StringArray bad;
            auto expect = [&] (bool ok, const String& what) { if (! ok) bad.add (what); };
            expect (near (at (EQ::bell, 1000, 6, 1, 0, 1000), 6, 0.05), "bell +6 at f");
            expect (near (at (EQ::bell, 1000, -12, 4, 0, 1000), -12, 0.05) && at (EQ::bell, 1000, -12, 4, 0, 3000) > -1.0, "narrow bell");
            expect (near (at (EQ::lowShelf, 200, 6, 0.71, 1, 20), 6, 0.3) && near (at (EQ::lowShelf, 200, 6, 0.71, 1, 10000), 0, 0.1), "low shelf");
            expect (near (at (EQ::highShelf, 4000, -9, 0.71, 1, 20000), -9, 0.6) && near (at (EQ::highShelf, 4000, -9, 0.71, 1, 50), 0, 0.1), "high shelf");
            expect (near (at (EQ::highShelf, 4000, 6, 1, 0, 23000), 6, 0.4), "6 dB/oct shelf");
            const int slopeDb[] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
            for (int sIdx = 0; sIdx < 9; ++sIdx)
            {
                // one octave below a low cut: about -slope dB (Butterworth: -3 dB at f)
                const double atF = at (EQ::lowCut, 1000, 0, 0.7071, sIdx, 1000), oct = at (EQ::lowCut, 1000, 0, 0.7071, sIdx, 500);
                const double perOct = oct - at (EQ::lowCut, 1000, 0, 0.7071, sIdx, 250);
                expect (near (atF, -3.0, 0.3), "low cut " + String (slopeDb[sIdx]) + " -3 dB at f (" + String (atF, 2) + ")");
                expect (near (perOct, slopeDb[sIdx], slopeDb[sIdx] * 0.1 + 0.6), "low cut slope " + String (slopeDb[sIdx]) + " (" + String (perOct, 1) + ")");
                expect (near (at (EQ::highCut, 1000, 0, 0.7071, sIdx, 100), 0.0, 0.1), "high cut passes lows " + String (slopeDb[sIdx]));
            }
            expect (at (EQ::notch, 1000, 0, 2, 0, 1000) < -60.0 && at (EQ::notch, 1000, 0, 2, 0, 3000) > -0.5, "notch");
            expect (near (at (EQ::bandPass, 1000, 0, 2, 1, 1000), 0, 0.1) && at (EQ::bandPass, 1000, 0, 2, 1, 4000) < -10.0, "band pass");
            expect (near (at (EQ::bandPass, 1000, 0, 2, 6, 1000), 0, 0.1) && at (EQ::bandPass, 1000, 0, 2, 6, 4000) < at (EQ::bandPass, 1000, 0, 2, 1, 4000) - 15.0, "steeper band pass");
            bool flatAll = true;
            for (double f : { 30.0, 300.0, 1000.0, 5000.0, 18000.0 }) flatAll = flatAll && near (at (EQ::allPass, 1000, 0, 1, 0, f), 0, 0.01);
            expect (flatAll, "all pass is flat");
            expect (near (at (EQ::tiltShelf, 1000, 6, 0.71, 1, 1000), 0, 0.3) && near (at (EQ::tiltShelf, 1000, 6, 0.71, 1, 20), -3, 0.4)
                    && near (at (EQ::tiltShelf, 1000, 6, 0.71, 1, 20000), 3, 0.4), "tilt shelf");
            const double ft1k = at (EQ::flatTilt, 1000, 6, 1, 0, 1000), ft20 = at (EQ::flatTilt, 1000, 6, 1, 0, 20), ft20k = at (EQ::flatTilt, 1000, 6, 1, 0, 20000);
            const double ft100 = at (EQ::flatTilt, 1000, 6, 1, 0, 100), ft10k = at (EQ::flatTilt, 1000, 6, 1, 0, 10000);
            expect (near (ft1k, 0, 0.01) && ft20 < -3.0 && ft20k > 3.0 && ft100 < 0 && ft10k > 0 && ft100 > ft20 && ft10k < ft20k, "flat tilt ("
                    + String (ft20, 1) + " / " + String (ft100, 1) + " / " + String (ft10k, 1) + " / " + String (ft20k, 1) + ")");
            // a resonant cut peaks above 0 dB near f
            expect (at (EQ::lowCut, 1000, 0, 6, 3, 1000) > 8.0, "resonant low cut");
            rep.check (bad.isEmpty(), "Prism EQ filter shapes (10 types, 6-96 dB/oct)", bad.isEmpty() ? String ("all within tolerance") : bad.joinIntoString ("; "));

            // audio: a stereo sine through the EQ, measured after it settles
            auto t = ClipImport::createContentTrack (*edit, "Prism", false);
            auto pl = edit->getPluginCache().createNewPlugin (EQ::xmlTypeName, {});
            t->pluginList.insertPlugin (pl, 0, nullptr);
            auto* eq = dynamic_cast<EQ*> (pl.get());
            auto run = [&] (double hzL, double hzR, float ampL, float ampR, double seconds, std::function<void (int)> perBlock = {}) -> std::pair<float, float>
            {
                const int block = 480;
                eq->initialise ({ te::TimePosition(), sr, block });
                AudioBuffer<float> audio (2, block);
                double ph = 0;
                float rl = 0, rr = 0; int counted = 0;
                const int blocks = (int) (seconds * sr / block);
                for (int k = 0; k < blocks; ++k)
                {
                    if (perBlock) perBlock (k);
                    for (int i = 0; i < block; ++i, ph += 1.0)
                    {
                        audio.setSample (0, i, ampL * (float) std::sin (MathConstants<double>::twoPi * hzL * ph / sr));
                        audio.setSample (1, i, ampR * (float) std::sin (MathConstants<double>::twoPi * hzR * ph / sr));
                    }
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                                true, false, true, false);
                    eq->applyToBuffer (fc);
                    if (k >= blocks - 10) { rl += audio.getRMSLevel (0, 0, block); rr += audio.getRMSLevel (1, 0, block); ++counted; }
                }
                eq->deinitialise();
                return { Decibels::gainToDecibels (rl / jmax (1, counted) * 1.4142f), Decibels::gainToDecibels (rr / jmax (1, counted) * 1.4142f) };
            };
            auto setB = [&] (int b, int which, float v) { eq->set (EQ::idx (b, which), v); };
            for (int b = 0; b < EQ::numBands; ++b) setB (b, EQ::pOn, 0.0f);
            // static bell +6 at 1 kHz
            setB (0, EQ::pOn, 1); setB (0, EQ::pType, EQ::bell); setB (0, EQ::pFreq, 1000); setB (0, EQ::pGain, 6); setB (0, EQ::pQ, 1);
            const float staticOut = run (1000, 1000, 0.5f, 0.5f, 0.5).first - Decibels::gainToDecibels (0.5f);
            // dynamic: cut by up to 12 dB above -40 dB: a loud tone comes out much quieter, a quiet one stays near +6
            setB (0, EQ::pGain, 0); setB (0, EQ::pDyn, 1); setB (0, EQ::pRange, -12); setB (0, EQ::pThresh, -40); setB (0, EQ::pAttack, 1); setB (0, EQ::pRelease, 50);
            const float loud = run (1000, 1000, 0.5f, 0.5f, 0.6).first - Decibels::gainToDecibels (0.5f);
            const float dynNow = eq->dynGain[0].load();   // after the loud tone
            const float quiet = run (1000, 1000, 0.001f, 0.001f, 0.6).first - Decibels::gainToDecibels (0.001f);
            setB (0, EQ::pDyn, 0);
            // mid / side: a side-only boost leaves a mono signal alone, and boosts an out-of-phase one
            setB (0, EQ::pGain, 9); setB (0, EQ::pPlace, EQ::side);
            const float monoSide = run (1000, 1000, 0.5f, 0.5f, 0.3).first - Decibels::gainToDecibels (0.5f);
            setB (0, EQ::pPlace, EQ::left);
            const auto lr = run (1000, 1000, 0.5f, 0.5f, 0.3);
            setB (0, EQ::pPlace, EQ::stereo);
            // movement: an LFO sweeps the band's frequency
            setB (0, EQ::pModSrc, 1); setB (0, EQ::pModFreq, 2); setB (0, EQ::pModRate, 4); setB (0, EQ::pModSync, 0);
            float fMin = 1e9f, fMax = 0;
            run (1000, 1000, 0.5f, 0.5f, 0.6, [&] (int) { fMin = jmin (fMin, eq->effFreq[0].load()); fMax = jmax (fMax, eq->effFreq[0].load()); });
            setB (0, EQ::pModSrc, 0);
            // solo: only the band's region is heard (a 100 Hz tone vanishes when band 1 at 5 kHz is soloed)
            setB (0, EQ::pFreq, 5000); setB (0, EQ::pGain, 0); setB (0, EQ::pSolo, 1);
            const float soloed = run (100, 100, 0.5f, 0.5f, 0.3).first - Decibels::gainToDecibels (0.5f);
            setB (0, EQ::pSolo, 0);
            // auto gain: a big high shelf boost is compensated
            setB (0, EQ::pType, EQ::highShelf); setB (0, EQ::pFreq, 2000); setB (0, EQ::pGain, 12); eq->set (eq->g (EQ::gAutoGain), 1.0f);
            run (1000, 1000, 0.5f, 0.5f, 0.1);
            const float autoG = eq->autoGainDb.load();
            const bool audioOk = near (staticOut, 6.0, 0.3) && loud < -6.0f && quiet > -1.0f && dynNow < -6.0f && near (monoSide, 0.0, 0.2)
                                 && lr.first > 8.0f - 6.03f && near (lr.second, -6.02, 0.2) && fMax > 2500.0f && fMin < 400.0f && soloed < -30.0f && autoG < -2.0f;
            rep.check (audioOk, "Prism EQ audio (static, dynamic, mid/side, left, LFO movement, solo, auto gain)",
                       "bell " + String (staticOut, 1) + ", dyn loud " + String (loud, 1) + " / quiet " + String (quiet, 1) + ", side-only on mono " + String (monoSide, 2)
                         + ", left " + String (lr.first, 1) + "/" + String (lr.second, 1) + ", LFO " + String (roundToInt (fMin)) + "-" + String (roundToInt (fMax))
                         + " Hz, solo " + String (soloed, 1) + ", auto gain " + String (autoG, 1));

            // the editor and its display build (snapshot: prism_eq.png)
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                eq->set (eq->g (EQ::gAutoGain), 0.0f);
                setB (0, EQ::pType, EQ::lowCut); setB (0, EQ::pFreq, 40); setB (0, EQ::pSlope, 6);
                setB (1, EQ::pOn, 1); setB (1, EQ::pType, EQ::lowShelf); setB (1, EQ::pFreq, 120); setB (1, EQ::pGain, 3);
                setB (2, EQ::pOn, 1); setB (2, EQ::pType, EQ::bell); setB (2, EQ::pFreq, 350); setB (2, EQ::pGain, -4); setB (2, EQ::pQ, 1.5f);
                setB (3, EQ::pOn, 1); setB (3, EQ::pType, EQ::bell); setB (3, EQ::pFreq, 3000); setB (3, EQ::pGain, 4); setB (3, EQ::pDyn, 1); setB (3, EQ::pRange, -6);
                setB (4, EQ::pOn, 1); setB (4, EQ::pType, EQ::notch); setB (4, EQ::pFreq, 7000); setB (4, EQ::pQ, 6);
                setB (5, EQ::pOn, 1); setB (5, EQ::pType, EQ::highShelf); setB (5, EQ::pFreq, 12000); setB (5, EQ::pGain, 2.5f); setB (5, EQ::pPlace, EQ::side);
                PrismUI::View view (evs, *eq, true);
                view.setSize (1100, 760);
                view.showBand (3);
                auto img = view.createComponentSnapshot (view.getLocalBounds());
                auto f = dir.getChildFile ("prism_eq.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                PrismUI::View card (evs, *eq, false);
                card.setSize (560, 300);
                auto img2 = card.createComponentSnapshot (card.getLocalBounds());
                auto f2 = dir.getChildFile ("prism_eq_card.png");
                f2.deleteFile();
                if (auto os = f2.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img2, *os); }
                // double-click on an empty spot adds a band there (all six are on: none free)
                setB (5, EQ::pOn, 0);
                auto& gr = view.getGraph();
                const int added = gr.addBandAt (2000.0, 5.0);
                const bool addOk = added == 5 && std::abs (eq->get (EQ::idx (5, EQ::pFreq)) - 2000.0f) < 1.0f && eq->bandType (5) == EQ::bell
                                   && std::abs (eq->get (EQ::idx (5, EQ::pGain)) - 5.0f) < 0.01f;
                rep.check (img.isValid() && img2.isValid() && addOk, "Prism EQ editor (display, band panel, add band by double-click)");
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            pl = nullptr;
            edit->deleteTrack (t);
        }

        // --- v0.43: built-in sounds, return / master automation --------------------------------------------
        {
            rep.step ("factory sounds");
            const auto t0 = Time::getMillisecondCounterHiRes();
            const int written = FactorySounds::renderAll (engine);
            const double secs = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            auto all = FactorySounds::folder (engine).findChildFiles (File::findFiles, true, "*.wav");
            all.removeIf ([] (const File& f) { return f.getParentDirectory().getFileName() == "Clicks"; });   // metronome sounds live here too
            int silent = 0;
            AudioFormatManager afm;
            afm.registerBasicFormats();
            double houseLen = 0;
            for (auto& f : all)
                if (std::unique_ptr<AudioFormatReader> r (afm.createReaderFor (f)); r != nullptr)
                {
                    float lo[2] {}, hi[2] {};
                    r->readMaxLevels (0, r->lengthInSamples, lo[0], hi[0], lo[1], hi[1]);
                    if (jmax (std::abs (lo[0]), hi[0]) < 0.05f) ++silent;
                    if (f.getFileName().startsWith ("House")) houseLen = r->lengthInSamples / r->sampleRate;
                }
                else ++silent;
            const double expectHouse = 60.0 / 124.0 * 8.0;   // two bars
            rep.check (written >= 100 && (int) all.size() == written && silent == 0 && std::abs (houseLen - expectHouse) < 0.01 && FactorySounds::isReady (engine),
                       "Built-in sounds rendered (kits, loops, multisampled instruments)",
                       String (written) + " files in " + String (secs, 1) + " s, " + String (silent) + " silent, House loop " + String (houseLen, 3) + " s");

            // an empty Simpler gets a factory instrument and plays it; a sampled kit loads and plays
            auto st = ClipImport::createContentTrack (*edit, "FactorySimpler", false);
            auto sp = ClipImport::createPluginFromDescription (*edit, FactoryPresets::description (pulse::Simpler::xmlTypeName, "Mono Bass"));
            st->pluginList.insertPlugin (sp, 0, nullptr);
            auto* simpler = dynamic_cast<pulse::Simpler*> (sp.get());
            const int zones = simpler != nullptr ? simpler->numZones() : 0;
            const bool isBass = simpler != nullptr && zones > 0 && simpler->getZone (0).file.getParentDirectory().getFileName() == "Bass";
            auto mcS = dynamic_cast<te::MidiClip*> (st->insertNewClip (te::TrackItem::Type::midi, "n", { te::TimePosition(), te::TimePosition::fromSeconds (1.0) }, nullptr));
            mcS->getSequence().addNote (40, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            const te::TimeRange one { te::TimePosition(), te::TimePosition::fromSeconds (1.0) };
            const float simplerPeak = peakDb (engine, EditingOps::renderTrack (*st, one, "factory-simpler"));

            auto dt = ClipImport::createContentTrack (*edit, "FactoryKit", false);
            auto dk = ClipImport::createPluginFromDescription (*edit, "plugin:builtin:drumracksamples");
            dt->pluginList.insertPlugin (dk, 0, nullptr);
            int pads = 0;
            if (auto ri = dynamic_cast<te::RackInstance*> (dk.get()); ri != nullptr && ri->type != nullptr)
                for (auto p : ri->type->getPlugins()) if (dynamic_cast<pulse::DrumPad*> (p) != nullptr) ++pads;
            auto mcD = dynamic_cast<te::MidiClip*> (dt->insertNewClip (te::TrackItem::Type::midi, "d", { te::TimePosition(), te::TimePosition::fromSeconds (1.0) }, nullptr));
            mcD->getSequence().addNote (36, te::BeatPosition(), te::BeatDuration::fromBeats (0.5), 110, 0, nullptr);
            mcD->getSequence().addNote (38, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (0.5), 110, 0, nullptr);
            const float kitPeak = peakDb (engine, EditingOps::renderTrack (*dt, one, "factory-kit"));
            rep.check (zones >= 5 && isBass && simplerPeak > -30.0f && pads >= 11 && kitPeak > -30.0f, "Simpler presets and sampled kits use the built-in sounds",
                       String (zones) + " zones (" + (isBass ? "bass" : "other") + "), " + String (simplerPeak, 1) + " dBFS; kit " + String (pads) + " pads, " + String (kitPeak, 1) + " dBFS");
            edit->deleteTrack (st);
            edit->deleteTrack (dt);

            // return / master automation lanes
            {
                EditViewState evs (*edit, sm);
                BusLanes::Area area (evs, 230);
                const int h0 = area.idealHeight();
                auto* master = edit->getMasterTrack();
                bool ok = master != nullptr;
                if (ok)
                {
                    evs.setAutomationVisible (*master, true);
                    if (auto r = Routing::getReturnTracks (*edit).getFirst()) evs.setAutomationVisible (*r, true);
                    area.rebuild();
                    auto* vol = evs.getAutomationParam (*master);
                    ok = area.idealHeight() >= h0 + 2 * EditViewState::automationHeight && vol == edit->getMasterVolumePlugin()->volParam.get();
                    // a master device's parameter can be chosen and stays chosen
                    auto eq = edit->getPluginCache().createNewPlugin (pulse::PulseEQ::xmlTypeName, {});
                    edit->getMasterPluginList().insertPlugin (eq, 0, nullptr);
                    evs.setAutomationParam (*master, eq->getAutomatableParameters()[2]);
                    ok = ok && evs.getAutomationParam (*master) == eq->getAutomatableParameters()[2]
                            && EditViewState::pluginsOf (*master).contains (eq.get());
                    evs.setAutomationVisible (*master, false);
                    eq->deleteFromParent();
                }
                rep.check (ok, "Automation lanes for returns and the master");
            }
        }

        // --- v0.44: mixer EQ glance, send grid, narrow strips, folded groups, undo history ------------------
        {
            rep.step ("mixer / undo history");
            PulseLookAndFeel laf;
            LookAndFeel::setDefaultLookAndFeel (&laf);
            {
                EditViewState evs (*edit, sm);
                auto t = ClipImport::createContentTrack (*edit, "MixerTest", false);
                // EQ at a glance: click adds a Prism EQ, which the strip then shows
                MiniEQ mini (evs, t);
                mini.setSize (80, 26);
                const bool emptyFirst = mini.findEQ() == nullptr;
                auto src = Desktop::getInstance().getMainMouseSource();
                const auto now = Time::getCurrentTime();
                MouseEvent up (src, { 20.0f, 10.0f }, ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &mini, &mini, now, { 20.0f, 10.0f }, now, 1, false);
                mini.mouseUp (up);
                auto* eqp = dynamic_cast<pulse::PrismEQ*> (mini.findEQ());
                const bool added = eqp != nullptr && t->pluginList.indexOf (eqp) < t->pluginList.indexOf (t->getVolumePlugin());

                // send grid: tracks x returns, and moving a cell sets the send
                auto ret = Routing::createReturnTrack (*edit, "GridReturn");
                SendMatrix grid (*edit);
                int cellIndex = -1, i = 0;
                for (auto at : te::getAudioTracks (*edit))
                    if (! Routing::isReturnTrack (*at))
                        for (auto r : Routing::getReturnTracks (*edit))
                        {
                            if (at == t && r == ret) cellIndex = i;
                            ++i;
                        }
                bool sendOk = false;
                if (cellIndex >= 0 && cellIndex < grid.numCells())
                {
                    grid.cell (cellIndex)->setValue (-9.0, sendNotificationSync);
                    sendOk = std::abs (Routing::getSendDb (*t, Routing::getReturnBus (*ret)) + 9.0f) < 0.2f;
                }

                // narrow strips and folded groups
                Mixer mixer (evs);
                mixer.setSize (1400, 600);
                MessageManager::getInstance()->runDispatchLoopUntil (50);
                const int wide = mixer.stripWidth (0);
                mixer.setNarrow (true);
                const int slim = mixer.stripWidth (0);
                mixer.setNarrow (false);
                const int before = mixer.numStrips();
                auto c1 = ClipImport::createContentTrack (*edit, "Child1", false);
                auto c2 = ClipImport::createContentTrack (*edit, "Child2", false);
                te::FolderTrack::Ptr folder = EditingOps::groupTracks (*edit, { c1, c2 }, "Group");
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                const int open = mixer.numStrips();
                if (folder != nullptr) { evs.collapsed.insert (folder->itemID); evs.layoutChanged.sendChangeMessage(); }
                MessageManager::getInstance()->runDispatchLoopUntil (100);
                const int folded = mixer.numStrips();
                rep.check (emptyFirst && added && sendOk && wide == 92 && slim == 58 && open >= before + 2 && folded == open - 2,
                           "Mixer: EQ glance adds Prism EQ, send grid, narrow strips, folded groups",
                           "strip " + String (wide) + " -> " + String (slim) + " px, strips " + String (before) + " / " + String (open) + " / folded " + String (folded)
                             + ", send " + (sendOk ? "ok" : "no"));
                if (folder != nullptr) { evs.collapsed.erase (folder->itemID); edit->deleteTrack (c1); edit->deleteTrack (c2); edit->deleteTrack (folder.get()); }
                edit->deleteTrack (ret);
                edit->deleteTrack (t);
            }
            LookAndFeel::setDefaultLookAndFeel (nullptr);

            // undo history: three renames, jump back to the first, then forward to the last
            {
                auto t = ClipImport::createContentTrack (*edit, "Undo0", false);
                auto& um = edit->getUndoManager();
                um.clearUndoHistory();
                for (auto n : { "Undo1", "Undo2", "Undo3" }) { um.beginNewTransaction (String ("Rename to ") + n); t->setName (n); }
                um.beginNewTransaction();
                UndoHistory::View view (*edit);
                const int rows = view.getNumRows();
                view.goTo (1);
                const auto atFirst = t->getName();
                view.goTo (view.getNumRows() - 1);
                const auto atLast = t->getName();
                view.goTo (0);
                const auto atStart = t->getName();
                rep.check (rows == 4 && atFirst == "Undo1" && atLast == "Undo3" && atStart == "Undo0", "Undo history (jump back and forward)",
                           String (rows) + " rows: " + atFirst + " / " + atLast + " / " + atStart);
                edit->deleteTrack (t);
            }
        }

        // --- v0.45: A/B in every device editor ----------------------------------------------------------------
        {
            rep.step ("device editor A/B");
            auto t = ClipImport::createContentTrack (*edit, "ABTest", false);
            auto pl = edit->getPluginCache().createNewPlugin (pulse::Reverb::xmlTypeName, {});
            t->pluginList.insertPlugin (pl, 0, nullptr);
            auto* rv = dynamic_cast<pulse::Reverb*> (pl.get());
            bool ok = false;
            {
                EditViewState evs (*edit, sm);
                DeviceEditors::Editor ed (evs, *rv);
                rv->set (pulse::Reverb::pDecay, 2.0f);
                ed.clickAB();                                   // A stored, now on B (still 2.0)
                rv->set (pulse::Reverb::pDecay, 9.0f);
                ed.clickAB();                                   // back to A
                const float a = rv->get (pulse::Reverb::pDecay);
                ed.clickAB();                                   // B again
                const float b = rv->get (pulse::Reverb::pDecay);
                ok = std::abs (a - 2.0f) < 0.01f && std::abs (b - 9.0f) < 0.01f;
            }
            rep.check (ok, "A/B compare in the device editor");
            edit->deleteTrack (t);
        }

        // --- v0.46: pro dynamics ----------------------------------------------------------------------------
        {
            rep.step ("dynamics");
            const double sr = 48000.0;
            // runs a stereo signal through an effect; returns the output peak (dB) over the last 0.1 s
            auto runFx = [&] (te::Plugin& fx, std::function<float (int, double)> sig, double seconds, AudioBuffer<float>* keep = nullptr) -> float
            {
                const int block = 480;
                fx.initialise ({ te::TimePosition(), sr, block });
                AudioBuffer<float> audio (2, block);
                float peak = 0;
                const int blocks = (int) (seconds * sr / block);
                if (keep != nullptr) keep->setSize (2, blocks * block);
                for (int k = 0; k < blocks; ++k)
                {
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < block; ++i) audio.setSample (c, i, sig (c, (k * block + i) / sr));
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                                true, false, true, false);
                    fx.applyToBuffer (fc);
                    if (keep != nullptr) for (int c = 0; c < 2; ++c) keep->copyFrom (c, k * block, audio, c, 0, block);
                    if (k >= blocks - 10) peak = jmax (peak, audio.getMagnitude (0, block));
                }
                fx.deinitialise();
                return Decibels::gainToDecibels (peak, -200.0f);
            };
            auto sine = [] (double hz, float amp) { return [hz, amp] (int, double t) { return amp * (float) std::sin (MathConstants<double>::twoPi * hz * t); }; };
            auto t = ClipImport::createContentTrack (*edit, "Dyn", false);

            // compressor: -10 dBFS sine, threshold -20, 4:1, hard knee -> about -17.5 dBFS
            auto cp = edit->getPluginCache().createNewPlugin (pulse::ProCompressor::xmlTypeName, {});
            t->pluginList.insertPlugin (cp, 0, nullptr);
            auto* comp = dynamic_cast<pulse::ProCompressor*> (cp.get());
            using C = pulse::ProCompressor;
            comp->set (C::pThreshold, -20); comp->set (C::pRatio, 4); comp->set (C::pKnee, 0); comp->set (C::pAttack, 1); comp->set (C::pRelease, 50);
            const float a10 = Decibels::decibelsToGain (-10.0f);
            const float out4 = runFx (*comp, sine (1000, a10), 0.6);
            comp->set (C::pRatio, 100);
            const float outInf = runFx (*comp, sine (1000, a10), 0.6);
            comp->set (C::pRatio, 4); comp->set (C::pRange, 3);
            const float outRange = runFx (*comp, sine (1000, a10), 0.6);
            comp->set (C::pRange, 60);
            // sidechain low cut: a 50 Hz tone barely triggers it when the SC cut is at 500 Hz
            comp->set (C::pScHpf, 500);
            const float outHpf = runFx (*comp, sine (50, a10), 0.8);
            comp->set (C::pScHpf, 10);
            // lookahead reports latency
            comp->set (C::pLookahead, 5);
            const double lat = comp->getLatencySeconds();
            comp->set (C::pLookahead, 0);
            const bool compOk = std::abs (out4 + 17.5f) < 0.8f && std::abs (outInf + 20.0f) < 0.8f && std::abs (outRange + 13.0f) < 0.6f
                                && outHpf > -11.5f && std::abs (lat - 0.005) < 1e-6 && std::abs (comp->transfer (-10.0f) + 17.5f) < 0.01f;
            rep.check (compOk, "Pulse Compressor (ratio, infinity, range, sidechain filter, lookahead latency)",
                       "4:1 " + String (out4, 1) + ", inf " + String (outInf, 1) + ", range 3 dB " + String (outRange, 1) + ", 50 Hz with SC cut " + String (outHpf, 1));

            // multiband: with nothing compressing, the bands sum back to flat
            auto mp = edit->getPluginCache().createNewPlugin (pulse::MultibandDynamics::xmlTypeName, {});
            t->pluginList.insertPlugin (mp, 0, nullptr);
            auto* mb = dynamic_cast<pulse::MultibandDynamics*> (mp.get());
            using M = pulse::MultibandDynamics;
            mb->set (mb->g (M::gBands), 2);   // 4 bands
            for (int b = 0; b < 4; ++b) mb->set (M::idx (b, M::bThreshold), 0.0f);
            float worst = 0;
            String perHz;
            for (double hz : { 40.0, 120.0, 500.0, 1000.0, 3000.0, 6000.0, 12000.0 })
            {
                AudioBuffer<float> kept;
                runFx (*mb, sine (hz, 0.25f), 0.4, &kept);   // RMS of the last 0.1 s (a sampled peak moves with phase at high frequencies)
                const int last = (int) (0.1 * sr);
                const float e = Decibels::gainToDecibels (kept.getRMSLevel (0, kept.getNumSamples() - last, last) * 1.41421f) - Decibels::gainToDecibels (0.25f);
                perHz << String (roundToInt (hz)) << ":" << String (e, 2) << " ";
                worst = jmax (worst, std::abs (e));
            }
            // band 1 compresses hard: a loud 60 Hz tone drops, a loud 5 kHz tone does not
            mb->set (M::idx (0, M::bThreshold), -40.0f); mb->set (M::idx (0, M::bRatio), 10.0f);
            const float low = runFx (*mb, sine (60, 0.5f), 0.6) - Decibels::gainToDecibels (0.5f);
            const float high = runFx (*mb, sine (5000, 0.5f), 0.6) - Decibels::gainToDecibels (0.5f);
            rep.check (worst < 0.3f && low < -15.0f && std::abs (high) < 0.5f, "Multiband Dynamics (flat sum, band-only compression)",
                       "sum error " + String (worst, 2) + " dB (" + perHz.trim() + "), low band " + String (low, 1) + " dB, high " + String (high, 2) + " dB");

            // limiter: a sine with peaks between the samples stays under the ceiling with true peak on
            auto lp = edit->getPluginCache().createNewPlugin (pulse::Limiter::xmlTypeName, {});
            t->pluginList.insertPlugin (lp, 0, nullptr);
            auto* lim = dynamic_cast<pulse::Limiter*> (lp.get());
            using L = pulse::Limiter;
            lim->set (L::pGain, 12); lim->set (L::pCeiling, -1); lim->set (L::pTruePeak, 1);
            AudioBuffer<float> outBuf;
            auto isp = [] (int, double t) { return 0.5f * (float) std::sin (MathConstants<double>::twoPi * 12000.0 * t + 0.785); };   // fs/4: worst case
            runFx (*lim, isp, 3.5, &outBuf);
            pulse::LoudnessAnalyser la;
            la.prepare (sr);
            la.process (outBuf.getReadPointer (0, (int) sr), outBuf.getReadPointer (1, (int) sr), outBuf.getNumSamples() - (int) sr);
            const float tp = la.truePeakDb();
            const float measured = lim->truePeakDb.load(), st = lim->shortTermLufs.load();
            rep.check (tp < -0.9f && measured < -0.5f && st > -30.0f && lim->history.write.load() > 0, "Limiter: true-peak ceiling, loudness readouts, history",
                       "true peak " + String (tp, 2) + " dBTP (limiter reads " + String (measured, 2) + "), short-term " + String (st, 1) + " LUFS");

            // displays build
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                int built = 0;
                for (auto* p : std::initializer_list<te::Plugin*> { comp, mb, lim })
                {
                    DeviceEditors::Editor ed (evs, *dynamic_cast<pulse::PulsePlugin*> (p));
                    ed.setSize (1100, 760);
                    auto img = ed.createComponentSnapshot (ed.getLocalBounds());
                    auto f = dir.getChildFile ("dyn_" + File::createLegalFileName (p->getName()) + ".png");
                    f.deleteFile();
                    if (auto os = f.createOutputStream()) { PNGImageFormat png; if (png.writeImageToStream (img, *os)) ++built; }
                }
                rep.check (built == 3, "Dynamics editors build");
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            cp = nullptr; mp = nullptr; lp = nullptr;
            edit->deleteTrack (t);
        }

        // --- v0.47: flagship Wavetable ---------------------------------------------------------------------
        {
            rep.step ("wavetable");
            using W = pulse::WavetableSynth;
            auto wp = edit->getPluginCache().createNewPlugin (W::xmlTypeName, {});
            auto* w = dynamic_cast<W*> (wp.get());
            auto note = [&] (int n, double hold) { return FactorySounds::renderNote (*w, n, hold); };
            auto reset = [&] { for (auto* prm : w->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync);
                               FactoryPresets::clearRoutes (*w); };
            auto windowSpread = [] (const AudioBuffer<float>& b, double from, double to)
            {
                // spread (max / min) of the RMS in 20 ms windows: how much the level moves
                float lo = 1e9f, hi = 0;
                for (int s0 = (int) (from * 48000); s0 + 960 < (int) (to * 48000) && s0 + 960 < b.getNumSamples(); s0 += 960)
                {
                    const float r = b.getRMSLevel (0, s0, 960);
                    lo = jmin (lo, r); hi = jmax (hi, r);
                }
                return Decibels::gainToDecibels (hi) - Decibels::gainToDecibels (jmax (1e-6f, lo));
            };
            // ids of older versions keep their places
            const char* oldIds[] = { "set1", "pos1", "set2", "pos2", "semi2", "fine2", "mix", "voices", "detune", "sub", "ftype", "cutoff", "reso", "fenv",
                                     "fa", "fd", "fs", "fr", "a", "d", "s", "r", "lforate", "lfopos", "lfocut", "glide", "level", "bendrange", "pressure", "slide" };
            bool compat = true;
            for (int i = 0; i < 30; ++i) compat = compat && w->param (i)->paramID == oldIds[i];

            reset();
            w->set (W::pLevel, -6);
            const auto plain = note (48, 1.0);
            const float plainSpread = windowSpread (plain, 0.3, 0.95);
            // LFO 2 (4 Hz) on the level: the level wobbles
            w->set (W::pL2Rate, 4.0f);
            w->setRoute (2, "level", 1.0f);
            const auto wob = note (48, 1.0);
            const float wobSpread = windowSpread (wob, 0.3, 0.95);
            const float depth = w->modulationDepth (W::pLevel);
            reset();
            // warp: sync changes the sound
            const auto base = note (48, 0.4);
            w->set (W::pWarp1, 3); w->set (W::pWarpAmt1, 0.8f);
            const auto warped = note (48, 0.4);
            float diff = 0;
            for (int i = 0; i < jmin (base.getNumSamples(), warped.getNumSamples()); ++i) diff += std::abs (base.getSample (0, i) - warped.getSample (0, i));
            reset();
            // filter 2: a high pass at 8 kHz on a low note takes most of it away
            const float before = note (36, 0.4).getRMSLevel (0, 0, 9600);
            w->set (W::pF2Type, 4); w->set (W::pF2Cutoff, 8000);
            const float after = note (36, 0.4).getRMSLevel (0, 0, 9600);
            reset();
            // reverb: more tail after the release
            w->set (W::pR, 50);
            const auto dry = note (60, 0.3);
            w->set (W::pReverbMix, 0.8f);
            const auto wet = note (60, 0.3);
            const float dryTail = dry.getNumSamples() > 30000 ? dry.getRMSLevel (0, 24000, jmin (12000, dry.getNumSamples() - 24000)) : 0.0f;
            const float wetTail = wet.getNumSamples() > 30000 ? wet.getRMSLevel (0, 24000, jmin (12000, wet.getNumSamples() - 24000)) : 0.0f;
            reset();
            // drawn LFO
            std::vector<float> pts (W::lfoPoints, 0.0f); pts[4] = 1.0f;
            w->setLfoPoints (1, pts);
            w->set (W::pL2Shape, 6);
            const float drawnAt = w->lfoShape (1, 4.0 / W::lfoPoints);
            // presets with routes
            FactoryPresets::apply (*w, "Neuro Reese");
            const int routes = (int) w->getRoutes().size();
            const int wtPresets = (int) FactoryPresets::forType (W::xmlTypeName).size();
            rep.check (compat && wobSpread > plainSpread + 6.0f && depth > 0.99f && diff > 50.0f && after < before * 0.3f && wetTail > dryTail * 3.0f
                         && std::abs (drawnAt - 1.0f) < 0.01f && routes == 2 && wtPresets >= 50,
                       "Wavetable: modulation matrix, warp, filter 2, effects, drawn LFO, 50+ presets",
                       "LFO on level " + String (plainSpread, 1) + " -> " + String (wobSpread, 1) + " dB, warp diff " + String (diff, 0) + ", F2 HP "
                         + String (Decibels::gainToDecibels (after / jmax (1e-6f, before)), 1) + " dB, reverb tail x" + String (wetTail / jmax (1e-6f, dryTail), 1)
                         + ", routes " + String (routes) + ", presets " + String (wtPresets));

            // dropping a source on a knob adds a route
            {
                EditViewState evs (*edit, sm);
                reset();
                ParamKnob k (evs, *w->param (W::pCutoff));
                const bool accepts = ModDrop::accepts && ModDrop::accepts (*w->param (W::pCutoff)) && ! ModDrop::accepts (*w->param (W::pSet1));
                if (ModDrop::drop) ModDrop::drop (*w->param (W::pCutoff), 3);
                const auto rs = w->getRoutes();
                rep.check (accepts && rs.size() == 1 && rs[0].source == 3 && rs[0].dest == W::pCutoff, "Drag a modulation source onto a knob");
            }
            wp = nullptr;
        }

        // --- v0.48: recording workflow --------------------------------------------------------------------
        {
            rep.step ("recording workflow");
            // metronome sounds: each style renders, is stored, and the engine is pointed at it
            int clicksOk = 0;
            for (int st = 1; st < FactorySounds::clickStyles().size(); ++st)
            {
                FactorySounds::setClickStyle (engine, st);
                const auto bar = FactorySounds::clickFile (engine, st, true), beat = FactorySounds::clickFile (engine, st, false);
                const bool set = engine.getPropertyStorage().getProperty (te::SettingID::clickTrackSampleBig).toString() == bar.getFullPathName();
                if (bar.existsAsFile() && beat.existsAsFile() && set && FactorySounds::renderClick (st, true).getMagnitude (0, 0, 2400) > 0.1f) ++clicksOk;
            }
            FactorySounds::setClickStyle (engine, 0);
            const bool restored = engine.getPropertyStorage().getProperty (te::SettingID::clickTrackSampleBig).toString().isEmpty();
            // punch in / out turns looping off; count-in modes
            edit->getTransport().looping = true;
            edit->recordingPunchInOut = false;
            {
                TransportBar tb (engine, {});
                tb.setEdit (edit.get());
                tb.clickPunch();
                const bool punchOn = edit->recordingPunchInOut.get() && ! edit->getTransport().looping.get();
                tb.clickPunch();
                const bool punchOff = ! edit->recordingPunchInOut.get();
                edit->setCountInMode (te::Edit::CountIn::oneBar);
                const int beats = edit->getNumCountInBeats();
                edit->setCountInMode (te::Edit::CountIn::none);
                tb.setEdit (nullptr);
                // an input meter whose device went away is dropped without touching it
                bool safe = true;
                {
                    auto measurer = std::make_unique<te::LevelMeasurer>();
                    auto m = std::make_unique<LevelMeter> (*measurer, true);
                    m->stillValid = [] { return false; };
                    measurer.reset();   // the "device" is gone first
                    m.reset();          // must not touch it
                }
                rep.check (clicksOk == FactorySounds::clickStyles().size() - 1 && restored && punchOn && punchOff && beats == EngineHelpers::beatsPerBar (*edit) && safe,
                           "Recording: metronome sounds, punch in / out, count-in, input meter safety",
                           String (clicksOk) + " click styles, punch " + (punchOn && punchOff ? "ok" : "no") + ", count-in " + String (beats) + " beats");
            }
        }

        // --- v0.49: song sections ------------------------------------------------------------------------
        {
            rep.step ("sections");
            auto ed = te::createEmptyEdit (engine, dir.getChildFile ("sections.tracktionedit"));
            ed->ensureNumberOfAudioTracks (1);
            auto* t = te::getAudioTracks (*ed).getFirst();
            auto b2t = [&] (double b) { return EngineHelpers::beatToTime (*ed, b); };
            auto clipAt = [&] (double beat) -> String
            {
                for (auto c : t->getClips()) if (std::abs (EngineHelpers::timeToBeat (*ed, c->getPosition().getStart()) - beat) < 0.01) return c->getName();
                return "-";
            };
            auto addClip = [&] (const String& name, double b0, double b1)
            {
                if (auto c = t->insertNewClip (te::TrackItem::Type::midi, name, { b2t (b0), b2t (b1) }, nullptr)) c->setName (name);
            };
            addClip ("A", 0, 8); addClip ("B", 8, 16); addClip ("C", 16, 24);
            const int a = Sections::add (*ed, "Intro", 0, 8), b = Sections::add (*ed, "Verse", 8, 8), c = Sections::add (*ed, "Chorus", 16, 8);
            // move the chorus to the start: C A B
            Sections::move (*ed, c, 0.0);
            const String afterMove = clipAt (0) + clipAt (8) + clipAt (16);
            auto order = [&]
            {
                String o;
                for (auto& sec : Sections::all (*ed)) o << sec.name.substring (0, 1) << (int) sec.start << " ";
                return o.trim();
            };
            const String secAfterMove = order();
            // move the intro (now at 8) to the end: C B A
            Sections::move (*ed, a, 24.0);
            const String afterMove2 = clipAt (0) + clipAt (8) + clipAt (16);
            // duplicate the verse (at 8): C B B A
            Sections::duplicate (*ed, b);
            const String afterDup = clipAt (0) + clipAt (8) + clipAt (16) + clipAt (24);
            // delete the chorus (at 0): B B A
            Sections::remove (*ed, c);
            const String afterDel = clipAt (0) + clipAt (8) + clipAt (16);
            // a section that starts inside another is placed after it; one that would overlap the next is cut short
            const int d = Sections::add (*ed, "Tag", 4, 100);
            double dStart = -1, dLen = -1;
            for (auto& sec : Sections::all (*ed)) if (sec.index == d) { dStart = sec.start; dLen = sec.length; }
            rep.check (afterMove == "CAB" && secAfterMove == "C0 I8 V16" && afterMove2 == "CBA" && afterDup == "CBBA" && afterDel == "BBA" && dStart >= 8.0,
                       "Song sections move, duplicate and delete their content",
                       afterMove + " / " + afterMove2 + " / " + afterDup + " / " + afterDel + " | " + secAfterMove + " | added at " + String (dStart) + " len " + String (dLen));
            ed = nullptr;
        }

        // --- v0.50: MP3 export, streaming-ready true peak ---------------------------------------------------
        {
            rep.step ("export formats");
            // a sine with peaks between the samples (fs/4 at 45 degrees): its true peak is 3 dB above the sample peak
            auto wav = dir.getChildFile ("tp.wav");
            {
                AudioBuffer<float> b (2, 48000 * 2);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c) b.setSample (c, i, 0.98f * (float) std::sin (MathConstants<double>::halfPi * i + 0.785));
                wav.deleteFile();
                WavAudioFormat fmt;
                std::unique_ptr<OutputStream> os (wav.createOutputStream());
                std::unique_ptr<AudioFormatWriter> w (fmt.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0));
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            const float beforeTp = pulse::LoudnessAnalyser::measureFile (engine, wav).truePeakDb;
            const float reduced = ExportTools::limitTruePeak (engine, wav, -1.0f);
            const float afterTp = pulse::LoudnessAnalyser::measureFile (engine, wav).truePeakDb;
            // MP3 through Windows' encoder
            auto mp3 = dir.getChildFile ("tp.mp3");
            const auto err = Mp3EncoderWin::encode (wav, mp3, 192);
            const auto size = mp3.getSize();
            rep.check (beforeTp > -0.5f && reduced > 0.5f && afterTp <= -0.9f && err.isEmpty() && size > 20000,
                       "Export: true-peak limit for streaming, MP3 encoding",
                       "true peak " + String (beforeTp, 1) + " -> " + String (afterTp, 2) + " dBTP, MP3 " + (err.isEmpty() ? String (size / 1024) + " KB" : err));
        }

        // --- v0.51: what may be dropped where -------------------------------------------------------------
        {
            rep.step ("drop rules");
            StringArray refused;
            PluginMenu::onRefused = [&refused] (const String& why) { refused.add (why); };
            auto make = [&] (const char* type) { return edit->getPluginCache().createNewPlugin (type, {}); };
            auto ret = Routing::createReturnTrack (*edit, "RuleReturn");
            auto plain = ClipImport::createContentTrack (*edit, "RulePlain", false);
            auto& master = edit->getMasterPluginList();
            const int masterBefore = master.size();
            // instruments and MIDI effects: not on returns or the master
            const bool r1 = ! PluginMenu::insertPlugin (ret, ret->pluginList, make (pulse::WavetableSynth::xmlTypeName), nullptr);
            const bool r2 = ! PluginMenu::insertPlugin (nullptr, master, make (pulse::Arpeggiator::xmlTypeName), nullptr);
            const bool r3 = ! PluginMenu::insertPlugin (ret, ret->pluginList, DrumRack::create (*edit), nullptr);
            const bool okFx = PluginMenu::insertPlugin (ret, ret->pluginList, make (pulse::Reverb::xmlTypeName), nullptr);
            // MIDI effects need an instrument
            const bool r4 = ! PluginMenu::insertPlugin (plain, plain->pluginList, make (pulse::ChordFx::xmlTypeName), nullptr);
            PluginMenu::insertPlugin (plain, plain->pluginList, make (pulse::WavetableSynth::xmlTypeName), nullptr);
            const bool okMidi = PluginMenu::insertPlugin (plain, plain->pluginList, make (pulse::ChordFx::xmlTypeName), nullptr);
            // a second instrument replaces the first
            PluginMenu::insertPlugin (plain, plain->pluginList, make (pulse::FMSynth::xmlTypeName), nullptr);
            int synths = 0;
            for (auto p : plain->pluginList) if (p->isSynth()) ++synths;
            const bool replaced = synths == 1 && plain->pluginList.findFirstPluginOfType<pulse::FMSynth>() != nullptr;
            // moving an instrument onto the master is refused
            auto* fm = plain->pluginList.findFirstPluginOfType<pulse::FMSynth>();
            DragDrop::placeDevice (*fm, nullptr, master, nullptr, false);
            const bool stayed = fm->getOwnerTrack() == plain && master.size() == masterBefore;
            // audio files dropped on an instrument track get their own track; MIDI files don't
            const bool ownTrack = ClipImport::needsOwnAudioTrack (*plain, File ("C:/x/loop.wav")) && ! ClipImport::needsOwnAudioTrack (*plain, File ("C:/x/riff.mid"))
                                  && ! ClipImport::needsOwnAudioTrack (*ret, File ("C:/x/loop.wav"));
            // the menu for a return offers no instruments or MIDI effects
            const auto allowRet = PluginMenu::allowedFor (ret->pluginList, ret), allowPlain = PluginMenu::allowedFor (plain->pluginList, plain);
            const bool menus = ! allowRet.instruments && ! allowRet.midiEffects && allowPlain.instruments && allowPlain.midiEffects;
            PluginMenu::onRefused = nullptr;
            rep.check (r1 && r2 && r3 && okFx && r4 && okMidi && replaced && stayed && ownTrack && menus && refused.size() == 5,
                       "Drops make sense (no instruments on returns / master, MIDI effects need an instrument, one instrument per track)",
                       String (refused.size()) + " refused: " + refused[0].upToFirstOccurrenceOf (":", false, false));
            edit->deleteTrack (ret);
            edit->deleteTrack (plain);
        }

        // --- 1.0 RC: every device comes back from a saved project exactly as it was ----------------------------
        {
            rep.step ("device state round trip");
            StringArray bad;
            int devices = 0, values = 0;
            Random rng (20261005);
            for (auto& b : ClipImport::builtIns())
            {
                auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get());
                if (pp == nullptr) continue;
                ++devices;
                for (int i = 0; i < pp->numParams(); ++i)   // every parameter somewhere new
                {
                    auto* prm = pp->param (i);
                    pp->set (i, prm->valueRange.snapToLegalValue (prm->valueRange.convertFrom0to1 (rng.nextFloat())));
                }
                if (auto* w = dynamic_cast<pulse::WavetableSynth*> (pp)) { w->setRoute (3, "cutoff", 0.37f); w->setRoute (15, "pos2", -0.62f); }
                p->flushPluginStateToValueTree();
                // through text, as a project file does it
                auto xml = p->state.createXml();
                auto v = xml != nullptr ? ValueTree::fromXml (xml->toString()) : ValueTree();
                te::EditItemID::remapIDs (v, nullptr, *edit);
                auto q = edit->getPluginCache().createNewPlugin (v);
                auto* qq = dynamic_cast<pulse::PulsePlugin*> (q.get());
                if (qq == nullptr || qq->numParams() != pp->numParams()) { bad.add (String (b.name) + " (not recreated: " + (q == nullptr ? String ("null, tree ") + (v.isValid() ? v.getType().toString() + " type=" + v["type"].toString() : String ("invalid"))
                                                                                                 : q->getName() + " params " + String (qq != nullptr ? qq->numParams() : -1)) + ")"); continue; }
                for (int i = 0; i < pp->numParams(); ++i)
                {
                    ++values;
                    const float a = pp->get (i), c = qq->get (i);
                    const float tol = 1.0e-4f * jmax (1.0f, std::abs (a));
                    if (std::abs (a - c) > tol) { bad.add (String (b.name) + "." + pp->param (i)->paramID + " " + String (a) + " -> " + String (c)); break; }
                }
                if (auto* w = dynamic_cast<pulse::WavetableSynth*> (qq))
                {
                    auto routes = w->getRoutes();
                    if (routes.size() != 2 || routes[0].source != 3 || std::abs (routes[1].amount + 0.62f) > 1.0e-4f) bad.add ("Wavetable routes");
                }
            }
            {
                // a project from an older version with a Saturator whose Type had been changed (it was saved under the device's own "type")
                auto xml = parseXML ("<EDIT><TRACK><PLUGIN type=\"3.0\" id=\"1099\" enabled=\"1\" drive=\"12.0\" bias=\"0.2\"/><PLUGIN type=\"pulseReverb\" id=\"1100\"/></TRACK>"
                                     "<MASTERPLUGINS><PLUGIN type=\"2.0\" id=\"1101\" lfoamount=\"0.5\"/><PLUGIN type=\"volume\" id=\"1102\"/></MASTERPLUGINS></EDIT>");
                const int fixed = xml != nullptr ? Upgrade::repairDeviceTypes (*xml) : -1;
                auto* sat = xml->getChildByName ("TRACK")->getChildElement (0);
                auto* af = xml->getChildByName ("MASTERPLUGINS")->getChildElement (0);
                const bool ok = fixed == 2 && sat->getStringAttribute ("type") == "pulseSaturator" && sat->getDoubleAttribute ("pulse_type") == 3.0
                                && af->getStringAttribute ("type") == "pulseAutoFilter" && xml->getChildByName ("TRACK")->getChildElement (1)->getStringAttribute ("type") == "pulseReverb";
                auto v = ValueTree::fromXml (*sat);
                te::EditItemID::remapIDs (v, nullptr, *edit);
                auto back = edit->getPluginCache().createNewPlugin (v);
                auto* bs = dynamic_cast<pulse::Saturator*> (back.get());
                rep.check (ok && bs != nullptr && bs->getInt (pulse::Saturator::pType) == 3 && std::abs (bs->get (pulse::Saturator::pDrive) - 12.0f) < 0.01f,
                           "Projects from older versions: a Saturator / Auto Filter saved with a changed Type is repaired on opening", String (fixed) + " repaired");
            }
            rep.check (bad.isEmpty() && devices >= 20, "Every built-in device reloads from a saved project with every value intact",
                       bad.isEmpty() ? String (devices) + " devices, " + String (values) + " values" : bad.joinIntoString ("; "));
        }

        // --- 1.0 RC: preset browser in the device windows --------------------------------------------------------
        {
            rep.step ("preset browser");
            auto wp = edit->getPluginCache().createNewPlugin (pulse::WavetableSynth::xmlTypeName, {});
            auto* w = dynamic_cast<pulse::WavetableSynth*> (wp.get());
            const auto names = FactoryPresets::ordered (pulse::WavetableSynth::xmlTypeName);
            const int total = (int) FactoryPresets::forType (pulse::WavetableSynth::xmlTypeName).size();
            StringArray unique (names);
            unique.removeDuplicates (false);
            int more = 0;
            for (auto& n : names) if (FactoryPresets::category (n) == "More") ++more;
            EditViewState pevs (*edit, sm);
            DeviceEditors::Editor ed (pevs, *w);
            ed.stepPreset (1);
            const auto first = ed.presetName();
            ed.stepPreset (1);
            const auto second = ed.presetName();
            ed.stepPreset (-1);
            const bool steps = first == names[0] && second == names[1] && ed.presetName() == first && FactoryPresets::current (*w) == first;
            ed.stepPreset (-1);
            const bool wraps = ed.presetName() == names[names.size() - 1];
            rep.check (names.size() == total && unique.size() == total && more < total / 3 && steps && wraps,
                       "Preset browser: presets grouped by kind, previous / next steps through them, the name is kept with the project",
                       String (total) + " Wavetable presets, " + String (more) + " ungrouped, first \"" + first + "\", ordered " + String (names.size()) + ", unique " + String (unique.size())
                       + (steps ? "" : ", steps wrong: " + second + " vs " + names[1]) + (wraps ? "" : ", wrap wrong: " + ed.presetName() + " vs " + names[names.size() - 1]));
        }

        // --- 1.0 RC: no setting of any device makes it blow up -------------------------------------------------
        {
            rep.step ("device fuzz");
            StringArray bad;
            int runs = 0;
            Random rng (777);
            const double sr = 48000.0;
            const int block = 512;
            for (auto& b : ClipImport::builtIns())
            {
                if (b.midiFx) continue;
                auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get());
                if (pp == nullptr) continue;
                rep.step (String ("  fuzzing ") + b.name);
                bool ok = true;
                String worst;
                for (int round = 0; round < 24 && ok; ++round)
                {
                    // every parameter somewhere random; every third round pushes them all to an end of their range
                    for (int i = 0; i < pp->numParams(); ++i)
                    {
                        auto* prm = pp->param (i);
                        const float n = round % 3 == 2 ? (rng.nextBool() ? 1.0f : 0.0f) : rng.nextFloat();
                        pp->set (i, prm->valueRange.snapToLegalValue (prm->valueRange.convertFrom0to1 (n)));
                    }
                    p->initialise ({ te::TimePosition(), sr, block });
                    AudioBuffer<float> audio (2, block);
                    const auto src = te::createUniqueMPESourceID();
                    int pinned = 0, total = 0;
                    float tailPeak = 0.0f;
                    for (int k = 0; k < 130 && ok; ++k)
                    {
                        const bool quiet = k >= 30;   // then 100 blocks (about a second) of silence
                        // effects get a loud sine sweep with clicks; instruments get a chord
                        for (int i = 0; i < block; ++i)
                        {
                            const double t = (k * block + i) / sr;
                            float v = p->isSynth() || quiet ? 0.0f : 0.9f * (float) std::sin (MathConstants<double>::twoPi * (60.0 + t * 9000.0) * t);
                            if (! p->isSynth() && ! quiet && (k * block + i) % 4800 == 0) v = 1.0f;
                            audio.setSample (0, i, v);
                            audio.setSample (1, i, -v * 0.8f);
                        }
                        te::MidiMessageArray midi;
                        if (p->isSynth() && k == 0) for (int note : { 36, 60, 96 }) midi.addMidiMessage (MidiMessage::noteOn (1, note, (uint8) 127), 0.0, src);
                        if (p->isSynth() && k == 20) for (int note : { 36, 60, 96 }) midi.addMidiMessage (MidiMessage::noteOff (1, note), 0.0, src);
                        te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                                    true, false, true, false);
                        p->applyToBuffer (fc);
                        for (int ch = 0; ch < 2 && ok; ++ch)
                            for (int i = 0; i < block; ++i)
                            {
                                const float v = audio.getSample (ch, i);
                                if (! std::isfinite (v)) { ok = false; worst = "not a number"; break; }
                                ++total;
                                if (std::abs (v) >= 3.99f) ++pinned;
                                if (k >= 125) tailPeak = jmax (tailPeak, std::abs (v));
                            }
                    }
                    p->deinitialise();
                    ++runs;
                    // a second after the input stopped (notes released) it must not still be screaming: that is a runaway
                    // (instruments are left out: a ten-second release at full level is loud for a long time and that is what was asked for)
                    juce::ignoreUnused (pinned);
                    if (ok && ! p->isSynth() && tailPeak >= 3.99f) { ok = false; worst = "runs away (still at the clamp a second after the input stopped)"; }
                    if (! ok)
                    {
                        StringArray settings;
                        for (int i = 0; i < jmin (pp->numParams(), 60); ++i) settings.add (pp->param (i)->paramID + "=" + String (pp->get (i), 3));
                        rep.step ("  !! " + String (b.name) + " " + worst + " with " + settings.joinIntoString (" "));
                    }
                }
                if (! ok) bad.add (String (b.name) + ": " + worst);
            }
            rep.check (bad.isEmpty(), "No setting makes a device blow up (random and extreme settings on every instrument and effect)",
                       bad.isEmpty() ? String (runs) + " runs" : bad.joinIntoString ("; "));
        }

        // --- 1.0 RC: undo of a knob move ------------------------------------------------------------------------
        {
            rep.step ("knob undo");
            auto t = ClipImport::createContentTrack (*edit, "KnobUndo", false);
            auto p = edit->getPluginCache().createNewPlugin (pulse::Reverb::xmlTypeName, {});
            t->pluginList.insertPlugin (p, 0, nullptr);
            auto* rv = dynamic_cast<pulse::Reverb*> (p.get());
            auto settle = [] { MessageManager::getInstance()->runDispatchLoopUntil (150); };
            auto& um = edit->getUndoManager();
            settle();
            um.clearUndoHistory();
            const float was = rv->get (pulse::Reverb::pDecay);
            um.beginNewTransaction ("Change Decay");
            rv->param (pulse::Reverb::pDecay)->setParameter (9.0f, sendNotificationSync);
            settle();
            um.beginNewTransaction ("after");
            const float moved = rv->get (pulse::Reverb::pDecay);
            const String stateMoved = p->state["decay"].toString();
            const bool could = um.canUndo();
            um.undo();
            settle();
            const float undone = rv->get (pulse::Reverb::pDecay);
            const String stateUndone = p->state.hasProperty ("decay") ? p->state["decay"].toString() : String ("(default)");
            um.redo();
            settle();
            const float redone = rv->get (pulse::Reverb::pDecay);
            rep.check (std::abs (moved - 9.0f) < 0.01f && could && std::abs (undone - was) < 0.01f && std::abs (redone - 9.0f) < 0.01f,
                       "Undo puts a knob back where it was, redo moves it again",
                       "decay " + String (was, 2) + " -> " + String (moved, 2) + " (project " + stateMoved + "), undo -> " + String (undone, 2) + " (project " + stateUndone + "), redo -> " + String (redone, 2)
                       + (could ? "" : ", nothing to undo"));
            UndoSync undoSync (*edit);   // as the app has for its project
            // the track's own volume fader (an engine device) through the same steps
            String volInfo;
            bool volOk = false;
            if (auto vp = t->getVolumePlugin())
            {
                um.clearUndoHistory();
                const float v0 = vp->volParam->getCurrentValue();
                um.beginNewTransaction ("Change volume");
                vp->volParam->setParameter (0.3f, sendNotificationSync);
                settle();
                um.beginNewTransaction ("after");
                um.undo();
                settle();
                const float v1 = vp->volParam->getCurrentValue();
                volOk = std::abs (v1 - v0) < 0.001f;
                volInfo = String (v0, 3) + " -> 0.3, undo -> " + String (v1, 3);
            }
            rep.check (volOk, "Undo puts a track's volume fader back", volInfo);
            edit->deleteTrack (t);
        }

        // --- 1.0 RC: presets and copies of every device keep every value ----------------------------------------
        {
            rep.step ("preset and duplicate round trip");
            auto t = ClipImport::createContentTrack (*edit, "PresetTrip", false);
            auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("PulseStudioSelfTest").getChildFile ("roundtrip.pulsepreset");
            StringArray bad;
            int devices = 0;
            Random rng (4242);
            auto same = [] (pulse::PulsePlugin& a, pulse::PulsePlugin& b)
            {
                if (a.numParams() != b.numParams()) return String ("different device");
                for (int i = 0; i < a.numParams(); ++i)
                    if (std::abs (a.get (i) - b.get (i)) > 1.0e-4f * jmax (1.0f, std::abs (a.get (i)))) return a.param (i)->paramID + " " + String (a.get (i)) + " -> " + String (b.get (i));
                return String();
            };
            for (auto& b : ClipImport::builtIns())
            {
                if (b.synth || b.midiFx) continue;   // one device at a time on a plain track (instruments and MIDI effects are covered by the state test)
                auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get());
                if (pp == nullptr) continue;
                t->pluginList.insertPlugin (p, 0, nullptr);
                ++devices;
                for (int i = 0; i < pp->numParams(); ++i)
                    pp->set (i, pp->param (i)->valueRange.snapToLegalValue (pp->param (i)->valueRange.convertFrom0to1 (rng.nextFloat())));
                // a copy (Ctrl-drag, duplicate track)
                auto copy = DragDrop::duplicate (*p);
                if (auto* cp = dynamic_cast<pulse::PulsePlugin*> (copy.get())) { if (auto d = same (*pp, *cp); d.isNotEmpty()) bad.add (String (b.name) + " copy: " + d); }
                else bad.add (String (b.name) + " copy: not made");
                // a preset saved and loaded onto a fresh device
                if (auto xml = Presets::snapshot (*p).createXml()) xml->writeTo (tmp);
                auto fresh = edit->getPluginCache().createNewPlugin (b.type, {});
                t->pluginList.insertPlugin (fresh, 0, nullptr);
                auto loaded = Presets::loadDevice (*fresh, tmp);
                if (auto* lp = dynamic_cast<pulse::PulsePlugin*> (loaded.get())) { if (auto d = same (*pp, *lp); d.isNotEmpty()) bad.add (String (b.name) + " preset: " + d); }
                else bad.add (String (b.name) + " preset: not loaded");
                if (loaded != nullptr) loaded->deleteFromParent();
                p->deleteFromParent();
            }
            // a preset from an older version: a Saturator saved with its Type changed
            tmp.replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PLUGIN type=\"5.0\" id=\"77\" enabled=\"1\" drive=\"20.0\" bias=\"-0.3\"/>");
            auto host = edit->getPluginCache().createNewPlugin (pulse::Saturator::xmlTypeName, {});
            t->pluginList.insertPlugin (host, 0, nullptr);
            auto old = Presets::loadDevice (*host, tmp);
            auto* os = dynamic_cast<pulse::Saturator*> (old.get());
            const bool oldOk = os != nullptr && os->getInt (pulse::Saturator::pType) == 5 && std::abs (os->get (pulse::Saturator::pDrive) - 20.0f) < 0.01f;
            tmp.deleteFile();
            edit->deleteTrack (t);
            rep.check (bad.isEmpty() && devices >= 14 && oldOk, "Device presets and copies keep every value (and presets from older versions load)",
                       bad.isEmpty() ? String (devices) + " effects, old Saturator preset " + (oldOk ? "repaired" : "NOT loaded") : bad.joinIntoString ("; "));
        }

        // --- 1.0 RC: every device window draws at any size -------------------------------------------------------
        {
            rep.step ("device windows at any size");
            EditViewState wevs (*edit, sm);
            int drawn = 0;
            StringArray blank;
            for (auto& b : ClipImport::builtIns())
            {
                auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get());
                if (pp == nullptr) continue;
                rep.step (String ("  window ") + b.name);
                DeviceEditors::Editor ed (wevs, *pp);
                // the smallest the window allows, an awkward tall one, the default, and a very large one
                for (auto [w, h] : { std::pair<int, int> { 560, 420 }, { 600, 1400 }, { 1100, 760 }, { 2600, 1500 } })
                {
                    ed.setSize (w, h);
                    MessageManager::getInstance()->runDispatchLoopUntil (15);
                    auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 0.5f);
                    ++drawn;
                    // something other than the background must be there
                    const auto corner = img.getPixelAt (2, img.getHeight() - 3);
                    int different = 0;
                    for (int y = 30; y < img.getHeight(); y += 9)
                        for (int x = 4; x < img.getWidth(); x += 9)
                            if (img.getPixelAt (x, y) != corner) ++different;
                    if (different < 40) blank.add (String (b.name) + " at " + String (w) + "x" + String (h));
                }
                if (auto* w = dynamic_cast<pulse::WavetableSynth*> (pp))   // each tab of the Wavetable, small
                    for (int tab = 0; tab < 4; ++tab)
                    {
                        DeviceEditors::WavetablePanel panel (wevs, *w);
                        panel.showTab (tab);
                        panel.setSize (540, 380);
                        panel.createComponentSnapshot (panel.getLocalBounds(), true, 0.5f);
                        panel.setSize (1900, 1200);
                        panel.createComponentSnapshot (panel.getLocalBounds(), true, 0.5f);
                        drawn += 2;
                    }
            }
            rep.check (drawn > 80 && blank.isEmpty(), "Every device window draws at its smallest, its default and a very large size",
                       blank.isEmpty() ? String (drawn) + " drawings" : "blank: " + blank.joinIntoString (", "));
        }

        // --- 1.0 RC: effects rest on silence and wake with the sound ------------------------------------------
        {
            rep.step ("sleep on silence");
            const double sr = 48000.0;
            const int block = 480;
            auto run = [&] (te::Plugin& p, float level, int blocks, int& k)
            {
                AudioBuffer<float> audio (2, block);
                float peak = 0.0f;
                for (int j = 0; j < blocks; ++j, ++k)
                {
                    for (int i = 0; i < block; ++i) { const float v = level * (float) std::sin ((k * block + i) * 0.05); audio.setSample (0, i, v); audio.setSample (1, i, v); }
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) }, true, false, true, false);
                    p.applyToBuffer (fc);
                    peak = jmax (peak, audio.getMagnitude (0, block));
                }
                return peak;
            };
            auto dp = edit->getPluginCache().createNewPlugin (pulse::Delay::xmlTypeName, {});
            auto* dl = dynamic_cast<pulse::Delay*> (dp.get());
            dl->set (pulse::Delay::pSync, 0.0f); dl->set (pulse::Delay::pTime, 2000.0f); dl->set (pulse::Delay::pFeedback, 0.6f); dl->set (pulse::Delay::pMix, 1.0f);
            dp->initialise ({ te::TimePosition(), sr, block });
            int k = 0;
            run (*dp, 0.5f, 50, k);                                  // half a second of sound
            const float echo = run (*dp, 0.0f, 300, k);               // three seconds of silence: the 2 s echo must still come
            const bool awakeForEcho = echo > 0.05f && ! dl->asleep.load();
            run (*dp, 0.0f, 6000, k);                                 // a minute of silence: the echoes die away, then it rests
            const bool rests = dl->asleep.load();
            const float woke = run (*dp, 0.5f, 300, k);               // sound again: it works at once (and echoes after 2 s)
            const bool wakes = ! dl->asleep.load() && woke > 0.05f;
            dp->deinitialise();
            rep.check (awakeForEcho && rests && wakes, "Effects rest when there is silence in and out, never cut an echo short, and wake with the sound",
                       "echo after silence " + String (echo, 3) + ", resting " + String ((int) rests) + ", after waking " + String (woke, 3));
        }

        // --- 1.0 RC: how fast the Wavetable is --------------------------------------------------------------------
        {
            rep.step ("wavetable speed");
            using W = pulse::WavetableSynth;
            auto wp = edit->getPluginCache().createNewPlugin (W::xmlTypeName, {});
            auto* w = dynamic_cast<W*> (wp.get());
            auto bench = [&] (const String& what, std::function<void()> setup)
            {
                for (auto* prm : w->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync);
                FactoryPresets::clearRoutes (*w);
                setup();
                const double sr = 48000.0;
                const int block = 512, blocks = 470;   // five seconds
                w->initialise ({ te::TimePosition(), sr, block });
                AudioBuffer<float> audio (2, block);
                const auto src = te::createUniqueMPESourceID();
                const double t0 = Time::getMillisecondCounterHiRes();
                for (int k = 0; k < blocks; ++k)
                {
                    audio.clear();
                    te::MidiMessageArray midi;
                    if (k == 0) for (int n : { 36, 43, 48, 52, 55, 60, 64, 67 }) midi.addMidiMessage (MidiMessage::noteOn (1, n, (uint8) 100), 0.0, src);
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) }, true, false, true, false);
                    w->applyToBuffer (fc);
                }
                const double seconds = (Time::getMillisecondCounterHiRes() - t0) * 0.001;
                w->deinitialise();
                const double realTime = blocks * block / sr / jmax (1.0e-6, seconds);
                rep.step ("  speed " + what + ": " + String (realTime, 1) + "x real time (8 notes)");
                return realTime;
            };
            const double plain = bench ("default patch", [] {});
            const double heavy = bench ("7 unison voices, both filters, warp, 4 routes, all effects", [&]
            {
                w->set (W::pVoices, 7); w->set (W::pMix, 0.5f); w->set (W::pWarp1, 1); w->set (W::pWarpAmt1, 0.5f); w->set (W::pWarp2, 3); w->set (W::pWarpAmt2, 0.4f);
                w->set (W::pF2Type, 2); w->set (W::pSub, 0.5f); w->set (W::pNoise, 0.2f); w->set (W::pFDrive, 0.3f);
                w->setRoute (1, "cutoff", 0.3f); w->setRoute (2, "pos1", 0.3f); w->setRoute (5, "pos2", 0.4f); w->setRoute (14, "reso", 0.2f);
                w->set (W::pDrive, 0.3f); w->set (W::pChorusMix, 0.4f); w->set (W::pDelayMix, 0.3f); w->set (W::pReverbMix, 0.3f); w->set (W::pPhaserMix, 0.3f); w->set (W::pEqLow, 3.0f);
            });
            rep.check (plain > 8.0 && heavy > 2.0, "Wavetable: 8 held notes play many times faster than real time on one core",
                       "default patch " + String (plain, 1) + "x, heaviest patch " + String (heavy, 1) + "x");
        }

        // --- 1.0 RC: automation travels with a song section -------------------------------------------------------
        {
            rep.step ("section automation");
            auto e2 = te::createEmptyEdit (engine, dir.getChildFile ("sections-automation.tracktionedit"));
            auto t = ClipImport::createContentTrack (*e2, "Auto", false);
            auto vol = t->getVolumePlugin()->volParam;
            auto& curve = vol->getCurve();
            auto at = [&] (double beat) { return EngineHelpers::beatToTime (*e2, beat); };
            // section A = beats 0..8 with a dip at beat 4; section B = beats 8..16, flat
            curve.addPoint (at (0.0), 0.8f, 0.0f, nullptr);
            curve.addPoint (at (4.0), 0.2f, 0.0f, nullptr);
            curve.addPoint (at (7.9), 0.8f, 0.0f, nullptr);
            curve.addPoint (at (8.0), 0.6f, 0.0f, nullptr);
            curve.addPoint (at (16.0), 0.6f, 0.0f, nullptr);
            Sections::add (*e2, "A", 0.0, 8.0);
            Sections::add (*e2, "B", 8.0, 8.0);
            auto v = [&] (double beat) { return curve.getValueAt (at (beat), 0.0f); };
            Sections::duplicate (*e2, 0);   // A A B
            const bool dup = std::abs (v (4.0) - 0.2f) < 0.02f && std::abs (v (12.0) - 0.2f) < 0.02f && std::abs (v (9.0) - 0.8f) < 0.2f && std::abs (v (20.0) - 0.6f) < 0.02f;
            const float d4 = v (4.0), d12 = v (12.0), d20 = v (20.0);
            // move the first A to the end: A B A  ->  the dip is at 4 and at 16 + 4
            int first = -1;
            for (auto& sct : Sections::all (*e2)) if (first < 0) first = sct.index;
            Sections::move (*e2, first, 24.0);
            const bool moved = std::abs (v (4.0) - 0.2f) < 0.02f && std::abs (v (12.0) - 0.6f) < 0.02f && std::abs (v (20.0) - 0.2f) < 0.02f;
            rep.check (dup && moved, "Automation travels with a song section when it is duplicated or moved",
                       "after duplicate: " + String (d4, 2) + " / " + String (d12, 2) + " / " + String (d20, 2) + "; after move: " + String (v (4.0), 2) + " / " + String (v (12.0), 2) + " / " + String (v (20.0), 2));
        }

        // --- 1.0 RC: found by the random-input run ---------------------------------------------------------------
        {
            rep.step ("monkey regressions");
            auto t = ClipImport::createContentTrack (*edit, "Monkey", true);
            auto mc = te::insertMIDIClip (*t, "Edge", { te::TimePosition::fromSeconds (100.0), te::TimePosition::fromSeconds (104.0) });
            mc->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            // a split exactly at a clip's start or end does nothing (it used to leave a clip of no length)
            sm.selectOnly (mc.get());
            edit->getTransport().setPosition (te::TimePosition::fromSeconds (100.0));
            EditingOps::splitAtPlayhead (*edit, sm);
            edit->getTransport().setPosition (te::TimePosition::fromSeconds (104.0));
            EditingOps::splitAtPlayhead (*edit, sm);
            const int afterEdges = t->getClips().size();
            edit->getTransport().setPosition (te::TimePosition::fromSeconds (102.0));
            EditingOps::splitAtPlayhead (*edit, sm);
            const int afterMiddle = t->getClips().size();
            bool noEmpty = true;
            for (auto c : t->getClips()) noEmpty = noEmpty && c->getPosition().getLength().inSeconds() > 0.01;
            // the piano roll lets go of a clip that has left the project (undo, deleted track) and still draws
            EditViewState pevs (*edit, sm);
            PianoRoll roll (pevs);
            roll.setSize (900, 400);
            auto shown = dynamic_cast<te::MidiClip*> (t->getClips().getFirst());
            roll.setClip (shown);
            roll.createComponentSnapshot (roll.getLocalBounds());
            edit->deleteTrack (t);
            roll.createComponentSnapshot (roll.getLocalBounds());   // this used to crash
            rep.check (afterEdges == 1 && afterMiddle == 2 && noEmpty && roll.getClip() == nullptr,
                       "Splitting at a clip's edge does nothing; the piano roll lets go of a clip that has left the project",
                       String (afterEdges) + " clip after edge splits, " + String (afterMiddle) + " after a middle split");
        }

        // --- 1.0 RC: release pieces ---------------------------------------------------------------------------
        {
            rep.step ("release pieces");
            auto projectFile = edit->editFileRetriever ? edit->editFileRetriever() : File();
            auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("PulseStudioSelfTest").getChildFile ("Double Click.tracktionedit");
            tmp.getParentDirectory().createDirectory();
            tmp.replaceWithText ("<EDIT/>");
            const bool found = About::projectFromCommandLine ({ "--dev-profile", tmp.getFullPathName().quoted() }) == tmp;
            const bool ignored = ! About::projectFromCommandLine ({ "--selftest", "report.txt" }).existsAsFile();
            tmp.replaceWithText ("this is not a project at all, just some text");
            const bool rejectsJunk = ! About::isProjectFile (tmp);
            tmp.replaceWithText ("<?xml version=\"1.0\"?><EDIT appVersion=\"x\"><TRACK/></EDIT>");
            const bool acceptsEdit = About::isProjectFile (tmp);
            tmp.deleteFile();
            rep.check (About::wantsOwnInstance ("--PluginScan:abc") && About::wantsOwnInstance ("--dev-profile --selftest r.txt")
                       && ! About::wantsOwnInstance ("") && ! About::wantsOwnInstance (String ("C:/Music/My Song.tracktionedit").quoted()),
                       "One app at a time: a double-clicked project goes to the running app; helpers and test runs stay separate", {});
            rep.check (rejectsJunk && acceptsEdit, "A damaged file is recognised (it is left untouched instead of being opened and saved over)", {});
            rep.check (found && ignored && About::versionLine().contains (JUCEApplication::getInstance()->getApplicationVersion()) && About::licenceSummary().contains ("Tracktion"),
                       "A project named on the command line is opened; the About box has the version and the licences", About::versionLine());
            // the Saturator and Pitch Correction keep their work buffers (nothing allocated while playing)
            auto sp = edit->getPluginCache().createNewPlugin (pulse::Saturator::xmlTypeName, {});
            auto pc = edit->getPluginCache().createNewPlugin (pulse::PitchCorrect::xmlTypeName, {});
            bool finite = true;
            for (auto* pl : { sp.get(), pc.get() })
            {
                te::PluginInitialisationInfo info { te::TimePosition(), 48000.0, 512 };
                pl->initialise (info);
                AudioBuffer<float> buf (2, 512);
                for (int k = 0; k < 40; ++k)
                {
                    for (int i = 0; i < 512; ++i) { const float v = 0.5f * std::sin ((k * 512 + i) * 0.03f); buf.setSample (0, i, v); buf.setSample (1, i, v); }
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&buf, 0, 512, &midi, 0.0, { te::TimePosition::fromSeconds (k * 512 / 48000.0), te::TimePosition::fromSeconds ((k + 1) * 512 / 48000.0) }, true, false, true, false);
                    pl->applyToBuffer (fc);
                    finite = finite && std::isfinite (buf.getSample (0, 100)) && buf.getMagnitude (0, 512) < 4.01f;
                }
                pl->deinitialise();
            }
            rep.check (finite, "Saturator and Pitch Correction run with their own work buffers", {});
        }

        // --- v0.58: cutoff knobs show the envelope's sweep; nothing lost in the new windows ---------------------
        {
            rep.step ("sweep arcs");
            using W = pulse::WavetableSynth;
            auto wp = edit->getPluginCache().createNewPlugin (W::xmlTypeName, {});
            auto* w = dynamic_cast<W*> (wp.get());
            w->set (W::pCutoff, 1000.0f); w->set (W::pFilterEnv, 0.5f); w->set (W::pLfoCut, 0.0f);
            float lo = 0, hi = 0;
            const bool up = ModDrop::sweep && ModDrop::sweep (*w->param (W::pCutoff), lo, hi) && std::abs (hi - 8000.0f) < 10.0f && std::abs (lo - 1000.0f) < 1.0f;
            w->set (W::pFilterEnv, -0.5f);
            const bool down = ModDrop::sweep (*w->param (W::pCutoff), lo, hi) && std::abs (lo - 125.0f) < 1.0f;
            w->set (W::pFilterEnv, 0.0f);
            const bool none = ! ModDrop::sweep (*w->param (W::pCutoff), lo, hi) && ! ModDrop::sweep (*w->param (W::pReso), lo, hi);
            auto fp = edit->getPluginCache().createNewPlugin (pulse::FMSynth::xmlTypeName, {});
            auto* fm = dynamic_cast<pulse::FMSynth*> (fp.get());
            fm->set (pulse::FMSynth::pCutoff, 500.0f); fm->set (pulse::FMSynth::pFEnv, 1.0f);
            const bool fmOk = ModDrop::sweep (*fm->param (pulse::FMSynth::pCutoff), lo, hi) && hi > 19000.0f;
            rep.check (up && down && none && fmOk, "Cutoff knobs show how far the filter envelope sweeps them", "1 kHz with +50%: up to " + String (roundToInt (hi)) + " Hz (FM)");
            // every parameter of the Wavetable and the FM synth has a control in its window
            EditViewState sevs (*edit, sm);
            auto controlsFor = [&] (pulse::PulsePlugin& pl, Component& panel, std::function<void (int)> showTab, int tabsN)
            {
                std::set<te::AutomatableParameter*> seen;
                std::function<void (Component&)> walk = [&] (Component& c)
                {
                    if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<DeviceEditors::ParamValueBox*> (&c)) seen.insert (&k->getParameter());
                    for (auto* ch : c.getChildren()) walk (*ch);
                };
                for (int t = 0; t < tabsN; ++t) { if (showTab) showTab (t); walk (panel); }
                StringArray missing;
                for (int i = 0; i < pl.numParams(); ++i) if (! seen.count (&*pl.param (i))) missing.add (pl.param (i)->paramID);
                return missing;
            };
            StringArray missW, missF;
            {
                DeviceEditors::WavetablePanel panel (sevs, *w);
                panel.setSize (1260, 800);
                missW = controlsFor (*w, panel, nullptr, 1);
                // the LFO and envelope tabs swap their knobs: those are reached through the tabs (LFO 2-4 rate / sync / retrigger, filter / mod envelope)
                for (int i = missW.size(); --i >= 0;)
                    if (missW[i].startsWith ("l2") || missW[i].startsWith ("l3") || missW[i].startsWith ("l4") || missW[i].endsWith ("shape")
                        || StringArray ({ "fa", "fd", "fs", "fr", "ma", "md", "ms", "mr" }).contains (missW[i])) missW.remove (i);
            }
            {
                DeviceEditors::FMPanel panel (sevs, *fm);
                panel.setSize (1260, 800);
                missF = controlsFor (*fm, panel, nullptr, 1);
            }
            StringArray missS;
            {
                auto sp = edit->getPluginCache().createNewPlugin (pulse::Simpler::xmlTypeName, {});
                auto* simp = dynamic_cast<pulse::Simpler*> (sp.get());
                DeviceEditors::SimplerPanel panel (sevs, *simp);
                panel.setSize (1260, 800);
                missS = controlsFor (*simp, panel, nullptr, 1);
            }
            const bool allThere = missW.isEmpty() && missF.isEmpty() && missS.isEmpty();
            rep.check (allThere, "Wavetable, FM and Simpler windows have a control for every parameter",
                       allThere ? String ("nothing missing") : "missing: " + missW.joinIntoString (" ") + " | " + missF.joinIntoString (" ") + " | " + missS.joinIntoString (" "));
        }

        // --- v0.55: Wavetable overhaul (tables, noise, sub, macros, effects, tabbed window) ------------------
        {
            rep.step ("wavetable 2");
            using W = pulse::WavetableSynth;
            auto wp = edit->getPluginCache().createNewPlugin (W::xmlTypeName, {});
            auto* w = dynamic_cast<W*> (wp.get());
            auto note = [&] (int n, double hold) { return FactorySounds::renderNote (*w, n, hold); };
            auto reset = [&] { for (auto* prm : w->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync);
                               FactoryPresets::clearRoutes (*w); w->set (W::pMix, 0.0f); w->set (W::pVoices, 1.0f); };
            auto rms = [] (const AudioBuffer<float>& b) { return b.getRMSLevel (0, 0, jmin (b.getNumSamples(), 14000)); };
            auto crossings = [] (const AudioBuffer<float>& b)
            {
                int c = 0;
                for (int i = 2400; i < jmin (b.getNumSamples(), 12000); ++i) if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f) ++c;
                return c;
            };
            // every table makes sound
            reset();
            int sounding = 0;
            for (int set = 0; set < pulse::WavetableBank::numSets(); ++set)
            {
                w->set (W::pSet1, (float) set); w->set (W::pPos1, 0.7f);
                if (rms (note (48, 0.3)) > 0.01f) ++sounding;
            }
            rep.check (sounding == pulse::WavetableBank::numSets() && sounding == 17, "Wavetable: 17 wavetables, all sounding", String (sounding) + " tables");
            // noise and sub on their own
            reset(); w->set (W::pLevel1, 0.0f);
            const float silent = rms (note (48, 0.3));
            w->set (W::pNoise, 1.0f);
            const float noisy = rms (note (48, 0.3));
            w->set (W::pNoise, 0.0f); w->set (W::pSub, 1.0f); w->set (W::pFilterType, 0.0f); w->set (W::pCutoff, 20000.0f); w->set (W::pFilterEnv, 0.0f);
            const int subOne = crossings (note (60, 0.3));
            w->set (W::pSubOct, 1.0f);
            const int subTwo = crossings (note (60, 0.3));
            rep.check (silent < 0.001f && noisy > 0.02f && subOne > 20 && std::abs (subOne - 2 * subTwo) <= 3, "Wavetable: noise oscillator, sub one or two octaves down",
                       "noise rms " + String (noisy, 3) + ", sub crossings " + String (subOne) + " / " + String (subTwo));
            // octave switch, macro as a source, EQ
            reset(); w->set (W::pFilterType, 0.0f); w->set (W::pCutoff, 20000.0f); w->set (W::pFilterEnv, 0.0f); w->set (W::pSet1, 0.0f); w->set (W::pPos1, 0.0f);
            const int base = crossings (note (48, 0.3));
            w->set (W::pOct1, 1.0f);
            const int up = crossings (note (48, 0.3));
            reset(); w->set (W::pLevel, -30.0f);
            w->setRoute (14, "level", 1.0f);
            const float quiet = rms (note (48, 0.3));
            w->set (W::pMacro1, 1.0f);
            const float loud = rms (note (48, 0.3));
            reset();
            const float flat = rms (note (36, 0.3));
            w->set (W::pEqLow, 12.0f);
            const float bassy = rms (note (36, 0.3));
            rep.check (std::abs (up - 2 * base) <= 3 && loud > quiet * 4.0f && bassy > flat * 1.5f, "Wavetable: octave switch, macros as modulation sources, EQ",
                       "crossings " + String (base) + " -> " + String (up) + ", macro " + String (Decibels::gainToDecibels (loud / jmax (1e-6f, quiet)), 1) + " dB, low +12: x" + String (bassy / flat, 2));
            // the window: four tabs, the keyboard plays the synth
            {
                reset();
                EditViewState pevs (*edit, sm);
                DeviceEditors::WavetablePanel panel (pevs, *w);
                panel.setSize (1260, 800);
                panel.showTab (2);
                const bool tabsOk = panel.currentTab() == 2 && panel.numTabs() == 4;
                panel.showTab (0);
                w->keyboard.noteOn (1, 60, 0.9f);
                AudioBuffer<float> buf (2, 4800);
                float kbRms = 0;
                for (int k = 0; k < 4; ++k)
                {
                    buf.clear();
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&buf, 0, buf.getNumSamples(), &midi, 0.0, { te::TimePosition::fromSeconds (k * 0.1), te::TimePosition::fromSeconds ((k + 1) * 0.1) }, true, false, true, false);
                    w->applyToBuffer (fc);
                    kbRms = jmax (kbRms, buf.getRMSLevel (0, 0, buf.getNumSamples()));
                }
                w->keyboard.noteOff (1, 60, 0.0f);
                rep.check (tabsOk && kbRms > 0.01f, "Wavetable window: four tabs; notes sent to it from outside the MIDI stream play", "note rms " + String (kbRms, 3));
            }
        }

        // --- v0.53: graph-first Reverb, Delay and Saturator editors ---------------------------------------
        {
            rep.step ("space editors");
            auto make = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); return pl; };
            auto rp = make (pulse::Reverb::xmlTypeName), dp = make (pulse::Delay::xmlTypeName), sp = make (pulse::Saturator::xmlTypeName);
            auto& rv = *dynamic_cast<pulse::Reverb*> (rp.get());
            auto& dl = *dynamic_cast<pulse::Delay*> (dp.get());
            auto& st = *dynamic_cast<pulse::Saturator*> (sp.get());
            SpaceUI::ReverbView rView (rv);
            SpaceUI::DelayView dView (dl);
            SpaceUI::SaturatorView sView (st);
            for (Component* c : { (Component*) &rView, (Component*) &dView, (Component*) &sView }) c->setSize (1000, 560);
            Image img (Image::ARGB, 1000, 560, true);
            { Graphics g (img); rView.paint (g); dView.paint (g); sView.paint (g); }   // lays out the time spans
            // reverb: drag the decay point right -> longer; the predelay point right -> more predelay
            const float decay0 = rv.get (pulse::Reverb::pDecay), pre0 = rv.get (pulse::Reverb::pPredelay);
            rView.dragHandle (2, 120.0f, 0.0f);
            rView.dragHandle (0, 20.0f, 0.0f);
            const bool revOk = rv.get (pulse::Reverb::pDecay) > decay0 + 0.3f && rv.get (pulse::Reverb::pPredelay) > pre0 + 5.0f;
            // delay (synced): dragging the first echo snaps to a note value; dragging the second up raises the feedback
            dl.set (pulse::Delay::pSync, 1.0f);
            dl.set (pulse::Delay::pNote, 4.0f);   // 1/8
            dView.setTime (60.0 / dl.lastBpm.load());   // a quarter note
            const bool snapped = dl.getInt (pulse::Delay::pNote) == 7;
            const float fb0 = dl.get (pulse::Delay::pFeedback);
            dView.dragHandle (1, 0.0f, -40.0f);
            const bool fbUp = dl.get (pulse::Delay::pFeedback) > fb0 + 0.05f;
            // saturator: dragging the shape point up adds drive, sideways moves the bias
            const float drive0 = st.get (pulse::Saturator::pDrive);
            sView.dragHandle (0, 30.0f, -40.0f);
            const bool satOk = st.get (pulse::Saturator::pDrive) > drive0 + 1.0f && st.get (pulse::Saturator::pBias) > 0.05f;
            rep.check (revOk && snapped && fbUp && satOk && rView.getNumHandles() == 5 && dView.getNumHandles() == 4,
                       "Reverb, Delay and Saturator: controls are handles on the graph",
                       "decay " + String (decay0, 2) + " -> " + String (rv.get (pulse::Reverb::pDecay), 2) + " s, note " + dl.param (pulse::Delay::pNote)->getCurrentValueAsString()
                       + ", feedback " + String (fb0, 2) + " -> " + String (dl.get (pulse::Delay::pFeedback), 2)
                       + ", drive " + String (drive0, 1) + " -> " + String (st.get (pulse::Saturator::pDrive), 1));
            rep.check (DeviceEditors::hasFullEditor (rv) && DeviceEditors::hasFullEditor (dl) && DeviceEditors::hasFullEditor (st),
                       "Reverb, Delay and Saturator open as full panels", {});
            {
                auto fp = make (pulse::FMSynth::xmlTypeName);
                auto& fmSynth = *dynamic_cast<pulse::FMSynth*> (fp.get());
                EditViewState fevs (*edit, sm);
                DeviceEditors::FMPanel panel (fevs, fmSynth);
                panel.setSize (1200, 780);
                rep.check (DeviceEditors::hasFullEditor (fmSynth) && panel.numOperatorCards() == 4, "FM Synth opens as its own panel (algorithm, wave, four operators)", {});
            }
        }

        // --- v0.52: arrangement overview (scrolling / zooming) ------------------------------------------------
        {
            rep.step ("overview");
            EditViewState evs (*edit, sm);
            evs.viewStartBeat = 0.0;
            evs.pixelsPerBeat = 40.0;
            Overview ov (evs, [] { return 800; });
            ov.setSize (600, 30);
            auto src = Desktop::getInstance().getMainMouseSource();
            auto ev = [&] (juce::Point<float> down, juce::Point<float> pos)
            {
                const auto now = Time::getCurrentTime();
                return MouseEvent (src, pos, ModifierKeys (ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &ov, &ov, now, down, now, 1, pos != down);
            };
            // click far right: the view jumps there
            const auto far = juce::Point<float> (560.0f, 15.0f);
            ov.mouseDown (ev (far, far));
            const double jumped = evs.viewStartBeat;
            // drag the box back left: it scrolls
            const float boxX = ov.xFor (evs.viewStartBeat) + 20.0f;
            ov.mouseDown (ev ({ boxX, 15.0f }, { boxX, 15.0f }));
            ov.mouseDrag (ev ({ boxX, 15.0f }, { boxX - 100.0f, 15.0f }));
            const double dragged = evs.viewStartBeat;
            // drag the right edge out: it zooms out
            const double ppbBefore = evs.pixelsPerBeat;
            const float right = ov.xFor (ov.viewEnd());
            ov.mouseDown (ev ({ right, 15.0f }, { right, 15.0f }));
            ov.mouseDrag (ev ({ right, 15.0f }, { right + 60.0f, 15.0f }));
            const double ppbAfter = evs.pixelsPerBeat;
            rep.check (jumped > 20.0 && dragged < jumped - 5.0 && ppbAfter < ppbBefore, "Arrangement overview: jump, drag to scroll, drag an edge to zoom",
                       "jump to " + String (jumped, 1) + ", drag to " + String (dragged, 1) + ", zoom " + String (ppbBefore, 1) + " -> " + String (ppbAfter, 1) + " px/beat");
        }

        // --- 1.0 RC: the whole project comes back from disk as it was -----------------------------------------
        {
            rep.step ("project round trip");
            // one line per thing in the project; two projects are the same if these lists are
            auto fingerprint = [] (te::Edit& e)
            {
                StringArray out;
                out.add ("tempo " + String (e.tempoSequence.getTempo (0)->getBpm(), 3) + " sig " + e.tempoSequence.getTimeSig (0)->getStringTimeSig());
                auto plugins = [&out] (const String& where, te::PluginList& list)
                {
                    for (auto pl : list)
                    {
                        String line = where + " device " + pl->getPluginType() + (pl->isEnabled() ? "" : " (off)");
                        // every setting of every device (the built-in ones, the engine's own such as sends, and plugins)
                        double sum = 0;
                        int index = 0;
                        for (auto* prm : pl->getAutomatableParameters())
                        {
                            ++index;
                            if (prm->getCurve().getNumPoints() == 0) sum += prm->valueRange.convertTo0to1 (prm->getCurrentBaseValue()) * index;
                        }
                        line << " params " << String (sum, 3);
                        out.add (line);
                    }
                };
                int ti = 0;
                for (auto t : te::getAudioTracks (e))
                {
                    const String where = "track " + String (ti++) + " \"" + t->getName() + "\"";
                    out.add (where + (t->isMuted (false) ? " muted" : "") + (t->isSolo (false) ? " solo" : "") + " colour " + t->getColour().toString());
                    // (a fader with automation follows the playhead; its curve is counted below instead)
                    if (auto vp = t->getVolumePlugin())
                        out.add (where + " vol " + (vp->volParam->getCurve().getNumPoints() > 0 ? String ("automated") : String (vp->getVolumeDb(), 2))
                                 + " pan " + (vp->panParam->getCurve().getNumPoints() > 0 ? String ("automated") : String (vp->getPan(), 3)));
                    plugins (where, t->pluginList);
                    for (auto c : t->getClips())
                    {
                        String line = where + " clip \"" + c->getName() + "\" " + String (c->getPosition().getStart().inSeconds(), 4) + "-" + String (c->getPosition().getEnd().inSeconds(), 4)
                                      + (c->isLooping() ? " looping" : "");
                        if (auto mc = dynamic_cast<te::MidiClip*> (c))
                        {
                            double sum = 0;
                            for (auto n : mc->getSequence().getNotes()) sum += n->getNoteNumber() * 1.0 + n->getStartBeat().inBeats() * 7.0 + n->getLengthBeats().inBeats() * 3.0 + n->getVelocity() * 0.1;
                            line << " notes " << mc->getSequence().getNumNotes() << " sum " << String (sum, 3);
                        }
                        if (auto ac = dynamic_cast<te::WaveAudioClip*> (c)) line << " file " << ac->getOriginalFile().getFileName() << " gain " << String (ac->getGainDB(), 2);
                        out.add (line);
                    }
                    // session view: what sits in each slot
                    int slotIndex = 0;
                    for (auto* slot : t->getClipSlotList().getClipSlots())
                    {
                        if (auto* c = slot->getClip())
                            out.add (where + " slot " + String (slotIndex) + " \"" + c->getName() + "\" " + String (c->getPosition().getLength().inSeconds(), 4)
                                     + (dynamic_cast<te::MidiClip*> (c) != nullptr ? " notes " + String (dynamic_cast<te::MidiClip*> (c)->getSequence().getNumNotes()) : String()));
                        ++slotIndex;
                    }
                    for (auto ap : t->getAllAutomatableParams())
                        if (ap->getCurve().getNumPoints() > 0) out.add (where + " automation " + ap->paramID + " points " + String (ap->getCurve().getNumPoints()));
                }
                plugins ("master", e.getMasterPluginList());
                out.add ("tempos " + String (e.tempoSequence.getNumTempos()) + " time signatures " + String (e.tempoSequence.getNumTimeSigs())
                         + " loop beats " + String (e.tempoSequence.toBeats (e.getTransport().getLoopRange().getStart()).inBeats(), 3)
                         + "-" + String (e.tempoSequence.toBeats (e.getTransport().getLoopRange().getEnd()).inBeats(), 3));
                out.add ("scenes " + String (e.getSceneList().getNumScenes()));
                for (auto& sec : Sections::all (e)) out.add ("section \"" + sec.name + "\" " + String (sec.start, 3) + " + " + String (sec.length, 3));
                for (auto m : e.getMarkerManager().getMarkers()) out.add ("marker \"" + m->getName() + "\" " + String (m->getPosition().getStart().inSeconds(), 3));
                return out;
            };
            {
                // undo takes a string of edits all the way back, redo brings them all forward again
                auto& um = edit->getUndoManager();
                um.clearUndoHistory();
                const auto start = fingerprint (*edit);
                auto tracks = te::getAudioTracks (*edit);
                um.beginNewTransaction ("t1");
                if (auto t = tracks.getFirst()) t->setName ("Undo Test Name");
                um.beginNewTransaction ("t2");
                for (auto t : tracks)
                    for (auto pl : t->pluginList)
                        if (auto* pp = dynamic_cast<pulse::PulsePlugin*> (pl); pp != nullptr && pp->numParams() > 0)
                        {
                            auto* prm = pp->param (0);
                            pp->set (0, prm->valueRange.snapToLegalValue (prm->valueRange.convertFrom0to1 (1.0f - prm->valueRange.convertTo0to1 (pp->get (0)))));
                        }
                um.beginNewTransaction ("t3");
                for (auto t : tracks)
                    for (auto c : t->getClips())
                        if (auto mc = dynamic_cast<te::MidiClip*> (c)) { mc->getSequence().addNote (64, te::BeatPosition::fromBeats (0.5), te::BeatDuration::fromBeats (0.25), 90, 0, &um); break; }
                um.beginNewTransaction ("t4");
                if (tracks.size() > 1) if (auto c = tracks[1]->getClips().getFirst()) c->setName ("Renamed By Undo Test");
                um.beginNewTransaction ("end");
                MessageManager::getInstance()->runDispatchLoopUntil (120);
                const auto changed = fingerprint (*edit);
                int undos = 0, redos = 0;
                auto settle = [] { MessageManager::getInstance()->runDispatchLoopUntil (120); };   // knobs follow the project a moment later
                settle();
                while (um.canUndo() && undos < 50) { um.undo(); ++undos; }
                settle();
                const auto undone = fingerprint (*edit);
                while (um.canRedo() && redos < 50) { um.redo(); ++redos; }
                settle();
                const auto redone = fingerprint (*edit);
                String firstDiff;
                for (int i = 0; i < jmax (start.size(), undone.size()) && firstDiff.isEmpty(); ++i) if (start[i] != undone[i]) firstDiff = "after undo: was " + start[i] + " | now " + undone[i];
                for (int i = 0; i < jmax (changed.size(), redone.size()) && firstDiff.isEmpty(); ++i) if (changed[i] != redone[i]) firstDiff = "after redo: was " + changed[i] + " | now " + redone[i];
                rep.check (changed != start && firstDiff.isEmpty(), "Undo takes a string of edits all the way back and redo brings them all forward",
                           firstDiff.isEmpty() ? String (undos) + " undos, " + String (redos) + " redos" : firstDiff);
                while (um.canUndo()) um.undo();   // leave the project as it was
                um.clearUndoHistory();
                settle();
            }
            {
                // one editing step at a time: undo must give exactly the project before it, redo exactly the project after it
                rep.step ("each editing step undone and redone");
                auto& um = edit->getUndoManager();
                auto settle = [] { MessageManager::getInstance()->runDispatchLoopUntil (60); };
                UndoSync undoSync (*edit);
                auto firstMidi = [&] () -> te::MidiClip*
                {
                    for (auto t : te::getAudioTracks (*edit))
                        for (auto c : t->getClips())
                            if (auto mc = dynamic_cast<te::MidiClip*> (c); mc != nullptr && mc->getSequence().getNumNotes() >= 4) return mc;
                    return nullptr;
                };
                struct Step { String name; std::function<void()> run; };
                Random rng (99);
                te::AudioTrack* made = nullptr;
                te::Plugin::Ptr addedFx;
                std::vector<Step> steps {
                    { "rename a track",        [&] { if (auto t = te::getAudioTracks (*edit).getFirst()) t->setName ("Step Test"); } },
                    { "change the tempo",      [&] { edit->tempoSequence.getTempo (0)->setBpm (133.0); } },
                    { "mute a track",          [&] { if (auto t = te::getAudioTracks (*edit).getFirst()) EngineHelpers::toggleMute (*t); } },
                    { "solo a track",          [&] { if (auto t = te::getAudioTracks (*edit)[1]) EngineHelpers::toggleSolo (*t); } },
                    { "add a section",         [&] { Sections::add (*edit, "Step Section", 8.0, 8.0); } },
                    { "rename a section",      [&] { if (! Sections::all (*edit).empty()) Sections::rename (*edit, 0, "Renamed"); } },
                    { "duplicate a section",   [&] { if (! Sections::all (*edit).empty()) Sections::duplicate (*edit, 0); } },
                    { "move a section",        [&] { if (Sections::all (*edit).size() > 1) Sections::move (*edit, 0, 64.0, &sm); } },
                    { "remove a section",      [&] { if (! Sections::all (*edit).empty()) Sections::remove (*edit, 0, &sm); } },
                    { "rename a clip",         [&] { if (auto c = firstMidi()) c->setName ("Step Clip"); } },
                    { "move a clip",           [&] { if (auto c = firstMidi()) c->setStart (c->getPosition().getStart() + te::TimeDuration::fromSeconds (1.0), false, true); } },
                    { "duplicate a clip",      [&] { if (auto c = firstMidi()) { sm.selectOnly (c); EditingOps::duplicate (sm); } } },
                    { "split a clip",          [&] { if (auto c = firstMidi()) { sm.selectOnly (c); EditingOps::splitAtPlayhead (*edit, sm); } } },
                    { "MIDI: legato",          [&] { if (auto c = firstMidi()) MidiTools::legato (*c, {}); } },
                    { "MIDI: reverse",         [&] { if (auto c = firstMidi()) MidiTools::reverse (*c, {}); } },
                    { "MIDI: invert",          [&] { if (auto c = firstMidi()) MidiTools::invert (*c, {}); } },
                    { "MIDI: stretch",         [&] { if (auto c = firstMidi()) MidiTools::stretch (*c, {}, 0.5); } },
                    { "MIDI: humanize",        [&] { if (auto c = firstMidi()) MidiTools::humanize (*c, {}, 0.05, 20, rng); } },
                    { "MIDI: strum",           [&] { if (auto c = firstMidi()) MidiTools::strum (*c, {}, 0.05, true); } },
                    { "MIDI: fit to scale",    [&] { if (auto c = firstMidi()) MidiTools::fitToScale (*c, {}, 1, 3); } },
                    { "MIDI: velocities",      [&] { if (auto c = firstMidi()) MidiTools::velocities (*c, {}, MidiTools::Velocity::softer, rng); } },
                    { "delete a clip",         [&] { if (auto c = firstMidi()) { sm.selectOnly (c); EditingOps::deleteSelected (sm); } } },
                    { "add a track",           [&] { made = ClipImport::createContentTrack (*edit, "Step Track", true); } },
                    { "add an effect",         [&] { if (made != nullptr) { addedFx = edit->getPluginCache().createNewPlugin (pulse::Delay::xmlTypeName, {}); PluginMenu::insertPlugin (made, made->pluginList, addedFx, nullptr); } } },
                    { "turn an effect's knob", [&] { if (auto* d = dynamic_cast<pulse::Delay*> (addedFx.get())) d->param (pulse::Delay::pFeedback)->setParameter (0.83f, sendNotificationSync); } },
                    { "remove an effect",      [&] { if (addedFx != nullptr) addedFx->deleteFromParent(); } },
                    { "track colour",          [&] { if (made != nullptr) made->setColour (Colours::hotpink); } },
                    { "track volume",          [&] { if (made != nullptr) if (auto vp = made->getVolumePlugin()) vp->volParam->setParameter (0.4f, sendNotificationSync); } },
                    { "delete a track",        [&] { if (made != nullptr) { edit->deleteTrack (made); made = nullptr; } } },
                };
                StringArray wrong, skipped;
                int done = 0;
                um.clearUndoHistory();
                for (auto& st : steps)
                {
                    rep.step ("  step: " + st.name);
                    // the playhead sits in the middle of the first MIDI clip (for the split); moving it is not an edit
                    if (auto c = firstMidi()) edit->getTransport().setPosition (c->getPosition().getStart() + c->getPosition().getLength() * 0.5);
                    settle();
                    um.beginNewTransaction (st.name);
                    const auto fp0 = fingerprint (*edit);
                    st.run();
                    settle();
                    um.beginNewTransaction ("after " + st.name);
                    const auto fp1 = fingerprint (*edit);
                    if (fp1 == fp0) { skipped.add (st.name); continue; }   // nothing for it to do in this project
                    auto diff = [] (const StringArray& a, const StringArray& b)
                    {
                        for (int i = 0; i < jmax (a.size(), b.size()); ++i) if (a[i] != b[i]) return "was: " + a[i] + " | now: " + b[i];
                        return String();
                    };
                    um.undo();
                    settle();
                    const auto fpU = fingerprint (*edit);
                    if (fpU != fp0) { wrong.add (st.name + " not undone (" + diff (fp0, fpU) + ")"); }
                    um.redo();
                    settle();
                    const auto fpR = fingerprint (*edit);
                    if (fpR != fp1) wrong.add (st.name + " not redone (" + diff (fp1, fpR) + ")");
                    ++done;
                }
                // back to where the project was before these steps
                settle();
                for (int i = 0; i < 200 && um.canUndo(); ++i) um.undo();
                settle();
                um.clearUndoHistory();
                rep.check (wrong.isEmpty() && done >= 22, "Every editing step is undone and redone exactly (" + String ((int) steps.size()) + " kinds of edit)",
                           wrong.isEmpty() ? String (done) + " steps" + (skipped.isEmpty() ? String() : ", nothing to do for: " + skipped.joinIntoString (", ")) : wrong.joinIntoString ("  //  "));
            }
            te::EditFileOperations (*edit).save (true, true, false);
            const auto before = fingerprint (*edit);
            const auto file = te::EditFileOperations (*edit).getEditFile();
            auto copyFile = file.getSiblingFile ("Round Trip Copy.tracktionedit");
            file.copyFileTo (copyFile);
            StringArray diffs;
            int lines = 0;
            if (auto again = te::loadEditFromFile (engine, copyFile))
            {
                DrumRack::migrateAll (*again);
                const auto after = fingerprint (*again);
                lines = after.size();
                for (int i = 0; i < jmax (before.size(), after.size()); ++i)
                    if (before[i] != after[i]) { diffs.add ("was: " + before[i] + " | now: " + after[i]); if (diffs.size() >= 4) break; }
            }
            else diffs.add ("the saved project did not load");
            copyFile.deleteFile();
            rep.check (diffs.isEmpty() && lines > 20, "A saved project reopens identical (tracks, clips, notes, devices and their settings, automation, markers)",
                       diffs.isEmpty() ? String (lines) + " things compared" : diffs.joinIntoString ("  //  "));
        }

        // --- sharing: project zip, crash bundle, update check -----------------------------------------
        {
            rep.step ("sharing tools");
            // project zip round trip
            auto zipFile = dir.getChildFile ("project.zip");
            const auto err = ShareTools::exportZip (*edit, zipFile);
            int entries = 0;
            bool hasEdit = false, hasSamples = false;
            {
                ZipFile z (zipFile);
                entries = z.getNumEntries();
                for (int i = 0; i < entries; ++i)
                {
                    auto name = z.getEntry (i)->filename;
                    hasEdit = hasEdit || name.endsWith (".tracktionedit");
                    hasSamples = hasSamples || name.contains ("Samples/");
                }
            }
            auto importDir = dir.getChildFile ("Imported");
            importDir.deleteRecursively();
            importDir.createDirectory();
            auto imported = ShareTools::importZip (zipFile, importDir);
            int importedTracks = -1;
            if (imported.existsAsFile())
                if (auto e2 = te::loadEditFromFile (engine, imported)) importedTracks = EngineHelpers::getUserTracks (*e2).size();
            rep.check (err.isEmpty() && hasEdit && hasSamples && importedTracks == EngineHelpers::getUserTracks (*edit).size(),
                       "Project as a single .zip (export, open)", String (entries) + " files, " + String (importedTracks) + " tracks after opening");

            // crash bundle
            auto fake = CrashGuard::crashFolder().getChildFile ("crash-selftest.txt");
            fake.replaceWithText ("test crash");
            auto bundle = ShareTools::bundleCrashReports (dir, "test");
            bool bundled = false;
            if (bundle.existsAsFile())
            {
                ZipFile z (bundle);
                bundled = z.getIndexOfFileName ("system.txt") >= 0 && z.getIndexOfFileName ("crashes/crash-selftest.txt") >= 0;
            }
            fake.deleteFile();
            bundle.deleteFile();
            rep.check (bundled, "Crash report bundle (zip with system details)");

            // update check (a local file stands in for the address; nothing is downloaded)
            auto json = dir.getChildFile ("update.json");
            json.replaceWithText ("{\"version\": \"9.9.0\", \"url\": \"https://example.invalid/setup.exe\", \"notes\": \"test\"}");
            std::optional<ShareTools::UpdateInfo> got;
            bool called = false;
            ShareTools::checkForUpdate (URL (json).toString (false), [&] (std::optional<ShareTools::UpdateInfo> i) { got = i; called = true; });
            for (int i = 0; i < 100 && ! called; ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
            const bool versions = ShareTools::isNewer ("0.35.1", "0.35.0") && ShareTools::isNewer ("1.0", "0.99.9") && ! ShareTools::isNewer ("0.9.10", "0.10.0")
                                  && ! ShareTools::isNewer ("0.35.0", "0.35.0") && ShareTools::isNewer ("0.10.0", "0.9.10");
            rep.check (called && got.has_value() && got->version == "9.9.0" && versions, "Update check (opt-in address, version compare)",
                       called ? (got.has_value() ? got->version : String ("no data")) : String ("no reply"));
        }

        // --- undo -------------------------------------------------------------------------------
        rep.check (um.canUndo(), "Undo history recorded");

        for (auto& w : PulseUIBehaviour::warnings())
            rep.lines.add ("NOTE  engine warning: " + w);

        sm.deselectAll();   // the selection must not outlive the Edit

        rep.lines.insert (0, "Pulse Studio self-test  " + Time::getCurrentTime().toString (true, true));
        rep.lines.insert (1, String (rep.lines.size() - 1 - rep.failures) + " passed, " + String (rep.failures) + " failed");
        rep.lines.insert (2, {});
        reportFile.replaceWithText (rep.lines.joinIntoString ("\n"));

        // release every object owned by the Edit before the Edit itself goes
        mc = nullptr;
        ac = nullptr;
        rackPlugin = nullptr;
        edit = nullptr;
        return rep.failures;
    }
}
