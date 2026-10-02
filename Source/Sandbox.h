#pragma once

#include "PulseDevices.h"

namespace CrashGuardWin { void disableCrashDialogs(); int currentProcessId(); bool isProcessAlive (int pid); }

/** Plugin sandboxing: a VST3 runs inside a helper process ("Pulse Studio.exe" in host mode),
    so a crashing plugin only takes down its helper, not Pulse.

    Each audio block (audio + MIDI + parameter changes) goes to the helper over a named pipe and
    the processed block comes back. If the helper dies, the device goes silent (effects pass audio
    through) and can be restarted from its card. */
namespace sandbox
{
    static constexpr const char* commandLineID = "pulse-plugin-sandbox";

    // message types
    enum : uint8 { msgLoad = 'L', msgLoaded = 'l', msgPrepare = 'R', msgProcess = 'P', msgProcessed = 'p',
                   msgGetState = 'S', msgState = 's', msgShowEditor = 'E', msgCrashForTest = 'K' };

    struct ParamInfo { String name, label; float defaultValue = 0, value = 0; };

    //==============================================================================
    /** Runs in the helper process: hosts one plugin. Messages arrive on the pipe thread. */
    class Worker : public ChildProcessWorker
    {
    public:
        Worker() { formats.addFormat (new VST3PluginFormat()); }

        void handleMessageFromCoordinator (const MemoryBlock& mb) override
        {
            MemoryInputStream in (mb, false);
            const auto type = (uint8) in.readByte();

            switch (type)
            {
                case msgLoad:       load (in); break;
                case msgPrepare:    prepare (in.readDouble(), in.readInt()); break;
                case msgProcess:    process (in); break;
                case msgGetState:   sendState(); break;
                case msgShowEditor: MessageManager::callAsync ([this] { showEditor(); }); break;
                case msgCrashForTest: { volatile int* p = nullptr; *p = 1; break; }
                default: break;
            }
        }

        void handleConnectionLost() override
        {
            MessageManager::callAsync ([] { JUCEApplicationBase::quit(); });
        }

    private:
        void load (MemoryInputStream& in)
        {
            const auto descXml = in.readString();
            const auto file = in.readString();
            sampleRate = in.readDouble();
            blockSize = in.readInt();
            MemoryBlock state;
            if (auto size = in.readInt(); size > 0) in.readIntoMemoryBlock (state, size);

            String error;
            PluginDescription desc;
            if (auto xml = parseXML (descXml)) desc.loadFromXml (*xml);
            if (desc.fileOrIdentifier.isEmpty() && file.isNotEmpty())
            {
                OwnedArray<PluginDescription> found;
                VST3PluginFormat fmt;
                fmt.findAllTypesForFile (found, file);
                if (! found.isEmpty()) desc = *found[0];
            }

            // plugins must be created on the message thread
            WaitableEvent done;
            MessageManager::callAsync ([&]
            {
                plugin = formats.createPluginInstance (desc, sampleRate, blockSize, error);
                if (plugin != nullptr)
                {
                    plugin->enableAllBuses();
                    plugin->prepareToPlay (sampleRate, blockSize);
                    if (state.getSize() > 0) plugin->setStateInformation (state.getData(), (int) state.getSize());
                }
                done.signal();
            });
            done.wait (30000);

            MemoryOutputStream out;
            out.writeByte ((char) msgLoaded);
            out.writeBool (plugin != nullptr);
            out.writeInt (CrashGuardWin::currentProcessId());
            out.writeString (plugin != nullptr ? plugin->getName() : error);
            if (plugin != nullptr)
            {
                PluginDescription d;
                plugin->fillInPluginDescription (d);
                if (auto x = d.createXml()) out.writeString (x->toString());
                else out.writeString ({});
                out.writeInt (plugin->getLatencySamples());
                out.writeBool (plugin->acceptsMidi() && plugin->getTotalNumInputChannels() == 0);
                auto& params = plugin->getParameters();
                out.writeInt (params.size());
                lastValues.resize ((size_t) params.size());
                for (int i = 0; i < params.size(); ++i)
                {
                    auto* p = params[i];
                    out.writeString (p->getName (64));
                    out.writeString (p->getLabel());
                    out.writeFloat (p->getDefaultValue());
                    out.writeFloat (lastValues[(size_t) i] = p->getValue());
                }
            }
            sendMessageToCoordinator (out.getMemoryBlock());
        }

        void prepare (double sr, int block)
        {
            const ScopedLock sl (lock);
            sampleRate = sr; blockSize = block;
            if (plugin != nullptr) { plugin->releaseResources(); plugin->prepareToPlay (sr, block); }
        }

        void process (MemoryInputStream& in)
        {
            const int n = in.readInt(), numCh = in.readInt();
            MemoryOutputStream out;
            out.writeByte ((char) msgProcessed);

            const ScopedLock sl (lock);
            const int pluginCh = plugin != nullptr ? jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()) : 2;
            buffer.setSize (pluginCh, jmax (1, n), false, false, true);
            buffer.clear();
            for (int ch = 0; ch < numCh; ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float v = in.readFloat();
                    if (ch < pluginCh) buffer.setSample (ch, i, v);
                }

            MidiBuffer midi;
            for (int i = 0, numMidi = in.readInt(); i < numMidi; ++i)
            {
                const int pos = in.readInt(), size = in.readInt();
                HeapBlock<uint8> data ((size_t) size);
                in.read (data.get(), size);
                midi.addEvent (data.get(), size, pos);
            }

            if (plugin != nullptr)
            {
                auto& params = plugin->getParameters();
                for (int i = 0, numChanges = in.readInt(); i < numChanges; ++i)
                {
                    const int idx = in.readInt();
                    const float v = in.readFloat();
                    if (isPositiveAndBelow (idx, params.size()))
                    {
                        params[idx]->setValue (v);
                        lastValues[(size_t) idx] = v;
                    }
                }
                plugin->processBlock (buffer, midi);
            }

            out.writeInt (n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    out.writeFloat (buffer.getSample (jmin (ch, buffer.getNumChannels() - 1), i));

            // parameters the plugin changed itself (its own UI, presets)
            std::vector<std::pair<int, float>> changed;
            if (plugin != nullptr)
            {
                auto& params = plugin->getParameters();
                for (int i = 0; i < params.size() && i < (int) lastValues.size(); ++i)
                    if (const float v = params[i]->getValue(); std::abs (v - lastValues[(size_t) i]) > 1.0e-6f)
                    {
                        lastValues[(size_t) i] = v;
                        changed.push_back ({ i, v });
                    }
            }
            out.writeInt ((int) changed.size());
            for (auto& [i, v] : changed) { out.writeInt (i); out.writeFloat (v); }
            sendMessageToCoordinator (out.getMemoryBlock());
        }

        void sendState()
        {
            MemoryBlock state;
            {
                const ScopedLock sl (lock);
                if (plugin != nullptr) plugin->getStateInformation (state);
            }
            MemoryOutputStream out;
            out.writeByte ((char) msgState);
            out.writeInt ((int) state.getSize());
            out.write (state.getData(), state.getSize());
            sendMessageToCoordinator (out.getMemoryBlock());
        }

        struct EditorWindow : public DocumentWindow
        {
            EditorWindow (AudioProcessorEditor* ed, const String& name)
                : DocumentWindow (name + "  (sandboxed)", Theme::panel, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
            {
                setUsingNativeTitleBar (true);
                setContentOwned (ed, true);
                setAlwaysOnTop (true);
                centreWithSize (getWidth(), getHeight());
                setVisible (true);
            }
            void closeButtonPressed() override { setVisible (false); }
        };

        void showEditor()
        {
            if (plugin == nullptr) return;
            if (editorWindow == nullptr)
            {
                auto* ed = plugin->hasEditor() ? plugin->createEditorIfNeeded() : nullptr;
                if (ed == nullptr) ed = new GenericAudioProcessorEditor (*plugin);
                editorWindow = std::make_unique<EditorWindow> (ed, plugin->getName());
            }
            editorWindow->setVisible (true);
            editorWindow->toFront (true);
        }

        AudioPluginFormatManager formats;
        std::unique_ptr<AudioPluginInstance> plugin;
        std::unique_ptr<EditorWindow> editorWindow;
        CriticalSection lock;
        AudioBuffer<float> buffer;
        std::vector<float> lastValues;
        double sampleRate = 44100.0;
        int blockSize = 512;
    };

    /** Called at startup: true if this process is a sandbox helper (it then just hosts). */
    inline std::unique_ptr<Worker>& worker() { static std::unique_ptr<Worker> w; return w; }

    inline bool startWorkerIfRequested (const String& commandLine)
    {
        if (! commandLine.contains (commandLineID)) return false;
        auto w = std::make_unique<Worker>();
        if (! w->initialiseFromCommandLine (commandLine, commandLineID, 0))
            return false;
        // if the plugin crashes, exit at once (no error-report dialog) so Pulse notices straight away
        CrashGuardWin::disableCrashDialogs();
        worker() = std::move (w);
        return true;
    }
}

namespace pulse
{
    //==============================================================================
    /** Host side: a device that runs a VST3 in a sandbox helper process. */
    class SandboxedPlugin : public PulsePlugin
    {
    public:
        static inline const char* xmlTypeName = "pulseSandbox";
        static const char* getPluginName() { return "Sandboxed plugin"; }
        static inline const Identifier descID { "sandboxDesc" }, fileID { "sandboxFile" }, stateID { "sandboxState" },
                                       nameID { "sandboxName" }, synthID { "sandboxSynth" };

        explicit SandboxedPlugin (te::PluginCreationInfo info) : PulsePlugin (info), host (*this)
        {
            displayName = state[nameID].toString();
            instrument = state[synthID];
            launchAndLoad();
        }

        ~SandboxedPlugin() override { host.killWorkerProcess(); }

        /** State tree for a new sandboxed copy of a plugin (by description, or a .vst3 file). */
        static ValueTree createState (const PluginDescription* desc, const File& file = {}, const MemoryBlock* pluginState = nullptr)
        {
            ValueTree v (te::IDs::PLUGIN);
            v.setProperty (te::IDs::type, xmlTypeName, nullptr);
            if (desc != nullptr)
                if (auto x = desc->createXml()) v.setProperty (descID, x->toString(), nullptr);
            if (file != File()) v.setProperty (fileID, file.getFullPathName(), nullptr);
            if (pluginState != nullptr && pluginState->getSize() > 0) v.setProperty (stateID, pluginState->toBase64Encoding(), nullptr);
            if (desc != nullptr) { v.setProperty (nameID, desc->name, nullptr); v.setProperty (synthID, desc->isInstrument, nullptr); }
            return v;
        }

        String getName() const override              { return displayName.isNotEmpty() ? displayName : String ("Sandboxed plugin"); }
        String getPluginType() override              { return xmlTypeName; }
        bool isSynth() override                      { return instrument; }
        bool takesMidiInput() override               { return true; }
        bool takesAudioInput() override              { return true; }
        bool producesAudioWhenNoAudioInput() override { return instrument; }
        int getNumOutputChannelsGivenInputs (int) override { return 2; }
        BusLayout getBusses() const override         { return BusLayout::singleStereoInOut(); }
        double getLatencySeconds() override          { return latencySamples / jmax (1.0, sampleRate); }
        void deinitialise() override                 {}

        std::vector<Section> getSections() const override
        {
            Section s { "Parameters", {} };
            for (int i = 0; i < jmin (16, numParams()); ++i) s.params.push_back (i);
            return { s };
        }

        bool isLoaded() const      { return loaded.load(); }
        bool hasCrashed() const
        {
            // the pipe only reports a dead helper on the next write, so also check the process itself
            if (! crashed && loaded && helperPid > 0 && ! CrashGuardWin::isProcessAlive (helperPid))
                crashed = true;
            return crashed.load();
        }
        int getLateBlocks() const  { return lateBlocks.load(); }
        String getStatus() const
        {
            if (crashed) return "The plugin crashed (Pulse kept running). Click Restart.";
            if (! loaded) return "Not loaded: " + loadError;
            return "Running in its own process" + (lateBlocks > 0 ? " - " + String (lateBlocks.load()) + " late blocks" : String());
        }

        void showEditor()
        {
            MemoryOutputStream out;
            out.writeByte ((char) sandbox::msgShowEditor);
            host.sendMessageToWorker (out.getMemoryBlock());
        }

        /** Relaunches the helper and reloads the plugin with its last saved state. */
        void restart()
        {
            host.killWorkerProcess();
            crashed = false;
            launchAndLoad();
        }

        /** For the self-test: makes the helper process crash. */
        void crashHelperForTest()
        {
            MemoryOutputStream out;
            out.writeByte ((char) sandbox::msgCrashForTest);
            host.sendMessageToWorker (out.getMemoryBlock());
        }

        void flushPluginStateToValueTree() override
        {
            PulsePlugin::flushPluginStateToValueTree();
            if (! loaded || crashed) return;
            if (auto reply = request (sandbox::msgGetState, {}, 5000))
            {
                MemoryInputStream in (*reply, false);
                in.readByte();
                MemoryBlock st;
                if (auto size = in.readInt(); size > 0) in.readIntoMemoryBlock (st, size);
                if (st.getSize() > 0) state.setProperty (stateID, st.toBase64Encoding(), nullptr);
            }
        }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            sampleRate = info.sampleRate;
            blockSize = info.blockSizeSamples;
            if (! loaded || crashed) return;
            MemoryOutputStream out;
            out.writeByte ((char) sandbox::msgPrepare);
            out.writeDouble (sampleRate);
            out.writeInt (blockSize);
            host.sendMessageToWorker (out.getMemoryBlock());
        }

        void applyToBuffer (const te::PluginRenderContext& fc) override
        {
            if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0) return;
            auto& b = *fc.destBuffer;
            const int n = fc.bufferNumSamples, start = fc.bufferStartSample;

            if (! loaded || crashed)
            {
                if (instrument) b.clear (start, n);   // effects pass audio through
                return;
            }

            MemoryOutputStream out ((size_t) (n * 8 + 256));
            out.writeByte ((char) sandbox::msgProcess);
            out.writeInt (n);
            out.writeInt (2);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    out.writeFloat (ch < b.getNumChannels() ? b.getSample (ch, start + i) : 0.0f);

            std::vector<std::pair<int, MidiMessage>> midi;
            if (auto* m = fc.bufferForMidiMessages)
                for (auto& msg : *m)
                    midi.push_back ({ jlimit (0, n - 1, roundToInt (msg.getTimeStamp() * sampleRate)), msg });
            out.writeInt ((int) midi.size());
            for (auto& [pos, msg] : midi)
            {
                out.writeInt (pos);
                out.writeInt (msg.getRawDataSize());
                out.write (msg.getRawData(), (size_t) msg.getRawDataSize());
            }

            // parameter changes made on our side (automation, knobs, macros)
            std::vector<std::pair<int, float>> changes;
            for (int i = 0; i < numParams(); ++i)
            {
                const float v = param (i)->getCurrentNormalisedValue();
                if (std::abs (v - sent[(size_t) i]) > 1.0e-6f) { sent[(size_t) i] = v; changes.push_back ({ i, v }); }
            }
            out.writeInt ((int) changes.size());
            for (auto& [i, v] : changes) { out.writeInt (i); out.writeFloat (v); }

            replyReady.reset();
            if (! host.sendMessageToWorker (out.getMemoryBlock())) { b.clear (start, n); return; }

            const int waitMs = jmax (20, (int) (4000.0 * n / sampleRate));
            if (! replyReady.wait (waitMs))
            {
                ++lateBlocks;
                if (instrument) b.clear (start, n);
                return;
            }

            const SpinLock::ScopedLockType sl (replyLock);
            MemoryInputStream in (reply, false);
            in.readByte();
            const int got = in.readInt();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < got; ++i)
                {
                    const float v = in.readFloat();
                    if (ch < b.getNumChannels() && i < n) b.setSample (ch, start + i, v);
                }
            for (int i = 0, num = in.readInt(); i < num; ++i)
            {
                const int idx = in.readInt();
                const float v = in.readFloat();
                if (isPositiveAndBelow (idx, numParams()))
                {
                    sent[(size_t) idx] = v;
                    pendingFromPlugin.push_back ({ idx, v });
                }
            }
        }

        /** Applies parameter moves made inside the plugin's own window (call on the message thread). */
        void syncParametersFromPlugin()
        {
            std::vector<std::pair<int, float>> moves;
            { const SpinLock::ScopedLockType sl (replyLock); moves.swap (pendingFromPlugin); }
            for (auto& [i, v] : moves)
                param (i)->setNormalisedParameter (v, dontSendNotification);
        }

    private:
        struct Host : public ChildProcessCoordinator
        {
            explicit Host (SandboxedPlugin& o) : owner (o) {}
            void handleMessageFromWorker (const MemoryBlock& mb) override { owner.messageFromWorker (mb); }
            void handleConnectionLost() override                       { owner.crashed = true; owner.replyReady.signal(); }
            SandboxedPlugin& owner;
        };

        void messageFromWorker (const MemoryBlock& mb)
        {
            if (mb.getSize() == 0) return;
            const auto type = (uint8) static_cast<const char*> (mb.getData())[0];
            if (type == sandbox::msgProcessed)
            {
                { const SpinLock::ScopedLockType sl (replyLock); reply = mb; }
                replyReady.signal();
            }
            else
            {
                const ScopedLock sl (requestLock);
                if (type == awaitedType) { requestReply = mb; requestDone.signal(); }
            }
        }

        std::optional<MemoryBlock> request (uint8 type, const MemoryBlock& payload, int timeoutMs, uint8 replyType = 0)
        {
            {
                const ScopedLock sl (requestLock);
                awaitedType = replyType != 0 ? replyType : (uint8) (type == sandbox::msgGetState ? sandbox::msgState : sandbox::msgLoaded);
                requestDone.reset();
            }
            MemoryOutputStream out;
            out.writeByte ((char) type);
            out.write (payload.getData(), payload.getSize());
            if (! host.sendMessageToWorker (out.getMemoryBlock())) return {};
            if (! requestDone.wait (timeoutMs)) return {};
            const ScopedLock sl (requestLock);
            return requestReply;
        }

        void launchAndLoad()
        {
            loaded = false;
            const auto exe = File::getSpecialLocation (File::currentExecutableFile);
            if (! host.launchWorkerProcess (exe, sandbox::commandLineID, 15000, 0))
            {
                loadError = "couldn't start the helper process";
                return;
            }

            MemoryOutputStream payload;
            payload.writeString (state[descID].toString());
            payload.writeString (state[fileID].toString());
            payload.writeDouble (sampleRate);
            payload.writeInt (blockSize);
            MemoryBlock st;
            st.fromBase64Encoding (state[stateID].toString());
            payload.writeInt ((int) st.getSize());
            payload.write (st.getData(), st.getSize());

            auto r = request (sandbox::msgLoad, payload.getMemoryBlock(), 40000);
            if (! r) { loadError = "the helper didn't answer"; return; }

            MemoryInputStream in (*r, false);
            in.readByte();
            const bool ok = in.readBool();
            helperPid = in.readInt();
            if (! ok) { loadError = in.readString(); return; }

            displayName = in.readString();
            state.setProperty (nameID, displayName, nullptr);
            const auto descXml = in.readString();
            if (descXml.isNotEmpty() && ! state.hasProperty (descID)) state.setProperty (descID, descXml, nullptr);
            latencySamples = in.readInt();
            instrument = in.readBool();
            state.setProperty (synthID, instrument, nullptr);

            const int count = in.readInt();
            if (numParams() == 0)   // first load: create the parameters (restarts reuse them)
                for (int i = 0; i < count; ++i)
                {
                    auto name = in.readString(), label = in.readString();
                    const float def = in.readFloat(), value = in.readFloat();
                    add ("p" + String (i), name.isNotEmpty() ? name : "Param " + String (i + 1), { 0.0f, 1.0f }, value,
                         [label] (float v) { return String (v, 3) + (label.isNotEmpty() ? " " + label : String()); });
                    ignoreUnused (def);
                }
            sent.assign ((size_t) numParams(), -1.0f);
            loaded = true;
        }

        Host host;
        String displayName, loadError;
        bool instrument = false;
        int latencySamples = 0;
        std::atomic<bool> loaded { false };
        mutable std::atomic<bool> crashed { false };
        int helperPid = 0;
        std::atomic<int> lateBlocks { 0 };
        std::vector<float> sent;
        std::vector<std::pair<int, float>> pendingFromPlugin;

        SpinLock replyLock;
        MemoryBlock reply;
        WaitableEvent replyReady;

        CriticalSection requestLock;
        uint8 awaitedType = 0;
        MemoryBlock requestReply;
        WaitableEvent requestDone;
    };
}

namespace sandbox
{
    inline bool enabledForNewPlugins (te::Engine& e) { return e.getPropertyStorage().getPropertiesFile().getBoolValue ("sandboxPlugins", false); }
    inline void setEnabledForNewPlugins (te::Engine& e, bool b) { e.getPropertyStorage().getPropertiesFile().setValue ("sandboxPlugins", b); }

    /** A new plugin from a description, sandboxed if that's switched on. */
    inline te::Plugin::Ptr createPlugin (te::Edit& edit, const PluginDescription& desc)
    {
        if (enabledForNewPlugins (edit.engine))
            return edit.getPluginCache().createNewPlugin (pulse::SandboxedPlugin::createState (&desc));
        return edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
    }

    /** Moves an in-process VST3 into a sandbox (keeping its settings). */
    inline te::Plugin::Ptr moveToSandbox (te::ExternalPlugin& p)
    {
        auto list = p.getOwnerList();
        if (list == nullptr) return {};
        MemoryBlock st;
        if (auto inst = p.getAudioPluginInstance()) inst->getStateInformation (st);
        const int index = list->indexOf (&p);
        auto np = list->insertPlugin (pulse::SandboxedPlugin::createState (&p.desc, {}, &st), index);
        if (np != nullptr) p.deleteFromParent();
        return np;
    }

    /** Moves a sandboxed plugin back into Pulse's process (keeping its settings). */
    inline te::Plugin::Ptr moveInProcess (pulse::SandboxedPlugin& s)
    {
        auto list = s.getOwnerList();
        if (list == nullptr) return {};
        s.flushPluginStateToValueTree();
        PluginDescription desc;
        if (auto x = parseXML (s.state[pulse::SandboxedPlugin::descID].toString())) desc.loadFromXml (*x);
        MemoryBlock st;
        st.fromBase64Encoding (s.state[pulse::SandboxedPlugin::stateID].toString());
        auto np = s.edit.getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);
        if (np == nullptr) return {};
        list->insertPlugin (np, list->indexOf (&s), nullptr);
        if (auto ext = dynamic_cast<te::ExternalPlugin*> (np.get()))
            if (auto inst = ext->getAudioPluginInstance(); inst != nullptr && st.getSize() > 0)
                inst->setStateInformation (st.getData(), (int) st.getSize());
        s.deleteFromParent();
        return np;
    }
}
