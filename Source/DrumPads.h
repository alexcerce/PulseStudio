#pragma once

// Included from PulseSynths.h (needs PulseSynth, SampleData, SVF).

/** Drum devices: the per-pad voice used inside a Drum Rack (sample layers or a synthesised
    drum, with choke groups), the pad mixer stage (volume / pan / mute / solo / two sends),
    and a standalone Drum Synth instrument. */
namespace pulse
{
    inline const Identifier drumSlotID { "pulsePad" };

    //==============================================================================
    /** State shared by all pads of one rack on the audio thread: who listens to which note,
        choke groups and solo. Each pad / output publishes its own slot every block. */
    struct DrumRackShared
    {
        static constexpr int numSlots = 130;   // 128 pads + 2 returns

        struct Slot
        {
            std::atomic<const void*> padOwner { nullptr }, outOwner { nullptr };
            std::atomic<int> note { -1 }, choke { 0 };
            std::atomic<bool> solo { false };
        };
        std::array<Slot, numSlots> slots;

        int chokeGroupOfNote (int note) const noexcept
        {
            for (auto& s : slots)
                if (s.note.load (std::memory_order_relaxed) == note)
                    if (const int c = s.choke.load (std::memory_order_relaxed); c > 0)
                        return c;
            return 0;
        }

        bool anySolo() const noexcept
        {
            for (int i = 0; i < 128; ++i)
                if (slots[(size_t) i].solo.load (std::memory_order_relaxed))
                    return true;
            return false;
        }

        static std::shared_ptr<DrumRackShared> forKey (const String& key)
        {
            static std::mutex m;
            static std::map<String, std::weak_ptr<DrumRackShared>> map;
            std::lock_guard<std::mutex> l (m);
            auto& w = map[key];
            auto s = w.lock();
            if (s == nullptr) { s = std::make_shared<DrumRackShared>(); w = s; }
            return s;
        }

        /** The shared block for the rack a plugin lives in (or a private one outside racks). */
        static std::shared_ptr<DrumRackShared> forPlugin (te::Plugin& p)
        {
            if (auto rack = p.getOwnerRackType())
                return forKey ("rack:" + rack->itemID.toString());
            return forKey ("plugin:" + p.itemID.toString());
        }
    };

    //==============================================================================
    /** A synthesised drum hit (808/909-flavoured analogue models). */
    struct DrumVoice
    {
        enum Type { kick, snare, closedHat, openHat, clap, tom, rim, cowbell, cymbal, numTypes };

        static const StringArray& typeNames()
        {
            static StringArray n { "Kick", "Snare", "Closed hat", "Open hat", "Clap", "Tom", "Rim", "Cowbell", "Cymbal" };
            return n;
        }

        /** Seconds to fade to -60 dB at 100% decay. */
        static float baseDecay (int t)
        {
            static const float d[] = { 0.55f, 0.25f, 0.07f, 0.55f, 0.3f, 0.5f, 0.06f, 0.35f, 1.8f };
            return d[jlimit (0, numTypes - 1, t)];
        }

        /** tune in semitones, decay as a multiplier (1 = default), tone / snap / drive 0..1. */
        void start (double sampleRate, int t, float tune, float decay, float tone_, float snap_, float drive_, uint32 seed)
        {
            sr = sampleRate;
            type = jlimit (0, numTypes - 1, t);
            T = std::pow (2.0f, tune / 12.0f);
            tone = tone_; snap = snap_; drive = drive_;
            tau = jmax (0.005f, baseDecay (type) * decay) / 6.9f;
            n = 0;
            noise = seed | 1u;
            for (auto& p : ph) p = 0;
            phase = 0;
            for (auto* f : { &f1, &f2 }) *f = {};
            switch (type)
            {
                case snare:     f1.set (1800.0f + tone * 3000.0f, 0.1f, sr); break;
                case closedHat:
                case openHat:   f1.set (7000.0f + tone * 5000.0f, 0.35f, sr); f2.set (6500.0f, 0.0f, sr); break;
                case clap:      f1.set (900.0f + tone * 1600.0f, 0.45f, sr); break;
                case rim:       f1.set (3500.0f + tone * 2000.0f, 0.5f, sr); break;
                case cowbell:   f1.set (850.0f + tone * 900.0f, 0.55f, sr); break;
                case cymbal:    f1.set (5000.0f + tone * 5000.0f, 0.25f, sr); f2.set (4000.0f, 0.0f, sr); break;
                default: break;
            }
            active = true;
        }

        float next() noexcept
        {
            if (! active) return 0.0f;
            const float t = (float) (n++ / sr);
            const float env = std::exp (-t / tau);
            float out = 0.0f;

            switch (type)
            {
                case kick:
                {
                    const float punch = 1.5f + snap * 5.0f;
                    const float f = 48.0f * T * (1.0f + punch * std::exp (-t / 0.028f));
                    phase += f / (float) sr;
                    phase -= std::floor (phase);
                    out = std::sin (MathConstants<float>::twoPi * phase) * env
                          + white() * std::exp (-t / 0.0018f) * tone * 0.6f;
                    break;
                }
                case tom:
                {
                    const float f = 105.0f * T * (1.0f + (0.4f + snap) * std::exp (-t / 0.05f));
                    phase += f / (float) sr;
                    phase -= std::floor (phase);
                    out = std::sin (MathConstants<float>::twoPi * phase) * env
                          + white() * std::exp (-t / 0.004f) * tone * 0.3f;
                    break;
                }
                case snare:
                {
                    const float body = (osc (0, 185.0f * T) * 0.6f + osc (1, 330.0f * T) * 0.4f) * std::exp (-t / (tau * 0.45f));
                    const float nz = f1.process (white(), 0, 2) * env;
                    out = body * (1.0f - tone * 0.5f) + nz * (0.35f + snap * 0.9f);
                    break;
                }
                case closedHat:
                case openHat:
                case cymbal:
                {
                    static const float ratios[] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                    float metal = 0;
                    for (int i = 0; i < 6; ++i) metal += square (i, ratios[i] * T * (type == cymbal ? 0.8f : 1.0f));
                    const float x = metal * (1.0f - snap * 0.6f) / 6.0f + white() * (0.15f + snap * 0.5f);
                    out = f2.process (f1.process (x, 0, 1), 1, 2) * env * 2.2f;
                    break;
                }
                case clap:
                {
                    float e = 0;
                    for (int b = 0; b < 3; ++b)
                        if (const float tb = t - 0.011f * (float) b; tb >= 0)
                            e = jmax (e, std::exp (-tb / 0.0035f));
                    if (t > 0.03f) e = jmax (e, 0.6f * std::exp (-(t - 0.03f) / tau));
                    out = f1.process (white(), 0, 1) * e * (1.6f + snap);
                    break;
                }
                case rim:
                    out = osc (0, 1700.0f * T) * std::exp (-t / 0.006f) * 0.7f
                          + f1.process (white(), 0, 1) * std::exp (-t / 0.0035f) * (0.6f + snap);
                    break;
                case cowbell:
                {
                    const float x = square (0, 540.0f * T) + square (1, 800.0f * T);
                    out = f1.process (x * 0.5f, 0, 1) * (0.25f + 0.75f * std::exp (-t / 0.012f)) * env * 1.6f;
                    break;
                }
                default: break;
            }

            if (drive > 0.001f)
            {
                const float k = 1.0f + drive * 9.0f;
                out = std::tanh (out * k) / std::tanh (k);
            }
            if (t > baseDecay (type) * 12.0f || (t > 0.05f && env < 0.0005f))
                active = false;
            return out;
        }

        bool active = false;

    private:
        float white() noexcept
        {
            noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
            return (float) (noise & 0xffffff) / (float) 0x7fffff - 1.0f;
        }
        float osc (int i, float f) noexcept
        {
            ph[(size_t) i] += f / (float) sr;
            ph[(size_t) i] -= std::floor (ph[(size_t) i]);
            return std::sin (MathConstants<float>::twoPi * ph[(size_t) i]);
        }
        float square (int i, float f) noexcept
        {
            ph[(size_t) i] += f / (float) sr;
            ph[(size_t) i] -= std::floor (ph[(size_t) i]);
            return ph[(size_t) i] < 0.5f ? 1.0f : -1.0f;
        }

        double sr = 44100.0;
        int type = 0;
        int64 n = 0;
        uint32 noise = 1;
        float T = 1, tone = 0.5f, snap = 0.5f, drive = 0, tau = 0.1f, phase = 0;
        std::array<float, 6> ph {};
        SVF f1, f2;
    };

    inline float hermite (const float* d, double p, int len) noexcept
    {
        const int i = (int) p;
        const float f = (float) (p - i);
        auto at = [d, len] (int k) { return d[jlimit (0, len - 1, k)]; };
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    //==============================================================================
    /** One Drum Rack pad: plays on its receive note only. The sound is either up to 8 sample
        layers (picked by velocity, round robin or at random) or a synthesised drum. Pads in the
        same choke group cut each other off. */
    class DrumPad : public PulseSynth
    {
    public:
        enum { pNote, pChoke, pSource, pLayerMode, pStart, pEnd, pReverse, pPlayMode, pTranspose, pFine,
               pA, pD, pS, pR, pFilterType, pCutoff, pReso, pVelFilter, pVelSens, pGain,
               pSynthType, pSynthTune, pSynthDecay, pSynthTone, pSynthSnap, pSynthDrive };
        static inline const char* xmlTypeName = "pulseDrumPad";
        static const char* getPluginName() { return "Drum Pad"; }
        static inline const Identifier layerType { "LAYER" }, fileID { "file" }, vloID { "vlo" }, vhiID { "vhi" };
        static inline const StringArray sourceNames { "Sample", "Synth" };
        static inline const StringArray layerModeNames { "Velocity", "Round robin", "Random", "Velocity + cycle" };
        static inline const StringArray filterNames { "Off", "Low pass", "Band pass", "High pass" };
        static constexpr int maxLayers = 8;

        explicit DrumPad (te::PluginCreationInfo info) : PulseSynth (info)
        {
            const int slot = jlimit (0, 127, (int) state.getProperty (drumSlotID, 36));
            add ("note", "Receive Note", { 0.0f, 127.0f, 1.0f }, (float) slot, [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); });
            add ("choke", "Choke Group", { 0.0f, 16.0f, 1.0f }, 0.0f, [] (float v) { return v < 0.5f ? String ("None") : String (roundToInt (v)); });
            add ("source", "Source", steps (2), 0.0f, fmt::choice (sourceNames));
            add ("layermode", "Layers", steps (layerModeNames.size()), 0.0f, fmt::choice (layerModeNames));
            add ("start", "Start", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("end", "End", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("reverse", "Reverse", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("playmode", "Play Mode", steps (2), 0.0f, fmt::choice ({ "One-shot", "Gate" }));
            add ("transpose", "Transpose", { -48.0f, 48.0f, 1.0f }, 0.0f, fmt::st);
            add ("fine", "Detune", { -100.0f, 100.0f }, 0.0f, fmt::ct);
            add ("a", "Attack", skewed (0.0f, 2000.0f, 50.0f), 0.0f, fmt::ms);
            add ("d", "Decay", skewed (1.0f, 10000.0f, 400.0f), 1000.0f, fmt::ms);
            add ("s", "Sustain", { 0.0f, 1.0f }, 1.0f, fmt::pct);
            add ("r", "Release", skewed (1.0f, 5000.0f, 200.0f), 50.0f, fmt::ms);
            add ("ftype", "Filter", steps (filterNames.size()), 0.0f, fmt::choice (filterNames));
            add ("cutoff", "Cutoff", skewed (30.0f, 20000.0f, 1500.0f), 20000.0f, fmt::hz);
            add ("reso", "Resonance", { 0.0f, 1.0f }, 0.1f, fmt::pct);
            add ("velfilter", "Vel > Filter", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("velsens", "Vel > Volume", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("gain", "Gain", { -36.0f, 12.0f }, 0.0f, fmt::db);
            add ("stype", "Drum", steps (DrumVoice::numTypes), 0.0f, fmt::choice (DrumVoice::typeNames()));
            add ("stune", "Tune", { -24.0f, 24.0f }, 0.0f, fmt::st);
            add ("sdecay", "Length", skewed (0.1f, 4.0f, 1.0f), 1.0f, fmt::pct);
            add ("stone", "Tone", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("ssnap", "Snap", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("sdrive", "Drive", { 0.0f, 1.0f }, 0.0f, fmt::pct);

            for (int i = 0; i < 8; ++i)
                synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
            synth.acceptNoteOn = [this] (int note) { return acceptNote (note); };
            synth.acceptNoteOff = [this] (int note) { return note == myNote; };

            slotIndex = slot;
            watcher.owner = this;
            state.addListener (&watcher);
            reloadLayers();
        }

        ~DrumPad() override
        {
            state.removeListener (&watcher);
            unpublish();
        }

        String getName() const override      { return "Drum Pad"; }
        String getPluginType() override      { return xmlTypeName; }
        int getDisplayWidth() const override  { return 0; }

        std::vector<Section> getSections() const override
        {
            return { { "Pad", { pNote, pChoke, pSource, pLayerMode } },
                     { "Sample", { pStart, pEnd, pReverse, pPlayMode } },
                     { "Pitch", { pTranspose, pFine } },
                     { "Envelope", { pA, pD, pS, pR } },
                     { "Filter", { pFilterType, pCutoff, pReso, pVelFilter } },
                     { "Output", { pVelSens, pGain } },
                     { "Synth", { pSynthType, pSynthTune, pSynthDecay, pSynthTone, pSynthSnap, pSynthDrive } } };
        }

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            PulseSynth::initialise (info);
            unpublish();
            shared = DrumRackShared::forPlugin (*this);
        }

        //==============================================================================
        // Layers (message thread; stored as LAYER children, undoable)
        int numLayers() const                 { return countLayers(); }
        File layerFile (int i) const          { return File (layerTree (i)[fileID].toString()); }
        Range<int> layerVelocity (int i) const
        {
            auto v = layerTree (i);
            return { (int) v.getProperty (vloID, 1), (int) v.getProperty (vhiID, 127) };
        }

        /** Replaces all layers with one sample and switches the pad to sample mode. */
        void setSample (const File& f)
        {
            clearLayers();
            addLayer (f);
            set (pSource, 0.0f);
        }

        bool addLayer (const File& f, int vlo = 1, int vhi = 127)
        {
            if (numLayers() >= maxLayers) return false;
            ValueTree v (layerType);
            v.setProperty (fileID, f.getFullPathName(), nullptr);
            v.setProperty (vloID, vlo, nullptr);
            v.setProperty (vhiID, vhi, nullptr);
            state.appendChild (v, getUndoManager());
            set (pSource, 0.0f);
            return true;
        }

        void removeLayer (int i)
        {
            auto v = layerTree (i);
            if (v.isValid()) state.removeChild (v, getUndoManager());
        }

        void clearLayers()
        {
            for (int i = state.getNumChildren(); --i >= 0;)
                if (state.getChild (i).hasType (layerType))
                    state.removeChild (i, getUndoManager());
        }

        void setLayerVelocity (int i, int lo, int hi)
        {
            auto v = layerTree (i);
            if (! v.isValid()) return;
            v.setProperty (vloID, jlimit (1, 127, jmin (lo, hi)), getUndoManager());
            v.setProperty (vhiID, jlimit (1, 127, jmax (lo, hi)), getUndoManager());
        }

        /** Splits the velocity range evenly across the layers (layer 1 = softest). */
        void spreadVelocities()
        {
            const int n = numLayers();
            for (int i = 0; i < n; ++i)
                setLayerVelocity (i, 1 + i * 127 / n, (i + 1) * 127 / n);
        }

        void setSynth (int drumType)
        {
            set (pSource, 1.0f);
            set (pSynthType, (float) drumType);
        }

        bool isSynth() const          { return getInt (pSource) == 1; }
        bool isEmpty() const          { return ! isSynth() && numLayers() == 0; }

        String displayName() const
        {
            if (isSynth()) return DrumVoice::typeNames()[getInt (pSynthType)] + " (synth)";
            const int n = numLayers();
            if (n == 0) return {};
            return layerFile (0).getFileNameWithoutExtension() + (n > 1 ? " +" + String (n - 1) : String());
        }

        struct LoadedLayer { std::shared_ptr<SampleData> data; int vlo = 1, vhi = 127; };
        using LayerSet = std::vector<LoadedLayer>;

        std::shared_ptr<const LayerSet> getLayers() const
        {
            const SpinLock::ScopedLockType sl (layerLock);
            return layers;
        }

        /** Last played layer and a normalised playhead (for the display; -1 = idle). */
        std::atomic<int> lastLayer { 0 };
        std::atomic<float> playhead { -1.0f };
        std::atomic<int> triggerCount { 0 }, layersVersion { 0 };

        /** Picks a layer index for a velocity 1..127 (audio thread). */
        int pickLayer (int vel)
        {
            auto& ls = audioLayers;
            const int n = ls != nullptr ? (int) ls->size() : 0;
            if (n == 0) return -1;
            const int mode = getInt (pLayerMode);
            if (mode == 1) return (int) (rr++ % (uint32) n);
            if (mode == 2) return rng.nextInt (n);

            int matches[maxLayers], m = 0;
            for (int i = 0; i < n; ++i)
                if (vel >= (*ls)[(size_t) i].vlo && vel <= (*ls)[(size_t) i].vhi)
                    matches[m++] = i;
            if (m == 0)   // nothing covers it: nearest range
            {
                int best = 0, bestD = 1000;
                for (int i = 0; i < n; ++i)
                {
                    const int d = jmin (std::abs (vel - (*ls)[(size_t) i].vlo), std::abs (vel - (*ls)[(size_t) i].vhi));
                    if (d < bestD) { bestD = d; best = i; }
                }
                return best;
            }
            return mode == 3 ? matches[rr++ % (uint32) m] : matches[0];
        }

    private:
        int countLayers() const
        {
            int n = 0;
            for (auto c : state) if (c.hasType (layerType)) ++n;
            return n;
        }

        ValueTree layerTree (int i) const
        {
            for (auto c : state)
                if (c.hasType (layerType) && i-- == 0)
                    return c;
            return {};
        }

        struct Watcher : public ValueTree::Listener
        {
            DrumPad* owner = nullptr;
            void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
            {
                if (v == owner->state && id == drumSlotID)
                {
                    owner->unpublish();
                    owner->slotIndex = jlimit (0, 129, (int) v.getProperty (drumSlotID));
                }
                else if (v.hasType (layerType) && v.getParent() == owner->state)
                    owner->reloadLayers();
            }
            void valueTreeChildAdded (ValueTree& p, ValueTree& c) override   { if (p == owner->state && c.hasType (layerType)) owner->reloadLayers(); }
            void valueTreeChildRemoved (ValueTree& p, ValueTree& c, int) override { if (p == owner->state && c.hasType (layerType)) owner->reloadLayers(); }
        } watcher;

        void reloadLayers()
        {
            auto old = getLayers();
            auto set = std::make_shared<LayerSet>();
            for (int i = 0; i < numLayers(); ++i)
            {
                LoadedLayer l;
                const auto f = layerFile (i);
                const auto vel = layerVelocity (i);
                l.vlo = vel.getStart(); l.vhi = vel.getEnd();
                if (old != nullptr)   // reuse audio already in memory
                    for (auto& o : *old)
                        if (o.data != nullptr && o.data->file == f) { l.data = o.data; break; }
                if (l.data == nullptr && f.existsAsFile())
                    l.data = SampleData::load (engine, f);
                if (l.data != nullptr) set->push_back (l);
            }
            {
                const SpinLock::ScopedLockType sl (layerLock);
                layers = set;
            }
            ++layersVersion;
        }

        void unpublish()
        {
            if (shared == nullptr) return;
            for (auto& s : shared->slots)
                if (s.padOwner.load() == this)
                {
                    s.padOwner = nullptr;
                    s.note = -1;
                    s.choke = 0;
                }
        }

        void beforeBlock() override
        {
            {
                const SpinLock::ScopedTryLockType sl (layerLock);
                if (sl.isLocked()) audioLayers = layers;
            }
            myNote = getInt (pNote);
            myChoke = getInt (pChoke);
            if (shared != nullptr)
            {
                auto& s = shared->slots[(size_t) jlimit (0, DrumRackShared::numSlots - 1, slotIndex.load())];
                s.padOwner = this;
                s.note.store (myNote, std::memory_order_relaxed);
                s.choke.store (myChoke, std::memory_order_relaxed);
            }
        }

        struct Voice : public SynthesiserVoice
        {
            explicit Voice (DrumPad& p) : o (p) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }

            void startNote (int, float vel, SynthesiserSound*, int) override
            {
                velocity = vel;
                choking = false;
                chokeGain = 1.0f;
                releasing = false;
                synthMode = o.getInt (pSource) == 1;
                filter.reset();
                amp.setSampleRate (getSampleRate());
                amp.reset();

                if (synthMode)
                {
                    drum.start (getSampleRate(), o.getInt (pSynthType), o.get (pSynthTune) + o.get (pTranspose) + o.get (pFine) / 100.0f,
                                o.get (pSynthDecay), o.get (pSynthTone), o.get (pSynthSnap), o.get (pSynthDrive), (uint32) o.rng.nextInt());
                }
                else
                {
                    const int li = o.pickLayer (jlimit (1, 127, roundToInt (vel * 127.0f)));
                    data = li >= 0 ? (*o.audioLayers)[(size_t) li].data : nullptr;
                    if (data == nullptr || data->buffer.getNumSamples() < 2) { clearCurrentNote(); return; }
                    o.lastLayer = li;
                    reverse = o.getInt (pReverse) > 0;
                    const int len = data->buffer.getNumSamples();
                    s0 = jlimit (0.0, (double) len - 2, (double) jmin (o.get (pStart), o.get (pEnd)) * len);
                    s1 = jlimit (s0 + 1, (double) len - 1, (double) jmax (o.get (pStart), o.get (pEnd)) * len);
                    pos = reverse ? s1 : s0;
                }
                amp.noteOn();
            }

            void stopNote (float, bool allowTailOff) override
            {
                if (! allowTailOff) { finish(); return; }
                if (o.getInt (pPlayMode) == 0) return;   // one-shot plays out
                amp.noteOff();
                releasing = true;
            }

            void choke() { if (isVoiceActive()) choking = true; }

            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}

            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;

                const bool oneShot = o.getInt (pPlayMode) == 0;
                amp.setParameters (adsrFrom (jmax (0.05f, o.get (pA)), o.get (pD), o.get (pS), o.get (pR)));
                const int ftype = o.getInt (pFilterType);
                if (ftype > 0)
                {
                    const float cut = o.get (pCutoff) * std::pow (2.0f, -o.get (pVelFilter) * (1.0f - velocity) * 5.0f);
                    filter.set (cut, o.get (pReso), getSampleRate());
                }
                const float gain = Decibels::decibelsToGain (o.get (pGain)) * (1.0f - o.get (pVelSens) * (1.0f - velocity));
                const float chokeStep = 1.0f / (float) (0.004 * getSampleRate());

                const float* L = nullptr; const float* R = nullptr;
                int len = 0;
                double inc = 0;
                if (! synthMode)
                {
                    L = data->buffer.getReadPointer (0);
                    R = data->buffer.getReadPointer (1);
                    len = data->buffer.getNumSamples();
                    inc = std::pow (2.0, (o.get (pTranspose) + o.get (pFine) / 100.0) / 12.0) * data->sampleRate / getSampleRate();
                }

                for (int i = 0; i < num; ++i)
                {
                    float l, r;
                    if (synthMode)
                    {
                        l = r = drum.next() * 0.9f;
                        if (! drum.active && ! releasing) { amp.noteOff(); releasing = true; }
                    }
                    else
                    {
                        const bool atEnd = reverse ? pos <= s0 : pos >= s1;
                        if (atEnd) { finish(); return; }
                        l = hermite (L, pos, len);
                        r = hermite (R, pos, len);
                        pos += reverse ? -inc : inc;
                        // one-shot: fade the last 2 ms so a shortened sample doesn't click
                        if (oneShot && ! releasing && (reverse ? pos - s0 : s1 - pos) < inc * 0.002 * getSampleRate())
                        {
                            amp.setParameters (adsrFrom (1.0f, 1.0f, 1.0f, 2.0f));
                            amp.noteOff();
                            releasing = true;
                        }
                    }

                    if (ftype > 0)
                    {
                        const int mode = ftype == 1 ? 0 : ftype == 2 ? 1 : 2;
                        l = filter.process (l, 0, mode);
                        r = filter.process (r, 1, mode);
                    }

                    float e = amp.getNextSample() * gain;
                    if (choking)
                    {
                        chokeGain -= chokeStep;
                        if (chokeGain <= 0.0f) { finish(); return; }
                        e *= chokeGain;
                    }
                    out.addSample (0, start + i, l * e);
                    if (out.getNumChannels() > 1) out.addSample (1, start + i, r * e);

                    if (! amp.isActive()) { finish(); return; }
                }
                if (! synthMode && len > 0) o.playhead.store ((float) (pos / len));
            }

            void finish()
            {
                clearCurrentNote();
                amp.reset();
                data.reset();
                o.playhead.store (-1.0f);
            }

            DrumPad& o;
            std::shared_ptr<SampleData> data;
            DrumVoice drum;
            double pos = 0, s0 = 0, s1 = 0;
            float velocity = 1, chokeGain = 1;
            bool releasing = false, reverse = false, synthMode = false, choking = false;
            ADSR amp;
            SVF filter;
        };

        /** Called for every note-on the rack sends (audio thread). */
        bool acceptNote (int note)
        {
            if (note != myNote)
            {
                if (myChoke > 0 && shared != nullptr && shared->chokeGroupOfNote (note) == myChoke)
                    chokeAll();
                return false;
            }
            if (myChoke > 0) chokeAll();   // a pad in a choke group is monophonic
            ++triggerCount;
            return true;
        }

        void chokeAll()
        {
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto v = dynamic_cast<Voice*> (synth.getVoice (i)))
                    v->choke();
        }

        std::shared_ptr<DrumRackShared> shared;
        std::atomic<int> slotIndex { 36 };
        int myNote = 36, myChoke = 0;
        uint32 rr = 0;
        Random rng;
        mutable SpinLock layerLock;
        std::shared_ptr<const LayerSet> layers, audioLayers;
    };

    //==============================================================================
    /** End of a Drum Rack pad chain: volume, pan, mute, solo and two post-fader sends to the
        rack's returns. Outputs 6 channels: main L/R, send A L/R, send B L/R. */
    class PadOut : public PulsePlugin
    {
    public:
        enum { pVol, pPan, pMute, pSolo, pSendA, pSendB };
        static inline const char* xmlTypeName = "pulsePadOut";
        static const char* getPluginName() { return "Pad Mixer"; }

        explicit PadOut (te::PluginCreationInfo info) : PulsePlugin (info)
        {
            auto dbText = [] (float v) { return v <= -69.5f ? String ("-inf dB") : fmt::db (v); };
            add ("vol", "Volume", skewed (-70.0f, 6.0f, -12.0f), 0.0f, dbText);
            add ("pan", "Pan", { -1.0f, 1.0f }, 0.0f, [] (float v) { return std::abs (v) < 0.01f ? String ("C") : String (roundToInt (std::abs (v) * 50)) + (v < 0 ? "L" : "R"); });
            add ("mute", "Mute", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("solo", "Solo", steps (2), 0.0f, fmt::choice ({ "Off", "On" }));
            add ("senda", "Send A", skewed (-70.0f, 6.0f, -12.0f), -70.0f, dbText);
            add ("sendb", "Send B", skewed (-70.0f, 6.0f, -12.0f), -70.0f, dbText);
            slotIndex = jlimit (0, 129, (int) state.getProperty (drumSlotID, 36));
            watcher.owner = this;
            state.addListener (&watcher);
        }

        ~PadOut() override
        {
            state.removeListener (&watcher);
            unpublish();
        }

        String getName() const override      { return "Pad Mixer"; }
        String getPluginType() override      { return xmlTypeName; }
        std::vector<Section> getSections() const override { return { { "Mixer", { pVol, pPan, pMute, pSolo, pSendA, pSendB } } }; }

        int getNumOutputChannelsGivenInputs (int) override  { return 6; }
        BusLayout getBusses() const override                 { return BusLayout::singleStereoInOut(); }
        void getChannelNames (StringArray* ins, StringArray* outs) override
        {
            if (ins != nullptr)  ins->addArray ({ "Left", "Right" });
            if (outs != nullptr) outs->addArray ({ "Left", "Right", "Send A Left", "Send A Right", "Send B Left", "Send B Right" });
        }
        void deinitialise() override {}

        void initialise (const te::PluginInitialisationInfo& info) override
        {
            sampleRate = info.sampleRate;
            unpublish();
            shared = DrumRackShared::forPlugin (*this);
        }

        bool isReturn() const { return slotIndex.load() >= 128; }

        /** Peak level since the last read (for meters). */
        float readPeak() { return peak.exchange (0.0f); }

        void applyToBuffer (const te::PluginRenderContext& fc) override
        {
            if (fc.destBuffer == nullptr || fc.bufferNumSamples <= 0) return;
            auto& b = *fc.destBuffer;
            const int n = fc.bufferNumSamples, s0 = fc.bufferStartSample;
            if (b.getNumChannels() < 2) return;

            const int slot = jlimit (0, DrumRackShared::numSlots - 1, slotIndex.load());
            const bool solo = getInt (pSolo) > 0;
            if (shared != nullptr)
            {
                auto& s = shared->slots[(size_t) slot];
                s.outOwner = this;
                s.solo.store (solo && slot < 128, std::memory_order_relaxed);
            }
            const bool silenced = getInt (pMute) > 0 || (slot < 128 && shared != nullptr && shared->anySolo() && ! solo);

            const float vol = silenced || get (pVol) <= -69.5f ? 0.0f : Decibels::decibelsToGain (get (pVol));
            const float pan = get (pPan);
            const float gl = vol * std::sqrt (0.5f * (1.0f - pan)) * 1.4142f, gr = vol * std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
            auto sendGain = [this, silenced] (int p) { return silenced || get (p) <= -69.5f ? 0.0f : Decibels::decibelsToGain (get (p)); };
            const float ga = slot < 128 ? sendGain (pSendA) : 0.0f, gb = slot < 128 ? sendGain (pSendB) : 0.0f;

            float* L = b.getWritePointer (0, s0);
            float* R = b.getWritePointer (1, s0);
            const bool sends = b.getNumChannels() >= 6;
            float* AL = sends ? b.getWritePointer (2, s0) : nullptr;
            float* AR = sends ? b.getWritePointer (3, s0) : nullptr;
            float* BL = sends ? b.getWritePointer (4, s0) : nullptr;
            float* BR = sends ? b.getWritePointer (5, s0) : nullptr;

            float pk = 0;
            for (int i = 0; i < n; ++i)
            {
                const float t = (float) (i + 1) / (float) n;   // ramp from the last block's gains (no zipper noise)
                const float l = L[i] * (lastL + (gl - lastL) * t), r = R[i] * (lastR + (gr - lastR) * t);
                L[i] = l; R[i] = r;
                pk = jmax (pk, std::abs (l), std::abs (r));
                if (sends)
                {
                    const float a = lastA + (ga - lastA) * t, bb = lastB + (gb - lastB) * t;
                    AL[i] = l * a; AR[i] = r * a;
                    BL[i] = l * bb; BR[i] = r * bb;
                }
            }
            lastL = gl; lastR = gr; lastA = ga; lastB = gb;
            if (pk > peak.load()) peak.store (pk);
            te::sanitiseValues (b, s0, n, 4.0f);
        }

    private:
        struct Watcher : public ValueTree::Listener
        {
            PadOut* owner = nullptr;
            void valueTreePropertyChanged (ValueTree& v, const Identifier& id) override
            {
                if (v == owner->state && id == drumSlotID)
                {
                    owner->unpublish();
                    owner->slotIndex = jlimit (0, 129, (int) v.getProperty (drumSlotID));
                }
            }
        } watcher;

        void unpublish()
        {
            if (shared == nullptr) return;
            for (auto& s : shared->slots)
                if (s.outOwner.load() == this)
                {
                    s.outOwner = nullptr;
                    s.solo = false;
                }
        }

        std::shared_ptr<DrumRackShared> shared;
        std::atomic<int> slotIndex { 36 };
        std::atomic<float> peak { 0.0f };
        float lastL = 0, lastR = 0, lastA = 0, lastB = 0;
    };

    //==============================================================================
    /** Standalone synthesised drum kit: General MIDI note map (36 kick, 38 snare, 42 hat...),
        or one drum played chromatically. */
    class DrumSynth : public PulseSynth
    {
    public:
        enum { pMap, pTune, pDecay, pTone, pSnap, pDrive, pVelSens, pGain };
        static inline const char* xmlTypeName = "pulseDrumSynth";
        static const char* getPluginName() { return "Drum Synth"; }

        static StringArray mapNames()
        {
            StringArray n { "GM kit" };
            for (auto& t : DrumVoice::typeNames()) n.add (t + " (chromatic)");
            return n;
        }

        /** GM drum note -> {drum type, tune offset}; type -1 = not mapped. */
        static std::pair<int, float> gmVoice (int note)
        {
            switch (note)
            {
                case 35: case 36: return { DrumVoice::kick, 0.0f };
                case 37:          return { DrumVoice::rim, 0.0f };
                case 38:          return { DrumVoice::snare, 0.0f };
                case 40:          return { DrumVoice::snare, 1.0f };
                case 39:          return { DrumVoice::clap, 0.0f };
                case 42:          return { DrumVoice::closedHat, 0.0f };
                case 44:          return { DrumVoice::closedHat, -1.0f };
                case 46:          return { DrumVoice::openHat, 0.0f };
                case 41:          return { DrumVoice::tom, -5.0f };
                case 43:          return { DrumVoice::tom, -3.0f };
                case 45:          return { DrumVoice::tom, 0.0f };
                case 47:          return { DrumVoice::tom, 2.0f };
                case 48:          return { DrumVoice::tom, 4.0f };
                case 50:          return { DrumVoice::tom, 7.0f };
                case 49: case 52: case 55: case 57: return { DrumVoice::cymbal, 0.0f };
                case 51: case 53: case 59: return { DrumVoice::cymbal, 5.0f };
                case 56:          return { DrumVoice::cowbell, 0.0f };
                default:          return { -1, 0.0f };
            }
        }

        explicit DrumSynth (te::PluginCreationInfo info) : PulseSynth (info)
        {
            add ("map", "Mode", steps (mapNames().size()), 0.0f, fmt::choice (mapNames()));
            add ("tune", "Tune", { -24.0f, 24.0f }, 0.0f, fmt::st);
            add ("decay", "Length", skewed (0.1f, 4.0f, 1.0f), 1.0f, fmt::pct);
            add ("tone", "Tone", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("snap", "Snap", { 0.0f, 1.0f }, 0.5f, fmt::pct);
            add ("drive", "Drive", { 0.0f, 1.0f }, 0.0f, fmt::pct);
            add ("velsens", "Velocity", { 0.0f, 1.0f }, 0.6f, fmt::pct);
            add ("gain", "Gain", { -36.0f, 12.0f }, 0.0f, fmt::db);
            for (int i = 0; i < 16; ++i)
                synth.addVoice (new Voice (*this));
            synth.addSound (new AnySound());
            synth.setNoteStealingEnabled (true);
        }

        String getName() const override      { return "Drum Synth"; }
        String getPluginType() override      { return xmlTypeName; }
        std::vector<Section> getSections() const override
        {
            return { { "Kit", { pMap, pTune, pDecay } }, { "Character", { pTone, pSnap, pDrive } }, { "Output", { pVelSens, pGain } } };
        }

    private:
        struct Voice : public SynthesiserVoice
        {
            explicit Voice (DrumSynth& s) : o (s) {}
            bool canPlaySound (SynthesiserSound*) override { return true; }
            void startNote (int note, float vel, SynthesiserSound*, int) override
            {
                int type = 0;
                float tune = 0.0f;
                if (o.getInt (pMap) == 0)
                {
                    std::tie (type, tune) = gmVoice (note);
                    if (type < 0) { clearCurrentNote(); return; }
                }
                else
                {
                    type = o.getInt (pMap) - 1;
                    tune = (float) (note - 60);
                }
                drum.start (getSampleRate(), type, tune + o.get (pTune), o.get (pDecay), o.get (pTone), o.get (pSnap), o.get (pDrive),
                            (uint32) o.rng.nextInt());
                gain = Decibels::decibelsToGain (o.get (pGain)) * (1.0f - o.get (pVelSens) * (1.0f - vel)) * 0.9f;
            }
            void stopNote (float, bool allowTailOff) override { if (! allowTailOff) { drum.active = false; clearCurrentNote(); } }
            void pitchWheelMoved (int) override {}
            void controllerMoved (int, int) override {}
            void renderNextBlock (AudioBuffer<float>& out, int start, int num) override
            {
                if (! isVoiceActive()) return;
                for (int i = 0; i < num; ++i)
                {
                    const float v = drum.next() * gain;
                    out.addSample (0, start + i, v);
                    if (out.getNumChannels() > 1) out.addSample (1, start + i, v);
                    if (! drum.active) { clearCurrentNote(); return; }
                }
            }
            DrumSynth& o;
            DrumVoice drum;
            float gain = 1;
        };

        Random rng;
    };
}
