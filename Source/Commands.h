#pragma once

#include "Common.h"
#include "Icons.h"

/** Every app command in one place: menus, the command palette (Ctrl+P) and keyboard shortcuts
    (which the user can reassign; overrides are saved in the settings file). */
class CommandRegistry
{
public:
    struct Command
    {
        String id, name, category;
        KeyPress defaultKey;
        std::function<void()> run;
    };

    explicit CommandRegistry (PropertiesFile& s) : settings (s) {}

    void add (const String& id, const String& name, const String& category, KeyPress key, std::function<void()> fn)
    {
        commands.push_back ({ id, name, category, key, std::move (fn) });
    }

    const std::vector<Command>& all() const { return commands; }

    const Command* find (const String& id) const
    {
        for (auto& c : commands) if (c.id == id) return &c;
        return nullptr;
    }

    KeyPress keyFor (const Command& c) const
    {
        auto v = settings.getValue ("key." + c.id);
        if (v == "none") return {};
        return v.isNotEmpty() ? KeyPress::createFromDescription (v) : c.defaultKey;
    }

    void setKey (const String& id, const KeyPress& k)
    {
        settings.setValue ("key." + id, k.isValid() ? k.getTextDescription() : String ("none"));
    }

    void resetKey (const String& id) { settings.removeValue ("key." + id); }
    void resetAll()                  { for (auto& c : commands) resetKey (c.id); }

    /** The command (other than `exceptId`) already using this key, if any. */
    const Command* commandUsing (const KeyPress& k, const String& exceptId = {}) const
    {
        for (auto& c : commands)
            if (c.id != exceptId && keyFor (c).isValid() && keyFor (c) == normalise (k)) return &c;
        return nullptr;
    }

    /** Windows reports Ctrl+letter as a control character: turn it back into the letter. */
    static KeyPress normalise (const KeyPress& k)
    {
        auto code = k.getKeyCode();
        if (k.getModifiers().isCommandDown() && code >= 1 && code <= 26)
            return KeyPress ('A' + code - 1, k.getModifiers(), 0);
        if (code >= 'a' && code <= 'z')
            return KeyPress (code - 'a' + 'A', k.getModifiers(), 0);
        return KeyPress (code, k.getModifiers(), 0);
    }

    bool dispatch (const KeyPress& k) const
    {
        const auto n = normalise (k);
        for (auto& c : commands)
        {
            auto key = keyFor (c);
            if (key.isValid() && normalise (key) == n && c.run)
            {
                c.run();
                return true;
            }
        }
        return false;
    }

    static String describe (const KeyPress& k)
    {
        return k.isValid() ? k.getTextDescription().replace ("ctrl", "Ctrl").replace ("shift", "Shift").replace ("alt", "Alt")
                                                     .replace ("spacebar", "Space").replace ("cursor", "") : String();
    }

private:
    PropertiesFile& settings;
    std::vector<Command> commands;
};

//==============================================================================
/** Ctrl+P: type to find any command (or device / marker), Enter to run it. */
class CommandPalette : public Component,
                       private ListBoxModel,
                       private TextEditor::Listener
{
public:
    struct Entry { String name, detail; std::function<void()> run; };

    CommandPalette (std::vector<Entry> entriesToShow) : entries (std::move (entriesToShow))
    {
        search.setTextToShowWhenEmpty ("Type a command, device or marker...", Theme::muted);
        search.setFont (FontOptions (16.0f));
        search.addListener (this);
        search.setWantsKeyboardFocus (true);
        addAndMakeVisible (search);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        addAndMakeVisible (list);
        filter();
    }

    std::function<void()> onDismiss;

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0, 4), 10.0f);
        g.setColour (Theme::header);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Theme::line.brighter (0.25f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
        Icons::draw (g, Icons::Type::search, Rectangle<float> (14.0f, 14.0f, 20.0f, 20.0f), Theme::muted);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        search.setBounds (r.removeFromTop (28).withTrimmedLeft (30));
        r.removeFromTop (6);
        list.setBounds (r);
    }

    void focus() { search.grabKeyboardFocus(); }

    bool keyPressed (const KeyPress& k) override { return handleKey (k); }

private:
    bool handleKey (const KeyPress& k)
    {
        if (k == KeyPress::escapeKey) { dismiss(); return true; }
        if (k == KeyPress::returnKey) { runSelected(); return true; }
        if (k == KeyPress::downKey)   { list.selectRow (jmin (list.getSelectedRow() + 1, (int) shown.size() - 1)); return true; }
        if (k == KeyPress::upKey)     { list.selectRow (jmax (0, list.getSelectedRow() - 1)); return true; }
        return false;
    }

    void textEditorTextChanged (TextEditor&) override  { filter(); }
    void textEditorReturnKeyPressed (TextEditor&) override { runSelected(); }
    void textEditorEscapeKeyPressed (TextEditor&) override { dismiss(); }

    /** Every typed word must appear; earlier and word-start matches rank higher. */
    static int score (const String& text, const StringArray& words)
    {
        int s = 0;
        auto lower = text.toLowerCase();
        for (auto& w : words)
        {
            const int i = lower.indexOf (w);
            if (i < 0) return -1;
            s += 100 - jmin (90, i) + ((i == 0 || lower[i - 1] == ' ') ? 50 : 0);
        }
        return s;
    }

    void filter()
    {
        auto words = StringArray::fromTokens (search.getText().toLowerCase(), " ", {});
        words.removeEmptyStrings();
        std::vector<std::pair<int, int>> ranked;
        for (int i = 0; i < (int) entries.size(); ++i)
            if (const int sc = words.isEmpty() ? 0 : score (entries[(size_t) i].name + " " + entries[(size_t) i].detail, words); sc >= 0)
                ranked.push_back ({ -sc, i });
        std::stable_sort (ranked.begin(), ranked.end());
        shown.clear();
        for (auto& [sc, i] : ranked) shown.push_back (i);
        list.updateContent();
        list.selectRow (0);
        list.repaint();
    }

    void runSelected()
    {
        const int row = list.getSelectedRow();
        if (! isPositiveAndBelow (row, (int) shown.size())) return;
        auto fn = entries[(size_t) shown[(size_t) row]].run;
        dismiss();
        if (fn) MessageManager::callAsync (fn);
    }

    void dismiss() { if (onDismiss) MessageManager::callAsync (onDismiss); }

    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (! isPositiveAndBelow (row, (int) shown.size())) return;
        auto& e = entries[(size_t) shown[(size_t) row]];
        if (selected)
        {
            g.setColour (Theme::accent.withAlpha (0.25f));
            g.fillRoundedRectangle (Rectangle<float> ((float) w, (float) h).reduced (2.0f, 1.0f), 5.0f);
        }
        g.setColour (Theme::text);
        g.setFont (FontOptions (14.5f));
        g.drawText (e.name, 12, 0, w - 160, h, Justification::centredLeft, true);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (12.5f));
        g.drawText (e.detail, w - 170, 0, 160, h, Justification::centredRight, true);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override { list.selectRow (row); runSelected(); }

    TextEditor search;
    ListBox list;
    std::vector<Entry> entries;
    std::vector<int> shown;
};

//==============================================================================
/** Keyboard shortcut editor: click a command, press its new keys. */
class ShortcutEditor : public Component,
                       private ListBoxModel
{
public:
    explicit ShortcutEditor (CommandRegistry& r) : registry (r)
    {
        list.setModel (this);
        list.setRowHeight (26);
        addAndMakeVisible (list);
        hint.setText ("Select a command and click Assign, then press the new keys. Esc cancels, Backspace clears.", dontSendNotification);
        hint.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (hint);
        assignBtn.onClick = [this] { capturing = list.getSelectedRow() >= 0; repaint(); grabKeyboardFocus(); };
        resetBtn.onClick  = [this] { if (auto c = selected()) registry.resetKey (c->id); list.repaint(); };
        resetAllBtn.onClick = [this] { registry.resetAll(); list.repaint(); };
        for (auto* b : { &assignBtn, &resetBtn, &resetAllBtn }) addAndMakeVisible (b);
        setWantsKeyboardFocus (true);
        setSize (560, 620);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        if (capturing)
        {
            g.setColour (Theme::orange);
            g.drawRect (list.getBounds().expanded (2), 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        hint.setBounds (r.removeFromTop (22));
        auto bottom = r.removeFromBottom (30);
        assignBtn.setBounds (bottom.removeFromLeft (110));
        bottom.removeFromLeft (6);
        resetBtn.setBounds (bottom.removeFromLeft (90));
        resetAllBtn.setBounds (bottom.removeFromRight (110));
        r.removeFromBottom (8);
        list.setBounds (r);
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (! capturing) return false;
        capturing = false;
        if (auto c = selected())
        {
            if (k == KeyPress::backspaceKey) registry.setKey (c->id, {});
            else if (k != KeyPress::escapeKey)
            {
                auto n = CommandRegistry::normalise (k);
                if (auto other = registry.commandUsing (n, c->id))
                    registry.setKey (other->id, {});   // a key does one thing
                registry.setKey (c->id, n);
            }
        }
        list.repaint();
        repaint();
        return true;
    }

private:
    const CommandRegistry::Command* selected() const
    {
        auto row = list.getSelectedRow();
        return isPositiveAndBelow (row, (int) registry.all().size()) ? &registry.all()[(size_t) row] : nullptr;
    }

    int getNumRows() override { return (int) registry.all().size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool sel) override
    {
        auto& c = registry.all()[(size_t) row];
        if (sel) g.fillAll (Theme::accent.withAlpha (0.22f));
        g.setColour (Theme::muted);
        g.setFont (FontOptions (12.0f));
        g.drawText (c.category, 8, 0, 90, h, Justification::centredLeft, true);
        g.setColour (Theme::text);
        g.setFont (FontOptions (13.5f));
        g.drawText (c.name, 100, 0, w - 260, h, Justification::centredLeft, true);
        auto key = CommandRegistry::describe (registry.keyFor (c));
        g.setColour (sel && capturing ? Theme::orange : Theme::accent.brighter (0.3f));
        g.drawText (sel && capturing ? String ("Press keys...") : (key.isNotEmpty() ? key : String ("-")), w - 160, 0, 150, h, Justification::centredRight, true);
    }

    CommandRegistry& registry;
    ListBox list;
    Label hint;
    TextButton assignBtn { "Assign..." }, resetBtn { "Default" }, resetAllBtn { "Reset all" };
    bool capturing = false;
};
