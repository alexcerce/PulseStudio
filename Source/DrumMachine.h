#pragma once

#include "StepSequencer.h"

/** A MIDI clip as a drum machine shows it: sixteen steps for each pad, four patterns (A to D) and a chain that
    says in which order the patterns play, one bar each. A step is off, a hit, or an accented hit; swing pushes
    every second sixteenth later.

    The patterns, the chain, the swing and the level of a plain hit are kept with the clip (so they are saved,
    copied with the clip and undone like everything else), and the clip's notes are written out from them over
    its whole length, the chain going round. The notes are ordinary notes: they play, export and show in the
    piano roll like any other. A clip that has never been edited here is shown as its notes read (the first four
    bars as A to D) and nothing is written until a step, the chain or a setting is changed; from then on the
    machine writes all of the clip's notes. */
class DrumMachine : public Component,
                    public TooltipClient,
                    private Timer
{
public:
    static constexpr int numPatterns = 4, numSteps = 16, maxChain = 16;
    static constexpr double stepBeats = 0.25, barBeats = 4.0;
    enum { off = 0, hit = 1, accent = 2 };

    explicit DrumMachine (EditViewState& e) : evs (e) { startTimerHz (20); }

    void setClip (te::MidiClip* c) { clip = c; scrollRows = 0; lastBars = numBars(); repaint(); }
    te::MidiClip* getClip() const { return clip.get(); }

    //==============================================================================
    using RowSteps = std::array<uint8, numSteps>;
    struct Model
    {
        std::array<std::map<int, RowSteps>, numPatterns> pat;
        String chain { "A" };
        int swing = 0;      // 0..100: how far every second sixteenth is pushed towards the next one (100 = half a step)
        int level = 96;     // how hard a hit without an accent plays (an accent is always 127)
        bool stored = false;

        int at (int p, int note, int s) const
        {
            if (p < 0 || p >= numPatterns || s < 0 || s >= numSteps) return off;
            const auto it = pat[(size_t) p].find (note);
            return it == pat[(size_t) p].end() ? off : (int) it->second[(size_t) s];
        }
        int hits (int p) const
        {
            int n = 0;
            if (p >= 0 && p < numPatterns) for (auto& [note, row] : pat[(size_t) p]) for (auto v : row) n += v != off ? 1 : 0;
            return n;
        }
        /** Which pattern a bar of the clip plays (the chain goes round). */
        int patternOfBar (int bar) const { return chain.isEmpty() ? 0 : jlimit (0, numPatterns - 1, (int) chain[bar % chain.length()] - 'A'); }
    };

    /** Whether the machine has written this clip (otherwise it only shows the clip's notes). */
    bool isMachine() const { return clip != nullptr && clip->state.getChildWithName (machineID).isValid(); }

    /** What the clip holds: its stored patterns, or, for a clip never edited here, its notes read as patterns. */
    Model model() const
    {
        Model m;
        if (clip == nullptr) return m;
        auto t = clip->state.getChildWithName (machineID);
        if (! t.isValid()) return capture();
        m.stored = true;
        m.chain = cleanChain (t[chainID].toString());
        m.swing = jlimit (0, 100, (int) t.getProperty (swingID, 0));
        m.level = jlimit (1, 126, (int) t.getProperty (levelID, 96));
        for (auto p : t)
        {
            const int id = p[idID];
            if (! p.hasType (patID) || id < 0 || id >= numPatterns) continue;
            for (auto r : p)
            {
                RowSteps row {};
                const auto text = r[stepsID].toString();
                for (int s = 0; s < numSteps && s < text.length(); ++s) row[(size_t) s] = (uint8) (text[s] == 'X' ? accent : text[s] == 'x' ? hit : off);
                m.pat[(size_t) id][(int) r[noteID]] = row;
            }
        }
        return m;
    }

    int selected() const { return sel; }
    void selectPattern (int p) { sel = jlimit (0, numPatterns - 1, p); repaint(); }
    int stepAt (int note, int s) const { return model().at (sel, note, s); }
    String chain() const { return model().chain; }
    int swing() const { return model().swing; }
    int level() const { return model().level; }
    int numBars() const { return clip == nullptr ? 0 : jmax (1, (int) std::ceil (clipLengthBeats() / barBeats - 1.0e-6)); }
    std::vector<StepGrid::Row> rows() const
    {
        auto out = StepGrid::rowsFor (clip.get());
        // a row that only the patterns know (no pad, and its notes are in a pattern the chain does not play) is still shown
        std::set<int> have;
        for (auto& r : out) have.insert (r.note);
        const auto m = model();
        bool added = false;
        for (auto& p : m.pat)
            for (auto& [note, row] : p)
                if (have.insert (note).second) { out.push_back ({ note, MidiMessage::getMidiNoteName (note, true, true, 3) }); added = true; }
        if (added) std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.note > b.note; });
        return out;
    }

    //==============================================================================
    // Editing (each is one step of undo)
    void setStep (int note, int s, int value)
    {
        if (clip == nullptr || s < 0 || s >= numSteps) return;
        auto m = model();
        auto& row = m.pat[(size_t) sel][note];
        row[(size_t) s] = (uint8) jlimit (0, 2, value);
        save (m);
    }
    /** Click: a hit goes in where there is none, and comes out where there is one. */
    void toggle (int note, int s)
    {
        um().beginNewTransaction ("Step");
        setStep (note, s, stepAt (note, s) == off ? hit : off);
    }
    /** A hit becomes an accent and an accent a hit; an empty step gets an accented hit. */
    void toggleAccent (int note, int s)
    {
        um().beginNewTransaction ("Accent");
        setStep (note, s, stepAt (note, s) == accent ? hit : accent);
    }
    /** The order the patterns play in, one bar each, as letters ("AABA"). It goes round for as long as the clip is. */
    void setChain (const String& letters)
    {
        if (clip == nullptr) return;
        um().beginNewTransaction ("Pattern chain");
        auto m = model();
        m.chain = cleanChain (letters);
        save (m);
    }
    void setSwing (int percent, bool newStep = true)
    {
        if (clip == nullptr) return;
        if (newStep) um().beginNewTransaction ("Swing");
        auto m = model();
        m.swing = jlimit (0, 100, percent);
        save (m);
    }
    void setLevel (int velocity, bool newStep = true)
    {
        if (clip == nullptr) return;
        if (newStep) um().beginNewTransaction ("Hit level");
        auto m = model();
        m.level = jlimit (1, 126, velocity);
        save (m);
    }
    void copyPattern (int from, int to)
    {
        if (clip == nullptr || from == to || from < 0 || to < 0 || from >= numPatterns || to >= numPatterns) return;
        um().beginNewTransaction ("Copy pattern");
        auto m = model();
        m.pat[(size_t) to] = m.pat[(size_t) from];
        save (m);
    }
    void clearPattern (int p)
    {
        if (clip == nullptr || p < 0 || p >= numPatterns) return;
        um().beginNewTransaction ("Clear pattern");
        auto m = model();
        m.pat[(size_t) p].clear();
        save (m);
    }
    void setRow (int note, const RowSteps& steps, const String& what)
    {
        if (clip == nullptr) return;
        um().beginNewTransaction (what);
        auto m = model();
        m.pat[(size_t) sel][note] = steps;
        save (m);
    }
    void fillRow (int note, int every, int offset = 0)
    {
        RowSteps r {};
        for (int s = offset; s < numSteps && every > 0; s += every) r[(size_t) s] = hit;
        setRow (note, r, "Fill row");
    }
    void shiftRow (int note, int by)
    {
        RowSteps r {};
        for (int s = 0; s < numSteps; ++s) r[(size_t) (((s + by) % numSteps + numSteps) % numSteps)] = (uint8) stepAt (note, s);
        setRow (note, r, "Shift row");
    }
    void randomiseRow (int note, float density)
    {
        Random rng;
        RowSteps r {};
        for (auto& v : r) if (rng.nextFloat() < density) v = (uint8) (rng.nextFloat() < 0.25f ? accent : hit);
        setRow (note, r, "Random row");
    }

    //==============================================================================
    // Ready-made patterns
    struct Factory { const char* name; std::vector<std::pair<int, const char*>> rows; };
    static const std::vector<Factory>& factory()
    {
        // by General MIDI drum note, which is where the Drum Rack's kits put their sounds:
        // 36 kick, 37 rim, 38 snare, 39 clap, 42 closed hat, 45 tom, 46 open hat, 49 cymbal, 56 cowbell
        static const std::vector<Factory> f {
            { "Four on the floor",   { { 36, "X...X...X...X..." }, { 42, "..x...x...x...x." }, { 39, "....X.......X..." } } },
            { "Driving",             { { 36, "X...X...X...X..." }, { 42, "xx.xxx.xxx.xxx.x" }, { 46, "..X...X...X...X." }, { 39, "....X.......X..." } } },
            { "Rolling hats",        { { 36, "X...X...X...X..." }, { 42, "xxXxxxXxxxXxxxXx" }, { 37, "...x..x....x..x." } } },
            { "Hypnotic toms",       { { 36, "X...X...X...X..." }, { 45, "..x..x..x..x..x." }, { 37, "......x.......x." }, { 42, "..x...x...x...x." } } },
            { "Minimal",             { { 36, "X...X...X...X..." }, { 37, "..x....x..x....." }, { 42, "....x.......x..." } } },
            { "Broken",              { { 36, "X.....x...X..x.." }, { 38, "....X.......X..." }, { 42, "x.x.x.x.x.x.x.x." }, { 46, "..........x....." } } },
            { "Industrial",          { { 36, "X..x..X...x.X..." }, { 39, "....X.......X..x" }, { 42, "..x...x...x...x." }, { 45, ".......x.....x.." }, { 56, "...x......x....." } } },
            { "Electro",             { { 36, "X.....X...x....." }, { 38, "....X.......X..." }, { 42, "x.x.x.x.x.x.x.x." }, { 56, "..x......x......" } } },
            { "Fill: snare roll",    { { 36, "X...X...X...X..." }, { 38, "........x.x.xxXX" }, { 42, "..x...x........." } } },
            { "Fill: toms down",     { { 36, "X...X...X......." }, { 45, "........x.xx.xXX" }, { 49, "X..............." } } },
            { "Breakdown (no kick)", { { 42, "..x...x...x...x." }, { 39, "....X.......X..." }, { 46, "......x.......x." } } },
            { "Kick only",           { { 36, "X...X...X...X..." } } },
        };
        return f;
    }
    /** Puts a ready-made pattern into the pattern being edited (in place of what it holds). On a track with a
        Drum Rack only the drums that have a pad are written. */
    void applyFactory (int index)
    {
        if (clip == nullptr || index < 0 || index >= (int) factory().size()) return;
        um().beginNewTransaction ("Pattern");
        auto m = model();
        std::set<int> pads;
        if (auto* t = dynamic_cast<te::AudioTrack*> (clip->getTrack()))
            if (auto* rack = DrumRack::rackOf (DrumRack::findOnTrack (*t)))
                for (int slot : DrumRack::usedPads (*rack)) pads.insert (DrumRack::noteOfPad (*rack, slot));
        m.pat[(size_t) sel].clear();
        for (auto& [note, text] : factory()[(size_t) index].rows)
        {
            if (! pads.empty() && pads.count (note) == 0) continue;
            RowSteps r {};
            for (int s = 0; s < numSteps; ++s) r[(size_t) s] = (uint8) (text[s] == 'X' ? accent : text[s] == 'x' ? hit : off);
            m.pat[(size_t) sel][note] = r;
        }
        save (m);
    }

    /** Whether the clip's length can be set here (a looping clip's is set by its loop, in the clip's own settings). */
    bool canFitClip() const { return clip != nullptr && ! clip->isLooping(); }
    /** Makes the clip as long as the chain: one bar for each of its letters. */
    void fitClipToChain()
    {
        if (! canFitClip()) return;
        um().beginNewTransaction ("Fit clip to chain");
        auto m = model();
        const double startBeat = EngineHelpers::timeToBeat (clip->edit, clip->getPosition().getStart());
        clip->setEnd (EngineHelpers::beatToTime (clip->edit, startBeat + m.chain.length() * barBeats), true);
        lastBars = numBars();
        save (m);
    }

    /** Where the song is in the clip right now: the bar of the clip and the step in it (-1, -1 when it is not playing inside the clip). */
    std::pair<int, int> playing() const
    {
        auto& tr = evs.edit.getTransport();
        if (clip == nullptr || ! tr.isPlaying()) return { -1, -1 };
        const auto now = tr.getPosition();
        if (now < clip->getPosition().getStart() || now >= clip->getPosition().getEnd()) return { -1, -1 };
        const double rel = clip->getContentBeatAtTime (now).inBeats() - contentStart();
        if (rel < 0.0) return { -1, -1 };
        return { (int) (rel / barBeats), jlimit (0, numSteps - 1, (int) (std::fmod (rel, barBeats) / stepBeats)) };
    }

    bool follow = false;   // while the song plays, show the pattern that is playing

    /** The clip a track's drum machine should open: the MIDI clip under the playhead, else the track's first one,
        else a new clip of four bars at the bar the playhead is in. */
    static te::MidiClip* clipFor (te::AudioTrack& t, juce::UndoManager& um)
    {
        auto& edit = t.edit;
        const auto now = edit.getTransport().getPosition();
        te::MidiClip* first = nullptr;
        for (auto* c : t.getClips())
            if (auto* mc = dynamic_cast<te::MidiClip*> (c))
            {
                if (mc->getPosition().time.contains (now)) return mc;
                if (first == nullptr) first = mc;
            }
        if (first != nullptr) return first;
        um.beginNewTransaction ("Drum machine clip");
        const int perBar = EngineHelpers::beatsPerBar (edit);
        const double bar = std::floor (EngineHelpers::timeToBeat (edit, now) / perBar) * perBar;
        return te::insertMIDIClip (t, "Beat", { EngineHelpers::beatToTime (edit, bar), EngineHelpers::beatToTime (edit, bar + 4.0 * barBeats) }).get();
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (Pro::bg());
        if (clip == nullptr)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (13.0f));
            g.drawText ("No clip - double-click a MIDI clip in the arrangement", getLocalBounds(), Justification::centred);
            return;
        }
        const auto m = model();
        const auto L = layout (m);
        const auto [playBar, playStep] = playing();
        const int playPattern = playBar >= 0 ? m.patternOfBar (playBar) : -1;

        // the bar along the top
        g.setGradientFill (ColourGradient (Pro::barTop(), 0.0f, 0.0f, Pro::barBot(), 0.0f, (float) barH, false));
        g.fillRect (0, 0, getWidth(), barH);
        g.setColour (Pro::shade (0.5f));
        g.fillRect (0, barH - 1, getWidth(), 1);

        auto caption = [&g] (const String& text, Rectangle<int> r)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (9.5f, Font::bold));
            g.drawText (text, r, Justification::centredLeft);
        };
        auto box = [&g] (Rectangle<int> r, Colour fill, Colour outline)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (outline);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);
        };

        caption ("PATTERN", L.patternCaption);
        for (int p = 0; p < numPatterns; ++p)
        {
            const auto r = L.pattern[(size_t) p];
            const bool on = p == sel, plays = p == playPattern;
            box (r, on ? Theme::accent : Pro::deep(), plays ? Colours::white.withAlpha (0.9f) : on ? Theme::accent : Theme::ink.withAlpha (0.18f));
            g.setColour (on ? Colours::black.withAlpha (0.85f) : m.hits (p) > 0 ? Theme::text : Theme::muted);
            g.setFont (FontOptions (14.0f, Font::bold));
            g.drawText (String::charToString ((juce_wchar) ('A' + p)), r, Justification::centred);
            if (m.hits (p) > 0 && ! on)   // a dot: this pattern has something in it
            {
                g.setColour (Theme::accent);
                g.fillEllipse ((float) r.getRight() - 8.0f, (float) r.getY() + 4.0f, 4.0f, 4.0f);
            }
        }

        caption ("CHAIN", L.chainCaption);
        for (int i = 0; i < (int) L.chain.size(); ++i)
        {
            const auto r = L.chain[(size_t) i];
            const bool isAdd = i >= m.chain.length();
            const bool plays = ! isAdd && playBar >= 0 && playBar % m.chain.length() == i;
            box (r, plays ? Theme::accent.withAlpha (0.55f) : Pro::deep(), Theme::ink.withAlpha (isAdd ? 0.10f : 0.22f));
            g.setColour (isAdd ? Theme::muted : Theme::text);
            g.setFont (FontOptions (12.5f, isAdd ? Font::plain : Font::bold));
            g.drawText (isAdd ? String ("+") : m.chain.substring (i, i + 1), r, Justification::centred);
        }

        auto valueBox = [&] (Rectangle<int> r, Rectangle<int> cap, const String& name, const String& value, float amount)
        {
            caption (name, cap);
            box (r, Pro::deep(), Theme::ink.withAlpha (0.18f));
            g.setColour (Theme::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (r.toFloat().reduced (2.0f).withWidth (jmax (0.0f, ((float) r.getWidth() - 4.0f) * jlimit (0.0f, 1.0f, amount))), 3.0f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (12.0f));
            g.drawText (value, r, Justification::centred);
        };
        valueBox (L.swing, L.swingCaption, "SWING", swingText (m.swing), (float) m.swing / 100.0f);
        valueBox (L.level, L.levelCaption, "HIT LEVEL", String (m.level), (float) m.level / 127.0f);

        box (L.follow, follow ? Theme::accentDim : Pro::deep(), Theme::ink.withAlpha (0.18f));
        g.setColour (follow ? Theme::text : Theme::muted);
        g.setFont (FontOptions (12.0f));
        g.drawText ("Follow", L.follow, Justification::centred);
        box (L.menu, Pro::deep(), Theme::ink.withAlpha (0.18f));
        g.setColour (Theme::text);
        g.drawText ("Patterns", L.menu, Justification::centred);

        // step numbers
        g.setFont (FontOptions (10.0f, Font::bold));
        for (int s = 0; s < numSteps; ++s)
        {
            const auto c = cell (L, 0, s);
            g.setColour (s == playStep && playPattern == sel ? Theme::text : s % 4 == 0 ? Theme::muted : Theme::muted.withAlpha (0.45f));
            g.drawText (String (s + 1), Rectangle<float> (c.getX(), (float) barH, c.getWidth(), (float) numbersH), Justification::centred);
        }

        // the rows
        const auto rs = rows();
        g.saveState();
        g.reduceClipRegion (0, gridTop(), getWidth(), getHeight() - gridTop());
        for (int ri = scrollRows; ri < (int) rs.size(); ++ri)
        {
            const float y = (float) gridTop() + (float) (ri - scrollRows) * L.rowH;
            if (y > (float) getHeight()) break;
            g.setColour (ri % 2 == 0 ? Pro::cardTop() : Pro::cardBot());
            g.fillRect (0.0f, y, (float) getWidth(), L.rowH);
            g.setColour (ri == hoverRow ? Theme::text : Theme::muted);
            g.setFont (FontOptions (jmin (12.5f, L.rowH * 0.55f)));
            g.drawText (rs[(size_t) ri].name, 10, (int) y, labelW - 14, (int) L.rowH, Justification::centredLeft, true);
            const auto colour = Theme::trackColour (ri);
            for (int s = 0; s < numSteps; ++s)
            {
                const auto c = cell (L, ri - scrollRows, s);
                const int v = m.at (sel, rs[(size_t) ri].note, s);
                const bool now = s == playStep && playPattern == sel;
                if (v == off)
                {
                    g.setColour (Theme::ink.withAlpha (now ? 0.20f : (s / 4) % 2 == 0 ? 0.09f : 0.05f));
                    g.fillRoundedRectangle (c, 3.5f);
                }
                else
                {
                    g.setColour ((now ? Colours::white : colour).withAlpha (v == accent ? 1.0f : 0.55f));
                    g.fillRoundedRectangle (c, 3.5f);
                    if (v == accent)   // an accent has a bright bar along its top
                    {
                        g.setColour (Colours::white.withAlpha (0.85f));
                        g.fillRoundedRectangle (c.reduced (3.0f, 2.5f).withHeight (3.0f), 1.5f);
                    }
                }
            }
        }
        g.restoreState();

        if (! m.stored)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (11.0f));
            g.drawText ("Showing the clip's notes. The first change here makes the machine write the clip.", L.note, Justification::centredRight, true);
        }
    }

    String getTooltip() override
    {
        if (clip == nullptr) return {};
        const auto pos = getMouseXYRelative();
        const auto m = model();
        const auto L = layout (m);
        for (int p = 0; p < numPatterns; ++p)
            if (L.pattern[(size_t) p].contains (pos)) return "Pattern " + String::charToString ((juce_wchar) ('A' + p)) + ": click to edit it. Right-click to copy it to another pattern or clear it";
        for (int i = 0; i < (int) L.chain.size(); ++i)
            if (L.chain[(size_t) i].contains (pos))
                return i >= m.chain.length() ? String ("Adds a bar to the chain, playing the pattern being edited")
                                             : String ("Chain: the patterns in the order they play, one bar each, going round for as long as the clip is. Click a bar for its next pattern, right-click to take it out");
        if (L.swing.contains (pos)) return "Swing: pushes every second sixteenth later. Drag up or down, or turn the wheel; double-click for none; right-click for the usual amounts (66% is a triplet feel)";
        if (L.level.contains (pos)) return "Hit level: how hard a hit without an accent plays (an accent always plays at 127). Drag up or down, or turn the wheel; double-click for 96";
        if (L.follow.contains (pos)) return "Follow: while the song plays, show the pattern that is playing";
        if (L.menu.contains (pos)) return "Ready-made patterns for the pattern being edited, copying and clearing patterns, and making the clip as long as the chain";
        if (pos.y >= gridTop() && pos.x < labelW) return "Click to hear the drum. Right-click to fill, move or clear its row";
        return helpText();
    }

    static String helpText()
    {
        return "Drum machine: click puts a hit in or takes it out, drag paints. Shift-click or right-click a step for an accent; the wheel on a hit does the same. "
               "A to D are four patterns; the chain plays them in order, one bar each";
    }

    void mouseMove (const MouseEvent& e) override { const int r = rowAt (e.position.y); if (r != hoverRow) { hoverRow = r; repaint(); } }
    void mouseExit (const MouseEvent&) override { hoverRow = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        if (clip == nullptr) return;
        const auto m = model();
        const auto L = layout (m);
        const auto pos = e.getPosition();
        drag = Drag::none;

        for (int p = 0; p < numPatterns; ++p)
            if (L.pattern[(size_t) p].contains (pos))
            {
                if (e.mods.isPopupMenu()) showPatternMenu (p); else selectPattern (p);
                return;
            }
        for (int i = 0; i < (int) L.chain.size(); ++i)
            if (L.chain[(size_t) i].contains (pos))
            {
                auto c = m.chain;
                if (i >= c.length()) c << String::charToString ((juce_wchar) ('A' + sel));
                else if (e.mods.isPopupMenu()) c = c.substring (0, i) + c.substring (i + 1);
                else c = c.substring (0, i) + String::charToString ((juce_wchar) ('A' + (c[i] - 'A' + 1) % numPatterns)) + c.substring (i + 1);
                setChain (c);
                repaint();
                return;
            }
        if (L.swing.contains (pos) || L.level.contains (pos))
        {
            const bool isSwing = L.swing.contains (pos);
            if (e.mods.isPopupMenu()) { showValueMenu (isSwing); return; }
            if (e.getNumberOfClicks() > 1) { if (isSwing) setSwing (0); else setLevel (96); repaint(); return; }
            drag = isSwing ? Drag::swing : Drag::level;
            dragStart = isSwing ? m.swing : m.level;
            um().beginNewTransaction (isSwing ? "Swing" : "Hit level");
            return;
        }
        if (L.follow.contains (pos)) { follow = ! follow; repaint(); return; }
        if (L.menu.contains (pos)) { showMainMenu(); return; }

        const auto rs = rows();
        const int ri = rowAt (e.position.y);
        if (ri < 0 || ri >= (int) rs.size()) return;
        const int note = rs[(size_t) ri].note;
        if (pos.x < labelW)
        {
            if (e.mods.isPopupMenu()) showRowMenu (note, rs[(size_t) ri].name);
            else if (auto* t = dynamic_cast<te::AudioTrack*> (clip->getTrack())) t->playGuideNote (note, te::MidiChannel (1), 110, true, true, true);
            return;
        }
        const int s = stepAtX (L, e.position.x);
        if (s < 0) return;
        if (e.mods.isPopupMenu() || e.mods.isShiftDown()) { toggleAccent (note, s); repaint(); return; }
        um().beginNewTransaction ("Steps");
        painting = m.at (sel, note, s) != off ? -1 : 1;   // the first step decides whether the drag puts hits in or takes them out
        setStep (note, s, painting > 0 ? hit : off);
        drag = Drag::paint;
        dragNote = note;
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (clip == nullptr) return;
        if (drag == Drag::swing) { setSwing (dragStart - e.getDistanceFromDragStartY() / 2, false); repaint(); }
        else if (drag == Drag::level) { setLevel (dragStart - e.getDistanceFromDragStartY() / 2, false); repaint(); }
        else if (drag == Drag::paint)
        {
            const auto m = model();
            const int s = stepAtX (layout (m), e.position.x);
            if (s < 0) return;
            const int have = m.at (sel, dragNote, s);
            if (painting > 0 && have == off) setStep (dragNote, s, hit);
            else if (painting < 0 && have != off) setStep (dragNote, s, off);
            else return;
            repaint();
        }
    }

    void mouseUp (const MouseEvent&) override { drag = Drag::none; painting = 0; }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (clip == nullptr || w.deltaY == 0.0f) return;
        const auto m = model();
        const auto L = layout (m);
        const int dir = w.deltaY > 0 ? 1 : -1;
        if (L.swing.contains (e.getPosition())) { setSwing (m.swing + dir * 2); repaint(); return; }
        if (L.level.contains (e.getPosition())) { setLevel (m.level + dir * 2); repaint(); return; }
        const auto rs = rows();
        const int ri = rowAt (e.position.y), s = stepAtX (L, e.position.x);
        if (ri >= 0 && ri < (int) rs.size() && s >= 0 && m.at (sel, rs[(size_t) ri].note, s) != off)
        {
            // over a hit: up makes it an accent, down a plain hit
            um().beginNewTransaction ("Accent");
            setStep (rs[(size_t) ri].note, s, dir > 0 ? accent : hit);
            repaint();
            return;
        }
        const int visible = jmax (1, (int) ((float) (getHeight() - gridTop()) / L.rowH));
        scrollRows = jlimit (0, jmax (0, (int) rs.size() - visible), scrollRows - dir);
        repaint();
    }

private:
    static constexpr int barH = 52, numbersH = 16, labelW = 150;
    static inline const Identifier machineID { "DRUMMACHINE" }, patID { "PAT" }, rowID { "ROW" }, idID { "id" }, noteID { "note" }, stepsID { "steps" },
                                   chainID { "chain" }, swingID { "swing" }, levelID { "level" };
    enum class Drag { none, paint, swing, level };

    juce::UndoManager& um() const { return evs.edit.getUndoManager(); }
    double contentStart() const    { return clip->getContentBeatAtTime (clip->getPosition().getStart()).inBeats(); }
    double clipLengthBeats() const { return clip->getContentBeatAtTime (clip->getPosition().getEnd()).inBeats() - contentStart(); }
    static int gridTop() { return barH + numbersH; }
    static String swingText (int s) { return s <= 0 ? String ("Off") : String (s) + "%"; }

    static String cleanChain (const String& letters)
    {
        String out;
        for (auto c : letters.toUpperCase())
            if (c >= 'A' && c < 'A' + numPatterns && out.length() < maxChain) out << String::charToString (c);
        return out.isEmpty() ? String ("A") : out;
    }

    /** A clip that the machine has not written yet, read as patterns: its first four bars as A to D. */
    Model capture() const
    {
        Model m;
        const double from = contentStart();
        const int bars = jmin (numPatterns, numBars());
        int sum = 0, count = 0;
        for (auto* n : clip->getSequence().getNotes())
        {
            const double rel = n->getStartBeat().inBeats() - from;
            const int index = roundToInt (rel / stepBeats);
            if (rel < -1.0e-4 || index < 0 || index >= bars * numSteps) continue;
            const bool hard = n->getVelocity() >= 115;
            auto& v = m.pat[(size_t) (index / numSteps)][n->getNoteNumber()][(size_t) (index % numSteps)];
            v = (uint8) jmax ((int) v, hard ? (int) accent : (int) hit);
            if (! hard) { sum += n->getVelocity(); ++count; }
        }
        if (count > 0) m.level = jlimit (1, 114, roundToInt ((double) sum / count));
        // the chain: the bars as they are, unless they are all the same pattern
        auto same = [&m] (int a, int b)
        {
            for (auto note : notesOf (m, a, b)) for (int s = 0; s < numSteps; ++s) if (m.at (a, note, s) != m.at (b, note, s)) return false;
            return true;
        };
        bool allSame = true;
        for (int p = 1; p < bars; ++p) allSame = allSame && same (0, p);
        if (allSame) { for (int p = 1; p < numPatterns; ++p) m.pat[(size_t) p].clear(); m.chain = "A"; }
        else m.chain = String ("ABCD").substring (0, bars);
        return m;
    }
    static std::set<int> notesOf (const Model& m, int a, int b)
    {
        std::set<int> out;
        for (int p : { a, b }) for (auto& [note, row] : m.pat[(size_t) p]) out.insert (note);
        return out;
    }

    /** Keeps the patterns with the clip and writes the clip's notes out from them. */
    void save (Model& m)
    {
        ValueTree t (machineID);
        t.setProperty (chainID, m.chain, nullptr);
        t.setProperty (swingID, m.swing, nullptr);
        t.setProperty (levelID, m.level, nullptr);
        for (int p = 0; p < numPatterns; ++p)
        {
            ValueTree pt (patID);
            pt.setProperty (idID, p, nullptr);
            for (auto& [note, row] : m.pat[(size_t) p])
            {
                String text;
                bool any = false;
                for (auto v : row) { text << (v == accent ? "X" : v == hit ? "x" : "."); any = any || v != off; }
                if (! any) continue;
                ValueTree r (rowID);
                r.setProperty (noteID, note, nullptr);
                r.setProperty (stepsID, text, nullptr);
                pt.appendChild (r, nullptr);
            }
            t.appendChild (pt, nullptr);
        }
        if (auto old = clip->state.getChildWithName (machineID); old.isValid()) old.copyPropertiesAndChildrenFrom (t, &um());
        else clip->state.addChild (t, -1, &um());
        m.stored = true;
        render (m);
    }

    /** Writes the clip's notes: a bar for each letter of the chain, the chain going round to the clip's end. */
    void render (const Model& m)
    {
        auto& seq = clip->getSequence();
        StepGrid::forgetAllPatterns (*clip, &um());   // the machine writes every note of the clip: a step grid's own rows would write over it
        const double from = contentStart(), len = clipLengthBeats();
        Array<te::MidiNote*> old;
        for (auto* n : seq.getNotes()) old.add (n);   // all of them, also any past the end of a clip that has been made shorter
        for (auto* n : old) seq.removeNote (*n, &um());
        const double late = (double) m.swing / 100.0 * 0.5 * stepBeats;
        for (int bar = 0; bar < numBars(); ++bar)
            for (auto& [note, row] : m.pat[(size_t) m.patternOfBar (bar)])
                for (int s = 0; s < numSteps; ++s)
                {
                    if (row[(size_t) s] == off) continue;
                    const bool swung = s % 2 == 1;
                    const double start = bar * barBeats + s * stepBeats + (swung ? late : 0.0);
                    if (start >= len - 1.0e-4) continue;
                    seq.addNote (note, te::BeatPosition::fromBeats (from + start), te::BeatDuration::fromBeats (0.9 * (swung ? stepBeats - late : stepBeats)),
                                 row[(size_t) s] == accent ? 127 : m.level, 0, &um());
                }
    }

    //==============================================================================
    struct Layout
    {
        std::array<Rectangle<int>, numPatterns> pattern;
        std::vector<Rectangle<int>> chain;   // one for each bar of the chain, and one more to add a bar (while there is room)
        Rectangle<int> patternCaption, chainCaption, swing, swingCaption, level, levelCaption, follow, menu, note;
        float cellW = 30.0f, rowH = 28.0f, gridX = (float) labelW;
    };

    Layout layout (const Model& m) const
    {
        Layout L;
        const int capH = 13, y = 6 + capH, h = barH - y - 7;
        int x = 10;
        L.patternCaption = { x, 5, 120, capH };
        for (int p = 0; p < numPatterns; ++p) { L.pattern[(size_t) p] = { x, y, 32, h }; x += 35; }
        x += 12;
        // from the right: Patterns, Follow, Hit level, Swing; the chain has what is left between
        int right = getWidth() - 10;
        L.menu = { right - 78, y, 78, h };       right -= 84;
        L.follow = { right - 60, y, 60, h };     right -= 72;
        L.level = { right - 72, y, 72, h };      L.levelCaption = { right - 72, 5, 72, capH };   right -= 80;
        L.swing = { right - 72, y, 72, h };      L.swingCaption = { right - 72, 5, 72, capH };   right -= 86;
        L.chainCaption = { x, 5, 120, capH };
        const int slots = m.chain.length() + (m.chain.length() < maxChain ? 1 : 0);
        const int slotW = jlimit (12, 26, (right - x) / jmax (1, slots) - 2);
        for (int i = 0; i < slots; ++i) { L.chain.push_back ({ x, y, slotW, h }); x += slotW + 2; }

        const int numRows = (int) rows().size();
        L.rowH = jlimit (20.0f, 40.0f, (float) (getHeight() - gridTop()) / (float) jmax (1, numRows));
        L.cellW = jlimit (16.0f, 72.0f, ((float) (getWidth() - labelW) - 3.0f * 8.0f - 12.0f) / (float) numSteps);
        L.note = { labelW, getHeight() - 20, getWidth() - labelW - 12, 16 };
        return L;
    }
    /** A step's place (a little more room between beats, so the row reads in fours). */
    Rectangle<float> cell (const Layout& L, int visibleRow, int s) const
    {
        return Rectangle<float> (L.gridX + (float) s * L.cellW + (float) (s / 4) * 8.0f, (float) gridTop() + (float) visibleRow * L.rowH, L.cellW, L.rowH).reduced (1.5f, 2.5f);
    }
    int rowAt (float y) const
    {
        if (clip == nullptr || y < (float) gridTop()) return -1;
        const auto n = (int) rows().size();
        const float rh = jlimit (20.0f, 40.0f, (float) (getHeight() - gridTop()) / (float) jmax (1, n));
        const int r = scrollRows + (int) ((y - (float) gridTop()) / rh);
        return r < n ? r : -1;
    }
    int stepAtX (const Layout& L, float x) const
    {
        for (int s = 0; s < numSteps; ++s)
        {
            const auto c = cell (L, 0, s).expanded (1.5f, 0.0f);
            if (x >= c.getX() && x < c.getRight()) return s;
        }
        return -1;
    }

    //==============================================================================
    void showPatternMenu (int p)
    {
        PopupMenu m;
        const auto name = String::charToString ((juce_wchar) ('A' + p));
        m.addSectionHeader ("Pattern " + name);
        for (int to = 0; to < numPatterns; ++to)
            if (to != p) m.addItem ("Copy to " + String::charToString ((juce_wchar) ('A' + to)), [this, p, to] { copyPattern (p, to); selectPattern (to); });
        m.addSeparator();
        m.addItem ("Clear", [this, p] { clearPattern (p); repaint(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showMainMenu()
    {
        PopupMenu m;
        m.addSectionHeader ("Into pattern " + String::charToString ((juce_wchar) ('A' + sel)));
        for (int i = 0; i < (int) factory().size(); ++i)
            m.addItem (factory()[(size_t) i].name, [this, i] { applyFactory (i); repaint(); });
        m.addSeparator();
        PopupMenu copy;
        for (int to = 0; to < numPatterns; ++to)
            if (to != sel) copy.addItem (String::charToString ((juce_wchar) ('A' + to)), [this, to] { copyPattern (sel, to); selectPattern (to); });
        m.addSubMenu ("Copy this pattern to", copy);
        m.addItem ("Clear this pattern", [this] { clearPattern (sel); repaint(); });
        m.addSeparator();
        PopupMenu chains;
        for (auto* c : { "A", "AB", "AAAB", "AABA", "ABAC", "AABB", "ABCD", "AAABAAAC", "AABAAABC", "AAABAAACAAABAAAD" })
            chains.addItem (c, [this, c] { setChain (c); repaint(); });
        m.addSubMenu ("Chain", chains);
        m.addItem ("Make the clip as long as the chain (" + String (model().chain.length()) + (model().chain.length() == 1 ? " bar)" : " bars)"), canFitClip(), false, [this] { fitClipToChain(); repaint(); });
        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (layout (model()).menu)));
    }

    void showValueMenu (bool isSwing)
    {
        PopupMenu m;
        if (isSwing)
        {
            m.addSectionHeader ("Swing");
            for (auto [v, name] : std::initializer_list<std::pair<int, const char*>> { { 0, "Off" }, { 16, "16% (a touch)" }, { 33, "33%" }, { 50, "50%" }, { 66, "66% (triplets)" }, { 80, "80%" }, { 100, "100% (a dotted feel)" } })
                m.addItem (name, true, swing() == v, [this, v = v] { setSwing (v); repaint(); });
        }
        else
        {
            m.addSectionHeader ("Hit level");
            for (int v : { 64, 80, 96, 110, 120 })
                m.addItem (String (v) + (v == 96 ? " (as new)" : ""), true, level() == v, [this, v] { setLevel (v); repaint(); });
        }
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void showRowMenu (int note, const String& name)
    {
        PopupMenu m;
        m.addSectionHeader (name);
        m.addItem ("A hit on every step", [this, note] { fillRow (note, 1); repaint(); });
        m.addItem ("On every second step", [this, note] { fillRow (note, 2); repaint(); });
        m.addItem ("On every beat", [this, note] { fillRow (note, 4); repaint(); });
        m.addItem ("On the off-beats", [this, note] { fillRow (note, 4, 2); repaint(); });
        m.addItem ("On beats 2 and 4", [this, note] { fillRow (note, 8, 4); repaint(); });
        m.addSeparator();
        m.addItem ("Move one step later", [this, note] { shiftRow (note, 1); repaint(); });
        m.addItem ("Move one step earlier", [this, note] { shiftRow (note, -1); repaint(); });
        m.addItem ("Scatter hits at random", [this, note] { randomiseRow (note, 0.3f); repaint(); });
        m.addSeparator();
        m.addItem ("Clear this row", [this, note] { fillRow (note, 0); repaint(); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }

    void timerCallback() override
    {
        if (clip != nullptr && (clip->getTrack() == nullptr || ! clip->state.getParent().isValid())) clip = nullptr;   // taken out of the project behind our back
        if (! isShowing()) return;
        const auto [bar, step] = playing();
        double sig = bar * 7919.0 + step * 131.0 + sel * 17.0 + scrollRows + (follow ? 0.5 : 0.0);
        if (clip != nullptr)
        {
            // a clip made longer or shorter: the chain is written out over its new length
            if (const int n = numBars(); n != lastBars)
            {
                if (lastBars > 0 && isMachine() && drag == Drag::none) { auto m = model(); render (m); }
                lastBars = n;
            }
            if (follow && bar >= 0) sel = model().patternOfBar (bar);
            sig += sel * 17.0;
            if (auto t = clip->state.getChildWithName (machineID); t.isValid())
            {
                sig += (double) t[chainID].toString().hashCode() + (int) t[swingID] * 3.0 + (int) t[levelID] * 5.0;
                for (auto p : t) for (auto r : p) sig += (double) r[stepsID].toString().hashCode() * ((int) p[idID] + 1) + (int) r[noteID];
            }
            else
                for (auto* n : clip->getSequence().getNotes()) sig += n->getNoteNumber() * 31.0 + n->getStartBeat().inBeats() * 101.0 + n->getVelocity();
        }
        if (sig != lastSig) { lastSig = sig; repaint(); }
    }

    EditViewState& evs;
    te::MidiClip::Ptr clip;
    int sel = 0, scrollRows = 0, hoverRow = -1, painting = 0, dragNote = 0, dragStart = 0, lastBars = 0;
    Drag drag = Drag::none;
    double lastSig = -1.0;
};
