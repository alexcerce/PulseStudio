#pragma once

#include "PushMap.h"

#include "EditViewState.h"
#include "Macros.h"
#include "MidiLearn.h"
#include "Modulators.h"

//==============================================================================
/** Device-internal modulation (e.g. Wavetable's matrix): a device sets these so modulation
    sources can be dropped onto its knobs ("PulseMod:<source index>") and the knobs show the depth. */
namespace ModDrop
{
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
        value.setInterceptsMouseClicks (false, false);
        if (showName) addAndMakeVisible (name);
        addAndMakeVisible (value);
        startTimerHz (12);
        timerCallback();   // show the value straight away
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
    void timerCallback() override
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (Modulators::isModulated (*param) ? param->getCurrentBaseValue() : param->getCurrentValue(), dontSendNotification);
        value.setText (formatter ? formatter (param->getCurrentBaseValue()) : param->getCurrentValueAsStringWithLabel(), dontSendNotification);
        repaint();
    }

    EditViewState& evs;
    te::AutomatableParameter::Ptr param;
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
        return n >= 2 && n <= 16 ? n : 0;
    }

    /** True for an on / off style parameter (two options, the second meaning "on"). */
    inline bool isToggle (te::AutomatableParameter& p) { return numOptions (p) == 2; }
}

/** A lit button for an on / off parameter: shows the parameter name, click to switch. */
class ParamToggle : public TextButton, private Timer
{
public:
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

/** A drop-down for a choice parameter with many options (tables, shapes, sync values). */
class ParamCombo : public Component, private Timer
{
public:
    explicit ParamCombo (te::AutomatableParameter& p, bool showName = true) : param (&p)
    {
        const auto& r = p.valueRange;
        const int n = roundToInt (r.end - r.start) + 1;
        for (int i = 0; i < n; ++i) box.addItem (p.valueToString (r.start + (float) i), i + 1);
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
