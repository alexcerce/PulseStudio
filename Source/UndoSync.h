#pragma once

/** Makes knobs and faders follow Undo and Redo.

    The engine keeps a parameter's value in the project and undoes it there, but leaves the
    parameter itself (what the knob shows and what you hear) where it was. This watches the
    project while an undo or redo is being carried out and moves the parameter of any device
    whose stored value changed: track volume and pan, sends, the built-in devices, plugins. */
class UndoSync : private ValueTree::Listener
{
public:
    explicit UndoSync (te::Edit& e) : edit (e) { edit.state.addListener (this); }
    ~UndoSync() override                       { edit.state.removeListener (this); }

private:
    void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
    {
        if (! v.hasType (te::IDs::PLUGIN) || ! edit.getUndoManager().isPerformingUndoRedo()) return;
        if (auto plugin = edit.getPluginCache().getPluginFor (v))
        {
            const auto name = id.toString();
            for (auto* p : plugin->getAutomatableParameters())
                if (p->paramID == name || "pulse_" + p->paramID == name) { p->updateFromAttachedValue(); break; }
        }
    }

    te::Edit& edit;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoSync)
};
