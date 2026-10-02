#pragma once

#include "Common.h"

/** Runs exports on a background thread so the app stays usable (a progress pill with Cancel
    shows in the transport bar). Playback is paused while rendering because the engine shares
    plugin instances between playback and rendering. */
class BackgroundRender : private Thread
{
public:
    struct Job
    {
        String name;
        std::vector<te::Renderer::Parameters> renders;
        std::function<void (int ok, int total, bool cancelled)> onDone;
    };

    BackgroundRender() : Thread ("Pulse background render") {}
    ~BackgroundRender() override { cancel(); stopThread (10000); }

    /** The one renderer (lives for the whole app run). */
    static BackgroundRender& instance() { static auto* r = new BackgroundRender(); return *r; }

    /** Global flag the transport checks before playing. */
    static std::atomic<bool>& busyFlag() { return EngineHelpers::renderBusy(); }

    /** Cancels and waits (e.g. before the project closes). */
    void cancelAndWait()
    {
        cancel();
        for (int i = 0; i < 400 && isThreadRunning(); ++i)
            MessageManager::getInstance()->runDispatchLoopUntil (25);
    }

    bool isBusy() const                 { return busyFlag().load() || isThreadRunning(); }
    float getProgress() const           { return progress.load(); }
    String getStatus() const            { const ScopedLock sl (statusLock); return status; }

    /** Starts a job (returns false if one is already running). Call on the message thread. */
    bool start (Job j)
    {
        if (isBusy() || j.renders.empty()) return false;
        stopThread (1000);
        job = std::move (j);
        cancelled = false;
        progress = 0.0f;
        okCount = 0;
        busyFlag() = true;
        if (auto* e = job.renders.front().edit)
        {
            te::TransportControl::stopAllTransports (e->engine, false, true);
            te::Renderer::turnOffAllPlugins (*e);
        }
        startThread();
        return true;
    }

    void cancel()
    {
        cancelled = true;
        const ScopedLock sl (taskLock);
        if (task != nullptr) task->signalJobShouldExit();
    }

private:
    void setStatus (const String& s) { const ScopedLock sl (statusLock); status = s; }

    void run() override
    {
        const int total = (int) job.renders.size();
        for (int i = 0; i < total && ! threadShouldExit() && ! cancelled; ++i)
        {
            auto params = job.renders[(size_t) i];
            setStatus (job.name + (total > 1 ? " " + String (i + 1) + "/" + String (total) : String()));

            // the render graph has to be built on the message thread
            std::unique_ptr<te::Renderer::RenderTask> t;
            WaitableEvent built;
            MessageManager::callAsync ([&]
            {
                if (params.tracksToDo.countNumberOfSetBits() > 0 && ! params.destFile.isDirectory())
                    t = te::render_utils::createRenderTask (params, job.name, nullptr, nullptr);
                built.signal();
            });
            while (! built.wait (50))
                if (threadShouldExit()) return;
            if (t == nullptr) continue;

            { const ScopedLock sl (taskLock); task = t.get(); }
            while (! threadShouldExit() && ! cancelled)
            {
                if (t->runJob() != ThreadPoolJob::jobNeedsRunningAgain) break;
                progress = ((float) i + jlimit (0.0f, 1.0f, t->getCurrentTaskProgress())) / (float) total;
            }

            const bool ok = ! cancelled && t->errorMessage.isEmpty() && params.destFile.existsAsFile();
            if (! ok) params.destFile.deleteFile();
            else ++okCount;

            // tear the graph down on the message thread too
            WaitableEvent gone;
            MessageManager::callAsync ([&] { { const ScopedLock sl (taskLock); task = nullptr; } t.reset(); gone.signal(); });
            gone.wait (10000);
        }

        MessageManager::callAsync ([this, total, edit = job.renders.front().edit]
        {
            if (edit != nullptr) te::Renderer::turnOffAllPlugins (*edit);
            busyFlag() = false;
            progress = 1.0f;
            setStatus ({});
            if (job.onDone) job.onDone (okCount, total, cancelled.load());
        });
    }

    Job job;
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> cancelled { false };
    std::atomic<int> okCount { 0 };
    CriticalSection taskLock, statusLock;
    te::Renderer::RenderTask* task = nullptr;
    String status;
};
