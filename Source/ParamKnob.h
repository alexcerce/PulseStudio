#pragma once

#include "EditViewState.h"
#include "Macros.h"
#include "MidiLearn.h"
#include "Modulators.h"

//==============================================================================
/** Rotary bound to an AutomatableParameter, with a right-click menu
    (MIDI Learn, Map to Macro). */
class ParamKnob : public Component,
                  public SettableTooltipClient,
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

    void resized() override
    {
        auto r = getLocalBounds();
        const int textH = compact ? 12 : 14;
        if (name.isVisible()) name.setBounds (r.removeFromTop (textH));
        value.setBounds (r.removeFromBottom (textH));
        slider.setBounds (r.reduced (compact ? 4 : 8, 0));
    }

    void paintOverChildren (Graphics& g) override
    {
        // small dots: orange = MIDI mapped, blue = macro-controlled
        if (evs.midiLearn != nullptr && evs.midiLearn->isMapped (*param))
        {
            g.setColour (Theme::orange);
            g.fillEllipse (2.0f, 2.0f, 5.0f, 5.0f);
        }
        if (! param->getModifiers().isEmpty())
        {
            g.setColour (Modulators::isModulated (*param) ? Colour (0xffb197fc) : Theme::accent);
            g.fillEllipse ((float) getWidth() - 7.0f, 2.0f, 5.0f, 5.0f);
        }
        if (Modulators::isModulated (*param))   // where the modulation has moved it right now
        {
            auto b = slider.getBounds().toFloat().reduced (3.0f);
            const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f + 1.5f;
            auto params = slider.getRotaryParameters();
            const float a = params.startAngleRadians + param->getCurrentNormalisedValue() * (params.endAngleRadians - params.startAngleRadians);
            auto c = b.getCentre();
            g.setColour (Colour (0xffb197fc));
            g.fillEllipse (c.x + r * std::sin (a) - 2.5f, c.y - r * std::cos (a) - 2.5f, 5.0f, 5.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;

        PopupMenu m;
        m.addSectionHeader (MidiLearn::describe (*param));
        if (evs.midiLearn != nullptr) evs.midiLearn->addToMenu (m, *param);
        Macros::addToMenu (m, *param);
        Modulators::addToMenu (m, *param);
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
};

