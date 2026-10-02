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
    /** No "has stopped working" dialog for this process: a crash just ends it (used by sandbox helpers). */
    void disableCrashDialogs()
    {
        SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        DWORD mode = 0;
        if (GetThreadErrorMode != nullptr) SetThreadErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX, &mode);
        // a last-chance handler that terminates straight away
        SetUnhandledExceptionFilter ([] (EXCEPTION_POINTERS*) -> LONG { TerminateProcess (GetCurrentProcess(), 3); return EXCEPTION_EXECUTE_HANDLER; });
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
