#pragma once

#include "EditViewState.h"

/** Notes: free text kept with the project, and with each track ("retake the second verse", "borrowed mic, +6 dB
    at the preamp", lyrics, a to-do list). One window holds them all: the project's on top, then every track's. */
namespace Notes
{
    inline const Identifier noteID { "pulseNote" };

    inline String ofProject (te::Edit& e)                 { return e.state[noteID].toString(); }
    inline String of (te::Track& t)                       { return t.state[noteID].toString(); }
    inline bool has (te::Track& t)                        { return of (t).trim().isNotEmpty(); }

    inline void store (ValueTree v, const String& text, UndoManager* um)
    {
        if (v[noteID].toString() == text) return;
        if (text.isEmpty()) v.removeProperty (noteID, um); else v.setProperty (noteID, text, um);
    }
    inline void setProject (te::Edit& e, const String& text) { store (e.state, text, &e.getUndoManager()); }
    inline void set (te::Track& t, const String& text)       { store (t.state, text, &t.edit.getUndoManager()); }

    /** The start of a note, short enough for a tooltip. */
    inline String summary (const String& text, int most = 240)
    {
        const auto t = text.trim();
        return t.length() <= most ? t : t.substring (0, most).trimEnd() + "...";
    }

    /** Every track of the project, in order (the list of the window). */
    inline Array<te::Track*> tracks (te::Edit& e)
    {
        Array<te::Track*> out;
        for (auto t : te::getAllTracks (e))
            if (dynamic_cast<te::AudioTrack*> (t) != nullptr || dynamic_cast<te::FolderTrack*> (t) != nullptr) out.add (t);
        return out;
    }

    //==============================================================================
    /** The project's notes and each track's, side by side: a list to choose from, and the text. */
    class Panel : public Component,
                  private ListBoxModel,
                  private Timer
    {
    public:
        explicit Panel (te::Edit& e) : edit (e)
        {
            list.setModel (this);
            list.setRowHeight (26);
            list.setColour (ListBox::backgroundColourId, Theme::well);
            addAndMakeVisible (list);
            text.setMultiLine (true, true);
            text.setReturnKeyStartsNewLine (true);
            text.setTabKeyUsedAsCharacter (true);
            text.setScrollbarsShown (true);
            text.setFont (FontOptions (15.0f));
            text.setColour (TextEditor::backgroundColourId, Theme::panel);
            text.setColour (TextEditor::textColourId, Theme::text);
            text.setColour (TextEditor::outlineColourId, Colours::transparentBlack);
            text.setColour (TextEditor::focusedOutlineColourId, Colours::transparentBlack);
            text.setBorder ({ 10, 12, 10, 12 });
            text.setTextToShowWhenEmpty ("Nothing written yet. Anything goes here: what is left to do, how something was recorded, lyrics.", Theme::muted);
            text.onTextChange = [this] { write(); };
            addAndMakeVisible (text);
            title.setFont (FontOptions (13.0f, Font::bold));
            title.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (title);
            rows = tracks (edit);
            list.selectRow (0);
            show (0);
            startTimerHz (4);
        }

        /** Shows the notes of a track (nullptr = the project's). */
        void select (te::Track* t)
        {
            rows = tracks (edit);
            const int row = t == nullptr ? 0 : rows.indexOf (t) + 1;
            list.updateContent();
            list.selectRow (jmax (0, row));
        }

        /** The text as it stands in the window, and typing into it (for the self-test). */
        String shownText() const            { return text.getText(); }
        void type (const String& s)         { text.setText (s, false); write(); }   // (the editor tells of a change only later)
        int numRows() const                 { return rows.size() + 1; }
        int selectedRow() const             { return current; }

        void resized() override
        {
            auto r = getLocalBounds();
            list.setBounds (r.removeFromLeft (jlimit (150, 240, getWidth() / 3)));
            title.setBounds (r.removeFromTop (30).reduced (12, 0));
            text.setBounds (r);
        }

        void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    private:
        int getNumRows() override { return rows.size() + 1; }

        void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
        {
            if (row < 0 || row > rows.size()) return;
            if (selected) { g.setColour (Theme::accent.withAlpha (0.22f)); g.fillRect (0, 0, w, h); }
            te::Track* t = row == 0 ? nullptr : rows[row - 1];
            const bool noted = row == 0 ? ofProject (edit).trim().isNotEmpty() : has (*t);
            if (t != nullptr)
            {
                g.setColour (t->getColour().isTransparent() ? Theme::muted : t->getColour());
                g.fillRoundedRectangle (8.0f, (float) h * 0.5f - 5.0f, 4.0f, 10.0f, 2.0f);
            }
            g.setColour (noted ? Theme::text : Theme::muted);
            g.setFont (FontOptions (13.5f, row == 0 ? Font::bold : Font::plain));
            g.drawText (row == 0 ? String ("The project") : t->getName(), 20, 0, w - 44, h, Justification::centredLeft, true);
            if (noted)   // a mark for the ones that have something written
            {
                g.setColour (Theme::yellow);
                g.fillEllipse ((float) w - 18.0f, (float) h * 0.5f - 3.0f, 6.0f, 6.0f);
            }
        }

        void selectedRowsChanged (int row) override { if (row >= 0) show (row); }

        ValueTree target() const
        {
            if (current <= 0) return edit.state;
            return isPositiveAndBelow (current - 1, rows.size()) ? rows[current - 1]->state : ValueTree();
        }

        void show (int row)
        {
            current = row;
            auto v = target();
            shown = v.isValid() ? v[noteID].toString() : String();
            text.setText (shown, false);
            title.setText (row == 0 ? "NOTES FOR THE PROJECT" : "NOTES FOR " + rows[row - 1]->getName().toUpperCase(), dontSendNotification);
        }

        /** What was typed goes into the project: one undo step for each burst of typing. */
        void write()
        {
            auto v = target();
            if (! v.isValid()) return;
            const auto now = Time::getMillisecondCounter();
            if (now - lastWrite > 1500 || lastTarget != current) edit.getUndoManager().beginNewTransaction ("Notes");
            lastWrite = now;
            lastTarget = current;
            shown = text.getText();
            store (v, shown, &edit.getUndoManager());
            list.repaintRow (current);
        }

        /** Follows the project: tracks that come and go, their names, and undo. */
        void timerCallback() override
        {
            const auto nowRows = tracks (edit);
            if (nowRows != rows)
            {
                te::Track* was = isPositiveAndBelow (current - 1, rows.size()) ? rows[current - 1] : nullptr;
                rows = nowRows;
                list.updateContent();
                const int row = was != nullptr ? rows.indexOf (was) + 1 : 0;
                list.selectRow (jmax (0, row));
                show (jmax (0, row));
            }
            list.repaint();
            auto v = target();
            if (v.isValid() && v[noteID].toString() != shown) show (current);
        }

        te::Edit& edit;
        ListBox list;
        TextEditor text;
        Label title;
        Array<te::Track*> rows;
        int current = 0, lastTarget = -1;
        uint32 lastWrite = 0;
        String shown;
    };

    //==============================================================================
    struct Window : public DocumentWindow
    {
        explicit Window (te::Edit& e) : DocumentWindow ("Notes", Theme::panel, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            auto* p = new Panel (e);
            p->setSize (720, 460);
            setContentOwned (p, true);
            setResizable (true, false);
            setResizeLimits (420, 260, 2400, 1800);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        Panel& panel() { return *dynamic_cast<Panel*> (getContentComponent()); }
        void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }
    };

    inline Component::SafePointer<Window>& openWindow()
    {
        static Component::SafePointer<Window> w;
        return w;
    }

    /** Opens the notes window (or brings it to the front), on a track's notes or the project's. */
    inline void show (te::Edit& e, te::Track* t = nullptr)
    {
        auto& w = openWindow();
        if (w == nullptr) w = new Window (e);
        w->panel().select (t);
        w->toFront (true);
    }

    /** Closes the window (before the project goes away). */
    inline void close() { delete openWindow().getComponent(); }
}
