#pragma once

#include "PushMap.h"

#include "EditViewState.h"
#include "Macros.h"
#include "ValueEntry.h"
#include "MidiLearn.h"
#include "Modulators.h"

//==============================================================================
/** Device-internal modulation (e.g. Wavetable's matrix): a device sets these so modulation
    sources can be dropped onto its knobs ("PulseMod:<source index>") and the knobs show the depth. */
namespace ModDrop
{
    /** Where something built into the device (a filter envelope, an LFO) sweeps this parameter:
        the lowest and highest value it reaches, in the parameter's own units. False = nothing. */
    inline std::function<bool (te::AutomatableParameter&, float& lo, float& hi)> sweep;

    inline const String prefix { "PulseMod:" };
    inline std::function<bool (te::AutomatableParameter&)> accepts;                    // can this knob be modulated?
    inline std::function<void (te::AutomatableParameter&, int source)> drop;            // add a route
    inline std::function<float (te::AutomatableParameter&)> depth;                      // total depth (-1..1), 0 = none
    inline std::function<void (te::AutomatableParameter&, PopupMenu&)> addToMenu;      // routes in the right-click menu
}

/** Rotary bound to an AutomatableParameter, with a right-click menu
    (MIDI Learn, Map to Macro). */
class ParamKnob : public Component,
                  public SettableTooltipClient,
                  public DragAndDropTarget,
                  private Timer
{
public:
    te::AutomatableParameter& getParameter() const { return *param; }
    ParamKnob (EditViewState& e, te::AutomatableParameter& p, bool showName = true) : evs (e), param (&p)
    {
        auto range = p.getValueRange();
        slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        // follow the parameter's own mapping (skew, steps), e.g. 20 Hz - 20 kHz with 1 kHz at the centre
        NormalisableRange<double> nr (range.getStart(), range.getEnd(),
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.convertFrom0to1 ((float) v); },
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.convertTo0to1 ((float) v); },
                                      [pp = param] (double, double, double v) { return (double) pp->valueRange.snapToLegalValue ((float) v); });
        slider.setNormalisableRange (nr);
        slider.setValue (p.getCurrentValue(), dontSendNotification);
        slider.onDragStart = [this]
        {
            param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName());
            param->parameterChangeGestureBegin();
        };
        slider.onDragEnd   = [this] { param->parameterChangeGestureEnd(); };
        slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), sendNotificationSync); };
        if (auto def = p.getDefaultValue())
            slider.setDoubleClickReturnValue (true, *def);
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);

        name.setText (p.getParameterName(), dontSendNotification);
        name.setFont (FontOptions (11.5f));
        name.setColour (Label::textColourId, Theme::muted);
        name.setJustificationType (Justification::centred);
        name.setInterceptsMouseClicks (false, false);
        value.setFont (FontOptions (11.5f));
        value.setJustificationType (Justification::centred);
        // click the value to type one: "2k", "-6", "35%", "0.02 s", or the name of a choice
        value.setEditable (true, false, true);
        value.setTooltip ("Click to type a value");
        value.setColour (Label::backgroundWhenEditingColourId, Theme::well);
        value.setColour (Label::outlineWhenEditingColourId, Theme::accent);
        value.setColour (Label::textWhenEditingColourId, Theme::text);
        value.onEditorShow = [this]
        {
            if (auto* ed = value.getCurrentTextEditor())
            {
                ed->setJustification (Justification::centred);
                ed->setInputRestrictions (32);
                ed->setFont (value.getFont());
                ed->selectAll();
            }
        };
        value.onTextChange = [this] { typeValue (value.getText()); };
        if (showName) addAndMakeVisible (name);
        addAndMakeVisible (value);
        startTimerHz (12);
        timerCallback();   // show the value straight away
    }

    /** A knob that was given no help of its own still says what it is, and how a knob is used. */
    String getTooltip() override
    {
        const auto own = SettableTooltipClient::getTooltip();
        return (own.isNotEmpty() ? own : param->getParameterName()) + "  (drag or use the wheel, double-click for the default, click the value to type one, right-click for more)";
    }

    /** Sets the value from typed text (what the box under the knob does). False when the text means nothing here. */
    bool typeValue (const String& text)
    {
        const bool ok = ValueEntry::apply (*param, text);
        refreshValueText();
        return ok;
    }

    /** A shorter label than the parameter's name (e.g. "Freq" inside a "Band 3" section). */
    void setLabel (const String& t) { name.setText (t, dontSendNotification); }

    void resized() override
    {
        auto r = getLocalBounds();
        const int textH = compact ? 12 : 14;
        if (name.isVisible()) name.setBounds (r.removeFromTop (textH));
        value.setBounds (r.removeFromBottom (textH));
        slider.setBounds (r.reduced (compact ? 4 : 8, 0));
    }

    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith (ModDrop::prefix) && ModDrop::accepts && ModDrop::accepts (*param);
    }
    void itemDragEnter (const SourceDetails&) override { modHover = true; repaint(); }
    void itemDragExit (const SourceDetails&) override  { modHover = false; repaint(); }
    void itemDropped (const SourceDetails& d) override
    {
        modHover = false;
        if (ModDrop::drop) ModDrop::drop (*param, d.description.toString().fromFirstOccurrenceOf (ModDrop::prefix, false, false).getIntValue());
        repaint();
    }

    void paintOverChildren (Graphics& g) override
    {
        if (modHover)
        {
            g.setColour (Theme::green.withAlpha (0.25f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        }
        if (const float depth = ModDrop::depth ? ModDrop::depth (*param) : 0.0f; std::abs (depth) > 0.001f)
        {
            // the modulation range as an arc next to the knob's own ring
            auto b = slider.getBounds().toFloat().reduced (3.0f);
            const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f + 2.5f;
            auto rp = slider.getRotaryParameters();
            const float span = rp.endAngleRadians - rp.startAngleRadians;
            const float v = param->getCurrentNormalisedValue();
            const float a0 = rp.startAngleRadians + v * span, a1 = rp.startAngleRadians + jlimit (0.0f, 1.0f, v + depth) * span;
            Path arc;
            arc.addCentredArc (b.getCentreX(), b.getCentreY(), r, r, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
            g.setColour (Theme::green);
            g.strokePath (arc, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (float lo = 0, hi = 0; ModDrop::sweep && ModDrop::sweep (*param, lo, hi))
        {
            // the range an envelope or LFO moves this value over, as a thin arc outside the knob's ring
            auto b = slider.getBounds().toFloat().reduced (3.0f);
            // outside the modulation arc when there is one, so both stay readable
            const bool hasDepth = ModDrop::depth && std::abs (ModDrop::depth (*param)) > 0.001f;
            const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f + (hasDepth ? 6.5f : 3.5f);
            auto rp = slider.getRotaryParameters();
            const float span = rp.endAngleRadians - rp.startAngleRadians;
            const auto& range = param->valueRange;
            const float n0 = range.convertTo0to1 (jlimit (range.start, range.end, lo)), n1 = range.convertTo0to1 (jlimit (range.start, range.end, hi));
            if (std::abs (n1 - n0) > 0.004f)
            {
                Path arc;
                arc.addCentredArc (b.getCentreX(), b.getCentreY(), r, r, 0.0f, rp.startAngleRadians + jmin (n0, n1) * span, rp.startAngleRadians + jmax (n0, n1) * span, true);
                const auto c = slider.findColour (Slider::rotarySliderFillColourId).brighter (0.6f);
                g.setColour (c.withAlpha (0.3f));
                g.strokePath (arc, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
                g.setColour (c);
                g.strokePath (arc, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));
                // a dot at the far end (where the envelope takes it)
                const float cur = param->getCurrentNormalisedValue();
                const float far = std::abs (n1 - cur) >= std::abs (n0 - cur) ? n1 : n0;
                const float a = rp.startAngleRadians + far * span;
                g.fillEllipse (b.getCentreX() + r * std::sin (a) - 2.5f, b.getCentreY() - r * std::cos (a) - 2.5f, 5.0f, 5.0f);
            }
        }
        // small dots: orange = MIDI mapped, blue = macro-controlled
        if (evs.midiLearn != nullptr && evs.midiLearn->isMapped (*param))
        {
            g.setColour (Theme::orange);
            g.fillEllipse (2.0f, 2.0f, 5.0f, 5.0f);
        }
        if (! param->getModifiers().isEmpty())
        {
            g.setColour (Modulators::isModulated (*param) ? Theme::purple : Theme::accent);
            g.fillEllipse ((float) getWidth() - 7.0f, 2.0f, 5.0f, 5.0f);
        }
        if (Modulators::isModulated (*param))   // where the modulation has moved it right now
        {
            auto b = slider.getBounds().toFloat().reduced (3.0f);
            const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f + 1.5f;
            auto params = slider.getRotaryParameters();
            const float a = params.startAngleRadians + param->getCurrentNormalisedValue() * (params.endAngleRadians - params.startAngleRadians);
            auto c = b.getCentre();
            g.setColour (Theme::purple);
            g.fillEllipse (c.x + r * std::sin (a) - 2.5f, c.y - r * std::cos (a) - 2.5f, 5.0f, 5.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;

        PopupMenu m;
        m.addSectionHeader (MidiLearn::describe (*param));
        if (ModDrop::addToMenu) ModDrop::addToMenu (*param, m);
        if (evs.midiLearn != nullptr) evs.midiLearn->addToMenu (m, *param);
        Macros::addToMenu (m, *param);
        Modulators::addToMenu (m, *param);
        PushMap::addToMenu (m, *param);
        m.addItem ("Type a value...", [sp = SafePointer<ParamKnob> (this)] { if (sp != nullptr) sp->value.showEditor(); });
        if (auto def = param->getDefaultValue())
            m.addItem ("Reset to default", [this, v = *def] { param->setParameter (v, sendNotificationSync); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

    te::AutomatableParameter& getParam() { return *param; }

    /** Smaller text for dense device cards, and the device's accent colour on the ring. */
    void setCompact (Colour accent)
    {
        compact = true;
        name.setFont (FontOptions (10.5f));
        value.setFont (FontOptions (10.5f, Font::bold));
        value.setColour (Label::textColourId, Theme::text.withAlpha (0.9f));
        slider.setColour (Slider::rotarySliderFillColourId, accent);
        resized();
    }

    /** Optional value text (e.g. names for choice parameters). */
    std::function<String (float)> formatter;
    Label name;

private:
    void refreshValueText() { value.setText (formatter ? formatter (param->getCurrentBaseValue()) : param->getCurrentValueAsStringWithLabel(), dontSendNotification); }

    void timerCallback() override
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (Modulators::isModulated (*param) ? param->getCurrentBaseValue() : param->getCurrentValue(), dontSendNotification);
        if (! value.isBeingEdited()) refreshValueText();
        // the overlays (modulation arcs and dots) only need drawing again when one of them has moved
        float lo = 0, hi = 0;
        const bool swept = ModDrop::sweep && ModDrop::sweep (*param, lo, hi);
        const double sig = param->getCurrentValue() * 3.1 + (ModDrop::depth ? ModDrop::depth (*param) : 0.0f) * 17.0 + (swept ? lo * 0.37 + hi * 0.11 : 0.0)
                           + param->getModifiers().size() * 101.0 + (Modulators::isModulated (*param) ? 1009.0 : 0.0)
                           + (evs.midiLearn != nullptr && evs.midiLearn->isMapped (*param) ? 5003.0 : 0.0) + (modHover ? 7.0 : 0.0);
        if (sig != lastOverlaySig) { lastOverlaySig = sig; repaint(); }
    }

    EditViewState& evs;
    te::AutomatableParameter::Ptr param;
    double lastOverlaySig = -1.0e30;
    Slider slider;
    Label value;
    bool compact = false;
    bool modHover = false;
};


//==============================================================================
/** Choice parameters as buttons instead of knobs: a toggle for on / off parameters, a row of
    option buttons for choices with a few options. */
namespace ParamButtons
{
    /** Number of options when the parameter is a small choice (2..8), otherwise 0. */
    inline int numOptions (te::AutomatableParameter& p)
    {
        const auto& r = p.valueRange;
        if (r.interval < 0.999f || r.start != 0.0f) return 0;
        const int n = roundToInt (r.end - r.start) + 1;
        return n >= 2 && n <= 40 ? n : 0;
    }

    /** True for an on / off style parameter (two options, the second meaning "on"). */
    inline bool isToggle (te::AutomatableParameter& p) { return numOptions (p) == 2; }
}

/** A lit button for an on / off parameter: shows the parameter name, click to switch. */
class ParamToggle : public TextButton, private Timer
{
public:
    te::AutomatableParameter& getParameter() const { return *param; }
    ParamToggle (te::AutomatableParameter& p, const String& label = {}, Colour on = Theme::accent) : param (&p)
    {
        setButtonText (label.isNotEmpty() ? label : p.getParameterName());
        setClickingTogglesState (false);
        setColour (TextButton::buttonOnColourId, on.withAlpha (0.85f));
        setColour (TextButton::textColourOnId, Colour (0xff111317));
        setTooltip (p.getParameterName() + ": " + p.valueToString (0.0f) + " / " + p.valueToString (1.0f));
        onClick = [this]
        {
            param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName());
            param->setParameter (param->getCurrentValue() > 0.5f ? 0.0f : 1.0f, sendNotificationSync);
        };
        startTimerHz (10);
        timerCallback();
    }
private:
    void timerCallback() override { setToggleState (param->getCurrentValue() > 0.5f, dontSendNotification); }
    te::AutomatableParameter::Ptr param;
};

/** One button per option of a choice parameter (Clean / Punchy / Vintage ...). */
class ParamChoice : public Component, private Timer
{
public:
    te::AutomatableParameter& getParameter() const { return *param; }
    ParamChoice (te::AutomatableParameter& p, Colour on = Theme::accent, bool showName = true) : param (&p)
    {
        const int n = jmax (2, ParamButtons::numOptions (p));
        for (int i = 0; i < n; ++i)
        {
            auto* b = buttons.add (new TextButton (p.valueToString ((float) i)));
            b->setColour (TextButton::buttonOnColourId, on.withAlpha (0.85f));
            b->setColour (TextButton::textColourOnId, Colour (0xff111317));
            b->setConnectedEdges ((i > 0 ? Button::ConnectedOnLeft : 0) | (i < n - 1 ? Button::ConnectedOnRight : 0));
            b->onClick = [this, i]
            {
                param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName());
                param->setParameter ((float) i, sendNotificationSync);
            };
            b->setTooltip (p.getParameterName() + ": " + p.valueToString ((float) i));
            addAndMakeVisible (b);
        }
        if (showName)
        {
            name.setText (p.getParameterName(), dontSendNotification);
            name.setFont (FontOptions (11.0f));
            name.setColour (Label::textColourId, Theme::muted);
            name.setJustificationType (Justification::centred);
            addAndMakeVisible (name);
        }
        startTimerHz (10);
        timerCallback();
    }

    /** Help for the whole row of buttons (each button otherwise says the parameter's name and its own choice). */
    void setTooltip (const String& t) { for (auto* b : buttons) b->setTooltip (t); }

    void setLabel (const String& t) { name.setText (t, dontSendNotification); }

    /** A good width for the buttons' labels. */
    int idealWidth() const
    {
        int w = 0;
        for (auto* b : buttons) w += jmax (34, GlyphArrangement::getStringWidthInt (FontOptions (12.0f), b->getButtonText()) + 16);
        return w;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (name.isVisible()) name.setBounds (r.removeFromTop (14));
        r = r.withSizeKeepingCentre (r.getWidth(), jmin (26, r.getHeight()));
        const int total = jmax (1, idealWidth()), full = r.getWidth();
        for (auto* b : buttons)
        {
            const int w = b == buttons.getLast() ? r.getWidth()   // the last one takes what is left (no rounding gap)
                                                 : full * jmax (34, GlyphArrangement::getStringWidthInt (FontOptions (12.0f), b->getButtonText()) + 16) / total;
            b->setBounds (r.removeFromLeft (w));
        }
    }

private:
    void timerCallback() override
    {
        const int v = roundToInt (param->getCurrentValue());
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == v, dontSendNotification);
    }
    te::AutomatableParameter::Ptr param;
    OwnedArray<TextButton> buttons;
    Label name;
};

/** A choice in the space of one knob: the name, and the current option in a small box. Click for
    the list of options, or use the wheel to step through them. */
class ParamMenuBox : public Component, public SettableTooltipClient, private Timer
{
public:
    te::AutomatableParameter& getParameter() const { return *param; }
    ParamMenuBox (te::AutomatableParameter& p, Colour c = Theme::accent) : param (&p), colour (c)
    {
        label = p.getParameterName();
        setTooltip (p.getParameterName() + ": click to choose");
        setMouseCursor (MouseCursor::PointingHandCursor);
        startTimerHz (8);
    }
    void setLabel (const String& t) { label = t; repaint(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::muted);
        g.setFont (FontOptions (10.5f));
        g.drawText (label, r.removeFromTop (12.0f), Justification::centred, true);
        auto box = r.withSizeKeepingCentre (r.getWidth() - 4.0f, jmin (26.0f, r.getHeight() - 6.0f));
        g.setColour (Theme::well);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (over ? colour : Theme::line);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (box.withWidth (2.5f).reduced (0.0f, 4.0f).translated (2.0f, 0.0f), 1.2f);
        g.setColour (Theme::text);
        g.setFont (FontOptions (10.5f, Font::bold));
        g.drawFittedText (shown, box.reduced (6.0f, 1.0f).toNearestInt(), Justification::centred, 2, 0.75f);
    }
    void mouseEnter (const MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const MouseEvent&) override  { over = false; repaint(); }
    void mouseDown (const MouseEvent&) override
    {
        PopupMenu m;
        const int n = jmax (2, ParamButtons::numOptions (*param));
        const int cur = roundToInt (param->getCurrentValue());
        for (int i = 0; i < n; ++i)
            m.addItem (i + 1, param->valueToString ((float) i), true, i == cur);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [sp = SafePointer<ParamMenuBox> (this)] (int r)
        {
            if (sp == nullptr || r <= 0) return;
            sp->param->getEdit().getUndoManager().beginNewTransaction ("Change " + sp->param->getParameterName());
            sp->param->setParameter ((float) (r - 1), sendNotificationSync);
        });
    }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        const int n = jmax (2, ParamButtons::numOptions (*param));
        param->setParameter ((float) jlimit (0, n - 1, roundToInt (param->getCurrentValue()) + (w.deltaY > 0 ? -1 : 1)), sendNotificationSync);
    }
private:
    void timerCallback() override
    {
        auto t = param->getCurrentValueAsString();
        if (t != shown) { shown = t; repaint(); }
    }
    te::AutomatableParameter::Ptr param;
    Colour colour;
    String label, shown;
    bool over = false;
};

/** A drop-down for a choice parameter with many options (tables, shapes, sync values). */
class ParamCombo : public Component, private Timer
{
public:
    te::AutomatableParameter& getParameter() const { return *param; }
    explicit ParamCombo (te::AutomatableParameter& p, bool showName = true) : param (&p)
    {
        const auto& r = p.valueRange;
        const int n = roundToInt (r.end - r.start) + 1;
        for (int i = 0; i < n; ++i) box.addItem (p.valueToString (r.start + (float) i), i + 1);
        box.setTooltip (p.getParameterName());
        box.onChange = [this]
        {
            const float v = param->valueRange.start + (float) (box.getSelectedId() - 1);
            if (std::abs (v - param->getCurrentValue()) < 0.01f) return;
            param->getEdit().getUndoManager().beginNewTransaction ("Change " + param->getParameterName());
            param->setParameter (v, sendNotificationSync);
        };
        addAndMakeVisible (box);
        if (showName)
        {
            name.setText (p.getParameterName(), dontSendNotification);
            name.setFont (FontOptions (11.0f));
            name.setColour (Label::textColourId, Theme::muted);
            name.setJustificationType (Justification::centred);
            addAndMakeVisible (name);
        }
        startTimerHz (10);
        timerCallback();
    }
    void setLabel (const String& t) { name.setText (t, dontSendNotification); }
    void resized() override
    {
        auto r = getLocalBounds();
        if (name.isVisible()) name.setBounds (r.removeFromTop (14));
        box.setBounds (r.withSizeKeepingCentre (r.getWidth(), jmin (26, r.getHeight())));
    }
private:
    void timerCallback() override
    {
        const int id = roundToInt (param->getCurrentValue() - param->valueRange.start) + 1;
        if (box.getSelectedId() != id && ! box.isPopupActive()) box.setSelectedId (id, dontSendNotification);
    }
    te::AutomatableParameter::Ptr param;
    ComboBox box;
    Label name;
};
