#pragma once

#include <JuceHeader.h>

namespace te = tracktion;
using namespace std::literals;
using namespace juce;

//==============================================================================
namespace Theme
{
    // The current colours (set by apply()). Everything draws with these, so a theme change
    // followed by a repaint recolours the whole app.
    inline Colour bg        { 0xff111317 };   // window background
    inline Colour bg2       { 0xff15171b };   // lanes, track rows
    inline Colour well      { 0xff0e1013 };   // displays, graphs, meters, text fields
    inline Colour panel     { 0xff181b20 };   // panels, cards
    inline Colour panel2    { 0xff20242b };   // buttons, widgets
    inline Colour header    { 0xff1b1e23 };   // headers, menus, toolbars
    inline Colour raised    { 0xff1d2026 };   // hover, bars
    inline Colour line      { 0xff2b3039 };
    inline Colour text      { 0xffe4e8ee };
    inline Colour muted     { 0xff8a929d };
    inline Colour accent    { 0xff4da3ff };
    inline Colour accentDim { 0xff2b5b8a };   // toggled-on button backgrounds, selection
    inline Colour orange    { 0xffffa94d };
    inline Colour red       { 0xffff6b6b };
    inline Colour green     { 0xff69db7c };
    inline Colour yellow    { 0xffffd43b };
    inline Colour purple    { 0xffb197fc };
    inline Colour ink       { 0xffffffff };   // grid lines and overlays (white on dark themes, black on light)
    inline bool isLight = false;

    struct Def
    {
        const char* name;
        uint32 bg, bg2, well, panel, panel2, header, raised, line, text, muted, accent, orange, red, green, yellow, purple;
        bool light;
    };

    inline const std::vector<Def>& all()
    {
        static const std::vector<Def> d {
            //  name                bg          bg2         well        panel       panel2      header      raised      line        text        muted       accent      orange      red         green       yellow      purple   light
            { "Pulse Dark",        0xff111317, 0xff15171b, 0xff0e1013, 0xff181b20, 0xff20242b, 0xff1b1e23, 0xff1d2026, 0xff2b3039, 0xffe4e8ee, 0xff8a929d, 0xff4da3ff, 0xffffa94d, 0xffff6b6b, 0xff69db7c, 0xffffd43b, 0xffb197fc, false },
            { "Midnight Blue",     0xff0c1220, 0xff101829, 0xff080d18, 0xff141d30, 0xff1b2640, 0xff16203a, 0xff1d2945, 0xff28365a, 0xffe2e9f7, 0xff8091b5, 0xff5c8dff, 0xffffa95c, 0xffff6b81, 0xff5fd6a4, 0xffffd75e, 0xffa98bff, false },
            { "Graphite",          0xff1a1a1a, 0xff1f1f1f, 0xff141414, 0xff242424, 0xff2e2e2e, 0xff262626, 0xff2a2a2a, 0xff3a3a3a, 0xffe6e6e6, 0xff9a9a9a, 0xffd0d0d0, 0xffffa94d, 0xffff6b6b, 0xff7bd88f, 0xffffd43b, 0xffbfa6ff, false },
            { "Studio Grey",       0xff2b2d31, 0xff303237, 0xff222327, 0xff36383d, 0xff42454b, 0xff3a3c41, 0xff3e4046, 0xff4c4f56, 0xffeef0f3, 0xffa6aab2, 0xffffb347, 0xffffb347, 0xffff6b6b, 0xff7bd88f, 0xffffe066, 0xffc0a8ff, false },
            { "OLED Black",        0xff000000, 0xff050505, 0xff000000, 0xff0a0a0a, 0xff141414, 0xff0a0a0a, 0xff111111, 0xff222222, 0xfff2f2f2, 0xff8c8c8c, 0xff00b3ff, 0xffff9f1c, 0xffff4d4d, 0xff3ddc84, 0xffffd60a, 0xffb388ff, false },
            { "Nord",              0xff2e3440, 0xff323946, 0xff272c36, 0xff3b4252, 0xff434c5e, 0xff3b4252, 0xff434c5e, 0xff4c566a, 0xffeceff4, 0xff9aa5b8, 0xff88c0d0, 0xffd08770, 0xffbf616a, 0xffa3be8c, 0xffebcb8b, 0xffb48ead, false },
            { "Dracula",           0xff282a36, 0xff2c2e3b, 0xff21222c, 0xff313342, 0xff3a3c4e, 0xff343746, 0xff3b3e51, 0xff44475a, 0xfff8f8f2, 0xff9ea2c0, 0xffbd93f9, 0xffffb86c, 0xffff5555, 0xff50fa7b, 0xfff1fa8c, 0xffff79c6, false },
            { "Solarized Dark",    0xff002b36, 0xff04313d, 0xff00232c, 0xff073642, 0xff0d404e, 0xff073642, 0xff0f4553, 0xff1c5260, 0xffeee8d5, 0xff839496, 0xff268bd2, 0xffcb4b16, 0xffdc322f, 0xff859900, 0xffb58900, 0xff6c71c4, false },
            { "Monokai",           0xff272822, 0xff2c2d26, 0xff1e1f1b, 0xff31322b, 0xff3b3c34, 0xff33342d, 0xff3e3f37, 0xff49483e, 0xfff8f8f2, 0xffa59f85, 0xff66d9ef, 0xfffd971f, 0xfff92672, 0xffa6e22e, 0xffe6db74, 0xffae81ff, false },
            { "Gruvbox",           0xff282828, 0xff2d2c2a, 0xff1d2021, 0xff32302f, 0xff3c3836, 0xff32302f, 0xff3f3b38, 0xff504945, 0xffebdbb2, 0xffa89984, 0xfffabd2f, 0xfffe8019, 0xfffb4934, 0xffb8bb26, 0xfffabd2f, 0xffd3869b, false },
            { "Synthwave",         0xff1a1028, 0xff1f1330, 0xff130b1e, 0xff241638, 0xff2e1c47, 0xff26183c, 0xff331f50, 0xff45306a, 0xfff5e9ff, 0xffa58fc4, 0xffff3ea5, 0xffff9e44, 0xffff4f6d, 0xff36f9c5, 0xfffff36d, 0xff9d6bff, false },
            { "Forest",            0xff121a15, 0xff151f19, 0xff0d140f, 0xff19241d, 0xff213027, 0xff1b2720, 0xff23332a, 0xff2e4236, 0xffe3efe6, 0xff8ea597, 0xff5fd38d, 0xffe9a55a, 0xffe76f6f, 0xff9be37a, 0xffe8d067, 0xffa8a2f0, false },
            { "Ocean",             0xff0b1b22, 0xff0e2029, 0xff07141a, 0xff112731, 0xff17323e, 0xff132a35, 0xff1a3744, 0xff234756, 0xffe0f2f7, 0xff86a9b5, 0xff2ec4d6, 0xffffa36b, 0xffff6f7d, 0xff4fe0a6, 0xffffd166, 0xff9b9cf5, false },
            { "Ember",             0xff1b1310, 0xff201713, 0xff140e0b, 0xff251a16, 0xff30221c, 0xff281d18, 0xff342620, 0xff45322a, 0xfff6e8df, 0xffb09484, 0xffff7a3d, 0xffffb057, 0xffff5a4e, 0xff9ad46b, 0xffffd166, 0xffd59bff, false },
            { "Rose Pine",         0xff191724, 0xff1d1a2b, 0xff141220, 0xff1f1d2e, 0xff26233a, 0xff221f33, 0xff2a2740, 0xff3a3654, 0xffe0def4, 0xff908caa, 0xffebbcba, 0xfff6c177, 0xffeb6f92, 0xff9ccfd8, 0xfff6c177, 0xffc4a7e7, false },
            { "Cyber Lime",        0xff0f1112, 0xff131617, 0xff0a0c0d, 0xff171b1c, 0xff1f2426, 0xff191d1f, 0xff222829, 0xff2e3537, 0xffe8f0ea, 0xff8c9a93, 0xffb6f23a, 0xffffa94d, 0xffff5e5e, 0xff5ff08a, 0xffffe24d, 0xffb18cff, false },
            { "Daylight",          0xffe9ecf0, 0xfff1f3f6, 0xfffafbfc, 0xffdfe3e8, 0xffffffff, 0xffd6dbe2, 0xffcfd5dd, 0xffb9c1cc, 0xff1d232b, 0xff5d6673, 0xff1f74e0, 0xffd9731a, 0xffd93a3a, 0xff2e9e4f, 0xffc79a00, 0xff7a55d6, true  },
            { "Paper",             0xfff1ebe0, 0xfff6f1e8, 0xfffbf8f2, 0xffe8e0d2, 0xfffffcf6, 0xffe2d9c8, 0xffd9cfbd, 0xffc4b8a3, 0xff2b241b, 0xff6f6352, 0xffb5561d, 0xffc96d1e, 0xffc23b2f, 0xff4c8a3c, 0xffad8400, 0xff7d55a8, true  },
            { "Solarized Light",   0xfffdf6e3, 0xfffbf3dd, 0xffffffff, 0xffeee8d5, 0xfffffbef, 0xffeae3cc, 0xffe3dcc3, 0xffd3cbb0, 0xff073642, 0xff657b83, 0xff268bd2, 0xffcb4b16, 0xffdc322f, 0xff859900, 0xffb58900, 0xff6c71c4, true  },
            { "High Contrast",     0xff000000, 0xff000000, 0xff000000, 0xff0d0d0d, 0xff1a1a1a, 0xff000000, 0xff262626, 0xff8c8c8c, 0xffffffff, 0xffd0d0d0, 0xffffee00, 0xffff9900, 0xffff3333, 0xff33ff66, 0xffffee00, 0xffcc99ff, false },
        };
        return d;
    }

    inline StringArray names()
    {
        StringArray n;
        for (auto& d : all()) n.add (d.name);
        return n;
    }

    inline int& currentIndex() { static int i = 0; return i; }

    /** Switches the colours (callers then refresh the look and feel and repaint). */
    inline void apply (int index)
    {
        auto& d = all()[(size_t) jlimit (0, (int) all().size() - 1, index)];
        currentIndex() = jlimit (0, (int) all().size() - 1, index);
        bg = Colour (d.bg); bg2 = Colour (d.bg2); well = Colour (d.well); panel = Colour (d.panel); panel2 = Colour (d.panel2);
        header = Colour (d.header); raised = Colour (d.raised); line = Colour (d.line); text = Colour (d.text); muted = Colour (d.muted);
        accent = Colour (d.accent); orange = Colour (d.orange); red = Colour (d.red); green = Colour (d.green); yellow = Colour (d.yellow);
        purple = Colour (d.purple);
        isLight = d.light;
        ink = d.light ? Colour (0xff000000) : Colour (0xffffffff);
        accentDim = accent.interpolatedWith (panel2, d.light ? 0.7f : 0.55f);
    }

    inline int indexOf (const String& name) { return jmax (0, names().indexOf (name)); }

    /** The current token colours (to remap colours components set when they were built). */
    inline std::vector<Colour> tokens() { return { bg, bg2, well, panel, panel2, header, raised, line, text, muted, accent, accentDim, orange, red, green, yellow, purple }; }

    /** Walks a component tree and swaps every explicitly set colour that was an old token
        (any transparency) for the same token of the new theme. */
    inline void remapColours (Component& c, const std::vector<Colour>& from, const std::vector<Colour>& to)
    {
        std::vector<std::pair<int, Colour>> changes;
        auto& props = c.getProperties();
        for (int i = 0; i < props.size(); ++i)
        {
            const auto name = props.getName (i).toString();
            if (! name.startsWith ("jcclr_")) continue;
            const Colour col ((uint32) (int) props.getValueAt (i));
            for (size_t k = 0; k < from.size() && k < to.size(); ++k)
                if ((col.getARGB() & 0xffffff) == (from[k].getARGB() & 0xffffff))
                {
                    changes.push_back ({ name.substring (6).getHexValue32(), to[k].withAlpha (col.getAlpha()) });
                    break;
                }
        }
        for (auto& [id, colour] : changes) c.setColour (id, colour);
        for (auto* ch : c.getChildren()) remapColours (*ch, from, to);
    }

    inline Colour trackColour (int index)
    {
        static const uint32 cols[] = { 0xffff6b6b, 0xff4dabf7, 0xffda77f2, 0xffffd43b, 0xff69db7c,
                                       0xffffa94d, 0xff38d9a9, 0xff748ffc, 0xfff783ac };
        return Colour (cols[(size_t) index % std::size (cols)]);
    }
}

//==============================================================================
namespace EngineHelpers
{
    inline te::AudioTrack* getSelectedTrack (te::SelectionManager& sm)
    {
        if (auto t = sm.getFirstItemOfType<te::AudioTrack>())
            return t;

        if (auto c = sm.getFirstItemOfType<te::Clip>())
            return dynamic_cast<te::AudioTrack*> (c->getTrack());

        return nullptr;
    }

    inline bool trackHasInstrument (te::AudioTrack& t)
    {
        for (auto p : t.pluginList)
            if (p->isSynth())
                return true;

        return false;
    }

    inline void armTrack (te::AudioTrack& t, bool arm)
    {
        for (auto instance : t.edit.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0))
                instance->setRecordingEnabled (t.itemID, arm);
    }

    inline bool isTrackArmed (te::AudioTrack& t)
    {
        for (auto instance : t.edit.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0))
                if (instance->isRecordingEnabled (t.itemID))
                    return true;

        return false;
    }

    inline bool trackHasInput (te::AudioTrack& t)
    {
        for (auto instance : t.edit.getAllInputDevices())
            if (te::isOnTargetTrack (*instance, t, 0))
                return true;

        return false;
    }

    /** Routes every MIDI input to the given track (so the selected track plays live). */
    inline void routeMidiInputsTo (te::AudioTrack& t)
    {
        for (auto instance : t.edit.getAllInputDevices())
            if (instance->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice
                || instance->getInputDevice().getDeviceType() == te::InputDevice::virtualMidiDevice)
                if (instance->getTargets().getFirst() != t.itemID)
                    [[maybe_unused]] auto res = instance->setTarget (t.itemID, true, &t.edit.getUndoManager(), 0);
    }

    /** True while an export renders in the background (playback must wait). */
    inline std::atomic<bool>& renderBusy() { static std::atomic<bool> b { false }; return b; }

    inline void togglePlay (te::Edit& edit)
    {
        if (renderBusy()) return;
        auto& tc = edit.getTransport();

        if (tc.isPlaying())
            tc.stop (false, false);
        else
            tc.play (false);
    }

    inline void toggleRecord (te::Edit& edit)
    {
        if (renderBusy()) return;
        auto& tc = edit.getTransport();

        if (tc.isRecording())
            tc.stop (false, false);
        else
            tc.record (false);
    }

    inline te::TimePosition beatToTime (te::Edit& e, double beat)
    {
        return e.tempoSequence.toTime (te::BeatPosition::fromBeats (beat));
    }

    inline double timeToBeat (te::Edit& e, te::TimePosition t)
    {
        return e.tempoSequence.toBeats (t).inBeats();
    }

    inline int beatsPerBar (te::Edit& e)
    {
        auto& sig = e.tempoSequence.getTimeSigAt (te::TimePosition());
        return jmax (1, sig.numerator.get() * 4 / jmax (1, sig.denominator.get()));
    }

    /** User tracks that hold clips (excludes return tracks, see Routing.h). */
    inline juce::Array<te::AudioTrack*> getUserTracks (te::Edit& e)
    {
        juce::Array<te::AudioTrack*> out;
        for (auto t : te::getAudioTracks (e))
            if (dynamic_cast<te::AuxReturnPlugin*> (t->pluginList[0]) == nullptr)
                out.add (t);
        return out;
    }

    inline String formatBarsBeats (te::Edit& e, te::TimePosition t)
    {
        auto bb = e.tempoSequence.toBarsAndBeats (t);
        auto beatsFrac = bb.getFractionalBeats().inBeats();
        return String (bb.bars + 1) + "." + String ((int) bb.getWholeBeats() + 1) + "." + String ((int) (beatsFrac * 4.0) + 1);
    }

    inline String formatTime (te::TimePosition t)
    {
        auto s = t.inSeconds();
        auto m = (int) (s / 60.0);
        return String (m) + ":" + String (s - m * 60.0, 3).paddedLeft ('0', 6);
    }
}

//==============================================================================
/** True for Ctrl (or Cmd) + the given letter, regardless of how the OS reports the key code. */
inline bool isCtrlKey (const KeyPress& k, juce_wchar letter)
{
    if (! k.getModifiers().isCommandDown())
        return false;

    auto code = (juce_wchar) k.getKeyCode();

    // Windows may report Ctrl+letter as the ASCII control character (Ctrl+A = 1 ... Ctrl+Z = 26).
    if (code >= 1 && code <= 26)
        code = 'a' + code - 1;

    return CharacterFunctions::toLowerCase (code) == letter
        || CharacterFunctions::toLowerCase (k.getTextCharacter()) == letter;
}

//==============================================================================
/** Small helper that coalesces change notifications into one async UI update. */
class FlaggedAsyncUpdater : public AsyncUpdater
{
public:
    void markAndUpdate (bool& flag)     { flag = true; triggerAsyncUpdate(); }

    bool compareAndReset (bool& flag) noexcept
    {
        if (! flag)
            return false;

        flag = false;
        return true;
    }
};

//==============================================================================
/** Startup timing: each mark is written (ms since launch) to %TEMP%/pulse-startup.log. */
namespace StartupTimer
{
    inline double& origin() { static double t = Time::getMillisecondCounterHiRes(); return t; }
    inline StringArray& lines() { static StringArray l; return l; }
    inline void mark (const String& what)
    {
        const double ms = Time::getMillisecondCounterHiRes() - origin();
        lines().add (String (roundToInt (ms)).paddedLeft (' ', 6) + " ms  " + what);
    }
    inline void save() { File::getSpecialLocation (File::tempDirectory).getChildFile ("pulse-startup.log").replaceWithText (lines().joinIntoString ("\n")); }
}
