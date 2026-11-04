#pragma once

#include <windows.h>
#include <string>
#include <vector>

namespace Slinky {

struct TargetWindow {
    HWND hWnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
};

class ProcessUtil {
public:
    static std::vector<DWORD> GetJavaProcessIds();
    static std::vector<TargetWindow> FindMinecraftWindows();
    static bool IsModuleLoaded(DWORD pid, const wchar_t* moduleName);

    // Whether a Slinky engine really owns that process. The module list cannot tell: after an unload the
    // image usually stays mapped with nothing running in it, which reads as "still loaded".
    static bool IsSlinkyEngineRunning(DWORD pid);
    static bool WaitForSlinkyEngineToLeave(DWORD pid, DWORD timeoutMs);
    static bool WaitForSlinkyEngine(DWORD pid, DWORD timeoutMs);

    // Exact full-path match, to tell a leftover image from an image we are about to load ourselves.
    static bool IsModuleMappedAt(DWORD pid, const std::wstring& fullPath);
};

} // namespace Slinky
