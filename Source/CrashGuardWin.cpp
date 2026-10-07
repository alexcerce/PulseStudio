// Windows side of the crash guard: finds which module (e.g. a .vst3) an exception happened in.
// Kept out of the JUCE headers because it needs <windows.h>.
#include <JuceHeader.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace CrashGuardWin
{
    /** For an EXCEPTION_POINTERS* from JUCE's crash handler: the module path containing the faulting
        address, and a description of the exception. */
    void describe (void* exceptionPointers, juce::String& modulePath, juce::String& description)
    {
        auto* ep = static_cast<EXCEPTION_POINTERS*> (exceptionPointers);
        if (ep == nullptr || ep->ExceptionRecord == nullptr)
            return;

        const auto code = ep->ExceptionRecord->ExceptionCode;
        void* address = ep->ExceptionRecord->ExceptionAddress;
        description = "Exception 0x" + juce::String::toHexString ((juce::int64) code).paddedLeft ('0', 8)
                    + " at 0x" + juce::String::toHexString ((juce::pointer_sized_int) address);

        HMODULE module = nullptr;
        if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR) address, &module) && module != nullptr)
        {
            wchar_t path[MAX_PATH * 2] = {};
            if (GetModuleFileNameW (module, path, (DWORD) (sizeof (path) / sizeof (path[0]))) > 0)
                modulePath = juce::String (path);
        }
    }
}

namespace CrashGuardWin
{
    static char helperStage[160] = "starting";

    /** Sandbox helpers say what they are doing, so that a crash note can say where it happened. */
    void setHelperStage (const char* stage)
    {
        strncpy_s (helperStage, sizeof (helperStage), stage != nullptr ? stage : "", _TRUNCATE);
    }

    /** Where a sandbox helper with this process id leaves its crash note (the folder, for pid 0). */
    juce::File helperNoteFile (int pid)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PulseSandbox");
        return pid > 0 ? dir.getChildFile ("crash-" + juce::String (pid) + ".txt") : dir;
    }

    static wchar_t helperNotePath[MAX_PATH * 2] = {};

    // Runs inside a crashing process: Windows calls only, nothing that allocates from the C++ heap.
    static void writeHelperNote (EXCEPTION_POINTERS* ep)
    {
        if (helperNotePath[0] == 0) return;
        char text[1400] = {};
        char module[MAX_PATH * 2] = "(unknown)";
        DWORD code = 0;
        void* address = nullptr;
        if (ep != nullptr && ep->ExceptionRecord != nullptr)
        {
            code = ep->ExceptionRecord->ExceptionCode;
            address = ep->ExceptionRecord->ExceptionAddress;
            HMODULE m = nullptr;
            if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) address, &m) && m != nullptr)
            {
                wchar_t wide[MAX_PATH * 2] = {};
                if (GetModuleFileNameW (m, wide, (DWORD) (sizeof (wide) / sizeof (wide[0]))) > 0)
                    WideCharToMultiByte (CP_UTF8, 0, wide, -1, module, (int) sizeof (module), nullptr, nullptr);
            }
        }
        const int n = wsprintfA (text, "exception 0x%08lX\r\nmodule %s\r\nstage %s\r\n", code, module, helperStage);
        HANDLE f = CreateFileW (helperNotePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile (f, text, (DWORD) n, &written, nullptr);
        CloseHandle (f);
    }

    /** No "has stopped working" dialog for this process: a crash just ends it (used by sandbox helpers). */
    void disableCrashDialogs()
    {
        helperNoteFile (0).createDirectory();
        helperNoteFile ((int) GetCurrentProcessId()).getFullPathName().copyToUTF16 (helperNotePath, sizeof (helperNotePath) - sizeof (wchar_t));

        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        DWORD mode = 0;
        if (GetThreadErrorMode != nullptr) SetThreadErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX, &mode);
        // a last-chance handler that leaves a note of what happened and terminates straight away
        SetUnhandledExceptionFilter ([] (EXCEPTION_POINTERS* ep) -> LONG { writeHelperNote (ep); TerminateProcess (GetCurrentProcess(), 3); return EXCEPTION_EXECUTE_HANDLER; });
    }
}

namespace CrashGuardWin
{
    int currentProcessId() { return (int) GetCurrentProcessId(); }

    bool isProcessAlive (int pid)
    {
        if (pid <= 0) return false;
        HANDLE h = OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD) pid);
        if (h == nullptr) return false;
        DWORD code = 0;
        const bool alive = GetExitCodeProcess (h, &code) && code == STILL_ACTIVE;
        CloseHandle (h);
        return alive;
    }
}

#include <crtdbg.h>

namespace CrashGuardWin
{
    static void (*debugCheckFailed) (const char*) = nullptr;

   #ifdef _DEBUG
    static int debugReportHook (int reportType, char* message, int* returnValue)
    {
        if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR)
        {
            if (debugCheckFailed != nullptr) debugCheckFailed (message != nullptr ? message : "");   // writes the report and ends the program
            if (returnValue != nullptr) *returnValue = 0;
            return 1;   // handled: no window
        }
        return 0;
    }
   #endif

    /** Debug builds: a failed check in the C++ runtime (such as reading past the end of a list) calls `onFail`
        with its message instead of putting a window on the screen. Does nothing in release builds. */
    void installDebugCheckHook (void (*onFail) (const char*))
    {
        debugCheckFailed = onFail;
       #ifdef _DEBUG
        _CrtSetReportHook (debugReportHook);
       #endif
    }
}

#include <tlhelp32.h>
#include <map>

namespace CrashGuardWin
{
    /** Processor time used so far by each thread of this process, with the thread's name where it has one
        (for --idle-cpu, to see which threads are busy when the app is doing nothing). */
    std::map<unsigned long, std::pair<juce::String, double>> threadTimes()
    {
        std::map<unsigned long, std::pair<juce::String, double>> out;
        const DWORD pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return out;
        THREADENTRY32 te {};
        te.dwSize = sizeof (te);
        if (Thread32First (snap, &te))
            do
            {
                if (te.th32OwnerProcessID != pid) continue;
                if (HANDLE h = OpenThread (THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID))
                {
                    FILETIME created, exited, kernel, user;
                    double seconds = 0.0;
                    if (GetThreadTimes (h, &created, &exited, &kernel, &user))
                    {
                        auto toSeconds = [] (FILETIME t) { return (((unsigned long long) t.dwHighDateTime << 32) | t.dwLowDateTime) * 1.0e-7; };
                        seconds = toSeconds (kernel) + toSeconds (user);
                    }
                    juce::String name;
                    PWSTR desc = nullptr;
                    if (SUCCEEDED (GetThreadDescription (h, &desc)) && desc != nullptr) { name = juce::String (desc); LocalFree (desc); }
                    out[te.th32ThreadID] = { name, seconds };
                    CloseHandle (h);
                }
            }
            while (Thread32Next (snap, &te));
        CloseHandle (snap);
        return out;
    }
}

namespace CrashGuardWin
{
    /** A picture of a window with everything inside it, child windows of other makers included (a hosted
        plugin's own window is one of those, and a snapshot of our component does not see it). */
    juce::Image grabWindow (void* windowHandle)
    {
        auto hwnd = (HWND) windowHandle;
        RECT r {};
        if (hwnd == nullptr || ! GetClientRect (hwnd, &r) || r.right <= 0 || r.bottom <= 0) return {};
        const int w = r.right, h = r.bottom;
        juce::Image img (juce::Image::ARGB, w, h, true);
        HDC screen = GetDC (nullptr);
        HDC dc = CreateCompatibleDC (screen);
        BITMAPINFO bi {};
        bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection (dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp != nullptr && bits != nullptr)
        {
            auto old = SelectObject (dc, bmp);
            PrintWindow (hwnd, dc, 0x00000001 /* client only */ | 0x00000002 /* full content */);
            GdiFlush();
            juce::Image::BitmapData data (img, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < h; ++y)
            {
                auto* src = static_cast<const juce::uint8*> (bits) + (size_t) y * (size_t) w * 4;
                auto* dst = data.getLinePointer (y);
                for (int x = 0; x < w; ++x) { dst[x * 4] = src[x * 4]; dst[x * 4 + 1] = src[x * 4 + 1]; dst[x * 4 + 2] = src[x * 4 + 2]; dst[x * 4 + 3] = 255; }
            }
            SelectObject (dc, old);
        }
        if (bmp != nullptr) DeleteObject (bmp);
        DeleteDC (dc);
        ReleaseDC (nullptr, screen);
        return img;
    }
}
