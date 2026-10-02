#pragma once

#include "Common.h"

/** Level meters used everywhere (track headers, session columns, mixer, top bar) and the track
    colour palette. */
namespace MeterStyle
{
    /** Meter position 0..1 for a level in dB (-60 .. +6, with more room at the top). */
    inline float norm (float db)
    {
        if (db <= -60.0f) return 0.0f;
        if (db >= 6.0f) return 1.0f;
        // piecewise: -60..-24 takes 25 %, -24..0 takes 66 %, 0..+6 the rest
        if (db < -24.0f) return (db + 60.0f) / 36.0f * 0.25f;
        if (db < 0.0f)   return 0.25f + (db + 24.0f) / 24.0f * 0.66f;
        return 0.91f + db / 6.0f * 0.09f;
    }

    inline ColourGradient gradient (Rectangle<float> r, bool vertical)
    {
        ColourGradient g (Colour (0xff1f7a3a), vertical ? 0.0f : r.getX(), vertical ? r.getBottom() : 0.0f,
                          Colour (0xffff4d4d), vertical ? 0.0f : r.getRight(), vertical ? r.getY() : 0.0f, false);
        g.addColour (norm (-18.0f), Colour (0xff40c463));
        g.addColour (norm (-6.0f), Colour (0xffb8e05a));
        g.addColour (norm (-2.0f), Colour (0xffffd43b));
        g.addColour (norm (0.0f), Colour (0xffff922b));
        return g;
    }
}

//==============================================================================
/** Stereo peak meter fed by a LevelMeterPlugin's measurer: smooth fall-off, peak hold, and a clip
    light that stays on until clicked. Vertical or horizontal. */
class LevelMeter : public Component,
                   public SettableTooltipClient,
                   private Timer
{
public:
    explicit LevelMeter (te::LevelMeterPlugin& p, bool isVertical = true) : holder (&p), plugin (p), vertical (isVertical)
    {
        plugin.measurer.addClient (client);
        setTooltip ("Level (the light at the end turns red after clipping; click to reset)");
        startTimerHz (30);
    }

    ~LevelMeter() override { plugin.measurer.removeClient (client); }

    /** Highest peak since the last reset (dB). */
    float getMaxPeak() const { return maxPeak; }
    void resetPeak() { maxPeak = -100.0f; clipped = false; repaint(); }
    float getLevelDb (int ch) const { return level[jlimit (0, 1, ch)]; }


    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float ledSize = vertical ? jmin (4.0f, r.getHeight() * 0.06f) : jmin (4.0f, r.getWidth() * 0.06f);
        auto led = vertical ? r.removeFromTop (ledSize) : r.removeFromRight (ledSize);
        if (vertical) r.removeFromTop (1.0f); else r.removeFromRight (1.0f);
        g.setColour (clipped ? Colour (0xffff4d4d) : Colour (0xff2a2e35));
        g.fillRect (led);

        if (vertical)
        {
            const float w = (r.getWidth() - 1.0f) / 2.0f;
            drawBar (g, r.withWidth (w), level[0], peak[0]);
            drawBar (g, r.withTrimmedLeft (w + 1.0f), level[1], peak[1]);
        }
        else
        {
            const float h = (r.getHeight() - 1.0f) / 2.0f;
            drawBar (g, r.withHeight (h), level[0], peak[0]);
            drawBar (g, r.withTrimmedTop (h + 1.0f), level[1], peak[1]);
        }
    }

    void mouseDown (const MouseEvent&) override { resetPeak(); }

    /** Reads the measurer now (the timer does this 30 times a second). */
    void poll() { timerCallback(); }

private:
    void drawBar (Graphics& g, Rectangle<float> r, float db, float pk)
    {
        g.setColour (Colour (0xff0b0d10));
        g.fillRect (r);
        const float n = MeterStyle::norm (db);
        if (n > 0.0f)
        {
            g.setGradientFill (MeterStyle::gradient (r, vertical));
            g.fillRect (vertical ? r.withTrimmedTop (r.getHeight() * (1.0f - n)) : r.withWidth (r.getWidth() * n));
        }
        const float pn = MeterStyle::norm (pk);
        if (pn > 0.0f)
        {
            g.setColour (pk > 0.0f ? Colour (0xffff4d4d) : Colours::white.withAlpha (0.8f));
            if (vertical) g.fillRect (r.getX(), r.getY() + r.getHeight() * (1.0f - pn), r.getWidth(), 1.5f);
            else          g.fillRect (r.getX() + r.getWidth() * pn - 1.5f, r.getY(), 1.5f, r.getHeight());
        }
    }

    void timerCallback() override
    {
        const auto now = Time::getMillisecondCounter();
        const float dt = lastTick == 0 ? 0.033f : (float) (now - lastTick) * 0.001f;
        lastTick = now;
        bool changed = false;
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto db = client.getAndClearAudioLevel (jmin (ch, jmax (0, client.getNumChannelsUsed() - 1))).dB;
            const float before = level[ch];
            level[ch] = jmax (db, level[ch] - 26.0f * dt);   // ~26 dB/s fall-off
            if (db >= peak[ch] || now - peakTime[ch] > 1500) { peak[ch] = db; peakTime[ch] = now; }
            maxPeak = jmax (maxPeak, db);
            if (db > 0.0f) clipped = true;
            changed = changed || std::abs (before - level[ch]) > 0.05f;
        }
        if (changed || level[0] > -90.0f || level[1] > -90.0f) repaint();
    }

    te::Plugin::Ptr holder;          // keeps the plugin (and its measurer) alive
    te::LevelMeterPlugin& plugin;
    te::LevelMeasurer::Client client;
    const bool vertical;
    float level[2] { -100.0f, -100.0f }, peak[2] { -100.0f, -100.0f }, maxPeak = -100.0f;
    uint32 peakTime[2] {}, lastTick = 0;
    bool clipped = false;
};

//==============================================================================
/** dB tick labels that line up with LevelMeter / fader positions (vertical). */
class DbScale : public Component
{
public:
    /** toY maps a dB value to a y position in this component (so it can follow a fader's skew). */
    explicit DbScale (std::function<float (float)> yForDb = {}) : yFor (std::move (yForDb)) { setInterceptsMouseClicks (false, false); }

    void paint (Graphics& g) override
    {
        g.setFont (FontOptions (9.5f));
        for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = yFor ? yFor (db) : (float) getHeight() * (1.0f - MeterStyle::norm (db));
            g.setColour (db == 0.0f ? Theme::text.withAlpha (0.8f) : Theme::muted.withAlpha (0.7f));
            g.fillRect ((float) getWidth() - 3.0f, y, 3.0f, 1.0f);
            g.drawText (db > 0 ? "+" + String ((int) db) : String ((int) db), Rectangle<float> (0.0f, y - 6.0f, (float) getWidth() - 4.0f, 12.0f),
                        Justification::centredRight);
        }
    }

private:
    std::function<float (float)> yFor;
};

//==============================================================================
/** Track colours: 32 swatches in a grid (opens in a call-out). */
namespace TrackColours
{
    inline const std::array<uint32, 32>& palette()
    {
        static const std::array<uint32, 32> p {
            0xffff6b6b, 0xffff8787, 0xffffa94d, 0xffffc078, 0xffffd43b, 0xffd8f5a2, 0xff94d82d, 0xff69db7c,
            0xff38d9a9, 0xff3bc9db, 0xff66d9e8, 0xff4dabf7, 0xff74c0fc, 0xff748ffc, 0xff9775fa, 0xffb197fc,
            0xffda77f2, 0xfff783ac, 0xffe64980, 0xffc92a2a, 0xffe8590c, 0xfff08c00, 0xff5c940d, 0xff2b8a3e,
            0xff0b7285, 0xff1864ab, 0xff364fc7, 0xff5f3dc4, 0xff862e9c, 0xffa61e4d, 0xff868e96, 0xffced4da };
        return p;
    }

    class Grid : public Component
    {
    public:
        Grid (Colour current, std::function<void (Colour)> pick) : selected (current), onPick (std::move (pick)) { setSize (8 * 26 + 8, 4 * 26 + 8); }

        void paint (Graphics& g) override
        {
            g.fillAll (Theme::panel);
            for (int i = 0; i < 32; ++i)
            {
                auto r = cell (i).toFloat().reduced (2.0f);
                const Colour c (palette()[(size_t) i]);
                g.setColour (c);
                g.fillRoundedRectangle (r, 3.0f);
                if (c == selected || i == hover)
                {
                    g.setColour (Colours::white);
                    g.drawRoundedRectangle (r.expanded (1.0f), 3.0f, c == selected ? 2.0f : 1.0f);
                }
            }
        }

        void mouseMove (const MouseEvent& e) override { hover = indexAt (e.getPosition()); repaint(); }
        void mouseExit (const MouseEvent&) override   { hover = -1; repaint(); }
        void mouseUp (const MouseEvent& e) override
        {
            const int i = indexAt (e.getPosition());
            if (i < 0) return;
            if (onPick) onPick (Colour (palette()[(size_t) i]));
            if (auto* box = findParentComponentOfClass<CallOutBox>()) box->dismiss();
        }

    private:
        Rectangle<int> cell (int i) const { return { 4 + (i % 8) * 26, 4 + (i / 8) * 26, 26, 26 }; }
        int indexAt (juce::Point<int> p) const
        {
            for (int i = 0; i < 32; ++i) if (cell (i).contains (p)) return i;
            return -1;
        }
        Colour selected;
        std::function<void (Colour)> onPick;
        int hover = -1;
    };

    inline void show (Component& target, Colour current, std::function<void (Colour)> onPick)
    {
        CallOutBox::launchAsynchronously (std::make_unique<Grid> (current, std::move (onPick)), target.getScreenBounds(), nullptr);
    }

    /** Text colour that reads well on top of a track colour. */
    inline Colour textOn (Colour c) { return c.getPerceivedBrightness() > 0.62f ? Colour (0xff15171b) : Colours::white; }
}
