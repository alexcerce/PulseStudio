#pragma once

#include "Common.h"
#include "Routing.h"
#include "ClipImport.h"
#include "EditingOps.h"
#include "DrumRack.h"
#include "Presets.h"
#include "Macros.h"
#include "MidiLearn.h"
#include "SessionView.h"
#include "Browser.h"
#include "PluginWindow.h"
#include "StateControls.h"

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

    inline int run (te::Engine& engine, const File& reportFile)
    {
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
            rep.check (DrumRack::loadSample (*rack, 0, hit) && DrumRack::padName (*rack, 0) == "kick", "Load sample onto pad");
            DrumRack::addEffect (*rack, 0, edit->getPluginCache().createNewPlugin (te::LowPassPlugin::xmlTypeName, {}));
            rep.check (DrumRack::chain (*rack, 0).size() == 1, "Per-pad effect chain");
            DrumRack::setTune (*rack, 0, 3);
            rep.check (DrumRack::getTune (*rack, 0) == 3, "Pad tune");

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
                if (r == nullptr) return {};
                AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
                r->read (&b, 0, b.getNumSamples(), 0, true, true);
                r = nullptr;
                f.deleteFile();
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
                if (b.synth) continue;
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
