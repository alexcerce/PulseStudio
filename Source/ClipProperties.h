#pragma once

#include "Common.h"
#include "ClipGain.h"
#include "Graphs.h"

/** The clip's audio with its gain envelope: click the line to add a point, drag points, double-click
    one to delete it. Edits are rendered shortly after you stop dragging. */
class GainEnvelopeEditor : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit GainEnvelopeEditor (te::WaveAudioClip& c) : clip (&c)
    {
        data = pulse::SampleData::load (c.edit.engine, ClipGain::sourceFile (c));
        pts = ClipGain::points (c);
        setTooltip ("Gain envelope: click the line to add a point, drag points, double-click a point to delete it");
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        Graphs::panel (g, b);
        area = b.reduced (6.0f, 6.0f);
        if (data != nullptr)
        {
            auto& ov = data->overview;
            g.setColour (clip->getTrack() != nullptr ? clip->getTrack()->getColour().withAlpha (0.55f) : Theme::accent.withAlpha (0.55f));
            const float mid = area.getCentreY(), h = area.getHeight() * 0.45f;
            for (int x = 0; x < (int) area.getWidth(); ++x)
            {
                const double t = timeAt (area.getX() + (float) x);
                const float gain = Decibels::decibelsToGain (ClipGain::dbAt (pts, t));
                auto [lo, hi] = ov[(size_t) jmin ((int) ov.size() - 1, x * (int) ov.size() / jmax (1, (int) area.getWidth()))];
                g.drawVerticalLine ((int) area.getX() + x, mid - jmin (1.0f, hi * gain) * h, mid - jmax (-1.0f, lo * gain) * h + 1.0f);
            }
        }
        // 0 dB guide
        g.setColour (Theme::ink.withAlpha (0.12f));
        g.drawHorizontalLine ((int) yFor (0.0f), area.getX(), area.getRight());
        // envelope
        Path p;
        if (pts.empty()) { p.startNewSubPath (area.getX(), yFor (0.0f)); p.lineTo (area.getRight(), yFor (0.0f)); }
        else
        {
            p.startNewSubPath (area.getX(), yFor (pts.front().db));
            for (auto& pt : pts) p.lineTo (xFor (pt.t), yFor (pt.db));
            p.lineTo (area.getRight(), yFor (pts.back().db));
        }
        g.setColour (Theme::yellow);
        g.strokePath (p, PathStrokeType (1.8f));
        for (int i = 0; i < (int) pts.size(); ++i)
            Graphs::node (g, { xFor (pts[(size_t) i].t), yFor (pts[(size_t) i].db) }, Theme::yellow, i == dragging || i == hot);
        if (dragging >= 0 || hot >= 0)
        {
            auto& pt = pts[(size_t) (dragging >= 0 ? dragging : hot)];
            g.setColour (Theme::text);
            g.setFont (FontOptions (10.5f, Font::bold));
            g.drawText (String (pt.db, 1) + " dB", area.withHeight (14.0f), Justification::topRight);
        }
        if (pending)
        {
            g.setColour (Theme::muted);
            g.setFont (FontOptions (10.0f));
            g.drawText ("updating...", area.withHeight (14.0f), Justification::topLeft);
        }
    }

    void mouseMove (const MouseEvent& e) override { const int h = hit (e.position); if (h != hot) { hot = h; repaint(); } }
    void mouseExit (const MouseEvent&) override   { hot = -1; repaint(); }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = hit (e.position);
        if (dragging < 0)   // add a point on the line
        {
            pts.push_back ({ timeAt (e.position.x), ClipGain::dbAt (pts, timeAt (e.position.x)) });
            std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.t < b.t; });
            dragging = hit (e.position);
        }
        clip->edit.getUndoManager().beginNewTransaction ("Clip gain envelope");
        mouseDrag (e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! isPositiveAndBelow (dragging, (int) pts.size())) return;
        auto& pt = pts[(size_t) dragging];
        const double lo = dragging > 0 ? pts[(size_t) dragging - 1].t + 0.001 : 0.0;
        const double hi = dragging + 1 < (int) pts.size() ? pts[(size_t) dragging + 1].t - 0.001 : length();
        pt.t = jlimit (lo, jmax (lo, hi), timeAt (e.position.x));
        pt.db = jlimit (-36.0f, 12.0f, dbFor (e.position.y));
        commitSoon();
    }

    void mouseUp (const MouseEvent&) override { dragging = -1; }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        const int h = hit (e.position);
        if (h < 0) return;
        clip->edit.getUndoManager().beginNewTransaction ("Delete gain point");
        pts.erase (pts.begin() + h);
        hot = dragging = -1;
        commitSoon();
    }

    /** Removes every point (the original file plays again). */
    void clear()
    {
        clip->edit.getUndoManager().beginNewTransaction ("Clear gain envelope");
        pts.clear();
        commitSoon();
    }

private:
    double length() const { return data != nullptr ? data->buffer.getNumSamples() / data->sampleRate : 1.0; }
    float xFor (double t) const   { return area.getX() + (float) (t / length()) * area.getWidth(); }
    double timeAt (float x) const { return jlimit (0.0, length(), (double) (x - area.getX()) / area.getWidth() * length()); }
    float yFor (float db) const   { return area.getY() + (12.0f - db) / 48.0f * area.getHeight(); }   // +12 .. -36 dB
    float dbFor (float y) const   { return 12.0f - (y - area.getY()) / area.getHeight() * 48.0f; }

    int hit (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) pts.size(); ++i)
            if (juce::Point<float> (xFor (pts[(size_t) i].t), yFor (pts[(size_t) i].db)).getDistanceFrom (p) < 8.0f) return i;
        return -1;
    }

    void commitSoon()
    {
        ClipGain::setPoints (*clip, pts, &clip->edit.getUndoManager());
        pending = true;
        repaint();
        startTimer (300);   // render once the dragging pauses
    }

    void timerCallback() override
    {
        stopTimer();
        ClipGain::apply (*clip);
        pending = false;
        repaint();
    }

    te::WaveAudioClip::Ptr clip;
    std::shared_ptr<pulse::SampleData> data;
    std::vector<ClipGain::Point> pts;
    Rectangle<float> area;
    int dragging = -1, hot = -1;
    bool pending = false;
};

/** Call-out panel for audio clip properties: gain envelope, gain, pitch, fades (with shapes),
    reverse and warp-to-tempo. */
class AudioClipPanel : public Component
{
public:
    explicit AudioClipPanel (te::WaveAudioClip& c) : clip (&c), envelope (c)
    {
        addAndMakeVisible (envelope);
        clearEnvBtn.onClick = [this] { envelope.clear(); };
        clearEnvBtn.setTooltip ("Remove the gain envelope (the original audio plays again)");
        addAndMakeVisible (clearEnvBtn);
        envLabel.setText ("GAIN ENVELOPE", dontSendNotification);
        envLabel.setFont (FontOptions (10.5f, Font::bold));
        envLabel.setColour (Label::textColourId, Theme::muted);
        addAndMakeVisible (envLabel);
        for (auto* box : { &fadeInShape, &fadeOutShape })
        {
            box->addItemList ({ "Linear", "Fast (convex)", "Slow (concave)", "S-curve" }, 1);
            box->setTooltip ("Fade shape");
            addAndMakeVisible (box);
        }
        fadeInShape.setSelectedId ((int) c.getFadeInType(), dontSendNotification);
        fadeOutShape.setSelectedId ((int) c.getFadeOutType(), dontSendNotification);
        fadeInShape.onChange = [this] { clip->setFadeInType ((te::AudioFadeCurve::Type) fadeInShape.getSelectedId()); };
        fadeOutShape.onChange = [this] { clip->setFadeOutType ((te::AudioFadeCurve::Type) fadeOutShape.getSelectedId()); };
        auto setupSlider = [this] (Slider& s, Label& l, const String& name, double lo, double hi, double step, const String& suffix)
        {
            s.setSliderStyle (Slider::LinearHorizontal);
            s.setTextBoxStyle (Slider::TextBoxRight, false, 70, 20);
            s.setRange (lo, hi, step);
            s.setTextValueSuffix (suffix);
            l.setText (name, dontSendNotification);
            l.setColour (Label::textColourId, Theme::muted);
            addAndMakeVisible (s);
            addAndMakeVisible (l);
        };

        setupSlider (gain, gainL, "Gain", -36.0, 12.0, 0.1, " dB");
        setupSlider (pitch, pitchL, "Pitch", -24.0, 24.0, 0.1, " st");
        setupSlider (fadeIn, fadeInL, "Fade in", 0.0, 10000.0, 1.0, " ms");
        setupSlider (fadeOut, fadeOutL, "Fade out", 0.0, 10000.0, 1.0, " ms");
        setupSlider (bpm, bpmL, "Source BPM", 40.0, 240.0, 0.01, "");
        fadeIn.setSkewFactorFromMidPoint (500.0);
        fadeOut.setSkewFactorFromMidPoint (500.0);
        gain.setDoubleClickReturnValue (true, 0.0);
        pitch.setDoubleClickReturnValue (true, 0.0);

        gain.setValue (clip->getGainDB(), dontSendNotification);
        pitch.setValue (clip->getPitchChange(), dontSendNotification);
        fadeIn.setValue (clip->getFadeIn().inSeconds() * 1000.0, dontSendNotification);
        fadeOut.setValue (clip->getFadeOut().inSeconds() * 1000.0, dontSendNotification);

        auto info = clip->getAudioFile().getInfo();
        auto srcBpm = clip->getLoopInfo().getBpm (info);
        bpm.setValue (srcBpm > 0 ? srcBpm : clip->edit.tempoSequence.getTempo (0)->getBpm(), dontSendNotification);

        gain.onValueChange    = [this] { clip->setGainDB ((float) gain.getValue()); };
        pitch.onValueChange   = [this] { clip->setPitchChange ((float) pitch.getValue()); };
        fadeIn.onValueChange  = [this] { clip->setFadeIn (te::TimeDuration::fromSeconds (fadeIn.getValue() / 1000.0)); };
        fadeOut.onValueChange = [this] { clip->setFadeOut (te::TimeDuration::fromSeconds (fadeOut.getValue() / 1000.0)); };
        bpm.onValueChange     = [this] { applySourceBpm(); };

        reverse.setToggleState (clip->getIsReversed(), dontSendNotification);
        reverse.onClick = [this] { clip->setIsReversed (reverse.getToggleState()); };
        warp.setToggleState (clip->getAutoTempo(), dontSendNotification);
        warp.setTooltip ("Time-stretch the clip so it follows the project tempo");
        warp.onClick = [this]
        {
            if (warp.getToggleState())
                applySourceBpm();
            clip->setAutoTempo (warp.getToggleState());
            updateEnablement();
        };
        addAndMakeVisible (reverse);
        addAndMakeVisible (warp);

        title.setText (clip->getName(), dontSendNotification);
        title.setFont (FontOptions (14.5f, Font::bold));
        addAndMakeVisible (title);

        updateEnablement();
        setSize (440, 380);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto envHead = r.removeFromTop (18);
        clearEnvBtn.setBounds (envHead.removeFromRight (60));
        envLabel.setBounds (envHead);
        envelope.setBounds (r.removeFromTop (110));
        r.removeFromTop (8);
        auto row = [&r] (Label& l, Component& c, Component* extra = nullptr)
        {
            auto a = r.removeFromTop (26);
            l.setBounds (a.removeFromLeft (80));
            if (extra != nullptr) { extra->setBounds (a.removeFromRight (110)); a.removeFromRight (6); }
            c.setBounds (a);
            r.removeFromTop (4);
        };
        row (gainL, gain);
        row (pitchL, pitch);
        row (fadeInL, fadeIn, &fadeInShape);
        row (fadeOutL, fadeOut, &fadeOutShape);
        auto toggles = r.removeFromTop (24);
        reverse.setBounds (toggles.removeFromLeft (110));
        warp.setBounds (toggles);
        r.removeFromTop (4);
        row (bpmL, bpm);
    }

private:
    void applySourceBpm()
    {
        auto info = clip->getAudioFile().getInfo();
        auto li = clip->getLoopInfo();
        li.setBpm (bpm.getValue(), info);
        clip->setLoopInfo (li);
    }

    void updateEnablement()
    {
        bpm.setEnabled (warp.getToggleState());
        bpmL.setEnabled (warp.getToggleState());
    }

    te::WaveAudioClip::Ptr clip;
    GainEnvelopeEditor envelope;
    TextButton clearEnvBtn { "Clear" };
    Label envLabel;
    ComboBox fadeInShape, fadeOutShape;
    Label title, gainL, pitchL, fadeInL, fadeOutL, bpmL;
    Slider gain, pitch, fadeIn, fadeOut, bpm;
    ToggleButton reverse { "Reverse" }, warp { "Warp to tempo" };
};

inline void showAudioClipPanel (te::WaveAudioClip& clip, Component& target)
{
    CallOutBox::launchAsynchronously (std::make_unique<AudioClipPanel> (clip), target.getScreenBounds(), nullptr);
}
