#pragma once

#include "Graphs.h"
#include "ParamKnob.h"
#include "PulseSynths.h"

/** Wavetable's modulation UI: draggable source chips, the modulation matrix and the LFO editor. */
namespace WavetableUI
{
    using W = pulse::WavetableSynth;

    inline Colour sourceColour (int s)
    {
        const uint32 c[] = { 0xff8a929d, 0xff4dabf7, 0xff69db7c, 0xffffd43b, 0xffff922b, 0xffda77f2, 0xffff6b6b, 0xffffa94d,
                             0xff38d9a9, 0xff748ffc, 0xfff783ac, 0xffb197fc, 0xffced4da, 0xff63e6be };
        return Colour (c[jlimit (0, 13, s)]);
    }

    /** Hooks ParamKnob's modulation drops to the Wavetable matrix (call once at startup). */
    inline void installModDrop()
    {
        FactoryPresets::clearRoutes = [] (te::Plugin& p)
        {
            if (auto* w = dynamic_cast<W*> (&p))
                if (auto m = w->state.getChildWithName (W::matrixID); m.isValid()) m.removeAllChildren (w->getUndoManager());
        };
        FactoryPresets::addRoute = [] (te::Plugin& p, int source, const String& dest, float amount)
        {
            if (auto* w = dynamic_cast<W*> (&p)) w->setRoute (source, dest, amount);
        };
        ModDrop::accepts = [] (te::AutomatableParameter& p)
        {
            auto* w = dynamic_cast<W*> (p.getPlugin());
            return w != nullptr && w->isModulatable (w->indexOfParam (p.paramID));
        };
        ModDrop::drop = [] (te::AutomatableParameter& p, int source)
        {
            if (auto* w = dynamic_cast<W*> (p.getPlugin()))
            {
                w->edit.getUndoManager().beginNewTransaction ("Add modulation");
                w->setRoute (source, p.paramID, 0.5f);
            }
        };
        ModDrop::depth = [] (te::AutomatableParameter& p) -> float
        {
            if (auto* w = dynamic_cast<W*> (p.getPlugin())) return w->modulationDepth (w->indexOfParam (p.paramID));
            return 0.0f;
        };
        ModDrop::addToMenu = [] (te::AutomatableParameter& p, PopupMenu& m)
        {
            auto* w = dynamic_cast<W*> (p.getPlugin());
            if (w == nullptr) return;
            const int idx = w->indexOfParam (p.paramID);
            auto routes = w->getRoutes();
            PopupMenu sub;
            for (int i = 0; i < (int) routes.size(); ++i)
                if (routes[(size_t) i].dest == idx)
                    sub.addItem ("Remove " + W::sourceNames[routes[(size_t) i].source] + " (" + String (roundToInt (routes[(size_t) i].amount * 100)) + "%)",
                                 [w, i] { w->edit.getUndoManager().beginNewTransaction ("Remove modulation"); w->removeRoute (i); });
            if (w->isModulatable (idx))
            {
                PopupMenu add;
                for (int s = 1; s < W::sourceNames.size(); ++s)
                    add.addItem (W::sourceNames[s], [w, s, id = p.paramID] { w->edit.getUndoManager().beginNewTransaction ("Add modulation"); w->setRoute (s, id, 0.5f); });
                sub.addSubMenu ("Modulate with", add);
            }
            if (sub.getNumItems() > 0) m.addSubMenu ("Modulation", sub);
        };
    }

    //==============================================================================
    /** A draggable modulation source (drop it on any knob). */
    class SourceChip : public Component, public SettableTooltipClient
    {
    public:
        explicit SourceChip (int s) : source (s)
        {
            setTooltip ("Drag " + W::sourceNames[s] + " onto any knob of this synth to modulate it");
            setMouseCursor (MouseCursor::DraggingHandCursor);
        }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.5f);
            const auto c = sourceColour (source);
            g.setColour (c.withAlpha (hover ? 0.45f : 0.25f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (c);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            g.setColour (Theme::text);
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText (W::sourceNames[source], r, Justification::centred, true);
        }
        void mouseEnter (const MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const MouseEvent&) override  { hover = false; repaint(); }
        void mouseDrag (const MouseEvent& e) override
        {
            if (e.getDistanceFromDragStart() < 4) return;
            if (auto* c = DragAndDropContainer::findParentDragContainerFor (this))
                if (! c->isDragAndDropActive())
                    c->startDragging (ModDrop::prefix + String (source), this, ScaledImage (createComponentSnapshot (getLocalBounds())), true);
        }
        int source;
        bool hover = false;
    };

    //==============================================================================
    /** A route's amount: a bar from the centre (left is negative), drag sideways, double-click = 0. */
    struct AmountBar : public Component
    {
        float value = 0.0f;
        Colour colour { Theme::accent };
        std::function<void()> onCommit;

        AmountBar() { setMouseCursor (MouseCursor::LeftRightResizeCursor); }
        void paint (Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f, 3.0f);
            g.setColour (Pro::deep());
            g.fillRoundedRectangle (r, 4.0f);
            const float cx = r.getCentreX(), x = cx + value * r.getWidth() * 0.5f;
            g.setColour (colour.withAlpha (0.75f));
            g.fillRoundedRectangle (Rectangle<float> (jmin (cx, x), r.getY() + 2.0f, std::abs (x - cx), r.getHeight() - 4.0f), 2.5f);
            g.setColour (Theme::ink.withAlpha (0.25f));
            g.drawVerticalLine ((int) cx, r.getY() + 2.0f, r.getBottom() - 2.0f);
            g.setColour (Theme::ink.withAlpha (0.08f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            g.setColour (Theme::ink.withAlpha (0.92f));
            g.setFont (FontOptions (11.0f, Font::bold));
            g.drawText ((value > 0 ? "+" : "") + String (roundToInt (value * 100)) + "%", r, Justification::centred);
        }
        void mouseDown (const MouseEvent&) override { start = value; }
        void mouseDrag (const MouseEvent& e) override
        {
            value = jlimit (-1.0f, 1.0f, start + e.getDistanceFromDragStartX() / (float) jmax (40, getWidth()) * (e.mods.isShiftDown() ? 0.4f : 2.0f));
            value = std::round (value * 100.0f) / 100.0f;
            repaint();
        }
        void mouseUp (const MouseEvent&) override { if (value != start && onCommit) onCommit(); }
        void mouseDoubleClick (const MouseEvent&) override { value = 0.0f; repaint(); if (onCommit) onCommit(); }
        void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
        {
            value = jlimit (-1.0f, 1.0f, value + (w.deltaY > 0 ? 0.05f : -0.05f));
            repaint();
            if (onCommit) onCommit();
        }
        float start = 0.0f;
    };

    /** An LFO shape as a small picture (0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square, 5 random, 6 drawn). */
    struct ShapeButton : public Button
    {
        int shape;
        Colour colour { Theme::accent };
        ShapeButton (int s, const String& name) : Button (name), shape (s) { setTooltip (name); }
        static float valueAt (int shape, float ph)
        {
            switch (shape)
            {
                case 0:  return std::sin (ph * MathConstants<float>::twoPi);
                case 1:  return ph < 0.25f ? ph * 4.0f : ph < 0.75f ? 2.0f - ph * 4.0f : ph * 4.0f - 4.0f;
                case 2:  return ph * 2.0f - 1.0f;
                case 3:  return 1.0f - ph * 2.0f;
                case 4:  return ph < 0.5f ? 1.0f : -1.0f;
                case 5:  { const float v[] = { 0.3f, -0.7f, 0.9f, -0.2f, 0.6f, -0.9f }; return v[jlimit (0, 5, (int) (ph * 6.0f))]; }
                default: { const float v[] = { 0.0f, 0.9f, 0.5f, 0.7f, -0.6f, -0.9f, -0.2f, 0.0f };
                           const float x = ph * 7.0f; const int i = jlimit (0, 6, (int) x); return v[i] + (v[i + 1] - v[i]) * (x - (float) i); }
            }
        }
        void paintButton (Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            const bool on = getToggleState();
            g.setColour (on ? colour.withAlpha (0.22f) : Theme::ink.withAlpha (over ? 0.08f : 0.03f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (on ? colour : Theme::ink.withAlpha (0.08f));
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            auto a = r.reduced (jmax (5.0f, r.getWidth() * 0.2f), 6.0f);
            Path p;
            const int n = 40;
            for (int i = 0; i <= n; ++i)
            {
                const float ph = jmin (0.9999f, i / (float) n);
                const float x = a.getX() + a.getWidth() * i / (float) n, y = a.getCentreY() - valueAt (shape, ph) * a.getHeight() * 0.5f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (on ? colour.brighter (0.3f) : Theme::text.withAlpha (over ? 0.9f : 0.6f));
            g.strokePath (p, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
    };

    //==============================================================================
    /** The modulation matrix: one row per route (source, destination, amount, remove), plus the
        source chips to drag onto knobs. */
    class MatrixView : public Component, private Timer
    {
    public:
        explicit MatrixView (W& w) : synth (w), holder (&w)
        {
            for (int s = 1; s < W::sourceNames.size(); ++s) addAndMakeVisible (chips.add (new SourceChip (s)));
            addBtn.onClick = [this]
            {
                synth.edit.getUndoManager().beginNewTransaction ("Add modulation");
                for (int i = 0; i < synth.numParams(); ++i)   // the first knob with nothing on it yet
                    if (synth.isModulatable (i) && std::abs (synth.modulationDepth (i)) < 0.001f) { synth.setRoute (1, synth.param (i)->paramID, 0.25f); break; }
            };
            addBtn.setTooltip ("Add a route (or drag a source chip onto a knob)");
            addAndMakeVisible (addBtn);
            hint.setText ("Drag a source onto a knob. Right-click a knob to clear it.", dontSendNotification);
            hint.setColour (Label::textColourId, Theme::muted);
            hint.setFont (FontOptions (11.0f));
            addAndMakeVisible (hint);
            viewport.setViewedComponent (&rowsHolder, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);
            rebuild();
            startTimerHz (4);
        }

        void paint (Graphics&) override {}

        void resized() override
        {
            auto r = getLocalBounds();
            auto chipArea = r.removeFromTop (jmin (52, r.getHeight() / 3));
            const int perRow = jmax (1, (int) std::ceil (chips.size() / 2.0));
            const int cw = chipArea.getWidth() / perRow, ch = chipArea.getHeight() / 2;
            for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds (chipArea.getX() + (i % perRow) * cw, chipArea.getY() + (i / perRow) * ch, cw, ch);
            r.removeFromTop (4);
            auto top = r.removeFromTop (22);
            addBtn.setBounds (top.removeFromRight (90));
            hint.setBounds (top);
            r.removeFromTop (4);
            viewport.setBounds (r);
            layoutRows();
        }

        int numRoutes() const { return rows.size(); }

    private:
        struct Row : public Component
        {
            Row (MatrixView& o, int i) : owner (o), index (i)
            {
                auto& w = owner.synth;
                auto rt = w.getRoutes()[(size_t) i];
                for (int s = 1; s < W::sourceNames.size(); ++s) src.addItem (W::sourceNames[s], s);
                src.setSelectedId (jmax (1, rt.source), dontSendNotification);
                for (int p = 0; p < w.numParams(); ++p)
                    if (w.isModulatable (p)) dest.addItem (w.param (p)->getParameterName(), p + 1);
                dest.setSelectedId (rt.dest + 1, dontSendNotification);
                amount.value = rt.amount;
                amount.colour = sourceColour (rt.source);
                auto commit = [this]
                {
                    auto& ww = owner.synth;
                    ww.edit.getUndoManager().beginNewTransaction ("Change modulation");
                    const int d = dest.getSelectedId() - 1;
                    if (d >= 0) ww.setRouteAt (index, src.getSelectedId(), ww.param (d)->paramID, amount.value);
                };
                src.onChange = commit;
                dest.onChange = commit;
                amount.onCommit = commit;
                remove.onClick = [this] { auto& ww = owner.synth; ww.edit.getUndoManager().beginNewTransaction ("Remove modulation"); ww.removeRoute (index); };
                for (auto* c : std::initializer_list<Component*> { &src, &dest, &amount, &remove }) addAndMakeVisible (c);
            }
            void resized() override
            {
                auto r = getLocalBounds().reduced (0, 2);
                remove.setBounds (r.removeFromRight (24));
                r.removeFromRight (4);
                src.setBounds (r.removeFromLeft (r.getWidth() / 4));
                r.removeFromLeft (4);
                dest.setBounds (r.removeFromLeft (r.getWidth() * 55 / 100));
                r.removeFromLeft (4);
                amount.setBounds (r);
            }
            MatrixView& owner;
            int index;
            ComboBox src, dest;
            AmountBar amount;
            TextButton remove { "x" };
        };

        void rebuild()
        {
            rows.clear();
            const auto routes = synth.getRoutes();
            lastSig = signature();
            for (int i = 0; i < (int) routes.size(); ++i) rowsHolder.addAndMakeVisible (rows.add (new Row (*this, i)));
            layoutRows();
        }

        void layoutRows()
        {
            const int w = jmax (100, viewport.getWidth() - viewport.getScrollBarThickness() - 2);
            for (int i = 0; i < rows.size(); ++i) rows[i]->setBounds (0, i * 28, w, 28);
            rowsHolder.setSize (w, jmax (28, rows.size() * 28));
        }

        String signature() const
        {
            String s;
            for (auto& r : synth.getRoutes()) s << r.source << ":" << r.dest << ":" << r.amount << ";";
            return s;
        }

        void timerCallback() override
        {
            if (signature() != lastSig && ! isMouseButtonDownAnywhere()) rebuild();
        }

        W& synth;
        te::Plugin::Ptr holder;
        OwnedArray<SourceChip> chips;
        TextButton addBtn { "+ Route" };
        Label hint;
        Viewport viewport;
        Component rowsHolder;
        OwnedArray<Row> rows;
        String lastSig;
    };

    //==============================================================================
    /** The four LFOs: tabs, the shape (draw it when the shape is "Drawn"), the moving phase. */
    class LfoView : public Component, private Timer
    {
    public:
        explicit LfoView (W& w) : synth (w), holder (&w)
        {
            for (int l = 0; l < W::numLfos; ++l)
            {
                auto* b = tabs.add (new TextButton ("LFO " + String (l + 1)));
                b->setClickingTogglesState (false);
                b->setColour (TextButton::buttonOnColourId, sourceColour (l + 1).withAlpha (0.7f));
                b->onClick = [this, l] { current = l; repaint(); updateTabs(); if (onSelect) onSelect (l); };
                addAndMakeVisible (b);
            }
            for (int s = 0; s < W::lfoShapes.size(); ++s)
            {
                auto* b = shapes.add (new ShapeButton (s, W::lfoShapes[s]));
                b->setClickingTogglesState (false);
                b->onClick = [this, s]
                {
                    synth.edit.getUndoManager().beginNewTransaction ("LFO shape");
                    synth.param (synth.lfoParam (current, 2))->setParameter ((float) s, sendNotificationSync);
                };
                addAndMakeVisible (b);
            }
            updateTabs();
            startTimerHz (30);
        }

        void setCurrent (int l) { current = jlimit (0, W::numLfos - 1, l); updateTabs(); repaint(); }
        std::function<void (int)> onSelect;

        void resized() override
        {
            auto r = getLocalBounds().reduced (6);
            auto top = r.removeFromTop (22);
            const int tw = top.getWidth() / W::numLfos;
            for (auto* t : tabs) t->setBounds (top.removeFromLeft (tw).reduced (1, 0));
            r.removeFromTop (4);
            auto bottom = r.removeFromBottom (26);
            const int sw = bottom.getWidth() / jmax (1, shapes.size());
            for (auto* s : shapes) s->setBounds (bottom.removeFromLeft (sw).reduced (1, 0));
            area = r.reduced (2, 6).toFloat();
        }

        void paint (Graphics& g) override
        {
            const auto c = sourceColour (current + 1);
            auto well = area.expanded (2.0f, 4.0f);
            g.setColour (Pro::deep());
            g.fillRoundedRectangle (well, 5.0f);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawRoundedRectangle (well, 5.0f, 1.0f);
            for (int q = 1; q < 4; ++q)
                g.drawVerticalLine ((int) (area.getX() + area.getWidth() * q / 4.0f), area.getY(), area.getBottom());
            g.setColour (Theme::ink.withAlpha (0.1f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
            for (auto* s : shapes) s->colour = c;
            {
                Path fill;
                fill.startNewSubPath (area.getX(), area.getCentreY());
                for (int i = 0; i <= (int) area.getWidth(); ++i)
                    fill.lineTo (area.getX() + (float) i, area.getCentreY() - synth.lfoShape (current, i / area.getWidth(), 0.6f) * area.getHeight() * 0.45f);
                fill.lineTo (area.getRight(), area.getCentreY());
                fill.closeSubPath();
                g.setColour (c.withAlpha (0.16f));
                g.fillPath (fill);
            }
            Path p;
            for (int i = 0; i <= (int) area.getWidth(); ++i)
            {
                const double ph = i / area.getWidth();
                const float v = synth.lfoShape (current, ph, 0.6f);
                const float y = area.getCentreY() - v * area.getHeight() * 0.45f;
                if (i == 0) p.startNewSubPath (area.getX(), y); else p.lineTo (area.getX() + (float) i, y);
            }
            Graphs::glowStroke (g, p, c, 2.0f);
            if (synth.getInt (synth.lfoParam (current, 2)) == 6)   // drawn: show the points
            {
                auto pts = synth.getLfoPoints (current);
                for (int i = 0; i < W::lfoPoints; ++i)
                    Graphs::node (g, { area.getX() + area.getWidth() * i / W::lfoPoints, area.getCentreY() - pts[(size_t) i] * area.getHeight() * 0.45f }, c, false);
            }
            const float x = area.getX() + (float) synth.lfoClockPhase (current) * area.getWidth();
            g.setColour (Theme::ink.withAlpha (0.5f));
            g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
            {
                const float v = synth.lfoShape (current, synth.lfoClockPhase (current), 0.6f);
                g.setColour (Colours::white);
                g.fillEllipse (x - 3.5f, area.getCentreY() - v * area.getHeight() * 0.45f - 3.5f, 7.0f, 7.0f);
            }
            String info = synth.param (synth.lfoParam (current, 1))->getCurrentValueAsString();
            if (synth.getInt (synth.lfoParam (current, 1)) == 0) info = synth.param (synth.lfoParam (current, 0))->getCurrentValueAsString();
            info << "   " << synth.param (synth.lfoParam (current, 3))->getCurrentValueAsString();
            Graphs::label (g, info, area.withHeight (12.0f).translated (4.0f, 2.0f), Justification::centredLeft, 0.8f);
            if (synth.getInt (synth.lfoParam (current, 2)) == 6)
                Graphs::label (g, "Draw the shape with the mouse", area.withTop (area.getBottom() - 12.0f).translated (4.0f, 0), Justification::centredLeft, 0.6f);
        }

        void mouseDown (const MouseEvent& e) override { if (area.contains (e.position)) { synth.edit.getUndoManager().beginNewTransaction ("Draw LFO"); draw (e); } }
        void mouseDrag (const MouseEvent& e) override { draw (e); }

    private:
        void draw (const MouseEvent& e)
        {
            if (synth.getInt (synth.lfoParam (current, 2)) != 6)   // drawing switches the LFO to the drawn shape
                synth.param (synth.lfoParam (current, 2))->setParameter (6.0f, sendNotificationSync);
            auto pts = synth.getLfoPoints (current);
            const int i = jlimit (0, W::lfoPoints - 1, (int) ((e.position.x - area.getX()) / area.getWidth() * W::lfoPoints));
            pts[(size_t) i] = jlimit (-1.0f, 1.0f, (area.getCentreY() - e.position.y) / (area.getHeight() * 0.45f));
            synth.setLfoPoints (current, pts);
            repaint();
        }

        void updateTabs()
        {
            for (int l = 0; l < tabs.size(); ++l) tabs[l]->setToggleState (l == current, dontSendNotification);
            const int shape = synth.getInt (synth.lfoParam (current, 2));
            for (int s = 0; s < shapes.size(); ++s) shapes[s]->setToggleState (s == shape, dontSendNotification);
        }

        void timerCallback() override { updateTabs(); repaint(); }

        W& synth;
        te::Plugin::Ptr holder;
        OwnedArray<TextButton> tabs;
        OwnedArray<ShapeButton> shapes;
        Rectangle<float> area;
        int current = 0;
    };
}
