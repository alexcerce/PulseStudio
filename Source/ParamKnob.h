#pragma once

#include "EditViewState.h"
#include "Macros.h"
#include "MidiLearn.h"

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
        slider.onDragStart = [this] { param->parameterChangeGestureBegin(); };
        slider.onDragEnd   = [this] { param->parameterChangeGestureEnd(); };
        slider.onValueChange = [this] { param->setParameter ((float) slider.getValue(), sendNotificationSync); };
        if (auto def = p.getDefaultValue())
            slider.setDoubleClickReturnValue (true, *def);
        slider.addMouseListener (this, false);
        addAndMakeVisible (slider);

        name.setText (p.getParameterName(), dontSendNotification);
        name.setFont (FontOptions (10.5f));
        name.setColour (Label::textColourId, Theme::muted);
        name.setJustificationType (Justification::centred);
        name.setInterceptsMouseClicks (false, false);
        value.setFont (FontOptions (10.5f));
        value.setJustificationType (Justification::centred);
        value.setInterceptsMouseClicks (false, false);
        if (showName) addAndMakeVisible (name);
        addAndMakeVisible (value);
        startTimerHz (12);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (name.isVisible()) name.setBounds (r.removeFromTop (14));
        value.setBounds (r.removeFromBottom (14));
        slider.setBounds (r.reduced (8, 0));
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
            g.setColour (Theme::accent);
            g.fillEllipse ((float) getWidth() - 7.0f, 2.0f, 5.0f, 5.0f);
        }
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;

        PopupMenu m;
        m.addSectionHeader (MidiLearn::describe (*param));
        if (evs.midiLearn != nullptr) evs.midiLearn->addToMenu (m, *param);
        Macros::addToMenu (m, *param);
        if (auto def = param->getDefaultValue())
            m.addItem ("Reset to default", [this, v = *def] { param->setParameter (v, sendNotificationSync); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this));
    }

    te::AutomatableParameter& getParam() { return *param; }
    Label name;

private:
    void timerCallback() override
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (param->getCurrentValue(), dontSendNotification);
        value.setText (param->getCurrentValueAsStringWithLabel(), dontSendNotification);
        repaint();
    }

    EditViewState& evs;
    te::AutomatableParameter::Ptr param;
    Slider slider;
    Label value;
};

