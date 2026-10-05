#pragma once

/** Makes knobs and faders follow Undo and Redo.

    The engine keeps a parameter's value in the project and undoes it there, but leaves the
    parameter itself (what the knob shows and what you hear) where it was. This watches the
    project while an undo or redo is being carried out and moves the parameter of any device
    whose stored value changed: track volume and pan, sends, the built-in devices, plugins. */
class UndoSync : private ValueTree::Listener,
                 private ChangeListener
{
public:
    explicit UndoSync (te::Edit& e) : edit (e)
    {
        edit.state.addListener (this);
        edit.getUndoManager().addChangeListener (this);
        rememberLoop();
    }
    ~UndoSync() override
    {
        edit.getUndoManager().removeChangeListener (this);
        edit.state.removeListener (this);
    }

private:
    void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
    {
        if (! edit.getUndoManager().isPerformingUndoRedo()) return;
        if (v.hasType (te::IDs::TEMPO) || v.hasType (te::IDs::TIMESIG)) { tempoMoved = true; return; }
        if (! v.hasType (te::IDs::PLUGIN)) return;
        if (auto plugin = edit.getPluginCache().getPluginFor (v))
        {
            const auto name = id.toString();
            for (auto* p : plugin->getAutomatableParameters())
                if (p->paramID == name || "pulse_" + p->paramID == name) { p->updateFromAttachedValue(); break; }
        }
    }

    // The loop is a range of beats to the musician. The engine keeps it in seconds and re-times it when the
    // tempo changes, but not when a tempo change is undone or redone; so it is put back on its beats here.
    void rememberLoop()
    {
        const auto r = edit.getTransport().getLoopRange();
        loopBeats = { edit.tempoSequence.toBeats (r.getStart()).inBeats(), edit.tempoSequence.toBeats (r.getEnd()).inBeats() };
    }

    void changeListenerCallback (ChangeBroadcaster*) override
    {
        if (tempoMoved)
        {
            tempoMoved = false;
            edit.getTransport().setLoopRange ({ edit.tempoSequence.toTime (te::BeatPosition::fromBeats (loopBeats.first)),
                                                edit.tempoSequence.toTime (te::BeatPosition::fromBeats (loopBeats.second)) });
        }
        rememberLoop();
    }

    te::Edit& edit;
    std::pair<double, double> loopBeats { 0.0, 4.0 };
    bool tempoMoved = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndoSync)
};
