#pragma once

#include "Common.h"
#include "DrumRack.h"
#include "PulseSynths.h"
#include "FactoryPresets.h"
#include "FactorySounds.h"

/** Shared helpers for turning files / plugin IDs into clips, tracks and devices. */
namespace ClipImport
{
    inline bool isMidiFile (const File& f)   { return f.hasFileExtension ("mid;midi"); }
    inline bool isAudioFile (const File& f)  { return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3;bwf"); }
    inline bool isImportable (const File& f) { return isMidiFile (f) || isAudioFile (f); }

    /** Imports an audio or MIDI file into a track or clip slot at the given time.
        Returns the new clip (or nullptr). */
    inline te::Clip::Ptr importFile (te::ClipOwner& owner, te::Edit& edit, const File& f, te::TimePosition start)
    {
        if (isMidiFile (f))
        {
            if (auto mc = te::createClipFromFile (f, owner, false))
            {
                mc->setStart (start, false, true);
                mc->setName (f.getFileNameWithoutExtension());
                return mc;
            }
            return {};
        }

        te::AudioFile af (edit.engine, f);
        if (! af.isValid())
            return {};

        return te::insertWaveClip (owner, f.getFileNameWithoutExtension(), f,
                                   { { start, te::TimeDuration::fromSeconds (af.getLength()) }, {} },
                                   te::DeleteExistingClips::no);
    }

    /** Imports several files one after another on the same track. */
    inline void importFiles (te::ClipOwner& owner, te::Edit& edit, const Array<File>& files, te::TimePosition start)
    {
        for (auto& f : files)
            if (auto c = importFile (owner, edit, f, start))
                start = c->getPosition().getEnd();
    }

    /** Creates a new clip track placed after the last content track (i.e. before the returns). */
    inline te::AudioTrack* createContentTrack (te::Edit& edit, const String& name, bool withInstrument)
    {
        auto content = EngineHelpers::getUserTracks (edit);
        auto t = edit.insertNewAudioTrack (te::TrackInsertPoint (nullptr, content.getLast()), nullptr);
        if (t == nullptr)
            return nullptr;

        t->setName (name);
        t->setColour (Theme::trackColour (content.size()));

        if (withInstrument)
            if (auto p = edit.getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}))
                t->pluginList.insertPlugin (p, 0, nullptr);

        return t.get();
    }

    //==============================================================================
    // Plugin IDs used for drag & drop: "plugin:builtin:<xmlType>" or "plugin:ext:<identifier>"
    struct BuiltIn { const char* name; const char* type; bool synth; bool midiFx = false; };

    inline const std::vector<BuiltIn>& builtIns()
    {
        static const std::vector<BuiltIn> list
        {
            { "Drum Rack",    "drumrack",                          true },
            { "Drum Rack (synth kit)", "drumrackkit",              true },
            { "Drum Rack (sampled kit)", "drumracksamples",        true },
            { "Drum Synth",   pulse::DrumSynth::xmlTypeName,       true },
            { "Wavetable",    pulse::WavetableSynth::xmlTypeName,  true },
            { "FM Synth",     pulse::FMSynth::xmlTypeName,         true },
            { "Pulse Analog", pulse::Analog::xmlTypeName,          true },
            { "Pulse String", pulse::StringModel::xmlTypeName,     true },
            { "Simpler",      pulse::Simpler::xmlTypeName,         true },
            { "Electric Piano", pulse::EPiano::xmlTypeName,        true },
            { "Tonewheel Organ", pulse::Organ::xmlTypeName,        true },
            { "4OSC Synth",   te::FourOscPlugin::xmlTypeName,      true },
            { "Sampler",      te::SamplerPlugin::xmlTypeName,      true },
            { "EQ Six",       pulse::PulseEQ::xmlTypeName,         false },
            { "Prism EQ",     pulse::PrismEQ::xmlTypeName,         false },
            { "Pulse Compressor", pulse::ProCompressor::xmlTypeName, false },
            { "Multiband Dynamics", pulse::MultibandDynamics::xmlTypeName, false },
            { "Glue Compressor", pulse::GlueCompressor::xmlTypeName, false },
            { "Saturator",    pulse::Saturator::xmlTypeName,       false },
            { "Pulse Amp",    pulse::Amp::xmlTypeName,             false },
            { "Auto Filter",  pulse::AutoFilter::xmlTypeName,      false },
            { "Limiter",      pulse::Limiter::xmlTypeName,         false },
            { "Pulse Gate",   pulse::Gate::xmlTypeName,            false },
            { "Transient Shaper", pulse::TransientShaper::xmlTypeName, false },
            { "De-esser",     pulse::DeEsser::xmlTypeName,         false },
            { "Pulse Reverb", pulse::Reverb::xmlTypeName,          false },
            { "Pulse Space",  pulse::ConvolutionReverb::xmlTypeName, false },
            { "Pulse Delay",  pulse::Delay::xmlTypeName,           false },
            { "Shimmer",      pulse::Shimmer::xmlTypeName,         false },
            { "Grain Delay",  pulse::GrainDelay::xmlTypeName,      false },
            { "Resonators",   pulse::Resonators::xmlTypeName,      false },
            { "Pulse Spectral", pulse::Spectral::xmlTypeName,      false },
            { "Beat Repeat",  pulse::BeatRepeat::xmlTypeName,      false },
            { "Looper",       pulse::Looper::xmlTypeName,          false },
            { "Pulse Chorus", pulse::Chorus::xmlTypeName,          false },
            { "Phaser-Flanger", pulse::PhaserFlanger::xmlTypeName, false },
            { "Auto Pan",     pulse::AutoPan::xmlTypeName,         false },
            { "Frequency Shifter", pulse::FreqShifter::xmlTypeName, false },
            { "Redux",        pulse::Redux::xmlTypeName,           false },
            { "Exciter",      pulse::Exciter::xmlTypeName,         false },
            { "Vocoder",      pulse::Vocoder::xmlTypeName,         false },
            { "Pitch Correction", pulse::PitchCorrect::xmlTypeName, false },
            { "Utility",      pulse::Utility::xmlTypeName,         false },
            { "Tuner",        pulse::Tuner::xmlTypeName,           false },
            { "Spectrum",     pulse::Spectrum::xmlTypeName,        false },
            { "Arpeggiator",  pulse::Arpeggiator::xmlTypeName,     false, true },
            { "Chord",        pulse::ChordFx::xmlTypeName,         false, true },
            { "Scale",        pulse::ScaleFx::xmlTypeName,         false, true },
            { "Note Echo",    pulse::NoteEcho::xmlTypeName,        false, true },
            { "Equaliser",    te::EqualiserPlugin::xmlTypeName,    false },
            { "Compressor",   te::CompressorPlugin::xmlTypeName,   false },
            { "Reverb",       te::ReverbPlugin::xmlTypeName,       false },
            { "Delay",        te::DelayPlugin::xmlTypeName,        false },
            { "Chorus",       te::ChorusPlugin::xmlTypeName,       false },
            { "Phaser",       te::PhaserPlugin::xmlTypeName,       false },
            { "Low/High Pass",te::LowPassPlugin::xmlTypeName,      false },
            { "Pitch Shift",  te::PitchShiftPlugin::xmlTypeName,   false },
            { "Volume & Pan", te::VolumeAndPanPlugin::xmlTypeName, false },
        };
        return list;
    }

    inline bool isPluginDescription (const var& d) { return d.toString().startsWith ("plugin:"); }

    /** A readable track name for a device dragged from the browser. */
    inline String trackNameFor (te::Engine& engine, const String& d)
    {
        if (d == "plugin:builtin:drumrack" || d == "plugin:builtin:drumrackkit" || d == "plugin:builtin:drumracksamples") return "Drums";
        if (d.startsWith ("plugin:builtin:"))
        {
            const auto type = d.fromFirstOccurrenceOf ("plugin:builtin:", false, false);
            if (type == pulse::DrumSynth::xmlTypeName) return "Drums";
            for (auto& b : builtIns()) if (type == b.type) return b.name;
            return "Track";
        }
        if (d.startsWith ("plugin:factory:")) return d.fromLastOccurrenceOf (":", false, false);
        if (d.startsWith ("plugin:chain:") || d.startsWith ("plugin:track:"))
            return File (d.fromFirstOccurrenceOf (":", false, false).fromFirstOccurrenceOf (":", false, false)).getFileNameWithoutExtension();
        if (d.startsWith ("plugin:ext:"))
            if (auto desc = engine.getPluginManager().knownPluginList.getTypeForIdentifierString (d.fromFirstOccurrenceOf ("plugin:ext:", false, false)))
                return desc->name;
        return "Track";
    }

    /** Marks a track as named by Pulse (not the user): it then follows its instrument's name. */
    inline const Identifier autoNameID { "pulseAutoName" }, autoInstID { "pulseAutoInst" };

    /** Remembers a track's current instrument so only a later change renames it. */
    inline void noteInstrument (te::AudioTrack& t)
    {
        for (auto p : t.pluginList)
            if (p->isSynth() || dynamic_cast<te::RackInstance*> (p) != nullptr) { t.state.setProperty (autoInstID, p->itemID.toString(), nullptr); return; }
    }

    /** Turns a raw id that ended up as a name ("pulseDrumSynth", "drumrackkit", "plugin:builtin:x")
        into plain words; other names are returned unchanged. */
    inline String friendlyName (te::Engine& engine, const String& raw)
    {
        auto n = raw.trim();
        if (n.startsWith ("plugin:")) return trackNameFor (engine, n);
        const auto lower = n.toLowerCase();
        if (lower == "drumrack" || lower == "drumrackkit" || lower == "drum rack kit") return "Drums";
        for (auto& b : builtIns())
            if (n == b.type) return trackNameFor (engine, String ("plugin:builtin:") + b.type);
        if (n.startsWith ("pulse") && n.length() > 5 && CharacterFunctions::isUpperCase (n[5]))   // pulseSomeDevice -> Some Device
        {
            String out;
            auto rest = n.substring (5);
            for (int i = 0; i < rest.length(); ++i)
            {
                if (i > 0 && CharacterFunctions::isUpperCase (rest[i]) && CharacterFunctions::isLowerCase (rest[i - 1])) out << ' ';
                out << rest[i];
            }
            return out;
        }
        return n;
    }

    /** The name a track gets from its instrument ("Drums" for drum devices). */
    inline String instrumentTrackName (te::Plugin& p)
    {
        const auto type = p.getPluginType();
        if (type == pulse::DrumSynth::xmlTypeName || dynamic_cast<te::RackInstance*> (&p) != nullptr) return "Drums";
        return p.getName();
    }

    /** Renames tracks that carry a raw id as their name (projects from before 0.36). */
    inline int fixRawTrackNames (te::Edit& edit)
    {
        int fixed = 0;
        for (auto t : EngineHelpers::getUserTracks (edit))
        {
            const auto n = friendlyName (edit.engine, t->getName());
            if (n != t->getName())
            {
                t->setName (n);
                t->state.setProperty (autoNameID, true, nullptr);
                ++fixed;
            }
        }
        return fixed;
    }

    /** Audio on a track whose instrument would ignore it: true for audio files dropped on a
        track that has an instrument (they go onto a new audio track instead). */
    inline bool needsOwnAudioTrack (te::AudioTrack& t, const File& f)
    {
        if (isMidiFile (f)) return false;
        for (auto p : t.pluginList)
            if (p->isSynth() || dynamic_cast<te::RackInstance*> (p) != nullptr) return true;
        return false;
    }

    inline te::Plugin::Ptr createPluginFromDescription (te::Edit& edit, const String& d)
    {
        if (d == "plugin:builtin:drumrack")
            return DrumRack::create (edit);
        if (d == "plugin:builtin:drumrackkit")
            return DrumRack::create (edit, true);
        if (d == "plugin:builtin:drumracksamples")
        {
            auto p = DrumRack::create (edit);
            if (auto ri = dynamic_cast<te::RackInstance*> (p.get()); ri != nullptr && ri->type != nullptr)
            {
                FactorySounds::loadKit (*ri->type, 0);
                DrumRack::rewire (*ri->type);
            }
            return p;
        }

        if (d.startsWith ("plugin:factory:"))   // plugin:factory:<type>:<preset>
        {
            auto rest = d.fromFirstOccurrenceOf ("plugin:factory:", false, false);
            auto p = edit.getPluginCache().createNewPlugin (rest.upToFirstOccurrenceOf (":", false, false), {});
            if (p != nullptr)
            {
                const auto name = rest.fromFirstOccurrenceOf (":", false, false);
                FactoryPresets::apply (*p, name);
                if (auto s = dynamic_cast<pulse::Simpler*> (p.get())) FactorySounds::fillSimpler (*s, FactorySounds::instrumentFor (name));
            }
            return p;
        }

        if (d.startsWith ("plugin:builtin:"))
            return edit.getPluginCache().createNewPlugin (d.fromFirstOccurrenceOf ("plugin:builtin:", false, false), {});

        if (d.startsWith ("plugin:ext:"))
            if (auto desc = edit.engine.getPluginManager().knownPluginList.getTypeForIdentifierString (d.fromFirstOccurrenceOf ("plugin:ext:", false, false)))
                return sandbox::createPlugin (edit, *desc);

        return {};
    }

    /** Files carried by a drag from the browser's file tree. */
    /** A component that drags files (besides FileTreeComponent), e.g. the browser's search results. */
    struct FileDragSource
    {
        virtual ~FileDragSource() = default;
        virtual Array<File> getDraggedFiles() = 0;
    };

    inline Array<File> filesFromDrag (const DragAndDropTarget::SourceDetails& details)
    {
        Array<File> files;

        if (auto* src = dynamic_cast<FileDragSource*> (details.sourceComponent.get()))
            for (auto& f : src->getDraggedFiles())
                if (isImportable (f))
                    files.add (f);

        if (auto* tree = dynamic_cast<FileTreeComponent*> (details.sourceComponent.get()))
            for (int i = 0; i < tree->getNumSelectedFiles(); ++i)
                if (isImportable (tree->getSelectedFile (i)))
                    files.add (tree->getSelectedFile (i));

        return files;
    }
}
