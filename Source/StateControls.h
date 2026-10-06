#pragma once

#include "Common.h"

/** Some built-in Tracktion devices keep settings as plain state values rather than
    automatable parameters (Chorus and Phaser keep *all* of them that way). These
    descriptors + controls expose them in the UI. Changes are undoable and saved. */
namespace StateControls
{
    struct Desc
    {
        Identifier id;
        String label, unit;
        double min = 0, max = 1, def = 0, skewMid = 0, step = 0;
        StringArray choices, choiceValues;   // for text-valued settings (e.g. filter mode)
    };

    inline std::vector<Desc> forPlugin (te::Plugin& p)
    {
        const auto type = p.getPluginType();

        if (type == te::ChorusPlugin::xmlTypeName)
            return { { te::IDs::depthMs, "Depth", "ms", 0.1, 10.0, 3.0, 2.0 },
                     { te::IDs::speedHz, "Rate", "Hz", 0.05, 10.0, 1.0, 1.0 },
                     { te::IDs::width, "Width", "%", 0.0, 1.0, 0.5 },
                     { te::IDs::mixProportion, "Mix", "%", 0.0, 1.0, 0.5 } };

        if (type == te::PhaserPlugin::xmlTypeName)
            return { { te::IDs::depth, "Depth", "oct", 0.5, 8.0, 5.0 },
                     { te::IDs::rate, "Rate", "", 0.01, 4.0, 0.4, 0.5 },
                     { te::IDs::feedback, "Feedback", "%", 0.0, 0.95, 0.7 } };

        if (type == te::DelayPlugin::xmlTypeName)
            return { { te::IDs::length, "Time", "ms", 1.0, 2000.0, 150.0, 300.0, 1.0 } };

        if (type == te::LowPassPlugin::xmlTypeName)
        {
            Desc mode { te::IDs::mode, "Mode" };
            mode.choices = { "Low pass", "High pass" };
            mode.choiceValues = { "lowpass", "highpass" };
            return { mode };
        }

        return {};
    }

    inline String format (const Desc& d, double v)
    {
        if (d.unit == "%")  return String (roundToInt (v * 100.0)) + "%";
        if (d.unit == "ms") return v >= 1000.0 ? String (v / 1000.0, 2) + " s" : String (v, v < 10.0 ? 1 : 0) + " ms";
        if (d.unit == "Hz") return String (v, 2) + " Hz";
        if (d.unit == "oct") return String (v, 1) + " oct";
        return String (v, 2);
    }

    //==============================================================================
    /** Knob (or drop-down for text settings) bound to one state property of a plugin. */
    class Control : public Component,
                    private Timer
    {
    public:
        Control (te::Plugin& p, Desc d) : plugin (&p), desc (std::move (d))
        {
            name.setText (desc.label, dontSendNotification);
            name.setFont (FontOptions (11.5f));
            name.setColour (Label::textColourId, Theme::muted);
            name.setJustificationType (Justification::centred);
            name.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (name);

            if (! desc.choices.isEmpty())
            {
                box.addItemList (desc.choices, 1);
                box.onChange = [this]
                {
                    plugin->edit.getUndoManager().beginNewTransaction ("Change " + desc.label);
                    plugin->state.setProperty (desc.id, desc.choiceValues[box.getSelectedId() - 1], &plugin->edit.getUndoManager());
                };
                addAndMakeVisible (box);
            }
            else
            {
                slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
                slider.setRange (desc.min, desc.max, desc.step);
                if (desc.skewMid > desc.min && desc.skewMid < desc.max)
                    slider.setSkewFactorFromMidPoint (desc.skewMid);
                slider.setDoubleClickReturnValue (true, desc.def);
                slider.onDragStart = [this] { plugin->edit.getUndoManager().beginNewTransaction ("Change " + desc.label); };
                slider.onValueChange = [this]
                {
                    plugin->state.setProperty (desc.id, slider.getValue(), &plugin->edit.getUndoManager());
                    value.setText (format (desc, slider.getValue()), dontSendNotification);
                };
                addAndMakeVisible (slider);
                value.setFont (FontOptions (11.5f));
                value.setJustificationType (Justification::centred);
                value.setInterceptsMouseClicks (false, false);
                addAndMakeVisible (value);
            }

            refresh();
            startTimerHz (6);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromTop (14));
            if (! desc.choices.isEmpty())
            {
                box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 4, 22));
                return;
            }
            value.setBounds (r.removeFromBottom (14));
            slider.setBounds (r.reduced (8, 0));
        }

    private:
        void timerCallback() override { refresh(); }   // follows undo / preset loads

        void refresh()
        {
            auto v = plugin->state.getProperty (desc.id);
            if (! desc.choices.isEmpty())
            {
                auto idx = desc.choiceValues.indexOf (v.toString());
                box.setSelectedId (idx >= 0 ? idx + 1 : 1, dontSendNotification);
                return;
            }
            const double d = v.isVoid() ? desc.def : (double) v;
            if (! slider.isMouseButtonDown())
                slider.setValue (d, dontSendNotification);
            value.setText (format (desc, d), dontSendNotification);
        }

        te::Plugin::Ptr plugin;
        Desc desc;
        Label name, value;
        Slider slider;
        ComboBox box;
    };

    //==============================================================================
    /** Note values for the delay's tempo-sync menu (in beats). */
    inline void showDelaySyncMenu (te::Plugin& p, Component& target)
    {
        PopupMenu m;
        const std::pair<const char*, double> notes[] = { { "1/32", 0.125 }, { "1/16", 0.25 }, { "1/16 dotted", 0.375 }, { "1/8 triplet", 1.0 / 3.0 },
                                                         { "1/8", 0.5 }, { "1/8 dotted", 0.75 }, { "1/4 triplet", 2.0 / 3.0 }, { "1/4", 1.0 },
                                                         { "1/4 dotted", 1.5 }, { "1/2", 2.0 }, { "1 bar", 4.0 } };
        for (auto& [name, beats] : notes)
            m.addItem (name, [ptr = te::Plugin::Ptr (&p), b = beats]
            {
                const double bpm = ptr->edit.tempoSequence.getTempo (0)->getBpm();
                ptr->edit.getUndoManager().beginNewTransaction ("Delay sync");
                ptr->state.setProperty (te::IDs::length, jlimit (1, 2000, roundToInt (b * 60000.0 / bpm)), &ptr->edit.getUndoManager());
            });
        SearchMenu::withSearch (m).showMenuAsync (PopupMenu::Options().withTargetComponent (&target));
    }
}
