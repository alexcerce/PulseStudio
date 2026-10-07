// One of Pulse's effects as a plugin for other hosts (VST3): the same device, the same window.
//
// The device itself is the app's own (an effect, or an instrument: PULSE_PLUGIN_SYNTH says which). Here it lives in a small project of its own that is
// never played: the host's sound is handed straight to the device, block by block, and the device's settings are
// offered to the host as the plugin's parameters. Which device this file becomes is decided when it is compiled:
// PULSE_PLUGIN_TYPE is the device's type ("pulsePrismEQ") and JucePlugin_Name its name.
#include <JuceHeader.h>

#include "PulseLookAndFeel.h"
#include "DeviceChain.h"
#include "DeviceEditor.h"

namespace
{
    /** The engine every Pulse plugin in a host shares. No sound device is opened: the host has the sound. */
    struct SharedEngine
    {
        struct Behaviour : public te::EngineBehaviour
        {
            bool autoInitialiseDeviceManager() override { return false; }
        };

        SharedEngine() : engine ("Pulse Plugins", nullptr, std::make_unique<Behaviour>())
        {
            pulse::registerDevices (engine);
            LookAndFeel::setDefaultLookAndFeel (&look);
        }
        ~SharedEngine() { LookAndFeel::setDefaultLookAndFeel (nullptr); }

        PulseLookAndFeel look;
        te::Engine engine;
    };

    /** One setting of the device, as the host sees it.

        Two worlds meet here and they must be kept apart. The host may set a value from any thread, above all from
        its audio thread in the middle of a block. The device's parameter belongs to the engine, and the engine lets
        it be set from the message thread only (setting it starts things that are not safe anywhere else: the first
        version of this plugin set it straight from the host's thread, and a host that moved a band while the
        plugin's own window moved it too froze and crashed). So a value from the host is only noted here, and put
        into the device a moment later by applyHostValue(), which the plugin calls on the message thread. */
    class Setting : public AudioProcessorParameter,
                    private te::AutomatableParameter::Listener
    {
    public:
        Setting (te::AutomatableParameter& p, const std::atomic<bool>& quietFlag) : param (&p), quiet (quietFlag) { param->addListener (this); }
        ~Setting() override { param->removeListener (this); }

        float getValue() const override
        {
            return waiting.load (std::memory_order_acquire) ? fromHost.load (std::memory_order_relaxed) : ofDevice();
        }
        void setValue (float v) override
        {
            // from the host (automation, a preset, its own controls), on whatever thread it likes: noted, nothing more
            fromHost.store (jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
            waiting.store (true, std::memory_order_release);
        }
        /** Message thread: puts a value the host has sent into the device. */
        void applyHostValue()
        {
            if (! waiting.exchange (false, std::memory_order_acq_rel)) return;
            // What the host sends back while a hand is on the control in our own window is its echo of that hand,
            // a little late: letting it in would pull the control back to where it was a moment ago.
            if (inGesture || Time::getMillisecondCounter() - lastOwnMove < 300u) return;
            const float want = param->valueRange.convertFrom0to1 (fromHost.load (std::memory_order_relaxed));
            if (want == param->getCurrentBaseValue()) return;
            const ScopedValueSetter<bool> svs (applying, true);
            param->setParameter (want, dontSendNotification);
        }
        float getDefaultValue() const override             { return param->valueRange.convertTo0to1 (param->getDefaultValue().value_or (param->valueRange.start)); }
        String getName (int maxLength) const override      { return param->getParameterName().substring (0, maxLength); }
        String getLabel() const override                   { return {}; }
        String getText (float v, int) const override       { return param->valueToString (param->valueRange.convertFrom0to1 (jlimit (0.0f, 1.0f, v))); }
        float getValueForText (const String& text) const override
        {
            float v = 0.0f;
            return ValueEntry::valueFor (*param, text, v) ? param->valueRange.convertTo0to1 (v) : getValue();
        }
        bool isDiscrete() const override                   { return param->valueRange.interval >= 0.999f; }
        int getNumSteps() const override
        {
            return isDiscrete() ? roundToInt ((param->valueRange.end - param->valueRange.start) / param->valueRange.interval) + 1 : AudioProcessorParameter::getNumSteps();
        }
        bool isBoolean() const override                    { return isDiscrete() && getNumSteps() == 2; }

    private:
        float ofDevice() const { return param->valueRange.convertTo0to1 (param->getCurrentBaseValue()); }

        // From the plugin's own window (always the message thread): the host is told, so that it can show and record
        // the move. Not told: what it sent itself, and what is put back when a saved state is loaded.
        void curveHasChanged (te::AutomatableParameter&) override {}
        void currentValueChanged (te::AutomatableParameter&) override {}
        void parameterChanged (te::AutomatableParameter&, float) override
        {
            if (applying || quiet.load (std::memory_order_relaxed) || ! MessageManager::getInstance()->isThisTheMessageThread()) return;
            lastOwnMove = Time::getMillisecondCounter();
            waiting.store (false, std::memory_order_release);   // (whatever the host had sent is older than this)
            const float now = ofDevice();
            if (inGesture) { sendValueChangedMessageToListeners (now); return; }
            beginChangeGesture();   // a click on a switch, a typed value, a preset: a whole move in one
            sendValueChangedMessageToListeners (now);
            endChangeGesture();
        }
        void parameterChangeGestureBegin (te::AutomatableParameter&) override { if (! inGesture) { inGesture = true; beginChangeGesture(); } }
        void parameterChangeGestureEnd (te::AutomatableParameter&) override   { if (inGesture) { inGesture = false; lastOwnMove = Time::getMillisecondCounter(); endChangeGesture(); } }

        te::AutomatableParameter::Ptr param;
        const std::atomic<bool>& quiet;
        std::atomic<float> fromHost { 0.0f };
        std::atomic<bool> waiting { false };
        bool applying = false, inGesture = false;   // (message thread only)
        uint32 lastOwnMove = 0;
    };
}

//==============================================================================
class PulseFxProcessor : public AudioProcessor,
                         private Timer
{
public:
    PulseFxProcessor()
        : AudioProcessor (buses()),
          edit (te::Edit::createSingleTrackEdit (shared->engine)),
          selection (shared->engine)
    {
        device = edit->getPluginCache().createNewPlugin (PULSE_PLUGIN_TYPE, {});
        fx = dynamic_cast<pulse::PulsePlugin*> (device.get());
        if (fx != nullptr)
        {
            if (auto t = te::getAudioTracks (*edit).getFirst()) t->pluginList.insertPlugin (device, 0, nullptr);
            for (int i = 0; i < fx->numParams(); ++i) { auto* st = new Setting (*fx->param (i), restoring); settings.add (st); addParameter (st); }
           #if PULSE_PLUGIN_SIDECHAIN
            // In the app the key is another track, chosen on the track. Here it is the host's sidechain input.
            addParameter (keyed = new AudioParameterBool (ParameterID { "hostSidechain", 1 }, "External Sidechain", false));
           #endif
        }
        edit->getUndoManager().clearUndoHistory();
        startTimerHz (100);   // (values from the host are put into the device from here: see Setting)
    }

    ~PulseFxProcessor() override
    {
        stopTimer();
        if (fx != nullptr && prepared) fx->deinitialise();
    }

    const String getName() const override                 { return JucePlugin_Name; }
    bool acceptsMidi() const override                      { return PULSE_PLUGIN_SYNTH != 0; }
    bool producesMidi() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 8.0; }
    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const String getProgramName (int) override             { return {}; }
    void changeProgramName (int, const String&) override   {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
       #if PULSE_PLUGIN_SYNTH
        return l.getMainOutputChannelSet() == AudioChannelSet::stereo();
       #else
        if (l.getMainInputChannelSet() != AudioChannelSet::stereo() || l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
        if (l.inputBuses.size() > 1 && ! (l.inputBuses[1].isDisabled() || l.inputBuses[1] == AudioChannelSet::stereo())) return false;   // (the sidechain: stereo, or not there)
        return true;
       #endif
    }

    void prepareToPlay (double sampleRate, int blockSize) override
    {
        if (fx == nullptr) return;
        if (prepared) fx->deinitialise();
        fx->initialise ({ te::TimePosition(), sampleRate, jmax (16, blockSize) });
        prepared = true;
        rate = sampleRate;
        samplesDone = 0;
        setLatencySamples (roundToInt (fx->getLatencySeconds() * sampleRate));
    }

    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& hostMidi) override
    {
        ScopedNoDenormals noDenormals;
        const int n = buffer.getNumSamples();
        juce::ignoreUnused (hostMidi);
        if (fx == nullptr || ! prepared || buffer.getNumChannels() < 2 || n <= 0) return;

        bool playing = true;
        Optional<double> beat;
        if (auto* head = getPlayHead())
            if (auto pos = head->getPosition())
            {
                playing = pos->getIsPlaying();
                beat = pos->getPpqPosition();
                if (auto bpm = pos->getBpm()) hostBpm.store (*bpm, std::memory_order_relaxed);   // (the project's tempo is set from this, away from the sound)
            }
        // Where the song is: the host's beat, turned into this project's time (which runs at the host's tempo),
        // so that whatever keeps time with the song lands on the host's beats. Stopped, or with a host that
        // does not say: a clock of our own.
        double start = (double) samplesDone / rate;
        if (playing && beat.hasValue() && hostBpm.load (std::memory_order_relaxed) > 20.0) start = jmax (0.0, *beat * 60.0 / hostBpm.load (std::memory_order_relaxed));
        const auto from = te::TimePosition::fromSeconds (start), to = te::TimePosition::fromSeconds (start + (double) n / rate);
        samplesDone += n;
        if (auto* effect = dynamic_cast<pulse::PulseEffect*> (fx))
            effect->hostSidechain.store (keyed != nullptr && keyed->get() && buffer.getNumChannels() >= 4, std::memory_order_relaxed);
        te::MidiMessageArray midi;
       #if PULSE_PLUGIN_SYNTH
        // the host's notes, each at its moment within the block
        for (const auto meta : hostMidi) midi.addMidiMessage (meta.getMessage(), (double) meta.samplePosition / rate, midiSource);
       #endif
        te::PluginRenderContext rc (&buffer, 0, n, &midi, 0.0, { from, to }, playing, false, false, false);
        fx->applyToBuffer (rc);
    }

    //==============================================================================
    // The device's whole state (its settings, and whatever else it keeps: a learned curve, a loaded file's name)
    void getStateInformation (MemoryBlock& dest) override
    {
        if (fx == nullptr) return;
        // (the device's state is the engine's, which is the message thread's: a host that asks from another thread waits for it)
        if (! MessageManager::getInstance()->isThisTheMessageThread())
        {
            if (MessageManager::getInstance()->isThisTheMessageThread() || MessageManager::getInstanceWithoutCreating() == nullptr) return;
            WaitableEvent done;
            WeakReference<PulseFxProcessor> weak (this);
            MessageManager::callAsync ([weak, &dest, &done] { if (weak != nullptr) weak->getStateInformation (dest); done.signal(); });
            done.wait (2000);
            return;
        }
        for (auto* st : settings) st->applyHostValue();
        fx->flushPluginStateToValueTree();
        auto saved = fx->state.createCopy();
        if (keyed != nullptr) saved.setProperty (keyedID, keyed->get(), nullptr);
        saved.setProperty (windowWID, windowW, nullptr);
        saved.setProperty (windowHID, windowH, nullptr);
        if (auto xml = saved.createXml()) copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (fx == nullptr) return;
        auto xml = getXmlFromBinary (data, size);
        if (xml == nullptr) return;
        const auto saved = ValueTree::fromXml (*xml);
        if (! saved.isValid() || saved[te::IDs::type].toString() != String (PULSE_PLUGIN_TYPE)) return;
        if (MessageManager::getInstance()->isThisTheMessageThread()) { restore (saved); return; }
        WeakReference<PulseFxProcessor> weak (this);
        MessageManager::callAsync ([weak, saved] { if (weak != nullptr) weak->restore (saved); });
    }

    /** Message thread: puts a saved state into the device. */
    void restore (const ValueTree& saved)
    {
        restoring.store (true);   // (the host is not told of each setting as if a hand had moved it)
        // what is not a setting first (these are plain properties), then each setting through the device itself
        StringArray settingNames;
        for (int i = 0; i < fx->numParams(); ++i) settingNames.add (pulse::PulsePlugin::storageID (fx->param (i)->paramID).toString());
        for (int i = 0; i < saved.getNumProperties(); ++i)
        {
            const auto name = saved.getPropertyName (i);
            if (name != te::IDs::id && name != te::IDs::type && name != keyedID && name != windowWID && name != windowHID
                && ! settingNames.contains (name.toString())) fx->state.setProperty (name, saved[name], nullptr);
        }
        if (keyed != nullptr) *keyed = (bool) saved.getProperty (keyedID, false);
        windowW = saved.getProperty (windowWID, 0);
        windowH = saved.getProperty (windowHID, 0);
        for (int i = 0; i < fx->numParams(); ++i)
        {
            const Identifier name (settingNames[i]);
            if (saved.hasProperty (name)) fx->set (i, (float) saved[name]);
        }
        restoring.store (false);
        edit->getUndoManager().clearUndoHistory();
        updateHostDisplay (ChangeDetails().withParameterInfoChanged (false).withNonParameterStateChanged (true));
    }

    //==============================================================================
    bool hasEditor() const override { return fx != nullptr; }
    AudioProcessorEditor* createEditor() override;

    pulse::PulsePlugin* effect() const   { return fx; }
    AudioParameterBool* sidechainSwitch() const { return keyed; }
    int windowW = 0, windowH = 0;        // the size the window was left at (0: the device's own)
    te::Edit& project()                  { return *edit; }
    te::SelectionManager& selected()     { return selection; }

private:
    void timerCallback() override
    {
        if (fx == nullptr) return;
        for (auto* st : settings) st->applyHostValue();
        // (for testing, asked for by PULSE_PLUGIN_SELF_MOVE in the environment: the settings are moved from in here as
        // the window's controls move them, hand on and hand off, so that a test host can ride them from its side at the
        // same time and see whether the two get in each other's way)
        if (selfMove && fx->numParams() > 0)
        {
            auto* prm = fx->param (selfRandom.nextInt (fx->numParams()));
            prm->parameterChangeGestureBegin();
            prm->setParameter (prm->valueRange.snapToLegalValue (prm->valueRange.convertFrom0to1 (selfRandom.nextFloat())), sendNotification);
            prm->parameterChangeGestureEnd();
        }
        if (++tick % 10 != 0) return;
        // the tempo of the host, for whatever in the device keeps time with the song
        const double bpm = hostBpm.load (std::memory_order_relaxed);
        if (auto tempo = edit->tempoSequence.getTempo (0); tempo != nullptr && bpm > 20.0 && std::abs (tempo->getBpm() - bpm) > 0.01) tempo->setBpm (bpm);
        // a device whose delay changes with a setting (a linear-phase mode, a lookahead) tells the host
        if (prepared)
            if (const int late = roundToInt (fx->getLatencySeconds() * rate); late != getLatencySamples()) setLatencySamples (late);
    }

    static BusesProperties buses()
    {
       #if PULSE_PLUGIN_SYNTH
        auto b = BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true);   // an instrument: notes in, sound out
       #else
        auto b = BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true);
       #endif
       #if PULSE_PLUGIN_SIDECHAIN
        b = b.withInput ("Sidechain", AudioChannelSet::stereo(), true);
       #endif
        return b;
    }

    SharedResourcePointer<SharedEngine> shared;
    std::unique_ptr<te::Edit> edit;
    te::SelectionManager selection;
    te::Plugin::Ptr device;
    pulse::PulsePlugin* fx = nullptr;
    Array<Setting*> settings;             // (owned by the processor's parameter list)
    std::atomic<bool> restoring { false };
    int tick = 0;
    const bool selfMove = SystemStats::getEnvironmentVariable ("PULSE_PLUGIN_SELF_MOVE", {}) == "1";
    Random selfRandom { 99 };
    JUCE_DECLARE_WEAK_REFERENCEABLE (PulseFxProcessor)
    const te::MPESourceID midiSource = te::createUniqueMPESourceID();
    AudioParameterBool* keyed = nullptr;
    static inline const Identifier keyedID { "pulseHostSidechain" }, windowWID { "pulseWindowW" }, windowHID { "pulseWindowH" };
    bool prepared = false;
    double rate = 48000.0;
    int64 samplesDone = 0;
    std::atomic<double> hostBpm { 120.0 };
};

//==============================================================================
/** The plugin's window: the device's own window from the app, at a size the host can change. */
class PulseFxEditor : public AudioProcessorEditor
{
public:
    explicit PulseFxEditor (PulseFxProcessor& p) : AudioProcessorEditor (p), proc (p), evs (p.project(), p.selected()), window (evs, *p.effect())
    {
        addAndMakeVisible (window);
        if (auto* sw = p.sidechainSwitch())
        {
            keyBtn.setButtonText ("Key from the host's sidechain input");
            keyBtn.setTooltip ("On: the device listens to what the host sends to this plugin's sidechain input. Off: it listens to its own input.");
            keyBtn.setClickingTogglesState (true);
            addAndMakeVisible (keyBtn);
            keyLink = std::make_unique<ButtonParameterAttachment> (*sw, keyBtn);
        }
        // the device's own size (as in the app), kept to what a host's window can reasonably take; then whatever
        // size it was left at last time
        const int bar = keyBtn.isVisible() ? barH : 0;
        int w = jlimit (560, 1280, window.getWidth()), h = jlimit (420, 860, window.getHeight()) + bar;   // (the big instruments ask for 1280 x 860)
        if (p.windowW >= 560 && p.windowH >= 420) { w = p.windowW; h = p.windowH; }
        setResizable (true, true);
        setResizeLimits (560, 420, 3000, 2000);
        ready = true;
        setSize (w, h);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        if (keyBtn.isVisible()) keyBtn.setBounds (r.removeFromBottom (barH).reduced (12, 4).removeFromLeft (300));
        window.setBounds (r);
        if (ready) { proc.windowW = getWidth(); proc.windowH = getHeight(); }
    }
    void paint (Graphics& g) override { g.fillAll (Pro::bg()); }

private:
    static constexpr int barH = 34;
    SharedResourcePointer<SharedEngine> shared;   // (keeps the look alive as long as a window is)
    PulseFxProcessor& proc;
    EditViewState evs;
    DeviceEditors::Editor window;
    TextButton keyBtn;
    std::unique_ptr<ButtonParameterAttachment> keyLink;
    bool ready = false;
    TooltipWindow tips { this, 600 };
};

AudioProcessorEditor* PulseFxProcessor::createEditor() { return new PulseFxEditor (*this); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PulseFxProcessor(); }
