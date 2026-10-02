#pragma once

#include "Common.h"
#include "DrumRack.h"
#include "PulseSynths.h"

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
            { "Wavetable",    pulse::WavetableSynth::xmlTypeName,  true },
            { "Simpler",      pulse::Simpler::xmlTypeName,         true },
            { "4OSC Synth",   te::FourOscPlugin::xmlTypeName,      true },
            { "Sampler",      te::SamplerPlugin::xmlTypeName,      true },
            { "EQ Six",       pulse::PulseEQ::xmlTypeName,         false },
            { "Glue Compressor", pulse::GlueCompressor::xmlTypeName, false },
            { "Saturator",    pulse::Saturator::xmlTypeName,       false },
            { "Auto Filter",  pulse::AutoFilter::xmlTypeName,      false },
            { "Limiter",      pulse::Limiter::xmlTypeName,         false },
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

    inline te::Plugin::Ptr createPluginFromDescription (te::Edit& edit, const String& d)
    {
        if (d == "plugin:builtin:drumrack")
            return DrumRack::create (edit);

        if (d.startsWith ("plugin:builtin:"))
            return edit.getPluginCache().createNewPlugin (d.fromFirstOccurrenceOf ("plugin:builtin:", false, false), {});

        if (d.startsWith ("plugin:ext:"))
            if (auto desc = edit.engine.getPluginManager().knownPluginList.getTypeForIdentifierString (d.fromFirstOccurrenceOf ("plugin:ext:", false, false)))
                return edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, *desc);

        return {};
    }

    /** Files carried by a drag from the browser's file tree. */
    inline Array<File> filesFromDrag (const DragAndDropTarget::SourceDetails& details)
    {
        Array<File> files;

        if (auto* tree = dynamic_cast<FileTreeComponent*> (details.sourceComponent.get()))
            for (int i = 0; i < tree->getNumSelectedFiles(); ++i)
                if (isImportable (tree->getSelectedFile (i)))
                    files.add (tree->getSelectedFile (i));

        return files;
    }
}
