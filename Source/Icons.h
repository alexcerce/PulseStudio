#pragma once

#include "Common.h"

/** Vector icons (no font glyphs, so they always render) and a button that shows one. */
namespace Icons
{
    enum class Type
    {
        play, pause, stop, record, loop, metronome, link, undo, redo, plus, close, menu,
        chevronLeft, chevronRight, chevronDown, chevronUp, power, followAction, gear, folder,
        browser, session, arrange, mixer, piano, devices, scissors, waveform, pencil, search
    };

    /** The icon in a unit square (0..1); stroke icons are drawn with strokeWidth relative to that. */
    inline Path make (Type t, bool& filled)
    {
        Path p;
        filled = false;
        auto arc = [&p] (float cx, float cy, float r, float a0, float a1)
        {
            p.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
        };
        const float pi = MathConstants<float>::pi;

        switch (t)
        {
            case Type::play:     filled = true; p.addTriangle (0.28f, 0.18f, 0.28f, 0.82f, 0.84f, 0.5f); break;
            case Type::pause:    filled = true; p.addRoundedRectangle (0.24f, 0.2f, 0.17f, 0.6f, 0.03f); p.addRoundedRectangle (0.59f, 0.2f, 0.17f, 0.6f, 0.03f); break;
            case Type::stop:     filled = true; p.addRoundedRectangle (0.24f, 0.24f, 0.52f, 0.52f, 0.06f); break;
            case Type::record:   filled = true; p.addEllipse (0.22f, 0.22f, 0.56f, 0.56f); break;
            case Type::loop:
                arc (0.5f, 0.5f, 0.3f, 0.35f, pi * 2.0f - 0.35f);
                p.startNewSubPath (0.62f, 0.17f); p.lineTo (0.62f + 0.0f, 0.33f); p.lineTo (0.46f, 0.25f);
                break;
            case Type::metronome:
                p.startNewSubPath (0.3f, 0.85f); p.lineTo (0.42f, 0.15f); p.lineTo (0.58f, 0.15f); p.lineTo (0.7f, 0.85f); p.closeSubPath();
                p.startNewSubPath (0.5f, 0.68f); p.lineTo (0.76f, 0.3f);
                break;
            case Type::link:
                p.addRoundedRectangle (0.12f, 0.36f, 0.42f, 0.28f, 0.14f);
                p.addRoundedRectangle (0.46f, 0.36f, 0.42f, 0.28f, 0.14f);
                break;
            case Type::undo:
                arc (0.52f, 0.56f, 0.28f, -pi * 0.55f, pi * 0.75f);
                p.startNewSubPath (0.16f, 0.24f); p.lineTo (0.27f, 0.42f); p.lineTo (0.42f, 0.27f);
                break;
            case Type::redo:
                arc (0.48f, 0.56f, 0.28f, -pi * 0.75f, pi * 0.55f);
                p.startNewSubPath (0.84f, 0.24f); p.lineTo (0.73f, 0.42f); p.lineTo (0.58f, 0.27f);
                break;
            case Type::plus:     p.startNewSubPath (0.5f, 0.2f); p.lineTo (0.5f, 0.8f); p.startNewSubPath (0.2f, 0.5f); p.lineTo (0.8f, 0.5f); break;
            case Type::close:    p.startNewSubPath (0.27f, 0.27f); p.lineTo (0.73f, 0.73f); p.startNewSubPath (0.73f, 0.27f); p.lineTo (0.27f, 0.73f); break;
            case Type::menu:
                for (float y : { 0.28f, 0.5f, 0.72f }) { p.startNewSubPath (0.2f, y); p.lineTo (0.8f, y); }
                break;
            case Type::chevronLeft:  p.startNewSubPath (0.6f, 0.22f); p.lineTo (0.34f, 0.5f); p.lineTo (0.6f, 0.78f); break;
            case Type::chevronRight: p.startNewSubPath (0.4f, 0.22f); p.lineTo (0.66f, 0.5f); p.lineTo (0.4f, 0.78f); break;
            case Type::chevronDown:  p.startNewSubPath (0.22f, 0.38f); p.lineTo (0.5f, 0.66f); p.lineTo (0.78f, 0.38f); break;
            case Type::chevronUp:    p.startNewSubPath (0.22f, 0.62f); p.lineTo (0.5f, 0.34f); p.lineTo (0.78f, 0.62f); break;
            case Type::power:
                arc (0.5f, 0.54f, 0.28f, 0.6f, pi * 2.0f - 0.6f);
                p.startNewSubPath (0.5f, 0.14f); p.lineTo (0.5f, 0.48f);
                break;
            case Type::followAction:
                arc (0.42f, 0.5f, 0.26f, -pi * 0.5f, pi * 0.6f);
                p.startNewSubPath (0.42f, 0.24f); p.lineTo (0.84f, 0.24f);
                p.startNewSubPath (0.72f, 0.12f); p.lineTo (0.84f, 0.24f); p.lineTo (0.72f, 0.36f);
                break;
            case Type::gear:
                arc (0.5f, 0.5f, 0.17f, 0.0f, pi * 2.0f);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = (float) i * pi / 4.0f;
                    p.startNewSubPath (0.5f + 0.27f * std::sin (a), 0.5f - 0.27f * std::cos (a));
                    p.lineTo (0.5f + 0.38f * std::sin (a), 0.5f - 0.38f * std::cos (a));
                }
                break;
            case Type::folder:
                p.startNewSubPath (0.14f, 0.28f); p.lineTo (0.4f, 0.28f); p.lineTo (0.48f, 0.36f); p.lineTo (0.86f, 0.36f);
                p.lineTo (0.86f, 0.76f); p.lineTo (0.14f, 0.76f); p.closeSubPath();
                break;
            case Type::browser:
                p.addRoundedRectangle (0.14f, 0.2f, 0.72f, 0.6f, 0.06f);
                p.startNewSubPath (0.38f, 0.2f); p.lineTo (0.38f, 0.8f);
                break;
            case Type::session:
                for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
                    p.addRoundedRectangle (0.16f + (float) c * 0.24f, 0.16f + (float) r * 0.24f, 0.18f, 0.18f, 0.03f);
                break;
            case Type::arrange:
                for (int r = 0; r < 3; ++r) { p.startNewSubPath (0.16f + (float) r * 0.1f, 0.28f + (float) r * 0.22f); p.lineTo (0.62f + (float) r * 0.1f, 0.28f + (float) r * 0.22f); }
                break;
            case Type::mixer:
                for (int i = 0; i < 3; ++i)
                {
                    const float x = 0.25f + (float) i * 0.25f, y = i == 1 ? 0.62f : 0.38f;
                    p.startNewSubPath (x, 0.16f); p.lineTo (x, 0.84f);
                    p.startNewSubPath (x - 0.08f, y); p.lineTo (x + 0.08f, y);
                }
                break;
            case Type::piano:
                p.addRoundedRectangle (0.14f, 0.22f, 0.72f, 0.56f, 0.04f);
                for (float x : { 0.32f, 0.5f, 0.68f }) { p.startNewSubPath (x, 0.22f); p.lineTo (x, 0.78f); }
                break;
            case Type::devices:
                p.addRoundedRectangle (0.12f, 0.3f, 0.32f, 0.4f, 0.05f);
                p.addRoundedRectangle (0.56f, 0.3f, 0.32f, 0.4f, 0.05f);
                p.startNewSubPath (0.44f, 0.5f); p.lineTo (0.56f, 0.5f);
                break;
            case Type::scissors:
                p.addEllipse (0.16f, 0.58f, 0.22f, 0.22f); p.addEllipse (0.62f, 0.58f, 0.22f, 0.22f);
                p.startNewSubPath (0.32f, 0.6f); p.lineTo (0.7f, 0.16f); p.startNewSubPath (0.68f, 0.6f); p.lineTo (0.3f, 0.16f);
                break;
            case Type::waveform:
            {
                const float h[] = { 0.12f, 0.3f, 0.2f, 0.36f, 0.16f, 0.28f, 0.1f };
                for (int i = 0; i < 7; ++i) { const float x = 0.17f + (float) i * 0.11f; p.startNewSubPath (x, 0.5f - h[i]); p.lineTo (x, 0.5f + h[i]); }
                break;
            }
            case Type::pencil:
                p.startNewSubPath (0.2f, 0.8f); p.lineTo (0.24f, 0.62f); p.lineTo (0.66f, 0.2f); p.lineTo (0.8f, 0.34f); p.lineTo (0.38f, 0.76f); p.closeSubPath();
                break;
            case Type::search:
                p.addEllipse (0.18f, 0.18f, 0.44f, 0.44f);
                p.startNewSubPath (0.56f, 0.56f); p.lineTo (0.82f, 0.82f);
                break;
        }
        return p;
    }

    /** Draws an icon fitted into `area`. */
    inline void draw (Graphics& g, Type t, Rectangle<float> area, Colour c, float strokeWidth = 1.7f)
    {
        bool filled = false;
        auto p = make (t, filled);
        const float s = jmin (area.getWidth(), area.getHeight());
        auto box = area.withSizeKeepingCentre (s, s);
        p.applyTransform (AffineTransform::scale (s).translated (box.getX(), box.getY()));
        g.setColour (c);
        if (filled) g.fillPath (p);
        else        g.strokePath (p, PathStrokeType (strokeWidth, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

//==============================================================================
/** A rounded button showing an icon (and optional text). Toggle buttons light up in their own colour. */
class IconButton : public Button
{
public:
    IconButton (Icons::Type t, const String& tooltip = {}, Colour onColour = Theme::accent)
        : Button (tooltip), icon (t), on (onColour)
    {
        setTooltip (tooltip);
    }

    void setIcon (Icons::Type t)          { if (icon != t) { icon = t; repaint(); } }
    void setText (const String& t)        { text = t; repaint(); }
    void setOnColour (Colour c)           { on = c; repaint(); }
    void setIconColour (Colour c)         { iconColour = c; repaint(); }
    void setFlat (bool f)                 { flat = f; repaint(); }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const bool lit = getToggleState();

        if (! flat || over || lit)
        {
            Colour base = lit ? on.withMultipliedSaturation (0.85f).withAlpha (0.9f) : Theme::panel2;
            if (down) base = base.brighter (0.12f); else if (over) base = base.brighter (0.06f);
            g.setGradientFill (ColourGradient (base.brighter (0.05f), 0, r.getY(), base.darker (0.08f), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (lit ? on.brighter (0.2f).withAlpha (0.9f) : Theme::line.brighter (over ? 0.25f : 0.0f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }

        auto ic = iconColour.isTransparent() ? (lit ? (on.getPerceivedBrightness() > 0.55f ? Colours::black.withAlpha (0.85f) : Colours::white) : Theme::text)
                                             : iconColour;
        if (! isEnabled()) ic = ic.withAlpha (0.35f);

        auto content = r.reduced (5.0f, 4.0f);
        if (text.isEmpty())
        {
            Icons::draw (g, icon, content.withSizeKeepingCentre (jmin (content.getWidth(), 18.0f), jmin (content.getHeight(), 18.0f)), ic);
            return;
        }

        const float iconSize = jmin (content.getHeight(), 15.0f);
        g.setFont (FontOptions (13.0f));
        const float textW = GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const float total = iconSize + 5.0f + textW;
        auto row = content.withSizeKeepingCentre (jmin (total, content.getWidth()), content.getHeight());
        Icons::draw (g, icon, row.removeFromLeft (iconSize), ic, 1.6f);
        row.removeFromLeft (5.0f);
        g.setColour (ic);
        g.drawText (text, row, Justification::centredLeft, false);
    }

private:
    Icons::Type icon;
    Colour on, iconColour { Colours::transparentBlack };
    String text;
    bool flat = false;
};
