#pragma once

#include "Common.h"

/** The help line: one line of text that says what the thing under the mouse does, at once and without waiting
    for a tooltip to pop up. It reads the same help the tooltips show (the control's own, or that of whatever it
    sits in), so everything that has a tooltip is explained here too. */
namespace HelpLine
{
    /** The help for a component: its own, or the nearest one above it that has any. Line breaks become spaces. */
    inline String textFor (Component* c)
    {
        for (; c != nullptr; c = c->getParentComponent())
            if (auto* tip = dynamic_cast<TooltipClient*> (c))
                if (const auto t = tip->getTooltip().trim(); t.isNotEmpty())
                    return t.replace ("\r", "").replace ("\n", "  ");
        return {};
    }

    class Line : public Component,
                 private Timer
    {
    public:
        Line() { setInterceptsMouseClicks (false, false); startTimerHz (8); }

        const String& shown() const { return text; }
        /** Looks at what is under the mouse now (the line does this itself eight times a second). */
        void refresh()
        {
            auto t = textFor (Desktop::getInstance().getMainMouseSource().getComponentUnderMouse());
            if (t != text) { text = t; repaint(); }
        }
        /** Shows the help for one component (for the self-test, where no mouse is over anything). */
        void showFor (Component* c) { const auto t = textFor (c); if (t != text) { text = t; repaint(); } }

        void paint (Graphics& g) override
        {
            if (text.isEmpty()) return;
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.0f));
            g.drawText (text, getLocalBounds().reduced (8, 0), Justification::centredLeft, true);
        }

    private:
        void timerCallback() override { refresh(); }
        String text;
    };
}
