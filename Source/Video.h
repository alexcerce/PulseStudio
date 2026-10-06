#pragma once

#include "EditViewState.h"
#include "About.h"

// Windows' own media player, in VideoPlayerWin.cpp (it needs the Windows headers, which stay out of these).
namespace VideoPlayerWin
{
    void* create();
    void destroy (void* player);
    void* windowOf (void* player);
    bool open (void* player, const String& path);
    void close (void* player);
    String error (void* player);
    bool isReady (void* player);
    double duration (void* player);
    double position (void* player);
    void setPosition (void* player, double seconds);
    void play (void* player);
    void pause (void* player);
    bool isPlaying (void* player);
    void setVolume (void* player, float volume);
    void pump (int milliseconds);
    String describe (void* player);
    bool frame (void* player, Image& into);
    String probe (const String& url);
}

/** Video: a film to write music to. One video file belongs to the project, with the place on the timeline where
    it begins. Its window shows the frame for wherever the song is: it plays when the song plays, stops when it
    stops, and jumps when you jump, so the music can be put against the picture.

    The picture is played by Windows' own media player (Media Foundation: MP4, MOV, WMV, AVI and whatever else
    Windows can play), which keeps its own time. The window keeps comparing where the picture is with where the
    song is and puts it right when the two are more than a few frames apart. */
namespace Video
{
    inline const Identifier videoID { "PULSEVIDEO" }, fileID { "file" }, startID { "start" }, soundID { "sound" };

    inline ValueTree node (te::Edit& e)                 { return e.state.getChildWithName (videoID); }
    inline bool has (te::Edit& e)                       { return node (e)[fileID].toString().isNotEmpty(); }
    inline File file (te::Edit& e)                      { const auto p = node (e)[fileID].toString(); return p.isEmpty() ? File() : File (p); }
    /** Where on the timeline the video begins, in seconds. */
    inline double start (te::Edit& e)                   { return (double) node (e).getProperty (startID, 0.0); }
    /** Whether the video's own sound is heard (it does not go through the mixer). */
    inline bool sound (te::Edit& e)                     { return (bool) node (e).getProperty (soundID, false); }

    inline void setFile (te::Edit& e, const File& f)
    {
        auto* um = &e.getUndoManager();
        if (f == File()) { e.state.removeChild (node (e), um); return; }
        e.state.getOrCreateChildWithName (videoID, um).setProperty (fileID, f.getFullPathName(), um);
    }
    inline void setStart (te::Edit& e, double seconds)  { if (has (e)) node (e).setProperty (startID, jmax (0.0, seconds), &e.getUndoManager()); }
    inline void setSound (te::Edit& e, bool on)         { if (has (e)) node (e).setProperty (soundID, on, &e.getUndoManager()); }

    /** Where in the video the song's position falls (under 0: the video has not begun yet). */
    inline double videoTimeAt (double songSeconds, double videoStart) { return songSeconds - videoStart; }

    /** True when the song is at a place the video covers. */
    inline bool covers (double videoTime, double duration) { return videoTime >= 0.0 && (duration <= 0.0 || videoTime < duration); }

    /** True when the picture is far enough from where it should be to be worth a jump: three frames or so while it
        plays (a jump is seen, so small differences are left alone), under a frame while it stands still. */
    inline bool needsJump (double shouldBe, double is, bool playing) { return std::abs (shouldBe - is) > (playing ? 0.12 : 0.03); }

    inline bool isVideoFile (const File& f) { return f.hasFileExtension ("mp4;mov;m4v;avi;wmv;mkv;mpg;mpeg;webm"); }

    //==============================================================================
    class Panel : public Component,
                  private Timer
    {
    public:
        explicit Panel (te::Edit& e) : edit (e), player (VideoPlayerWin::create())
        {
            setOpaque (true);
            chooseBtn.setTooltip ("Choose the video for this project");
            chooseBtn.onClick = [this] { choose(); };
            hereBtn.setTooltip ("The video begins where the song is now");
            hereBtn.onClick = [this]
            {
                edit.getUndoManager().beginNewTransaction ("Video start");
                setStart (edit, edit.getTransport().getPosition().inSeconds());
            };
            soundBtn.setClickingTogglesState (true);
            soundBtn.setColour (TextButton::buttonOnColourId, Theme::accentDim);
            soundBtn.setTooltip ("Hear the video's own sound (it does not go through the mixer, and is not part of an export)");
            soundBtn.onClick = [this] { edit.getUndoManager().beginNewTransaction ("Video sound"); setSound (edit, soundBtn.getToggleState()); };
            removeBtn.setTooltip ("Take the video out of the project (the file itself is not touched)");
            removeBtn.onClick = [this] { edit.getUndoManager().beginNewTransaction ("Remove video"); setFile (edit, {}); };
            info.setFont (FontOptions (12.5f));
            info.setColour (Label::textColourId, Theme::muted);
            info.setMinimumHorizontalScale (0.8f);
            note.setFont (FontOptions (14.0f));
            note.setColour (Label::textColourId, Theme::text.withAlpha (0.8f));
            note.setJustificationType (Justification::centred);
            for (auto* c : std::initializer_list<Component*> { &chooseBtn, &hereBtn, &soundBtn, &removeBtn, &info, &note }) addAndMakeVisible (c);
            startTimerHz (30);
            timerCallback();
        }

        ~Panel() override
        {
            stopTimer();
            VideoPlayerWin::destroy (player);
        }

        /** How long the video is (0 when none is open), and whether one is open (it takes a moment after choosing). */
        double duration() const   { return VideoPlayerWin::duration (player); }
        bool isOpen() const       { return VideoPlayerWin::isReady (player); }
        String playerState() const { return VideoPlayerWin::describe (player); }
        /** The picture as last handed over by the player. */
        const Image& currentPicture() const { return picture; }
        String noteShown() const  { return note.getText(); }
        /** Looks at the project and the song's position now (the window does this itself thirty times a second). */
        void refreshNow()         { timerCallback(); }

        void resized() override
        {
            auto r = getLocalBounds();
            auto bar = r.removeFromBottom (36).reduced (8, 5);
            chooseBtn.setBounds (bar.removeFromLeft (112));
            bar.removeFromLeft (6);
            hereBtn.setBounds (bar.removeFromLeft (96));
            bar.removeFromLeft (6);
            soundBtn.setBounds (bar.removeFromLeft (64));
            removeBtn.setBounds (bar.removeFromRight (72));
            bar.removeFromRight (6);
            info.setBounds (bar.reduced (6, 0));
            pictureArea = r;
            note.setBounds (r.reduced (30, 0));
        }

        void paint (Graphics& g) override
        {
            g.fillAll (Colours::black);
            if (showPicture && picture.isValid())   // the whole picture, as big as fits
                g.drawImage (picture, pictureArea.toFloat(), RectanglePlacement::centred);
            g.setColour (Theme::panel);
            g.fillRect (getLocalBounds().removeFromBottom (36));
        }

    private:
        void choose()
        {
            const auto now = file (edit);
            chooser = std::make_shared<FileChooser> ("Choose a video", now.existsAsFile() ? now : File::getSpecialLocation (File::userMoviesDirectory),
                                                     "*.mp4;*.mov;*.m4v;*.avi;*.wmv;*.mkv;*.mpg;*.mpeg;*.webm", ! About::isCapturing());
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<Panel> (this)] (const FileChooser& fc)
                                  {
                                      if (sp == nullptr || fc.getResult() == File()) return;
                                      sp->edit.getUndoManager().beginNewTransaction ("Choose video");
                                      setFile (sp->edit, fc.getResult());
                                  });
        }

        static String clock (double seconds)
        {
            const int s = (int) std::floor (jmax (0.0, seconds));
            return String (s / 60) + ":" + String (s % 60).paddedLeft ('0', 2);
        }

        void timerCallback() override
        {
            // the file: open it when it changes (also after undo)
            const auto wanted = file (edit);
            if (wanted != opened)
            {
                opened = wanted;
                problem.clear();
                lastAsked = -1.0;
                volumeSet = -1.0f;
                picture = {};
                VideoPlayerWin::close (player);
                if (wanted != File())
                {
                    if (! wanted.existsAsFile()) problem = "The file is not there any more: " + wanted.getFullPathName();
                    else if (! VideoPlayerWin::open (player, wanted.getFullPathName())) problem = VideoPlayerWin::error (player);   // (a plain path: Windows' player refuses the file:// form)
                }
            }
            if (problem.isEmpty() && wanted != File()) problem = VideoPlayerWin::error (player);   // (what goes wrong while it loads is known a moment later)
            const bool open = VideoPlayerWin::isReady (player);
            const double len = VideoPlayerWin::duration (player), begins = start (edit);
            auto& tc = edit.getTransport();
            const double t = videoTimeAt (tc.getPosition().inSeconds(), begins);
            const bool inside = open && covers (t, len);
            if (const float vol = sound (edit) ? 1.0f : 0.0f; open && vol != volumeSet) { VideoPlayerWin::setVolume (player, vol); volumeSet = vol; }
            if (inside)
            {
                const double at = VideoPlayerWin::position (player);
                if (tc.isPlaying())
                {
                    if (! VideoPlayerWin::isPlaying (player)) { VideoPlayerWin::setPosition (player, t); VideoPlayerWin::play (player); }
                    else if (needsJump (t, at, true)) VideoPlayerWin::setPosition (player, t);
                }
                else
                {
                    if (VideoPlayerWin::isPlaying (player)) VideoPlayerWin::pause (player);
                    if (needsJump (t, at, false) && std::abs (t - lastAsked) > 0.001) { VideoPlayerWin::setPosition (player, t); lastAsked = t; }
                }
            }
            else if (open && VideoPlayerWin::isPlaying (player))
                VideoPlayerWin::pause (player);
            if (open && VideoPlayerWin::frame (player, picture)) repaint (pictureArea);
            if (inside != showPicture) { showPicture = inside; repaint (pictureArea); }
            const bool loading = wanted != File() && problem.isEmpty() && ! open;

            String say;
            if (wanted == File())         say = "No video yet. \"Choose video...\" picks the film this project is written to:\nits picture follows the song as it plays, stops and jumps.";
            else if (problem.isNotEmpty()) say = "This video cannot be shown.\n" + problem;
            else if (loading)              say = "Opening the video...";
            else if (open && t < 0.0)      say = "The video begins at " + clock (begins) + " (in " + clock (-t + 0.999) + ").";
            else if (open && ! inside)     say = "The video has ended (it is " + clock (len) + " long).";
            if (note.getText() != say) note.setText (say, dontSendNotification);
            note.setVisible (say.isNotEmpty());
            const String line = wanted == File() ? String() : wanted.getFileName() + (open ? "   " + clock (jlimit (0.0, jmax (0.0, len), t)) + " / " + clock (len) + "   begins at " + clock (begins) : String());
            if (info.getText() != line) info.setText (line, dontSendNotification);
            const bool got = wanted != File();
            hereBtn.setEnabled (got); soundBtn.setEnabled (got); removeBtn.setEnabled (got);
            soundBtn.setToggleState (sound (edit), dontSendNotification);
        }

        te::Edit& edit;
        void* player = nullptr;
        Image picture;
        Rectangle<int> pictureArea;
        bool showPicture = false;
        double lastAsked = -1.0;
        float volumeSet = -1.0f;
        TextButton chooseBtn { "Choose video..." }, hereBtn { "Begins here" }, soundBtn { "Sound" }, removeBtn { "Remove" };
        Label info, note;
        File opened;
        String problem;
        std::shared_ptr<FileChooser> chooser;
    };

    struct Window : public DocumentWindow
    {
        explicit Window (te::Edit& e) : DocumentWindow ("Video", Colours::black, DocumentWindow::closeButton | DocumentWindow::maximiseButton)
        {
            setUsingNativeTitleBar (true);
            auto* p = new Panel (e);
            p->setSize (720, 441);
            setContentOwned (p, true);
            setResizable (true, false);
            setResizeLimits (360, 240, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { MessageManager::callAsync ([sp = SafePointer<Component> (this)] { delete sp.getComponent(); }); }
    };

    inline Component::SafePointer<Window>& openWindow()
    {
        static Component::SafePointer<Window> w;
        return w;
    }

    inline void show (te::Edit& e)
    {
        auto& w = openWindow();
        if (w == nullptr) w = new Window (e);
        w->toFront (true);
    }

    /** Closes the window (before the project goes away). */
    inline void close() { delete openWindow().getComponent(); }
}
