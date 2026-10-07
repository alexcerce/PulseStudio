#pragma once

#include "Common.h"
#include "Routing.h"
#include "ClipImport.h"
#include "EditingOps.h"
#include "AutoFreeze.h"
#include "Layouts.h"
#include "SongKey.h"
#include "Notes.h"
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
#include "MidiMapView.h"
#include "Video.h"
#include "HelpLine.h"
#include "FirstRun.h"
#include "DemoSong.h"
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
        shoot (pulse::ConvolutionReverb::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::ConvolutionReverb::pPredelay, 40); p.set (pulse::ConvolutionReverb::pLowCut, 180); p.set (pulse::ConvolutionReverb::pHighCut, 9000); }, "shot_space.png");
        shoot (pulse::Chorus::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Chorus::pMode, 1); p.set (pulse::Chorus::pAmount, 0.7f); }, "shot_chorus.png");
        shoot (pulse::PhaserFlanger::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::PhaserFlanger::pStages, 2); p.set (pulse::PhaserFlanger::pFeedback, 0.6f); }, "shot_phaser.png");
        shoot (pulse::AutoPan::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::AutoPan::pAmount, 0.8f); p.set (pulse::AutoPan::pPhase, 120); }, "shot_autopan.png");
        shoot (pulse::FreqShifter::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::FreqShifter::pShift, 240); p.set (pulse::FreqShifter::pLfoAmount, 120); }, "shot_shifter.png");
        shoot (pulse::Redux::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Redux::pRate, 6000); p.set (pulse::Redux::pBits, 4); }, "shot_redux.png");
        shoot (pulse::Exciter::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Exciter::pAmount, 0.6f); p.set (pulse::Exciter::pColour, 0.6f); }, "shot_exciter.png");
        shoot (pulse::KickSynth::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::KickSynth::pClick, 0.6f); }, "shot_kick.png");
        shoot (pulse::Rumble::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Rumble::pAmount, -3.0f); }, "shot_rumble.png");
        shoot (pulse::Perform::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Perform::pSweep, -0.5f); p.set (pulse::Perform::pResonance, 0.6f); }, "shot_perform.png");
        shoot (pulse::Amp::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Amp::pModel, 2); p.set (pulse::Amp::pGain, 0.7f); p.set (pulse::Amp::pMid, 0.7f); p.set (pulse::Amp::pCabinet, 3); }, "shot_amp.png");
        shoot (pulse::Resonators::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Resonators::pChord, 5); p.set (pulse::Resonators::pDecay, 3.0f); }, "shot_resonators.png");
        shoot (pulse::Shimmer::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Shimmer::pShimmer, 0.7f); }, "shot_shimmer.png");
        shoot (pulse::GrainDelay::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::GrainDelay::pPitch, 7); p.set (pulse::GrainDelay::pSpray, 150); p.set (pulse::GrainDelay::pRandomPitch, 0.3f); p.set (pulse::GrainDelay::pFeedback, 0.6f); }, "shot_grain.png");
        shoot (pulse::BeatRepeat::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::BeatRepeat::pDecay, 0.4f); p.set (pulse::BeatRepeat::pGate, 3); p.set (pulse::BeatRepeat::pPitch, -7); p.set (pulse::BeatRepeat::pPitchDecay, 1.0f); }, "shot_beatrepeat.png");
        shoot (pulse::Looper::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Looper::pState, 1); p.set (pulse::Looper::pLength, 1); }, "shot_looper.png");
        shoot (pulse::Spectral::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Spectral::pDelay, 900); p.set (pulse::Spectral::pTilt, 0.7f); p.set (pulse::Spectral::pShift, 7); }, "shot_spectral.png");
        shoot (pulse::Organ::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Organ::pBar4, 6); p.set (pulse::Organ::pBar6, 3); p.set (pulse::Organ::pBar9, 5); p.set (pulse::Organ::pRotary, 2); }, "shot_organ.png");
        shoot (pulse::EPiano::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::EPiano::pBark, 0.7f); }, "shot_epiano.png");
        shoot (pulse::Analog::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Analog::pWave2, 1); p.set (pulse::Analog::pReso, 0.6f); p.set (pulse::Analog::pCutoff, 1200); }, "shot_analog.png");
        shoot (pulse::StringModel::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::StringModel::pPosition, 0.15f); p.set (pulse::StringModel::pBrightness, 0.8f); }, "shot_string.png");
        shoot (pulse::NoteLength::xmlTypeName, nullptr, "shot_notelength.png");
        shoot (pulse::VelocityFx::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::VelocityFx::pCurve, 0.5f); p.set (pulse::VelocityFx::pOutLow, 40); }, "shot_velocity.png");
        shoot (pulse::RandomFx::xmlTypeName, nullptr, "shot_random.png");
        shoot (pulse::Arpeggiator::xmlTypeName, [] (pulse::PulsePlugin& p) { dynamic_cast<pulse::Arpeggiator&> (p).setPattern ({ 1.0f, 0.4f, 0.7f, 0.0f, 1.0f, 0.4f, 0.0f, 0.6f, 1.0f, 0.3f, 0.7f, 0.0f, 0.9f, 0.5f, 0.7f, 0.3f }); }, "shot_arp.png");
        shoot (pulse::MatchEQ::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            // two made-up sounds, so that the picture has something in it: a dull one, and a brighter reference
            auto& m = dynamic_cast<pulse::MatchEQ&> (p);
            auto f1 = File::getSpecialLocation (File::tempDirectory).getChildFile ("pulse_shot_match_a.wav"), f2 = File::getSpecialLocation (File::tempDirectory).getChildFile ("pulse_shot_match_b.wav");
            Random rnd (5);
            AudioBuffer<float> a (2, 96000), b (2, 96000);
            pulse::Biquad lp, hs, bell;
            lp.set (pulse::Biquad::Type::highCut, 48000.0, 5000.0, 0.6, 0.0);
            hs.set (pulse::Biquad::Type::highShelf, 48000.0, 6000.0, 0.7, 5.0);
            bell.set (pulse::Biquad::Type::bell, 48000.0, 300.0, 1.2, -5.0);
            float brown = 0.0f;
            for (int i = 0; i < 96000; ++i)
            {
                brown = brown * 0.97f + (rnd.nextFloat() * 2.0f - 1.0f) * 0.1f;
                const float x = lp.process (brown, 0), y = bell.process (hs.process (brown, 0), 0);
                a.setSample (0, i, x); a.setSample (1, i, x); b.setSample (0, i, y); b.setSample (1, i, y);
            }
            WavAudioFormat wav;
            for (auto [f, buf] : { std::pair<File, AudioBuffer<float>*> { f1, &a }, std::pair<File, AudioBuffer<float>*> { f2, &b } })
            {
                f.deleteFile();
                if (auto os = f.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (*buf, 0, 96000);
            }
            m.learnFromFile (f1, false);
            m.learnFromFile (f2, true);
            f1.deleteFile(); f2.deleteFile();
        }, "shot_matcheq.png");
        shoot (pulse::Reference::xmlTypeName, [] (pulse::PulsePlugin& p)
        {
            // a made-up song, so that the picture has something in it
            auto& r = dynamic_cast<pulse::Reference&> (p);
            auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("A Finished Song.wav");
            Random rnd (11);
            AudioBuffer<float> a (2, 48000 * 20);
            float lp = 0.0f;
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const double tm = i / 48000.0;
                const float loud = (tm < 3.0 ? 0.25f : tm < 8.0 ? 0.6f : tm < 11.0 ? 0.35f : tm < 17.0 ? 0.8f : 0.8f * (float) (20.0 - tm) / 3.0f) * (0.6f + 0.4f * (float) std::pow (1.0 - std::fmod (tm * 2.0, 1.0), 3.0));
                lp = lp * 0.9f + (rnd.nextFloat() * 2.0f - 1.0f) * 0.1f;
                a.setSample (0, i, lp * loud * 3.0f); a.setSample (1, i, lp * loud * 3.0f);
            }
            f.deleteFile();
            WavAudioFormat wav;
            if (auto os = f.createOutputStream())
                if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
            r.loadNow (f);
            r.set (pulse::Reference::pOffset, 3.0f);
            f.deleteFile();
        }, "shot_reference.png");
        shoot (pulse::Gate::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::Gate::pThreshold, -22); p.set (pulse::Gate::pRange, 40); p.set (pulse::Gate::pHysteresis, 6); }, "shot_gate.png");
        shoot (pulse::TransientShaper::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::TransientShaper::pAttack, 0.6f); p.set (pulse::TransientShaper::pSustain, -0.4f); }, "shot_transient.png");
        shoot (pulse::DeEsser::xmlTypeName, [] (pulse::PulsePlugin& p) { p.set (pulse::DeEsser::pThreshold, -36); p.set (pulse::DeEsser::pRange, 10); }, "shot_deesser.png");
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

            {
                // a track with several clips is left with the one rendered clip and nothing else
                auto many = ClipImport::createContentTrack (*edit, "ToFlattenMany", true);
                for (int i = 0; i < 3; ++i)
                {
                    auto mc = many->insertMIDIClip ("part " + String (i + 1), { bars (i), bars (i + 1) }, nullptr);
                    mc->getSequence().addNote (60 + i, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 110, 0, nullptr);
                }
                const bool flat = EditingOps::flatten (*many);
                rep.check (flat && many->getClips().size() == 1 && dynamic_cast<te::WaveAudioClip*> (many->getClips()[0]) != nullptr, "Flatten a track with several clips (only the rendered clip is left)",
                           String (many->getClips().size()) + " clips afterwards");
                edit->deleteTrack (many);
            }

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
                if (EditingOps::renderDetached ("stem", p).existsAsFile() && peakDb (engine, p.destFile) > -60.0f)
                    ++stemsOk;
            }
            rep.check (stemsOk == 2, "Render stems (non-silent)", String (stemsOk) + "/2");

            auto mix = dir.getChildFile ("mix.wav");
            EditingOps::renderDetached (*edit, mix);
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
                if (b.synth || b.midiFx || String (b.type) == "fxrack") continue;   // (the Effect Rack is not one device: it has its own check)
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
                if (! responds && String (b.type) == pulse::Reference::xmlTypeName) { responds = true; moved = "nothing (this is a render: the reference song is never part of one, the mix passes through)"; }
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

            // --- v0.99.19: note length, pitch, random, velocity ---------------------------------------------
            {
                auto on = [] (int n, int v = 100) { return MidiMessage::noteOn (1, n, (uint8) v); };
                auto off = [] (int n) { return MidiMessage::noteOff (1, n); };
                auto offTimes = [] (const std::vector<std::pair<double, MidiMessage>>& v) { std::vector<double> t; for (auto& [tm, m] : v) if (m.isNoteOff()) t.push_back (tm); return t; };
                auto onTimes2 = [] (const std::vector<std::pair<double, MidiMessage>>& v) { std::vector<double> t; for (auto& [tm, m] : v) if (m.isNoteOn()) t.push_back (tm); return t; };

                // note length: a key held for a second comes out a sixteenth long (0.125 s at 120); half that with the gate at 50%; and in milliseconds
                auto nlp = make (pulse::NoteLength::xmlTypeName);
                auto* nl = dynamic_cast<pulse::NoteLength*> (nlp.get());
                using NL = pulse::NoteLength;
                auto held = runFx (*nlp, { { 0.0, on (60) }, { 1.0, off (60) } }, 1.5);
                const double sixteenth = offTimes (held).size() == 1 ? offTimes (held)[0] : -1.0;
                nl->set (NL::pGate, 0.5f);
                auto half = runFx (*nlp, { { 0.0, on (60) }, { 1.0, off (60) } }, 1.5);
                const double halfGate = offTimes (half).size() == 1 ? offTimes (half)[0] : -1.0;
                nl->set (NL::pGate, 1.0f); nl->set (NL::pMode, 0); nl->set (NL::pTime, 300.0f);
                auto timed = runFx (*nlp, { { 0.0, on (60) }, { 0.01, off (60) } }, 1.0);
                const double threeHundred = offTimes (timed).size() == 1 ? offTimes (timed)[0] : -1.0;
                // the same note again before the first has ended: the first is ended first, and there is one ending for each beginning
                auto again = runFx (*nlp, { { 0.0, on (60) }, { 0.05, off (60) }, { 0.1, on (60) }, { 0.15, off (60) } }, 1.0);
                const bool paired = onTimes2 (again).size() == 2 && offTimes (again).size() == 2 && std::abs (offTimes (again)[0] - 0.1) < 0.011 && std::abs (offTimes (again)[1] - 0.4) < 0.011;
                // played when the key comes up, with the velocity the key went down with
                nl->set (NL::pTrigger, 1);
                auto released = runFx (*nlp, { { 0.0, on (64, 77) }, { 0.5, off (64) } }, 1.2);
                int relVel = 0;
                for (auto& [tm, m] : released) if (m.isNoteOn()) relVel = m.getVelocity();
                const bool onUp = onTimes2 (released).size() == 1 && std::abs (onTimes2 (released)[0] - 0.5) < 0.011 && relVel == 77 && offTimes (released).size() == 1 && std::abs (offTimes (released)[0] - 0.8) < 0.011;
                rep.check (std::abs (sixteenth - 0.125) < 0.011 && std::abs (halfGate - 0.0625) < 0.011 && std::abs (threeHundred - 0.3) < 0.011 && paired && onUp,
                           "MIDI effect: Note Length (a note value, the gate, milliseconds, the same note twice, play on key up)",
                           "1/16 ends at " + String (sixteenth, 3) + " s, at 50% " + String (halfGate, 4) + ", 300 ms at " + String (threeHundred, 3));

                // pitch: moved, and only a range let through (with the right note-offs)
                auto pp5 = make (pulse::PitchFx::xmlTypeName);
                auto* pf = dynamic_cast<pulse::PitchFx*> (pp5.get());
                using PT = pulse::PitchFx;
                pf->set (PT::pSemis, 7.0f); pf->set (PT::pOctaves, 1.0f);
                auto moved = runFx (*pp5, { { 0.0, on (60) }, { 0.2, off (60) } }, 0.5);
                int movedOff = -1;
                for (auto& [tm, m] : moved) if (m.isNoteOff()) movedOff = m.getNoteNumber();
                pf->set (PT::pSemis, 0.0f); pf->set (PT::pOctaves, 0.0f); pf->set (PT::pLowest, 60.0f); pf->set (PT::pRange, 12.0f);
                auto ranged = noteOns (runFx (*pp5, { { 0.0, on (59) }, { 0.0, on (60) }, { 0.0, on (71) }, { 0.0, on (72) } }, 0.3));
                std::sort (ranged.begin(), ranged.end());
                rep.check (noteOns (moved) == std::vector<int> { 79 } && movedOff == 79 && ranged == std::vector<int> { 60, 71 } && pf->map (59) == -1,
                           "MIDI effect: Pitch (an octave and a fifth up; only C3 to B3 let through)");

                // random: at 0% nothing moves; at 100% every note moves by one of the steps, in the direction asked, and its note-off follows it; a scale keeps it in key
                auto rp5 = make (pulse::RandomFx::xmlTypeName);
                auto* rf = dynamic_cast<pulse::RandomFx*> (rp5.get());
                using RF = pulse::RandomFx;
                std::vector<std::pair<double, MidiMessage>> many;
                for (int i = 0; i < 40; ++i) { many.push_back ({ i * 0.02, on (60) }); many.push_back ({ i * 0.02 + 0.01, off (60) }); }
                rf->set (RF::pChance, 0.0f);
                bool untouched = true;
                for (int n : noteOns (runFx (*rp5, many, 1.0))) untouched = untouched && n == 60;
                rf->set (RF::pChance, 1.0f); rf->set (RF::pChoices, 3.0f); rf->set (RF::pStep, 2.0f); rf->set (RF::pDirection, 0);
                auto rnd = runFx (*rp5, many, 1.0);
                std::set<int> seen;
                bool stepsOk = true, offsFollow = true;
                int lastOn = -1;
                for (auto& [tm, m] : rnd)
                {
                    if (m.isNoteOn()) { lastOn = m.getNoteNumber(); seen.insert (lastOn); stepsOk = stepsOk && (lastOn == 62 || lastOn == 64 || lastOn == 66); }
                    if (m.isNoteOff()) offsFollow = offsFollow && m.getNoteNumber() == lastOn;
                }
                rf->set (RF::pDirection, 2); rf->set (RF::pChoices, 12.0f); rf->set (RF::pStep, 1.0f); rf->set (RF::pRoot, 0); rf->set (RF::pScale, 1.0f);
                bool inKey = true;
                for (int n : noteOns (runFx (*rp5, many, 1.0))) inKey = inKey && Scales::contains (0, 0, n);
                rep.check (untouched && stepsOk && offsFollow && seen.size() == 3 && inKey && rf->reach() == 12,
                           "MIDI effect: Random (never at 0%, always at 100%, steps and direction, note-offs follow, stays in a scale)", String ((int) seen.size()) + " different notes from 3 choices");

                // velocity: fixed, a range, a curve that brings soft playing up, and randomness that stays in bounds
                auto vp5 = make (pulse::VelocityFx::xmlTypeName);
                auto* vf = dynamic_cast<pulse::VelocityFx*> (vp5.get());
                using VF = pulse::VelocityFx;
                auto velsOf = [&] (std::initializer_list<int> in)
                {
                    std::vector<std::pair<double, MidiMessage>> msgs;
                    int i = 0;
                    for (int v : in) msgs.push_back ({ 0.01 * i++, on (60 + i, v) });
                    std::vector<int> out;
                    for (auto& [tm, m] : runFx (*vp5, msgs, 0.5)) if (m.isNoteOn()) out.push_back (m.getVelocity());
                    return out;
                };
                const bool straight = velsOf ({ 1, 64, 127 }) == std::vector<int> { 1, 64, 127 };
                vf->set (VF::pMode, 1); vf->set (VF::pOutHigh, 90.0f);
                const bool fixed = velsOf ({ 10, 64, 127 }) == std::vector<int> { 90, 90, 90 };
                vf->set (VF::pMode, 0); vf->set (VF::pOutLow, 70.0f); vf->set (VF::pOutHigh, 110.0f);
                const bool fitted = velsOf ({ 1, 64, 127 }) == std::vector<int> { 70, 90, 110 };
                vf->set (VF::pOutLow, 1.0f); vf->set (VF::pOutHigh, 127.0f); vf->set (VF::pCurve, 0.5f);
                const int softUp = vf->map (30), hardSame = vf->map (127);
                vf->set (VF::pCurve, 0.0f); vf->set (VF::pRandom, 1.0f);
                bool spread = false, inBounds = true;
                for (int v : velsOf ({ 64, 64, 64, 64, 64, 64, 64, 64, 2, 126 })) { spread = spread || std::abs (v - 64) > 5; inBounds = inBounds && v >= 1 && v <= 127; }
                // its picture: the two ends and the middle of the line are dragged
                vf->set (VF::pRandom, 0.0f);
                MidiFxUI::VelocityView vview (*vf);
                vview.setSize (700, 500);
                { Image img (Image::ARGB, 700, 500, true); Graphics g (img); vview.paint (g); }
                vview.dragHandle (0, 0.0f, -80.0f);
                vview.dragHandle (2, 0.0f, 60.0f);
                vview.dragCurve (-60.0f);
                const bool dragged = vf->get (VF::pOutLow) > 20.0f && vf->get (VF::pOutHigh) < 115.0f && vf->get (VF::pCurve) > 0.2f && vview.getNumHandles() == 3;
                rep.check (straight && fixed && fitted && softUp > 45 && hardSame == 127 && spread && inBounds && dragged,
                           "MIDI effect: Velocity (straight by default, fixed, fitted into a range, a curve, randomness within bounds, its picture)", "30 becomes " + String (softUp) + " with the curve at +50%");

                // the Arpeggiator's drawn pattern: rests wait (the melody is not skipped), levels scale the velocity, the length wraps, swing delays every second step
                {
                    auto ap6 = make (pulse::Arpeggiator::xmlTypeName);
                    auto* arp6 = dynamic_cast<pulse::Arpeggiator*> (ap6.get());
                    using AR = pulse::Arpeggiator;
                    const std::vector<std::pair<double, MidiMessage>> chordHeld { { 0.0, on (60) }, { 0.0, on (64) }, { 0.0, on (67) }, { 0.99, off (60) }, { 0.99, off (64) }, { 0.99, off (67) } };
                    arp6->setPattern ({ 1.0f, 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.5f, 0.0f });
                    auto pat = runFx (*ap6, chordHeld, 1.5);
                    std::vector<int> pNotes, pVels; std::vector<double> pTimes;
                    for (auto& [tm, m] : pat) if (m.isNoteOn()) { pNotes.push_back (m.getNoteNumber()); pVels.push_back (m.getVelocity()); pTimes.push_back (tm); }
                    const bool rests = pNotes == std::vector<int> { 60, 64, 67, 60 } && pVels == std::vector<int> { 100, 50, 100, 50 } && pTimes.size() == 4 && std::abs (pTimes[1] - 0.25) < 0.011 && std::abs (pTimes[3] - 0.75) < 0.011;
                    arp6->setPattern ({ 1.0f, 1.0f, 0.0f }); arp6->set (AR::pSteps, 3.0f);
                    const bool wraps = noteOns (runFx (*ap6, chordHeld, 1.5)).size() == 6;   // 8 steps, steps 3 and 6 are rests
                    arp6->setPattern ({}); arp6->set (AR::pSteps, 16.0f); arp6->set (AR::pSwing, 0.5f);
                    std::vector<double> sw;
                    for (auto& [tm, m] : runFx (*ap6, chordHeld, 1.5)) if (m.isNoteOn()) sw.push_back (tm);
                    const bool swung = sw.size() == 8 && std::abs (sw[1] - (0.125 + 0.03125)) < 0.011 && std::abs (sw[2] - 0.25) < 0.011;
                    // the picture: bars are set by dragging, and a bar pulled to the bottom is a rest
                    arp6->set (AR::pSwing, 0.0f);
                    auto barsView = MidiFxUI::arpPattern (*arp6);
                    barsView->setSize (800, 300);
                    barsView->setBar (2, 0.4f);
                    barsView->setBar (5, 0.02f);
                    const auto drawn = arp6->getPattern();
                    const bool bars = std::abs (drawn[2] - 0.4f) < 0.01f && drawn[5] == 0.0f && drawn[0] == 1.0f && drawn.size() == 16;
                    // the Note Echo's drawn repeats: each has its own level, and one can be left out
                    auto ep6 = make (pulse::NoteEcho::xmlTypeName);
                    auto* echo6 = dynamic_cast<pulse::NoteEcho*> (ep6.get());
                    const bool byFeedback = ! echo6->hasDrawnLevels() && std::abs (echo6->getLevels()[1] - 0.36f) < 0.01f;
                    echo6->setLevels ({ 1.0f, 0.0f, 0.5f });
                    std::vector<int> eVels; std::vector<double> eTimes;
                    for (auto& [tm, m] : runFx (*ep6, { { 0.0, on (72) }, { 0.1, off (72) } }, 1.5)) if (m.isNoteOn()) { eVels.push_back (m.getVelocity()); eTimes.push_back (tm); }
                    const bool echoes = eVels == std::vector<int> { 100, 100, 50 } && eTimes.size() == 3 && std::abs (eTimes[2] - 0.75) < 0.011;
                    rep.check (rests && wraps && swung && bars && byFeedback && echoes,
                               "MIDI effects: drawn patterns (Arpeggiator steps with rests, length and swing; Note Echo repeats)",
                               String (rests ? "" : "rests wrong; ") + (wraps ? "" : "length wrong; ") + (swung ? "" : "swing wrong; ") + (bars ? "" : "bars wrong; ") + (echoes ? "" : "echo levels wrong; ")
                               + String ((int) pNotes.size()) + " notes from 8 steps with every second one a rest");
                }
            }
        }

        // --- v0.99.21: the chord track --------------------------------------------------------------------
        {
            rep.step ("chords");
            Chords::clear (*edit);
            // chords never overlap: a new one cuts away what it covers of the others (here it splits one in two)
            Chords::add (*edit, 0.0, 8.0, 0, Chords::major);
            Chords::add (*edit, 2.0, 2.0, 9, Chords::minor);
            auto list = Chords::all (*edit);
            const bool cut = list.size() == 3 && list[0].name() == "C" && std::abs (list[0].length - 2.0) < 1e-6 && list[1].name() == "Am" && std::abs (list[1].start - 2.0) < 1e-6
                             && list[2].name() == "C" && std::abs (list[2].start - 4.0) < 1e-6 && std::abs (list[2].length - 4.0) < 1e-6;
            const auto atThree = Chords::at (*edit, 3.0), atTen = Chords::at (*edit, 10.0);
            const bool found = atThree.has_value() && atThree->name() == "Am" && ! atTen.has_value();
            // names and notes
            Chords::Chord g7; g7.root = 7; g7.kind = Chords::dominant7;
            const bool named = g7.name() == "G7" && g7.contains (65) && ! g7.contains (64) && g7.snap (64) == 65 && g7.snap (63) == 62 && g7.snap (66) == 65 && Chords::voicing (g7, 4, true) == std::vector<int> { 55, 67, 71, 74, 77 };
            // progressions in any key
            Chords::clear (*edit);
            const int nPop = Chords::addProgression (*edit, 0, 0, false, 0.0, 4.0);
            String pop;
            for (auto& c : Chords::all (*edit)) pop << c.name() << " ";
            Chords::clear (*edit);
            Chords::addProgression (*edit, 6, 9, true, 0.0, 4.0);
            String epic;
            for (auto& c : Chords::all (*edit)) epic << c.name() << " ";
            Chords::clear (*edit);
            Chords::addProgression (*edit, 4, 2, false, 0.0, 4.0);
            String jazz;
            for (auto& c : Chords::all (*edit)) jazz << c.name() << " ";
            const bool progs = nPop == 4 && pop.trim() == "C G Am F" && epic.trim() == "Am F C G" && jazz.trim() == "Em7 A7 Dmaj7 Dmaj7";
            // written out as notes, and notes fitted to the chords
            Chords::clear (*edit);
            Chords::addProgression (*edit, 0, 0, false, 0.0, 4.0);
            auto ct = ClipImport::createContentTrack (*edit, "Chord test", false);
            auto mc = te::insertMIDIClip (*ct, "chords", { EngineHelpers::beatToTime (*edit, 0.0), EngineHelpers::beatToTime (*edit, 16.0) });
            const int written = Chords::writeToClip (*mc);
            bool rightNotes = written == 16 && mc->getSequence().getNumNotes() == 16;
            for (auto* n : mc->getSequence().getNotes())
                if (auto c = Chords::at (*edit, n->getStartBeat().inBeats())) rightNotes = rightNotes && c->contains (n->getNoteNumber()) && std::abs (n->getLengthBeats().inBeats() - 4.0) < 1e-6;
            mc->getSequence().clear (nullptr);
            mc->getSequence().addNote (61, te::BeatPosition::fromBeats (0.5), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);    // C# over C  -> C
            mc->getSequence().addNote (61, te::BeatPosition::fromBeats (4.5), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);    // C# over G  -> D (one up; B would be two down)
            mc->getSequence().addNote (64, te::BeatPosition::fromBeats (12.5), te::BeatDuration::fromBeats (0.5), 100, 0, nullptr);   // E over F   -> F
            const int movedNotes = Chords::fitNotes (*mc);
            std::vector<int> fitted;
            for (auto* n : mc->getSequence().getNotes()) fitted.push_back (n->getNoteNumber());
            const bool fits = movedNotes == 3 && fitted == std::vector<int> { 60, 62, 65 };
            // undo takes a chord away again
            const int before = (int) Chords::all (*edit).size();
            edit->getUndoManager().beginNewTransaction ("add chord");
            Chords::add (*edit, 32.0, 4.0, 5, Chords::minor);
            const int during = (int) Chords::all (*edit).size();
            edit->getUndoManager().undo();
            const bool undone = during == before + 1 && (int) Chords::all (*edit).size() == before;
            // the Scale effect follows the chord track: a C# played over the G bar comes out as a note of G
            auto sfx = edit->getPluginCache().createNewPlugin (pulse::ScaleFx::xmlTypeName, {});
            auto* scaleFx = dynamic_cast<pulse::ScaleFx*> (sfx.get());
            scaleFx->set (pulse::ScaleFx::pScale, 11.0f);   // chromatic: the scale itself changes nothing
            scaleFx->set (pulse::ScaleFx::pFollow, 1.0f);
            const double beatSec = 60.0 / edit->tempoSequence.getTempo (0)->getBpm();
            auto followAt = [&] (double beat, int pitch)
            {
                sfx->initialise ({ te::TimePosition(), 48000.0, 480 });
                AudioBuffer<float> audio (2, 480);
                te::MidiMessageArray midi;
                midi.addMidiMessage (MidiMessage::noteOn (1, pitch, (uint8) 100), 0.0, te::createUniqueMPESourceID());
                const double t0 = beat * beatSec;
                te::PluginRenderContext fc (&audio, 0, 480, &midi, 0.0, { te::TimePosition::fromSeconds (t0), te::TimePosition::fromSeconds (t0 + 0.01) }, true, false, true, false);
                sfx->applyToBuffer (fc);
                int out = -1;
                for (auto& m : midi) if (m.isNoteOn()) out = m.getNoteNumber();
                sfx->deinitialise();
                return out;
            };
            const int overC = followAt (1.0, 61), overG = followAt (5.0, 61), pastTheEnd = followAt (40.0, 61);
            scaleFx->set (pulse::ScaleFx::pFollow, 0.0f);
            const int notFollowing = followAt (5.0, 61);
            const bool follows = overC == 60 && overG == 62 && pastTheEnd == 61 && notFollowing == 61;
            // the strip in the arrangement: drawing, and a chord added by a double-click
            EditViewState cevs (*edit, sm);
            ChordStrip stripView (cevs);
            stripView.setSize (1200, 22);
            { Image img (Image::ARGB, 1200, 22, true); Graphics g (img); stripView.paint (g); }
            const int nBefore = (int) Chords::all (*edit).size();
            const int addedIndex = stripView.addAt (cevs.beatToX (20.5));
            const auto added = Chords::at (*edit, 20.5);
            const bool stripOk = addedIndex >= 0 && (int) Chords::all (*edit).size() == nBefore + 1 && added.has_value() && added->name() == "F" && std::abs (added->start - 20.0) < 1e-6;
            rep.check (cut && found && named && progs && rightNotes && fits && undone && follows && stripOk,
                       "Chord track (chords never overlap, names and notes, progressions in any key, written as notes, notes fitted, undo, the Scale effect follows, the strip)",
                       String (cut ? "" : "cutting wrong; ") + (found ? "" : "lookup wrong; ") + (named ? "" : "names wrong; ") + (progs ? "" : "progressions wrong (" + pop + "| " + epic + "| " + jazz + "); ")
                       + (rightNotes ? "" : "written notes wrong; ") + (fits ? "" : "fitting wrong; ") + (undone ? "" : "undo wrong; ")
                       + (follows ? "" : "follow wrong (" + String (overC) + " " + String (overG) + " " + String (pastTheEnd) + " " + String (notFollowing) + "); ") + (stripOk ? "" : "strip wrong; ")
                       + "C major pop: " + pop.trim() + "; A minor: " + epic.trim() + "; ii V I in D: " + jazz.trim());
            sfx = nullptr;
            edit->deleteTrack (ct);
            Chords::clear (*edit);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.22: the step grid ------------------------------------------------------------------------
        {
            rep.step ("steps");
            auto st = ClipImport::createContentTrack (*edit, "Step test", false);
            auto rackPlugin = DrumRack::create (*edit, true);   // with the synthesised kit on its pads
            st->pluginList.insertPlugin (rackPlugin, 0, nullptr);
            auto mc = te::insertMIDIClip (*st, "beat", { EngineHelpers::beatToTime (*edit, 0.0), EngineHelpers::beatToTime (*edit, 8.0) });
            EditViewState sevs (*edit, sm);
            StepGrid grid (sevs);
            grid.setSize (1100, 400);
            grid.setClip (mc.get());
            const auto rws = grid.rows();
            const int kick = rws.empty() ? 36 : rws.back().note;   // the lowest pad
            // 8 beats of sixteenths = 32 steps; a click puts a hit in, another takes it out
            const bool sized = grid.numSteps() == 32 && rws.size() >= 4 && rws.front().note > rws.back().note && rws.back().name.isNotEmpty();
            grid.toggle (kick, 4);
            const auto* hit = grid.noteAt (kick, 4);
            const bool put = hit != nullptr && std::abs (hit->getStartBeat().inBeats() - 1.0) < 1e-6 && hit->getVelocity() == 100 && mc->getSequence().getNumNotes() == 1;
            grid.toggle (kick, 4);
            const bool taken = grid.noteAt (kick, 4) == nullptr && mc->getSequence().getNumNotes() == 0;
            // rows: on every beat, on 2 and 4, shifted, cleared; and undo takes a fill away in one go
            grid.fillRow (kick, 4);
            const bool onBeats = mc->getSequence().getNumNotes() == 8 && grid.noteAt (kick, 0) != nullptr && grid.noteAt (kick, 4) != nullptr && grid.noteAt (kick, 2) == nullptr;
            grid.shiftRow (kick, 1);
            const bool shifted = grid.noteAt (kick, 0) == nullptr && grid.noteAt (kick, 1) != nullptr && mc->getSequence().getNumNotes() == 8;
            grid.shiftRow (kick, -2);
            const bool wrapped = grid.noteAt (kick, 31) != nullptr && mc->getSequence().getNumNotes() == 8;
            const int snare = rws.size() >= 2 ? rws[rws.size() - 2].note : 38;
            grid.fillRow (snare, 8, 4);
            const bool backbeat = grid.noteAt (snare, 4) != nullptr && grid.noteAt (snare, 12) != nullptr && grid.noteAt (snare, 0) == nullptr && grid.noteAt (snare, 8) == nullptr;
            edit->getUndoManager().undo();
            const bool undone = grid.noteAt (snare, 4) == nullptr && mc->getSequence().getNumNotes() == 8;
            grid.clearRow (kick);
            const bool cleared = mc->getSequence().getNumNotes() == 0;
            // a triplet grid has its own number of steps, and the picture draws
            grid.setStep (1.0 / 3.0);
            const bool triplets = grid.numSteps() == 24;
            grid.setStep (0.25);
            grid.fillRow (kick, 4);
            grid.fillRow (snare, 8, 4);
            if (rws.size() >= 3) { grid.fillRow (rws[rws.size() - 3].note, 2, 1); grid.setHit (rws[rws.size() - 3].note, 3, 50); grid.setHit (rws[rws.size() - 3].note, 7, 127); }
            Image img (Image::ARGB, 1100, 400, true);
            { PulseLookAndFeel laf; LookAndFeel::setDefaultLookAndFeel (&laf); Graphics g (img); grid.paint (g); LookAndFeel::setDefaultLookAndFeel (nullptr); }
            { auto f = dir.getChildFile ("steps.png"); f.deleteFile(); if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); } }
            // inside the piano roll: the Steps button swaps it in, with the same clip
            PianoRoll roll (sevs);
            roll.setSize (1100, 500);
            roll.setClip (mc.get());
            roll.showSteps (true);
            const bool swapped = roll.stepGrid().isVisible() && roll.stepGrid().numSteps() == 32 && roll.stepGrid().noteAt (kick, 4) != nullptr;
            roll.showSteps (false);
            roll.setClip (nullptr);
            rep.check (sized && put && taken && onBeats && shifted && wrapped && backbeat && undone && cleared && triplets && swapped && ! roll.stepGrid().isVisible(),
                       "Step grid for drum clips (a row per pad, hits in and out, fill / shift / clear a row, undo, triplet steps, inside the piano roll)",
                       String ((int) rws.size()) + " rows, " + String (grid.numSteps()) + " steps"
                       + (sized ? "" : "; size wrong") + (put && taken ? "" : "; toggling wrong") + (onBeats && shifted && wrapped && backbeat ? "" : "; row tools wrong") + (undone ? "" : "; undo wrong") + (swapped ? "" : "; not shown in the piano roll"));
            // --- v0.99.54: pattern rows: a length of its own, repeats inside a step, a chance of playing ---------
            {
                auto mc2 = te::insertMIDIClip (*st, "poly", { EngineHelpers::beatToTime (*edit, 8.0), EngineHelpers::beatToTime (*edit, 24.0) });
                grid.setClip (mc2.get());
                auto& seq2 = mc2->getSequence();
                auto count = [&] (int pitch) { int c = 0; for (auto* nn : seq2.getNotes()) if (nn->getNoteNumber() == pitch) ++c; return c; };
                auto starts = [&] (int pitch) { String s; for (int k = 0; k < grid.numSteps(); ++k) s << (grid.noteAt (pitch, k) != nullptr ? "x" : "."); return s; };
                const int hat = rws.size() >= 3 ? rws[rws.size() - 3].note : 42;
                grid.fillRow (kick, 4);
                // seven steps under sixteen: hits on its steps 1 and 4 come round ten and nine times in 64 steps
                grid.setRowLength (snare, 7);
                grid.setHit (snare, 0, 100);
                grid.setHit (snare, 3, 90);
                const bool poly = grid.numSteps() == 64 && grid.rowLength (snare) == 7 && count (snare) == 19 && count (kick) == 16 && grid.noteAt (snare, 7) != nullptr
                                  && grid.noteAt (snare, 10) != nullptr && grid.noteAt (snare, 10)->getVelocity() == 90 && grid.noteAt (snare, 8) == nullptr && ! grid.isPatternRow (kick);
                // a click on a repeat edits the row's own step: step 9 is its step 2, heard at 2, 9, 16...
                grid.setHit (snare, 8, 100);
                const bool repeatEdits = grid.noteAt (snare, 1) != nullptr && grid.noteAt (snare, 15) != nullptr && count (snare) == 28;
                // three repeats inside the kick's second beat: two notes more, each a third of a step long, and undo takes them away
                grid.setRatchet (kick, 4, 3);
                int inStep = 0; double shortest = 1.0;
                for (auto* nn : seq2.getNotes())
                    if (nn->getNoteNumber() == kick && nn->getStartBeat().inBeats() >= 1.0 - 1e-6 && nn->getStartBeat().inBeats() < 1.25 - 1e-6) { ++inStep; shortest = jmin (shortest, nn->getLengthBeats().inBeats()); }
                const bool ratchet = count (kick) == 18 && inStep == 3 && shortest < 0.25 / 3.0 && shortest > 0.05 && grid.ratchetAt (kick, 4) == 3 && grid.isPatternRow (kick);
                edit->getUndoManager().undo();
                const bool ratchetUndone = count (kick) == 16 && grid.ratchetAt (kick, 4) == 1;
                edit->getUndoManager().redo();
                // chance: a row of sixteenths at one in two plays about half its 64 steps, differently in each bar; the
                // dice thrown again give other steps; a chance of none plays nothing, and the pattern is still there
                grid.fillRow (hat, 1);
                grid.setRowLength (hat, 16);
                for (int k = 0; k < 16; ++k) grid.setChance (hat, k, 50);
                const auto first = starts (hat);
                const int played = count (hat);
                const bool half = played > 16 && played < 48 && first.substring (0, 16) != first.substring (16, 32) && grid.chanceAt (hat, 5) == 50 && grid.chanceAt (hat, 21) == 50;
                const auto same = starts (hat);
                grid.rollAgain();
                const bool rolled = starts (hat) != first && same == first && count (hat) > 16 && count (hat) < 48;
                for (int k = 0; k < 16; ++k) grid.setChance (hat, k, 0);
                const bool never = count (hat) == 0 && grid.velocityAt (hat, 3) > 0;
                for (int k = 0; k < 16; ++k) grid.setChance (hat, k, 100);
                const bool always = count (hat) == 64;
                // the pattern is kept with the clip (it is there for a grid that opens the clip afresh), a row is unfolded
                // when it is made as long as the clip again, and "plain notes" leaves the notes and forgets the rest
                StepGrid other (sevs);
                other.setSize (1100, 400);
                other.setClip (mc2.get());
                const bool kept = other.rowLength (snare) == 7 && other.ratchetAt (kick, 4) == 3 && mc2->state.getChildWithName ("STEPROWS").getNumChildren() == 3;
                other.setClip (nullptr);
                Image img2 (Image::ARGB, 1100, 400, true);
                grid.setChance (hat, 2, 50); grid.setChance (hat, 6, 25); grid.setRatchet (hat, 12, 4);
                { PulseLookAndFeel laf; LookAndFeel::setDefaultLookAndFeel (&laf); Graphics g (img2); grid.paint (g); LookAndFeel::setDefaultLookAndFeel (nullptr); }
                { auto f = dir.getChildFile ("steps-patterns.png"); f.deleteFile(); if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img2, *os); } }
                const auto before = starts (snare);
                grid.setRowLength (snare, 0);
                const bool unfolded = grid.rowLength (snare) == 64 && starts (snare) == before;
                grid.makePlain (snare);
                const bool plain = ! grid.isPatternRow (snare) && starts (snare) == before;
                // the help line says what the gestures are
                const bool helped = HelpLine::textFor (&grid).contains ("chance");
                rep.check (poly && repeatEdits && ratchet && ratchetUndone && half && rolled && never && always && kept && unfolded && plain && helped,
                           "Step grid pattern rows (a row with a length of its own against the others, repeats inside a step, a chance of playing with dice that can be thrown again, undo, kept with the clip, back to plain notes)",
                           String (played) + " of 64 hats at one in two"
                           + (poly ? "" : "; a row of seven is not repeated right (" + String (count (snare)) + " notes)") + (repeatEdits ? "" : "; a click on a repeat does not edit the row's step")
                           + (ratchet ? "" : "; repeats wrong (" + String (inStep) + " in the step)") + (ratchetUndone ? "" : "; undo of repeats wrong") + (half ? "" : "; chance wrong")
                           + (rolled ? "" : "; throwing the dice again changes nothing") + (never && always ? "" : "; chance of none or of all wrong") + (kept ? "" : "; not kept with the clip")
                           + (unfolded && plain ? "" : "; unfolding or plain notes changed what is heard") + (helped ? "" : "; no help"));
            }
            grid.setClip (nullptr);
            edit->deleteTrack (st);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.60: the drum machine ---------------------------------------------------------------------
        {
            rep.step ("drum machine");
            using DM = DrumMachine;
            auto dt = ClipImport::createContentTrack (*edit, "Machine test", false);
            auto rackPlugin = DrumRack::create (*edit, true);   // with the synthesised kit on its pads
            dt->pluginList.insertPlugin (rackPlugin, 0, nullptr);
            auto beat = [&] (double b) { return EngineHelpers::beatToTime (*edit, b); };
            auto mc = te::insertMIDIClip (*dt, "beat", { beat (0.0), beat (16.0) });   // four bars
            EditViewState devs (*edit, sm);
            DM dm (devs);
            dm.setSize (1100, 420);
            dm.setClip (mc.get());
            using Starts = std::vector<double>;
            auto startsIn = [] (te::MidiClip& c, int note)
            {
                Starts v;
                for (auto* n : c.getSequence().getNotes()) if (n->getNoteNumber() == note) v.push_back (std::round (n->getStartBeat().inBeats() * 1.0e6) / 1.0e6);
                std::sort (v.begin(), v.end());
                return v;
            };
            auto velIn = [] (te::MidiClip& c, int note, double at)
            {
                for (auto* n : c.getSequence().getNotes()) if (n->getNoteNumber() == note && std::abs (n->getStartBeat().inBeats() - at) < 1.0e-6) return n->getVelocity();
                return 0;
            };
            auto starts = [&] (int note) { return startsIn (*mc, note); };
            // an empty clip: nothing written yet
            const bool fresh = ! dm.isMachine() && dm.numBars() == 4 && dm.chain() == "A" && dm.rows().size() >= 4 && mc->getSequence().getNumNotes() == 0;
            // a hit on the fifth step of the kick plays in every bar (the chain is "A", going round four bars)
            dm.toggle (36, 4);
            const bool everyBar = dm.isMachine() && starts (36) == Starts { 1.0, 5.0, 9.0, 13.0 } && velIn (*mc, 36, 1.0) == 96;
            dm.toggleAccent (36, 4);
            const bool accented = dm.stepAt (36, 4) == DM::accent && velIn (*mc, 36, 5.0) == 127 && mc->getSequence().getNumNotes() == 4;
            edit->getUndoManager().undo();
            const bool accentUndone = dm.stepAt (36, 4) == DM::hit && velIn (*mc, 36, 5.0) == 96 && mc->getSequence().getNumNotes() == 4;
            // pattern B has a snare on its first step; the chain A A B puts it in the third bar, and the fourth bar is A again
            dm.selectPattern (1);
            dm.toggle (38, 0);
            const bool notYet = starts (38).empty();
            dm.setChain ("aab?");
            const bool chained = dm.chain() == "AAB" && starts (38) == Starts { 8.0 } && starts (36) == Starts { 1.0, 5.0, 13.0 };
            // swing pushes every second sixteenth later and leaves the others where they are
            dm.selectPattern (0);
            dm.toggle (42, 1);
            dm.setSwing (66);
            const auto hats = starts (42);
            const bool swung = hats.size() == 3 && std::abs (hats[0] - (0.25 + 0.66 * 0.125)) < 1.0e-5 && starts (36).front() == 1.0;
            edit->getUndoManager().undo();
            const bool swingUndone = dm.swing() == 0 && std::abs (starts (42).front() - 0.25) < 1.0e-6;
            dm.setLevel (80);
            const bool levelled = velIn (*mc, 42, 0.25) == 80 && dm.level() == 80;
            // copying and clearing patterns
            dm.copyPattern (0, 2);
            const bool copied = dm.model().at (2, 36, 4) == DM::hit && dm.model().at (2, 42, 1) == DM::hit;
            dm.clearPattern (2);
            const bool wiped = dm.model().hits (2) == 0 && dm.model().hits (0) == 2;
            // a ready-made pattern, written only for drums that have a pad
            dm.selectPattern (3);
            dm.applyFactory (0);
            const auto fm = dm.model();
            const bool readyMade = fm.at (3, 36, 0) == DM::accent && fm.at (3, 36, 4) == DM::accent && fm.at (3, 42, 2) == DM::hit && fm.at (3, 39, 4) == DM::accent && DM::factory().size() >= 8;
            bool factoryShapes = true;
            for (auto& f : DM::factory()) for (auto& r : f.rows) factoryShapes = factoryShapes && String (r.second).length() == DM::numSteps;
            // rows: fill, shift round the end, clear
            dm.selectPattern (2);
            dm.fillRow (37, 4);
            dm.shiftRow (37, -1);
            const bool rowTools = dm.stepAt (37, 15) == DM::hit && dm.stepAt (37, 3) == DM::hit && dm.stepAt (37, 0) == DM::off && dm.model().hits (2) == 4;
            dm.clearPattern (2);
            // kept with the clip: another machine reads the same thing
            DM other (devs);
            other.setClip (mc.get());
            const bool kept = other.chain() == "AAB" && other.level() == 80 && other.model().at (1, 38, 0) == DM::hit && mc->state.getChildWithName ("DRUMMACHINE").isValid();
            other.setClip (nullptr);
            // the clip made as long as the chain: three bars, and undo gives the fourth back with its notes
            dm.fitClipToChain();
            const bool fitted = dm.numBars() == 3 && starts (36) == Starts { 1.0, 5.0 } && std::abs (EngineHelpers::timeToBeat (*edit, mc->getPosition().getEnd()) - 12.0) < 1.0e-6;
            edit->getUndoManager().undo();
            const bool fitUndone = dm.numBars() == 4 && starts (36) == Starts { 1.0, 5.0, 13.0 };
            // the pattern rows of a step grid give way: the machine writes every note of the clip
            {
                StepGrid sg (devs);
                sg.setSize (900, 300);
                sg.setClip (mc.get());
                sg.setRatchet (36, 4, 3);
                sg.setClip (nullptr);
            }
            const bool hadRows = mc->state.getChildWithName ("STEPROWS").isValid();
            dm.selectPattern (0);
            dm.toggle (46, 6);
            const bool rowsGone = hadRows && ! mc->state.getChildWithName ("STEPROWS").isValid() && starts (36) == Starts { 1.0, 5.0, 13.0 };

            // a clip of plain notes is read, not written: two different bars become A and B, hard notes accents
            auto mc2 = te::insertMIDIClip (*dt, "plain", { beat (32.0), beat (40.0) });
            for (int b = 0; b < 8; ++b) mc2->getSequence().addNote (36, te::BeatPosition::fromBeats ((double) b), te::BeatDuration::fromBeats (0.2), 90, 0, nullptr);
            mc2->getSequence().addNote (38, te::BeatPosition::fromBeats (5.0), te::BeatDuration::fromBeats (0.2), 127, 0, nullptr);
            dm.setClip (mc2.get());
            const auto cm = dm.model();
            const bool read = ! cm.stored && ! dm.isMachine() && cm.chain == "AB" && cm.at (0, 36, 0) == DM::hit && cm.at (0, 36, 4) == DM::hit && cm.at (0, 38, 4) == DM::off
                              && cm.at (1, 38, 4) == DM::accent && cm.level == 90 && mc2->getSequence().getNumNotes() == 9;
            dm.selectPattern (0);
            dm.toggle (42, 2);
            const bool taken = dm.isMachine() && mc2->getSequence().getNumNotes() == 10 && startsIn (*mc2, 42) == Starts { 0.5 } && velIn (*mc2, 38, 5.0) == 127 && velIn (*mc2, 36, 3.0) == 90;
            edit->getUndoManager().undo();
            const bool takenUndone = ! dm.isMachine() && mc2->getSequence().getNumNotes() == 9;

            // the picture, at its usual size and at a very small and a very large one
            dm.setClip (mc.get());
            dm.selectPattern (0);
            dm.applyFactory (1);
            dm.toggleAccent (42, 3);
            dm.setChain ("AABAAABD");
            dm.setSwing (33);
            Image img (Image::ARGB, 1100, 420, true);
            {
                PulseLookAndFeel laf; LookAndFeel::setDefaultLookAndFeel (&laf);
                { Graphics g (img); dm.paint (g); }
                for (auto [w, h] : { std::pair<int, int> { 620, 180 }, { 2400, 1300 } }) { dm.setSize (w, h); Image im (Image::ARGB, w, h, true); Graphics g (im); dm.paint (g); }
                dm.setSize (1100, 420);
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            { auto f = dir.getChildFile ("drum-machine.png"); f.deleteFile(); if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); } }
            const bool helped = HelpLine::textFor (&dm).isNotEmpty() && DM::helpText().contains ("accent");

            // inside the piano roll: Machine swaps it in with the same clip, and Steps swaps it out again
            PianoRoll roll (devs);
            roll.setSize (1100, 500);
            roll.setClip (mc.get());
            roll.showMachine (true);
            const bool swappedIn = roll.drumMachine().isVisible() && ! roll.stepGrid().isVisible() && roll.drumMachine().chain() == "AABAAABD";
            roll.showSteps (true);
            const bool swappedOut = ! roll.drumMachine().isVisible() && roll.stepGrid().isVisible();
            roll.showSteps (false);
            roll.setClip (nullptr);
            // the Machine button of the Drum Rack: the clip under the playhead, or a new one of four bars on a track with none
            edit->getTransport().setPosition (beat (2.0));
            const bool found = DM::clipFor (*dt, edit->getUndoManager()) == mc.get();
            auto et = ClipImport::createContentTrack (*edit, "Machine empty", false);
            auto* made = DM::clipFor (*et, edit->getUndoManager());
            const bool madeOne = made != nullptr && et->getClips().size() == 1 && std::abs (EngineHelpers::timeToBeat (*edit, made->getPosition().getEnd()) - EngineHelpers::timeToBeat (*edit, made->getPosition().getStart()) - 16.0) < 1.0e-6;
            edit->getTransport().setPosition (te::TimePosition());

            const bool model = fresh && everyBar && accented && accentUndone && notYet && chained && swung && swingUndone && levelled;
            const bool tools = copied && wiped && readyMade && factoryShapes && rowTools && kept && fitted && fitUndone && rowsGone;
            const bool reading = read && taken && takenUndone;
            const bool views = helped && swappedIn && swappedOut && found && madeOne;
            rep.check (model && tools && reading && views,
                       "Drum machine (sixteen steps a pad, accents, four patterns and a chain that goes round the clip, swing, hit level, ready-made patterns, undo, kept with the clip, a plain clip read as patterns, in the piano roll and from the Drum Rack)",
                       String (DM::factory().size()) + " ready-made patterns"
                       + (fresh ? "" : "; an empty clip is not empty") + (everyBar ? "" : "; a hit is not in every bar (" + String (starts (36).size()) + ")") + (accented && accentUndone ? "" : "; accent or its undo wrong")
                       + (notYet && chained ? "" : "; chain wrong") + (swung && swingUndone ? "" : "; swing wrong") + (levelled ? "" : "; hit level wrong")
                       + (copied && wiped ? "" : "; copy or clear wrong") + (readyMade && factoryShapes ? "" : "; ready-made pattern wrong") + (rowTools ? "" : "; row tools wrong")
                       + (kept ? "" : "; not kept with the clip") + (fitted && fitUndone ? "" : "; fitting the clip to the chain wrong") + (rowsGone ? "" : "; step grid rows not given up")
                       + (read ? "" : "; a plain clip is read wrong (chain " + cm.chain + ", level " + String (cm.level) + ")") + (taken && takenUndone ? "" : "; taking a plain clip over wrong")
                       + (helped ? "" : "; no help") + (swappedIn && swappedOut ? "" : "; piano roll swap wrong") + (found && madeOne ? "" : "; clip for the Machine button wrong"));
            dm.setClip (nullptr);
            edit->deleteTrack (et);
            edit->deleteTrack (dt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.23: crop and consolidate ------------------------------------------------------------------
        {
            rep.step ("crop and consolidate");
            auto ct2 = ClipImport::createContentTrack (*edit, "Join test", false);
            auto beat = [&] (double b) { return EngineHelpers::beatToTime (*edit, b); };
            // two MIDI clips become one, with every note where it sounded before; undo brings the two back
            auto m1 = te::insertMIDIClip (*ct2, "a", { beat (0.0), beat (4.0) });
            auto m2 = te::insertMIDIClip (*ct2, "b", { beat (8.0), beat (12.0) });
            m1->getSequence().addNote (60, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            m2->getSequence().addNote (67, te::BeatPosition::fromBeats (2.0), te::BeatDuration::fromBeats (1.0), 90, 0, nullptr);
            te::Clip::Array both;
            both.add (m1.get()); both.add (m2.get());
            const bool can = EditingOps::canConsolidate (both);
            edit->getUndoManager().beginNewTransaction ("consolidate");
            auto* joined = dynamic_cast<te::MidiClip*> (EditingOps::consolidate (both, &sm));
            bool midiOk = can && joined != nullptr && ct2->getClips().size() == 1;
            if (joined != nullptr)
            {
                std::vector<std::pair<int, double>> notes;
                const double clipBeat = EngineHelpers::timeToBeat (*edit, joined->getPosition().getStart());
                for (auto* n : joined->getSequence().getNotes()) notes.push_back ({ n->getNoteNumber(), clipBeat + n->getStartBeat().inBeats() - joined->getContentBeatAtTime (joined->getPosition().getStart()).inBeats() });
                std::sort (notes.begin(), notes.end());
                midiOk = midiOk && notes.size() == 2 && notes[0].first == 60 && std::abs (notes[0].second - 1.0) < 0.01 && notes[1].first == 67 && std::abs (notes[1].second - 10.0) < 0.01
                         && std::abs (EngineHelpers::timeToBeat (*edit, joined->getPosition().getEnd()) - 12.0) < 0.01;
            }
            edit->getUndoManager().undo();
            const bool midiUndone = ct2->getClips().size() == 2;
            for (int i = ct2->getClips().size(); --i >= 0;) ct2->getClips()[i]->removeFromParent();

            // two audio clips become one file: the first sound, the gap between them as silence, the second sound
            auto writeTone = [&] (const String& name, double hz)
            {
                auto f = dir.getChildFile (name);
                f.deleteFile();
                AudioBuffer<float> b (2, 24000);
                for (int i = 0; i < 24000; ++i) { const float v = 0.5f * (float) std::sin (MathConstants<double>::twoPi * hz * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
                WavAudioFormat wav;
                if (auto os = f.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (b, 0, 24000);
                return f;
            };
            te::Clip::Ptr a1 = ClipImport::importFile (*ct2, *edit, writeTone ("join_a.wav", 300.0), te::TimePosition::fromSeconds (0.0));
            te::Clip::Ptr a2 = ClipImport::importFile (*ct2, *edit, writeTone ("join_b.wav", 900.0), te::TimePosition::fromSeconds (1.0));
            // crop first: the second clip trimmed to its middle keeps its sound in place
            te::Clip::Array justB;
            justB.add (a2.get());
            const int cropped = a2 != nullptr ? EditingOps::cropToRange (justB, { te::TimePosition::fromSeconds (1.1), te::TimePosition::fromSeconds (1.4) }) : 0;
            const bool cropOk = cropped == 1 && a2 != nullptr && std::abs (a2->getPosition().getStart().inSeconds() - 1.1) < 0.001 && std::abs (a2->getPosition().getEnd().inSeconds() - 1.4) < 0.001
                                && std::abs (a2->getPosition().getOffset().inSeconds() - 0.1) < 0.001;
            const int notReached = a1 != nullptr ? EditingOps::cropToRange (justB, { te::TimePosition::fromSeconds (5.0), te::TimePosition::fromSeconds (6.0) }) : 1;
            te::Clip::Array audioBoth;
            if (a1 != nullptr) audioBoth.add (a1.get());
            if (a2 != nullptr) audioBoth.add (a2.get());
            auto* joinedAudio = dynamic_cast<te::WaveAudioClip*> (EditingOps::consolidate (audioBoth, &sm));
            bool audioOk = joinedAudio != nullptr && ct2->getClips().size() == 1;
            String levels;
            if (joinedAudio != nullptr)
            {
                audioOk = audioOk && std::abs (joinedAudio->getPosition().getStart().inSeconds()) < 0.001 && std::abs (joinedAudio->getPosition().getEnd().inSeconds() - 1.4) < 0.002;
                const auto file = joinedAudio->getOriginalFile();
                if (std::unique_ptr<AudioFormatReader> rd { edit->engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file) })
                {
                    AudioBuffer<float> all (2, (int) rd->lengthInSamples);
                    rd->read (&all, 0, (int) rd->lengthInSamples, 0, true, true);
                    auto level = [&] (double from, double len) { const int a = (int) (from * rd->sampleRate), n = (int) (len * rd->sampleRate); return a + n <= all.getNumSamples() ? Decibels::gainToDecibels (all.getRMSLevel (0, a, n), -120.0f) : -120.0f; };
                    const float first = level (0.1, 0.3), gap = level (0.6, 0.4), second = level (1.15, 0.2);
                    levels = String (first, 1) + " / " + String (gap, 0) + " / " + String (second, 1) + " dB";
                    audioOk = audioOk && first > -12.0f && gap < -80.0f && second > -12.0f && std::abs (rd->lengthInSamples / rd->sampleRate - 1.4) < 0.01;
                }
                else audioOk = false;
            }
            // a MIDI clip and an audio clip cannot be joined
            auto m3 = te::insertMIDIClip (*ct2, "c", { beat (20.0), beat (24.0) });
            te::Clip::Array mixed;
            for (auto c : ct2->getClips()) mixed.add (c);
            const bool mixedRefused = ! EditingOps::canConsolidate (mixed) && EditingOps::consolidate (mixed) == nullptr;
            rep.check (midiOk && midiUndone && cropOk && notReached == 0 && audioOk && mixedRefused,
                       "Crop and consolidate (MIDI clips joined with their notes in place, undo; audio clips joined into one file with the gap as silence; crop keeps the sound in place)",
                       String (midiOk ? "" : "MIDI join wrong; ") + (midiUndone ? "" : "undo wrong; ") + (cropOk ? "" : "crop wrong; ") + (audioOk ? "" : "audio join wrong; ") + (mixedRefused ? "" : "mixed not refused; ")
                       + "joined file: " + levels);
            edit->deleteTrack (ct2);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.24: warping by grabbing the hits ---------------------------------------------------------
        {
            rep.step ("warp hits");
            auto wt = ClipImport::createContentTrack (*edit, "Warp test", false);
            const double bpmNow = edit->tempoSequence.getTempo (0)->getBpm(), beatSec = 60.0 / bpmNow;
            // a recording of four drum-like hits, one on each beat at the song's tempo
            auto f = dir.getChildFile ("warp_hits.wav");
            f.deleteFile();
            {
                const int len = (int) (48000.0 * beatSec * 4.0);
                AudioBuffer<float> b (2, len);
                b.clear();
                Random r (8);
                for (int h = 0; h < 4; ++h)
                    for (int i = 0; i < 4000; ++i)
                    {
                        const int at = (int) (h * beatSec * 48000.0) + i;
                        const float v = 0.7f * std::exp (-i / 600.0f) * (r.nextFloat() * 2.0f - 1.0f);
                        if (at < len) { b.setSample (0, at, v); b.setSample (1, at, v); }
                    }
                WavAudioFormat wav;
                if (auto os = f.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (b, 0, len);
            }
            te::Clip::Ptr wc = ClipImport::importFile (*wt, *edit, f, te::TimePosition());
            auto* ac = dynamic_cast<te::AudioClipBase*> (wc.get());
            bool ok = ac != nullptr;
            String detail;
            if (ac != nullptr)
            {
                Workflow::enableWarpMarkers (*ac, true);
                const auto hits = Workflow::sourceTransients (*ac);
                const int markersBefore = ac->getWarpTimeManager().getMarkers().size();
                // the hit near beat 2 (the third one) is found; halfway between two hits there is none
                const auto near = Workflow::transientNearBeat (*ac, 2.03, 0.15), none = Workflow::transientNearBeat (*ac, 2.5, 0.15);
                const bool found = hits.size() == 4 && near.has_value() && std::abs (near->inSeconds() - 2.0 * beatSec) < 0.02 && ! none.has_value();
                // pinning it puts a marker exactly there and moves nothing; pinning it again gives the same marker
                int index = -1, again = -1;
                double pinnedBeat = -1.0, movedBeat = -1.0, sourceAfter = -1.0;
                if (near.has_value())
                {
                    index = Workflow::addWarpMarkerAtTransient (*ac, *near);
                    again = Workflow::addWarpMarkerAtTransient (*ac, *near);
                    auto& wm = ac->getWarpTimeManager();
                    if (index >= 0)
                    {
                        pinnedBeat = Workflow::warpTimeToEditBeat (*ac, wm.getMarkers()[index]->warpTime);
                        // dragging the marker a quarter of a beat later makes the hit sound there
                        wm.moveMarker (index, Workflow::editBeatToWarpTime (*ac, 2.25));
                        movedBeat = Workflow::warpTimeToEditBeat (*ac, wm.getMarkers()[index]->warpTime);
                        sourceAfter = wm.getMarkers()[index]->sourceTime.inSeconds();
                    }
                }
                const int markersAfter = ac->getWarpTimeManager().getMarkers().size();
                ok = found && index >= 0 && again == index && markersAfter == markersBefore + 1 && std::abs (pinnedBeat - 2.0) < 0.05 && std::abs (movedBeat - 2.25) < 0.01
                     && std::abs (sourceAfter - 2.0 * beatSec) < 0.02;
                detail = String (hits.size()) + " hits found; pinned at beat " + String (pinnedBeat, 2) + ", dragged to " + String (movedBeat, 2) + " (still the audio at " + String (sourceAfter, 2) + " s)";
            }
            rep.check (ok, "Warp by grabbing a hit (the hit under the mouse is found, pinned where it sounds, and moves with the marker)", detail);
            edit->deleteTrack (wt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.25: session launches land on the launch grid in the arrangement --------------------------
        {
            using SR = Launcher::SessionRecorder;
            const auto was = edit->getLaunchQuantisation().type.get();
            edit->getLaunchQuantisation().type = te::LaunchQType::sixteenth;
            const double sixteenth = SR::gridBeats (*edit);
            edit->getLaunchQuantisation().type = te::LaunchQType::bar;
            const double bar = SR::gridBeats (*edit);
            edit->getLaunchQuantisation().type = was;
            // a launch on beat 2.25 that is noticed a little later (up to a sixth of a beat) is still put on 2.25
            const bool punched = std::abs (SR::punchBeat (2.26, 0.25) - 2.25) < 1e-9 && std::abs (SR::punchBeat (2.41, 0.25) - 2.25) < 1e-9 && std::abs (SR::punchBeat (2.5, 0.25) - 2.5) < 1e-9
                                 && std::abs (SR::punchBeat (8.1, 4.0) - 8.0) < 1e-9 && std::abs (SR::punchBeat (7.99, 4.0) - 8.0) < 1e-9;
            rep.check (std::abs (sixteenth - 0.25) < 1e-9 && std::abs (bar - EngineHelpers::beatsPerBar (*edit)) < 1e-9 && punched,
                       "Session recording: launches written into the arrangement land on the launch grid (sixteenths to bars)", "grid for 1/16: " + String (sixteenth, 2) + " beats, for a bar: " + String (bar, 1));
        }

        // --- v0.99.26: the mastering chain and its assistant ------------------------------------------------
        {
            rep.step ("mastering");
            // a small project of its own: four seconds of a steady tone at -20 dB
            auto toneFile = dir.getChildFile ("master_tone.wav");
            toneFile.deleteFile();
            {
                AudioBuffer<float> b (2, 48000 * 4);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.1f * (float) std::sin (MathConstants<double>::twoPi * 440.0 * i / 48000.0); b.setSample (0, i, v); b.setSample (1, i, v); }
                WavAudioFormat wav;
                if (auto os = toneFile.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            auto small = te::createEmptyEdit (engine, dir.getChildFile ("master_test.tracktionedit"));
te::EditFileOperations (*small).save (true, true, false);   // like the app: the project exists on disk
            small->ensureNumberOfAudioTracks (1);
            auto* mt = te::getAudioTracks (*small)[0];
            mt->insertWaveClip ("tone", toneFile, { { te::TimePosition(), te::TimeDuration::fromSeconds (4.0) }, {} }, false);
            small->getMasterPluginList().insertPlugin (small->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);

            // the chain goes on in order, before the meter, and is not put on twice
            auto* lim = Mastering::addChain (*small);
            StringArray order;
            for (auto p : small->getMasterPluginList()) order.add (p->getName());
            const int count = small->getMasterPluginList().size();
            Mastering::addChain (*small);
            const bool chainOk = lim != nullptr && small->getMasterPluginList().size() == count
                                 && order.indexOf ("Prism EQ") >= 0 && order.indexOf ("Prism EQ") < order.indexOf ("Multiband Dynamics") && order.indexOf ("Multiband Dynamics") < order.indexOf ("Exciter")
                                 && order.indexOf ("Exciter") < order.indexOf ("Limiter") && order.indexOf ("Limiter") == count - 2;
            // the assistant brings the quiet tone up to the target, and says what it did
            const auto outcome = Mastering::assist (*small, -14.0);
            const bool assisted = outcome.ok && outcome.before < -20.0 && std::abs (outcome.after + 14.0) < 1.0 && outcome.limiterGain > 5.0f && outcome.peakAfter < -0.5f
                                  && lim != nullptr && std::abs (lim->get (pulse::Limiter::pGain) - outcome.limiterGain) < 0.01f && outcome.text().contains ("LUFS");
            const bool sums = std::abs (Mastering::gainFor (-20.0, -14.0, 0.0f) - 6.0f) < 1e-5f && Mastering::gainFor (-10.0, -14.0, 0.0f) == 0.0f && Mastering::gainFor (-40.0, -14.0, 20.0f) == 24.0f;
            rep.check (chainOk && assisted && sums, "Mastering (the chain goes on the master in order and only once; the assistant brings a song to -14 LUFS under the true-peak ceiling)",
                       "master: " + order.joinIntoString (" > ") + "; " + String (outcome.before, 1) + " -> " + String (outcome.after, 1) + " LUFS with " + String (outcome.limiterGain, 1) + " dB, true peak " + String (outcome.peakAfter, 1));
            small = nullptr;
        }

        // --- v0.99.27: VCA faders ----------------------------------------------------------------------------
        {
            rep.step ("vca");
            auto va = ClipImport::createContentTrack (*edit, "Vca A", false), vb = ClipImport::createContentTrack (*edit, "Vca B", false), vc = ClipImport::createContentTrack (*edit, "Vca C", false);
            va->getVolumePlugin()->setVolumeDb (-3.0f); vb->getVolumePlugin()->setVolumeDb (-10.0f); vc->getVolumePlugin()->setVolumeDb (-5.0f);
            const int g = Vca::add (*edit, "Drums");
            Vca::join (*edit, g, *va); Vca::join (*edit, g, *vb);
            const bool joined = Vca::groupOf (*edit, *va) == g && Vca::groupOf (*edit, *vb) == g && Vca::groupOf (*edit, *vc) == -1 && Vca::tracksOf (*edit, g).size() == 2;
            // the group fader moves its members by the same amount, and leaves other tracks alone
            Vca::setLevel (*edit, g, -6.0f);
            const bool moved = std::abs (va->getVolumePlugin()->getVolumeDb() + 9.0f) < 0.05f && std::abs (vb->getVolumePlugin()->getVolumeDb() + 16.0f) < 0.05f && std::abs (vc->getVolumePlugin()->getVolumeDb() + 5.0f) < 0.05f;
            // pushed past what one member's fader can do, the whole group stops there: the balance between them is kept
            Vca::setLevel (*edit, g, 6.0f);   // +12 from here, but A can only go up 15 dB to +6... and the group's own top is +6
            const float aTop = va->getVolumePlugin()->getVolumeDb(), bTop = vb->getVolumePlugin()->getVolumeDb();
            Vca::setLevel (*edit, g, -60.0f);
            const float aLow = va->getVolumePlugin()->getVolumeDb(), bLow = vb->getVolumePlugin()->getVolumeDb();
            const bool balance = std::abs ((aTop - bTop) - 7.0f) < 0.1f && std::abs ((aLow - bLow) - 7.0f) < 0.1f && bLow >= -60.05f && aTop <= 6.05f;
            Vca::setLevel (*edit, g, 0.0f);
            // a track is in one group only; leaving keeps its fader where it is; mute mutes the members
            const int g2 = Vca::add (*edit);
            Vca::join (*edit, g2, *vb);
            const bool oneOnly = Vca::groupOf (*edit, *vb) == g2 && Vca::tracksOf (*edit, g).size() == 1;
            Vca::setMuted (*edit, g, true);
            const bool mutes = va->isMuted (false) && ! vb->isMuted (false);
            Vca::setMuted (*edit, g, false);
            const float beforeLeave = va->getVolumePlugin()->getVolumeDb();
            Vca::leave (*edit, *va);
            const bool left = Vca::groupOf (*edit, *va) == -1 && std::abs (va->getVolumePlugin()->getVolumeDb() - beforeLeave) < 0.01f;
            // the strip draws, and removing a group is undone in one step
            EditViewState vevs (*edit, sm);
            Vca::Strip vstrip (vevs, g2);
            vstrip.setSize (78, 420);
            { PulseLookAndFeel laf; LookAndFeel::setDefaultLookAndFeel (&laf); Image img (Image::ARGB, 78, 420, true); Graphics gr (img); vstrip.paint (gr); LookAndFeel::setDefaultLookAndFeel (nullptr); }
            edit->getUndoManager().beginNewTransaction ("remove vca");
            Vca::remove (*edit, g2);
            const bool removed = Vca::all (*edit).size() == 1 && Vca::groupOf (*edit, *vb) == -1;
            edit->getUndoManager().undo();
            const bool back = Vca::all (*edit).size() == 2 && Vca::groupOf (*edit, *vb) == g2;
            rep.check (joined && moved && balance && oneOnly && mutes && left && removed && back,
                       "VCA faders (members move together and keep their balance at the ends, one group per track, mute, leave, remove and undo)",
                       String (joined ? "" : "joining wrong; ") + (moved ? "" : "moving wrong; ") + (balance ? "" : "balance lost (" + String (aTop, 1) + " / " + String (bTop, 1) + ", " + String (aLow, 1) + " / " + String (bLow, 1) + "); ")
                       + (oneOnly ? "" : "two groups at once; ") + (mutes ? "" : "mute wrong; ") + (left ? "" : "leave wrong; ") + (removed && back ? "" : "remove / undo wrong; ")
                       + "top " + String (aTop, 1) + " / " + String (bTop, 1) + " dB, bottom " + String (aLow, 1) + " / " + String (bLow, 1) + " dB");
            while (! Vca::all (*edit).empty()) Vca::remove (*edit, 0);
            edit->deleteTrack (va); edit->deleteTrack (vb); edit->deleteTrack (vc);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.30: the load guard (freeze the heaviest track when the processor is overloaded) -----------
        {
            rep.step ("load guard");
            // a spike is not an overload; three seconds in a row are; a dip that stays high does not start the count again
            LoadGuard::Monitor mon;
            for (int i = 0; i < 29; ++i) mon.tick (0.95, 0.1);
            const bool notYet = ! mon.overloaded() && ! mon.hasRequest();
            mon.tick (0.75, 0.1);
            mon.tick (0.95, 0.1); mon.tick (0.95, 0.1);
            const bool nowOver = mon.overloaded() && mon.hasRequest();
            const bool once = mon.takeRequest() && ! mon.takeRequest() && mon.overloaded();
            mon.tick (0.5, 0.1);
            const bool cleared = ! mon.overloaded();
            for (int i = 0; i < 31; ++i) mon.tick (0.9, 0.1);
            const bool again = mon.hasRequest();
            LoadGuard::Monitor spiky;
            for (int i = 0; i < 100; ++i) spiky.tick (i % 2 == 0 ? 0.99 : 0.2, 0.1);
            const bool spikesIgnored = ! spiky.overloaded();

            // the heaviest track that can be frozen: not an empty one, not a frozen one, whatever they cost
            auto ta = ClipImport::createContentTrack (*edit, "Light", true), tb = ClipImport::createContentTrack (*edit, "Heavy", true), tc2 = ClipImport::createContentTrack (*edit, "Empty", true);
            for (auto* tr : { ta, tb })
            {
                auto mc = tr->insertMIDIClip ("x", { te::TimePosition(), te::TimePosition::fromSeconds (2.0) }, nullptr);
                mc->getSequence().addNote (60, te::BeatPosition(), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
            }
            auto cost = [&] (te::AudioTrack& tr) { return &tr == ta ? 0.1 : &tr == tb ? 0.4 : &tr == tc2 ? 0.9 : 0.0; };
            const auto h = LoadGuard::heaviest (*edit, cost);
            const bool picked = h.track == tb && std::abs (h.cpu - 0.4) < 1.0e-9;
            const auto frozenName = LoadGuard::freezeHeaviest (*edit, cost);
            const bool froze = frozenName == "Heavy" && tb->isFrozen (te::Track::individualFreeze) && ! ta->isFrozen (te::Track::anyFreeze);
            const bool next = LoadGuard::heaviest (*edit, cost).track == ta;   // the frozen one is no longer offered
            (void) tb->setFrozen (false, te::Track::individualFreeze);
            // the choice of what to do is kept
            const auto was = LoadGuard::mode (edit->engine);
            LoadGuard::setMode (edit->engine, LoadGuard::automatic);
            const bool kept = LoadGuard::mode (edit->engine) == LoadGuard::automatic;
            LoadGuard::setMode (edit->engine, was);
            rep.check (notYet && nowOver && once && cleared && again && spikesIgnored && picked && froze && next && kept,
                       "Load guard (overload is seconds in a row and not spikes, the heaviest track that can be frozen is found and frozen, the choice is kept)",
                       String (notYet ? "" : "too early; ") + (nowOver ? "" : "not noticed; ") + (once ? "" : "asked twice; ") + (cleared ? "" : "not cleared; ") + (again ? "" : "not noticed again; ")
                       + (spikesIgnored ? "" : "spikes counted; ") + (picked ? "" : "wrong track; ") + (froze ? "" : "not frozen (" + frozenName + "); ") + (next ? "" : "frozen track offered again; ") + (kept ? "" : "mode not kept; ") + "ok");
            edit->deleteTrack (ta); edit->deleteTrack (tb); edit->deleteTrack (tc2);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.31: the reference song (A/B against a finished song, at matched loudness) -----------------
        {
            rep.step ("reference");
            using RF = pulse::Reference;
            const double sr = 48000.0;
            auto t = ClipImport::createContentTrack (*edit, "Ref", false);
            auto rp = edit->getPluginCache().createNewPlugin (RF::xmlTypeName, {});
            t->pluginList.insertPlugin (rp, 0, nullptr);
            auto* ref = dynamic_cast<RF*> (rp.get());
            // the song: three seconds of a 1 kHz tone at half scale, at 44.1 kHz (so it has to be brought to 48)
            auto songFile = dir.getChildFile ("reference_song.wav");
            songFile.deleteFile();
            {
                AudioBuffer<float> b (2, 44100 * 3);
                for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.5f * (float) std::sin (MathConstants<double>::twoPi * 1000.0 * i / 44100.0); b.setSample (0, i, v); b.setSample (1, i, v); }
                WavAudioFormat wav;
                if (auto os = songFile.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 44100.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
            ref->initialise ({ te::TimePosition(), sr, 480 });
            const bool loaded = ref->loadNow (songFile) && ref->hasSong() && std::abs (ref->songSeconds() - 3.0) < 0.01 && std::abs (ref->songLufs() + 6.7) < 0.6;
            // the mix: a 300 Hz tone at a tenth of full scale. Runs `seconds` from `from`; returns the peak and the share of 1 kHz in what came out
            struct Out { float peak = 0.0f, at1k = 0.0f, at300 = 0.0f; };
            auto run = [&] (double from, double seconds, bool playing, bool rendering) -> Out
            {
                const int block = 480, blocks = (int) (seconds * sr / block);
                AudioBuffer<float> audio (2, block);
                Out o;
                double s1 = 0, c1 = 0, s3 = 0, c3 = 0; int counted = 0;
                for (int k = 0; k < blocks; ++k)
                {
                    const double t0 = from + k * block / sr;
                    for (int i = 0; i < block; ++i) { const float v = 0.1f * (float) std::sin (MathConstants<double>::twoPi * 300.0 * (t0 + i / sr)); audio.setSample (0, i, v); audio.setSample (1, i, v); }
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (t0), te::TimePosition::fromSeconds (t0 + block / sr) }, playing, false, rendering, false);
                    ref->applyToBuffer (fc);
                    if (k >= blocks / 2)
                        for (int i = 0; i < block; ++i, ++counted)
                        {
                            const double x = audio.getSample (0, i), ph = MathConstants<double>::twoPi * (t0 + i / sr);
                            o.peak = jmax (o.peak, (float) std::abs (x));
                            s1 += x * std::sin (1000.0 * ph); c1 += x * std::cos (1000.0 * ph); s3 += x * std::sin (300.0 * ph); c3 += x * std::cos (300.0 * ph);
                        }
                }
                o.at1k = (float) (2.0 * std::sqrt (s1 * s1 + c1 * c1) / jmax (1, counted));
                o.at300 = (float) (2.0 * std::sqrt (s3 * s3 + c3 * c3) / jmax (1, counted));
                return o;
            };
            // on "Mix" the mix goes through, and is measured
            const auto mixOut = run (0.0, 2.0, true, false);
            const float mixLufs = ref->mixLufs.load();
            const bool passes = std::abs (mixOut.at300 - 0.1f) < 0.002f && mixOut.at1k < 0.001f && std::abs (mixLufs + 21.0f) < 1.0f;
            // on "Reference" the song is heard instead, brought to the loudness of the mix
            ref->set (RF::pListen, 1.0f);
            const auto refOut = run (2.0, 2.0, true, false);
            const float turned = ref->matchDb.load();
            const bool matched = refOut.at300 < 0.002f && std::abs (Decibels::gainToDecibels (refOut.at1k) - Decibels::gainToDecibels (0.5f) - turned) < 0.5f && std::abs (turned - (mixLufs - (float) ref->songLufs())) < 0.3f
                                 && turned < -10.0f;
            // without matching it plays as it is; the trim is on top
            ref->set (RF::pMatch, 0.0f);
            const auto rawOut = run (4.0, 3.0, true, false);
            ref->set (RF::pTrim, -6.0f);
            const auto trimOut = run (7.0, 3.0, true, false);
            ref->set (RF::pTrim, 0.0f);
            const bool raw = std::abs (rawOut.at1k - 0.5f) < 0.02f && std::abs (trimOut.at1k - 0.25f) < 0.015f;
            // the song is 3 s long: at 10 s it has come round again with Loop, and is over without
            const auto looped = run (10.0, 0.5, true, false);
            ref->set (RF::pLoop, 0.0f);
            const auto over = run (10.0, 0.5, true, false);
            ref->set (RF::pLoop, 1.0f);
            const bool loops = looped.at1k > 0.4f && over.peak < 0.001f;
            // "start at" moves the place in the song; stopped, nothing plays; in an export the mix goes through untouched
            ref->set (RF::pOffset, 1.0f);
            run (0.5, 0.2, true, false);
            const double posNow = ref->songPosition.load();
            ref->set (RF::pOffset, 0.0f);
            const auto stopped = run (1.0, 0.5, false, false);
            const auto exported = run (1.0, 0.5, true, true);
            const bool places = posNow > 1.5 && posNow < 1.8 && stopped.peak < 0.001f && std::abs (exported.at300 - 0.1f) < 0.002f && exported.at1k < 0.001f;
            const bool sums = RF::matchGain (-14.0, -9.0) == -5.0f && RF::matchGain (-200.0, -9.0) == 0.0f && RF::matchGain (-60.0, 0.0) == -24.0f;
            ref->deinitialise();

            // on the master it goes after the mastering chain and before the meter, and the switch command makes it
            auto small = te::createEmptyEdit (engine, dir.getChildFile ("reference_test.tracktionedit"));
            small->getMasterPluginList().insertPlugin (small->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {}), -1, nullptr);
            const bool nowRef = Mastering::toggleReference (*small);
            Mastering::addChain (*small);
            StringArray order;
            for (auto p : small->getMasterPluginList()) order.add (p->getName());
            const bool placed = nowRef && Mastering::reference (*small, false) != nullptr && order.indexOf ("Limiter") >= 0 && order.indexOf ("Limiter") < order.indexOf ("Reference")
                                && order.indexOf ("Reference") == order.size() - 2 && ! Mastering::toggleReference (*small);
            small = nullptr;

            bool windowOk = false;
            String windowNote;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                ReferenceUI::ReferenceView view (*ref);
                view.setSize (1000, 520);
                Image img (Image::ARGB, 1000, 520, true);
                { Graphics g (img); view.paint (g); }
                view.dragHandle (0, 300.0f, 0.0f);
                const bool handleOk = ref->get (RF::pOffset) > 0.5f && view.getNumHandles() == 1;
                DeviceEditors::Editor ed (evs, *ref);
                ed.setSize (1100, 760);
                std::set<te::AutomatableParameter*> seen;
                std::function<void (Component&)> walk = [&] (Component& c)
                {
                    if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                    if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                    for (auto* chd : c.getChildren()) walk (*chd);
                };
                walk (ed);
                StringArray missing;
                for (int i = 0; i < ref->numParams(); ++i) if (! seen.count (&*ref->param (i))) missing.add (ref->param (i)->paramID);
                windowOk = handleOk && missing.isEmpty() && DeviceEditors::hasFullEditor (*ref);
                windowNote = missing.isEmpty() ? "start at -> " + ref->param (RF::pOffset)->getCurrentValueAsString() : "no control for " + missing.joinIntoString (", ");
                auto shot = ed.createComponentSnapshot (ed.getLocalBounds());
                auto f = dir.getChildFile ("reference.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (shot, *os); }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (loaded && passes && matched && raw && loops && places && sums && placed && windowOk,
                       "Reference song (the mix passes and is measured, the song is heard instead at the loudness of the mix, trim, loop, start, never in an export, its place on the master, its window)",
                       String (loaded ? "" : "not loaded (" + String (ref->songSeconds(), 2) + " s, " + String (ref->songLufs(), 1) + " LUFS); ") + (passes ? "" : "mix not passed; ") + (matched ? "" : "not matched; ")
                       + (raw ? "" : "raw / trim wrong (" + String (rawOut.at1k, 3) + ", " + String (trimOut.at1k, 3) + "); ") + (loops ? "" : "loop wrong (" + String (looped.at1k, 3) + ", " + String (over.peak, 3) + "); ")
                       + (places ? "" : "start / stop / export wrong (" + String (posNow, 2) + " s, " + String (stopped.peak, 3) + ", " + String (exported.at300, 3) + "); ") + (sums ? "" : "sums; ")
                       + (placed ? "" : "master order: " + order.joinIntoString (" > ") + "; ") + "mix " + String (mixLufs, 1) + " LUFS, song " + String (ref->songLufs(), 1) + ", turned " + String (turned, 1) + " dB, heard at "
                       + String (Decibels::gainToDecibels (refOut.at1k), 1) + " dBFS; " + windowNote);
            songFile.deleteFile();
            rp = nullptr;
            edit->deleteTrack (t);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.32: notes for the project and for each track -----------------------------------------------
        {
            rep.step ("notes");
            auto na = ClipImport::createContentTrack (*edit, "Lead Vocal", false), nb = ClipImport::createContentTrack (*edit, "Guitar", false);
            edit->getUndoManager().beginNewTransaction ("note");
            Notes::set (*na, "Retake the second verse.\nBorrowed mic: +6 dB at the preamp.");
            Notes::setProject (*edit, "Master by Friday.");
            const bool stored = Notes::has (*na) && ! Notes::has (*nb) && Notes::ofProject (*edit) == "Master by Friday." && Notes::of (*na).startsWith ("Retake");
            const bool sums = Notes::summary (String::repeatedString ("word ", 100)).endsWith ("...") && Notes::summary (String::repeatedString ("word ", 100)).length() <= 243 && Notes::summary ("  short  ") == "short";
            // a note of nothing but spaces is no note; an empty one takes the property away again
            Notes::set (*nb, "   ");
            const bool blank = ! Notes::has (*nb);
            Notes::set (*nb, {});
            const bool removed = ! nb->state.hasProperty (Notes::noteID);
            // the window: the project first, then the tracks; what is typed goes to the one chosen, and undo brings the old text back
            bool windowOk = false;
            String windowNote;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                Notes::Panel panel (*edit);
                panel.setSize (720, 460);
                const bool lists = panel.numRows() == Notes::tracks (*edit).size() + 1 && panel.selectedRow() == 0 && panel.shownText() == "Master by Friday.";
                panel.select (na);
                const bool shows = panel.shownText().startsWith ("Retake") && panel.selectedRow() == Notes::tracks (*edit).indexOf (na) + 1;
                panel.select (nb);
                edit->getUndoManager().beginNewTransaction ("type");
                panel.type ("Tune the low E.");
                const bool typed = Notes::of (*nb) == "Tune the low E." && Notes::of (*na).startsWith ("Retake");
                edit->getUndoManager().undo();
                const bool undone = ! Notes::has (*nb);
                edit->getUndoManager().redo();
                const bool redone = Notes::of (*nb) == "Tune the low E.";
                auto img = panel.createComponentSnapshot (panel.getLocalBounds());
                auto f = dir.getChildFile ("notes.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                // the track's header wears the mark, and says the note when asked
                EditViewState evs (*edit, sm);
                TrackHeader head (evs, na), plain (evs, ClipImport::createContentTrack (*edit, "Plain", false));
                head.setSize (200, 70);
                const bool marked = head.overNoteMark ({ 5, 5 }) && ! head.overNoteMark ({ 60, 30 }) && ! plain.overNoteMark ({ 5, 5 });
                windowOk = lists && shows && typed && undone && redone && marked;
                windowNote = String (lists ? "" : "list wrong; ") + (shows ? "" : "track's note not shown; ") + (typed ? "" : "typing went elsewhere; ") + (undone && redone ? "" : "undo / redo wrong; ") + (marked ? "" : "header mark wrong; ");
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            // the notes travel with the project file
            const auto xml = edit->state.toXmlString();
            const bool saved = xml.contains ("Master by Friday.") && xml.contains ("Tune the low E.");
            rep.check (stored && sums && blank && removed && windowOk && saved, "Notes (for the project and for each track, kept in the project, undoable; the window, and the mark on the track)",
                       String (stored ? "" : "not stored; ") + (sums ? "" : "summary wrong; ") + (blank && removed ? "" : "empty notes wrong; ") + windowNote + (saved ? "" : "not in the project file; ") + "ok");
            Notes::setProject (*edit, {});
            for (auto tr : te::getAudioTracks (*edit)) if (tr->getName() == "Lead Vocal" || tr->getName() == "Guitar" || tr->getName() == "Plain") edit->deleteTrack (tr);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.33: the song's key, and previews in key ----------------------------------------------------
        {
            rep.step ("song key");
            using SongKey::fromName;
            auto is = [] (const String& name, int tonic, bool minor) { const auto f = SongKey::fromName (name); return f.valid() && f.tonic == tonic && f.minor == minor; };
            auto none = [] (const String& name) { return ! SongKey::fromName (name).valid(); };
            StringArray wrong;
            auto expect = [&] (bool ok, const char* what) { if (! ok) wrong.add (what); };
            expect (is ("Bass Loop Am 120bpm", 9, true), "Am");
            expect (is ("Pad_F#min", 6, true), "F#min");
            expect (is ("KSHMR_Lead_Bb", 10, false), "Bb");
            expect (is ("Keys Emaj7 chords", 4, false), "Emaj7");
            expect (is ("Strings in E minor", 4, true), "E minor");
            expect (is ("Guitar key G", 7, false), "key G");
            expect (is ("Pluck-C#m-128", 1, true), "C#m");
            expect (is ("Choir Dbmaj", 1, false), "Dbmaj");
            expect (none ("A Finished Song"), "A Finished Song");
            expect (none ("FM Bass 01"), "FM Bass");
            expect (none ("Drum Loop 120bpm"), "Drum Loop");
            expect (none ("Big Beat E"), "Big Beat E");
            expect (none ("Ambient Dub"), "Ambient Dub");
            // relative keys are the same notes; the shift is the shortest way round
            const bool sums = SongKey::fromName ("x Am").signature() == 0 && SongKey::fromName ("x F#m").signature() == 9 && SongKey::shiftFor (0, 7) == -5 && SongKey::shiftFor (0, 5) == 5
                              && SongKey::shiftFor (9, 0) == 3 && SongKey::shiftFor (4, 4) == 0 && SongKey::shiftFor (0, 6) == -6;
            // the song's key lives in the project and is undoable
            const bool unset = SongKey::get (*edit) == -1 && SongKey::shiftForFile (*edit, File::getCurrentWorkingDirectory().getChildFile ("Bass Am.wav")) == 0;
            edit->getUndoManager().beginNewTransaction ("key");
            SongKey::set (*edit, 7);   // G major / E minor
            const bool setOk = SongKey::get (*edit) == 7 && SongKey::name (7) == "G major / E minor" && SongKey::shortName (7) == "G/Em";
            const int amToG = SongKey::shiftForFile (*edit, File::getCurrentWorkingDirectory().getChildFile ("Bass Am.wav"));          // C -> G: down 5
            const int emToG = SongKey::shiftForFile (*edit, File::getCurrentWorkingDirectory().getChildFile ("Lead Em.wav"));          // the same key
            const int drums = SongKey::shiftForFile (*edit, File::getCurrentWorkingDirectory().getChildFile ("Drum Loop 120bpm.wav")); // says nothing
            edit->getUndoManager().undo();
            const bool undone = SongKey::get (*edit) == -1;
            rep.check (wrong.isEmpty() && sums && unset && setOk && amToG == -5 && emToG == 0 && drums == 0 && undone,
                       "Song key (a sample's key is read from its name and never guessed, relative keys count as one, the shortest shift, kept in the project)",
                       wrong.isEmpty() ? "Am into G/Em: " + String (amToG) + " st, Em: " + String (emToG) + ", drums: " + String (drums) + (sums ? "" : "; sums wrong") + (unset && setOk && undone ? "" : "; project key wrong")
                                       : "misread: " + wrong.joinIntoString (", "));
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.34: a track's macros on its header -------------------------------------------------------------
        {
            rep.step ("header macros");
            auto mt = ClipImport::createContentTrack (*edit, "Macro Track", true);
            Macros::ensure (*mt);
            const bool noneYet = HeaderMacros::used (*mt).isEmpty();
            te::AutomatableParameter::Ptr target;
            for (auto pl : mt->pluginList)
                if (pl != mt->getVolumePlugin() && pl != mt->getLevelMeterPlugin() && ! pl->getAutomatableParameters().isEmpty()) { target = pl->getAutomatableParameters()[0]; break; }
            auto ms = Macros::get (*mt);
            bool ok = false;
            String note = "no instrument parameter to map (" + String (mt->pluginList.size()) + " devices, " + String (ms.size()) + " macros)";
            if (target != nullptr && ms.size() >= 3)
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                ms[2]->macroName = "Bright";
                Macros::toggleMapping (*target, *ms[2]);
                const bool one = HeaderMacros::used (*mt).size() == 1 && HeaderMacros::used (*mt)[0] == ms[2];
                EditViewState evs (*edit, sm);
                TrackHeader head (evs, mt);
                head.setSize (220, 150);
                const bool shown = head.macros().isVisible() && head.macros().numDials() == 1 && head.macros().getBottom() <= 150;
                // turning the dial turns the macro; it is drawn; a short track has no room for it
                if (auto* d = head.macros().dial (0)) d->turnTo (0.8f);
                const bool turns = std::abs (ms[2]->getCurrentNormalisedValue() - 0.8f) < 0.01f;
                auto img = head.createComponentSnapshot (head.getLocalBounds());
                auto f = dir.getChildFile ("header_macros.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                head.setSize (220, 74);
                const bool hiddenWhenShort = ! head.macros().isVisible();
                head.setSize (220, 40);
                const bool hiddenWhenCompact = ! head.macros().isVisible();
                // unmapped again, the dial goes
                head.setSize (220, 150);
                Macros::toggleMapping (*target, *ms[2]);
                head.macros().refresh();
                const bool gone = head.macros().numDials() == 0 && ! head.macros().isVisible();
                ok = noneYet && one && shown && turns && hiddenWhenShort && hiddenWhenCompact && gone;
                note = String (noneYet ? "" : "unmapped macros counted; ") + (one ? "" : "mapped macro not found; ") + (shown ? "" : "not shown; ") + (turns ? "" : "dial does not turn the macro; ")
                       + (hiddenWhenShort && hiddenWhenCompact ? "" : "shown without room; ") + (gone ? "" : "stays after unmapping; ") + "ok";
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (ok, "Macros on the track header (a dial for each mapped macro when the track is tall enough; it turns the macro)", note);
            edit->deleteTrack (mt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.35: window layouts ------------------------------------------------------------------------
        {
            rep.step ("layouts");
            using Layouts::Layout;
            // a layout survives being written down and read again, whatever its name holds
            Layout a { "Vocal | comping\nday", false, true, 2, 0.456 }, back;
            const bool round = Layouts::fromText (Layouts::toText (a), back) && back.name == "Vocal / comping day" && ! back.browser && back.session && back.tab == 2 && std::abs (back.bottomShare - 0.456) < 0.001;
            Layout junk;
            const bool rejects = ! Layouts::fromText ("nonsense", junk) && ! Layouts::fromText ("|1|0|1|0.3", junk);
            // the lower panel keeps room for itself and for the top, on any window
            const bool heights = Layouts::bottomHeightFor (0.5, 1000) == 500 && Layouts::bottomHeightFor (0.05, 1000) == 120 && Layouts::bottomHeightFor (0.9, 1000) == 780 && Layouts::bottomHeightFor (0.5, 300) == 120;
            // the ready-made ones are all different, and each is recognised as itself
            bool distinct = Layouts::builtIn().size() == 5;
            for (size_t i = 0; i < Layouts::builtIn().size(); ++i)
                for (size_t k = 0; k < Layouts::builtIn().size(); ++k)
                    distinct = distinct && (Layouts::builtIn()[i].sameViewAs (Layouts::builtIn()[k]) == (i == k));
            // your own: kept in the settings, replaced by name, removed
            auto& st = engine.getPropertyStorage().getPropertiesFile();
            const auto before = st.getValue ("windowLayouts");
            st.setValue ("windowLayouts", String());
            const bool firstName = Layouts::freeName (st) == "My layout 1";
            Layouts::save (st, { "My layout 1", true, false, 1, 0.5 });
            Layouts::save (st, { "Tracking", false, false, 0, 0.2 });
            Layouts::save (st, { "my LAYOUT 1", false, false, 1, 0.7 });   // the same name: replaced, not added
            auto all = Layouts::saved (st);
            const bool kept = all.size() == 2 && all[0].name == "my LAYOUT 1" && ! all[0].browser && std::abs (all[0].bottomShare - 0.7) < 0.001 && all[1].name == "Tracking" && Layouts::freeName (st) == "My layout 2";
            Layouts::remove (st, "tracking");
            const bool removed = Layouts::saved (st).size() == 1;
            st.setValue ("windowLayouts", before);
            rep.check (round && rejects && heights && distinct && firstName && kept && removed, "Window layouts (written and read back, the lower panel always fits, five different ready-made ones, your own kept by name)",
                       String (round ? "" : "round trip wrong; ") + (rejects ? "" : "junk accepted; ") + (heights ? "" : "heights wrong; ") + (distinct ? "" : "ready-made ones not distinct; ")
                       + (firstName && kept ? "" : "saving wrong; ") + (removed ? "" : "removing wrong; ") + "ok");
        }

        // --- v0.99.36: track alternatives ----------------------------------------------------------------------
        {
            rep.step ("alternatives");
            auto at = ClipImport::createContentTrack (*edit, "Alt Track", true);
            auto addClip = [&] (const String& nm, double from, int note)
            {
                auto mc = at->insertMIDIClip (nm, { te::TimePosition::fromSeconds (from), te::TimePosition::fromSeconds (from + 2.0) }, nullptr);
                mc->getSequence().addNote (note, te::BeatPosition(), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            };
            auto clipNames = [&] { StringArray s; for (auto c : at->getClips()) s.add (c->getName()); s.sort (true); return s.joinIntoString (","); };
            auto firstNote = [&] () -> int
            {
                for (auto c : at->getClips()) if (auto mc = dynamic_cast<te::MidiClip*> (c)) if (mc->getSequence().getNumNotes() > 0) return mc->getSequence().getNote (0)->getNoteNumber();
                return -1;
            };
            addClip ("verse", 0.0, 60); addClip ("chorus", 4.0, 64);
            const bool plain = ! Alternatives::has (*at) && Alternatives::count (*at) == 1 && Alternatives::current (*at) == 0 && Alternatives::names (*at).joinIntoString (",") == "A";
            // a new, empty alternative: the track is empty, the old clips are kept under A
            edit->getUndoManager().beginNewTransaction ("new alt");
            const int b = Alternatives::add (*at, false);
            const bool emptyB = b == 1 && Alternatives::has (*at) && Alternatives::current (*at) == 1 && at->getClips().isEmpty() && Alternatives::names (*at).joinIntoString (",") == "A,B";
            addClip ("other verse", 0.0, 72);
            // back to A and forth to B: each has its own clips, with their notes
            edit->getUndoManager().beginNewTransaction ("switch");
            const bool toA = Alternatives::switchTo (*at, 0) && clipNames() == "chorus,verse" && firstNote() >= 60 && firstNote() <= 64;
            edit->getUndoManager().beginNewTransaction ("switch back");
            const bool toB = Alternatives::switchTo (*at, 1) && clipNames() == "other verse" && firstNote() == 72 && ! Alternatives::switchTo (*at, 1) && ! Alternatives::switchTo (*at, 7);
            // undo goes back to A's clips
            edit->getUndoManager().undo();
            const bool undone = Alternatives::current (*at) == 0 && clipNames() == "chorus,verse";
            // a copy: the same clips, changed without touching the original; next goes round
            edit->getUndoManager().beginNewTransaction ("copy alt");
            const int c3 = Alternatives::add (*at, true, "Loud take");
            addClip ("extra", 8.0, 50);
            const bool copied = c3 == 2 && clipNames() == "chorus,extra,verse" && Alternatives::nameOf (*at, 2) == "Loud take";
            const bool round = Alternatives::next (*at) && Alternatives::current (*at) == 0 && clipNames() == "chorus,verse" && Alternatives::next (*at) && Alternatives::current (*at) == 1 && clipNames() == "other verse";
            // no two clips anywhere share an id (the ones kept and the ones on the track)
            Alternatives::switchTo (*at, 2);
            std::set<String> ids;
            bool unique = true;
            for (auto tr : te::getAudioTracks (*edit)) for (auto cl : tr->getClips()) unique = unique && ids.insert (cl->itemID.toString()).second;
            // rename, and delete: the one in use hands over to its neighbour; the last one cannot go
            Alternatives::rename (*at, 1, "  Quiet  ");
            const bool renamed = Alternatives::nameOf (*at, 1) == "Quiet";
            const bool removedCurrent = Alternatives::remove (*at, 2) && Alternatives::count (*at) == 2 && Alternatives::current (*at) == 1 && clipNames() == "other verse";
            const bool removedOther = Alternatives::remove (*at, 0) && Alternatives::count (*at) == 1 && Alternatives::current (*at) == 0 && clipNames() == "other verse" && ! Alternatives::has (*at);
            const bool lastStays = ! Alternatives::remove (*at, 0) && clipNames() == "other verse";
            const bool letters = Alternatives::letter (0) == "A" && Alternatives::letter (25) == "Z" && Alternatives::letter (26) == "AA" && Alternatives::letter (27) == "AB";
            // the alternatives travel with the project file, and the header says which is in use
            Alternatives::add (*at, false);
            const bool saved = edit->state.toXmlString().contains ("PULSEALTS") && edit->state.toXmlString().contains ("other verse");
            bool headerOk = false;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                TrackHeader head (evs, at);
                head.setSize (220, 74);
                std::function<bool (Component&)> says = [&] (Component& c) -> bool
                {
                    if (auto* l = dynamic_cast<Label*> (&c); l != nullptr && l->getText().contains ("[B]")) return true;
                    for (auto* ch : c.getChildren()) if (says (*ch)) return true;
                    return false;
                };
                headerOk = says (head);
                PopupMenu menu;
                Alternatives::addToMenu (menu, *at);
                headerOk = headerOk && menu.getNumItems() == 1;
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (plain && emptyB && toA && toB && undone && copied && round && unique && renamed && removedCurrent && removedOther && lastStays && letters && saved && headerOk,
                       "Track alternatives (new empty and as a copy, switching keeps each one's clips and notes, undo, rename, delete, saved with the project, shown on the header)",
                       String (plain ? "" : "a plain track is wrong; ") + (emptyB ? "" : "new empty wrong; ") + (toA && toB ? "" : "switching wrong (" + clipNames() + "); ") + (undone ? "" : "undo wrong; ")
                       + (copied ? "" : "copy wrong; ") + (round ? "" : "next wrong; ") + (unique ? "" : "clip ids shared; ") + (renamed ? "" : "rename wrong; ")
                       + (removedCurrent && removedOther && lastStays ? "" : "delete wrong; ") + (letters ? "" : "letters wrong; ") + (saved ? "" : "not in the project; ") + (headerOk ? "" : "header / menu wrong; ") + "ok");
            edit->deleteTrack (at);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.37: controller mapping pages --------------------------------------------------------------
        {
            rep.step ("mapping pages");
            auto pt = ClipImport::createContentTrack (*edit, "Pages", false);
            auto vol = pt->getVolumePlugin()->volParam;
            auto pan = pt->getVolumePlugin()->panParam;
            edit->state.removeChild (edit->state.getChildWithName ("PULSEMIDIMAP"), nullptr);
            bool pagesOk = false, kept = false, windowOk = false;
            String note;
            {
                MidiLearn learn (*edit);
                // with no page chosen, a mapping is "always"
                learn.startLearning (*vol);
                learn.processControllerForTest (1, 30, 10);
                const bool always = learn.isMapped (*vol) && learn.numMappingsOn (0) == 1 && learn.getPage() == 0;
                // on page 1 knob 31 is the pan; on page 2 the same knob is the volume
                learn.setPage (1);
                learn.startLearning (*pan);
                learn.processControllerForTest (1, 31, 0);
                learn.setPage (2);
                const bool panOnlyOn1 = ! learn.isMapped (*pan);
                learn.startLearning (*vol);
                learn.processControllerForTest (1, 31, 0);
                const bool counts = learn.numMappingsOn (0) == 1 && learn.numMappingsOn (1) == 1 && learn.numMappingsOn (2) == 1;
                pan->setNormalisedParameter (0.5f, sendNotificationSync);
                vol->setNormalisedParameter (0.5f, sendNotificationSync);
                learn.processControllerForTest (1, 31, 127);   // page 2: the volume
                const bool page2 = vol->getCurrentNormalisedValue() > 0.95f && std::abs (pan->getCurrentNormalisedValue() - 0.5f) < 0.01f;
                learn.setPage (1);
                learn.processControllerForTest (1, 31, 0);     // page 1: the pan
                const bool page1 = pan->getCurrentNormalisedValue() < 0.05f && vol->getCurrentNormalisedValue() > 0.95f;
                learn.processControllerForTest (1, 30, 0);     // the "always" one works on every page
                const bool alwaysWorks = vol->getCurrentNormalisedValue() < 0.05f;
                // two buttons of the controller step through the pages that have something on them (3..8 are empty)
                learn.startLearningPageButton (true);
                const bool learningButton = learn.isLearning() && learn.getLearningName().contains ("next");
                learn.processControllerForTest (1, 40, 127);
                learn.startLearningPageButton (false);
                learn.processControllerForTest (1, 41, 127);
                learn.processControllerForTest (1, 40, 0); learn.processControllerForTest (1, 41, 0);
                learn.processControllerForTest (1, 40, 127);
                const bool stepped = learn.getPage() == 2 && learn.pageJustChanged();
                learn.processControllerForTest (1, 40, 100);   // still held: no second step
                const bool held = learn.getPage() == 2;
                learn.processControllerForTest (1, 40, 0);
                learn.processControllerForTest (1, 40, 127);   // round to page 1 (pages 3..8 have nothing)
                const bool wrapped = learn.getPage() == 1;
                learn.processControllerForTest (1, 41, 127);   // and back
                const bool back = learn.getPage() == 2 && learn.pageButtonText (true) == "CC 40 (ch 1)" && learn.pageButtonText (false) == "CC 41 (ch 1)";
                // names, moving a mapping to another page, removing one
                learn.setPageName (2, "Mixer");
                const bool named = learn.pageName (2) == "Mixer" && learn.pageName (1) == "Page 1" && learn.pageName (0) == "Always" && learn.mappingText (*vol).contains ("on Mixer");
                auto rows2 = learn.rows (2);
                const bool rowsOk = rows2.size() == 1 && rows2[0].cc == 31 && rows2[0].target.contains ("Pages");
                if (! rows2.empty()) learn.moveToPage (rows2[0].index, 3);
                const bool moved = learn.numMappingsOn (2) == 0 && learn.numMappingsOn (3) == 1;
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    MidiMap::Panel panel (learn);
                    panel.setSize (640, 420);
                    learn.setPage (3);
                    panel.refreshNow();
                    const bool shows = panel.numRowsShown() == 1 && panel.rowText (0).startsWith ("CC 31") && panel.nameShown() == "Page 3";
                    learn.setPage (2);
                    panel.refreshNow();
                    const bool empty = panel.numRowsShown() == 0 && panel.nameShown() == "Mixer";
                    auto img = panel.createComponentSnapshot (panel.getLocalBounds());
                    learn.setPage (3);
                    panel.refreshNow();
                    auto img2 = panel.createComponentSnapshot (panel.getLocalBounds());
                    auto f = dir.getChildFile ("midi_pages.png");
                    f.deleteFile();
                    if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img2, *os); }
                    windowOk = shows && empty && img.isValid();
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                pagesOk = always && panOnlyOn1 && counts && page2 && page1 && alwaysWorks && learningButton && stepped && held && wrapped && back && named && rowsOk && moved;
                note = String (always ? "" : "always wrong; ") + (panOnlyOn1 && counts ? "" : "pages mixed up; ") + (page2 && page1 ? "" : "the same knob does not change with the page; ") + (alwaysWorks ? "" : "always-mapping lost on a page; ")
                       + (learningButton && stepped && held && wrapped && back ? "" : "page buttons wrong (page " + String (learn.getPage()) + "); ") + (named ? "" : "names wrong; ") + (rowsOk && moved ? "" : "rows / move wrong; ");
            }
            {
                // everything is in the project: a new reader finds the page, the names, the buttons and the mappings
                MidiLearn again (*edit);
                kept = again.getPage() == 3 && again.pageName (2) == "Mixer" && again.numMappingsOn (0) == 1 && again.numMappingsOn (1) == 1 && again.numMappingsOn (3) == 1 && again.pageButtonText (true) == "CC 40 (ch 1)";
                again.setPage (0);
                again.clearPageButton (true); again.clearPageButton (false);
            }
            rep.check (pagesOk && kept && windowOk, "Controller mapping pages (the same knob does something else on each page, always-mappings, page buttons on the controller, names, kept in the project, the window)",
                       note + (kept ? "" : "not kept in the project; ") + (windowOk ? "" : "window wrong; ") + "ok");
            edit->state.removeChild (edit->state.getChildWithName ("PULSEMIDIMAP"), nullptr);
            edit->deleteTrack (pt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.38: Effect Rack (chains side by side) -------------------------------------------------------
        {
            rep.step ("effect rack");
            auto rt = ClipImport::createContentTrack (*edit, "RackTest", false);
            ClipImport::importFile (*rt, *edit, writeTestWav (dir.getChildFile ("rack.wav"), 2.0, 220.0), te::TimePosition());
            const te::TimeRange range { te::TimePosition(), te::TimePosition::fromSeconds (2.0) };
            int n = 0;
            auto level = [&] { return peakDb (engine, EditingOps::renderTrack (*rt, range, "rack" + String (++n))); };
            const float plain = level();
            auto rp = FxRack::create (*edit);
            rt->pluginList.insertPlugin (rp, 0, nullptr);
            auto* rack = FxRack::rackOf (rp.get());
            StringArray wrong;
            auto expect = [&] (bool ok, const String& what) { if (! ok) wrong.add (what); };
            auto nearDb = [] (float a, float b, float tol = 0.25f) { return std::abs (a - b) < tol; };
            if (rack == nullptr) wrong.add ("no rack");
            else
            {
                expect (FxRack::isFxRack (rp.get()) && ! FxRack::isInstrumentRack (rp.get()) && ! DrumRack::isDrumRack (rp.get()) && ! EngineHelpers::trackHasInstrument (*rt), "it counts as an instrument");
                auto first = FxRack::chains (*rack);
                expect (first.size() == 1 && first[0].name == "Chain 1" && FxRack::effects (*rack, first[0].id).isEmpty(), "does not start with one empty chain");
                const float one = level();
                expect (nearDb (one, plain), "one empty chain is not the sound itself (" + String (one - plain, 2) + " dB)");
                // a second empty chain: the same sound twice, 6 dB more
                edit->getUndoManager().beginNewTransaction ("chain");
                const int c2 = FxRack::addChain (*rack);
                const float two = level();
                expect (FxRack::chains (*rack).size() == 2 && nearDb (two, plain + 6.02f), "two empty chains are not +6 dB (" + String (two - plain, 2) + ")");
                auto* out2 = FxRack::output (*rack, c2);
                if (out2 == nullptr) wrong.add ("the chain has no mixer");
                else
                {
                    out2->set (pulse::ChainOut::pVol, -6.02f);
                    expect (nearDb (level(), plain + 3.52f), "a chain at -6 dB beside the dry one is not +3.5 dB");
                    out2->set (pulse::ChainOut::pVol, 0.0f); out2->set (pulse::ChainOut::pMute, 1.0f);
                    expect (nearDb (level(), plain), "a muted chain is still heard");
                    out2->set (pulse::ChainOut::pMute, 0.0f);
                    // listening to the first chain alone silences the second
                    FxRack::setSolo (*rack, first[0].id);
                    expect (nearDb (level(), plain) && FxRack::soloed (*rack) == first[0].id, "solo does not silence the other chain");
                    FxRack::setSolo (*rack, -1);
                    // an effect on the second chain: 12 dB down there, so the sum is 1 + 0.25
                    auto util = edit->getPluginCache().createNewPlugin (pulse::Utility::xmlTypeName, {});
                    expect (FxRack::addEffect (*rack, c2, util) && FxRack::effects (*rack, c2).size() == 1 && FxRack::effects (*rack, first[0].id).isEmpty(), "the effect did not go on its chain");
                    dynamic_cast<pulse::PulsePlugin*> (util.get())->set (pulse::Utility::pGain, -12.04f);
                    expect (nearDb (level(), plain + 1.94f), "an effect on one chain is not heard on that chain only");
                    // a chain whose device delays the sound (a linear-phase EQ, 128 ms) stays in step with the dry chain
                    auto eqp = edit->getPluginCache().createNewPlugin (pulse::PrismEQ::xmlTypeName, {});
                    FxRack::addEffect (*rack, c2, eqp);
                    auto* eq = dynamic_cast<pulse::PrismEQ*> (eqp.get());
                    for (int b = 0; b < pulse::PrismEQ::numBands; ++b) eq->set (pulse::PrismEQ::idx (b, pulse::PrismEQ::pOn), 0.0f);
                    eq->set (eq->g (pulse::PrismEQ::gPhase), 1.0f);
                    eq->syncPhaseMode();
                    dynamic_cast<pulse::PulsePlugin*> (util.get())->set (pulse::Utility::pGain, 0.0f);
                    const float aligned = level();
                    expect (nearDb (aligned, plain + 6.02f, 0.4f), "a delaying device puts its chain out of step with the others (" + String (aligned - plain, 2) + " dB where +6 is in step)");
                    // order within a chain, and removing
                    FxRack::moveEffect (*rack, *eqp, -1);
                    expect (FxRack::effects (*rack, c2).getFirst() == eqp.get(), "moving an effect earlier did nothing");
                    FxRack::removeEffect (*rack, *eqp);
                    FxRack::removeEffect (*rack, *util);
                    expect (FxRack::effects (*rack, c2).isEmpty() && nearDb (level(), plain + 6.02f), "removing the effects did not leave an empty chain");
                }
                FxRack::renameChain (*rack, c2, " Crushed ");
                expect (FxRack::chains (*rack)[1].name == "Crushed", "rename wrong");
                // the card
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    auto fx = edit->getPluginCache().createNewPlugin (pulse::Saturator::xmlTypeName, {});
                    FxRack::addEffect (*rack, c2, fx);
                    FxRackCard card (evs, *dynamic_cast<te::RackInstance*> (rp.get()));
                    card.setSize (560, 300);
                    expect (card.numChainRows() == 2 && card.numFxRows() == 0 && card.selectedChain() == first[0].id, "the card does not show the chains");
                    card.selectChain (c2);
                    expect (card.numFxRows() == 1, "the card does not show the chosen chain's effects");
                    auto img = card.createComponentSnapshot (card.getLocalBounds());
                    auto f = dir.getChildFile ("fx_rack.png");
                    f.deleteFile();
                    if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                // removing a chain takes its effects along; the last chain stays; undo brings a chain back
                edit->getUndoManager().beginNewTransaction ("remove chain");
                expect (FxRack::removeChain (*rack, c2) && FxRack::chains (*rack).size() == 1 && FxRack::output (*rack, c2) == nullptr && ! FxRack::removeChain (*rack, first[0].id), "removing chains wrong");
                expect (nearDb (level(), plain), "after removing the second chain the sound is not as it was");
                // a copy of the rack is its own rack
                auto copy = DragDrop::duplicate (*rp);
                expect (copy != nullptr && FxRack::isFxRack (copy.get()) && FxRack::rackOf (copy.get()) != rack && FxRack::chains (*FxRack::rackOf (copy.get())).size() == 1, "a copy is not a rack of its own");
            }
            rp->deleteFromParent();
            // putting a device into a rack: it stays where it was and sounds the same
            auto util2 = edit->getPluginCache().createNewPlugin (pulse::Utility::xmlTypeName, {});
            rt->pluginList.insertPlugin (util2, 0, nullptr);
            dynamic_cast<pulse::PulsePlugin*> (util2.get())->set (pulse::Utility::pGain, -6.02f);
            const float before = level();
            edit->getUndoManager().beginNewTransaction ("wrap");
            auto wrapped = FxRack::wrap (*util2);
            const bool inPlace = wrapped != nullptr && rt->pluginList.indexOf (wrapped.get()) == 0 && util2->getOwnerRackType() == FxRack::rackOf (wrapped.get());
            expect (inPlace && FxRack::chains (*FxRack::rackOf (wrapped.get())).front().name == "Utility", "putting a device in a rack: not in its place, or the chain is not named after it");
            expect (nearDb (level(), before) && nearDb (before, plain - 6.02f), "a device put in a rack sounds different");
            rep.check (wrong.isEmpty(), "Effect Rack (chains side by side are added together; level, mute, listen alone; effects per chain; a delaying chain stays in step; put a device in a rack)",
                       wrong.isEmpty() ? "dry " + String (plain, 1) + " dBFS, two chains " + String (plain + 6.0f, 1) : wrong.joinIntoString ("; "));
            edit->deleteTrack (rt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.39: the project's video -------------------------------------------------------------------
        {
            rep.step ("video");
            const bool none = ! Video::has (*edit) && Video::file (*edit) == File() && Video::start (*edit) == 0.0;
            // where the song is in the video, and when the picture is put right
            const bool sums = Video::videoTimeAt (12.0, 10.0) == 2.0 && Video::videoTimeAt (4.0, 10.0) == -6.0 && Video::covers (2.0, 30.0) && ! Video::covers (-0.1, 30.0) && ! Video::covers (30.0, 30.0)
                              && Video::covers (500.0, 0.0) && ! Video::needsJump (5.0, 5.08, true) && Video::needsJump (5.0, 5.2, true) && Video::needsJump (5.0, 5.05, false) && ! Video::needsJump (5.0, 5.01, false)
                              && Video::isVideoFile (File::getCurrentWorkingDirectory().getChildFile ("Film.MP4")) && ! Video::isVideoFile (File::getCurrentWorkingDirectory().getChildFile ("song.wav"));
            // kept in the project, undoable
            const auto film = File ("C:\\Windows\\SystemApps\\Microsoft.Windows.CloudExperienceHost_cw5n1h2txyewy\\media\\oobe-intro.mp4");
            const auto missing = dir.getChildFile ("not there.mp4");
            edit->getUndoManager().beginNewTransaction ("video");
            Video::setFile (*edit, missing);
            Video::setStart (*edit, 8.5);
            Video::setSound (*edit, true);
            const bool kept = Video::has (*edit) && Video::file (*edit) == missing && Video::start (*edit) == 8.5 && Video::sound (*edit) && edit->state.toXmlString().contains ("PULSEVIDEO");
            String windowNote = "no Windows sample video on this machine: opening one was not checked";
            bool windowOk = true;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                Video::Panel panel (*edit);
                panel.setSize (720, 441);
                // a file that is not there is said, not shown
                const bool says = ! panel.isOpen() && panel.noteShown().contains ("not there");
                windowOk = says;
                if (! says) windowNote = "a missing file is not reported";
                else if (film.existsAsFile())
                {
                    panel.addToDesktop (0);
                    panel.setVisible (true);
                    Video::setFile (*edit, film);
                    Video::setStart (*edit, 2.0);
                    edit->getTransport().setPosition (te::TimePosition::fromSeconds (0.0));
                    panel.refreshNow();   // (starts opening it: that happens on threads of its own)
                    for (int tries = 0; tries < 160 && ! panel.isOpen() && ! panel.noteShown().contains ("cannot"); ++tries) { VideoPlayerWin::pump (50); panel.refreshNow(); }
                    const bool opened = panel.isOpen() && panel.duration() > 1.0;
                    const bool waits = panel.noteShown().contains ("begins at 0:02");
                    edit->getTransport().setPosition (te::TimePosition::fromSeconds (3.0));
                    panel.refreshNow();
                    for (int tries = 0; tries < 60 && ! panel.currentPicture().isValid(); ++tries) { VideoPlayerWin::pump (50); panel.refreshNow(); }   // (the frame for that place arrives a moment later)
                    for (int tries = 0; tries < 20; ++tries) { VideoPlayerWin::pump (50); panel.refreshNow(); }   // (and the one asked for replaces the first one)
                    const auto& pic = panel.currentPicture();
                    float brightest = 0.0f;
                    if (pic.isValid())
                        for (int y = 0; y < pic.getHeight(); y += jmax (1, pic.getHeight() / 24))
                            for (int x = 0; x < pic.getWidth(); x += jmax (1, pic.getWidth() / 32)) brightest = jmax (brightest, pic.getPixelAt (x, y).getBrightness());
                    const bool shows = panel.noteShown().isEmpty() && pic.isValid() && pic.getWidth() > 16 && pic.getHeight() > 16 && brightest > 0.2f;
                    if (pic.isValid())
                    {
                        auto shot = panel.createComponentSnapshot (panel.getLocalBounds());
                        auto f = dir.getChildFile ("video.png");
                        f.deleteFile();
                        if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (shot, *os); }
                    }
                    edit->getTransport().setPosition (te::TimePosition::fromSeconds (2.0 + panel.duration() + 5.0));
                    panel.refreshNow();
                    const bool ended = panel.noteShown().contains ("ended");
                    edit->getTransport().setPosition (te::TimePosition());
                    windowOk = opened && waits && shows && ended;
                    windowNote = opened ? "a " + String (panel.duration(), 1) + " s video opened, picture " + String (pic.getWidth()) + " x " + String (pic.getHeight()) + " (brightest point " + String (roundToInt (brightest * 100.0f)) + "%), " + panel.playerState().upToFirstOccurrenceOf (", network", false, false) + String (waits ? "" : "; the wait before it is not said") + (shows ? "" : "; not shown inside its range") + (ended ? "" : "; its end is not said")
                                        : "Windows did not open its own sample video (" + panel.noteShown().replaceCharacter ('\n', ' ') + "; " + panel.playerState() + ")";
                    panel.removeFromDesktop();
                }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            edit->getUndoManager().beginNewTransaction ("remove video");
            Video::setFile (*edit, {});
            const bool removed = ! Video::has (*edit);
            edit->getUndoManager().undo();
            const bool undone = Video::has (*edit);
            Video::setFile (*edit, {});
            rep.check (none && sums && kept && windowOk && removed && undone, "Video (the song's place in the film, kept in the project, the window says what it cannot show and follows the song)",
                       String (none ? "" : "a project without video is wrong; ") + (sums ? "" : "sums wrong; ") + (kept ? "" : "not kept; ") + (removed && undone ? "" : "remove / undo wrong; ") + windowNote);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.40: typing a value under a knob ---------------------------------------------------------------
        {
            rep.step ("typed values");
            StringArray wrong;
            auto reads = [&] (const String& text, double want)
            {
                double got = 0.0;
                if (! ValueEntry::parse (text, got) || std::abs (got - want) > jmax (1.0e-9, std::abs (want) * 1.0e-6)) wrong.add ("\"" + text + "\" read as " + String (got));
            };
            reads ("1.5 kHz", 1500.0); reads ("2k", 2000.0); reads ("440", 440.0); reads ("250 ms", 0.25); reads ("1.2 s", 1.2); reads ("35%", 35.0); reads ("-6 dB", -6.0); reads ("+3", 3.0);
            reads ("25L", -25.0); reads ("25R", 25.0); reads ("C", 0.0); reads ("4.0:1", 4.0); reads ("0,5", 0.5); reads ("12 st", 12.0);
            double junk = 0.0;
            if (ValueEntry::parse ("hello", junk) || ValueEntry::parse ("", junk) || ValueEntry::parse ("-", junk)) wrong.add ("words read as numbers");
            if (! ValueEntry::parse ("-inf dB", junk) || junk > -1.0e6) wrong.add ("-inf not read");

            // on real parameters: a frequency, a level, a share shown as a percentage, a time, a choice
            auto vt = ClipImport::createContentTrack (*edit, "Typed", false);
            auto eqp = edit->getPluginCache().createNewPlugin (pulse::PrismEQ::xmlTypeName, {});
            auto cmp = edit->getPluginCache().createNewPlugin (pulse::ProCompressor::xmlTypeName, {});
            vt->pluginList.insertPlugin (eqp, 0, nullptr); vt->pluginList.insertPlugin (cmp, 1, nullptr);
            auto* eq = dynamic_cast<pulse::PrismEQ*> (eqp.get());
            auto* comp = dynamic_cast<pulse::ProCompressor*> (cmp.get());
            using EQ = pulse::PrismEQ;
            auto type = [&] (te::AutomatableParameter* p, const String& text, float want, float tol, const char* what)
            {
                if (! ValueEntry::apply (*p, text)) { wrong.add (String (what) + ": \"" + text + "\" refused"); return; }
                if (std::abs (p->getCurrentValue() - want) > tol) wrong.add (String (what) + ": \"" + text + "\" gave " + String (p->getCurrentValue(), 3) + " (" + p->getCurrentValueAsString() + ")");
            };
            auto* freq = eq->param (EQ::idx (2, EQ::pFreq));
            type (freq, "2k", 2000.0f, 0.5f, "frequency");
            type (freq, "3", 3000.0f, 0.5f, "frequency, a bare number under kHz");
            type (freq, "440 hz", 440.0f, 0.5f, "frequency");
            type (freq, "250", 250.0f, 0.5f, "frequency, a bare number under Hz");
            type (freq, "1234", 1234.0f, 0.5f, "frequency, more exact than it is shown");
            type (freq, "99999", 30000.0f, 0.5f, "frequency past the end");
            type (eq->param (EQ::idx (2, EQ::pGain)), "-6", -6.0f, 0.01f, "gain");
            type (eq->param (EQ::idx (2, EQ::pGain)), "+4.5 dB", 4.5f, 0.01f, "gain");
            type (eq->param (eq->g (EQ::gMix)), "35", 0.35f, 0.006f, "mix");
            type (eq->param (eq->g (EQ::gMix)), "80%", 0.8f, 0.006f, "mix");
            type (eq->param (EQ::idx (2, EQ::pAttack)), "0.02 s", 20.0f, 0.2f, "attack in seconds");
            type (eq->param (EQ::idx (2, EQ::pAttack)), "7.5", 7.5f, 0.06f, "attack, a bare number under ms");
            type (eq->param (EQ::idx (2, EQ::pType)), "notch", (float) EQ::notch, 0.01f, "a choice by name");
            type (eq->param (EQ::idx (2, EQ::pType)), "high sh", (float) EQ::highShelf, 0.01f, "a choice by the start of its name");
            type (eq->param (EQ::idx (2, EQ::pSlope)), "48", 6.0f, 0.01f, "a slope by its number");
            type (comp->param (pulse::ProCompressor::pRatio), "4", 4.0f, 0.05f, "ratio");
            type (comp->param (pulse::ProCompressor::pThreshold), "-18.5", -18.5f, 0.06f, "threshold");
            // nonsense is refused and changes nothing; typing is one undo step
            const float before = freq->getCurrentValue();
            if (ValueEntry::apply (*freq, "loud") || freq->getCurrentValue() != before) wrong.add ("nonsense accepted");
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                ParamKnob knob (evs, *freq);
                knob.setSize (64, 78);
                freq->setParameter (1000.0f, sendNotificationSync);
                edit->getUndoManager().beginNewTransaction ("before typing");
                if (! knob.typeValue ("5k") || std::abs (freq->getCurrentValue() - 5000.0f) > 0.5f) wrong.add ("the knob's box does not set the value");
                if (knob.typeValue ("purple") || std::abs (freq->getCurrentValue() - 5000.0f) > 0.5f) wrong.add ("the knob's box accepts nonsense");
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (wrong.isEmpty(), "Typing a value (units as the knobs show them, bare numbers in the unit shown, choices by name, more exact than the display, nonsense refused)",
                       wrong.isEmpty() ? String ("14 readings, 17 typed values on an EQ and a compressor") : wrong.joinIntoString ("; "));
            edit->deleteTrack (vt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.42: the help line ---------------------------------------------------------------------------
        {
            rep.step ("help line");
            struct Tipped : public Component, public SettableTooltipClient {};
            Tipped outer, inner, silent;
            Component plain, lonely;
            outer.setTooltip ("Drag to move.\nShift for fine steps.");
            inner.setTooltip ("  Mute  ");
            outer.addAndMakeVisible (inner);
            outer.addAndMakeVisible (plain);
            outer.addAndMakeVisible (silent);
            const bool own = HelpLine::textFor (&inner) == "Mute";
            const bool fromAbove = HelpLine::textFor (&plain) == "Drag to move.  Shift for fine steps." && HelpLine::textFor (&silent) == "Drag to move.  Shift for fine steps.";
            const bool none = HelpLine::textFor (&lonely).isEmpty() && HelpLine::textFor (nullptr).isEmpty();
            HelpLine::Line line;
            line.setSize (400, 24);
            line.showFor (&inner);
            const bool shows = line.shown() == "Mute";
            line.showFor (nullptr);
            const bool clears = line.shown().isEmpty();
            // a real control: a knob's help reaches the line from the slider inside it
            auto ht = ClipImport::createContentTrack (*edit, "Help", false);
            bool real = false;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                ParamKnob knob (evs, *ht->getVolumePlugin()->volParam);
                knob.setTooltip ("The track's level");
                knob.setSize (64, 78);
                Component* deepest = knob.getChildComponent (0);
                real = deepest != nullptr && HelpLine::textFor (deepest).isNotEmpty();
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (own && fromAbove && none && shows && clears && real, "Help line (says what the thing under the mouse does: its own help, or that of what it sits in)",
                       String (own ? "" : "own help wrong; ") + (fromAbove ? "" : "help from above wrong; ") + (none ? "" : "help out of nowhere; ") + (shows && clears ? "" : "line wrong; ") + (real ? "" : "a knob's help does not reach it; ") + "ok");
            edit->deleteTrack (ht);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.42: every control of every device window says what it is --------------------------------------
        {
            rep.step ("help audit");
            auto at = ClipImport::createContentTrack (*edit, "HelpAudit", false);
            StringArray silent;
            int looked = 0, devices = 0;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                for (auto& b : ClipImport::builtIns())
                {
                    auto p = edit->getPluginCache().createNewPlugin (b.type, {});
                    auto* pp = dynamic_cast<pulse::PulsePlugin*> (p.get());
                    if (pp == nullptr) continue;
                    at->pluginList.insertPlugin (p, 0, nullptr);
                    ++devices;
                    DeviceEditors::Editor ed (evs, *pp);
                    ed.setSize (1100, 760);
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        // the things one can click, turn or choose from
                        const bool control = dynamic_cast<Button*> (&c) != nullptr || dynamic_cast<Slider*> (&c) != nullptr || dynamic_cast<ComboBox*> (&c) != nullptr;
                        if (control && c.isVisible())
                        {
                            ++looked;
                            if (HelpLine::textFor (&c).isEmpty())
                            {
                                String what = dynamic_cast<Button*> (&c) != nullptr ? "button \"" + dynamic_cast<Button*> (&c)->getButtonText() + "\"" : dynamic_cast<Slider*> (&c) != nullptr ? String ("slider") : String ("box");
                                silent.addIfNotAlreadyThere (String (b.name) + ": " + what);
                            }
                            if (dynamic_cast<ComboBox*> (&c) != nullptr || dynamic_cast<Slider*> (&c) != nullptr) return;   // (their inner parts speak through them)
                        }
                        for (auto* ch : c.getChildren()) walk (*ch);
                    };
                    walk (ed);
                    p->deleteFromParent();
                }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (silent.isEmpty() && devices > 40 && looked > 500, "Every control in every device window has help (so the help line and the tooltips cover them all)",
                       silent.isEmpty() ? String (looked) + " controls in " + String (devices) + " device windows"
                                        : String (silent.size()) + " without help of " + String (looked) + ": " + silent.joinIntoString ("; ").substring (0, 6000));
            edit->deleteTrack (at);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.43: search in long menus ------------------------------------------------------------------
        {
            rep.step ("menu search");
            int chosen = -1;
            PopupMenu menu, effects, instruments, deep;
            for (int i = 0; i < 12; ++i) effects.addItem ("Effect " + String (i + 1), [&chosen, i] { chosen = i; });
            effects.addItem ("Pulse Delay", [&chosen] { chosen = 100; });
            effects.addItem ("Greyed out", false, false, [&chosen] { chosen = 999; });
            for (int i = 0; i < 9; ++i) instruments.addItem ("Synth " + String (i + 1), true, i == 2, [&chosen, i] { chosen = 200 + i; });
            deep.addItem ("Delay Designer", [&chosen] { chosen = 300; });
            instruments.addSubMenu ("More", deep);
            menu.addSectionHeader ("Devices");
            menu.addSubMenu ("Effects", effects);
            menu.addSeparator();
            menu.addSubMenu ("Instruments", instruments);
            menu.addItem ("Remove", [&chosen] { chosen = 400; });
            std::vector<SearchMenu::Entry> all;
            SearchMenu::collect (menu, {}, all);
            // everything that can be chosen is found, with where it is; what cannot be chosen is left out
            bool paths = all.size() == 24;
            StringArray names;
            for (auto& e : all) names.add (e.path);
            paths = paths && names.contains ("Effects > Pulse Delay") && names.contains ("Instruments > More > Delay Designer") && names.contains ("Remove") && ! names.joinIntoString ("|").contains ("Greyed");
            const bool words = SearchMenu::matches ("Effects > Pulse Delay", "delay pulse") && SearchMenu::matches ("Effects > Pulse Delay", "EFF del") && ! SearchMenu::matches ("Effects > Pulse Delay", "reverb")
                               && SearchMenu::matches ("anything", "");
            // "delay": both delays, the one whose own name starts with it first
            const auto found = SearchMenu::filter (all, "delay");
            const bool order = found.size() == 2 && all[(size_t) found[0]].path.endsWith ("Delay Designer") && all[(size_t) found[1]].path.endsWith ("Pulse Delay");
            // a long menu gets the search entry on top; a short one is left as it is
            auto longer = SearchMenu::withSearch (menu);
            PopupMenu::MenuItemIterator it (longer, false);
            const bool added = it.next() && it.getItem().text.startsWith ("Search this menu") && it.getItem().text.contains ("24");
            PopupMenu small;
            for (int i = 0; i < 20; ++i) small.addItem ("Item " + String (i), [] {});
            auto same = SearchMenu::withSearch (small);
            PopupMenu::MenuItemIterator it2 (same, false);
            const bool leftAlone = it2.next() && it2.getItem().text == "Item 0";
            // the box: typing narrows the list, choosing runs the entry
            bool boxOk = false;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                auto shared = std::make_shared<std::vector<SearchMenu::Entry>> (all);
                SearchMenu::Box box (shared);
                const bool everything = box.numShown() == 24;
                box.type ("synth 3");
                const bool narrowed = box.numShown() == 1 && box.shownText (0) == "Instruments > Synth 3";
                box.chooseRow (0);
                const bool ran = chosen == 202;
                box.type ("zzz");
                const bool nothing = box.numShown() == 0;
                box.chooseRow (0);   // nothing to choose: nothing happens
                boxOk = everything && narrowed && ran && nothing && chosen == 202;
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (paths && words && order && added && leftAlone && boxOk, "Search in long menus (every entry found with its submenu, any words in any order, the search entry only on long menus, choosing runs it)",
                       String (paths ? "" : "entries wrong (" + String ((int) all.size()) + "); ") + (words ? "" : "matching wrong; ") + (order ? "" : "order wrong; ") + (added && leftAlone ? "" : "search entry wrong; ") + (boxOk ? "" : "box wrong; ") + "ok");
        }

        // --- v0.99.44: missing files, and finding them again --------------------------------------------------
        {
            rep.step ("missing files");
            auto home = dir.getChildFile ("missing_home"), away = dir.getChildFile ("missing_away");
            home.deleteRecursively(); away.deleteRecursively();
            // the files are where the folder was moved to; the project still points at where it used to be
            const auto newHome = away.getChildFile ("Session 12").getChildFile ("missing_home");
            newHome.createDirectory();
            writeTestWav (newHome.getChildFile ("Lead Vocal.wav"), 1.0, 330.0);
            writeTestWav (newHome.getChildFile ("Finished Song.wav"), 1.0, 220.0);
            const int before = Sharing::missingFiles (*edit).size();
            auto mt = ClipImport::createContentTrack (*edit, "MissingTest", false);
            ClipImport::importFile (*mt, *edit, writeTestWav (dir.getChildFile ("missing_seed.wav"), 1.0, 330.0), te::TimePosition());
            for (auto c : mt->getClips())
                if (auto w = dynamic_cast<te::WaveAudioClip*> (c)) w->getSourceFileReference().setToFile (home.getChildFile ("Lead Vocal.wav"), te::SourceFileReference::PathStyle::chooseBest, false);
            auto refp = edit->getPluginCache().createNewPlugin (pulse::Reference::xmlTypeName, {});
            mt->pluginList.insertPlugin (refp, 0, nullptr);
            dynamic_cast<pulse::Reference*> (refp.get())->state.setProperty ("referenceFile", home.getChildFile ("Finished Song.wav").getFullPathName(), nullptr);
            const bool moved = ! home.exists();
            const auto missing = Sharing::missingFiles (*edit);
            const bool noticed = moved && missing.size() == before + 2 && missing.joinIntoString ("|").contains ("Lead Vocal.wav") && missing.joinIntoString ("|").contains ("Finished Song.wav");
            // looking in the wrong place finds nothing; looking above the new place finds both
            auto elsewhere = dir.getChildFile ("missing_elsewhere");
            elsewhere.createDirectory();
            const auto none = Sharing::relink (*edit, elsewhere);
            const bool stillGone = none.found == 0 && none.stillMissing.size() == before + 2 && none.summary().contains ("None of the missing");
            // a file of the same name in another folder is passed over for the one whose folders match the old path
            writeTestWav (away.getChildFile ("Other").getChildFile ("Lead Vocal.wav"), 0.5, 100.0);
            const auto found = Sharing::relink (*edit, away);
            te::WaveAudioClip* clip = nullptr;
            for (auto c : mt->getClips()) if (auto w = dynamic_cast<te::WaveAudioClip*> (c)) clip = w;
            const bool relinked = found.found == 2 && Sharing::missingFiles (*edit).size() == before && clip != nullptr && clip->getOriginalFile().existsAsFile()
                                  && clip->getOriginalFile().getParentDirectory().getFileName() == "missing_home" && dynamic_cast<pulse::Reference*> (refp.get())->getFile().existsAsFile()
                                  && found.summary().contains ("2 files were found");
            rep.check (noticed && stillGone && relinked, "Missing files (a moved folder is noticed, a clip's file and a device's file are found again by name, the likeliest of two same-named files is taken)",
                       String (noticed ? "" : "not noticed (" + String (missing.size() - before) + " missing); ") + (stillGone ? "" : "found where it is not; ") + (relinked ? "" : "not found again (" + String (found.found) + " found, " + String (found.stillMissing.size() - before) + " still missing); ") + "ok");
            edit->deleteTrack (mt);
            away.deleteRecursively(); elsewhere.deleteRecursively();
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.45: defaults for devices and for new tracks -------------------------------------------------
        {
            rep.step ("defaults");
            auto& st = engine.getPropertyStorage().getPropertiesFile();
            const auto utilType = String (pulse::Utility::xmlTypeName);
            const auto oldDevice = st.getValue (DeviceDefaults::keyFor (utilType)), oldTrack = st.getValue (Presets::defaultTrackKey);
            DeviceDefaults::forget (engine, utilType);
            Presets::forgetDefaultTrack (engine);
            auto dt = ClipImport::createContentTrack (*edit, "Defaults", false);
            auto fresh = [&] { return ClipImport::createPluginFromDescription (*edit, "plugin:builtin:" + utilType); };
            auto gainOf = [] (te::Plugin::Ptr p) { return dynamic_cast<pulse::PulsePlugin*> (p.get())->get (pulse::Utility::pGain); };
            // a new device starts as it came, until its kind is given a default
            auto first = fresh();
            const bool plain = ! DeviceDefaults::has (engine, utilType) && std::abs (gainOf (first)) < 0.001f;
            auto* u = dynamic_cast<pulse::PulsePlugin*> (first.get());
            u->set (pulse::Utility::pGain, -7.5f); u->set (pulse::Utility::pWidth, 1.4f);
            DeviceDefaults::save (*u);
            auto second = fresh();
            const bool fromDefault = DeviceDefaults::has (engine, utilType) && std::abs (gainOf (second) + 7.5f) < 0.001f
                                     && std::abs (dynamic_cast<pulse::PulsePlugin*> (second.get())->get (pulse::Utility::pWidth) - 1.4f) < 0.001f;
            // a device that is not made new by hand (from a preset, a copy, a project) is left as it is
            auto raw = edit->getPluginCache().createNewPlugin (utilType, {});
            const bool untouched = std::abs (gainOf (raw)) < 0.001f;
            // other kinds are not affected; forgetting brings the starting values back
            auto other = ClipImport::createPluginFromDescription (*edit, String ("plugin:builtin:") + pulse::Saturator::xmlTypeName);
            const bool otherKinds = other != nullptr && ! DeviceDefaults::has (engine, pulse::Saturator::xmlTypeName);
            PopupMenu menu;
            DeviceDefaults::addToMenu (menu, second);
            const bool offered = menu.getNumItems() == 2;
            DeviceDefaults::forget (engine, utilType);
            const bool forgotten = std::abs (gainOf (fresh())) < 0.001f;

            // new tracks: the effects of a chain (not its instrument) on every new track
            auto src = ClipImport::createContentTrack (*edit, "DefaultSource", true);
            auto fxA = edit->getPluginCache().createNewPlugin (utilType, {});
            PluginMenu::insertPlugin (src, src->pluginList, fxA, nullptr);
            dynamic_cast<pulse::PulsePlugin*> (fxA.get())->set (pulse::Utility::pGain, -3.0f);
            PluginMenu::insertPlugin (src, src->pluginList, edit->getPluginCache().createNewPlugin (pulse::Saturator::xmlTypeName, {}), nullptr);
            const int kept = Presets::saveDefaultTrack (src->pluginList, src);
            const int made = Presets::applyDefaultTrack (*dt);
            StringArray names;
            float carried = 0.0f;
            for (auto p : Presets::userChain (dt->pluginList, dt)) { names.add (p->getName()); if (auto pp = dynamic_cast<pulse::PulsePlugin*> (p); pp != nullptr && p->getPluginType() == utilType) carried = pp->get (pulse::Utility::pGain); }
            const bool trackDefault = kept == 2 && made == 2 && Presets::hasDefaultTrack (engine) && names.joinIntoString (",") == "Utility,Saturator" && std::abs (carried + 3.0f) < 0.001f
                                      && ! EngineHelpers::trackHasInstrument (*dt);
            Presets::forgetDefaultTrack (engine);
            auto dt2 = ClipImport::createContentTrack (*edit, "Defaults2", false);
            const bool emptyAgain = Presets::applyDefaultTrack (*dt2) == 0 && Presets::userChain (dt2->pluginList, dt2).isEmpty();
            rep.check (plain && fromDefault && untouched && otherKinds && offered && forgotten && trackDefault && emptyAgain,
                       "Defaults (a device's settings as the default for its kind, only for devices made new; a chain's effects on every new track; both can be forgotten)",
                       String (plain ? "" : "no default yet is wrong; ") + (fromDefault ? "" : "default not applied; ") + (untouched ? "" : "applied to a device not made new; ") + (otherKinds && offered ? "" : "menu / other kinds wrong; ")
                       + (forgotten ? "" : "not forgotten; ") + (trackDefault ? "" : "track default wrong (" + names.joinIntoString (",") + ", " + String (kept) + " kept, " + String (made) + " made); ") + (emptyAgain ? "" : "track default not forgotten; ") + "ok");
            if (oldDevice.isNotEmpty()) st.setValue (DeviceDefaults::keyFor (utilType), oldDevice);
            if (oldTrack.isNotEmpty()) st.setValue (Presets::defaultTrackKey, oldTrack);
            edit->deleteTrack (dt); edit->deleteTrack (dt2); edit->deleteTrack (src);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.46: the piano roll's zoom ------------------------------------------------------------------
        {
            rep.step ("piano roll zoom");
            auto zt = ClipImport::createContentTrack (*edit, "ZoomTest", true);
            auto ca = zt->insertMIDIClip ("first", { te::TimePosition(), te::TimePosition::fromSeconds (8.0) }, nullptr);
            auto cb = zt->insertMIDIClip ("second", { te::TimePosition::fromSeconds (8.0), te::TimePosition::fromSeconds (16.0) }, nullptr);
            ca->getSequence().addNote (48, te::BeatPosition::fromBeats (2.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            ca->getSequence().addNote (72, te::BeatPosition::fromBeats (6.0), te::BeatDuration::fromBeats (2.0), 100, 0, nullptr);
            cb->getSequence().addNote (60, te::BeatPosition::fromBeats (1.0), te::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
            bool ok = false;
            String note;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState evs (*edit, sm);
                PianoRoll roll (evs);
                roll.setSize (1000, 500);
                roll.setClip (ca.get());
                const auto start = roll.currentView();
                // fit everything: both notes are inside the view, left to right and top to bottom
                roll.zoomToFit (true);
                const auto low = roll.positionOf (2.0, 48), high = roll.positionOf (8.0, 72), origin = roll.positionOf (0.0, 60);
                const bool fits = low.x > 64.0f && high.x < 1000.0f && origin.x >= 64.0f && high.y > 32.0f && low.y < 500.0f - 64.0f && low.y > high.y;
                const auto fitted = roll.currentView();
                // Alt + wheel is what zoomRows does: taller rows, the pitch under the mouse stays put
                const float yBefore = roll.positionOf (2.0, 60).y;
                roll.zoomRows (1.5f, yBefore);
                const bool taller = roll.currentView().rowH > fitted.rowH * 1.3f && std::abs (roll.positionOf (2.0, 60).y - yBefore) < roll.currentView().rowH;
                roll.zoomRows (100.0f, 200.0f); const bool capped = roll.currentView().rowH <= 30.0f;
                roll.zoomRows (0.0001f, 200.0f); const bool floored = roll.currentView().rowH >= 7.0f;
                // each clip is found again as it was left
                roll.zoomToFit (true);
                const auto leftA = roll.currentView();
                roll.setClip (cb.get());
                roll.zoomRows (1.4f, 200.0f);
                const auto leftB = roll.currentView();
                roll.setClip (ca.get());
                const auto backA = roll.currentView();
                roll.setClip (cb.get());
                const auto backB = roll.currentView();
                const bool remembered = std::abs (backA.ppb - leftA.ppb) < 1.0e-6 && std::abs (backA.scrollY - leftA.scrollY) < 0.01f && std::abs (backB.rowH - leftB.rowH) < 0.01f
                                        && std::abs (backA.rowH - backB.rowH) > 0.5f && roll.numRememberedViews() == 2;
                // and after the project has been closed and opened: through the view state
                ValueTree state ("VIEW");
                roll.saveViews (state);
                PianoRoll again (evs);
                again.setSize (1000, 500);
                again.loadViews (state);
                again.setClip (ca.get());
                const bool saved = state.getChildWithName ("ROLLVIEWS").getNumChildren() == 2 && std::abs (again.currentView().ppb - leftA.ppb) < 1.0e-6 && std::abs (again.currentView().rowH - leftA.rowH) < 0.01f;
                again.setClip (nullptr);
                // Notes that go away while they are selected (an undo, an edit made elsewhere), then Delete: the roll
                // must not touch notes that are no longer there (found by the random-input test: it crashed here)
                roll.setClip (ca.get());
                roll.keyPressed (KeyPress ('a', ModifierKeys::ctrlModifier, 0));
                const int hadSelected = roll.numSelectedNotes();
                int manySelected = 0, manyLeft = -1;
                {
                    auto& seq = ca->getSequence();
                    struct Kept { int pitch; te::BeatPosition start; te::BeatDuration length; int velocity; };
                    std::vector<Kept> keptNotes;
                    for (auto* nn : seq.getNotes()) keptNotes.push_back ({ nn->getNoteNumber(), nn->getStartBeat(), nn->getLengthBeats(), nn->getVelocity() });
                    while (seq.getNotes().size() > 0) seq.removeNote (*seq.getNotes().getFirst(), nullptr);
                    roll.keyPressed (KeyPress (KeyPress::deleteKey));
                    // and a great many notes, all selected, deleted at once (each removal tidies the selection while
                    // the others are still being removed)
                    for (int k = 0; k < 600; ++k) seq.addNote (36 + k % 48, te::BeatPosition::fromBeats (k * 0.01), te::BeatDuration::fromBeats (0.01), 100, 0, nullptr);
                    roll.keyPressed (KeyPress ('a', ModifierKeys::ctrlModifier, 0));
                    manySelected = roll.numSelectedNotes();
                    roll.keyPressed (KeyPress (KeyPress::deleteKey));
                    manyLeft = seq.getNotes().size();
                    for (auto& k : keptNotes) seq.addNote (k.pitch, k.start, k.length, k.velocity, 0, nullptr);
                }
                const bool staleSafe = hadSelected > 0 && roll.numSelectedNotes() == 0 && manySelected == 600 && manyLeft == 0;
                roll.setClip (nullptr);
                ok = fits && taller && capped && floored && remembered && saved && staleSafe && std::abs (start.rowH - 13.0f) < 0.01f;
                note = String (fits ? "" : "fit leaves notes outside; ") + (taller ? "" : "row zoom wrong; ") + (capped && floored ? "" : "row height not held in its range; ") + (remembered ? "" : "a clip's view is not remembered; ") + (saved ? "" : "views not kept in the project; ");
                if (! staleSafe) note << "notes that are gone stay selected, or 600 selected notes are not all deleted (" << manyLeft << " left); ";
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (ok, "Piano roll zoom (fit all the notes, taller and flatter rows around the mouse, each clip's view remembered and kept with the project) and deleting hundreds of selected notes at once", note + "ok");
            edit->deleteTrack (zt);
            edit->getUndoManager().clearUndoHistory();
        }

        // --- v0.99.47: the sound check and the tour -------------------------------------------------------------
        {
            rep.step ("first run");
            const auto none = AudioCheck::describe ({}, 0.0, 0, 0, 0);
            const auto noOuts = AudioCheck::describe ("Speakers", 48000.0, 480, 0, 0);
            const auto good = AudioCheck::describe ("Speakers (Realtek)", 48000.0, 480, 2, 0);
            const auto late = AudioCheck::describe ("Speakers", 44100.0, 2048, 2, 512);
            const auto mono = AudioCheck::describe ("Headset", 48000.0, 256, 1, 0);
            const bool checks = ! none.ok && none.line.contains ("No sound device") && none.advice.isNotEmpty()
                                && ! noOuts.ok && noOuts.advice.contains ("outputs")
                                && good.ok && ! good.warning && good.advice.isEmpty() && good.line.contains ("Speakers (Realtek)") && good.line.contains ("48 kHz") && good.line.contains ("10 ms")
                                && late.ok && late.warning && late.line.contains ("44.1 kHz") && late.line.contains ("58 ms") && late.advice.contains ("58 ms")
                                && mono.ok && mono.warning && mono.advice.contains ("mono");
            // the tour: every step lights a part inside the window and puts its words beside it, not over it
            bool tourOk = false;
            String tourNote;
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                std::vector<Tour::Step> steps;
                const Rectangle<int> parts[] = { { 0, 0, 1200, 52 }, { 0, 52, 1200, 400 }, { 0, 52, 330, 400 }, { 0, 460, 1200, 340 }, { 320, 464, 700, 22 }, { 1100, 463, 84, 24 }, { 0, 0, 1200, 800 }, { 5000, 5000, 10, 10 } };
                for (auto& r : parts) steps.push_back ({ "Part", "What it is for.", [r] { return r; } });
                auto* tour = new Tour (steps);
                tour->setSize (1200, 800);
                bool placed = tour->numSteps() == 8 && tour->currentStep() == 0;
                for (int i = 0; i < tour->numSteps(); ++i)
                {
                    tour->go (i);
                    const auto lit = tour->litArea(), bub = tour->bubbleArea();
                    const bool inside = Rectangle<int> (0, 0, 1200, 800).contains (bub) && bub.getWidth() > 300 && bub.getHeight() > 120;
                    const bool clear = i >= 6 || ! bub.intersects (lit);   // (the last two are the whole window, and a part that is not there)
                    if (! inside || ! clear) { placed = false; tourNote << "step " << (i + 1) << " badly placed; "; }
                }
                tour->go (99);
                const bool clamps = tour->currentStep() == 7;
                tour->go (2);
                auto img = tour->createComponentSnapshot (tour->getLocalBounds());
                auto f = dir.getChildFile ("tour.png");
                f.deleteFile();
                if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                bool closed = false;
                tour->onClose = [&closed] { closed = true; };
                tour->keyPressed (KeyPress (KeyPress::escapeKey));
                delete tour;
                tourOk = placed && clamps && closed;
                // the welcome screen with its sound check (a picture to look at)
                WelcomeScreen::Callbacks wcb;
                wcb.audioStatus = [late] { return late; };
                WelcomeScreen welcome ({}, true, wcb);
                welcome.setSize (1200, 800);
                auto wimg = welcome.createComponentSnapshot (welcome.getLocalBounds());
                auto wf = dir.getChildFile ("welcome.png");
                wf.deleteFile();
                if (auto os = wf.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (wimg, *os); }
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
            rep.check (checks && tourOk, "First run (the sound check says what is wrong or worth knowing; the tour lights each part with its words beside it)",
                       String (checks ? "" : "sound check wrong (" + good.line + " | " + late.line + "); ") + (tourOk ? "" : "tour wrong: " + tourNote) + "ok");
        }

        // --- v0.99.48: recovery after a crash ---------------------------------------------------------------------
        {
            rep.step ("recovery");
            auto rdir = dir.getChildFile ("recovery_test");
            rdir.deleteRecursively();
            rdir.createDirectory();
            auto project = rdir.getChildFile ("Song.tracktionedit"), autosave = rdir.getChildFile ("Song.autosave");
            const String savedText = "<EDIT name=\"saved\"/>", autosavedText = "<EDIT name=\"autosaved\"/>";
            const bool nothing = About::autosaveState (project, autosave) == About::AutosaveState::none;
            project.replaceWithText (savedText);
            autosave.replaceWithText (autosavedText);
            // older than what was saved: nothing to recover
            autosave.setLastModificationTime (Time::getCurrentTime() - RelativeTime::minutes (10));
            project.setLastModificationTime (Time::getCurrentTime() - RelativeTime::minutes (5));
            const bool stale = About::autosaveState (project, autosave) == About::AutosaveState::stale;
            // newer: offered; newer but not a project: damaged, never offered
            autosave.setLastModificationTime (Time::getCurrentTime());
            const bool offered = About::autosaveState (project, autosave) == About::AutosaveState::offer;
            autosave.replaceWithText ("half a file <EDI");
            const bool damaged = About::autosaveState (project, autosave) == About::AutosaveState::damaged;
            File kept;
            const bool refuses = ! About::recoverAutosave (project, autosave, kept) && project.loadFileAsString() == savedText && autosave.existsAsFile() && kept == File();
            // recovering puts the autosave in place and keeps what was saved beside it, twice over if need be
            autosave.replaceWithText (autosavedText);
            const bool first = About::recoverAutosave (project, autosave, kept) && project.loadFileAsString() == autosavedText && ! autosave.existsAsFile()
                               && kept.getFileName() == "Song (before recovery).tracktionedit" && kept.loadFileAsString() == savedText;
            autosave.replaceWithText ("<EDIT name=\"autosaved again\"/>");
            File kept2;
            const bool second = About::recoverAutosave (project, autosave, kept2) && kept2 != kept && kept2.loadFileAsString() == autosavedText && kept.loadFileAsString() == savedText
                                && project.loadFileAsString().contains ("autosaved again");
            // a project that was never saved: there is nothing to keep, and it still recovers
            auto fresh = rdir.getChildFile ("New.tracktionedit"), freshAuto = rdir.getChildFile ("New.autosave");
            freshAuto.replaceWithText (autosavedText);
            File kept3;
            const bool neverSaved = About::autosaveState (fresh, freshAuto) == About::AutosaveState::offer && About::recoverAutosave (fresh, freshAuto, kept3) && kept3 == File() && fresh.loadFileAsString() == autosavedText;
            rep.check (nothing && stale && offered && damaged && refuses && first && second && neverSaved,
                       "Recovery after a crash (an autosave is offered only when newer and openable; recovering keeps what was last saved beside the project)",
                       String (nothing && stale && offered ? "" : "offered at the wrong time; ") + (damaged && refuses ? "" : "a damaged autosave is offered or used; ") + (first ? "" : "recover wrong; ")
                       + (second ? "" : "a second recovery loses the first kept version; ") + (neverSaved ? "" : "a project never saved does not recover; ") + "ok");
            rdir.deleteRecursively();
        }

        // --- v0.99.50: the demo song (a whole techno track) ---------------------------------------------------
        {
            rep.step ("demo song");
            auto demoFile = dir.getChildFile ("demo_song_test.tracktionedit");
            demoFile.deleteFile();
            auto demo = te::createEmptyEdit (engine, demoFile);
            te::EditFileOperations (*demo).save (true, true, false);
            DemoSong::build (*demo);
            te::EditFileOperations (*demo).save (true, true, false);
            auto dtracks = EngineHelpers::getUserTracks (*demo);
            StringArray wrong;
            auto expect = [&] (bool ok, const String& what) { if (! ok) wrong.add (what); };
            StringArray names;
            int notes = 0, emptyTracks = 0;
            for (auto t : dtracks)
            {
                names.add (t->getName());
                int n = 0;
                for (auto c : t->getClips()) if (auto mc = dynamic_cast<te::MidiClip*> (c)) n += mc->getSequence().getNumNotes();
                if (n == 0) ++emptyTracks;
                notes += n;
            }
            expect (names.joinIntoString (",") == "Kick,Hats,Perc,Bass,Tone,Blip,Atmos,Noise", "tracks: " + names.joinIntoString (","));
            expect (emptyTracks == 0 && notes > 4000, "notes: " + String (notes) + ", tracks without any: " + String (emptyTracks));
            const double songBars = demo->getLength().inSeconds() * DemoSong::bpm / 240.0;
            expect (std::abs (songBars - DemoSong::totalBars) < 1.0, "length " + String (songBars, 1) + " bars");
            expect (std::abs (demo->tempoSequence.getTempo (0)->getBpm() - DemoSong::bpm) < 0.01, "tempo");
            // the things it is meant to show are really there
            te::Plugin* acidSynth = dtracks.size() > DemoSong::tone ? dtracks[DemoSong::tone]->pluginList[0] : nullptr;
            {
                auto cutoff = acidSynth != nullptr ? acidSynth->getAutomatableParameterByID ("cutoff") : nullptr;   // (let go of before the project is closed)
                expect (cutoff != nullptr && cutoff->getCurve().getNumPoints() >= 20, "the tone's filter automation");
            }
            int ducked = 0;
            for (auto t : dtracks) for (auto p : t->pluginList) if (dynamic_cast<pulse::ProCompressor*> (p) != nullptr && p->getSidechainSourceID() == dtracks[0]->itemID) ++ducked;
            expect (ducked == 2, String (ducked) + " compressors keyed from the kick");
            expect (Routing::getReturnTracks (*demo).size() == 3 && Vca::all (*demo).size() == 2 && demo->getMasterPluginList().findFirstPluginOfType<pulse::Limiter>() != nullptr, "returns / VCAs / mastering chain");
            expect (SongKey::get (*demo) == 8 && Notes::ofProject (*demo).contains ("techno") && Notes::has (*dtracks[DemoSong::tone]), "key / notes");
            int slotClips = 0;
            for (auto t : dtracks) for (auto slot : t->getClipSlotList().getClipSlots()) if (slot->getClip() != nullptr) ++slotClips;
            expect (slotClips >= 16, String (slotClips) + " clips in the session view");
            expect (Sharing::missingFiles (*demo).isEmpty(), "it points at files");
            // and it sounds: the drop is loud but under full scale, the breakdown (no kick) quieter, the very start not silent
            demo->getTransport().ensureContextAllocated();
            auto renderBars = [&] (int from, int to, const String& tag, te::Track* only = nullptr)
            {
                te::Renderer::Parameters p (*demo);
                p.tracksToDo = only != nullptr ? te::toBitSet (juce::Array<te::Track*> { only }) : te::toBitSet (te::getAllTracks (*demo));
                if (only != nullptr) p.useMasterPlugins = false;
                p.destFile = dir.getChildFile ("demo_" + tag + ".wav");
                p.destFile.deleteFile();
                p.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
                p.bitDepth = 24;
                p.sampleRateForAudio = 48000.0;
                p.time = { EngineHelpers::beatToTime (*demo, from * 4.0), EngineHelpers::beatToTime (*demo, to * 4.0) };
                p.checkNodesForAudio = false;
                p.usePlugins = true; if (only == nullptr) p.useMasterPlugins = true;
                auto f = EditingOps::renderDetached ("demo " + tag, p);
                return f.existsAsFile() ? pulse::LoudnessAnalyser::measureFile (engine, f) : pulse::LoudnessAnalyser::Result();
            };
            const auto drop = renderBars (80, 88, "drop"), brk = renderBars (96, 104, "breakdown"), intro = renderBars (0, 8, "intro"), drop2 = renderBars (152, 160, "drop2");
            // each track on its own over the same eight bars of the second drop: the balance of the mix, in numbers
            String balance;
            for (auto t : dtracks) balance << t->getName() << " " << String (renderBars (152, 160, "solo", t).integrated, 1) << ", ";
            balance << "breakdown: ";
            for (auto t : dtracks) balance << t->getName() << " " << String (renderBars (96, 104, "solo", t).integrated, 1) << ", ";
            expect (drop.integrated > -18.0 && drop.integrated < -7.0 && drop.truePeakDb < -0.5f, "the drop: " + String (drop.integrated, 1) + " LUFS, peak " + String (drop.truePeakDb, 1));
            expect (drop2.integrated > -18.0 && drop2.integrated < -7.0 && drop2.truePeakDb < -0.5f, "the second drop: " + String (drop2.integrated, 1) + " LUFS, peak " + String (drop2.truePeakDb, 1));
            expect (brk.integrated < drop.integrated - 3.0 && brk.integrated > -40.0, "the breakdown: " + String (brk.integrated, 1) + " LUFS against the drop's " + String (drop.integrated, 1));
            expect (intro.integrated > -35.0 && intro.integrated < drop.integrated, "the intro: " + String (intro.integrated, 1) + " LUFS");
            rep.check (wrong.isEmpty(), "Demo song (a whole techno track: eight tracks, 208 bars, automation, sidechain, a rumble return, mastering; it renders at sensible levels)",
                       (wrong.isEmpty() ? String() : wrong.joinIntoString ("; ") + " | ") + String (notes) + " notes; intro " + String (intro.integrated, 1) + ", drop " + String (drop.integrated, 1) + " (peak " + String (drop.truePeakDb, 1)
                         + "), breakdown " + String (brk.integrated, 1) + ", drop II " + String (drop2.integrated, 1) + " LUFS (peak " + String (drop2.truePeakDb, 1) + "); each track alone in drop II: " + balance);
            demo = nullptr;
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
                    const auto note = sb->getStatus();
                    rep.check (note.containsIgnoreCase ("fault 0xC0000005") && note.contains ("while running"),
                               "A crashed helper says what happened and where", note);
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
                    return EditingOps::renderDetached ("duck", rp);
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
            EditingOps::renderDetached ("stress", rp);
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
                if (t == pulse::Tuner::xmlTypeName || t == pulse::Spectrum::xmlTypeName || t == "fxrack") continue;
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
                        EditingOps::renderDetached (*ed, out);
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

            // linear phase: the same levels as the natural EQ, a response that is the same forwards and backwards,
            // and a delay that is what the EQ says it is
            {
                for (int b = 0; b < EQ::numBands; ++b) setB (b, EQ::pOn, 0);
                eq->set (eq->g (EQ::gAutoGain), 0.0f);
                setB (0, EQ::pOn, 1); setB (0, EQ::pType, EQ::lowCut); setB (0, EQ::pFreq, 200); setB (0, EQ::pSlope, 3); setB (0, EQ::pQ, 0.7071f); setB (0, EQ::pPlace, EQ::stereo);
                setB (2, EQ::pOn, 1); setB (2, EQ::pType, EQ::bell); setB (2, EQ::pFreq, 1000); setB (2, EQ::pGain, 6); setB (2, EQ::pQ, 1.0f); setB (2, EQ::pPlace, EQ::stereo);
                setB (4, EQ::pOn, 1); setB (4, EQ::pType, EQ::highShelf); setB (4, EQ::pFreq, 6000); setB (4, EQ::pGain, -9); setB (4, EQ::pSlope, 1); setB (4, EQ::pPlace, EQ::stereo);
                const double freqs[] = { 100.0, 200.0, 1000.0, 3000.0, 12000.0 };
                float worst = 0.0f, worstDeep = 0.0f;
                String levels;
                for (int mode = 1; mode <= 2; ++mode)
                    for (double f : freqs)
                    {
                        eq->set (eq->g (EQ::gPhase), 0.0f);
                        const float natural = run (f, f, 0.5f, 0.5f, 0.4).first;
                        eq->set (eq->g (EQ::gPhase), (float) mode);
                        const float linear = run (f, f, 0.5f, 0.5f, mode == 1 ? 0.6 : 1.4).first;
                        (mode == 1 ? worst : worstDeep) = jmax (mode == 1 ? worst : worstDeep, std::abs (natural - linear));
                        if (mode == 1) levels << String (roundToInt (f)) << " Hz " << String (natural - Decibels::gainToDecibels (0.5f), 1) << " / " << String (linear - Decibels::gainToDecibels (0.5f), 1) << "; ";
                    }
                // a click: the response around its middle is a mirror of itself, and the middle is where the EQ says
                eq->set (eq->g (EQ::gPhase), 1.0f);
                const int block = 480, blocks = 40;
                eq->initialise ({ te::TimePosition(), sr, block });
                const int late = roundToInt (eq->getLatencySeconds() * sr);
                std::vector<float> got ((size_t) (block * blocks));
                AudioBuffer<float> audio (2, block);
                for (int k = 0; k < blocks; ++k)
                {
                    audio.clear();
                    if (k == 0) { audio.setSample (0, 100, 1.0f); audio.setSample (1, 100, 1.0f); }
                    te::MidiMessageArray midi;
                    te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) }, true, false, true, false);
                    eq->applyToBuffer (fc);
                    for (int i = 0; i < block; ++i) got[(size_t) (k * block + i)] = audio.getSample (0, i);
                }
                eq->deinitialise();
                int peakAt = 0;
                for (int i = 0; i < (int) got.size(); ++i) if (std::abs (got[(size_t) i]) > std::abs (got[(size_t) peakAt])) peakAt = i;
                float mirror = 0.0f;
                for (int i = 1; i < 2000; ++i) mirror = jmax (mirror, std::abs (got[(size_t) (peakAt + i)] - got[(size_t) (peakAt - i)]));
                // left only, and side only, in linear phase
                for (int b = 0; b < EQ::numBands; ++b) setB (b, EQ::pOn, 0);
                setB (2, EQ::pOn, 1); setB (2, EQ::pGain, 9); setB (2, EQ::pPlace, EQ::left);
                const auto linLR = run (1000, 1000, 0.5f, 0.5f, 0.6);
                setB (2, EQ::pPlace, EQ::side);
                const float linMonoSide = run (1000, 1000, 0.5f, 0.5f, 0.6).first - Decibels::gainToDecibels (0.5f);
                setB (2, EQ::pPlace, EQ::stereo); setB (2, EQ::pGain, 0);
                // half wet: the untreated sound is kept in step, so a flat EQ at 50% is still the sound itself
                eq->set (eq->g (EQ::gMix), 0.5f);
                const float halfWet = run (1000, 1000, 0.5f, 0.5f, 0.6).first - Decibels::gainToDecibels (0.5f);
                eq->set (eq->g (EQ::gMix), 1.0f);
                eq->set (eq->g (EQ::gPhase), 0.0f);
                eq->syncPhaseMode();
                setB (0, EQ::pOn, 1);   // (as the checks after this one found it)
                const bool noDelayNatural = eq->getLatencySeconds() == 0.0;
                const bool linOk = worst < 1.0f && worstDeep < 0.4f &&peakAt == 100 + late && late == 6144 && mirror < 1.0e-4f && linLR.first > 8.0f - 6.03f && near (linLR.second, -6.02, 0.2)
                                   && near (linMonoSide, 0.0, 0.2) && near (halfWet, 0.0, 0.1) && noDelayNatural;
                rep.check (linOk, "Prism EQ in linear phase (same levels as natural, a mirrored response, the delay it reports, left and side bands, dry/wet in step)",
                           "natural / linear: " + levels + "worst difference " + String (worst, 2) + " dB (deep: " + String (worstDeep, 2) + "); click comes out " + String (peakAt - 100) + " samples late (says " + String (late) + "), mirror error " + String (mirror, 6)
                             + "; left " + String (linLR.first, 1) + "/" + String (linLR.second, 1) + ", side on mono " + String (linMonoSide, 2) + ", half wet " + String (halfWet, 2));
            }

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
                // the selected band's controls have a strip of their own under the display: they never lie over the spectrum,
                // at the window's usual size or its smallest, with the dynamic part and the movement showing their knobs too
                {
                    bool clear = true, fits = true;
                    setB (3, EQ::pModSrc, 1);
                    for (auto [w, h] : { std::pair<int, int> { 1100, 760 }, { 1000, 680 }, { 1900, 1100 } })
                    {
                        view.setSize (w, h);
                        view.showBand (3);
                        const auto strip = view.bandStripBounds();
                        clear = clear && ! strip.isEmpty() && ! strip.intersects (view.getGraph().getBounds()) && view.getGraph().getHeight() > h / 2;
                        fits = fits && view.bandStripFits();
                    }
                    setB (3, EQ::pModSrc, 0);
                    view.setSize (1100, 760);
                    view.showBand (3);
                    rep.check (clear && fits, "Prism EQ: the band's controls sit under the display, never over the spectrum, and fit at the smallest window", String (clear ? "" : "over the display; ") + (fits ? "ok" : "a control is cut off"));
                }
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

            // --- gate / expander / ducker, transient shaper, de-esser -------------------------------------
            {
                auto mk = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (pl, 0, nullptr); return pl; };
                auto gp = mk (pulse::Gate::xmlTypeName), tp2 = mk (pulse::TransientShaper::xmlTypeName), dp2 = mk (pulse::DeEsser::xmlTypeName);
                auto* gate = dynamic_cast<pulse::Gate*> (gp.get());
                auto* shaper = dynamic_cast<pulse::TransientShaper*> (tp2.get());
                auto* deEss = dynamic_cast<pulse::DeEsser*> (dp2.get());
                using G = pulse::Gate;
                using T = pulse::TransientShaper;
                using D = pulse::DeEsser;
                const float a20 = Decibels::decibelsToGain (-20.0f), a50 = Decibels::decibelsToGain (-50.0f), a43 = Decibels::decibelsToGain (-43.0f);

                // gate: quiet is shut by the range, loud passes untouched
                gate->set (G::pThreshold, -40); gate->set (G::pRange, 60); gate->set (G::pHysteresis, 0); gate->set (G::pHold, 0); gate->set (G::pRelease, 20);
                const float gQuiet = runFx (*gate, sine (1000, a50), 0.6), gLoud = runFx (*gate, sine (1000, a20), 0.6);
                // hysteresis: after a loud start, 3 dB under the threshold stays open with 6 dB of hysteresis and shuts with none
                auto loudThenLow = [a20, a43] (int, double tm) { return (tm < 0.2 ? a20 : a43) * (float) std::sin (MathConstants<double>::twoPi * 1000.0 * tm); };
                const float noHyst = runFx (*gate, loudThenLow, 0.8);
                gate->set (G::pHysteresis, 6);
                const float withHyst = runFx (*gate, loudThenLow, 0.8);
                // expander: 10 dB under the threshold at 1 : 2 comes out 10 dB lower still
                gate->set (G::pMode, 1); gate->set (G::pRatio, 2);
                const float gExp = runFx (*gate, sine (1000, a50), 0.6);
                // ducker: turned down by the range while the key is above the threshold
                gate->set (G::pMode, 2); gate->set (G::pRange, 12);
                const float gDuck = runFx (*gate, sine (1000, a20), 0.6), gDuckQuiet = runFx (*gate, sine (1000, a50), 0.6);
                gate->set (G::pLookahead, 5);
                const double gLat = gate->getLatencySeconds();
                gate->set (G::pLookahead, 0); gate->set (G::pMode, 0); gate->set (G::pRange, 60);
                const bool gateOk = gQuiet < -105.0f && std::abs (gLoud + 20.0f) < 0.3f && noHyst < -95.0f && std::abs (withHyst + 43.0f) < 0.5f
                                    && std::abs (gExp + 60.0f) < 1.0f && std::abs (gDuck + 32.0f) < 0.6f && std::abs (gDuckQuiet + 50.0f) < 0.5f
                                    && std::abs (gLat - 0.005) < 1e-6 && std::abs (gate->staticGainDb (-50.0f) + 60.0f) < 0.01f;
                rep.check (gateOk, "Pulse Gate (gate, hysteresis, expander, ducker, lookahead latency)",
                           "quiet " + String (gQuiet, 1) + ", loud " + String (gLoud, 1) + ", 3 dB under: " + String (noHyst, 1) + " / with hysteresis " + String (withHyst, 1)
                           + ", expander " + String (gExp, 1) + ", ducker " + String (gDuck, 1) + " / " + String (gDuckQuiet, 1));

                // transient shaper: a steady low note is left alone; the start of a burst follows Attack; a dying tail follows Sustain
                shaper->set (T::pClip, 0); shaper->set (T::pAttack, 1.0f); shaper->set (T::pSustain, 1.0f);
                const float steady = runFx (*shaper, sine (60, 0.25f), 1.5) - Decibels::gainToDecibels (0.25f);
                auto bursts = [] (int, double tm) { const double ph = std::fmod (tm, 0.5); return ph < 0.1 ? 0.1f * (float) std::sin (MathConstants<double>::twoPi * 1000.0 * tm) : 0.0f; };
                auto hitPeak = [&] (float attack)
                {
                    shaper->set (T::pAttack, attack); shaper->set (T::pSustain, 0.0f);
                    AudioBuffer<float> kept;
                    runFx (*shaper, bursts, 1.2, &kept);
                    return Decibels::gainToDecibels (kept.getMagnitude (0, (int) (1.003 * sr), (int) (0.008 * sr)), -100.0f) + 20.0f;   // against the 0.1 that went in (3 ms late)
                };
                const float hitUp = hitPeak (1.0f), hitFlat = hitPeak (0.0f), hitDown = hitPeak (-1.0f);
                auto dying = [] (int, double tm) { const double ph = std::fmod (tm, 1.0); return 0.4f * (float) (std::exp (-ph * 18.0) * std::sin (MathConstants<double>::twoPi * 220.0 * tm)); };
                auto tailLevel = [&] (float sustain)
                {
                    shaper->set (T::pAttack, 0.0f); shaper->set (T::pSustain, sustain);
                    AudioBuffer<float> kept;
                    runFx (*shaper, dying, 1.4, &kept);
                    return Decibels::gainToDecibels (kept.getRMSLevel (0, (int) (1.15 * sr), (int) (0.1 * sr)), -100.0f);
                };
                const float tailUp = tailLevel (1.0f), tailFlat = tailLevel (0.0f), tailDown = tailLevel (-1.0f);
                // dry/wet at zero gives back exactly what went in
                shaper->set (T::pAttack, 1.0f); shaper->set (T::pMix, 0.0f);
                AudioBuffer<float> dryKept;
                runFx (*shaper, bursts, 0.3, &dryKept);
                float dryErr = 0;
                const int late = (int) (0.003 * sr);   // its lookahead, which it reports as latency
                for (int i = late; i < dryKept.getNumSamples(); ++i) dryErr = jmax (dryErr, std::abs (dryKept.getSample (0, i) - bursts (0, (i - late) / sr)));
                shaper->set (T::pMix, 1.0f); shaper->set (T::pAttack, 0.3f); shaper->set (T::pSustain, 0.0f); shaper->set (T::pClip, 1);
                const bool shaperOk = std::abs (steady) < 0.6f && std::abs (hitFlat) < 0.2f && hitUp > hitFlat + 5.0f && hitDown < hitFlat - 5.0f
                                      && tailUp > tailFlat + 4.0f && tailDown < tailFlat - 4.0f && dryErr < 1.0e-6f && std::abs (shaper->getLatencySeconds() - 0.003) < 1e-9;
                rep.check (shaperOk, "Transient Shaper (steady notes untouched, attack and sustain both ways, dry/wet)",
                           "steady 60 Hz " + String (steady, 2) + " dB, hit " + String (hitDown, 1) + " / " + String (hitFlat, 1) + " / " + String (hitUp, 1)
                           + " dB, tail " + String (tailDown, 1) + " / " + String (tailFlat, 1) + " / " + String (tailUp, 1) + " dB");

                // de-esser: a loud "s" tone comes down by the range, a low tone is left alone, and split band keeps the lows of a mix
                auto toneDb = [&] (const AudioBuffer<float>& b, double hz)   // level of one frequency over the last 0.1 s
                {
                    const int len = (int) (0.1 * sr), from = b.getNumSamples() - len;
                    double re = 0, im = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const double ph = MathConstants<double>::twoPi * hz * (from + i) / sr;
                        re += b.getSample (0, from + i) * std::cos (ph); im += b.getSample (0, from + i) * std::sin (ph);
                    }
                    return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / len), -120.0f);
                };
                deEss->set (D::pFreq, 6500); deEss->set (D::pThreshold, -30); deEss->set (D::pRange, 8);
                const float a10b = Decibels::decibelsToGain (-10.0f), a16 = Decibels::decibelsToGain (-16.0f);
                const float ess = runFx (*deEss, sine (6500, a10b), 0.6), lowTone = runFx (*deEss, sine (300, a10b), 0.6);
                auto mixSig = [a16] (int, double tm) { return a16 * (float) (std::sin (MathConstants<double>::twoPi * 300.0 * tm) + std::sin (MathConstants<double>::twoPi * 6500.0 * tm)); };
                AudioBuffer<float> splitOut, wideOut;
                runFx (*deEss, mixSig, 0.6, &splitOut);
                deEss->set (D::pMode, 1);
                runFx (*deEss, mixSig, 0.6, &wideOut);
                const float essWide = runFx (*deEss, sine (6500, a10b), 0.6);
                deEss->set (D::pMode, 0);
                const float splitLow = toneDb (splitOut, 300), splitHigh = toneDb (splitOut, 6500), wideLow = toneDb (wideOut, 300);
                const bool deEssOk = std::abs (ess + 18.0f) < 1.2f && std::abs (lowTone + 10.0f) < 0.3f && std::abs (essWide + 18.0f) < 0.6f
                                     && std::abs (splitLow + 16.0f) < 0.4f && splitHigh < -22.0f && wideLow < -22.0f && deEss->reductionDb.load() > 6.0f;
                rep.check (deEssOk, "De-esser (takes the range off an \"s\", leaves lows alone, split and wide band)",
                           "6.5 kHz " + String (ess, 1) + " (wide " + String (essWide, 1) + "), 300 Hz " + String (lowTone, 1) + ", in a mix: split keeps 300 Hz at " + String (splitLow, 1)
                           + " and 6.5 kHz at " + String (splitHigh, 1) + ", wide takes 300 Hz to " + String (wideLow, 1));

                // their windows: the settings are points on the graph, and every parameter also has a control
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    DynamicsUI::GateView gv (*gate);
                    DynamicsUI::TransientView tv (*shaper);
                    DynamicsUI::DeEsserView dv (*deEss);
                    for (Component* c : { (Component*) &gv, (Component*) &tv, (Component*) &dv }) c->setSize (1000, 520);
                    const float th0 = gate->get (G::pThreshold), rg0 = gate->get (G::pRange), at0 = shaper->get (T::pAttack), su0 = shaper->get (T::pSustain), ln0 = shaper->get (T::pLength);
                    const float fr0 = deEss->get (D::pFreq), dth0 = deEss->get (D::pThreshold);
                    gv.dragHandle (0, 0.0f, -40.0f);    // threshold up
                    gv.dragHandle (2, 0.0f, -30.0f);    // range smaller
                    tv.dragHandle (0, 0.0f, -40.0f);    // more attack
                    tv.dragHandle (1, 0.0f, 30.0f);     // less sustain
                    tv.dragHandle (2, 60.0f, 0.0f);     // longer hit
                    dv.dragHandle (0, -80.0f, -30.0f);  // lower frequency, higher threshold
                    const bool handlesOk = gate->get (G::pThreshold) > th0 + 2.0f && gate->get (G::pRange) < rg0 - 2.0f
                                           && shaper->get (T::pAttack) > at0 + 0.1f && shaper->get (T::pSustain) < su0 - 0.1f && shaper->get (T::pLength) > ln0 + 3.0f
                                           && deEss->get (D::pFreq) < fr0 - 300.0f && deEss->get (D::pThreshold) > dth0 + 1.0f;
                    StringArray missing;
                    int built = 0;
                    for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { gate, shaper, deEss })
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* ch : c.getChildren()) walk (*ch);
                        };
                        walk (ed);
                        for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                        auto img = ed.createComponentSnapshot (ed.getLocalBounds());
                        auto f = dir.getChildFile ("dyn_" + File::createLegalFileName (pp->getName()) + ".png");
                        f.deleteFile();
                        if (auto os = f.createOutputStream()) { PNGImageFormat png; if (png.writeImageToStream (img, *os)) ++built; }
                        if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty() && built == 3, "Gate, Transient Shaper and De-esser windows: points on the graph, and a control for every parameter",
                               missing.isEmpty() ? "threshold " + String (roundToInt (th0)) + " -> " + String (roundToInt (gate->get (G::pThreshold))) + " dB, attack " + String (at0, 2) + " -> " + String (shaper->get (T::pAttack), 2)
                                                   + ", frequency " + String (roundToInt (fr0)) + " -> " + String (roundToInt (deEss->get (D::pFreq))) + " Hz"
                                                 : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                gp = nullptr; tp2 = nullptr; dp2 = nullptr;
            }

            // --- Pulse Space (convolution reverb) ---------------------------------------------------------
            {
                auto sp2 = edit->getPluginCache().createNewPlugin (pulse::ConvolutionReverb::xmlTypeName, {});
                t->pluginList.insertPlugin (sp2, 0, nullptr);
                auto* space = dynamic_cast<pulse::ConvolutionReverb*> (sp2.get());
                using C = pulse::ConvolutionReverb;
                auto click = [] (int, double tm) { return tm == 0.0 ? 1.0f : 0.0f; };
                auto rmsDb = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = jlimit (0, b.getNumSamples() - 1, (int) (from * sr)), n = jlimit (1, b.getNumSamples() - a, (int) (len * sr));
                    return Decibels::gainToDecibels (b.getRMSLevel (0, a, n), -140.0f);
                };
                std::array<float, C::shapeSize> shape {};
                double secs = 0, secsBig = 0, secsShort = 0;
                AudioBuffer<float> out;

                // the Hall: a click turns into a tail that dies away; Size stretches it, Decay shortens it
                space->set (C::pSpace, 6); space->set (C::pMix, 1.0f); space->set (C::pLowCut, 20); space->set (C::pHighCut, 20000);
                runFx (*space, click, 3.2, &out);
                space->getShape (shape, secs);
                const float early = rmsDb (out, 0.1, 0.2), late = rmsDb (out, 1.6, 0.2), gone = rmsDb (out, 3.0, 0.15);
                space->set (C::pSize, 2.0f);
                runFx (*space, click, 0.1);
                space->getShape (shape, secsBig);
                space->set (C::pSize, 1.0f); space->set (C::pDecay, 0.5f);
                runFx (*space, click, 0.1);
                space->getShape (shape, secsShort);
                space->set (C::pDecay, 1.0f);
                // predelay: nothing comes out before it
                space->set (C::pPredelay, 100.0f);
                runFx (*space, click, 0.4, &out);
                const float beforePre = rmsDb (out, 0.0, 0.095), afterPre = rmsDb (out, 0.1, 0.1);
                space->set (C::pPredelay, 0.0f);
                // reverse: it swells instead of dying away
                space->set (C::pReverse, 1.0f);
                runFx (*space, click, 3.2, &out);
                const float revStart = rmsDb (out, 0.3, 0.2), revEnd = rmsDb (out, 2.4, 0.2);
                space->set (C::pReverse, 0.0f);
                // every space comes out about as loud as what goes in (steady noise at -20 dB)
                String levels;
                float worstLevel = 0;
                for (int which : { 0, 4, 5, 6, 10 })
                {
                    space->set (C::pSpace, (float) which);
                    Random rng (99);
                    runFx (*space, [&rng] (int, double) { return (rng.nextFloat() * 2.0f - 1.0f) * 0.1732f; }, 3.6, &out);   // -20 dB RMS
                    const float lv = rmsDb (out, 3.0, 0.6) + 20.0f;
                    levels << pulse::SpaceIR::spaces[which].name << " " << String (lv, 1) << "  ";
                    worstLevel = jmax (worstLevel, std::abs (lv));
                }
                // dry/wet at zero gives back what went in
                space->set (C::pSpace, 6); space->set (C::pMix, 0.0f);
                auto tone = sine (440, 0.3f);
                runFx (*space, tone, 0.2, &out);
                float dryErr = 0;
                for (int i = 0; i < out.getNumSamples(); ++i) dryErr = jmax (dryErr, std::abs (out.getSample (0, i) - tone (0, i / sr)));
                space->set (C::pMix, 1.0f);
                // an audio file as the space; a file that is not there falls back to the Hall
                auto irFile = dir.getChildFile ("space_test.wav");
                irFile.deleteFile();
                {
                    AudioBuffer<float> ir (2, 24000);
                    Random rng (5);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < 24000; ++i) ir.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 4000.0f));
                    WavAudioFormat wav;
                    if (auto os = irFile.createOutputStream())
                        if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), 48000.0, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (ir, 0, 24000);
                }
                double secsFile = 0, secsMissing = 0;
                space->setFile (irFile);
                runFx (*space, click, 0.8, &out);
                space->getShape (shape, secsFile);
                const bool fileOk = space->usingFile() && std::abs (secsFile - 0.5) < 0.01 && rmsDb (out, 0.0, 0.1) > -60.0f && rmsDb (out, 0.6, 0.15) < -100.0f;
                space->setFile (dir.getChildFile ("not_there.wav"));
                runFx (*space, click, 0.3, &out);
                space->getShape (shape, secsMissing);
                const bool missingOk = ! space->usingFile() && std::abs (secsMissing - secs) < 0.01 && rmsDb (out, 0.1, 0.1) > -80.0f;
                space->setFile ({});
                const bool spaceOk = early > late + 12.0f && late > -90.0f && gone < late - 20.0f && std::abs (secs - 2.81) < 0.02 && std::abs (secsBig - 5.57) < 0.03 && std::abs (secsShort - 1.43) < 0.02
                                     && beforePre < -120.0f && afterPre > -70.0f && revEnd > revStart + 10.0f && worstLevel < 1.5f && dryErr < 1.0e-6f && fileOk && missingOk
                                     && space->getLatencySeconds() == 0.0;
                rep.check (spaceOk, "Pulse Space (tail dies away, size, decay, predelay, reverse, even loudness, a file as the space)",
                           "Hall " + String (secs, 2) + " s (size 200% " + String (secsBig, 2) + ", decay 50% " + String (secsShort, 2) + "), tail " + String (roundToInt (early)) + " > " + String (roundToInt (late)) + " > " + String (roundToInt (gone))
                           + " dB, before predelay " + String (roundToInt (beforePre)) + ", reverse " + String (roundToInt (revStart)) + " -> " + String (roundToInt (revEnd)) + ", levels: " + levels.trim()
                           + ", file " + String (secsFile, 2) + " s" + (fileOk ? "" : " WRONG") + (missingOk ? "" : ", missing file WRONG"));

                // its window
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    SpaceUI::ConvolutionView view (*space);
                    view.setSize (1000, 560);
                    Image img (Image::ARGB, 1000, 560, true);
                    { Graphics g (img); view.paint (g); }
                    const float pre0 = space->get (C::pPredelay), size0 = space->get (C::pSize), decay0 = space->get (C::pDecay), low0 = space->get (C::pLowCut), high0 = space->get (C::pHighCut);
                    view.dragHandle (0, 40.0f, 0.0f);
                    view.dragHandle (1, 80.0f, 0.0f);
                    view.dragDecay (40.0f);
                    view.dragHandle (3, 60.0f, 0.0f);
                    view.dragHandle (4, -60.0f, 0.0f);
                    const bool handlesOk = space->get (C::pPredelay) > pre0 + 20.0f && space->get (C::pSize) > size0 + 0.05f && space->get (C::pDecay) < decay0 - 0.05f
                                           && space->get (C::pLowCut) > low0 + 10.0f && space->get (C::pHighCut) < high0 - 500.0f;
                    DeviceEditors::Editor ed (evs, *space);
                    ed.setSize (1100, 760);
                    std::set<te::AutomatableParameter*> seen;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                        for (auto* ch : c.getChildren()) walk (*ch);
                    };
                    walk (ed);
                    StringArray missing;
                    for (int i = 0; i < space->numParams(); ++i) if (! seen.count (&*space->param (i))) missing.add (space->param (i)->paramID);
                    rep.check (handlesOk && missing.isEmpty() && DeviceEditors::hasFullEditor (*space) && view.getNumHandles() == 5,
                               "Pulse Space window: predelay, length, decay and tone are points on the graph, and every parameter has a control",
                               missing.isEmpty() ? "size " + String (size0, 2) + " -> " + String (space->get (C::pSize), 2) + ", decay " + String (decay0, 2) + " -> " + String (space->get (C::pDecay), 2)
                                                 : missing.joinIntoString (", "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                sp2 = nullptr;
            }

            // --- chorus, phaser-flanger, auto pan ---------------------------------------------------------
            {
                auto mk = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (pl, 0, nullptr); return pl; };
                auto cp2 = mk (pulse::Chorus::xmlTypeName), pp2 = mk (pulse::PhaserFlanger::xmlTypeName), ap2 = mk (pulse::AutoPan::xmlTypeName);
                auto* chorus = dynamic_cast<pulse::Chorus*> (cp2.get());
                auto* phaser = dynamic_cast<pulse::PhaserFlanger*> (pp2.get());
                auto* autoPan = dynamic_cast<pulse::AutoPan*> (ap2.get());
                using CH = pulse::Chorus;
                using PF = pulse::PhaserFlanger;
                using AP = pulse::AutoPan;
                AudioBuffer<float> out;
                auto tone = sine (440, 0.25f);
                auto maxDiff = [&] (const AudioBuffer<float>& b, int chA, int chB, bool againstTone)
                {
                    float d = 0;
                    for (int i = 0; i < b.getNumSamples(); ++i) d = jmax (d, std::abs (b.getSample (chA, i) - (againstTone ? tone (0, i / sr) : b.getSample (chB, i))));
                    return d;
                };

                // chorus: dry/wet at zero is the input; the wet copy moves (it differs from the input); width opens the sides; feedback stays stable
                chorus->set (CH::pMix, 0.0f);
                runFx (*chorus, tone, 0.5, &out);
                const float chDry = maxDiff (out, 0, 0, true);
                chorus->set (CH::pMix, 0.5f);
                runFx (*chorus, tone, 1.0, &out);
                const float chWet = maxDiff (out, 0, 0, true), chSides = maxDiff (out, 0, 1, false);
                chorus->set (CH::pWidth, 0.0f);
                runFx (*chorus, tone, 1.0, &out);
                const float chMono = maxDiff (out, 0, 1, false);
                chorus->set (CH::pWidth, 1.0f); chorus->set (CH::pMode, 1); chorus->set (CH::pFeedback, 0.9f); chorus->set (CH::pAmount, 1.0f);
                const float chLoud = runFx (*chorus, tone, 3.0);
                // vibrato: only the moving copy, so the pitch really wobbles: the level stays, the waveform no longer lines up
                chorus->set (CH::pMode, 2); chorus->set (CH::pFeedback, 0.0f); chorus->set (CH::pRate, 5.0f);
                const float vibLevel = runFx (*chorus, tone, 1.0, &out) - Decibels::gainToDecibels (0.25f);
                chorus->set (CH::pMode, 0); chorus->set (CH::pRate, 0.8f); chorus->set (CH::pAmount, 0.5f);
                const bool chorusOk = chDry < 1.0e-6f && chWet > 0.02f && chSides > 0.01f && chMono < 1.0e-6f && chLoud < 12.0f && chLoud > -40.0f && std::abs (vibLevel) < 0.5f && chorus->voices() == 1;
                rep.check (chorusOk, "Pulse Chorus (dry at 0%, moving copy, width, stable feedback, vibrato)",
                           "differs from the input by " + String (chWet, 3) + ", sides differ by " + String (chSides, 3) + " (width 0: " + String (chMono, 6) + "), ensemble with 90% feedback peaks at "
                           + String (chLoud, 1) + " dB, vibrato level " + String (vibLevel, 2) + " dB");

                // phaser, standing still: two stages cancel the centre frequency and leave the lows; more feedback keeps it stable
                phaser->set (PF::pAmount, 0.0f); phaser->set (PF::pCentre, 1000.0f); phaser->set (PF::pFeedback, 0.0f); phaser->set (PF::pStages, 0); phaser->set (PF::pMix, 0.5f);
                const float a10c = Decibels::decibelsToGain (-10.0f);
                const float phNotch = runFx (*phaser, sine (1000, a10c), 0.5) + 10.0f, phLow = runFx (*phaser, sine (60, a10c), 0.5) + 10.0f;
                const float phShown = Decibels::gainToDecibels (phaser->responseAt (1000.0, 1000.0), -100.0f);
                // flanger: a comb: halfway between the multiples of the centre is cancelled, the multiples pass
                phaser->set (PF::pMode, 1);
                const float flNotch = runFx (*phaser, sine (500, a10c), 0.5) + 10.0f, flPeak = runFx (*phaser, sine (1000, a10c), 0.5) + 10.0f;
                phaser->set (PF::pFeedback, 0.95f); phaser->set (PF::pAmount, 1.0f); phaser->set (PF::pRate, 3.0f);
                Random prng (3);
                const float flLoud = runFx (*phaser, [&prng] (int, double) { return (prng.nextFloat() * 2.0f - 1.0f) * 0.1f; }, 3.0);
                phaser->set (PF::pMode, 0); phaser->set (PF::pFeedback, 0.4f); phaser->set (PF::pAmount, 0.6f); phaser->set (PF::pRate, 0.4f); phaser->set (PF::pCentre, 700.0f); phaser->set (PF::pStages, 1);
                const bool phaserOk = phNotch < -30.0f && std::abs (phLow) < 1.0f && phShown < -30.0f && flNotch < -25.0f && std::abs (flPeak) < 0.5f && flLoud < 12.0f;
                rep.check (phaserOk, "Phaser-Flanger (phaser notch at the centre, flanger comb, stable at full feedback, the display agrees)",
                           "phaser 1 kHz " + String (phNotch, 1) + " dB (drawn " + String (phShown, 1) + "), 60 Hz " + String (phLow, 2) + "; flanger 500 Hz " + String (flNotch, 1) + ", 1 kHz " + String (flPeak, 2)
                           + "; noise at 95% feedback peaks at " + String (flLoud, 1) + " dB");

                // auto pan: the two sides take turns and always add up to the same; phase 0 is a tremolo; amount 0 does nothing
                autoPan->set (AP::pAmount, 1.0f); autoPan->set (AP::pRate, 4.0f); autoPan->set (AP::pPhase, 180.0f); autoPan->set (AP::pSmooth, 0.0f);
                auto dc = [] (int, double) { return 0.5f; };
                runFx (*autoPan, dc, 1.0, &out);
                float sumErr = 0, lMin = 1, lMax = 0;
                for (int i = 4800; i < out.getNumSamples(); ++i)
                {
                    sumErr = jmax (sumErr, std::abs (out.getSample (0, i) + out.getSample (1, i) - 0.5f));
                    lMin = jmin (lMin, out.getSample (0, i)); lMax = jmax (lMax, out.getSample (0, i));
                }
                autoPan->set (AP::pPhase, 0.0f);
                runFx (*autoPan, dc, 1.0, &out);
                const float tremSides = maxDiff (out, 0, 1, false);
                autoPan->set (AP::pShape, 4);   // square: the steps are rounded off, never a jump from one sample to the next
                runFx (*autoPan, dc, 1.0, &out);
                float jump = 0;
                for (int i = 4801; i < out.getNumSamples(); ++i) jump = jmax (jump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
                autoPan->set (AP::pShape, 0); autoPan->set (AP::pAmount, 0.0f);
                runFx (*autoPan, tone, 0.3, &out);
                const float panOff = maxDiff (out, 0, 0, true);
                autoPan->set (AP::pAmount, 0.6f); autoPan->set (AP::pPhase, 180.0f); autoPan->set (AP::pSmooth, 0.1f); autoPan->set (AP::pRate, 2.0f);
                const double syncedHz = autoPan->rateNow (2.0f, 3), bpmNow = edit->tempoSequence.getTempo (0)->getBpm();
                const bool panOk = sumErr < 0.01f && lMin < 0.02f && lMax > 0.48f && tremSides < 1.0e-6f && jump < 0.05f && panOff < 1.0e-6f && std::abs (syncedHz - bpmNow / 60.0) < 0.01;
                rep.check (panOk, "Auto Pan (sides take turns and add up, tremolo at phase 0, no clicks on a square, tempo sync)",
                           "left swings " + String (lMin, 2) + " to " + String (lMax, 2) + " of 0.50, sum off by " + String (sumErr, 4) + ", biggest step on a square " + String (jump, 3)
                           + ", 1/4 at " + String (bpmNow, 0) + " bpm = " + String (syncedHz, 2) + " Hz");

                // their windows
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    ModUI::ChorusView cv (*chorus);
                    ModUI::PhaserView pv (*phaser);
                    ModUI::AutoPanView av (*autoPan);
                    Image img (Image::ARGB, 1000, 520, true);
                    for (Component* c : { (Component*) &cv, (Component*) &pv, (Component*) &av }) { c->setSize (1000, 520); Graphics g (img); c->paint (g); }
                    const float d0 = chorus->get (CH::pDelay), am0 = chorus->get (CH::pAmount), r0 = chorus->get (CH::pRate), c0 = phaser->get (PF::pCentre), f0 = phaser->get (PF::pFeedback), pa0 = phaser->get (PF::pAmount);
                    const float aa0 = autoPan->get (AP::pAmount), ar0 = autoPan->get (AP::pRate), ap0 = autoPan->get (AP::pPhase);
                    cv.dragHandle (0, 0.0f, -40.0f);
                    cv.dragAmountRate (-80.0f, -30.0f);
                    pv.dragHandle (0, 80.0f, -40.0f);
                    pv.dragHandle (1, -60.0f, 0.0f);
                    av.dragAmountRate (-80.0f, 60.0f);
                    av.dragHandle (1, -100.0f, 0.0f);
                    const bool handlesOk = chorus->get (CH::pDelay) > d0 + 1.0f && chorus->get (CH::pAmount) != am0 && chorus->get (CH::pRate) > r0 * 1.5f
                                           && phaser->get (PF::pCentre) > c0 * 1.2f && phaser->get (PF::pFeedback) > f0 + 0.05f && phaser->get (PF::pAmount) < pa0 - 0.02f
                                           && autoPan->get (AP::pAmount) > aa0 + 0.05f && autoPan->get (AP::pRate) > ar0 * 1.5f && autoPan->get (AP::pPhase) < ap0 - 10.0f;
                    StringArray missing;
                    for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { chorus, phaser, autoPan })
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Chorus, Phaser-Flanger and Auto Pan windows: points on the graph, and a control for every parameter",
                               missing.isEmpty() ? "chorus rate " + String (r0, 2) + " -> " + String (chorus->get (CH::pRate), 2) + " Hz, phaser centre " + String (roundToInt (c0)) + " -> "
                                                   + String (roundToInt (phaser->get (PF::pCentre))) + " Hz, pan phase " + String (roundToInt (ap0)) + " -> " + String (roundToInt (autoPan->get (AP::pPhase)))
                                                 : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                cp2 = nullptr; pp2 = nullptr; ap2 = nullptr;
            }

            // --- frequency shifter, redux, exciter --------------------------------------------------------
            {
                auto mk = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (pl, 0, nullptr); return pl; };
                auto fp2 = mk (pulse::FreqShifter::xmlTypeName), rp2 = mk (pulse::Redux::xmlTypeName), ep2 = mk (pulse::Exciter::xmlTypeName);
                auto* shifter = dynamic_cast<pulse::FreqShifter*> (fp2.get());
                auto* redux = dynamic_cast<pulse::Redux*> (rp2.get());
                auto* exciter = dynamic_cast<pulse::Exciter*> (ep2.get());
                using FS = pulse::FreqShifter;
                using RX = pulse::Redux;
                using EX = pulse::Exciter;
                AudioBuffer<float> out;
                auto partDb = [&] (const AudioBuffer<float>& b, double hz)   // level of one frequency over the last 0.2 s
                {
                    const int len = (int) (0.2 * sr), from = b.getNumSamples() - len;
                    double re = 0, im = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const double ph = MathConstants<double>::twoPi * hz * (from + i) / sr;
                        re += b.getSample (0, from + i) * std::cos (ph); im += b.getSample (0, from + i) * std::sin (ph);
                    }
                    return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / len), -120.0f);
                };
                const float a10d = Decibels::decibelsToGain (-10.0f);

                // shifter: 1 kHz + 200 Hz is 1.2 kHz and nothing else; down works too; ring mod gives both
                shifter->set (FS::pShift, 200.0f); shifter->set (FS::pMix, 1.0f);
                runFx (*shifter, sine (1000, a10d), 0.6, &out);
                const float up = partDb (out, 1200), upOrig = partDb (out, 1000), upMirror = partDb (out, 800);
                shifter->set (FS::pShift, -300.0f);
                runFx (*shifter, sine (1000, a10d), 0.6, &out);
                const float down = partDb (out, 700), downMirror = partDb (out, 1300);
                // low and high notes are shifted just as cleanly
                shifter->set (FS::pShift, 100.0f);
                runFx (*shifter, sine (80, a10d), 0.6, &out);
                const float lowUp = partDb (out, 180), lowMirror = partDb (out, 20);
                runFx (*shifter, sine (9000, a10d), 0.6, &out);
                const float highUp = partDb (out, 9100), highMirror = partDb (out, 8900);
                shifter->set (FS::pMode, 1); shifter->set (FS::pShift, 200.0f);
                runFx (*shifter, sine (1000, a10d), 0.6, &out);
                const float ringA = partDb (out, 800), ringB = partDb (out, 1200);
                shifter->set (FS::pMode, 0); shifter->set (FS::pFeedback, 0.9f); shifter->set (FS::pShift, 30.0f);
                const float spiral = runFx (*shifter, sine (440, a10d), 3.0);
                shifter->set (FS::pFeedback, 0.0f); shifter->set (FS::pShift, 100.0f);
                const bool shiftOk = std::abs (up + 10.0f) < 0.5f && upOrig < -50.0f && upMirror < -45.0f && std::abs (down + 10.0f) < 0.5f && downMirror < -45.0f
                                     && std::abs (lowUp + 10.0f) < 0.7f && lowMirror < -35.0f && std::abs (highUp + 10.0f) < 0.7f && highMirror < -35.0f
                                     && std::abs (ringA + 16.0f) < 0.5f && std::abs (ringB + 16.0f) < 0.5f && spiral < 12.0f && spiral > -30.0f;
                rep.check (shiftOk, "Frequency Shifter (moves up and down cleanly from 80 Hz to 9 kHz, ring mod, stable spiral)",
                           "1 kHz +200: " + String (up, 1) + " dB at 1.2 kHz, " + String (upOrig, 0) + " at 1 kHz, " + String (upMirror, 0) + " at 800 Hz; -300: " + String (down, 1) + " / mirror " + String (downMirror, 0)
                           + "; 80 Hz mirror " + String (lowMirror, 0) + ", 9 kHz mirror " + String (highMirror, 0) + "; ring " + String (ringA, 1) + " / " + String (ringB, 1) + "; 90% feedback peaks at " + String (spiral, 1));

                // redux: full rate and 16 bits changes almost nothing; 2 bits leaves a handful of levels; 1 kHz gives 1000 new values a second
                redux->set (RX::pRate, 48000.0f); redux->set (RX::pBits, 16.0f);
                auto tone2 = sine (440, 0.5f);
                runFx (*redux, tone2, 0.3, &out);
                float cleanErr = 0;
                for (int i = 0; i < out.getNumSamples(); ++i) cleanErr = jmax (cleanErr, std::abs (out.getSample (0, i) - tone2 (0, i / sr)));
                redux->set (RX::pBits, 2.0f);
                runFx (*redux, tone2, 0.3, &out);
                std::set<int> levelsSeen;
                for (int i = 0; i < out.getNumSamples(); ++i) levelsSeen.insert (roundToInt (out.getSample (0, i) * 1000.0f));
                redux->set (RX::pBits, 16.0f); redux->set (RX::pRate, 1000.0f);
                runFx (*redux, sine (97, 0.5f), 1.0, &out);
                int changes = 0;
                for (int i = 1; i < out.getNumSamples(); ++i) if (out.getSample (0, i) != out.getSample (0, i - 1)) ++changes;
                redux->set (RX::pMix, 0.0f);
                runFx (*redux, tone2, 0.2, &out);
                float rxDry = 0;
                for (int i = 0; i < out.getNumSamples(); ++i) rxDry = jmax (rxDry, std::abs (out.getSample (0, i) - tone2 (0, i / sr)));
                redux->set (RX::pMix, 1.0f); redux->set (RX::pRate, 12000.0f); redux->set (RX::pBits, 12.0f);
                const bool reduxOk = cleanErr < 1.0e-4f && levelsSeen.size() <= 5 && levelsSeen.size() >= 3 && std::abs (changes - 1000) < 30 && rxDry < 1.0e-6f
                                     && std::abs (pulse::Redux::quantise (0.3f, 1.0f)) < 1.0e-6f && std::abs (pulse::Redux::quantise (0.7f, 1.0f) - 1.0f) < 1.0e-6f;
                rep.check (reduxOk, "Redux (clean at full rate and 16 bits, few levels at 2 bits, a new value 1000 times a second at 1 kHz)",
                           "error at 48 kHz / 16 bit " + String (cleanErr, 6) + ", levels at 2 bits: " + String ((int) levelsSeen.size()) + ", new values in a second at 1 kHz: " + String (changes));

                // exciter: odd harmonics (3 x) with Colour at odd, even (2 x) at even, nothing added to a note below Frequency, Amount 0 changes nothing
                exciter->set (EX::pFreq, 600.0f); exciter->set (EX::pDrive, 18.0f); exciter->set (EX::pAmount, 1.0f); exciter->set (EX::pColour, -1.0f);
                runFx (*exciter, sine (1000, a10d), 0.6, &out);
                const float odd3 = partDb (out, 3000), odd2 = partDb (out, 2000);
                exciter->set (EX::pColour, 1.0f);
                runFx (*exciter, sine (1000, a10d), 0.6, &out);
                const float even2 = partDb (out, 2000);
                exciter->set (EX::pFreq, 6000.0f); exciter->set (EX::pColour, 0.0f);
                runFx (*exciter, sine (100, a10d), 0.6, &out);
                const float lowKept = partDb (out, 100), lowHarm = jmax (partDb (out, 200), partDb (out, 300));
                exciter->set (EX::pAmount, 0.0f);
                auto tone3 = sine (2000, 0.25f);
                runFx (*exciter, tone3, 0.3, &out);
                float exDry = 0;
                for (int i = 4800; i < out.getNumSamples(); ++i) exDry = jmax (exDry, std::abs (out.getSample (0, i) - tone3 (0, i / sr)));
                exciter->set (EX::pAmount, 0.3f); exciter->set (EX::pFreq, 3000.0f); exciter->set (EX::pDrive, 12.0f);
                const bool exciterOk = odd3 > -45.0f && odd2 < odd3 - 25.0f && even2 > -45.0f && std::abs (lowKept + 10.0f) < 0.2f && lowHarm < -75.0f && exDry < 1.0e-5f;
                rep.check (exciterOk, "Exciter (odd and even harmonics by Colour, low notes left alone, nothing at Amount 0)",
                           "odd: 3 kHz " + String (odd3, 1) + " dB, 2 kHz " + String (odd2, 1) + "; even: 2 kHz " + String (even2, 1) + "; 100 Hz note " + String (lowKept, 2) + " dB, its harmonics " + String (lowHarm, 0));

                // their windows
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    ColourUI::ShifterView sv (*shifter);
                    ColourUI::ReduxView rv2 (*redux);
                    ColourUI::ExciterView ev (*exciter);
                    Image img (Image::ARGB, 1000, 520, true);
                    for (Component* c : { (Component*) &sv, (Component*) &rv2, (Component*) &ev }) { c->setSize (1000, 520); Graphics g (img); c->paint (g); }
                    const float s0 = shifter->get (FS::pShift), l0 = shifter->get (FS::pLfoAmount), r0 = redux->get (RX::pRate), b0 = redux->get (RX::pBits), f0 = exciter->get (EX::pFreq), am0 = exciter->get (EX::pAmount);
                    sv.dragHandle (0, 60.0f, 0.0f);
                    sv.dragHandle (1, 40.0f, 0.0f);
                    rv2.dragHandle (0, -100.0f, 60.0f);
                    ev.dragHandle (0, 60.0f, -40.0f);
                    const bool handlesOk = shifter->get (FS::pShift) > s0 + 100.0f && shifter->get (FS::pLfoAmount) > l0 + 50.0f && redux->get (RX::pRate) < r0 * 0.8f && redux->get (RX::pBits) < b0 - 1.0f
                                           && exciter->get (EX::pFreq) > f0 * 1.2f && exciter->get (EX::pAmount) > am0 + 0.05f;
                    StringArray missing;
                    for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { shifter, redux, exciter })
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Frequency Shifter, Redux and Exciter windows: points on the graph, and a control for every parameter",
                               missing.isEmpty() ? "shift " + String (roundToInt (s0)) + " -> " + String (roundToInt (shifter->get (FS::pShift))) + " Hz, rate " + String (roundToInt (r0)) + " -> "
                                                   + String (roundToInt (redux->get (RX::pRate))) + " Hz, exciter from " + String (roundToInt (f0)) + " -> " + String (roundToInt (exciter->get (EX::pFreq))) + " Hz"
                                                 : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                fp2 = nullptr; rp2 = nullptr; ep2 = nullptr;
            }

            // --- v0.99.56: Rumble ------------------------------------------------------------------------
            {
                auto rp3 = edit->getPluginCache().createNewPlugin (pulse::Rumble::xmlTypeName, {});
                t->pluginList.insertPlugin (rp3, 0, nullptr);
                auto* rumble = dynamic_cast<pulse::Rumble*> (rp3.get());
                using RB = pulse::Rumble;
                // a kick: a burst of 55 Hz that dies away in 90 ms, every half second; `until`: no more kicks after that
                auto kicks = [] (double until) { return [until] (int, double tt)
                {
                    const double ph = std::fmod (tt, 0.5);
                    return tt < until && ph < 0.09 ? 0.8f * (float) (std::sin (MathConstants<double>::twoPi * 55.0 * ph) * (1.0 - ph / 0.09)) : 0.0f;
                }; };
                auto level = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    return Decibels::gainToDecibels (b.getRMSLevel (0, (int) (from * sr), (int) (len * sr)), -140.0f);
                };
                // how much of a stretch is highs: how much the signal changes from one sample to the next, against how big it is
                auto edge = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = (int) (from * sr), n = (int) (len * sr);
                    double d = 0, e = 1.0e-15;
                    for (int i = 0; i < n; ++i) { const double x = b.getSample (0, a + i), y = b.getSample (0, a + i + 1); d += (y - x) * (y - x); e += x * x; }
                    return (float) (10.0 * std::log10 (d / e + 1.0e-15));
                };
                auto defaults = [&] { for (auto* prm : rumble->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync); };
                AudioBuffer<float> wet, open, both, dark, bright, shortTail, longTail, wide;
                // the rumble alone: there between the kicks, pushed down while a kick sounds, and nothing but lows
                defaults(); rumble->set (RB::pDry, 0);
                runFx (*rumble, kicks (10.0), 3.0, &wet);
                rumble->set (RB::pDuck, 0.0f);
                runFx (*rumble, kicks (10.0), 3.0, &open);
                const float between = level (wet, 2.75, 0.2), duringDucked = level (wet, 2.5, 0.06), duringOpen = level (open, 2.5, 0.06), lows = edge (wet, 2.6, 0.35);
                float apart = 0.0f;
                for (int i = 0; i < wet.getNumSamples(); ++i) apart = jmax (apart, std::abs (wet.getSample (0, i) - wet.getSample (1, i)));
                // with the kick left in, the kick is there as it went in (the rumble is out of its way)
                defaults();
                runFx (*rumble, kicks (10.0), 3.0, &both);
                AudioBuffer<float> plainKick (2, both.getNumSamples());
                { auto k = kicks (10.0); for (int i = 0; i < plainKick.getNumSamples(); ++i) { const float v = k (0, i / sr); plainKick.setSample (0, i, v); plainKick.setSample (1, i, v); } }
                const float kickKept = level (both, 2.5, 0.05) - level (plainKick, 2.5, 0.05);
                // Tone opens it, Decay makes it last, Width takes it off the middle
                // (for the tone: a kick with a knock at 300 Hz in it, which a pure 55 Hz burst does not have)
                auto knocks = [] (int, double tt)
                {
                    const double ph = std::fmod (tt, 0.5);
                    return ph < 0.09 ? (float) ((0.6 * std::sin (MathConstants<double>::twoPi * 55.0 * ph) + 0.3 * std::sin (MathConstants<double>::twoPi * 300.0 * ph)) * (1.0 - ph / 0.09)) : 0.0f;
                };
                defaults(); rumble->set (RB::pDry, 0); rumble->set (RB::pTone, 50.0f);
                runFx (*rumble, knocks, 3.0, &dark);
                rumble->set (RB::pTone, 500.0f);
                runFx (*rumble, knocks, 3.0, &bright);
                defaults(); rumble->set (RB::pDry, 0); rumble->set (RB::pDuck, 0.0f); rumble->set (RB::pDecay, 0.3f);
                runFx (*rumble, kicks (1.0), 3.0, &shortTail);
                rumble->set (RB::pDecay, 4.0f);
                runFx (*rumble, kicks (1.0), 3.0, &longTail);
                defaults(); rumble->set (RB::pDry, 0); rumble->set (RB::pWidth, 1.0f);
                runFx (*rumble, kicks (10.0), 3.0, &wide);
                float wideApart = 0.0f;
                for (int i = 0; i < wide.getNumSamples(); ++i) wideApart = jmax (wideApart, std::abs (wide.getSample (0, i) - wide.getSample (1, i)));
                // everything turned up: loud, and in hand
                defaults(); rumble->set (RB::pDecay, 6.0f); rumble->set (RB::pDrive, 30.0f); rumble->set (RB::pAmount, 6.0f); rumble->set (RB::pDuck, 0.0f); rumble->set (RB::pTone, 600.0f);
                const float worst = runFx (*rumble, kicks (10.0), 6.0);
                defaults();
                const bool rumbleOk = between > -45.0f && duringOpen - duringDucked > 8.0f && lows < -20.0f && apart < 1.0e-5f && std::abs (kickKept) < 1.0f
                                      && edge (bright, 2.6, 0.35) - edge (dark, 2.6, 0.35) > 4.0f && level (longTail, 2.0, 0.5) - level (shortTail, 2.0, 0.5) > 25.0f && level (longTail, 2.0, 0.5) > -70.0f
                                      && wideApart > 0.005f && worst < 12.0f && worst > -30.0f;
                rep.check (rumbleOk, "Rumble (lows of a reverb behind the kick, pushed down while the kick sounds, the kick itself untouched; tone, decay and width; in hand with everything up)",
                           "between kicks " + String (between, 1) + " dB; under a kick " + String (duringDucked, 1) + " (not ducked " + String (duringOpen, 1) + "); highs " + String (lows, 0) + " dB; kick changed "
                           + String (kickKept, 2) + " dB; Tone 50 > 500 Hz: " + String (edge (bright, 2.6, 0.35) - edge (dark, 2.6, 0.35), 1) + " dB brighter; a second after the last kick: " + String (level (shortTail, 2.0, 0.5), 0)
                           + " / " + String (level (longTail, 2.0, 0.5), 0) + " dB; sides " + String (apart, 6) + " / " + String (wideApart, 3) + "; all up " + String (worst, 1) + " dBFS");
                // its window
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    RumbleUI::RumbleView rv3 (*rumble);
                    rv3.setSize (1000, 520);
                    { Image img (Image::ARGB, 1000, 520, true); Graphics g (img); rv3.paint (g); }
                    const float tone0 = rumble->get (RB::pTone), am0 = rumble->get (RB::pAmount), cut0 = rumble->get (RB::pLowCut), duck0 = rumble->get (RB::pDuck);
                    rv3.dragHandle (0, 40.0f, -30.0f);
                    rv3.dragHandle (1, 30.0f, 0.0f);
                    rv3.dragHandle (2, 0.0f, -60.0f);
                    const bool handlesOk = rumble->get (RB::pTone) > tone0 * 1.2f && rumble->get (RB::pAmount) > am0 + 1.0f && rumble->get (RB::pLowCut) > cut0 + 3.0f && rumble->get (RB::pDuck) < duck0 - 0.05f;
                    StringArray missing;
                    {
                        DeviceEditors::Editor ed (evs, *rumble);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < rumble->numParams(); ++i) if (! seen.count (&*rumble->param (i))) missing.add (rumble->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*rumble)) missing.add ("no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Rumble window: tone, amount, low cut and duck are dragged on the picture, and there is a control for every parameter",
                               missing.isEmpty() ? "tone " + String (roundToInt (tone0)) + " -> " + String (roundToInt (rumble->get (RB::pTone))) + " Hz, duck " + String (duck0, 2) + " -> " + String (rumble->get (RB::pDuck), 2) : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                rp3->deleteFromParent();
                rp3 = nullptr;
            }

            // --- v0.99.61: Perform and the Performance page ----------------------------------------------
            {
                auto pp4 = edit->getPluginCache().createNewPlugin (pulse::Perform::xmlTypeName, {});
                t->pluginList.insertPlugin (pp4, 0, nullptr);
                auto* perform = dynamic_cast<pulse::Perform*> (pp4.get());
                using PF = pulse::Perform;
                auto tone = [] (double hz) { return [hz] (int, double tt) { return 0.25f * (float) std::sin (MathConstants<double>::twoPi * hz * tt); }; };
                auto level = [&] (const AudioBuffer<float>& b, double from, double len) { return Decibels::gainToDecibels (b.getRMSLevel (0, (int) (from * sr), (int) (len * sr)), -140.0f); };
                auto through = [&] (float sweep, float reso, double hz)
                {
                    perform->set (PF::pSweep, sweep); perform->set (PF::pResonance, reso); perform->set (PF::pFill, 0.0f);
                    AudioBuffer<float> out;
                    runFx (*perform, tone (hz), 1.0, &out);
                    return level (out, 0.5, 0.4) - Decibels::gainToDecibels (0.25f / std::sqrt (2.0f));
                };
                // in the middle the sweep does nothing at all; to the left the highs go, to the right the lows
                perform->set (PF::pSweep, 0.0f); perform->set (PF::pFill, 0.0f);
                AudioBuffer<float> same;
                runFx (*perform, tone (1000.0), 0.5, &same);
                float worstOff = 0.0f;
                { auto g = tone (1000.0); for (int i = 0; i < same.getNumSamples(); ++i) worstOff = jmax (worstOff, std::abs (same.getSample (0, i) - g (0, i / sr))); }
                const float lpHigh = through (-0.83f, 0.0f, 2000.0), lpLow = through (-0.83f, 0.0f, 50.0);
                const float hpLow = through (0.41f, 0.0f, 50.0), hpHigh = through (0.41f, 0.0f, 3000.0);
                const float atCut = PF::cutoffFor (-0.5f);
                const float plain = through (-0.5f, 0.0f, atCut), ringing = through (-0.5f, 0.85f, atCut);
                const bool sweepOk = worstOff < 1.0e-5f && lpHigh < -30.0f && std::abs (lpLow) < 1.0f && hpLow < -30.0f && std::abs (hpHigh) < 1.0f && ringing > plain + 8.0f && ringing < 16.0f
                                     && PF::sweepText (0.0f) == "Off" && PF::sweepText (-0.83f).startsWith ("Low-pass") && PF::sweepText (0.41f).startsWith ("High-pass");

                // a fill: a slow ramp goes in, so that every moment has its own value. A sixteenth is held from 1.03 s to
                // 2 s: what comes out is the sixteenth that began on the grid line before 1.03 s, again and again, and
                // then the ramp itself as if nothing had happened
                const double bpmNow = edit->tempoSequence.getTempo (0)->getBpm(), sixteenth = 15.0 / bpmNow;
                auto ramp = [] (double tt) { return (float) (tt * 0.1); };
                perform->set (PF::pSweep, 0.0f); perform->set (PF::pFill, 0.0f);
                AudioBuffer<float> filled;
                runFx (*perform, [&] (int ch, double tt)
                {
                    if (ch == 0) { const float want = tt >= 1.03 && tt < 2.0 ? 3.0f : 0.0f; if (perform->get (PF::pFill) != want) perform->set (PF::pFill, want); }
                    return ramp (tt);
                }, 3.0, &filled);
                const double line = std::floor (1.03 / sixteenth) * sixteenth, firstRepeat = line + sixteenth;
                auto outAt = [&] (double tt) { return filled.getSample (0, (int) std::round (tt * sr)); };
                auto wantAt = [&] (double tt) { return ramp (line + std::fmod (tt - firstRepeat, sixteenth)); };
                // (asked in the middle of a slice: its two ends are faded)
                auto mid = [&] (int k) { return firstRepeat + (k + 0.5) * sixteenth; };
                const int lastWhole = (int) std::floor ((2.0 - firstRepeat) / sixteenth) - 1;
                const float errFirst = std::abs (outAt (mid (0)) - wantAt (mid (0))), errLast = std::abs (outAt (mid (lastWhole)) - wantAt (mid (lastWhole)));
                const float before = std::abs (outAt (0.9) - ramp (0.9)), after = std::abs (outAt (2.5) - ramp (2.5));
                float worstStep = 0.0f;
                for (int i = (int) (1.99 * sr); i < (int) (2.03 * sr); ++i) worstStep = jmax (worstStep, std::abs (filled.getSample (0, i + 1) - filled.getSample (0, i)));
                const bool fillOk = lastWhole >= 1 && errFirst < 2.0e-4f && errLast < 2.0e-4f && before < 1.0e-5f && after < 1.0e-5f && worstStep < 0.01f
                                    && std::abs (outAt (mid (lastWhole)) - ramp (mid (lastWhole))) > 0.02f;
                rep.check (sweepOk && fillOk, "Perform (a sweep that is out of the way in the middle, a low-pass to the left and a high-pass to the right, with resonance; a fill repeats the last sixteenth in time and gives the song back without a click)",
                           "middle " + String (worstOff, 7) + "; low-pass 205 Hz: 2 kHz " + String (lpHigh, 1) + " dB, 50 Hz " + String (lpLow, 2) + " dB; high-pass 248 Hz: 50 Hz " + String (hpLow, 1) + " dB, 3 kHz " + String (hpHigh, 2)
                           + " dB; at the cutoff " + String (plain, 1) + " dB plain, " + String (ringing, 1) + " dB resonant; fill at " + String (bpmNow, 0) + " BPM: off by " + String (errFirst, 6) + " / " + String (errLast, 6)
                           + ", before " + String (before, 7) + ", after " + String (after, 7) + ", biggest step at the release " + String (worstStep, 5));

                // its window: the point on the picture, and a control for every setting
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    perform->set (PF::pSweep, -0.3f); perform->set (PF::pResonance, 0.3f); perform->set (PF::pFill, 0.0f);
                    PerformUI::PerformView pv (*perform);
                    pv.setSize (1000, 520);
                    { Image img (Image::ARGB, 1000, 520, true); Graphics g (img); pv.paint (g); }
                    const float sw0 = perform->get (PF::pSweep), re0 = perform->get (PF::pResonance);
                    pv.dragHandle (0, 120.0f, -60.0f);
                    const bool handlesOk = perform->get (PF::pSweep) > sw0 + 0.1f && perform->get (PF::pResonance) > re0 + 0.05f;
                    StringArray missing;
                    {
                        DeviceEditors::Editor ed (evs, *perform);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < perform->numParams(); ++i) if (! seen.count (&*perform->param (i))) missing.add (perform->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*perform)) missing.add ("no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Perform window: sweep and resonance are dragged on the picture, and there is a control for every parameter",
                               missing.isEmpty() ? "sweep " + String (sw0, 2) + " -> " + String (perform->get (PF::pSweep), 2) + ", resonance " + String (re0, 2) + " -> " + String (perform->get (PF::pResonance), 2) : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                pp4->deleteFromParent();
                pp4 = nullptr;

                // the Performance page: the device goes on the master, a pad for every track that is not a return
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    MidiLearn learn (*edit);
                    evs.midiLearn = &learn;
                    const bool noneYet = PerformUI::device (*edit, false) == nullptr;
                    auto* onMaster = PerformUI::device (*edit, true);
                    const bool placed = onMaster != nullptr && edit->getMasterPluginList().indexOf (onMaster) == 0 && PerformUI::device (*edit, true) == onMaster;
                    auto mt1 = ClipImport::createContentTrack (*edit, "Pad one", false);
                    auto mt2 = ClipImport::createContentTrack (*edit, "Pad two", false);
                    const auto padsFor = PerformUI::padTracks (*edit);
                    bool noReturns = ! padsFor.isEmpty();
                    for (auto* pt : padsFor) noReturns = noReturns && ! Routing::isReturnTrack (*pt);
                    bool muteOk = false, fillPads = false, learned = false, pressed = false, keptOnLoad = false, listed = false, drawn = false, helped = false;
                    if (onMaster != nullptr)
                    {
                        PerformUI::Stage stage (evs, *onMaster);
                        stage.setSize (1040, 600);
                        const int i1 = padsFor.indexOf (mt1);
                        // a pad mutes its track, another click unmutes it, and undo takes the click back
                        stage.pressMute (i1);
                        const bool muted = mt1->isMuted (false) && ! mt2->isMuted (false);
                        edit->getUndoManager().undo();
                        const bool unmutedByUndo = ! mt1->isMuted (false);
                        stage.pressMute (i1); stage.pressMute (i1);
                        muteOk = stage.numMutePads() == padsFor.size() && i1 >= 0 && muted && unmutedByUndo && ! mt1->isMuted (false);
                        // a fill is on while its pad is held; a latched one stays until it is pressed again
                        stage.holdFill (3);
                        const bool held = onMaster->getInt (PF::pFill) == 3;
                        stage.holdFill (0);
                        const bool letGo = onMaster->getInt (PF::pFill) == 0;
                        stage.latchFill (2);
                        const bool latched = onMaster->getInt (PF::pFill) == 2;
                        stage.latchFill (2);
                        fillPads = held && letGo && latched && onMaster->getInt (PF::pFill) == 0;
                        // a button of a controller learned for a mute: each press toggles, letting go does nothing
                        learn.startLearningMute (*mt2);
                        const bool waiting = learn.isLearning() && learn.getLearningName().contains ("Pad two");
                        learn.processControllerForTest (1, 41, 127);
                        learned = waiting && ! learn.isLearning() && learn.isMuteMapped (*mt2) && ! learn.isMuteMapped (*mt1) && ! mt2->isMuted (false) && learn.muteMappingText (*mt2).contains ("CC 41");
                        learn.processControllerForTest (1, 41, 0);
                        learn.processControllerForTest (1, 41, 127);
                        const bool on1 = mt2->isMuted (false);
                        learn.processControllerForTest (1, 41, 0);
                        const bool stillOn = mt2->isMuted (false);
                        learn.processControllerForTest (1, 41, 127);
                        pressed = on1 && stillOn && ! mt2->isMuted (false);
                        for (auto& row : learn.rows (0)) listed = listed || (row.cc == 41 && row.target.contains ("Pad two") && row.target.contains ("Mute"));
                        { MidiLearn again (*edit); keptOnLoad = again.isMuteMapped (*mt2) && again.muteMappingText (*mt2).contains ("CC 41"); }
                        // the picture, at its usual size and at its smallest and a very large one
                        mt1->setMute (true);
                        onMaster->set (PF::pSweep, -0.55f); onMaster->set (PF::pResonance, 0.6f);
                        stage.latchFill (3);
                        Image img (Image::ARGB, 1040, 600, true);
                        { Graphics g (img); stage.paintEntireComponent (g, true); }
                        for (auto [w, h] : { std::pair<int, int> { 640, 380 }, { 2400, 1400 } }) { stage.setSize (w, h); Image im (Image::ARGB, w, h, true); Graphics g (im); stage.paintEntireComponent (g, true); }
                        { auto f = dir.getChildFile ("performance.png"); f.deleteFile(); if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); } }
                        drawn = true;
                        helped = HelpLine::textFor (&stage).isNotEmpty();
                        stage.latchFill (3);
                        learn.removeMuteMapping (*mt2);
                        drawn = drawn && ! learn.isMuteMapped (*mt2) && stage.deviceStillThere();
                        onMaster->deleteFromParent();
                        drawn = drawn && ! stage.deviceStillThere();
                    }
                    rep.check (noneYet && placed && noReturns && muteOk && fillPads && learned && pressed && listed && keptOnLoad && drawn && helped,
                               "Performance page (Perform goes first on the master, a mute pad for every track but the returns, undo of a mute, fills held and latched, a controller button learned for a mute and kept with the project)",
                               String (padsFor.size()) + " pads" + (noneYet && placed ? "" : "; the device is not put first on the master") + (noReturns ? "" : "; a return has a pad") + (muteOk ? "" : "; mute pads wrong")
                               + (fillPads ? "" : "; fill pads wrong") + (learned ? "" : "; learning a mute wrong") + (pressed ? "" : "; a learned button does not toggle on the press")
                               + (listed ? "" : "; not in the list of mappings") + (keptOnLoad ? "" : "; not kept with the project") + (drawn ? "" : "; picture or clean-up wrong") + (helped ? "" : "; no help"));
                    edit->deleteTrack (mt2);
                    edit->deleteTrack (mt1);
                    edit->getUndoManager().clearUndoHistory();
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
            }

            // --- Pulse Amp ---------------------------------------------------------------------------------
            {
                auto ap3 = edit->getPluginCache().createNewPlugin (pulse::Amp::xmlTypeName, {});
                t->pluginList.insertPlugin (ap3, 0, nullptr);
                auto* amp = dynamic_cast<pulse::Amp*> (ap3.get());
                using AM = pulse::Amp;
                AudioBuffer<float> out;
                auto partDb = [&] (const AudioBuffer<float>& b, double hz)
                {
                    const int len = (int) (0.2 * sr), from = b.getNumSamples() - len;
                    double re = 0, im = 0;
                    for (int i = 0; i < len; ++i)
                    {
                        const double ph = MathConstants<double>::twoPi * hz * (from + i) / sr;
                        re += b.getSample (0, from + i) * std::cos (ph); im += b.getSample (0, from + i) * std::sin (ph);
                    }
                    return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / len), -120.0f);
                };
                auto rms = [&] (const AudioBuffer<float>& b) { const int len = (int) (0.2 * sr); return Decibels::gainToDecibels (b.getRMSLevel (0, b.getNumSamples() - len, len), -120.0f); };
                amp->set (AM::pCabinet, 0);
                // clean and quiet: almost no harmonics; heavy and full up: plenty, and the level stays in hand
                amp->set (AM::pModel, 0); amp->set (AM::pGain, 0.0f);
                runFx (*amp, sine (440, 0.1f), 0.6, &out);
                const float cleanH3 = partDb (out, 1320) - partDb (out, 440), cleanLevel = partDb (out, 440) + 20.0f;
                amp->set (AM::pModel, 3); amp->set (AM::pGain, 1.0f);
                const float heavyPeak = runFx (*amp, sine (440, 0.5f), 0.6, &out);
                const float heavyH3 = partDb (out, 1320) - partDb (out, 440);
                // gain changes the sound more than the volume
                amp->set (AM::pModel, 1); amp->set (AM::pGain, 0.2f);
                runFx (*amp, sine (440, 0.25f), 0.6, &out);
                const float quietGain = rms (out);
                float offset = 0;
                amp->set (AM::pGain, 1.0f);
                runFx (*amp, sine (440, 0.25f), 0.6, &out);
                const float loudGain = rms (out);
                for (int i = out.getNumSamples() - 9600; i < out.getNumSamples(); ++i) offset += out.getSample (0, i);
                offset = std::abs (offset / 9600.0f);
                // The worst case (a full-level 5 kHz sine through four clipping stages, no cabinet): what folds back to 13 kHz stays 28 dB down.
                // (Twice the sample rate removes the first fold; very high harmonics of so square a wave still fold. A cabinet takes off 30 dB more.)
                amp->set (AM::pModel, 3);
                runFx (*amp, sine (5000, 0.5f), 0.6, &out);
                const float folded = partDb (out, 13000) - partDb (out, 5000);
                // the cabinet takes the fizz away, the display's curve agrees, and the bass control does what it says
                amp->set (AM::pModel, 0); amp->set (AM::pGain, 0.2f);
                const float openTop = runFx (*amp, sine (9000, 0.1f), 0.5);
                amp->set (AM::pCabinet, 3);
                const float cabTop = runFx (*amp, sine (9000, 0.1f), 0.5);
                const float drawnTop = Decibels::gainToDecibels (amp->toneResponse (9000.0), -100.0f);
                amp->set (AM::pCabinet, 0); amp->set (AM::pBass, 1.0f);
                const float bassUp = runFx (*amp, sine (70, 0.1f), 0.5);
                amp->set (AM::pBass, 0.0f);
                const float bassDown = runFx (*amp, sine (70, 0.1f), 0.5);
                amp->set (AM::pBass, 0.5f); amp->set (AM::pMix, 0.0f);
                auto tone4 = sine (440, 0.25f);
                runFx (*amp, tone4, 0.2, &out);
                float ampDry = 0;
                for (int i = 15; i < out.getNumSamples(); ++i) ampDry = jmax (ampDry, std::abs (out.getSample (0, i) - tone4 (0, (i - 15) / sr)));   // 15 samples late: its latency
                amp->set (AM::pMix, 1.0f); amp->set (AM::pModel, 1); amp->set (AM::pGain, 0.5f); amp->set (AM::pCabinet, 2);
                const bool ampOk = cleanH3 < -45.0f && heavyH3 > -20.0f && heavyPeak < -3.0f && heavyPeak > -14.0f && std::abs (cleanLevel) < 3.0f && folded < -28.0f && std::abs (amp->getLatencySeconds() * sr - 15.0) < 0.01 && std::abs (loudGain - quietGain) < 14.0f && offset < 0.01f
                                   && cabTop < openTop - 18.0f && std::abs (drawnTop - (cabTop - openTop)) < 3.0f && bassUp > bassDown + 14.0f && ampDry < 1.0e-6f;
                rep.check (ampOk, "Pulse Amp (clean stays clean, heavy distorts without getting louder, no fold-back, cabinet and tone controls, the display agrees)",
                           "3rd harmonic clean " + String (roundToInt (cleanH3)) + " dB (level " + String (cleanLevel, 1) + "), heavy " + String (roundToInt (heavyH3)) + " (peak " + String (heavyPeak, 1) + " dBFS); gain 2 -> 10 changes the level by " + String (loudGain - quietGain, 1)
                           + " dB; folded harmonic " + String (roundToInt (folded)) + " dB; 9 kHz through the 4x12 " + String (cabTop - openTop, 1) + " dB (drawn " + String (drawnTop, 1) + "); bass control " + String (bassUp - bassDown, 1) + " dB at 70 Hz; standing offset " + String (offset, 4) + ", dry error " + String (ampDry, 6)
                           + ", latency " + String (amp->getLatencySeconds() * sr, 2) + " samples");
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    AmpUI::AmpView view (*amp);
                    view.setSize (1000, 520);
                    Image img (Image::ARGB, 1000, 520, true);
                    { Graphics g (img); view.paint (g); }
                    const float b0 = amp->get (AM::pBass), m0 = amp->get (AM::pMid), g0 = amp->get (AM::pGain);
                    view.dragHandle (0, 0.0f, -30.0f);
                    view.dragHandle (1, 0.0f, 30.0f);
                    view.dragHandle (4, 40.0f, 0.0f);
                    const bool handlesOk = amp->get (AM::pBass) > b0 + 0.05f && amp->get (AM::pMid) < m0 - 0.05f && amp->get (AM::pGain) > g0 + 0.05f && view.getNumHandles() == 5;
                    DeviceEditors::Editor ed (evs, *amp);
                    ed.setSize (1100, 760);
                    std::set<te::AutomatableParameter*> seen;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                        for (auto* chd : c.getChildren()) walk (*chd);
                    };
                    walk (ed);
                    StringArray missing;
                    for (int i = 0; i < amp->numParams(); ++i) if (! seen.count (&*amp->param (i))) missing.add (amp->param (i)->paramID);
                    rep.check (handlesOk && missing.isEmpty() && DeviceEditors::hasFullEditor (*amp), "Pulse Amp window: tone controls and gain are points on the graphs, and every parameter has a control",
                               missing.isEmpty() ? "bass " + String (b0, 2) + " -> " + String (amp->get (AM::pBass), 2) + ", gain " + String (g0, 2) + " -> " + String (amp->get (AM::pGain), 2) : missing.joinIntoString (", "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                ap3 = nullptr;
            }

            // --- resonators, shimmer, grain delay ---------------------------------------------------------
            {
                auto mk = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (pl, 0, nullptr); return pl; };
                auto rp3 = mk (pulse::Resonators::xmlTypeName), sp3 = mk (pulse::Shimmer::xmlTypeName), gp3 = mk (pulse::GrainDelay::xmlTypeName);
                auto* reson = dynamic_cast<pulse::Resonators*> (rp3.get());
                auto* shim = dynamic_cast<pulse::Shimmer*> (sp3.get());
                auto* grain = dynamic_cast<pulse::GrainDelay*> (gp3.get());
                using RS = pulse::Resonators;
                using SH = pulse::Shimmer;
                using GD = pulse::GrainDelay;
                AudioBuffer<float> out;
                auto partAt = [&] (const AudioBuffer<float>& b, double hz, double from, double len)   // level of one frequency in a stretch of time
                {
                    const int a = (int) (from * sr), n = (int) (len * sr);
                    double re = 0, im = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        const double ph = MathConstants<double>::twoPi * hz * (a + i) / sr;
                        re += b.getSample (0, a + i) * std::cos (ph); im += b.getSample (0, a + i) * std::sin (ph);
                    }
                    return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / n), -140.0f);
                };
                auto rmsAt = [&] (const AudioBuffer<float>& b, double from, double len) { return Decibels::gainToDecibels (b.getRMSLevel (0, (int) (from * sr), (int) (len * sr)), -140.0f); };
                auto click = [] (int, double tm) { return tm == 0.0 ? 1.0f : 0.0f; };
                Random nrng (11);
                auto noiseBurst = [&nrng] (int, double tm) { return tm < 0.3 ? (nrng.nextFloat() * 2.0f - 1.0f) * 0.25f : 0.0f; };

                // resonators: noise comes out ringing at the notes of the strings (and not between them); the ring lasts as long as Decay says
                reson->set (RS::pNote, 57); reson->set (RS::pChord, 0); reson->set (RS::pDecay, 2.0f); reson->set (RS::pDetune, 0.0f); reson->set (RS::pMix, 1.0f); reson->set (RS::pColour, 16000.0f);
                runFx (*reson, noiseBurst, 2.4, &out);
                const float onNote = partAt (out, 220.0, 0.5, 0.5), offNote = partAt (out, 277.0, 0.5, 0.5);
                const float ringEarly = rmsAt (out, 0.4, 0.2), ringLate = rmsAt (out, 1.4, 0.2);
                reson->set (RS::pDecay, 12.0f); reson->set (RS::pInput, 12.0f);
                Random nrng2 (12);
                const float resLoud = runFx (*reson, [&nrng2] (int, double) { return (nrng2.nextFloat() * 2.0f - 1.0f) * 0.25f; }, 4.0);
                reson->set (RS::pDecay, 1.5f); reson->set (RS::pInput, 0.0f); reson->set (RS::pMix, 0.0f);
                auto tone5 = sine (440, 0.25f);
                runFx (*reson, tone5, 0.2, &out);
                float resDry = 0;
                for (int i = 0; i < out.getNumSamples(); ++i) resDry = jmax (resDry, std::abs (out.getSample (0, i) - tone5 (0, i / sr)));
                reson->set (RS::pMix, 0.5f);
                const bool resOk = onNote > offNote + 15.0f && std::abs ((ringEarly - ringLate) - 30.0f) < 8.0f && resLoud < 24.0f && resDry < 1.0e-6f
                                   && std::abs (reson->frequency (0) - 220.0f) < 0.01f && std::abs (reson->frequency (1) - 440.0f) < 0.01f;
                rep.check (resOk, "Resonators (ring at their notes, for as long as Decay says, stable at the longest decay)",
                           "220 Hz " + String (onNote, 1) + " dB against " + String (offNote, 1) + " between the notes; falls " + String (ringEarly - ringLate, 1) + " dB in a second at 2 s decay; loudest "
                           + String (resLoud, 1) + " dBFS with noise at 12 s and +12 dB input");

                // shimmer: a 440 Hz note leaves an octave above it in the tail, which it does not without Shimmer; it dies away; nothing runs off
                auto note440 = [] (int, double tm) { return tm < 0.5 ? 0.25f * (float) std::sin (MathConstants<double>::twoPi * 440.0 * tm) : 0.0f; };
                shim->set (SH::pMix, 1.0f); shim->set (SH::pDecay, 4.0f); shim->set (SH::pShimmer, 0.8f); shim->set (SH::pInterval, 3); shim->set (SH::pDamping, 16000.0f);
                runFx (*shim, note440, 6.0, &out);
                const float octave = partAt (out, 880.0, 1.5, 0.1), shimEarly = rmsAt (out, 1.0, 0.5), shimLate = rmsAt (out, 5.4, 0.5);
                shim->set (SH::pShimmer, 0.0f);
                runFx (*shim, note440, 2.2, &out);
                const float noOctave = partAt (out, 880.0, 1.5, 0.1);
                shim->set (SH::pShimmer, 1.0f); shim->set (SH::pDecay, 30.0f);
                Random nrng3 (13);
                const float shimLoud = runFx (*shim, [&nrng3] (int, double) { return (nrng3.nextFloat() * 2.0f - 1.0f) * 0.25f; }, 8.0);
                shim->set (SH::pInterval, 0);
                Random nrng4 (14);
                const float shimLoudDown = runFx (*shim, [&nrng4] (int, double) { return (nrng4.nextFloat() * 2.0f - 1.0f) * 0.25f; }, 8.0);
                shim->set (SH::pShimmer, 0.5f); shim->set (SH::pDecay, 5.0f); shim->set (SH::pInterval, 3); shim->set (SH::pMix, 0.35f); shim->set (SH::pDamping, 8000.0f);
                const bool shimOk = octave > noOctave + 20.0f && octave > -60.0f && shimLate < shimEarly - 10.0f && shimLoud < 12.0f && shimLoudDown < 12.0f;
                rep.check (shimOk, "Shimmer (the tail climbs an octave, dies away, and stays in hand at full shimmer and 30 s)",
                           "880 Hz in the tail of a 440 Hz note " + String (octave, 1) + " dB (without shimmer " + String (noOctave, 1) + "); tail " + String (shimEarly, 1) + " -> " + String (shimLate, 1)
                           + " dB; loudest with noise " + String (shimLoud, 1) + " dBFS up, " + String (shimLoudDown, 1) + " down");

                // grain delay: with no pitch change it is a plain delay; an octave up really is; feedback stays stable; sync follows the tempo
                grain->set (GD::pTime, 100.0f); grain->set (GD::pPitch, 0.0f); grain->set (GD::pSpray, 0.0f); grain->set (GD::pRandomPitch, 0.0f); grain->set (GD::pFeedback, 0.0f); grain->set (GD::pMix, 1.0f);
                runFx (*grain, tone5, 0.6, &out);
                float delayErr = 0;
                const int late = (int) (0.1 * sr);
                for (int i = late + (int) (0.2 * sr); i < out.getNumSamples(); ++i) delayErr = jmax (delayErr, std::abs (out.getSample (0, i) - tone5 (0, (i - late) / sr)));
                grain->set (GD::pPitch, 12.0f);
                runFx (*grain, tone5, 1.0, &out);
                // (each grain starts at its own point in the wave, so the octave is counted by how often the wave crosses zero, not by one exact frequency)
                int crossings = 0;
                for (int i = (int) (0.5 * sr) + 1; i < (int) (1.0 * sr); ++i) if ((out.getSample (0, i) >= 0.0f) != (out.getSample (0, i - 1) >= 0.0f)) ++crossings;
                const float upOct = (float) crossings, stayed = partAt (out, 440.0, 0.5, 0.5);
                grain->set (GD::pFeedback, 0.95f); grain->set (GD::pSpray, 300.0f); grain->set (GD::pRandomPitch, 1.0f);
                Random nrng5 (15);
                const float grainLoud = runFx (*grain, [&nrng5] (int, double) { return (nrng5.nextFloat() * 2.0f - 1.0f) * 0.25f; }, 6.0);
                grain->set (GD::pSync, 2);
                const float synced = grain->timeMs();
                grain->set (GD::pSync, 0); grain->set (GD::pFeedback, 0.3f); grain->set (GD::pSpray, 0.0f); grain->set (GD::pRandomPitch, 0.0f); grain->set (GD::pPitch, 0.0f); grain->set (GD::pTime, 250.0f);
                const float eighth = (float) (30000.0 / edit->tempoSequence.getTempo (0)->getBpm());
                const bool grainOk = delayErr < 1.0e-3f && std::abs (upOct - 880.0f) < 90.0f && stayed < -50.0f && grainLoud < 12.0f && std::abs (synced - eighth) < 0.5f;
                rep.check (grainOk, "Grain Delay (a plain delay at no pitch change, an octave up, stable at 95% feedback, tempo sync)",
                           "against a plain 100 ms delay: off by " + String (delayErr, 4) + "; +12: " + String (crossings) + " zero crossings in half a second (880 = an octave up), 440 Hz left at " + String (stayed, 1) + " dB" + "; loudest " + String (grainLoud, 1)
                           + " dBFS; 1/8 = " + String (synced, 1) + " ms");

                // their windows
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    TimeUI::ResonatorsView rv3 (*reson);
                    TimeUI::ShimmerView sv3 (*shim);
                    TimeUI::GrainView gv3 (*grain);
                    Image img (Image::ARGB, 1000, 520, true);
                    for (Component* c : { (Component*) &rv3, (Component*) &sv3, (Component*) &gv3 }) { c->setSize (1000, 520); Graphics g (img); c->paint (g); }
                    const float n0 = reson->get (RS::pNote), d0 = reson->get (RS::pDecay), sd0 = shim->get (SH::pDecay), ss0 = shim->get (SH::pShimmer);
                    const float gt0 = grain->get (GD::pTime), gp0 = grain->get (GD::pPitch), gs0 = grain->get (GD::pSpray);
                    rv3.dragHandle (0, 60.0f, -40.0f);
                    sv3.dragHandle (0, 80.0f, 0.0f);
                    sv3.dragHandle (1, 0.0f, -60.0f);
                    gv3.dragHandle (0, 100.0f, -80.0f);
                    gv3.dragHandle (1, 80.0f, -40.0f);
                    const bool handlesOk = reson->get (RS::pNote) > n0 + 1.0f && reson->get (RS::pDecay) > d0 * 1.2f && shim->get (SH::pDecay) > sd0 + 0.3f && shim->get (SH::pShimmer) > ss0 + 0.05f
                                           && grain->get (GD::pTime) > gt0 + 30.0f && grain->get (GD::pPitch) > gp0 + 3.0f && grain->get (GD::pSpray) > gs0 + 20.0f && grain->get (GD::pRandomPitch) > 0.05f;
                    StringArray missing;
                    for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { reson, shim, grain })
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Resonators, Shimmer and Grain Delay windows: points on the graph, and a control for every parameter",
                               missing.isEmpty() ? "note " + String (roundToInt (n0)) + " -> " + String (roundToInt (reson->get (RS::pNote))) + ", shimmer decay " + String (sd0, 1) + " -> " + String (shim->get (SH::pDecay), 1)
                                                   + " s, grain pitch " + String (gp0, 1) + " -> " + String (grain->get (GD::pPitch), 1) + " st"
                                                 : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                rp3 = nullptr; sp3 = nullptr; gp3 = nullptr;
            }

            // --- beat repeat, looper ----------------------------------------------------------------------
            {
                auto mk = [&] (const char* type) { auto pl = edit->getPluginCache().createNewPlugin (type, {}); t->pluginList.insertPlugin (pl, 0, nullptr); return pl; };
                auto bp4 = mk (pulse::BeatRepeat::xmlTypeName), lp4 = mk (pulse::Looper::xmlTypeName);
                auto* beatRep = dynamic_cast<pulse::BeatRepeat*> (bp4.get());
                auto* looper = dynamic_cast<pulse::Looper*> (lp4.get());
                using BR = pulse::BeatRepeat;
                using LO = pulse::Looper;
                AudioBuffer<float> out;
                const double bpmNow = edit->tempoSequence.getTempo (0)->getBpm(), beatSec = 60.0 / bpmNow;
                // a sound that never repeats by itself: a rising tone
                auto rising = [] (int, double tm) { return 0.3f * (float) std::sin (MathConstants<double>::twoPi * (200.0 + 150.0 * tm) * tm); };
                auto against = [&] (const AudioBuffer<float>& b, double from, double to, std::function<float (double)> want)
                {
                    float worst = 0;
                    for (int i = (int) (from * sr); i < (int) (to * sr); ++i) worst = jmax (worst, std::abs (b.getSample (0, i) - want (i / sr)));
                    return worst;
                };

                // beat repeat, every bar, 1/8 slices for half a bar, gate mode: the second eighth is the first one again; after the gate, silence
                beatRep->set (BR::pInterval, 2); beatRep->set (BR::pGrid, 4); beatRep->set (BR::pGate, 2); beatRep->set (BR::pChance, 1.0f); beatRep->set (BR::pMode, 2);
                const double eighthSec = beatSec * 0.5;
                runFx (*beatRep, rising, beatSec * 4.0, &out);
                const float firstPass = against (out, 0.01, eighthSec - 0.01, [&] (double tm) { return rising (0, tm); });
                const float secondPass = against (out, eighthSec + 0.01, 2.0 * eighthSec - 0.01, [&] (double tm) { return rising (0, tm - eighthSec); });
                const float fourthPass = against (out, 3.0 * eighthSec + 0.01, 4.0 * eighthSec - 0.01, [&] (double tm) { return rising (0, tm - 3.0 * eighthSec); });
                const float afterGate = against (out, beatSec * 2.0 + 0.02, beatSec * 3.9, [] (double) { return 0.0f; });
                // insert mode: the sound itself comes back after the gate; chance 0: nothing is touched; decay: later repeats are quieter
                beatRep->set (BR::pMode, 1);
                runFx (*beatRep, rising, beatSec * 4.0, &out);
                const float insertBack = against (out, beatSec * 2.0 + 0.02, beatSec * 3.9, [&] (double tm) { return rising (0, tm); });
                beatRep->set (BR::pChance, 0.0f);
                runFx (*beatRep, rising, beatSec * 4.0, &out);
                const float untouched = against (out, 0.0, beatSec * 3.9, [&] (double tm) { return rising (0, tm); });
                beatRep->set (BR::pChance, 1.0f); beatRep->set (BR::pDecay, 1.0f); beatRep->set (BR::pMode, 2);
                runFx (*beatRep, rising, beatSec * 4.0, &out);
                const float rep1 = Decibels::gainToDecibels (out.getRMSLevel (0, (int) ((eighthSec + 0.02) * sr), (int) ((eighthSec - 0.04) * sr)));
                const float rep3 = Decibels::gainToDecibels (out.getRMSLevel (0, (int) ((3.0 * eighthSec + 0.02) * sr), (int) ((eighthSec - 0.04) * sr)));
                beatRep->set (BR::pDecay, 0.0f); beatRep->set (BR::pMode, 1);
                const bool brOk = firstPass < 0.02f && secondPass < 1.0e-4f && fourthPass < 1.0e-4f && afterGate < 1.0e-3f && insertBack < 1.0e-3f && untouched < 1.0e-5f &&   /* (the engine rounds values under a millionth to zero) */ rep3 < rep1 - 10.0f;
                rep.check (brOk, "Beat Repeat (the slice comes round again exactly, silence or the sound after the gate, chance, decay)",
                           "second time round off by " + String (secondPass, 6) + ", fourth " + String (fourthPass, 6) + "; after the gate " + String (afterGate, 5) + " (gate) / " + String (insertBack, 5)
                           + " (insert); chance 0 off by " + String (untouched, 7) + "; decay: repeat 1 " + String (rep1, 1) + " dB, repeat 3 " + String (rep3, 1) + " dB");

                // looper: record a second, play it back twice over; overdub adds to it; a set length closes the loop by itself; stop is silent; clear empties it
                auto silence = [] (int, double) { return 0.0f; };
                looper->set (LO::pMonitor, 0.0f); looper->set (LO::pLength, 0); looper->set (LO::pState, (float) LO::recording);
                runFx (*looper, rising, 1.0);
                looper->set (LO::pState, (float) LO::playing);
                runFx (*looper, silence, 2.0, &out);
                const float loopA = against (out, 0.02, 0.98, [&] (double tm) { return rising (0, tm); }), loopB = against (out, 1.02, 1.98, [&] (double tm) { return rising (0, tm - 1.0); });
                const double recorded = looper->loopSeconds();
                looper->set (LO::pState, (float) LO::overdubbing);
                runFx (*looper, sine (1000, 0.2f), 1.0);
                looper->set (LO::pState, (float) LO::playing);
                runFx (*looper, silence, 1.0, &out);
                const float both = against (out, 0.02, 0.98, [&] (double tm) { return 1.5f * std::tanh ((rising (0, tm) + 0.2f * (float) std::sin (MathConstants<double>::twoPi * 1000.0 * tm)) / 1.5f); });
                looper->set (LO::pSpeed, 2.0f);
                runFx (*looper, silence, 1.0, &out);
                const float fast = against (out, 0.02, 0.48, [&] (double tm) { return 1.5f * std::tanh ((rising (0, tm * 2.0) + 0.2f * (float) std::sin (MathConstants<double>::twoPi * 1000.0 * tm * 2.0)) / 1.5f); });
                looper->set (LO::pSpeed, 1.0f); looper->set (LO::pState, (float) LO::stopped);
                const float stoppedDb = runFx (*looper, silence, 0.3);
                // a set length of one bar: recording for longer than that closes the loop and plays it
                looper->set (LO::pLength, 1); looper->set (LO::pState, (float) LO::recording);
                const double barSec = beatSec * 4.0;
                runFx (*looper, rising, barSec + 1.0, &out);
                const double barLen = looper->loopSeconds();
                const float autoPlay = against (out, barSec + 0.02, barSec + 0.98, [&] (double tm) { return rising (0, tm - barSec); });
                looper->clear();
                looper->set (LO::pState, (float) LO::playing);
                const float clearedDb = runFx (*looper, silence, 0.3);
                const double clearedLen = looper->loopSeconds();
                looper->set (LO::pState, (float) LO::stopped); looper->set (LO::pLength, 0); looper->set (LO::pMonitor, 1.0f);
                auto tone6 = sine (440, 0.25f);
                runFx (*looper, tone6, 0.2, &out);
                const float monitorErr = against (out, 0.0, 0.19, [&] (double tm) { return tone6 (0, tm); });
                const bool looperOk = loopA < 1.0e-5f && loopB < 1.0e-5f && std::abs (recorded - 1.0) < 0.001 && both < 1.0e-4f && fast < 0.02f && stoppedDb < -150.0f
                                      && std::abs (barLen - barSec) < 0.001 && autoPlay < 1.0e-5f && clearedDb < -150.0f && clearedLen == 0.0 && monitorErr < 1.0e-6f;
                rep.check (looperOk, "Looper (plays back what was recorded, overdub, double speed, set length, stop, clear, monitor)",
                           "loop of " + String (recorded, 3) + " s plays back off by " + String (loopA, 7) + " / " + String (loopB, 7) + "; with an overdub " + String (both, 6) + "; at double speed " + String (fast, 4)
                           + "; one bar set: " + String (barLen, 3) + " s, then plays off by " + String (autoPlay, 7) + "; cleared: " + String (clearedLen, 2) + " s");

                // their windows
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    LoopUI::BeatRepeatView bv (*beatRep);
                    LoopUI::LooperView lv (*looper);
                    Image img (Image::ARGB, 1000, 520, true);
                    for (Component* c : { (Component*) &bv, (Component*) &lv }) { c->setSize (1000, 520); Graphics g (img); c->paint (g); }
                    const int g0 = beatRep->getInt (BR::pGrid), ga0 = beatRep->getInt (BR::pGate);
                    bv.dragHandle (0, 120.0f, 0.0f);
                    bv.dragHandle (1, 300.0f, 0.0f);
                    bv.dragHandle (2, 0.0f, 80.0f);
                    const bool handlesOk = beatRep->getInt (BR::pGrid) > g0 && beatRep->getInt (BR::pGate) > ga0 && beatRep->get (BR::pDecay) > 0.1f;
                    StringArray missing;
                    for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { beatRep, looper })
                    {
                        DeviceEditors::Editor ed (evs, *pp);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                    }
                    rep.check (handlesOk && missing.isEmpty(), "Beat Repeat and Looper windows: grid, gate and decay are points on the graph, and every parameter has a control",
                               missing.isEmpty() ? "grid " + BR::gridNames[g0] + " -> " + beatRep->param (BR::pGrid)->getCurrentValueAsString() + ", gate " + BR::gateNames[ga0] + " -> " + beatRep->param (BR::pGate)->getCurrentValueAsString()
                                                 : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                bp4 = nullptr; lp4 = nullptr;
            }

            // --- Pulse Spectral ----------------------------------------------------------------------------
            {
                auto sp5 = edit->getPluginCache().createNewPlugin (pulse::Spectral::xmlTypeName, {});
                t->pluginList.insertPlugin (sp5, 0, nullptr);
                auto* spec = dynamic_cast<pulse::Spectral*> (sp5.get());
                using SP = pulse::Spectral;
                AudioBuffer<float> out;
                const int late = SP::size;
                auto partAt = [&] (const AudioBuffer<float>& b, double hz, double from, double len)
                {
                    const int a = (int) (from * sr), n = (int) (len * sr);
                    double re = 0, im = 0;
                    for (int i = 0; i < n; ++i)
                    {
                        const double ph = MathConstants<double>::twoPi * hz * (a + i) / sr;
                        re += b.getSample (0, a + i) * std::cos (ph); im += b.getSample (0, a + i) * std::sin (ph);
                    }
                    return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / n), -140.0f);
                };
                auto rmsAt = [&] (const AudioBuffer<float>& b, double from, double len) { return Decibels::gainToDecibels (b.getRMSLevel (0, (int) (from * sr), (int) (len * sr)), -140.0f); };
                // with nothing switched on, what comes out is what went in (2048 samples late, which it reports)
                auto rising2 = [] (int, double tm) { return 0.3f * (float) std::sin (MathConstants<double>::twoPi * (200.0 + 300.0 * tm) * tm); };
                runFx (*spec, rising2, 1.0, &out);
                float through = 0;
                for (int i = late * 2; i < out.getNumSamples(); ++i) through = jmax (through, std::abs (out.getSample (0, i) - rising2 (0, (i - late) / sr)));
                // shift: an octave up really is
                spec->set (SP::pShift, 12.0f);
                runFx (*spec, sine (440, 0.25f), 1.0, &out);
                const float up880 = partAt (out, 880.0, 0.5, 0.4), left440 = partAt (out, 440.0, 0.5, 0.4);
                spec->set (SP::pShift, 0.0f);
                // freeze: a short note is held long after it has stopped
                auto shortNote = [] (int, double tm) { return tm > 0.2 && tm < 0.6 ? 0.25f * (float) std::sin (MathConstants<double>::twoPi * 440.0 * tm) : 0.0f; };
                spec->set (SP::pFreeze, 1.0f);
                runFx (*spec, shortNote, 3.0, &out);
                const float heldLate = rmsAt (out, 2.5, 0.4), heldTone = partAt (out, 440.0, 2.5, 0.4);
                spec->set (SP::pFreeze, 0.0f);
                runFx (*spec, shortNote, 3.0, &out);
                const float notHeld = rmsAt (out, 2.5, 0.4);
                // blur: the note rings on for a while after it stops, then dies
                spec->set (SP::pBlur, 1.0f);
                runFx (*spec, shortNote, 3.0, &out);
                const float blurSoon = rmsAt (out, 0.75, 0.1), blurLater = rmsAt (out, 2.6, 0.3);
                spec->set (SP::pBlur, 0.0f);
                // the spectral delay: the note comes out half a second later; with full tilt the lows are not delayed at all
                spec->set (SP::pDelay, 500.0f);
                runFx (*spec, shortNote, 2.0, &out);
                const float beforeDelay = rmsAt (out, 0.3, 0.3), afterDelay = rmsAt (out, 0.85, 0.25);
                spec->set (SP::pTilt, 1.0f);
                auto lowNote = [] (int, double tm) { return tm > 0.2 && tm < 0.6 ? 0.25f * (float) std::sin (MathConstants<double>::twoPi * 60.0 * tm) : 0.0f; };
                runFx (*spec, lowNote, 2.0, &out);
                const float lowSoon = rmsAt (out, 0.35, 0.25);
                spec->set (SP::pTilt, 0.0f); spec->set (SP::pFeedback, 0.95f); spec->set (SP::pBlur, 1.0f);
                Random srng (21);
                const float specLoud = runFx (*spec, [&srng] (int, double) { return (srng.nextFloat() * 2.0f - 1.0f) * 0.25f; }, 6.0);
                spec->set (SP::pFeedback, 0.0f); spec->set (SP::pBlur, 0.0f); spec->set (SP::pDelay, 0.0f);
                const bool specOk = through < 2.0e-3f && up880 > left440 + 20.0f && up880 > -22.0f && heldLate > -40.0f && heldTone > -45.0f && notHeld < -100.0f
                                    && blurSoon > -50.0f && blurLater < blurSoon - 25.0f && beforeDelay < -80.0f && afterDelay > -30.0f && lowSoon > -30.0f && specLoud < 12.0f
                                    && std::abs (spec->getLatencySeconds() * sr - SP::size) < 0.01;
                rep.check (specOk, "Pulse Spectral (nothing on = nothing changed, octave up, freeze holds, blur rings on, each frequency delayed, stable)",
                           "round trip off by " + String (through, 5) + "; +12: 880 Hz " + String (up880, 1) + " dB, 440 Hz " + String (left440, 1) + "; a note 2 s after it stopped: frozen " + String (heldLate, 1)
                           + " dB, not frozen " + String (notHeld, 0) + "; blur " + String (blurSoon, 1) + " -> " + String (blurLater, 1) + "; 500 ms delay: before " + String (beforeDelay, 0) + ", after " + String (afterDelay, 1)
                           + ", lows with full tilt " + String (lowSoon, 1) + "; loudest " + String (specLoud, 1) + " dBFS");
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    SpectralUI::SpectralView view (*spec);
                    view.setSize (1000, 520);
                    Image img (Image::ARGB, 1000, 520, true);
                    { Graphics g (img); view.paint (g); }
                    view.dragHandle (0, 80.0f, 0.0f);
                    view.dragHandle (2, 0.0f, -100.0f);
                    const bool handlesOk = spec->get (SP::pShift) > 3.0f && spec->get (SP::pDelay) > 200.0f && spec->get (SP::pTilt) > 0.9f && view.getNumHandles() == 3;
                    DeviceEditors::Editor ed (evs, *spec);
                    ed.setSize (1100, 760);
                    std::set<te::AutomatableParameter*> seen;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                        for (auto* chd : c.getChildren()) walk (*chd);
                    };
                    walk (ed);
                    StringArray missing;
                    for (int i = 0; i < spec->numParams(); ++i) if (! seen.count (&*spec->param (i))) missing.add (spec->param (i)->paramID);
                    rep.check (handlesOk && missing.isEmpty() && DeviceEditors::hasFullEditor (*spec), "Pulse Spectral window: shift and the delay line are points on the graph, and every parameter has a control",
                               missing.isEmpty() ? "shift -> " + String (spec->get (SP::pShift), 1) + " st, highs delayed by " + String (roundToInt (spec->delayAt (1.0f))) + " ms, lows by " + String (roundToInt (spec->delayAt (0.0f)))
                                                 : missing.joinIntoString (", "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                sp5 = nullptr;
            }

            // Match EQ: learns this track and a reference, and takes the one towards the other
            {
                auto mq = edit->getPluginCache().createNewPlugin (pulse::MatchEQ::xmlTypeName, {});
                t->pluginList.insertPlugin (mq, 0, nullptr);
                auto* match = dynamic_cast<pulse::MatchEQ*> (mq.get());
                using MQ = pulse::MatchEQ;
                // this track: plain noise. The reference: the same noise, 8 dB brighter above 4 kHz and 6 dB thinner under 150 Hz
                const int len = (int) (sr * 6.0);
                std::vector<float> noise ((size_t) len);
                Random rnd (99);
                for (auto& v : noise) v = (rnd.nextFloat() * 2.0f - 1.0f) * 0.25f;
                AudioBuffer<float> refBuf (2, len);
                pulse::Biquad hs, ls;
                hs.set (pulse::Biquad::Type::highShelf, sr, 4000.0, 0.707, 8.0);
                ls.set (pulse::Biquad::Type::lowShelf, sr, 150.0, 0.707, -6.0);
                for (int i = 0; i < len; ++i) { const float v = ls.process (hs.process (noise[(size_t) i], 0), 0); refBuf.setSample (0, i, v); refBuf.setSample (1, i, v); }
                auto refFile = dir.getChildFile ("match_reference.wav");
                refFile.deleteFile();
                {
                    WavAudioFormat wav;
                    if (auto os = refFile.createOutputStream())
                        if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), sr, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (refBuf, 0, len);
                }
                auto noiseSig = [&] (int, double tm) { return noise[(size_t) jlimit (0, len - 1, roundToInt (tm * sr))]; };
                const bool nothingYet = ! match->hasSource() && ! match->hasReference();
                match->startLearning (MQ::learningSource);
                runFx (*match, noiseSig, 5.0);
                const float heard = match->learnedSeconds.load();
                edit->getUndoManager().beginNewTransaction ("learn");
                const bool learned = match->stopLearning() && match->hasSource() && match->isLearning() == MQ::notLearning;
                edit->getUndoManager().beginNewTransaction ("reference");
                const bool gotFile = match->learnFromFile (refFile, true) && match->hasReference() && match->referenceName() == "match_reference.wav";
                MQ::Points curve;
                match->set (MQ::pSmooth, 0.33f);
                match->getCurve (curve);
                const float highs = MQ::curveAt (curve, 12000.0) - MQ::curveAt (curve, 1000.0), lows = MQ::curveAt (curve, 50.0) - MQ::curveAt (curve, 1000.0);
                // what comes out: tones at 12 kHz and 1 kHz, compared with what the curve says
                const float a = Decibels::decibelsToGain (-20.0f);
                const float out12k = runFx (*match, sine (12000, a), 0.8) + 20.0f, out1k = runFx (*match, sine (1000, a), 0.8) + 20.0f, out50 = runFx (*match, sine (50, a), 0.8) + 20.0f;
                const bool sounds = std::abs ((out12k - out1k) - highs) < 0.5f && std::abs ((out50 - out1k) - lows) < 1.0f && std::abs (out1k - MQ::curveAt (curve, 1000.0)) < 0.3f;
                // the delay it reports is the delay there is
                AudioBuffer<float> click;
                runFx (*match, [&] (int, double tm) { return roundToInt (tm * sr) == 200 ? 1.0f : 0.0f; }, 0.4, &click);
                int peakAt = 0;
                for (int i = 0; i < click.getNumSamples(); ++i) if (std::abs (click.getSample (0, i)) > std::abs (click.getSample (0, peakAt))) peakAt = i;
                const int late = roundToInt (match->getLatencySeconds() * sr);
                // amount 0 is the sound itself; "most change" holds the curve; From keeps the lows out
                match->set (MQ::pAmount, 0.0f);
                const float flat12k = runFx (*match, sine (12000, a), 0.8) + 20.0f;
                match->set (MQ::pAmount, 1.0f); match->set (MQ::pRange, 3.0f);
                match->getCurve (curve);
                float most = 0.0f;
                for (auto v : curve) most = jmax (most, std::abs (v));
                match->set (MQ::pRange, 12.0f); match->set (MQ::pLow, 500.0f);
                match->getCurve (curve);
                const float lowsOut = MQ::curveAt (curve, 60.0), highsStill = MQ::curveAt (curve, 12000.0);
                match->set (MQ::pLow, 20.0f);
                // undo takes the reference away again, and with it the curve
                edit->getUndoManager().undo();
                match->refreshFromProject();
                const bool undone = ! match->hasReference() && match->hasSource();
                match->getCurve (curve);
                float mostUndone = 0.0f;
                for (auto v : curve) mostUndone = jmax (mostUndone, std::abs (v));
                edit->getUndoManager().redo();
                match->refreshFromProject();
                const bool redone = match->hasReference();
                const bool ok = nothingYet && learned && heard > 3.0f && gotFile && std::abs (highs - 8.0f) < 1.2f && std::abs (lows + 6.0f) < 1.5f && sounds && peakAt == 200 + late && late > 0
                                && std::abs (flat12k) < 0.1f && most <= 3.001f && most > 2.9f && std::abs (lowsOut) < 0.01f && highsStill > 4.0f && undone && mostUndone < 0.001f && redone;
                rep.check (ok, "Match EQ (learns this track and a reference file, the curve is their difference, it is what is heard, amount / most change / range, undo)",
                           "heard " + String (heard, 1) + " s; curve: highs " + String (highs, 1) + " dB (8 wanted), lows " + String (lows, 1) + " (-6 wanted); heard: 12 kHz " + String (out12k, 1) + ", 1 kHz " + String (out1k, 1)
                             + ", 50 Hz " + String (out50, 1) + "; click " + String (peakAt - 200) + " samples late (says " + String (late) + "); amount 0: " + String (flat12k, 2) + " dB; held to " + String (most, 2)
                             + " dB; from 500 Hz: " + String (lowsOut, 2) + " at 60 Hz, " + String (highsStill, 1) + " at 12 kHz; undo " + String ((int) undone) + String ((int) redone));
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    MatchUI::MatchView view (*match);
                    view.setSize (1000, 520);
                    Image img (Image::ARGB, 1000, 520, true);
                    { Graphics g (img); view.paint (g); }
                    match->set (MQ::pAmount, 0.5f);
                    view.dragAmount (-40.0f);
                    view.dragHandle (0, 150.0f, 0.0f);
                    view.dragHandle (1, -100.0f, 0.0f);
                    const bool handlesOk = match->get (MQ::pAmount) > 0.6f && match->get (MQ::pLow) > 40.0f && match->get (MQ::pHigh) < 15000.0f && view.getNumHandles() == 3;
                    DeviceEditors::Editor ed (evs, *match);
                    ed.setSize (1100, 760);
                    std::set<te::AutomatableParameter*> seen;
                    int buttons = 0;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* b = dynamic_cast<TextButton*> (&c); b != nullptr && (b->getButtonText().startsWith ("Learn") || b->getButtonText().startsWith ("Reference"))) ++buttons;
                        for (auto* chd : c.getChildren()) walk (*chd);
                    };
                    walk (ed);
                    StringArray missing;
                    for (int i = 0; i < match->numParams(); ++i) if (! seen.count (&*match->param (i))) missing.add (match->param (i)->paramID);
                    rep.check (handlesOk && missing.isEmpty() && buttons == 3 && DeviceEditors::hasFullEditor (*match), "Match EQ window: the range and the amount are points on the curve, the learn buttons are there, and every parameter has a control",
                               missing.isEmpty() ? "amount -> " + match->param (MQ::pAmount)->getCurrentValueAsString() + ", range " + match->param (MQ::pLow)->getCurrentValueAsString() + " - " + match->param (MQ::pHigh)->getCurrentValueAsString()
                                                   + ", " + String (buttons) + " buttons"
                                                 : missing.joinIntoString (", "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                refFile.deleteFile();
                mq = nullptr;
            }

            // every effect: meters of what goes in and comes out, and listening at matched loudness
            {
                auto up = edit->getPluginCache().createNewPlugin (pulse::Utility::xmlTypeName, {});
                t->pluginList.insertPlugin (up, 0, nullptr);
                auto* util = dynamic_cast<pulse::Utility*> (up.get());
                util->set (pulse::Utility::pGain, 12.0f);
                const float a = Decibels::decibelsToGain (-20.0f);
                const float loud = runFx (*util, sine (1000, a), 3.0);
                const float inShown = util->meterIn.load(), outShown = util->meterOut.load();
                const bool meters = std::abs (loud + 8.0f) < 0.2f && std::abs (inShown + 20.0f) < 1.5f && std::abs (outShown + 8.0f) < 1.5f && ! util->isGainMatched();
                edit->getUndoManager().beginNewTransaction ("match");
                util->setGainMatched (true);
                const float matched = runFx (*util, sine (1000, a), 3.0);
                const float turned = util->matchDb.load();
                const bool matches = std::abs (matched + 20.0f) < 0.3f && std::abs (turned + 12.0f) < 0.3f && std::abs (util->meterOut.load() + 20.0f) < 1.5f;
                // it is kept with the device and follows undo; silence asks for no gain
                const bool kept = (bool) util->state.getProperty ("pulseGainMatch", false);
                edit->getUndoManager().undo();
                const bool undone = ! util->isGainMatched();
                const bool sums = pulse::PulseEffect::matchGainDb (1.0, 0.25) > 6.0f && pulse::PulseEffect::matchGainDb (1.0, 0.25) < 6.05f && pulse::PulseEffect::matchGainDb (0.0, 1.0) == 0.0f
                                  && pulse::PulseEffect::matchGainDb (1.0, 0.0) == 0.0f && pulse::PulseEffect::matchGainDb (1.0, 1.0e-9) == 24.0f;
                // the window: undo and redo, copy A to B, and the meters
                bool windowOk = false;
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    DeviceEditors::Editor ed (evs, *util);
                    ed.setSize (1100, 760);
                    const bool hasThem = ed.hasMeters();
                    util->set (pulse::Utility::pGain, 3.0f);
                    ed.clickCopyAB();                     // B is now the same as A (gain 3)
                    ed.clickAB();                         // to B
                    const bool same = std::abs (util->get (pulse::Utility::pGain) - 3.0f) < 0.01f;
                    util->set (pulse::Utility::pGain, -9.0f);
                    ed.clickAB();                         // back to A
                    const bool aKept = std::abs (util->get (pulse::Utility::pGain) - 3.0f) < 0.01f;
                    ed.clickAB();
                    const bool bKept = std::abs (util->get (pulse::Utility::pGain) + 9.0f) < 0.01f;
                    edit->getUndoManager().beginNewTransaction ("turn");
                    util->set (pulse::Utility::pGain, 1.0f);
                    edit->getUndoManager().beginNewTransaction ("after");
                    ed.setSize (1100, 760);
                    const bool canUndo = edit->getUndoManager().canUndo();
                    ed.clickUndo();
                    const bool undid = std::abs (util->get (pulse::Utility::pGain) - 1.0f) > 0.01f;
                    auto img = ed.createComponentSnapshot (ed.getLocalBounds().removeFromTop (60));
                    auto f = dir.getChildFile ("device_window_header.png");
                    f.deleteFile();
                    if (auto os = f.createOutputStream()) { PNGImageFormat png; png.writeImageToStream (img, *os); }
                    windowOk = hasThem && same && aKept && bKept && canUndo && undid;
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                rep.check (meters && matches && kept && undone && sums && windowOk, "Device windows: in / out meters, gain match (a +12 dB device heard at the level that went in), copy A to B, undo in the window",
                           "+12 dB device: in " + String (inShown, 1) + " dB, out " + String (outShown, 1) + "; matched: out " + String (matched, 1) + " dBFS, turned " + String (turned, 1) + " dB"
                             + (kept && undone ? "" : "; not kept / undo wrong") + (sums ? "" : "; sums wrong") + (windowOk ? "" : "; window wrong"));
                up->deleteFromParent();
            }

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
                    // Still at the clamp a second after the input stopped: either a runaway, or something that rings long and
                    // loud because it was asked to (three low-passes at 20 Hz with a Q of 18 and 18 dB of gain on top ring for
                    // seconds, and die away). Eight more seconds of silence tell the two apart: a runaway does not die away.
                    if (ok && ! p->isSynth() && tailPeak >= 3.99f)
                        for (int k = 130; k < 130 + 750 && ok; ++k)
                        {
                            audio.clear();
                            te::MidiMessageArray midi;
                            te::PluginRenderContext fc (&audio, 0, block, &midi, 0.0, { te::TimePosition::fromSeconds (k * block / sr), te::TimePosition::fromSeconds ((k + 1) * block / sr) },
                                                        true, false, true, false);
                            p->applyToBuffer (fc);
                            tailPeak = audio.getMagnitude (0, block);
                            if (! std::isfinite (tailPeak)) { ok = false; worst = "not a number"; }
                            if (tailPeak < 1.0f) break;
                        }
                    p->deinitialise();
                    ++runs;
                    // long after the input stopped (notes released) it must not still be screaming: that is a runaway
                    // (instruments are left out: a ten-second release at full level is loud for a long time and that is what was asked for)
                    juce::ignoreUnused (pinned);
                    if (ok && ! p->isSynth() && tailPeak >= 3.99f) { ok = false; worst = "runs away (still at the clamp nine seconds after the input stopped)"; }
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
           #if JUCE_DEBUG
            const double wantPlain = 4.0, wantHeavy = 1.0;   // a build for finding mistakes, not for speed: it only has to keep up
           #else
            const double wantPlain = 8.0, wantHeavy = 2.0;
           #endif
            rep.check (plain > wantPlain && heavy > wantHeavy, "Wavetable: 8 held notes play many times faster than real time on one core",
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
            {
                auto dp = edit->getPluginCache().createNewPlugin (pulse::DrumSynth::xmlTypeName, {});
                auto* ds = dynamic_cast<pulse::DrumSynth*> (dp.get());
                DeviceEditors::DrumSynthPanel panel (sevs, *ds);
                panel.setSize (1000, 700);
                missS.addArray (controlsFor (*ds, panel, nullptr, 1));
            }
            const bool allThere = missW.isEmpty() && missF.isEmpty() && missS.isEmpty();
            rep.check (allThere, "Wavetable, FM, Simpler and Drum Synth windows have a control for every parameter",
                       allThere ? String ("nothing missing") : "missing: " + missW.joinIntoString (" ") + " | " + missF.joinIntoString (" ") + " | " + missS.joinIntoString (" "));
        }

        // --- v0.99.18: slices in the Simpler ------------------------------------------------------------------
        {
            rep.step ("slices");
            using SI = pulse::Simpler;
            auto sp6 = edit->getPluginCache().createNewPlugin (SI::xmlTypeName, {});
            auto* simp = dynamic_cast<SI*> (sp6.get());
            const double ksr = 48000.0;
            // a one-second recording of four hits a quarter of a second apart, each a different pitch (200, 400, 800, 1600 Hz)
            auto wavFile = dir.getChildFile ("slices_test.wav");
            wavFile.deleteFile();
            {
                AudioBuffer<float> rec (2, 48000);
                rec.clear();
                for (int h = 0; h < 4; ++h)
                    for (int i = 0; i < 4800; ++i)
                    {
                        const float v = 0.6f * std::exp (-i / 1500.0f) * (float) std::sin (MathConstants<double>::twoPi * 200.0 * std::pow (2.0, h) * i / ksr);
                        rec.setSample (0, h * 12000 + i, v); rec.setSample (1, h * 12000 + i, v);
                    }
                WavAudioFormat wav;
                if (auto os = wavFile.createOutputStream())
                    if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.release(), ksr, 2, 24, {}, 0) }) w->writeFromAudioSampleBuffer (rec, 0, 48000);
            }
            simp->setSampleFile (wavFile);
            auto partIn = [&] (const AudioBuffer<float>& b, double hz, double from, double len)
            {
                const int a = jmin (jmax (0, b.getNumSamples() - 2), (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                double re = 0, im = 0;
                for (int i = 0; i < n; ++i)
                {
                    const double ph = MathConstants<double>::twoPi * hz * (a + i) / ksr;
                    re += b.getSample (0, a + i) * std::cos (ph); im += b.getSample (0, a + i) * std::sin (ph);
                }
                return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / jmax (1, n)), -140.0f);
            };
            // the hits are found where they are
            simp->sliceAtHits (0.5f);
            auto found = simp->getSlices();
            String where;
            float worstPlace = found.size() == 4 ? 0.0f : 1.0f;
            for (size_t i = 0; i < found.size(); ++i) { where << String (found[i], 3) << " "; if (found.size() == 4) worstPlace = jmax (worstPlace, std::abs (found[i] - (float) i * 0.25f)); }
            // each key from the Root note plays its own slice, at its own pitch; a key past the last slice plays nothing
            simp->set (SI::pMode, (float) SI::sliceMode); simp->set (SI::pRoot, 60.0f); simp->set (SI::pVelSens, 0.0f);
            auto second = FactorySounds::renderNote (*simp, 61, 0.5);
            const float is400 = partIn (second, 400.0, 0.0, 0.08), not200 = partIn (second, 200.0, 0.0, 0.08), not800 = partIn (second, 800.0, 0.0, 0.08);
            auto fourth = FactorySounds::renderNote (*simp, 63, 0.5);
            const float is1600 = partIn (fourth, 1600.0, 0.0, 0.08);
            auto past = FactorySounds::renderNote (*simp, 64, 0.3);
            const float nothing = Decibels::gainToDecibels (past.getMagnitude (0, past.getNumSamples()), -140.0f);
            // a slice plays to its end even when the key is let go at once, unless it is set to play only while held
            auto tapped = FactorySounds::renderNote (*simp, 60, 0.02);
            const float tappedLate = Decibels::gainToDecibels (tapped.getNumSamples() > 3600 ? tapped.getRMSLevel (0, 2400, 1200) : 0.0f, -140.0f);
            simp->set (SI::pSliceGate, 1.0f); simp->set (SI::pR, 5.0f);
            auto gated = FactorySounds::renderNote (*simp, 60, 0.02);
            const float gatedLate = Decibels::gainToDecibels (gated.getNumSamples() > 3600 ? gated.getRMSLevel (0, 2400, 1200) : 0.0f, -140.0f);
            simp->set (SI::pSliceGate, 0.0f);
            // Slice Length: at half a note (one second at 120) the quarter-second slice lasts four times as long, at the same pitch
            auto normal = FactorySounds::renderNote (*simp, 61, 1.5);
            simp->set (SI::pStretch, 5.0f);
            auto longer = FactorySounds::renderNote (*simp, 61, 1.5);
            auto soundsFor = [&] (const AudioBuffer<float>& b)   // how long it stays above a hundredth of its loudest
            {
                const float top = b.getMagnitude (0, b.getNumSamples());
                int last = 0;
                for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > top * 0.01f) last = i;
                return (float) (last / ksr);
            };
            const float normalFor = soundsFor (normal), longerFor = soundsFor (longer);
            const float pitchKept = partIn (longer, 400.0, 0.02, 0.12), notLower = partIn (longer, 100.0, 0.02, 0.12);
            const float edgeKept = Decibels::gainToDecibels (longer.getMagnitude (0, jmin (longer.getNumSamples(), 480)), -100.0f) - Decibels::gainToDecibels (normal.getMagnitude (0, jmin (normal.getNumSamples(), 480)), -100.0f);
            simp->set (SI::pStretch, 0.0f);
            // cut by hand, cut evenly, and all of it survives being saved in the project
            simp->sliceEvenly (8);
            const bool even = simp->getSlices().size() == 8 && std::abs (simp->getSlices()[3] - 0.375f) < 1.0e-4f;
            DeviceDisplays::SimplerDisplay disp (*simp);
            disp.setSize (800, 300);
            { Image img (Image::ARGB, 800, 300, true); Graphics g (img); disp.paint (g); }
            disp.toggleSliceAt (0.3f);
            const bool added = simp->getSlices().size() == 9;
            disp.toggleSliceAt (0.3f);
            const bool takenAway = simp->getSlices().size() == 8;
            const bool kept = simp->state[SI::slicesID].toString().isNotEmpty();
            const bool slicesOk = found.size() == 4 && worstPlace < 0.01f && is400 > not200 + 20.0f && is400 > not800 + 20.0f && is1600 > -30.0f && nothing < -100.0f
                                  && tappedLate > -40.0f && gatedLate < tappedLate - 30.0f && longerFor > normalFor * 2.5f && pitchKept > notLower + 20.0f && std::abs (edgeKept) < 3.0f
                                  && even && added && takenAway && kept;
            rep.check (slicesOk, "Simpler slices (hits found, a key per slice, to the end or while held, made longer without changing pitch, cut by hand)",
                       "hits at " + where.trim() + "; key 2 plays 400 Hz at " + String (is400, 1) + " dB (200 Hz " + String (not200, 0) + ", 800 Hz " + String (not800, 0) + "); past the last slice " + String (nothing, 0)
                       + " dB; let go at once: " + String (tappedLate, 0) + " dB still sounding, " + String (gatedLate, 0) + " if only while held; a slice sounds for " + String (normalFor, 2) + " s, stretched "
                       + String (longerFor, 2) + " s, its first edge " + String (edgeKept, 1) + " dB different");
        }

        // --- v0.99.17: Pulse Analog and Pulse String ---------------------------------------------------------
        {
            rep.step ("analog and string");
            auto ap4 = edit->getPluginCache().createNewPlugin (pulse::Analog::xmlTypeName, {});
            auto sp4 = edit->getPluginCache().createNewPlugin (pulse::StringModel::xmlTypeName, {});
            auto* analog = dynamic_cast<pulse::Analog*> (ap4.get());
            auto* strng = dynamic_cast<pulse::StringModel*> (sp4.get());
            using AN = pulse::Analog;
            using ST = pulse::StringModel;
            const double ksr = 48000.0;
            auto partIn = [&] (const AudioBuffer<float>& b, double hz, double from, double len)
            {
                const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                double re = 0, im = 0;
                for (int i = 0; i < n; ++i)
                {
                    const double ph = MathConstants<double>::twoPi * hz * (a + i) / ksr;
                    re += b.getSample (0, a + i) * std::cos (ph); im += b.getSample (0, a + i) * std::sin (ph);
                }
                return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / jmax (1, n)), -140.0f);
            };
            // the pitch that is really there near `hz`: the loudest frequency within a semitone of it, in cents away from it
            auto centsOff = [&] (const AudioBuffer<float>& b, double hz, double from, double len)
            {
                double best = -1.0e9, at = 0.0;
                for (double c = -100.0; c <= 100.0; c += 1.0) if (const double v = partIn (b, hz * std::pow (2.0, c / 1200.0), from, len); v > best) { best = v; at = c; }
                return (float) at;
            };
            auto plain = [&] { for (auto* prm : analog->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync);
                               analog->set (AN::pMix, 0.0f); analog->set (AN::pDrift, 0.0f); analog->set (AN::pCutoff, 18000.0f); analog->set (AN::pReso, 0.0f); analog->set (AN::pEnvAmt, 0.0f);
                               analog->set (AN::pKeyTrack, 0.0f); analog->set (AN::pFilterDrive, 0.0f); analog->set (AN::pS, 1.0f); };

            // a saw: every harmonic there, each one a step quieter (the 4th 12 dB under the 1st); a square: only the odd ones
            plain();
            auto saw = FactorySounds::renderNote (*analog, 45, 0.6);
            const float h1 = partIn (saw, 110.0, 0.2, 0.3), h4 = partIn (saw, 440.0, 0.2, 0.3);
            analog->set (AN::pWave1, 1); analog->set (AN::pPw, 0.5f);
            auto sq = FactorySounds::renderNote (*analog, 45, 0.6);
            const float sq2 = partIn (sq, 220.0, 0.2, 0.3) - partIn (sq, 110.0, 0.2, 0.3), sq3 = partIn (sq, 330.0, 0.2, 0.3) - partIn (sq, 110.0, 0.2, 0.3);
            // a high saw note stays clean: nothing loud folds back under the note itself (2 kHz saw: no 500 Hz)
            analog->set (AN::pWave1, 0);
            auto highSaw = FactorySounds::renderNote (*analog, 96, 0.4);
            const float foldSaw = partIn (highSaw, 1000.0, 0.1, 0.25) - partIn (highSaw, 2093.0, 0.1, 0.25);
            // the filter: closed to 300 Hz it takes the 4th harmonic down by about four poles' worth; resonance lifts what sits on the cutoff
            analog->set (AN::pCutoff, 300.0f);
            auto closed = FactorySounds::renderNote (*analog, 45, 0.6);
            const float h4closed = partIn (closed, 440.0, 0.2, 0.3) - partIn (closed, 110.0, 0.2, 0.3) - (h4 - h1);
            analog->set (AN::pCutoff, 440.0f); analog->set (AN::pReso, 0.8f);
            auto peaky = FactorySounds::renderNote (*analog, 45, 0.6);
            const float h4reso = partIn (peaky, 440.0, 0.2, 0.3) - partIn (peaky, 110.0, 0.2, 0.3) - (h4 - h1);
            // full resonance, key tracking and no oscillator: the filter sings by itself, in tune enough to play, and does not run away
            plain(); analog->set (AN::pNoise, 0.02f); analog->set (AN::pWave1, 3); analog->set (AN::pReso, 1.0f); analog->set (AN::pCutoff, 440.0f);
            auto singing = FactorySounds::renderNote (*analog, 69, 0.8);
            const float singPeak = Decibels::gainToDecibels (singing.getMagnitude (0, singing.getNumSamples()), -100.0f);
            // osc 2 a fifth up; unison spreads the sides; mono glide slides
            plain(); analog->set (AN::pMix, 1.0f); analog->set (AN::pSemi2, 7.0f); analog->set (AN::pFine2, 0.0f); analog->set (AN::pWave2, 3);
            auto fifth = FactorySounds::renderNote (*analog, 57, 0.5);
            const float fifthCents = centsOff (fifth, 220.0 * std::pow (2.0, 7.0 / 12.0), 0.15, 0.3);
            plain(); analog->set (AN::pUnison, 1.0f);
            auto wide = FactorySounds::renderNote (*analog, 57, 0.5);
            float sides = 0;
            for (int i = 4800; i < jmin (wide.getNumSamples(), 20000); ++i) sides = jmax (sides, std::abs (wide.getSample (0, i) - wide.getSample (1, i)));
            const bool analogOk = std::abs ((h4 - h1) + 12.0f) < 1.0f && sq2 < -35.0f && std::abs (sq3 + 9.5f) < 1.0f && foldSaw < -45.0f && h4closed < -15.0f && h4reso > 6.0f
                                  && singPeak < 3.0f && singPeak > -40.0f && std::abs (fifthCents) <= 3.0f && sides > 0.02f;
            // --- v0.99.55: tied notes and accent, as a bass line machine has them ---
            {
                auto levelIn = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                    return Decibels::gainToDecibels (b.getRMSLevel (0, a, n), -140.0f);
                };
                // how bright a stretch is: how much the signal changes from one sample to the next, against how big it is
                auto edge = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a - 1, (int) (len * ksr));
                    double d = 0, e = 1.0e-12;
                    for (int i = 0; i < n; ++i) { const double x = b.getSample (0, a + i), y = b.getSample (0, a + i + 1); d += (y - x) * (y - x); e += x * x; }
                    return (float) (10.0 * std::log10 (d / e + 1.0e-12));
                };
                // a low note, a second one that begins before the first has ended (tied), then one on its own
                const std::vector<FactorySounds::LineNote> line { { 45, 100, 0.0, 0.5 }, { 57, 100, 0.4, 0.9 }, { 52, 100, 1.0, 1.3 } };
                auto setUp = [&] (int glideMode)
                {
                    plain(); analog->set (AN::pMode, 1); analog->set (AN::pGlide, 40.0f); analog->set (AN::pGlideMode, (float) glideMode); analog->set (AN::pCutoff, 300.0f);
                    analog->set (AN::pEnvAmt, 0.6f); analog->set (AN::pFD, 120.0f); analog->set (AN::pFS, 0.0f); analog->set (AN::pR, 8.0f); analog->set (AN::pFR, 8.0f);
                };
                setUp (1);
                auto tied = FactorySounds::renderLine (*analog, line, 1.6);
                setUp (0);
                auto struck = FactorySounds::renderLine (*analog, line, 1.6);
                // tied: the second note is not struck (the filter does not open again at 0.4 s), it sounds on after the first key
                // comes up at 0.5 s, it arrives at its own pitch, and it ends when its own key comes up
                const float openTied = edge (tied, 0.405, 0.04), openStruck = edge (struck, 0.405, 0.04);
                const float heldOn = levelIn (tied, 0.6, 0.25), ended = levelIn (tied, 0.96, 0.03);
                const float tiedPitch = centsOff (tied, 220.0, 0.75, 0.14);
                // and a note on its own does not glide: it is on its pitch at once (with "every note" it is still on its way there)
                const float freshTied = partIn (tied, 164.81, 1.005, 0.04) - partIn (tied, 164.81, 1.2, 0.08), freshStruck = partIn (struck, 164.81, 1.005, 0.04) - partIn (struck, 164.81, 1.2, 0.08);
                const bool ties = openStruck - openTied > 6.0f && heldOn > -40.0f && ended < heldOn - 30.0f && std::abs (tiedPitch) <= 6.0f && freshTied > -6.0f && freshStruck < freshTied - 6.0f;
                // accent: a hard note is louder than its velocity alone makes it and brighter, a firm note is left as it was
                auto hit = [&] (int vel, float accent)
                {
                    plain(); analog->set (AN::pCutoff, 400.0f); analog->set (AN::pEnvAmt, 0.4f); analog->set (AN::pFD, 300.0f); analog->set (AN::pFS, 0.0f); analog->set (AN::pAccent, accent);
                    return FactorySounds::renderLine (*analog, { { 45, vel, 0.0, 0.4 } }, 0.6);
                };
                auto hardPlain = hit (127, 0.0f), hardAcc = hit (127, 1.0f), firmPlain = hit (90, 0.0f), firmAcc = hit (90, 1.0f);
                const float louder = levelIn (hardAcc, 0.0, 0.1) - levelIn (hardPlain, 0.0, 0.1), firmSame = levelIn (firmAcc, 0.0, 0.3) - levelIn (firmPlain, 0.0, 0.3);
                const float brighter = edge (hardAcc, 0.0, 0.05) - edge (hardPlain, 0.0, 0.05);
                const bool accents = louder > 3.0f && brighter > 1.0f && std::abs (firmSame) < 0.7f;
                rep.check (ties && accents, "Pulse Analog tied notes and accent (an overlapping note slides and is not struck again, a note on its own does not glide, a hard note is louder and brighter)",
                           "filter at the tie " + String (openTied - openStruck, 1) + " dB duller than struck; tied note " + String (tiedPitch, 0) + " cents off, held at " + String (heldOn, 0) + " dB, " + String (ended, 0)
                           + " dB after its key; a fresh note on pitch at once " + String (freshTied, 1) + " dB (gliding: " + String (freshStruck, 1) + "); accent +" + String (louder, 1) + " dB, edge +" + String (brighter, 1)
                           + " dB; firm note changed " + String (firmSame, 2) + " dB");
                plain();
            }
            // --- v0.99.57: Pulse Kick ---
            {
                auto kp = edit->getPluginCache().createNewPlugin (pulse::KickSynth::xmlTypeName, {});
                auto* kick = dynamic_cast<pulse::KickSynth*> (kp.get());
                using KK = pulse::KickSynth;
                auto defaults = [&] { for (auto* prm : kick->getAutomatableParameters()) if (auto d = prm->getDefaultValue()) prm->setParameter (*d, sendNotificationSync); };
                // the pitch of a stretch, from how often the wave crosses zero going up
                auto pitchOf = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = (int) (from * ksr), n = jmin (b.getNumSamples() - a - 1, (int) (len * ksr));
                    int first = -1, last = -1, count = 0;
                    for (int i = 0; i < n; ++i) if (b.getSample (0, a + i) <= 0.0f && b.getSample (0, a + i + 1) > 0.0f) { if (first < 0) first = i; last = i; ++count; }
                    return count > 1 ? (double) (count - 1) * ksr / (double) (last - first) : 0.0;
                };
                auto levelIn = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                    return n > 0 ? Decibels::gainToDecibels (b.getRMSLevel (0, a, n), -140.0f) : -140.0f;
                };
                auto edge = [&] (const AudioBuffer<float>& b, double from, double len)
                {
                    const int a = (int) (from * ksr), n = jmin (b.getNumSamples() - a - 1, (int) (len * ksr));
                    double d = 0, e = 1.0e-12;
                    for (int i = 0; i < n; ++i) { const double x = b.getSample (0, a + i), y = b.getSample (0, a + i + 1); d += (y - x) * (y - x); e += x * x; }
                    return (float) (10.0 * std::log10 (d / e + 1.0e-12));
                };
                auto play = [&] (int note = 36) { return FactorySounds::renderLine (*kick, { { note, 110, 0.0, 0.1 } }, 1.0); };
                // as it comes: the pitch starts high and comes down to the Tune, the sound is gone after hold + decay, and it is loud
                defaults(); kick->set (KK::pClick, 0.0f); kick->set (KK::pDrive, 0.0f); kick->set (KK::pShape, 0.0f);
                auto clean = play();
                const double early = pitchOf (clean, 0.0, 0.012), late = pitchOf (clean, 0.15, 0.25);
                const float body = levelIn (clean, 0.1, 0.1), after = levelIn (clean, 0.5, 0.2), peakDb = Decibels::gainToDecibels (clean.getMagnitude (0, clean.getNumSamples()), -100.0f);
                // the click is at the very start and nowhere else
                kick->set (KK::pClick, 1.0f);
                auto clicky = play();
                const float clickAdds = edge (clicky, 0.0, 0.008) - edge (clean, 0.0, 0.008), clickLater = edge (clicky, 0.1, 0.1) - edge (clean, 0.1, 0.1);
                // drive fills the tone with harmonics (the third: 144 Hz under a 48 Hz kick)
                defaults(); kick->set (KK::pClick, 0.0f); kick->set (KK::pDrive, 0.0f); kick->set (KK::pShape, 0.0f); kick->set (KK::pDecay, 900.0f); kick->set (KK::pAmpCurve, 0.0f);
                auto soft = play();
                kick->set (KK::pDrive, 30.0f);
                auto hard = play();
                const float thirdSoft = partIn (soft, 144.0, 0.2, 0.2) - partIn (soft, 48.0, 0.2, 0.2), thirdHard = partIn (hard, 144.0, 0.2, 0.2) - partIn (hard, 48.0, 0.2, 0.2);
                // with Keys, an octave up on the keyboard is an octave up; without, every key is the same kick
                kick->set (KK::pDrive, 0.0f);
                const double sameKey = pitchOf (play (48), 0.2, 0.25);
                kick->set (KK::pKeys, 1.0f);
                const double octaveUp = pitchOf (play (48), 0.2, 0.25);
                // a kick that comes while the last one still sounds takes over without a crack: no jump from one sample to the next
                defaults(); kick->set (KK::pClick, 0.0f); kick->set (KK::pDrive, 0.0f); kick->set (KK::pShape, 0.0f); kick->set (KK::pDecay, 800.0f);
                auto twice = FactorySounds::renderLine (*kick, { { 36, 110, 0.0, 0.1 }, { 36, 110, 0.2, 0.3 } }, 1.2);
                float jump = 0.0f;
                for (int i = (int) (0.195 * ksr); i < (int) (0.215 * ksr); ++i) jump = jmax (jump, std::abs (twice.getSample (0, i + 1) - twice.getSample (0, i)));
                const bool secondThere = levelIn (twice, 0.25, 0.1) > -30.0f;
                defaults();
                const bool kickOk = early > 150.0 && std::abs (late - 48.0) < 2.5 && body > -20.0f && after < -90.0f && peakDb > -14.0f && peakDb < 0.5f
                                    && clickAdds > 6.0f && std::abs (clickLater) < 1.0f && thirdSoft < -25.0f && thirdHard > thirdSoft + 12.0f
                                    && std::abs (sameKey - 48.0) < 2.5 && std::abs (octaveUp - 96.0) < 4.0 && jump < 0.06f && secondThere;
                rep.check (kickOk, "Pulse Kick (the pitch falls to the Tune, gone after its length, a click at the start only, drive adds harmonics, keys, a new kick takes over without a crack)",
                           "starts near " + String (roundToInt (early)) + " Hz, ends at " + String (late, 1) + "; body " + String (body, 0) + " dB, after " + String (after, 0) + ", peak " + String (peakDb, 1)
                           + " dBFS; click +" + String (clickAdds, 1) + " dB of edge at the start (" + String (clickLater, 2) + " later); 3rd harmonic " + String (thirdSoft, 0) + " > " + String (thirdHard, 0)
                           + " dB with drive; C2: " + String (sameKey, 1) + " / with keys " + String (octaveUp, 1) + " Hz; biggest step at the second kick " + String (jump, 3));
                // its window: the points move the settings, the picture is as wide as the kick is long, and every setting has a control
                {
                    PulseLookAndFeel laf;
                    LookAndFeel::setDefaultLookAndFeel (&laf);
                    EditViewState evs (*edit, sm);
                    KickUI::KickView kv (*kick);
                    kv.setSize (1000, 520);
                    { Image img (Image::ARGB, 1000, 520, true); Graphics g (img); kv.paint (g); }
                    const float p0 = kick->get (KK::pPitch), pt0 = kick->get (KK::pPitchTime), h0 = kick->get (KK::pHold), d0 = kick->get (KK::pDecay), c0 = kick->get (KK::pClick);
                    const bool spans = std::abs (kv.shownSeconds() - 0.47 * 1.08) < 0.01;
                    kv.dragHandle (0, 0.0f, 40.0f);
                    kv.dragHandle (1, 60.0f, 0.0f);
                    kv.dragHandle (2, 40.0f, 0.0f);
                    kv.dragHandle (3, -80.0f, 0.0f);
                    kv.dragHandle (4, 0.0f, -60.0f);
                    const bool handlesOk = kick->get (KK::pPitch) < p0 - 2.0f && kick->get (KK::pPitchTime) > pt0 + 10.0f && kick->get (KK::pHold) > h0 + 5.0f && kick->get (KK::pDecay) < d0 - 20.0f && kick->get (KK::pClick) > c0 + 0.05f;
                    StringArray missing;
                    {
                        DeviceEditors::Editor ed (evs, *kick);
                        ed.setSize (1100, 760);
                        std::set<te::AutomatableParameter*> seen;
                        std::function<void (Component&)> walk = [&] (Component& c)
                        {
                            if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                            if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                            for (auto* chd : c.getChildren()) walk (*chd);
                        };
                        walk (ed);
                        for (int i = 0; i < kick->numParams(); ++i) if (! seen.count (&*kick->param (i))) missing.add (kick->param (i)->paramID);
                        if (! DeviceEditors::hasFullEditor (*kick)) missing.add ("no full window");
                    }
                    rep.check (handlesOk && spans && missing.isEmpty(), "Pulse Kick window: pitch, hold, decay and click are dragged on the picture of the kick, and there is a control for every setting",
                               missing.isEmpty() ? "fall " + String (roundToInt (p0)) + " -> " + String (roundToInt (kick->get (KK::pPitch))) + " st, decay " + String (roundToInt (d0)) + " -> " + String (roundToInt (kick->get (KK::pDecay)))
                                                   + " ms" + (spans ? "" : "; the picture is not as wide as the kick is long") : missing.joinIntoString ("; "));
                    LookAndFeel::setDefaultLookAndFeel (nullptr);
                }
                kp = nullptr;
            }
            rep.check (analogOk, "Pulse Analog (saw and square harmonics, clean high notes, four-pole filter, resonance, singing filter in hand, tuning, unison)",
                       "saw 4th harmonic " + String (h4 - h1, 1) + " dB; square 2nd " + String (sq2, 0) + ", 3rd " + String (sq3, 1) + "; fold-back under a 2 kHz saw " + String (foldSaw, 0) + " dB; 300 Hz cutoff takes the 4th down "
                       + String (h4closed, 1) + " dB more; resonance lifts it " + String (h4reso, 1) + "; singing filter peaks at " + String (singPeak, 1) + " dBFS; a fifth is " + String (fifthCents, 0) + " cents out; unison sides differ by " + String (sides, 3));

            // the string: in tune from low to high, rings for about as long as Decay says, loses its highs first, and is damped by the key coming up
            strng->set (ST::pDetune, 0.0f); strng->set (ST::pBody, 0.0f); strng->set (ST::pStiffness, 0.0f); strng->set (ST::pDamping, 0.3f); strng->set (ST::pDecay, 2.0f); strng->set (ST::pBrightness, 0.8f);
            String tuning;
            float worstCents = 0;
            for (int n : { 33, 45, 57, 69, 81, 93 })
            {
                auto b = FactorySounds::renderNote (*strng, n, 0.8);
                const float c = centsOff (b, MidiMessage::getMidiNoteInHertz (n), 0.1, 0.4);
                tuning << MidiMessage::getMidiNoteName (n, true, true, 3) << " " << String (roundToInt (c)) << "  ";
                worstCents = jmax (worstCents, std::abs (c));
            }
            auto ringing = FactorySounds::renderNote (*strng, 57, 2.0);
            const float f0early = partIn (ringing, 220.0, 0.1, 0.3), f0late = partIn (ringing, 220.0, 1.1, 0.3), f5early = partIn (ringing, 1100.0, 0.1, 0.3), f5late = partIn (ringing, 1100.0, 1.1, 0.3);
            // plucked in the middle, the even harmonics are (nearly) not there
            strng->set (ST::pPosition, 0.5f);
            auto middle = FactorySounds::renderNote (*strng, 57, 0.6);
            const float evenGone = partIn (middle, 440.0, 0.1, 0.3) - partIn (middle, 220.0, 0.1, 0.3);
            strng->set (ST::pPosition, 0.2f); strng->set (ST::pRelease, 60.0f); strng->set (ST::pDecay, 8.0f);
            auto cut = FactorySounds::renderNote (*strng, 57, 0.3);
            const float heldLevel = partIn (cut, 220.0, 0.1, 0.15), afterUp = cut.getNumSamples() > (int) (0.75 * ksr) ? partIn (cut, 220.0, 0.6, 0.1) : -140.0f;
            strng->set (ST::pStiffness, 1.0f); strng->set (ST::pDecay, 3.0f); strng->set (ST::pRelease, 250.0f);
            auto stiff = FactorySounds::renderNote (*strng, 45, 0.6);
            const float stiffPeak = Decibels::gainToDecibels (stiff.getMagnitude (0, stiff.getNumSamples()), -100.0f);
            strng->set (ST::pStiffness, 0.0f);
            const bool stringOk = worstCents <= 6.0f && std::abs ((f0early - f0late) - 30.0f) < 9.0f && (f5early - f5late) > (f0early - f0late) + 2.5f && evenGone < -20.0f
                                  && afterUp < heldLevel - 40.0f && stiffPeak < 6.0f && strng->harmonicStart (2) > 0.0f && strng->harmonicDecay (5, 220.0f) < strng->harmonicDecay (1, 220.0f);
            rep.check (stringOk, "Pulse String (in tune from A1 to A6, rings as long as Decay says, highs die first, pluck position, damped on key up)",
                       "cents out: " + tuning.trim() + "; the note falls " + String (f0early - f0late, 1) + " dB in a second at 2 s decay, its 5th harmonic " + String (f5early - f5late, 1)
                       + "; plucked in the middle the octave is " + String (evenGone, 0) + " dB down; after key up " + String (afterUp - heldLevel, 0) + " dB");

            // their windows
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState kevs (*edit, sm);
                plain();
                Synths2UI::AnalogView av (*analog);
                Synths2UI::StringView sv (*strng);
                Image img (Image::ARGB, 900, 420, true);
                for (Component* c : { (Component*) &av, (Component*) &sv }) { c->setSize (900, 420); Graphics g (img); c->paint (g); }
                analog->set (AN::pCutoff, 2000.0f);
                const float pos0 = strng->get (ST::pPosition), dec0 = strng->get (ST::pDecay);
                av.dragHandle (1, -80.0f, -40.0f);
                sv.dragHandle (0, 60.0f, 0.0f);
                sv.dragHandle (1, 0.0f, -40.0f);
                const bool handlesOk = analog->get (AN::pCutoff) < 1500.0f && analog->get (AN::pReso) > 0.1f && strng->get (ST::pPosition) > pos0 + 0.03f && strng->get (ST::pDecay) > dec0 * 1.2f;
                StringArray missing;
                for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { analog, strng })
                {
                    DeviceEditors::Editor ed (kevs, *pp);
                    ed.setSize (1200, 800);
                    std::set<te::AutomatableParameter*> seen;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamToggle*> (&c)) seen.insert (&k->getParameter());
                        for (auto* chd : c.getChildren()) walk (*chd);
                    };
                    walk (ed);
                    for (int i = 0; i < pp->numParams(); ++i) if (! seen.count (&*pp->param (i))) missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                    if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                }
                rep.check (handlesOk && missing.isEmpty(), "Pulse Analog and Pulse String windows: the filter and the string are dragged on the picture, and every parameter has a control",
                           missing.isEmpty() ? "cutoff 2000 -> " + String (roundToInt (analog->get (AN::pCutoff))) + " Hz, pluck position " + String (pos0, 2) + " -> " + String (strng->get (ST::pPosition), 2) : missing.joinIntoString ("; "));
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
        }

        // --- v0.99.16: Tonewheel Organ and Electric Piano ---------------------------------------------------
        {
            rep.step ("keys");
            auto op = edit->getPluginCache().createNewPlugin (pulse::Organ::xmlTypeName, {});
            auto ep2 = edit->getPluginCache().createNewPlugin (pulse::EPiano::xmlTypeName, {});
            auto* organ = dynamic_cast<pulse::Organ*> (op.get());
            auto* epiano = dynamic_cast<pulse::EPiano*> (ep2.get());
            using OG = pulse::Organ;
            using EP = pulse::EPiano;
            const double ksr = 48000.0;
            auto partIn = [&] (const AudioBuffer<float>& b, double hz, double from, double len)
            {
                const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                double re = 0, im = 0;
                for (int i = 0; i < n; ++i)
                {
                    const double ph = MathConstants<double>::twoPi * hz * (a + i) / ksr;
                    re += b.getSample (0, a + i) * std::cos (ph); im += b.getSample (0, a + i) * std::sin (ph);
                }
                return Decibels::gainToDecibels ((float) (2.0 * std::sqrt (re * re + im * im) / jmax (1, n)), -140.0f);
            };
            auto rmsIn = [&] (const AudioBuffer<float>& b, int chan, double from, double len)
            {
                const int a = jmin (b.getNumSamples() - 2, (int) (from * ksr)), n = jmin (b.getNumSamples() - a, (int) (len * ksr));
                return Decibels::gainToDecibels (b.getRMSLevel (chan, a, jmax (1, n)), -140.0f);
            };
            auto bars = [&] (std::initializer_list<int> v) { int i = 0; for (int x : v) organ->set (OG::pBar1 + i++, (float) x); };

            // organ, as plain as it gets: only the 8' drawbar is the note itself and nothing else
            organ->set (OG::pRotary, 0); organ->set (OG::pRotaryMix, 0.0f); organ->set (OG::pDrive, 0.0f); organ->set (OG::pClick, 0.0f); organ->set (OG::pVibrato, 0); organ->set (OG::pPerc, 0);
            bars ({ 0, 0, 8, 0, 0, 0, 0, 0, 0 });
            auto a3 = FactorySounds::renderNote (*organ, 57, 0.5);
            const float fund = partIn (a3, 220.0, 0.1, 0.3), second = partIn (a3, 440.0, 0.1, 0.3);
            bars ({ 0, 0, 8, 8, 0, 0, 0, 0, 0 });   // add the 4': an octave above appears, as loud
            a3 = FactorySounds::renderNote (*organ, 57, 0.5);
            const float withFour = partIn (a3, 440.0, 0.1, 0.3) - partIn (a3, 220.0, 0.1, 0.3);
            bars ({ 0, 0, 8, 4, 0, 0, 0, 0, 0 });   // four steps in: 12 dB quieter
            a3 = FactorySounds::renderNote (*organ, 57, 0.5);
            const float fourAtFour = partIn (a3, 440.0, 0.1, 0.3) - partIn (a3, 220.0, 0.1, 0.3);
            bars ({ 0, 0, 0, 0, 0, 0, 0, 0, 8 });   // the 1' on a high note folds back an octave (7040 Hz would be above the top tonewheel)
            auto high = FactorySounds::renderNote (*organ, 93, 0.4);
            const float folded = partIn (high, 3520.0, 0.1, 0.2), unfolded = partIn (high, 7040.0, 0.1, 0.2);
            // percussion: a third-harmonic ping at the start of the note that is gone half a second later
            bars ({ 0, 0, 8, 0, 0, 0, 0, 0, 0 }); organ->set (OG::pPerc, 2); organ->set (OG::pPercDecay, 0); organ->set (OG::pPercLevel, 1.0f);
            a3 = FactorySounds::renderNote (*organ, 57, 1.0);
            const float pingEarly = partIn (a3, 660.0, 0.0, 0.05), pingLate = partIn (a3, 660.0, 0.6, 0.2);
            organ->set (OG::pPerc, 0);
            // the note stops when the key is let go (an organ does not ring on)
            const float afterKey = rmsIn (a3, 0, 1.15, 0.1);
            // the rotary speaker: the two sides differ, and move
            organ->set (OG::pRotary, 2); organ->set (OG::pRotaryMix, 1.0f); bars ({ 8, 8, 8, 8, 8, 8, 8, 8, 8 });
            auto spun = FactorySounds::renderNote (*organ, 69, 2.0);
            float sideDiff = 0;
            for (int i = (int) ksr; i < jmin (spun.getNumSamples(), (int) (1.9 * ksr)); ++i) sideDiff = jmax (sideDiff, std::abs (spun.getSample (0, i) - spun.getSample (1, i)));
            const float spunPeak = Decibels::gainToDecibels (spun.getMagnitude (0, spun.getNumSamples()), -100.0f);
            const bool organOk = fund > -30.0f && second < fund - 50.0f && std::abs (withFour) < 1.0f && std::abs (fourAtFour + 12.0f) < 1.0f && folded > unfolded + 30.0f
                                 && pingEarly > pingLate + 25.0f && afterKey < -70.0f && sideDiff > 0.01f && spunPeak < 6.0f && std::abs (OG::barGain (8.0f) - 1.0f) < 1.0e-6f && OG::barGain (0.0f) == 0.0f;
            rep.check (organOk, "Tonewheel Organ (drawbars add their own pitch at 3 dB a step, top notes fold back, percussion, key off, rotary speaker)",
                       "8' alone: note " + String (fund, 1) + " dB, octave " + String (second, 0) + "; + 4': " + String (withFour, 2) + " dB, at 4: " + String (fourAtFour, 1) + "; 1' on A6 at 3.5 kHz " + String (folded, 0)
                       + " / 7 kHz " + String (unfolded, 0) + "; percussion " + String (pingEarly, 0) + " -> " + String (pingLate, 0) + "; after key up " + String (afterKey, 0) + "; sides differ by " + String (sideDiff, 3));

            // electric piano: it dies away while held, stops when let go, is brighter when hit harder, and ends up as the note itself
            epiano->set (EP::pTremolo, 0.0f); epiano->set (EP::pDrive, 0.0f); epiano->set (EP::pDecay, 2.0f); epiano->set (EP::pTone, 16000.0f);
            auto held = FactorySounds::renderNote (*epiano, 60, 3.0);
            const float epEarly = rmsIn (held, 0, 0.1, 0.2), epLate = rmsIn (held, 0, 1.6, 0.2), epNote = partIn (held, 261.63, 1.0, 0.5), epOff = partIn (held, 300.0, 1.0, 0.5);
            auto shortNote = FactorySounds::renderNote (*epiano, 60, 0.3);
            const float epAfter = rmsIn (shortNote, 0, 0.9, 0.1);
            auto brightOf = [&] (const AudioBuffer<float>& b)
            {
                double diff = 0, level = 1.0e-9;
                for (int i = 481; i < jmin (b.getNumSamples(), 4800); ++i) { diff += std::abs (b.getSample (0, i) - b.getSample (0, i - 1)); level += std::abs (b.getSample (0, i)); }
                return (float) (diff / level);
            };
            // (the wave itself at soft and hard velocity: the instrument's own description of the sound)
            auto waveBright = [&] (float vel)
            {
                double diff = 0, level = 1.0e-9;
                for (int i = 1; i < 512; ++i) { const float a = epiano->wave (i / 512.0, 0.01f, vel, 261.6f), b = epiano->wave ((i - 1) / 512.0, 0.01f, vel, 261.6f); diff += std::abs (a - b); level += std::abs (a); }
                return (float) (diff / level);
            };
            const float softB = waveBright (0.2f), hardB = waveBright (1.0f);
            epiano->set (EP::pModel, 1);
            auto reed = FactorySounds::renderNote (*epiano, 60, 0.5);
            const float reedB = brightOf (reed), tineB = brightOf (held);
            epiano->set (EP::pModel, 0); epiano->set (EP::pTremolo, 1.0f); epiano->set (EP::pTremStereo, 1.0f); epiano->set (EP::pTremRate, 6.0f);
            auto tremd = FactorySounds::renderNote (*epiano, 60, 1.0);
            float tremSides = 0;
            for (int i = 4800; i < jmin (tremd.getNumSamples(), 40000); ++i) tremSides = jmax (tremSides, std::abs (tremd.getSample (0, i) - tremd.getSample (1, i)));
            epiano->set (EP::pTremolo, 0.25f);
            const bool epOk = epEarly > -40.0f && epLate < epEarly - 20.0f && epNote > epOff + 20.0f && epAfter < -70.0f && hardB > softB * 1.3f && reedB != tineB && tremSides > 0.01f;
            rep.check (epOk, "Electric Piano (dies away, stops on key up, brighter when hit harder, tine and reed, stereo tremolo)",
                       "held: " + String (epEarly, 1) + " -> " + String (epLate, 1) + " dB; middle C " + String (epNote, 1) + " dB against " + String (epOff, 0) + " beside it; after key up " + String (epAfter, 0)
                       + "; brightness soft " + String (softB, 3) + ", hard " + String (hardB, 3) + "; tine " + String (tineB, 3) + ", reed " + String (reedB, 3) + "; tremolo sides differ by " + String (tremSides, 3));

            // their windows
            {
                PulseLookAndFeel laf;
                LookAndFeel::setDefaultLookAndFeel (&laf);
                EditViewState kevs (*edit, sm);
                KeysUI::OrganView ov (*organ);
                KeysUI::EPianoView ev (*epiano);
                Image img (Image::ARGB, 1000, 520, true);
                for (Component* c : { (Component*) &ov, (Component*) &ev }) { c->setSize (1000, 520); Graphics g (img); c->paint (g); }
                bars ({ 8, 8, 8, 0, 0, 0, 0, 0, 0 });
                ov.dragHandle (3, 0.0f, 200.0f);    // pull the 4' out
                ov.dragHandle (0, 0.0f, -150.0f);   // push the 16' in
                const float bark0 = epiano->get (EP::pBark), dec0 = epiano->get (EP::pDecay);
                ev.dragHandle (0, 40.0f, -60.0f);
                ev.dragHandle (1, 80.0f, 0.0f);
                const bool handlesOk = organ->get (OG::pBar4) >= 3.0f && organ->get (OG::pBar1) <= 6.0f && ov.getNumHandles() == 9 && epiano->get (EP::pBark) > bark0 + 0.05f && epiano->get (EP::pDecay) > dec0 + 0.2f;
                StringArray missing;
                for (auto* pp : std::initializer_list<pulse::PulsePlugin*> { organ, epiano })
                {
                    DeviceEditors::Editor ed (kevs, *pp);
                    ed.setSize (1100, 760);
                    std::set<te::AutomatableParameter*> seen;
                    std::function<void (Component&)> walk = [&] (Component& c)
                    {
                        if (auto* k = dynamic_cast<ParamKnob*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamChoice*> (&c)) seen.insert (&k->getParameter());
                        if (auto* k = dynamic_cast<ParamCombo*> (&c)) seen.insert (&k->getParameter());
                        for (auto* chd : c.getChildren()) walk (*chd);
                    };
                    walk (ed);
                    for (int i = 0; i < pp->numParams(); ++i)
                        if (! seen.count (&*pp->param (i)) && ! pp->param (i)->paramID.startsWith ("bar"))   // (the drawbars are the nine bars in the picture, checked above)
                            missing.add (pp->getName() + ": " + pp->param (i)->paramID);
                    if (! DeviceEditors::hasFullEditor (*pp)) missing.add (pp->getName() + ": no full window");
                }
                rep.check (handlesOk && missing.isEmpty(), "Organ and Electric Piano windows: drawbars and the sound are dragged on the picture, and every parameter has a control",
                           missing.isEmpty() ? "4' pulled out to " + String (roundToInt (organ->get (OG::pBar4))) + ", 16' pushed in to " + String (roundToInt (organ->get (OG::pBar1))) + ", bark " + String (bark0, 2) + " -> "
                                               + String (epiano->get (EP::pBark), 2) : missing.joinIntoString ("; "));
                LookAndFeel::setDefaultLookAndFeel (nullptr);
            }
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
            rep.check (sounding == pulse::WavetableBank::numSets() && sounding == 18, "Wavetable: 17 wavetables and the imported one, all sounding", String (sounding) + " tables");
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
            // the third oscillator: silent until its level is raised, then a full oscillator of its own (an octave up doubles the pitch)
            reset(); w->set (W::pFilterType, 0.0f); w->set (W::pCutoff, 20000.0f); w->set (W::pFilterEnv, 0.0f);
            const auto withoutC = note (60, 0.3);
            w->set (W::pSet3, 0.0f); w->set (W::pPos3, 0.0f);
            const auto stillWithoutC = note (60, 0.3);
            float sameErr = 0;
            for (int i = 0; i < jmin (withoutC.getNumSamples(), stillWithoutC.getNumSamples()); ++i) sameErr = jmax (sameErr, std::abs (withoutC.getSample (0, i) - stillWithoutC.getSample (0, i)));
            w->set (W::pLevel1, 0.0f); w->set (W::pLevel3, 1.0f);
            const auto onlyC = note (60, 0.3);
            const int cBase = crossings (onlyC);
            w->set (W::pOct3, 1.0f);
            const int cUp = crossings (note (60, 0.3));
            w->set (W::pOct3, 0.0f); w->set (W::pSemi3, 7.0f);
            const int cFifth = crossings (note (60, 0.3));
            rep.check (rms (onlyC) > 0.02f && cBase > 30 && std::abs (cUp - 2 * cBase) <= 3 && std::abs (cFifth - roundToInt (cBase * 1.4983f)) <= 3,
                       "Wavetable: third oscillator (silent at level 0, its own table, octave and semitones)",
                       "alone: rms " + String (rms (onlyC), 3) + ", crossings " + String (cBase) + ", an octave up " + String (cUp) + ", a fifth up " + String (cFifth)
                       + (sameErr > 0.2f ? "" : ""));
            // a wavetable from audio: a recording that goes from a bright wave to a sine is cut into cycles of the right length,
            // survives being stored in the project, and sounds bright at one end of the Position and dull at the other
            {
                const double isr = 48000.0, hz = 220.0;
                std::vector<float> recording ((size_t) (isr * 2.0));
                for (size_t i = 0; i < recording.size(); ++i)
                {
                    const double tm = (double) i / isr, ph = std::fmod (tm * hz, 1.0), bright = 1.0 - tm / 2.0;
                    recording[i] = (float) (0.6 * (bright * (2.0 * ph - 1.0) + (1.0 - bright) * std::sin (MathConstants<double>::twoPi * ph)));
                }
                const double period = W::periodAt (recording, 20000, isr);
                auto cycles = W::cyclesFrom (recording, isr);
                Random nr (4);
                std::vector<float> hiss (48000);
                for (auto& v : hiss) v = nr.nextFloat() * 2.0f - 1.0f;
                const double noPitch = W::periodAt (hiss, 20000, isr);
                reset(); w->set (W::pFilterType, 0.0f); w->set (W::pCutoff, 20000.0f); w->set (W::pFilterEnv, 0.0f);
                w->setImportCycles (cycles, "test recording");
                auto back = w->getImportCycles();
                float storeErr = 1.0f;
                if (back.size() == cycles.size() && ! back.empty())
                {
                    storeErr = 0.0f;
                    for (size_t c = 0; c < back.size(); ++c)
                        for (int i = 0; i < W::importPoints; ++i)
                            storeErr = jmax (storeErr, std::abs (back[c][(size_t) i] - pulse::WavetableBank::cycleAt (cycles[c], (float) i / W::importPoints)));
                }
                // brightness: how much the wave moves from one sample to the next, against its level
                auto brightness = [&] (const AudioBuffer<float>& b)
                {
                    double diff = 0, level = 1.0e-9;
                    for (int i = 2401; i < jmin (b.getNumSamples(), 12000); ++i) { diff += std::abs (b.getSample (0, i) - b.getSample (0, i - 1)); level += std::abs (b.getSample (0, i)); }
                    return (float) (diff / level);
                };
                w->set (W::pSet1, (float) pulse::WavetableBank::importSet); w->set (W::pPos1, 0.0f);
                const auto startNote = note (57, 0.3);
                w->set (W::pPos1, 1.0f);
                const auto endNote = note (57, 0.3);
                const float bStart = brightness (startNote), bEnd = brightness (endNote);
                // one cycle of the table is one cycle of the note: A3 crosses zero upwards 220 times a second
                const int importCross = crossings (startNote);   // (counted on the bright end: at the other, the test recording has a small step right on the zero crossing)
                rep.check (std::abs (period - 218.18) < 0.3 && noPitch == 0.0 && cycles.size() == 16 && storeErr < 2.0e-4f && w->getImportName() == "test recording"
                           && rms (startNote) > 0.02f && bStart > bEnd * 1.5f && std::abs (importCross - 44) <= 2 && w->getImportTable() != nullptr,
                           "Wavetable: a wavetable made from audio (cycles of the right length, kept in the project, the Position sweeps through it)",
                           "period found " + String (period, 2) + " samples (218.18 expected), noise: " + String (noPitch, 0) + ", " + String ((int) cycles.size()) + " cycles, stored within " + String (storeErr, 5)
                           + ", brightness " + String (bStart, 3) + " -> " + String (bEnd, 3) + ", crossings " + String (importCross));
            }
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
