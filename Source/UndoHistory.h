#pragma once

#include "Common.h"

/** Undo history: a list of everything done in the project; click a step to go back (or
    forward) to it. Also quick menus on right-clicking the undo / redo buttons. */
namespace UndoHistory
{
    /** Undoes or redoes until `steps` transactions have moved (negative = undo). */
    inline void move (te::Edit& edit, int steps)
    {
        auto& um = edit.getUndoManager();
        for (int i = 0; i < -steps && um.canUndo(); ++i) edit.undo();
        for (int i = 0; i < steps && um.canRedo(); ++i) edit.redo();
    }

    /** Right-click menu for the undo (or redo) button: the last 25 steps. */
    inline void showMenu (te::Edit& edit, Component& target, bool redo)
    {
        auto& um = edit.getUndoManager();
        const auto items = redo ? um.getRedoDescriptions() : um.getUndoDescriptions();
        PopupMenu m;
        m.addSectionHeader (redo ? "Redo up to..." : "Undo back to before...");
        juce::WeakReference<te::Selectable> ref (&edit);
        for (int i = 0; i < jmin (25, items.size()); ++i)
            m.addItem (String (i + 1) + ".  " + (items[i].isNotEmpty() ? items[i] : String ("(change)")), [ref, i, redo]
            {
                if (auto* e = dynamic_cast<te::Edit*> (ref.get())) move (*e, redo ? i + 1 : -(i + 1));
            });
        if (items.isEmpty()) m.addItem ("Nothing to " + String (redo ? "redo" : "undo"), false, false, [] {});
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&target));
    }

    //==============================================================================
    class View : public Component,
                 private ListBoxModel,
                 private ChangeListener
    {
    public:
        explicit View (te::Edit& e) : edit (e)
        {
            title.setText ("Click a step to go back (or forward) to it", dontSendNotification);
            title.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (title);
            list.setModel (this);
            list.setRowHeight (24);
            list.setColour (ListBox::backgroundColourId, Theme::well);
            addAndMakeVisible (list);
            edit.getUndoManager().addChangeListener (this);
            refresh();
            setSize (360, 520);
        }

        ~View() override { edit.getUndoManager().removeChangeListener (this); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8);
            title.setBounds (r.removeFromTop (22));
            list.setBounds (r);
        }

        void paint (Graphics& g) override { g.fillAll (Theme::panel); }

        /** Rows: "(start)", the undoable steps oldest first, then the redoable ones. */
        void refresh()
        {
            auto& um = edit.getUndoManager();
            undo = um.getUndoDescriptions();
            redo = um.getRedoDescriptions();
            list.updateContent();
            list.repaint();
            list.scrollToEnsureRowIsOnscreen (current());
        }

        int current() const       { return undo.size(); }
        int getNumRows() override { return 1 + undo.size() + redo.size(); }

        /** Jumps to a row (as if clicked). */
        void goTo (int row)
        {
            move (edit, row - current());
            refresh();
        }

    private:
        String nameOf (int row) const
        {
            if (row == 0) return "(project opened / start)";
            if (row <= undo.size()) return undo[undo.size() - row];
            return redo[row - undo.size() - 1];
        }

        void paintListBoxItem (int row, Graphics& g, int w, int h, bool) override
        {
            const bool isCurrent = row == current(), undone = row > current();
            if (isCurrent) { g.setColour (Theme::accent.withAlpha (0.25f)); g.fillRect (0, 0, w, h); }
            g.setColour (undone ? Theme::muted.withAlpha (0.6f) : Theme::text);
            g.setFont (FontOptions (12.5f, isCurrent ? Font::bold : Font::plain));
            auto n = nameOf (row);
            g.drawText ((row > 0 ? String (row) + ".  " : String()) + (n.isNotEmpty() ? n : String ("(change)")) + (undone ? "   (undone)" : String()),
                        10, 0, w - 16, h, Justification::centredLeft, true);
            g.setColour (Theme::line);
            g.drawHorizontalLine (h - 1, 0.0f, (float) w);
        }

        void listBoxItemClicked (int row, const MouseEvent&) override { goTo (row); }
        void changeListenerCallback (ChangeBroadcaster*) override       { refresh(); }

        te::Edit& edit;
        StringArray undo, redo;
        Label title;
        ListBox list;
    };

    inline Array<Component::SafePointer<DialogWindow>>& windows() { static Array<Component::SafePointer<DialogWindow>> w; return w; }

    inline void showWindow (te::Edit& edit)
    {
        DialogWindow::LaunchOptions o;
        o.dialogTitle = "Undo history";
        o.dialogBackgroundColour = Theme::panel;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.content.setOwned (new View (edit));
        windows().add (o.launchAsync());
    }

    /** Closes the history windows (before the project goes away). */
    inline void closeAll()
    {
        for (auto& w : windows()) delete w.getComponent();
        windows().clear();
    }
}
