#pragma once

#include "Common.h"

/** The first minutes with the app: is there sound, and where is everything.

    AudioCheck says in one line which sound device is in use and whether anything about it will get in the way.
    Tour walks through the parts of the window, one at a time, with the part lit and a few words beside it. */
namespace AudioCheck
{
    struct Status
    {
        bool ok = false;
        bool warning = false;   // it works, but something is worth knowing
        String line, advice;
    };

    /** The check itself, from the plain facts of a device (so it can be tested without one). */
    inline Status describe (const String& deviceName, double sampleRate, int bufferSamples, int outputChannels, int outputLatencySamples)
    {
        Status s;
        if (deviceName.isEmpty() || sampleRate <= 0.0)
        {
            s.line = "No sound device is open";
            s.advice = "Nothing will be heard. Open the audio settings and choose where the sound should go.";
            return s;
        }
        if (outputChannels < 1)
        {
            s.line = deviceName + ": no output is switched on";
            s.advice = "Nothing will be heard. Open the audio settings and switch on the outputs you listen through.";
            return s;
        }
        const double ms = (bufferSamples + jmax (0, outputLatencySamples)) * 1000.0 / sampleRate;
        s.ok = true;
        s.line = deviceName + "   " + String (sampleRate / 1000.0, sampleRate == std::floor (sampleRate / 1000.0) * 1000.0 ? 0 : 1) + " kHz   " + String (roundToInt (ms)) + " ms";
        if (ms > 30.0)
        {
            s.warning = true;
            s.advice = "Sound comes out " + String (roundToInt (ms)) + " ms late: fine for mixing, but an instrument played live will feel behind. A smaller buffer (or an ASIO driver) in the audio settings brings it down.";
        }
        else if (outputChannels < 2)
        {
            s.warning = true;
            s.advice = "Only one output is on, so everything is heard in mono.";
        }
        return s;
    }

    inline Status status (AudioDeviceManager& dm)
    {
        if (auto* d = dm.getCurrentAudioDevice())
            return describe (d->getName(), d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples(), d->getActiveOutputChannels().countNumberOfSetBits(), d->getOutputLatencyInSamples());
        return describe ({}, 0.0, 0, 0, 0);
    }
}

//==============================================================================
class Tour : public Component
{
public:
    struct Step
    {
        String title, text;
        std::function<Rectangle<int>()> where;   // the part of the window it is about, in the tour's own coordinates
    };

    explicit Tour (std::vector<Step> s) : steps (std::move (s))
    {
        setWantsKeyboardFocus (true);
        for (auto* b : { &backBtn, &nextBtn, &closeBtn }) addAndMakeVisible (b);
        backBtn.onClick = [this] { go (index - 1); };
        nextBtn.onClick = [this] { if (index + 1 < (int) steps.size()) go (index + 1); else close(); };
        closeBtn.onClick = [this] { close(); };
        nextBtn.setColour (TextButton::buttonColourId, Theme::accentDim);
        go (0);
    }

    int numSteps() const      { return (int) steps.size(); }
    int currentStep() const   { return index; }
    String currentTitle() const { return isPositiveAndBelow (index, (int) steps.size()) ? steps[(size_t) index].title : String(); }
    Rectangle<int> litArea() const { return lit; }
    Rectangle<int> bubbleArea() const { return bubble; }
    /** Called when the tour is over (it removes itself). */
    std::function<void()> onClose;

    void go (int i)
    {
        index = jlimit (0, jmax (0, (int) steps.size() - 1), i);
        layout();
        repaint();
    }

    void close()
    {
        setVisible (false);
        if (onClose) onClose();
        MessageManager::callAsync ([sp = SafePointer<Component> (this)] { if (sp != nullptr) delete sp.getComponent(); });
    }

    void resized() override { layout(); }

    void paint (Graphics& g) override
    {
        // everything dimmed but the part being talked about
        Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        dim.setUsingNonZeroWinding (false);
        dim.addRoundedRectangle (lit.toFloat(), 6.0f);
        g.setColour (Colours::black.withAlpha (0.62f));
        g.fillPath (dim);
        g.setColour (Theme::accent);
        g.drawRoundedRectangle (lit.toFloat(), 6.0f, 2.0f);

        auto b = bubble.toFloat();
        g.setColour (Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (b.translated (0.0f, 3.0f), 10.0f);
        g.setGradientFill (ColourGradient (Theme::raised, b.getX(), b.getY(), Theme::bg2, b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (b, 10.0f, 1.0f);
        if (! isPositiveAndBelow (index, (int) steps.size())) return;
        auto inner = bubble.reduced (18, 14);
        g.setColour (Theme::muted);
        g.setFont (FontOptions (11.0f, Font::bold));
        g.drawText (String (index + 1) + " OF " + String ((int) steps.size()), inner.removeFromTop (14), Justification::centredLeft);
        g.setColour (Theme::text);
        g.setFont (FontOptions (17.0f, Font::bold));
        g.drawText (steps[(size_t) index].title, inner.removeFromTop (26), Justification::centredLeft, true);
        inner.removeFromTop (4);
        inner.removeFromBottom (38);
        g.setColour (Theme::text.withAlpha (0.88f));
        g.setFont (FontOptions (13.5f));
        g.drawFittedText (steps[(size_t) index].text, inner, Justification::topLeft, 6);
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (k == KeyPress::escapeKey) { close(); return true; }
        if (k == KeyPress::rightKey || k == KeyPress::returnKey || k == KeyPress::spaceKey) { nextBtn.onClick(); return true; }
        if (k == KeyPress::leftKey) { backBtn.onClick(); return true; }
        return true;   // (nothing else gets through while the tour is up)
    }

    void mouseDown (const MouseEvent& e) override { if (! bubble.contains (e.getPosition())) nextBtn.onClick(); }

private:
    void layout()
    {
        if (! isPositiveAndBelow (index, (int) steps.size()) || getWidth() < 10) return;
        lit = steps[(size_t) index].where ? steps[(size_t) index].where().getIntersection (getLocalBounds()).reduced (2) : Rectangle<int>();
        if (lit.getWidth() < 8 || lit.getHeight() < 8) lit = getLocalBounds().withSizeKeepingCentre (8, 8);   // (a part that is not showing: the bubble alone)
        // the bubble goes where there is the most room beside the lit part, and stays inside the window
        const int w = jmin (420, getWidth() - 24), h = 176;
        const int below = getHeight() - lit.getBottom(), above = lit.getY(), right = getWidth() - lit.getRight(), left = lit.getX();
        Rectangle<int> b (0, 0, w, h);
        if (below >= h + 16)       b.setPosition (lit.getCentreX() - w / 2, lit.getBottom() + 12);
        else if (above >= h + 16)  b.setPosition (lit.getCentreX() - w / 2, lit.getY() - h - 12);
        else if (right >= w + 16)  b.setPosition (lit.getRight() + 12, lit.getCentreY() - h / 2);
        else if (left >= w + 16)   b.setPosition (lit.getX() - w - 12, lit.getCentreY() - h / 2);
        else                       b.setPosition (lit.getCentreX() - w / 2, lit.getCentreY() - h / 2);   // (a part as big as the window: in the middle of it)
        bubble = b.constrainedWithin (getLocalBounds().reduced (8));
        auto row = bubble.reduced (18, 14).removeFromBottom (28);
        nextBtn.setButtonText (index + 1 < (int) steps.size() ? "Next" : "Done");
        nextBtn.setBounds (row.removeFromRight (84));
        row.removeFromRight (6);
        backBtn.setBounds (row.removeFromRight (70));
        backBtn.setEnabled (index > 0);
        closeBtn.setBounds (row.removeFromLeft (96));
    }

    std::vector<Step> steps;
    int index = 0;
    Rectangle<int> lit, bubble;
    TextButton backBtn { "Back" }, nextBtn { "Next" }, closeBtn { "End the tour" };
};
