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
                             && DrumRack::padName (*copyRack->type, 0) == "kick",
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
            if (auto rt = DrumRack::rackOf (shareRack.get())) DrumRack::loadSample (*rt, 0, extPad);

            auto result = Sharing::collectAll (*edit);
            auto folder = Sharing::samplesFolder (*edit);
            auto* w = dynamic_cast<te::WaveAudioClip*> (extC.get());
            auto* sp = dynamic_cast<pulse::Simpler*> (simp.get());
            te::SamplerPlugin* padSampler = nullptr;
            if (auto rt = DrumRack::rackOf (shareRack.get()))
                for (auto pl : rt->getPlugins()) if (auto smp = dynamic_cast<te::SamplerPlugin*> (pl)) { padSampler = smp; break; }
            const bool clipInside = w != nullptr && w->getOriginalFile().isAChildOf (folder);
            const bool simplerInside = sp != nullptr && File (sp->state[pulse::Simpler::sampleFileID].toString()).isAChildOf (folder);
            const bool padInside = padSampler != nullptr && padSampler->getNumSounds() > 0 && padSampler->getSoundFile (0).getFile().isAChildOf (folder);
            rep.check (clipInside && simplerInside && padInside && result.copied >= 3,
                       "Collect all and save (clip, Simpler, Drum Rack pad)", String (result.copied) + " copied, " + String (result.missing.size()) + " missing; clip " + String ((int) clipInside) + " simpler " + String ((int) simplerInside) + " pad " + String ((int) padInside) + (padSampler != nullptr && padSampler->getNumSounds() > 0 ? " (" + padSampler->getSoundFile (0).getFile().getFullPathName() + ")" : String()));

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
