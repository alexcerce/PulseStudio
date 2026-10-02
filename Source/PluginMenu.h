#pragma once

#include "Common.h"
#include "PulseSynths.h"

/** Popup menu listing built-in Tracktion devices plus every scanned VST3,
    grouped by manufacturer. Calls back with the newly created plugin. */
namespace PluginMenu
{
    using Callback = std::function<void (te::Plugin::Ptr)>;

    /** Set at startup (avoids PluginMenu depending on the drum rack code). */
    inline std::function<te::Plugin::Ptr (te::Edit&, bool withSynthKit)> drumRackFactory;

    template <typename PluginType>
    inline void addBuiltIn (PopupMenu& m, te::Edit& edit, const Callback& cb)
    {
        m.addItem (TRANS (PluginType::getPluginName()), [&edit, cb]
        {
            if (auto p = edit.getPluginCache().createNewPlugin (PluginType::xmlTypeName, {}))
                cb (p);
        });
    }

    inline void addTree (PopupMenu& m, te::Edit& edit, const KnownPluginList::PluginTree& tree,
                         bool instruments, const Callback& cb)
    {
        for (auto* sub : tree.subFolders)
        {
            PopupMenu subMenu;
            addTree (subMenu, edit, *sub, instruments, cb);

            if (subMenu.getNumItems() > 0)
                m.addSubMenu (sub->folder, subMenu);
        }

        for (const auto& desc : tree.plugins)
        {
            if (desc.isInstrument != instruments)
                continue;

            m.addItem (desc.name, [&edit, desc, cb]
            {
                if (auto p = sandbox::createPlugin (edit, desc))
                    cb (p);
            });
        }
    }

    inline void show (te::Edit& edit, Component* target, Callback cb)
    {
        auto& list = edit.engine.getPluginManager().knownPluginList;
        auto tree = list.createTree (list.getTypes(), KnownPluginList::sortByManufacturer);

        PopupMenu instruments, effects, midiFx, vstInst, vstFx;
        addBuiltIn<pulse::Arpeggiator> (midiFx, edit, cb);
        addBuiltIn<pulse::ChordFx> (midiFx, edit, cb);
        addBuiltIn<pulse::ScaleFx> (midiFx, edit, cb);
        addBuiltIn<pulse::NoteEcho> (midiFx, edit, cb);

        if (drumRackFactory)
        {
            instruments.addItem ("Drum Rack", [&edit, cb] { if (auto p = drumRackFactory (edit, false)) cb (p); });
            instruments.addItem ("Drum Rack (synth kit)", [&edit, cb] { if (auto p = drumRackFactory (edit, true)) cb (p); });
        }
        addBuiltIn<pulse::DrumSynth> (instruments, edit, cb);
        addBuiltIn<pulse::WavetableSynth> (instruments, edit, cb);
        addBuiltIn<pulse::FMSynth> (instruments, edit, cb);
        addBuiltIn<pulse::Simpler> (instruments, edit, cb);
        addBuiltIn<te::FourOscPlugin> (instruments, edit, cb);
        addBuiltIn<te::SamplerPlugin> (instruments, edit, cb);

        addBuiltIn<pulse::PulseEQ> (effects, edit, cb);
        addBuiltIn<pulse::PrismEQ> (effects, edit, cb);
        addBuiltIn<pulse::GlueCompressor> (effects, edit, cb);
        addBuiltIn<pulse::Saturator> (effects, edit, cb);
        addBuiltIn<pulse::AutoFilter> (effects, edit, cb);
        addBuiltIn<pulse::Limiter> (effects, edit, cb);
        addBuiltIn<pulse::Reverb> (effects, edit, cb);
        addBuiltIn<pulse::Delay> (effects, edit, cb);
        addBuiltIn<pulse::Vocoder> (effects, edit, cb);
        addBuiltIn<pulse::PitchCorrect> (effects, edit, cb);
        addBuiltIn<pulse::Utility> (effects, edit, cb);
        addBuiltIn<pulse::Tuner> (effects, edit, cb);
        addBuiltIn<pulse::Spectrum> (effects, edit, cb);
        effects.addSeparator();
        addBuiltIn<te::EqualiserPlugin> (effects, edit, cb);
        addBuiltIn<te::CompressorPlugin> (effects, edit, cb);
        addBuiltIn<te::ReverbPlugin> (effects, edit, cb);
        addBuiltIn<te::DelayPlugin> (effects, edit, cb);
        addBuiltIn<te::ChorusPlugin> (effects, edit, cb);
        addBuiltIn<te::PhaserPlugin> (effects, edit, cb);
        addBuiltIn<te::LowPassPlugin> (effects, edit, cb);
        addBuiltIn<te::PitchShiftPlugin> (effects, edit, cb);
        addBuiltIn<te::VolumeAndPanPlugin> (effects, edit, cb);

        if (tree != nullptr)
        {
            addTree (vstInst, edit, *tree, true, cb);
            addTree (vstFx, edit, *tree, false, cb);
        }

        PopupMenu m;
        m.addSectionHeader ("Built-in");
        m.addSubMenu ("Instruments", instruments);
        m.addSubMenu ("MIDI Effects", midiFx);
        m.addSubMenu ("Audio Effects", effects);
        m.addSectionHeader ("VST3");

        if (vstInst.getNumItems() + vstFx.getNumItems() == 0)
        {
            m.addItem ("No plugins scanned yet - use \"Plugins...\" in the toolbar", false, false, nullptr);
        }
        else
        {
            m.addSubMenu ("Instruments", vstInst);
            m.addSubMenu ("Audio Effects", vstFx);
        }

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (target));
    }

    /** Index where new effects go: just before the track's volume/pan fader. */
    inline int insertIndexFor (te::PluginList& list, te::Track* track)
    {
        if (auto at = dynamic_cast<te::AudioTrack*> (track))
            if (auto vp = at->getVolumePlugin())
                if (auto idx = list.indexOf (vp); idx >= 0)
                    return idx;

        if (track == nullptr)   // the master: before its meter, so the meter shows the result
            if (auto lm = list.findFirstPluginOfType<te::LevelMeterPlugin>())
                if (auto idx = list.indexOf (lm); idx >= 0)
                    return idx;

        return list.size();
    }

    /** Instruments go at the very front of the chain. */
    inline void insertPlugin (te::Track* track, te::PluginList& list, te::Plugin::Ptr p, te::SelectionManager* sm)
    {
        // On a return track slot 0 holds the Aux Return, so nothing may go in front of it.
        const bool isReturn = dynamic_cast<te::AuxReturnPlugin*> (list[0]) != nullptr;
        if (dynamic_cast<pulse::PulseMidiEffect*> (p.get()) != nullptr)
        {
            int index = isReturn ? 1 : 0;
            for (int i = 0; i < list.size(); ++i)
                if (list[i]->isSynth()) { index = i; break; }   // just before the instrument
                else if (dynamic_cast<pulse::PulseMidiEffect*> (list[i]) != nullptr) index = i + 1;
            list.insertPlugin (p, index, sm);
            return;
        }
        list.insertPlugin (p, p->isSynth() ? (isReturn ? 1 : 0) : insertIndexFor (list, track), sm);
    }
}
