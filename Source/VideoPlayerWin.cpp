// The project's video, played by Windows' own Media Foundation (the "media engine" that also plays video in
// the system's apps), so MP4 and the other usual formats work with nothing else installed. Kept out of the
// JUCE headers because it needs <windows.h> and the Media Foundation headers.
#include <JuceHeader.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfmediaengine.h>
#include <mferror.h>
#include <wincodec.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>

#pragma comment (lib, "mfplat.lib")
#pragma comment (lib, "mfuuid.lib")
#pragma comment (lib, "ole32.lib")
#pragma comment (lib, "oleaut32.lib")
#pragma comment (lib, "windowscodecs.lib")

namespace VideoPlayerWin
{
    /** What the media engine tells us, from its own threads: only flags are kept. */
    struct Notify : public IMFMediaEngineNotify
    {
        std::atomic<long> refs { 1 };
        std::atomic<bool> ready { false }, failed { false }, ended { false };
        std::atomic<int> errorCode { 0 }, seen { 0 }, last { -1 };
        int order[16] {}, params[16] {};

        STDMETHODIMP QueryInterface (REFIID iid, void** out) override
        {
            if (iid == __uuidof (IUnknown) || iid == __uuidof (IMFMediaEngineNotify)) { *out = static_cast<IMFMediaEngineNotify*> (this); AddRef(); return S_OK; }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override  { return (ULONG) ++refs; }
        STDMETHODIMP_(ULONG) Release() override { const long n = --refs; if (n == 0) delete this; return (ULONG) n; }

        STDMETHODIMP EventNotify (DWORD event, DWORD_PTR param1, DWORD) override
        {
            { const int n = seen.fetch_add (1); if (n < 16) { order[n] = (int) event; params[n] = (int) param1; } }
            last = (int) event;
            // the engine waits, before it starts to load, until told that the caller has finished what it was doing: it has
            if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE && param1 != 0) SetEvent ((HANDLE) param1);
            if (event == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA) ready = true;
            if (event == MF_MEDIA_ENGINE_EVENT_ERROR)          { errorCode = (int) param1; failed = true; }
            if (event == MF_MEDIA_ENGINE_EVENT_ENDED)          ended = true;
            if (event == MF_MEDIA_ENGINE_EVENT_PLAYING || event == MF_MEDIA_ENGINE_EVENT_SEEKED) ended = false;
            return S_OK;
        }
        virtual ~Notify() = default;
    };

    static int probeMode = 2;   // (for --video-probe: 0 a hidden window, 1 a visible window, 2 as shipped: no window, the frames are handed over, 3 sound only)

    struct Player
    {
        HWND window = nullptr;
        IMFMediaEngine* engine = nullptr;
        Notify* notify = nullptr;
        BSTR url = nullptr;
        IWICImagingFactory* wic = nullptr;
        IWICBitmap* bitmap = nullptr;
        UINT bitmapW = 0, bitmapH = 0;
        bool started = false, comStarted = false;
        juce::String problem;

        // The engine is made on a thread of its own that lives as long as the player: made on the window's own
        // thread it never starts to load (it waits there for something that thread never gives it).
        std::thread worker;
        std::mutex lock;
        std::condition_variable wake;
        std::function<void()> job;
        bool jobWaiting = false, quitting = false;

        void run (std::function<void()> f)
        {
            std::unique_lock<std::mutex> l (lock);
            job = std::move (f);
            jobWaiting = true;
            wake.notify_all();
            // while waiting, this thread keeps answering Windows: the engine talks to the picture's window, which
            // belongs to this thread, and would wait for ever for an answer otherwise
            while (! wake.wait_for (l, std::chrono::milliseconds (4), [this] { return ! jobWaiting; }))
            {
                l.unlock();
                MSG msg;
                while (PeekMessageW (&msg, window, 0, 0, PM_REMOVE)) { TranslateMessage (&msg); DispatchMessageW (&msg); }
                l.lock();
            }
        }

        Player()
        {
            // a plain black window of our own for the picture (it is put inside the video window by the caller)
            window = CreateWindowExW (0, L"STATIC", L"", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW (nullptr), nullptr);
            worker = std::thread ([this]
            {
                comStarted = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
                started = SUCCEEDED (MFStartup (MF_VERSION));
                std::unique_lock<std::mutex> l (lock);
                for (;;)
                {
                    wake.wait (l, [this] { return jobWaiting || quitting; });
                    if (jobWaiting) { job(); job = nullptr; jobWaiting = false; wake.notify_all(); }
                    else if (quitting) break;
                }
                l.unlock();
                if (started) MFShutdown();
                if (comStarted) CoUninitialize();
            });
            run ([] {});   // (wait until the thread is ready)
        }

        ~Player()
        {
            close();
            { std::lock_guard<std::mutex> l (lock); quitting = true; }
            wake.notify_all();
            worker.join();
            if (bitmap != nullptr) bitmap->Release();
            if (wic != nullptr) wic->Release();
            if (window != nullptr) DestroyWindow (window);
        }

        void close()
        {
            run ([this]
            {
                if (engine != nullptr) { engine->Shutdown(); engine->Release(); engine = nullptr; }
                if (notify != nullptr) { notify->Release(); notify = nullptr; }
                if (url != nullptr) { SysFreeString (url); url = nullptr; }
            });
        }

        bool open (const juce::String& path)
        {
            close();
            bool ok = false;
            run ([this, &ok, path] { ok = openOnWorker (path); });
            return ok;
        }

        bool openOnWorker (const juce::String& path)
        {
            auto close = [this]
            {
                if (engine != nullptr) { engine->Shutdown(); engine->Release(); engine = nullptr; }
                if (notify != nullptr) { notify->Release(); notify = nullptr; }
                if (url != nullptr) { SysFreeString (url); url = nullptr; }
            };
            problem.clear();
            if (! started || window == nullptr) { problem = "Windows' media player (Media Foundation) is not available on this computer."; return false; }
            IMFMediaEngineClassFactory* factory = nullptr;
            IMFAttributes* attributes = nullptr;
            HRESULT hr = CoCreateInstance (CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_ALL, IID_PPV_ARGS (&factory));
            if (SUCCEEDED (hr)) hr = MFCreateAttributes (&attributes, 2);
            notify = new Notify();
            if (SUCCEEDED (hr)) hr = attributes->SetUnknown (MF_MEDIA_ENGINE_CALLBACK, notify);
            if (probeMode == 2)      { if (SUCCEEDED (hr)) hr = attributes->SetUINT32 (MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, 87); }
            else if (probeMode != 3) { if (SUCCEEDED (hr)) hr = attributes->SetUINT64 (MF_MEDIA_ENGINE_PLAYBACK_HWND, (UINT64) (UINT_PTR) window); }
            if (SUCCEEDED (hr)) hr = factory->CreateInstance (probeMode == 3 ? MF_MEDIA_ENGINE_AUDIOONLY : 0, attributes, &engine);
            if (attributes != nullptr) attributes->Release();
            if (factory != nullptr) factory->Release();
            if (FAILED (hr) || engine == nullptr) { problem = "Windows could not start its media player (error " + juce::String::toHexString ((int) hr) + ")."; close(); return false; }
            url = SysAllocString (path.toWideCharPointer());   // (kept until the video is closed: the engine reads it later, from its own threads)
            hr = engine->SetSource (url);
            if (SUCCEEDED (hr)) hr = engine->Load();
            if (FAILED (hr)) { problem = "Windows could not open this video (error " + juce::String::toHexString ((int) hr) + ")."; close(); return false; }
            engine->SetMuted (TRUE);
            return true;
        }

        /** The picture as it is now, when there is a new one since the last time (the caller draws it). */
        bool frame (juce::Image& into)
        {
            if (engine == nullptr || notify == nullptr || ! notify->ready.load()) return false;
            LONGLONG when = 0;
            if (engine->OnVideoStreamTick (&when) != S_OK) return false;   // nothing new
            DWORD w = 0, h = 0;
            if (FAILED (engine->GetNativeVideoSize (&w, &h)) || w == 0 || h == 0 || w > 16384 || h > 16384) return false;
            // no bigger than a screen needs: a very large film is made smaller as it is handed over, which costs far less than copying it whole
            const double shrink = juce::jmin (1.0, 1920.0 / w, 1200.0 / h);
            w = (DWORD) juce::jmax (2, juce::roundToInt (w * shrink));
            h = (DWORD) juce::jmax (2, juce::roundToInt (h * shrink));
            if (wic == nullptr && FAILED (CoCreateInstance (CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&wic)))) return false;
            if (bitmap == nullptr || bitmapW != w || bitmapH != h)
            {
                if (bitmap != nullptr) { bitmap->Release(); bitmap = nullptr; }
                if (FAILED (wic->CreateBitmap (w, h, GUID_WICPixelFormat32bppBGRA, WICBitmapCacheOnDemand, &bitmap))) return false;
                bitmapW = w; bitmapH = h;
            }
            const RECT whole { 0, 0, (LONG) w, (LONG) h };
            const MFARGB black { 0, 0, 0, 255 };
            if (FAILED (engine->TransferVideoFrame (bitmap, nullptr, &whole, &black))) return false;
            IWICBitmapLock* locked = nullptr;
            const WICRect all { 0, 0, (INT) w, (INT) h };
            if (FAILED (bitmap->Lock (&all, WICBitmapLockRead, &locked)) || locked == nullptr) return false;
            UINT size = 0, stride = 0;
            BYTE* data = nullptr;
            const bool ok = SUCCEEDED (locked->GetStride (&stride)) && SUCCEEDED (locked->GetDataPointer (&size, &data)) && data != nullptr;
            if (ok)
            {
                if (into.getWidth() != (int) w || into.getHeight() != (int) h || into.getFormat() != juce::Image::RGB) into = juce::Image (juce::Image::RGB, (int) w, (int) h, false);
                juce::Image::BitmapData dest (into, juce::Image::BitmapData::writeOnly);
                for (int y = 0; y < (int) h; ++y)
                {
                    const BYTE* src = data + (size_t) y * stride;
                    auto* line = dest.getLinePointer (y);
                    for (int x = 0; x < (int) w; ++x) { line[x * dest.pixelStride] = src[x * 4]; line[x * dest.pixelStride + 1] = src[x * 4 + 1]; line[x * dest.pixelStride + 2] = src[x * 4 + 2]; }
                }
            }
            locked->Release();
            return ok;
        }

        juce::String error() const
        {
            if (problem.isNotEmpty()) return problem;
            if (notify != nullptr && notify->failed.load())
            {
                switch (notify->errorCode.load())
                {
                    case MF_MEDIA_ENGINE_ERR_SRC_NOT_SUPPORTED: return "Windows cannot play this kind of video file.";
                    case MF_MEDIA_ENGINE_ERR_DECODE:            return "The video file is damaged, or uses a codec Windows does not have.";
                    case MF_MEDIA_ENGINE_ERR_NETWORK:           return "The video file could not be read.";
                    default:                                    return "Windows could not play this video.";
                }
            }
            return {};
        }
    };

    //==============================================================================
    // The plain functions the app calls (declared in Video.h).
    /** Lets Windows deliver what is waiting for this thread, for a while (the self-test, which has no message loop of its own running). */
    void pump (int milliseconds)
    {
        const DWORD until = GetTickCount() + (DWORD) milliseconds;
        do
        {
            MSG msg;
            while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage (&msg); DispatchMessageW (&msg); }
            Sleep (5);
        } while (GetTickCount() < until);
    }

    juce::String describe (void* p);

    /** Tries to open a video in several ways and says how each went (the --video-probe option). */
    juce::String probe (const juce::String& url)
    {
        juce::String out;
        for (int mode = 0; mode < 4; ++mode)
        {
            probeMode = mode;
            auto* pl = new Player();
            if (mode == 1) { SetWindowLongPtrW (pl->window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE); SetWindowPos (pl->window, nullptr, 100, 100, 640, 360, SWP_FRAMECHANGED | SWP_SHOWWINDOW); }
            const bool ok = pl->open (url);
            for (int i = 0; i < 60 && ! (pl->notify != nullptr && (pl->notify->ready.load() || pl->notify->failed.load())); ++i) pump (50);
            out << "mode " << mode << ": open " << (int) ok << "; " << describe (pl) << "; " << pl->error() << juce::newLine;
            if (pl->engine != nullptr && pl->notify->ready.load())
            {
                pl->engine->Play();
                juce::Image img;
                int frames = 0;
                for (int i = 0; i < 40; ++i) { pump (25); if (pl->frame (img)) ++frames; }
                out << "   frames handed over in a second: " << frames << ", size " << img.getWidth() << " x " << img.getHeight()
                    << (img.isValid() ? ", middle pixel " + img.getPixelAt (img.getWidth() / 2, img.getHeight() / 2).toDisplayString (false) : juce::String()) << juce::newLine;
                out << "   after a second of playing: at " << pl->engine->GetCurrentTime() << " s; " << describe (pl) << juce::newLine;
            }
            delete pl;
        }
        probeMode = 2;
        return out;
    }

    /** What the player is doing, in a line (for finding out why a video does not open). */
    juce::String describe (void* p)
    {
        auto* pl = static_cast<Player*> (p);
        if (pl->engine == nullptr) return "no engine; " + pl->problem;
        juce::String s;
        s << "ready state " << (int) pl->engine->GetReadyState() << ", network state " << (int) pl->engine->GetNetworkState() << ", events seen " << pl->notify->seen.load()
          << ", last event " << pl->notify->last.load() << ", duration " << pl->engine->GetDuration() << ", events:";
        for (int i = 0; i < juce::jmin (16, pl->notify->seen.load()); ++i) s << " " << pl->notify->order[i] << "(" << juce::String::toHexString (pl->notify->params[i]) << ")";
        IMFMediaError* err = nullptr;
        if (SUCCEEDED (pl->engine->GetError (&err)) && err != nullptr) { s << ", error " << (int) err->GetErrorCode() << " / " << juce::String::toHexString ((int) err->GetExtendedErrorCode()); err->Release(); }
        return s;
    }

    bool frame (void* p, juce::Image& into)         { return static_cast<Player*> (p)->frame (into); }
    void* create()                                  { return new Player(); }
    void destroy (void* p)                          { delete static_cast<Player*> (p); }
    void* windowOf (void* p)                        { return static_cast<Player*> (p)->window; }
    bool open (void* p, const juce::String& path)   { return static_cast<Player*> (p)->open (path); }
    void close (void* p)                            { static_cast<Player*> (p)->close(); }
    juce::String error (void* p)                    { return static_cast<Player*> (p)->error(); }

    bool isReady (void* p)
    {
        auto* pl = static_cast<Player*> (p);
        return pl->engine != nullptr && pl->notify != nullptr && pl->notify->ready.load() && ! pl->notify->failed.load();
    }

    double duration (void* p)
    {
        auto* pl = static_cast<Player*> (p);
        if (! isReady (p)) return 0.0;
        const double d = pl->engine->GetDuration();
        return d == d && d < 1.0e9 ? d : 0.0;   // (not a number, or endless, when it is not known)
    }

    double position (void* p)                       { auto* pl = static_cast<Player*> (p); return pl->engine != nullptr ? pl->engine->GetCurrentTime() : 0.0; }
    void setPosition (void* p, double seconds)      { auto* pl = static_cast<Player*> (p); if (pl->engine != nullptr) pl->engine->SetCurrentTime (seconds); }
    void play (void* p)                             { auto* pl = static_cast<Player*> (p); if (pl->engine != nullptr) pl->engine->Play(); }
    void pause (void* p)                            { auto* pl = static_cast<Player*> (p); if (pl->engine != nullptr) pl->engine->Pause(); }
    bool isPlaying (void* p)                        { auto* pl = static_cast<Player*> (p); return pl->engine != nullptr && ! pl->engine->IsPaused() && ! pl->engine->IsEnded(); }

    void setVolume (void* p, float volume)
    {
        auto* pl = static_cast<Player*> (p);
        if (pl->engine == nullptr) return;
        pl->engine->SetMuted (volume <= 0.0f ? TRUE : FALSE);
        pl->engine->SetVolume ((double) juce::jlimit (0.0f, 1.0f, volume));
    }
}
