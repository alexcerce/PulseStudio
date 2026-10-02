#pragma once

#include "Common.h"
#include "Icons.h"

/** Pulse's look: dark, softly graded controls, Segoe UI, rounded everything, vector icons. */
class PulseLookAndFeel : public LookAndFeel_V4
{
public:
    PulseLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Segoe UI");
        applyTheme();
    }

    /** Takes the current Theme colours (after Theme::apply). */
    void applyTheme()
    {

        auto scheme = Theme::isLight ? LookAndFeel_V4::getLightColourScheme() : LookAndFeel_V4::getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Theme::bg);
        scheme.setUIColour (ColourScheme::widgetBackground, Theme::panel2);
        scheme.setUIColour (ColourScheme::menuBackground, Theme::panel);
        scheme.setUIColour (ColourScheme::outline, Theme::line);
        scheme.setUIColour (ColourScheme::defaultText, Theme::text);
        scheme.setUIColour (ColourScheme::defaultFill, Theme::accent);
        scheme.setUIColour (ColourScheme::highlightedText, Colours::white);
        scheme.setUIColour (ColourScheme::highlightedFill, Theme::accent.withAlpha (0.35f));
        scheme.setUIColour (ColourScheme::menuText, Theme::text);
        setColourScheme (scheme);

        setColour (TextButton::buttonColourId, Theme::panel2);
        setColour (TextButton::buttonOnColourId, Theme::accent);
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Theme::isLight ? Colours::white : Colours::black);
        setColour (ComboBox::backgroundColourId, Theme::panel2);
        setColour (ComboBox::outlineColourId, Theme::line);
        setColour (ComboBox::arrowColourId, Theme::muted);
        setColour (Slider::thumbColourId, Theme::text);
        setColour (Slider::trackColourId, Theme::accent);
        setColour (Slider::rotarySliderFillColourId, Theme::accent);
        setColour (Slider::rotarySliderOutlineColourId, Theme::well);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxTextColourId, Theme::text);
        setColour (Label::textColourId, Theme::text);
        setColour (TextEditor::backgroundColourId, Theme::well);
        setColour (TextEditor::outlineColourId, Theme::line);
        setColour (TextEditor::focusedOutlineColourId, Theme::accent);
        setColour (TextEditor::highlightColourId, Theme::accent.withAlpha (0.35f));
        setColour (PopupMenu::backgroundColourId, Theme::header);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::accent.withAlpha (0.28f));
        setColour (PopupMenu::highlightedTextColourId, Theme::text);
        setColour (PopupMenu::headerTextColourId, Theme::muted);
        setColour (ListBox::backgroundColourId, Theme::panel);
        setColour (ResizableWindow::backgroundColourId, Theme::bg);
        setColour (ScrollBar::thumbColourId, Theme::line);
        setColour (AlertWindow::backgroundColourId, Theme::panel);
        setColour (AlertWindow::textColourId, Theme::text);
        setColour (AlertWindow::outlineColourId, Theme::line);
        setColour (TooltipWindow::backgroundColourId, Theme::raised);
        setColour (TooltipWindow::textColourId, Theme::text);
        setColour (TooltipWindow::outlineColourId, Theme::line);
        setColour (ToggleButton::textColourId, Theme::text);
        setColour (ToggleButton::tickColourId, Theme::accent);
        setColour (ToggleButton::tickDisabledColourId, Theme::muted);
        setColour (TreeView::backgroundColourId, Theme::panel);
        setColour (TreeView::selectedItemBackgroundColourId, Theme::accent.withAlpha (0.25f));
        setColour (DirectoryContentsDisplayComponent::highlightColourId, Theme::accent.withAlpha (0.25f));
        setColour (TabbedButtonBar::tabOutlineColourId, Colours::transparentBlack);
        setColour (TabbedComponent::outlineColourId, Theme::line);
    }

    //==============================================================================
    // Buttons
    void drawButtonBackground (Graphics& g, Button& b, const Colour& bgColour, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = b.getToggleState();
        auto c = bgColour;
        if (down)       c = c.brighter (0.14f);
        else if (over)  c = c.brighter (0.06f);

        g.setGradientFill (ColourGradient (c.brighter (0.06f), 0.0f, r.getY(), c.darker (0.1f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (lit ? c.brighter (0.35f) : Theme::line.brighter (over ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    Font getTextButtonFont (TextButton&, int buttonHeight) override
    {
        return FontOptions (jmin (13.5f, (float) buttonHeight * 0.56f));
    }

    void drawToggleButton (Graphics& g, ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        auto box = r.removeFromLeft (r.getHeight()).withSizeKeepingCentre (15.0f, 15.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? Theme::accent : (over ? Theme::panel2.brighter (0.1f) : Theme::panel2));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (on ? Theme::accent.brighter (0.3f) : Theme::line.brighter (0.2f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        if (on)
        {
            Path tick;
            tick.startNewSubPath (box.getX() + 3.5f, box.getCentreY());
            tick.lineTo (box.getX() + 6.5f, box.getBottom() - 4.0f);
            tick.lineTo (box.getRight() - 3.5f, box.getY() + 4.0f);
            g.setColour (Colours::black.withAlpha (0.85f));
            g.strokePath (tick, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        g.setColour (b.findColour (ToggleButton::textColourId).withAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.setFont (FontOptions (13.5f));
        g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), Justification::centredLeft, 2);
    }

    //==============================================================================
    // Knobs and faders
    void drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, Slider& s) override
    {
        auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
        auto radius = jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        const float lineW = jmax (2.5f, radius * 0.16f);
        auto arcR = radius - lineW * 0.5f;
        auto fill = s.findColour (Slider::rotarySliderFillColourId);
        if (! s.isEnabled()) fill = fill.withSaturation (0.0f).withAlpha (0.5f);

        // body
        auto bodyR = arcR - lineW * 0.9f;
        g.setGradientFill (ColourGradient (Theme::line, centre.x, centre.y - bodyR,
                                           Theme::panel, centre.x, centre.y + bodyR, false));
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (Colours::black.withAlpha (0.4f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

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
            g.setColour (fill.withAlpha (0.25f));   // soft glow
            g.strokePath (arc, PathStrokeType (lineW + 3.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (fill);
            g.strokePath (arc, PathStrokeType (lineW, PathStrokeType::curved, PathStrokeType::rounded));
        }

        Point<float> p (centre.x + bodyR * 0.25f * std::sin (a1), centre.y - bodyR * 0.25f * std::cos (a1));
        Point<float> q (centre.x + bodyR * 0.85f * std::sin (a1), centre.y - bodyR * 0.85f * std::cos (a1));
        g.setColour (Theme::text);
        g.drawLine ({ p, q }, jmax (1.6f, radius * 0.09f));
    }

    void drawLinearSlider (Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, Slider::SliderStyle style, Slider& s) override
    {
        if (style == Slider::LinearVertical)
        {
            // mixer fader: recessed groove, value fill, ridged cap
            auto cx = (float) x + (float) width * 0.5f;
            auto groove = Rectangle<float> (cx - 2.5f, (float) y, 5.0f, (float) height);
            g.setColour (Theme::well);
            g.fillRoundedRectangle (groove, 2.5f);
            g.setColour (s.findColour (Slider::trackColourId).withAlpha (0.55f));
            g.fillRoundedRectangle (groove.withTop (sliderPos), 2.5f);

            auto cap = Rectangle<float> (jmin ((float) width - 2.0f, 30.0f), 24.0f).withCentre ({ cx, sliderPos });
            g.setColour (Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 4.0f);
            g.setGradientFill (ColourGradient (Colour (0xff6a717d), cap.getX(), cap.getY(), Theme::line, cap.getX(), cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 4.0f);
            g.setColour (Theme::ink.withAlpha (0.12f));
            g.drawRoundedRectangle (cap.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (Theme::text);
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 8.0f, 2.0f));
            return;
        }

        if (style == Slider::LinearHorizontal || style == Slider::LinearBar)
        {
            auto cy = (float) y + (float) height * 0.5f;
            auto groove = Rectangle<float> ((float) x, cy - 2.0f, (float) width, 4.0f);
            g.setColour (Theme::well);
            g.fillRoundedRectangle (groove, 2.0f);
            g.setColour (s.findColour (Slider::trackColourId));
            g.fillRoundedRectangle (groove.withRight (sliderPos), 2.0f);
            g.setColour (Theme::text);
            g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre ({ sliderPos, cy }));
            return;
        }

        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, s);
    }

    //==============================================================================
    // Combo boxes and menus
    void drawComboBox (Graphics& g, int width, int height, bool, int, int, int, int, ComboBox& box) override
    {
        auto r = Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
        const bool over = box.isMouseOver (true);
        g.setGradientFill (ColourGradient (Theme::panel2.brighter (over ? 0.08f : 0.03f), 0, 0, Theme::panel2.darker (0.1f), 0, (float) height, false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) ? Theme::accent : Theme::line.brighter (over ? 0.25f : 0.0f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        Icons::draw (g, Icons::Type::chevronDown, Rectangle<float> ((float) width - 20.0f, 0.0f, 14.0f, (float) height).reduced (0, (float) height * 0.25f),
                     Theme::muted, 1.5f);
    }

    Font getComboBoxFont (ComboBox&) override   { return FontOptions (13.5f); }

    void positionComboBoxText (ComboBox& box, Label& label) override
    {
        label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    Font getPopupMenuFont() override            { return FontOptions (14.0f); }

    void drawPopupMenuBackground (Graphics& g, int width, int height) override
    {
        g.fillAll (findColour (PopupMenu::backgroundColourId));
        g.setColour (Theme::line.brighter (0.15f));
        g.drawRect (0, 0, width, height, 1);
    }

    void drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu, const String& text, const String& shortcutKeyText,
                            const Drawable* icon, const Colour* textColour) override
    {
        if (isSeparator)
        {
            g.setColour (Theme::line);
            g.fillRect (area.reduced (8, 0).withSizeKeepingCentre (area.getWidth() - 16, 1));
            return;
        }

        auto r = area.reduced (4, 1);
        if (isHighlighted && isActive)
        {
            g.setColour (findColour (PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
        }

        auto textCol = textColour != nullptr ? *textColour : Theme::text;
        if (! isActive) textCol = textCol.withAlpha (0.4f);

        auto tickArea = r.removeFromLeft (22);
        if (isTicked)
        {
            Path tick;
            auto t = tickArea.toFloat().withSizeKeepingCentre (10.0f, 10.0f);
            tick.startNewSubPath (t.getX(), t.getCentreY());
            tick.lineTo (t.getX() + 3.5f, t.getBottom() - 1.0f);
            tick.lineTo (t.getRight(), t.getY() + 1.0f);
            g.setColour (Theme::accent);
            g.strokePath (tick, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        else if (icon != nullptr)
        {
            icon->drawWithin (g, tickArea.toFloat().reduced (3.0f), RectanglePlacement::centred, 1.0f);
        }

        if (hasSubMenu)
            Icons::draw (g, Icons::Type::chevronRight, r.removeFromRight (16).toFloat().reduced (0, 4), textCol.withAlpha (0.7f), 1.4f);

        g.setColour (textCol);
        g.setFont (getPopupMenuFont());
        if (shortcutKeyText.isNotEmpty())
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (12.5f));
            g.drawText (shortcutKeyText, r.withTrimmedRight (6), Justification::centredRight, true);
            g.setColour (textCol);
            g.setFont (getPopupMenuFont());
        }
        g.drawFittedText (text, r.withTrimmedRight (shortcutKeyText.isNotEmpty() ? 110 : 4), Justification::centredLeft, 1);
    }

    void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& area, const String& name) override
    {
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.5f, Font::bold));
        g.drawText (name.toUpperCase(), area.reduced (12, 0), Justification::bottomLeft, true);
    }

    //==============================================================================
    // Scrollbars, tabs, tooltips, alerts
    void drawScrollbar (Graphics& g, ScrollBar&, int x, int y, int width, int height, bool vertical,
                        int thumbPos, int thumbSize, bool over, bool down) override
    {
        auto thumb = vertical ? Rectangle<int> (x, thumbPos, width, thumbSize) : Rectangle<int> (thumbPos, y, thumbSize, height);
        g.setColour (findColour (ScrollBar::thumbColourId).brighter (down ? 0.3f : over ? 0.15f : 0.0f));
        g.fillRoundedRectangle (thumb.toFloat().reduced (2.5f), 3.0f);
    }

    int getDefaultScrollbarWidth() override { return 10; }

    void drawTabButton (TabBarButton& b, Graphics& g, bool over, bool) override
    {
        auto r = b.getActiveArea().toFloat().reduced (2.0f, 3.0f);
        const bool front = b.isFrontTab();
        if (front || over)
        {
            g.setColour (front ? Theme::panel2.brighter (0.08f) : Theme::panel2);
            g.fillRoundedRectangle (r, 5.0f);
        }
        if (front)
        {
            g.setColour (Theme::accent);
            g.fillRoundedRectangle (r.removeFromBottom (2.0f).reduced (6.0f, 0.0f), 1.0f);
        }
        g.setColour (front ? Theme::text : Theme::muted);
        g.setFont (FontOptions (13.5f, front ? Font::bold : Font::plain));
        g.drawText (b.getButtonText(), b.getActiveArea(), Justification::centred);
    }

    void drawTabAreaBehindFrontButton (TabbedButtonBar&, Graphics&, int, int) override {}

    Rectangle<int> getTooltipBounds (const String& tipText, Point<int> screenPos, Rectangle<int> parentArea) override
    {
        auto tl = layoutTooltipText (tipText, Theme::text);
        auto w = (int) (tl.getWidth() + 18.0f), h = (int) (tl.getHeight() + 10.0f);
        return Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
                 .constrainedWithin (parentArea);
    }

    void drawTooltip (Graphics& g, const String& text, int width, int height) override
    {
        auto r = Rectangle<float> ((float) width, (float) height);
        g.setColour (findColour (TooltipWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (findColour (TooltipWindow::outlineColourId));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        layoutTooltipText (text, findColour (TooltipWindow::textColourId)).draw (g, r.reduced (9.0f, 5.0f));
    }

    void drawAlertBox (Graphics& g, AlertWindow& alert, const Rectangle<int>& textArea, TextLayout& textLayout) override
    {
        auto r = alert.getLocalBounds().toFloat();
        g.setColour (findColour (AlertWindow::backgroundColourId));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (Theme::line.brighter (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (r.withHeight (3.0f).reduced (8.0f, 0.0f), 1.5f);
        g.setColour (findColour (AlertWindow::textColourId));
        textLayout.draw (g, textArea.withLeft (18).toFloat());
    }

    int getAlertWindowButtonHeight() override { return 30; }
    Font getAlertWindowTitleFont() override   { return FontOptions (17.0f, Font::bold); }
    Font getAlertWindowMessageFont() override { return FontOptions (14.0f); }
    Font getAlertWindowFont() override        { return FontOptions (14.0f); }

private:
    static TextLayout layoutTooltipText (const String& text, Colour colour)
    {
        AttributedString s;
        s.setJustification (Justification::centredLeft);
        s.append (text, FontOptions (13.0f), colour);
        TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, 360.0f);
        return tl;
    }
};
