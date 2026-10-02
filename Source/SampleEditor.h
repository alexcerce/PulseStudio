#pragma once

#include "SampleEdit.h"
#include "Icons.h"

/** Sample editor window for an audio clip: zoomable waveform, range selection, cut / silence /
    normalise / fades / reverse / gain with its own undo, then Apply writes a new file
    (the original audio file is left alone) and the clip plays it. */
class SampleEditor : public Component
{
public:
    explicit SampleEditor (te::WaveAudioClip& c) : clip (&c), engine (&c.edit.engine), editPtr (&c.edit), clipID (c.itemID)
    {
        loaded = SampleEdit::readFile (c.edit.engine, c.getOriginalFile(), audio, sampleRate);
        viewEnd = audio.getNumSamples();

        auto add = [this] (TextButton& b, const String& tip, std::function<void()> fn)
        {
            b.setTooltip (tip);
            b.onClick = [this, fn] { fn(); };
            addAndMakeVisible (b);
        };
        add (cutBtn, "Remove the selection and close the gap", [this] { edit ([this] (auto& b, auto r) { SampleEdit::cut (b, r); }, true); });
        add (silenceBtn, "Silence the selection", [this] { edit ([] (auto& b, auto r) { SampleEdit::silence (b, r); }); });
        add (normaliseBtn, "Normalise the selection (or everything) to -0.3 dB", [this] { edit ([] (auto& b, auto r) { SampleEdit::normalise (b, r); }); });
        add (fadeInBtn, "Fade the selection in", [this] { edit ([] (auto& b, auto r) { SampleEdit::fade (b, r, true); }); });
        add (fadeOutBtn, "Fade the selection out", [this] { edit ([] (auto& b, auto r) { SampleEdit::fade (b, r, false); }); });
        add (reverseBtn, "Reverse the selection", [this] { edit ([] (auto& b, auto r) { SampleEdit::reverse (b, r); }); });
        add (gainDownBtn, "Selection -3 dB", [this] { edit ([] (auto& b, auto r) { SampleEdit::gain (b, r, -3.0f); }); });
        add (gainUpBtn, "Selection +3 dB", [this] { edit ([] (auto& b, auto r) { SampleEdit::gain (b, r, 3.0f); }); });
        add (undoBtn, "Undo the last edit in this window", [this] { undo(); });
        add (applyBtn, "Save the edits as a new file and use it in the clip", [this] { apply(); });
        add (cancelBtn, "Close without changing the clip", [this] { close(); });
        applyBtn.setColour (TextButton::buttonColourId, Theme::accent.darker (0.3f));

        info.setColour (Label::textColourId, Theme::muted);
        info.setFont (FontOptions (12.5f));
        addAndMakeVisible (info);
        setWantsKeyboardFocus (true);
        updateInfo();
        setSize (1000, 460);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        auto r = waveArea().toFloat();
        g.setColour (Theme::well);
        g.fillRoundedRectangle (r, 6.0f);
        if (! loaded || audio.getNumSamples() == 0)
        {
            g.setColour (Theme::muted);
            g.drawText ("Couldn't read the audio file.", r, Justification::centred);
            return;
        }

        // selection
        if (! selection.isEmpty())
        {
            const float x0 = sampleToX (selection.getStart()), x1 = sampleToX (selection.getEnd());
            g.setColour (Theme::accent.withAlpha (0.2f));
            g.fillRect (Rectangle<float> (x0, r.getY(), jmax (1.0f, x1 - x0), r.getHeight()));
        }

        // waveform: min/max per pixel column, one lane per channel
        const int chans = audio.getNumChannels();
        const float laneH = r.getHeight() / (float) chans;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float mid = r.getY() + laneH * ((float) ch + 0.5f);
            g.setColour (Theme::ink.withAlpha (0.06f));
            g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
            g.setColour (Theme::accent.brighter (0.2f));
            const auto* d = audio.getReadPointer (ch);
            for (int x = 0; x < (int) r.getWidth(); ++x)
            {
                const int s0 = xToSample (r.getX() + (float) x), s1 = jmax (s0 + 1, xToSample (r.getX() + (float) x + 1.0f));
                float lo = 0, hi = 0;
                for (int i = s0; i < s1 && i < audio.getNumSamples(); i += jmax (1, (s1 - s0) / 64))
                {
                    lo = jmin (lo, d[i]);
                    hi = jmax (hi, d[i]);
                }
                g.drawVerticalLine ((int) r.getX() + x, mid - hi * laneH * 0.48f, mid - lo * laneH * 0.48f + 1.0f);
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        auto bar = r.removeFromBottom (34);
        for (auto* b : { &cutBtn, &silenceBtn, &normaliseBtn, &fadeInBtn, &fadeOutBtn, &reverseBtn, &gainDownBtn, &gainUpBtn, &undoBtn })
        {
            b->setBounds (bar.removeFromLeft (b == &normaliseBtn ? 84 : 66).reduced (2, 2));
        }
        cancelBtn.setBounds (bar.removeFromRight (80).reduced (2, 2));
        applyBtn.setBounds (bar.removeFromRight (90).reduced (2, 2));
        r.removeFromBottom (6);
        info.setBounds (r.removeFromBottom (20));
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! waveArea().contains (e.getPosition())) return;
        dragStart = xToSample ((float) e.x);
        selection = { dragStart, dragStart };
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! waveArea().expanded (40, 400).contains (e.getPosition())) return;
        const int s = xToSample ((float) e.x);
        selection = { jmin (dragStart, s), jmax (dragStart, s) };
        updateInfo();
        repaint();
    }

    void mouseDoubleClick (const MouseEvent&) override { selectAll(); }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        // wheel zooms around the mouse; shift+wheel scrolls
        const int len = viewEnd - viewStart;
        if (e.mods.isShiftDown())
        {
            const int shift = (int) (-w.deltaY * len * 0.5);
            const int n = jlimit (0, jmax (0, audio.getNumSamples() - len), viewStart + shift);
            viewStart = n; viewEnd = n + len;
        }
        else
        {
            const int anchor = xToSample ((float) e.x);
            const double factor = w.deltaY > 0 ? 0.7 : 1.0 / 0.7;
            const int newLen = jlimit (64, audio.getNumSamples(), (int) (len * factor));
            const double rel = (double) (anchor - viewStart) / jmax (1, len);
            viewStart = jlimit (0, audio.getNumSamples() - newLen, anchor - (int) (rel * newLen));
            viewEnd = viewStart + newLen;
        }
        repaint();
    }

    bool keyPressed (const KeyPress& k) override
    {
        if (isCtrlKey (k, 'a')) { selectAll(); return true; }
        if (isCtrlKey (k, 'z')) { undo(); return true; }
        if (k == KeyPress::deleteKey) { cutBtn.triggerClick(); return true; }
        if (k == KeyPress::escapeKey) { close(); return true; }
        return false;
    }

    /** For the self-test: edits run through the same path as the buttons. */
    AudioBuffer<float>& getAudio() { return audio; }
    void setSelection (Range<int> r) { selection = r; }
    void runEdit (std::function<void (AudioBuffer<float>&, Range<int>)> fn, bool cut = false) { edit (std::move (fn), cut); }
    bool apply()
    {
        // the project may have been closed (or the clip deleted) while this window was open
        if (! engine->getActiveEdits().getEdits().contains (editPtr) || te::findClipForID (*editPtr, clipID) != clip)
        {
            close();
            return false;
        }
        const bool ok = clip != nullptr && SampleEdit::applyToClip (*clip, audio, sampleRate);
        if (ok) close();
        else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Sample editor", "Couldn't save the edited audio.");
        return ok;
    }

private:
    Rectangle<int> waveArea() const { return getLocalBounds().reduced (12).withTrimmedBottom (66); }
    float sampleToX (int s) const
    {
        auto r = waveArea().toFloat();
        return r.getX() + (float) (s - viewStart) / (float) jmax (1, viewEnd - viewStart) * r.getWidth();
    }
    int xToSample (float x) const
    {
        auto r = waveArea().toFloat();
        return jlimit (0, audio.getNumSamples(), viewStart + (int) ((x - r.getX()) / r.getWidth() * (float) (viewEnd - viewStart)));
    }

    void selectAll() { selection = { 0, audio.getNumSamples() }; updateInfo(); repaint(); }

    /** Runs an edit on the selection (or everything when nothing is selected), keeping undo. */
    void edit (std::function<void (AudioBuffer<float>&, Range<int>)> fn, bool isCut = false)
    {
        auto r = selection.isEmpty() ? (isCut ? Range<int>() : Range<int> (0, audio.getNumSamples())) : selection;
        if (r.isEmpty()) return;
        history.push_back (audio);
        if (history.size() > 20) history.erase (history.begin());
        fn (audio, r);
        if (isCut) selection = { r.getStart(), r.getStart() };
        viewEnd = jmin (viewEnd, audio.getNumSamples());
        viewStart = jmin (viewStart, jmax (0, viewEnd - 64));
        updateInfo();
        repaint();
    }

    void undo()
    {
        if (history.empty()) return;
        audio = std::move (history.back());
        history.pop_back();
        viewEnd = jlimit (64, audio.getNumSamples(), viewEnd);
        updateInfo();
        repaint();
    }

    void updateInfo()
    {
        auto secs = [this] (int s) { return String ((double) s / sampleRate, 3) + " s"; };
        String t;
        t << "Length " << secs (audio.getNumSamples());
        if (! selection.isEmpty()) t << "   |   selection " << secs (selection.getStart()) << " - " << secs (selection.getEnd())
                                    << " (" << secs (selection.getLength()) << ")";
        else t << "   |   no selection: edits apply to the whole file (drag to select, double-click = all)";
        t << "   |   wheel = zoom, Shift+wheel = scroll";
        info.setText (t, dontSendNotification);
        undoBtn.setEnabled (! history.empty());
    }

    void close()
    {
        if (auto* dw = findParentComponentOfClass<DialogWindow>()) dw->exitModalState (0);
        if (auto* dw = findParentComponentOfClass<DocumentWindow>())
        {
            dw->setVisible (false);
            MessageManager::callAsync ([sp = SafePointer<Component> (dw)] { delete sp.getComponent(); });
        }
    }

    te::WaveAudioClip* clip;
    te::Engine* engine;
    te::Edit* editPtr;
    te::EditItemID clipID;
    AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    bool loaded = false;
    int viewStart = 0, viewEnd = 0, dragStart = 0;
    Range<int> selection;
    std::vector<AudioBuffer<float>> history;
    Label info;
    TextButton cutBtn { "Cut" }, silenceBtn { "Silence" }, normaliseBtn { "Normalise" }, fadeInBtn { "Fade in" }, fadeOutBtn { "Fade out" },
               reverseBtn { "Reverse" }, gainDownBtn { "-3 dB" }, gainUpBtn { "+3 dB" }, undoBtn { "Undo" }, applyBtn { "Apply" }, cancelBtn { "Cancel" };
};

inline void showSampleEditor (te::WaveAudioClip& clip)
{
    struct Window : public DocumentWindow
    {
        explicit Window (te::WaveAudioClip& c) : DocumentWindow ("Sample editor - " + c.getName(), Theme::panel, DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new SampleEditor (c), true);
            setResizable (true, false);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }
    };
    new Window (clip);   // deletes itself when closed
}
