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
