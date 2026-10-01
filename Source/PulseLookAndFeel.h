#pragma once

#include "Common.h"

/** Dark, flat look in the style of the web prototype. */
class PulseLookAndFeel : public LookAndFeel_V4
{
public:
    PulseLookAndFeel()
    {
        auto scheme = LookAndFeel_V4::getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Theme::bg);
        scheme.setUIColour (ColourScheme::widgetBackground, Theme::panel2);
        scheme.setUIColour (ColourScheme::menuBackground, Theme::panel);
        scheme.setUIColour (ColourScheme::outline, Theme::line);
        scheme.setUIColour (ColourScheme::defaultText, Theme::text);
        scheme.setUIColour (ColourScheme::defaultFill, Theme::accent);
        scheme.setUIColour (ColourScheme::highlightedText, Colours::black);
        scheme.setUIColour (ColourScheme::highlightedFill, Theme::accent);
        scheme.setUIColour (ColourScheme::menuText, Theme::text);
        setColourScheme (scheme);

        setColour (TextButton::buttonColourId, Theme::panel2);
        setColour (TextButton::buttonOnColourId, Theme::accent);
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Colours::black);
        setColour (ComboBox::backgroundColourId, Theme::panel2);
        setColour (ComboBox::outlineColourId, Theme::line);
        setColour (Slider::thumbColourId, Theme::text);
        setColour (Slider::trackColourId, Theme::accent);
        setColour (Slider::rotarySliderFillColourId, Theme::accent);
        setColour (Slider::rotarySliderOutlineColourId, Colour (0xff0d0f12));
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxTextColourId, Theme::text);
        setColour (Label::textColourId, Theme::text);
        setColour (TextEditor::backgroundColourId, Colour (0xff0f1114));
        setColour (TextEditor::outlineColourId, Theme::line);
        setColour (PopupMenu::backgroundColourId, Theme::panel);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (ListBox::backgroundColourId, Theme::panel);
        setColour (ResizableWindow::backgroundColourId, Theme::bg);
        setColour (ScrollBar::thumbColourId, Colour (0xff3a3f48));
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour& bgColour, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = bgColour;

        if (down)       c = c.brighter (0.15f);
        else if (over)  c = c.brighter (0.07f);

        g.setColour (c);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (b.getToggleState() ? c : Theme::line);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }

    Font getTextButtonFont (TextButton&, int buttonHeight) override
    {
        return FontOptions (jmin (14.0f, (float) buttonHeight * 0.55f));
    }

    void drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, Slider& s) override
    {
        auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        auto radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto lineW = 3.5f;
        auto arcR = radius - lineW * 0.5f;

        Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (findColour (Slider::rotarySliderOutlineColourId));
        g.strokePath (track, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));

        // symmetric (bipolar) params like pan or +/-dB gain draw from the centre
        auto range = s.getRange();
        const bool bipolar = range.getStart() < 0.0 && std::abs (range.getStart() + range.getEnd()) < 1.0e-6;
        auto zeroPos = bipolar ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
        auto a0 = startAngle + zeroPos * (endAngle - startAngle);
        auto a1 = startAngle + pos * (endAngle - startAngle);

        if (std::abs (a1 - a0) > 0.001f)
        {
            Path arc;
            arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, jmin (a0, a1), jmax (a0, a1), true);
            g.setColour (s.findColour (Slider::rotarySliderFillColourId));
            g.strokePath (arc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
        }

        Point<float> p (centre.x + (arcR - 6.0f) * std::sin (a1), centre.y - (arcR - 6.0f) * std::cos (a1));
        Point<float> q (centre.x + (arcR + 0.5f) * std::sin (a1), centre.y - (arcR + 0.5f) * std::cos (a1));
        g.setColour (Theme::text);
        g.drawLine ({ p, q }, 2.0f);
    }

    void drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, Slider::SliderStyle style, Slider& s) override
    {
        if (style != Slider::LinearVertical)
            return LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, s);

        // Mixer-style fader
        auto cx = (float) x + (float) width * 0.5f;
        g.setColour (Colour (0xff0d0f12));
        g.fillRoundedRectangle (cx - 2.0f, (float) y, 4.0f, (float) height, 2.0f);

        auto thumb = Rectangle<float> ((float) width - 4.0f, 22.0f).withCentre ({ cx, sliderPos });
        g.setGradientFill (ColourGradient (Colour (0xff5a616c), thumb.getX(), thumb.getY(),
                                           Colour (0xff3a4049), thumb.getX(), thumb.getBottom(), false));
        g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (Theme::text);
        g.fillRect (thumb.withSizeKeepingCentre (thumb.getWidth() - 6.0f, 2.0f));
    }
};
