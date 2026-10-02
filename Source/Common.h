#pragma once

#include <JuceHeader.h>

namespace te = tracktion;
using namespace std::literals;
using namespace juce;

//==============================================================================
namespace Theme
{
    const Colour bg          { 0xff111317 };
    const Colour panel       { 0xff181b20 };
    const Colour panel2      { 0xff20242b };
    const Colour line        { 0xff2b3039 };
    const Colour text        { 0xffe4e8ee };
    const Colour muted       { 0xff8a929d };
    const Colour accent      { 0xff4da3ff };
    const Colour orange      { 0xffffa94d };
    const Colour red         { 0xffff6b6b };
    const Colour green       { 0xff69db7c };
    const Colour yellow      { 0xffffd43b };

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
