#pragma once

#include "Common.h"
#include "Sharing.h"

/** Start screen shown over the project: recent projects, new project (empty, demo, templates)
    and getting-started tips. File > Welcome screen opens it again. */
class WelcomeScreen : public Component
{
public:
    struct Callbacks
    {
        std::function<void (const File&)> openProject;
        std::function<void (const File& templateFile, bool demo)> newProject;
        std::function<void()> browseProject;
        std::function<void (bool)> setShowAtStartup;
    };

    WelcomeScreen (StringArray recentPaths, bool showAtStartup, Callbacks cb, Array<File> templateFiles = {}) : callbacks (std::move (cb))
    {
        for (auto& p : recentPaths)
            if (File (p).existsAsFile() && recent.size() < 9) recent.add (File (p));

        auto styleNew = [this] (TextButton& b, const String& tip)
        {
            b.setTooltip (tip);
            b.setColour (TextButton::buttonColourId, Colour (0xff232832));
            addAndMakeVisible (b);
        };
        styleNew (emptyBtn, "A new empty project with one instrument track");
        styleNew (demoBtn, "A short song showing off the instruments, the Drum Rack and the session view");
        styleNew (openBtn, "Open a project file");
        emptyBtn.onClick = [this] { close(); if (callbacks.newProject) callbacks.newProject ({}, false); };
        demoBtn.onClick  = [this] { close(); if (callbacks.newProject) callbacks.newProject ({}, true); };
        openBtn.onClick  = [this] { close(); if (callbacks.browseProject) callbacks.browseProject(); };

        if (templateFiles.isEmpty()) templateFiles = Sharing::templates();
        for (auto& t : templateFiles)
        {
            if (templateBtns.size() >= 5) break;
            auto* b = templateBtns.add (new TextButton (t.getFileNameWithoutExtension()));
            styleNew (*b, "New project from the template \"" + t.getFileNameWithoutExtension() + "\"");
            b->onClick = [this, t] { close(); if (callbacks.newProject) callbacks.newProject (t, false); };
        }

        for (int i = 0; i < recent.size(); ++i)
        {
            auto* b = recentBtns.add (new RecentRow (recent[i]));
            b->onClick = [this, f = recent[i]] { close(); if (callbacks.openProject) callbacks.openProject (f); };
            addAndMakeVisible (b);
        }

        showAtStart.setToggleState (showAtStartup, dontSendNotification);
        showAtStart.onClick = [this] { if (callbacks.setShowAtStartup) callbacks.setShowAtStartup (showAtStart.getToggleState()); };
        addAndMakeVisible (showAtStart);
        closeBtn.onClick = [this] { close(); };
        addAndMakeVisible (closeBtn);
        setWantsKeyboardFocus (true);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colours::black.withAlpha (0.55f));
        auto p = panel().toFloat();
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (p.translated (0.0f, 4.0f), 12.0f);
        g.setGradientFill (ColourGradient (Colour (0xff1e2229), p.getX(), p.getY(), Colour (0xff15181d), p.getX(), p.getBottom(), false));
        g.fillRoundedRectangle (p, 12.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (p, 12.0f, 1.0f);

        // logo + title
        auto head = p.reduced (28.0f, 22.0f).removeFromTop (44.0f);
        Path mark;
        const float mx = head.getX(), my = head.getCentreY();
        mark.startNewSubPath (mx, my);
        mark.lineTo (mx + 8, my); mark.lineTo (mx + 13, my - 12); mark.lineTo (mx + 19, my + 12); mark.lineTo (mx + 25, my - 6);
        mark.lineTo (mx + 29, my); mark.lineTo (mx + 38, my);
        g.setColour (Theme::accent);
        g.strokePath (mark, PathStrokeType (3.0f, PathStrokeType::curved, PathStrokeType::rounded));
        g.setColour (Theme::text);
        g.setFont (FontOptions (24.0f, Font::bold));
        g.drawText ("Pulse Studio", head.withTrimmedLeft (50.0f), Justification::centredLeft);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (12.0f));
        g.drawText ("Version " + String (JUCE_APPLICATION_VERSION_STRING), head, Justification::centredRight);

        auto col = [&] (Rectangle<int> r, const String& t)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText (t, r.removeFromTop (16), Justification::centredLeft);
        };
        col (recentArea, "RECENT PROJECTS");
        col (newArea, "NEW");
        col (tipsArea, "GETTING STARTED");
        if (recent.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.5f));
            g.drawText ("No recent projects yet.", recentArea.withTrimmedTop (24).removeFromTop (20), Justification::centredLeft);
        }

        // tips
        const char* tips[][2] = { { "Space", "Play / stop" }, { "Tab", "Arrangement / Session" }, { "Ctrl+P", "Command palette (find any action)" },
                                  { "B", "Browser" }, { "1 2 3", "Devices, Mixer, Piano Roll" }, { "Double-click", "Rename a track or open a clip" },
                                  { "Drag", "Files, devices, tracks and clips" }, { "Right-click", "Menus almost everywhere" } };
        auto t = tipsArea.withTrimmedTop (22);
        g.setFont (FontOptions (12.0f));
        for (auto& tip : tips)
        {
            auto row = t.removeFromTop (20);
            g.setColour (Theme::accent);
            g.drawText (tip[0], row.removeFromLeft (92), Justification::centredLeft);
            g.setColour (Theme::text.withAlpha (0.85f));
            g.drawText (tip[1], row, Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto p = panel().reduced (28, 22);
        p.removeFromTop (60);
        auto bottom = p.removeFromBottom (28);
        closeBtn.setBounds (bottom.removeFromRight (90));
        showAtStart.setBounds (bottom.removeFromLeft (220));
        p.removeFromBottom (10);

        recentArea = p.removeFromLeft (p.getWidth() * 4 / 10);
        p.removeFromLeft (24);
        newArea = p.removeFromLeft (p.getWidth() / 2);
        p.removeFromLeft (24);
        tipsArea = p;

        auto r = recentArea.withTrimmedTop (22);
        for (auto* b : recentBtns) b->setBounds (r.removeFromTop (38).withTrimmedBottom (4));

        auto n = newArea.withTrimmedTop (22);
        for (auto* b : { &emptyBtn, &demoBtn, &openBtn }) { b->setBounds (n.removeFromTop (30)); n.removeFromTop (6); }
        if (! templateBtns.isEmpty())
        {
            n.removeFromTop (8);
            templatesLabelY = n.getY();
            n.removeFromTop (18);
            for (auto* b : templateBtns) { b->setBounds (n.removeFromTop (28)); n.removeFromTop (4); }
        }
    }

    void paintOverChildren (Graphics& g) override
    {
        if (! templateBtns.isEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ("FROM A TEMPLATE", newArea.getX(), templatesLabelY, newArea.getWidth(), 16, Justification::centredLeft);
        }
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (k == KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    void mouseDown (const MouseEvent& e) override { if (! panel().contains (e.getPosition())) close(); }

    void close()
    {
        setVisible (false);
        MessageManager::callAsync ([sp = SafePointer<Component> (this)] { if (sp != nullptr) delete sp.getComponent(); });
    }

private:
    struct RecentRow : public Button
    {
        explicit RecentRow (const File& f) : Button (f.getFileNameWithoutExtension()), file (f)
        {
            setTooltip (f.getFullPathName());
        }
        void paintButton (Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (down ? Theme::accent.withAlpha (0.3f) : over ? Colour (0xff2a303a) : Colour (0xff20242c));
            g.fillRoundedRectangle (r, 6.0f);
            auto t = r.reduced (12.0f, 3.0f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (13.0f, Font::bold));
            g.drawText (file.getFileNameWithoutExtension(), t.removeFromTop (t.getHeight() * 0.55f), Justification::centredLeft, true);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f));
            g.drawText (file.getParentDirectory().getFileName() + "   " + file.getLastModificationTime().formatted ("%d %b %Y, %H:%M"), t, Justification::centredLeft, true);
        }
        File file;
    };

    Rectangle<int> panel() const { return getLocalBounds().withSizeKeepingCentre (jmin (900, getWidth() - 60), jmin (430, getHeight() - 60)); }

    Callbacks callbacks;
    Array<File> recent;
    OwnedArray<RecentRow> recentBtns;
    OwnedArray<TextButton> templateBtns;
    TextButton emptyBtn { "Empty project" }, demoBtn { "Demo song" }, openBtn { "Open project..." }, closeBtn { "Close" };
    ToggleButton showAtStart { "Show this at startup" };
    Rectangle<int> recentArea, newArea, tipsArea;
    int templatesLabelY = 0;
};
