#pragma once

#include "Common.h"

/** Call-out panel for audio clip properties: gain, pitch, fades, reverse and warp-to-tempo. */
class AudioClipPanel : public Component
{
public:
    explicit AudioClipPanel (te::WaveAudioClip& c) : clip (&c)
    {
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
        setSize (340, 236);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10);
        title.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
        auto row = [&r] (Label& l, Component& c)
        {
            auto a = r.removeFromTop (26);
            l.setBounds (a.removeFromLeft (80));
            c.setBounds (a);
            r.removeFromTop (4);
        };
        row (gainL, gain);
        row (pitchL, pitch);
        row (fadeInL, fadeIn);
        row (fadeOutL, fadeOut);
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
    Label title, gainL, pitchL, fadeInL, fadeOutL, bpmL;
    Slider gain, pitch, fadeIn, fadeOut, bpm;
    ToggleButton reverse { "Reverse" }, warp { "Warp to tempo" };
};

inline void showAudioClipPanel (te::WaveAudioClip& clip, Component& target)
{
    CallOutBox::launchAsynchronously (std::make_unique<AudioClipPanel> (clip), target.getScreenBounds(), nullptr);
}
