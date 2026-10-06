#pragma once

#include "MidiLearn.h"

/** The window of the controller mappings: the pages along the top, what is mapped on the page chosen, and the two
    buttons of the controller that step through the pages. */
namespace MidiMap
{
    class Panel : public Component,
                  private ListBoxModel,
                  private ChangeListener
    {
    public:
        explicit Panel (MidiLearn& l) : learn (l)
        {
            for (int p = 0; p <= MidiLearn::numPages; ++p)
            {
                auto* b = pages.add (new TextButton());
                b->setClickingTogglesState (false);
                b->setColour (TextButton::buttonOnColourId, Theme::accentDim);
                b->setColour (TextButton::textColourOnId, Theme::text);
                b->setConnectedEdges ((p > 0 ? Button::ConnectedOnLeft : 0) | (p < MidiLearn::numPages ? Button::ConnectedOnRight : 0));
                b->onClick = [this, p] { learn.setPage (p); };
                addAndMakeVisible (b);
            }
            name.setEditable (true);
            name.setFont (FontOptions (15.0f, Font::bold));
            name.setColour (Label::textColourId, Theme::text);
            name.setTooltip ("The page's name: click to change it");
            name.onTextChange = [this] { learn.setPageName (learn.getPage(), name.getText()); };
            addAndMakeVisible (name);
            hint.setFont (FontOptions (12.5f));
            hint.setColour (Label::textColourId, Theme::muted);
            hint.setJustificationType (Justification::centred);
            addAndMakeVisible (hint);
            list.setModel (this);
            list.setRowHeight (28);
            list.setColour (ListBox::backgroundColourId, Theme::well);
            addAndMakeVisible (list);
            for (auto* b : { &nextBtn, &prevBtn })
            {
                const bool next = b == &nextBtn;
                b->setTooltip (next ? "Choose a button of your controller that steps to the next page that has something on it (right-click: forget it)"
                                    : "Choose a button of your controller that steps to the previous page (right-click: forget it)");
                b->onClick = [this, next] { learn.startLearningPageButton (next); };
                b->addMouseListener (this, false);
                addAndMakeVisible (b);
            }
            learn.addChangeListener (this);
            refresh();
        }

        ~Panel() override { learn.removeChangeListener (this); }

        /** Looks at the mappings again now (the window is otherwise told of changes a moment later). */
        void refreshNow() { refresh(); }
        int numRowsShown() const { return (int) shown.size(); }
        String rowText (int i) const { return isPositiveAndBelow (i, (int) shown.size()) ? "CC " + String (shown[(size_t) i].cc) + " > " + shown[(size_t) i].target : String(); }
        String nameShown() const { return name.getText(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            auto top = r.removeFromTop (28);
            const int w = top.getWidth() / pages.size();
            for (auto* b : pages) b->setBounds (top.removeFromLeft (w));
            r.removeFromTop (10);
            name.setBounds (r.removeFromTop (26));
            auto bottom = r.removeFromBottom (30);
            prevBtn.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2).reduced (0, 1).withTrimmedRight (4));
            nextBtn.setBounds (bottom.reduced (0, 1).withTrimmedLeft (4));
            r.removeFromBottom (8);
            list.setBounds (r);
            hint.setBounds (r.reduced (30, 0));
        }

        void paint (Graphics& g) override { g.fillAll (Theme::panel); }

        void mouseDown (const MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu()) return;
            if (e.eventComponent == &nextBtn) learn.clearPageButton (true);
            if (e.eventComponent == &prevBtn) learn.clearPageButton (false);
        }

    private:
        int getNumRows() override { return (int) shown.size(); }

        void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
        {
            if (! isPositiveAndBelow (row, (int) shown.size())) return;
            auto& m = shown[(size_t) row];
            if (selected) { g.setColour (Theme::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
            g.setColour (Theme::accent);
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText ("CC " + String (m.cc), 12, 0, 60, h, Justification::centredLeft);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.5f));
            g.drawText ("ch " + String (m.channel), 72, 0, 44, h, Justification::centredLeft);
            g.setColour (Theme::text);
            g.setFont (FontOptions (13.5f));
            g.drawText (m.target, 122, 0, w - 134, h, Justification::centredLeft, true);
            g.setColour (Theme::ink.withAlpha (0.05f));
            g.fillRect (0, h - 1, w, 1);
        }

        void listBoxItemClicked (int row, const MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() || ! isPositiveAndBelow (row, (int) shown.size())) return;
            const int index = shown[(size_t) row].index;
            PopupMenu m, to;
            for (int p = 0; p <= MidiLearn::numPages; ++p)
                to.addItem (learn.pageName (p), p != learn.getPage(), false, [this, index, p] { learn.moveToPage (index, p); });
            m.addSubMenu ("Move to", to);
            m.addItem ("Remove this mapping", [this, index] { learn.removeAt (index); });
            m.showMenuAsync ({});
        }

        void deleteKeyPressed (int row) override
        {
            if (isPositiveAndBelow (row, (int) shown.size())) learn.removeAt (shown[(size_t) row].index);
        }

        void changeListenerCallback (ChangeBroadcaster*) override { refresh(); }

        void refresh()
        {
            const int page = learn.getPage();
            for (int p = 0; p < pages.size(); ++p)
            {
                const int n = learn.numMappingsOn (p);
                pages[p]->setButtonText ((p == 0 ? String ("Always") : String (p)) + (n > 0 ? " (" + String (n) + ")" : String()));
                pages[p]->setToggleState (p == page, dontSendNotification);
                pages[p]->setTooltip (learn.pageName (p) + (n > 0 ? ": " + String (n) + " mapped" : ": nothing mapped"));
            }
            if (! name.isBeingEdited()) name.setText (learn.pageName (page), dontSendNotification);
            name.setEditable (page > 0);
            shown = learn.rows (page);
            list.updateContent();
            list.repaint();
            hint.setVisible (shown.empty());
            hint.setText (page == 0 ? "Nothing is mapped yet. Right-click any knob or fader and choose MIDI Learn, then move a knob on your controller.\n\nWhat is mapped here works on every page."
                                    : "Nothing is mapped on this page. With this page chosen, right-click a knob and choose MIDI Learn:\nthe same knob of your controller can do something else on each page.", dontSendNotification);
            auto text = [&] (bool next) { const auto t = learn.pageButtonText (next); return String (next ? "Next page: " : "Previous page: ") + (t.isNotEmpty() ? t : String ("choose a button...")); };
            nextBtn.setButtonText (learn.isLearning() && learn.getLearningName().contains ("next") ? String ("Press the button now...") : text (true));
            prevBtn.setButtonText (learn.isLearning() && learn.getLearningName().contains ("previous") ? String ("Press the button now...") : text (false));
        }

        MidiLearn& learn;
        OwnedArray<TextButton> pages;
        Label name, hint;
        ListBox list;
        TextButton nextBtn, prevBtn;
        std::vector<MidiLearn::Row> shown;
    };

    struct Window : public DocumentWindow
    {
        explicit Window (MidiLearn& l) : DocumentWindow ("Controller mappings", Theme::panel, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            auto* p = new Panel (l);
            p->setSize (640, 420);
            setContentOwned (p, true);
            setResizable (true, false);
            setResizeLimits (520, 300, 2000, 1600);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }
    };

    inline Component::SafePointer<Window>& openWindow()
    {
        static Component::SafePointer<Window> w;
        return w;
    }

    inline void show (MidiLearn& l)
    {
        auto& w = openWindow();
        if (w == nullptr) w = new Window (l);
        w->toFront (true);
    }

    /** Closes the window (before the project, and its mappings, go away). */
    inline void close() { delete openWindow().getComponent(); }
}
