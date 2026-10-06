#pragma once

#include "EditViewState.h"

//==============================================================================
/** Header for an automation lane: pick the parameter, see its value, clear the curve. */
class AutomationHeader : public Component,
                         private Timer
{
public:
    AutomationHeader (EditViewState& e, te::Track::Ptr t) : evs (e), track (t)
    {
        chooseBtn.setTooltip ("Choose which parameter this lane automates");
        chooseBtn.onClick = [this] { showParamMenu(); };
        clearBtn.setTooltip ("Remove all automation points for this parameter");
        clearBtn.onClick = [this]
        {
            if (auto p = evs.getAutomationParam (*track))
            {
                evs.edit.getUndoManager().beginNewTransaction ("Clear automation");
                p->getCurve().clear (&evs.edit.getUndoManager());
            }
        };
        value.setFont (FontOptions (12.0f));
        value.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (chooseBtn);
        addAndMakeVisible (clearBtn);
        addAndMakeVisible (value);
        startTimerHz (10);
        timerCallback();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        g.setColour (track->getColour().withAlpha (0.6f));
        g.fillRect (0, 0, 5, getHeight());
        g.setColour (Theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (12).reduced (4, 8);
        chooseBtn.setBounds (r.removeFromTop (24));
        r.removeFromTop (6);
        clearBtn.setBounds (r.removeFromRight (52).removeFromTop (22));
        value.setBounds (r.removeFromTop (22));
    }

private:
    void timerCallback() override
    {
        if (auto p = evs.getAutomationParam (*track))
        {
            auto name = p->getParameterName();
            if (auto pl = p->getPlugin()) name = pl->getName() + ": " + name;
            chooseBtn.setButtonText (name);
            value.setText (p->getCurrentValueAsStringWithLabel()
                             + (p->hasAutomationPoints() ? "   (" + String (p->getCurve().getNumPoints()) + " pts)" : ""),
                           dontSendNotification);
        }
    }

    void showParamMenu()
    {
        PopupMenu m;
        auto current = evs.getAutomationParam (*track);

        for (auto plugin : EditViewState::pluginsOf (*track))
        {
            if (dynamic_cast<te::LevelMeterPlugin*> (plugin) != nullptr || plugin->getPluginType() == "pulseLoudness")
                continue;

            PopupMenu sub;
            for (auto param : plugin->getAutomatableParameters())
            {
                PopupMenu::Item item (param->getParameterName() + (param->hasAutomationPoints() ? "  *" : ""));
                item.isTicked = param == current;
                item.action = [this, p = te::AutomatableParameter::Ptr (param)] { evs.setAutomationParam (*track, p.get()); };
                sub.addItem (item);
            }

            if (sub.getNumItems() > 0)
                m.addSubMenu (plugin == EditViewState::volumeOf (*track) ? "Mixer (volume / pan)" : plugin->getName(), sub);
        }

        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (&chooseBtn));
    }

    EditViewState& evs;
    te::Track::Ptr track;
    TextButton chooseBtn, clearBtn { "Clear" };
    Label value;
};

//==============================================================================
/** Draws and edits the automation curve of one parameter.
    Click: add point | drag: move | right-click or double-click a point: delete. */
class AutomationLane : public Component,
                       public SettableTooltipClient,
                       private Timer
{
public:
    AutomationLane (EditViewState& e, te::Track::Ptr t) : evs (e), track (t)
    {
        startTimerHz (15);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::bg2);
        drawGrid (g);
        g.setColour (Theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);

        auto param = evs.getAutomationParam (*track);
        if (param == nullptr) return;

        auto& curve = param->getCurve();
        const auto col = track->getColour();
        const auto area = getLocalBounds().toFloat().reduced (0.0f, padY);

        // curve
        Path path;
        const float fallback = param->getCurrentValue();
        for (int x = 0; x <= getWidth(); x += 2)
        {
            auto v = curve.getNumPoints() > 0 ? curve.getValueAt (evs.xToTime ((float) x), fallback) : fallback;
            auto y = valueToY (*param, v, area);
            if (x == 0) path.startNewSubPath ((float) x, y);
            else        path.lineTo ((float) x, y);
        }

        if (curve.getNumPoints() > 0)
        {
            Path fill (path);
            fill.lineTo ((float) getWidth(), area.getBottom());
            fill.lineTo (0.0f, area.getBottom());
            fill.closeSubPath();
            g.setColour (col.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (col);
            g.strokePath (path, PathStrokeType (1.6f));
        }
        else
        {
            g.setColour (col.withAlpha (0.5f));
            const float dashes[] = { 4.0f, 4.0f };
            g.drawDashedLine ({ 0.0f, valueToY (*param, fallback, area), (float) getWidth(), valueToY (*param, fallback, area) },
                              dashes, 2, 1.0f);
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.0f));
            g.drawText ("Click to add automation points", getLocalBounds().reduced (8, 4), Justification::topLeft);
        }

        // points
        for (int i = 0; i < curve.getNumPoints(); ++i)
        {
            auto p = pointPos (*param, i, area);
            g.setColour (i == hoverIndex || i == dragIndex ? Colours::white : col);
            g.fillEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f);
            g.setColour (Colours::black.withAlpha (0.6f));
            g.drawEllipse (p.x - 4.0f, p.y - 4.0f, 8.0f, 8.0f, 1.0f);
        }
    }

    void mouseMove (const MouseEvent& e) override
    {
        auto idx = findPoint (e.position);
        if (idx != hoverIndex) { hoverIndex = idx; repaint(); }
        setMouseCursor (evs.drawMode ? MouseCursor::CrosshairCursor
                        : idx >= 0 ? MouseCursor::DraggingHandCursor
                        : e.mods.isAltDown() ? MouseCursor::UpDownResizeCursor : MouseCursor::CrosshairCursor);
        setTooltip (evs.drawMode ? "Pencil: drag to draw automation" : "Click: add point | drag: move | Alt-drag a line: bend it | right-click: delete");
    }

    void mouseExit (const MouseEvent&) override { hoverIndex = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        auto param = evs.getAutomationParam (*track);
        if (param == nullptr) return;

        auto& um = evs.edit.getUndoManager();
        um.beginNewTransaction ("Edit automation");
        auto idx = findPoint (e.position);
        bendIndex = -1;
        lastDrawBeat = -1.0;

        // pencil: draw points as you drag
        if (evs.drawMode && ! e.mods.isPopupMenu())
        {
            drawAt (*param, e);
            dragIndex = -1;
            return;
        }

        // Alt-drag on a line bends that segment
        if (idx < 0 && e.mods.isAltDown() && ! e.mods.isPopupMenu())
        {
            bendIndex = segmentAt (*param, e.position.x);
            if (bendIndex >= 0) bendStart = param->getCurve().getPointCurve (bendIndex);
            dragIndex = -1;
            return;
        }

        if (e.mods.isPopupMenu())
        {
            if (idx >= 0)
                param->getCurve().removePoint (idx, &um);
            dragIndex = -1;
            repaint();
            return;
        }

        if (idx < 0)
            idx = param->getCurve().addPoint (timeAt (e.position.x, e.mods), yToValue (*param, e.position.y), 0.0f, &um);

        dragIndex = idx;
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        auto param = evs.getAutomationParam (*track);
        if (param == nullptr) return;

        if (evs.drawMode) { drawAt (*param, e); return; }

        if (bendIndex >= 0)
        {
            auto bend = jlimit (-1.0f, 1.0f, bendStart - (float) e.getDistanceFromDragStartY() / 80.0f);
            param->getCurve().setCurveValue (bendIndex, bend, &evs.edit.getUndoManager());
            repaint();
            return;
        }

        if (dragIndex < 0) return;

        dragIndex = param->getCurve().movePoint (*param, dragIndex, timeAt (e.position.x, e.mods),
                                                 yToValue (*param, e.position.y), false, &evs.edit.getUndoManager());
        repaint();
    }

    void mouseUp (const MouseEvent&) override { dragIndex = -1; bendIndex = -1; lastDrawBeat = -1.0; repaint(); }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        auto param = evs.getAutomationParam (*track);
        if (param == nullptr) return;

        if (auto idx = findPoint (e.position); idx >= 0)
            param->getCurve().removePoint (idx, &evs.edit.getUndoManager());
        repaint();
    }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
            evs.zoom (std::exp (w.deltaY * 1.5), (float) e.x);
        else if (e.mods.isShiftDown())
            evs.scrollBy (-w.deltaY * 400.0 / evs.pixelsPerBeat);
        else
            Component::mouseWheelMove (e, w);
    }

private:
    static constexpr float padY = 6.0f;

    /** Index of the point that starts the segment under x (the segment bent by Alt-drag). */
    int segmentAt (te::AutomatableParameter& p, float x) const
    {
        auto& c = p.getCurve();
        auto t = evs.xToTime (x);
        int idx = -1;
        for (int i = 0; i < c.getNumPoints(); ++i)
            if (c.getPointTime (i) <= t) idx = i;
        return idx >= 0 && idx < c.getNumPoints() - 1 ? idx : -1;
    }

    /** Pencil: replaces the points under the stroke with steps at the grid size (free when snap is off). */
    void drawAt (te::AutomatableParameter& p, const MouseEvent& e)
    {
        auto& c = p.getCurve();
        auto* um = &evs.edit.getUndoManager();
        const double step = evs.snapBeats > 0.0 ? evs.snapBeats : 1.0 / 16.0;
        const double beat = jmax (0.0, std::floor (evs.xToBeat (e.position.x) / step) * step);
        const float value = yToValue (p, e.position.y);

        auto stepRange = [&] (double b)
        {
            return te::TimeRange (EngineHelpers::beatToTime (evs.edit, b),
                                  EngineHelpers::beatToTime (evs.edit, b + step) - te::TimeDuration::fromSeconds (0.0001));
        };

        double from = lastDrawBeat < 0.0 ? beat : jmin (lastDrawBeat, beat);
        double to = lastDrawBeat < 0.0 ? beat : jmax (lastDrawBeat, beat);
        for (double b = from; b <= to + 1.0e-9; b += step)
        {
            auto r = stepRange (b);
            c.removePointsInRegion (r.withEnd (r.getEnd() + te::TimeDuration::fromSeconds (0.0001)), um);
            c.addPoint (r.getStart(), value, 0.0f, um);
            c.addPoint (r.getEnd(), value, 0.0f, um);
        }
        lastDrawBeat = beat;
        repaint();
    }

    void timerCallback() override { repaint(); }

    void drawGrid (Graphics& g)
    {
        const int bpb = EngineHelpers::beatsPerBar (evs.edit);
        double step = 1.0;
        for (double s : { 1.0, (double) bpb, bpb * 4.0, bpb * 16.0 })
            if (s * evs.pixelsPerBeat >= 14.0) { step = s; break; }

        for (double b = std::floor (evs.xToBeat (0.0f) / step) * step; evs.beatToX (b) < getWidth(); b += step)
        {
            const bool bar = std::abs (std::fmod (b, (double) bpb)) < 1.0e-6;
            g.setColour (Theme::ink.withAlpha (bar ? 0.08f : 0.03f));
            g.drawVerticalLine ((int) evs.beatToX (b), 0.0f, (float) getHeight());
        }
    }

    te::TimePosition timeAt (float x, ModifierKeys mods) const
    {
        auto beat = evs.xToBeat (x);
        if (! mods.isAltDown())   // Alt = free placement
            beat = evs.snap (beat);
        return EngineHelpers::beatToTime (evs.edit, jmax (0.0, beat));
    }

    static float valueToY (te::AutomatableParameter& p, float v, Rectangle<float> area)
    {
        auto n = jlimit (0.0f, 1.0f, p.valueRange.convertTo0to1 (jlimit (p.valueRange.start, p.valueRange.end, v)));
        return area.getBottom() - n * area.getHeight();
    }

    float yToValue (te::AutomatableParameter& p, float y) const
    {
        auto area = getLocalBounds().toFloat().reduced (0.0f, padY);
        auto n = jlimit (0.0f, 1.0f, (area.getBottom() - y) / area.getHeight());
        return p.valueRange.convertFrom0to1 (n);
    }

    Point<float> pointPos (te::AutomatableParameter& p, int i, Rectangle<float> area) const
    {
        auto& c = p.getCurve();
        return { evs.timeToX (c.getPointTime (i)), valueToY (p, c.getPointValue (i), area) };
    }

    int findPoint (Point<float> pos)
    {
        auto param = evs.getAutomationParam (*track);
        if (param == nullptr) return -1;

        auto area = getLocalBounds().toFloat().reduced (0.0f, padY);
        for (int i = param->getCurve().getNumPoints(); --i >= 0;)
            if (pointPos (*param, i, area).getDistanceFrom (pos) <= 7.0f)
                return i;

        return -1;
    }

    EditViewState& evs;
    te::Track::Ptr track;
    int hoverIndex = -1, dragIndex = -1, bendIndex = -1;
    float bendStart = 0.0f;
    double lastDrawBeat = -1.0;
};
