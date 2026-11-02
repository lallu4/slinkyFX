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
    static bool WaitForModuleLoad(DWORD pid, const wchar_t* moduleName, DWORD timeoutMs);
    static bool UnloadModule(DWORD pid, const wchar_t* moduleName);
};

} // namespace Slinky
