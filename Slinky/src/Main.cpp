#include <windows.h>
#include "ConsoleUI.hpp"
#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>

using namespace Slinky;

// Titles used by the clients we support (lower-case compare).
static int TitleScore(const std::wstring& title) {
    std::wstring t = title;
    for (auto& ch : t) ch = (wchar_t)towlower(ch);
    int score = 0;
    if (t.find(L"minecraft") != std::wstring::npos) score += 4;
    if (t.find(L"lunar") != std::wstring::npos) score += 4;
    if (t.find(L"badlion") != std::wstring::npos) score += 4;
    if (t.find(L"1.8") != std::wstring::npos) score += 1;
    return score;
}

// Picks the window that really belongs to a Minecraft JVM. Any Java program with a window
// (launchers, IDEs, other tools) used to be treated as the game; now the JVM must have LWJGL
// loaded. After allowFallback is set we fall back to the best title match, then the first window.
static bool PickTarget(const std::vector<TargetWindow>& windows, bool allowFallback, TargetWindow& out) {
    int best = -1;
    for (const TargetWindow& w : windows) {
        if (!ProcessUtil::IsModuleLoaded(w.pid, L"lwjgl")) continue;
        int score = 10 + TitleScore(w.title);
        if (score > best) { best = score; out = w; }
    }
    if (best >= 0) return true;
    if (!allowFallback || windows.empty()) return false;
    best = -1;
    for (const TargetWindow& w : windows) {
        int score = TitleScore(w.title);
        if (score > best) { best = score; out = w; }
    }
    return true;
}

int main() {
    ConsoleUI::Initialize();
    ConsoleUI::StartAnimation();

    std::wstring dllPath;
    std::wstring logPath;

    if (!Injector::ExtractBundledDll(dllPath, logPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetStatusText("Waiting for Minecraft");

    DWORD startWait = GetTickCount();
    const DWORD timeoutWait = 180000;
    std::vector<TargetWindow> windows;
    TargetWindow target;
    bool found = false;

    while (GetTickCount() - startWait < timeoutWait) {
        windows = ProcessUtil::FindMinecraftWindows();
        if (PickTarget(windows, GetTickCount() - startWait > 20000, target)) {
            found = true;
            break;
        }

        Sleep(500);
    }

    if (!found) {
        ConsoleUI::PrintFailure("Minecraft not found after 3 minutes.");
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetPhase(ConsoleUI::Phase::Injecting);
    ConsoleUI::SetStatusText("Injecting into Minecraft");

    if (!Injector::InjectDll(target.pid, dllPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    bool success = Injector::MonitorStartup(target.pid, logPath);
    if (!success) {
        ConsoleUI::Shutdown();
        return 1;
    }

    // Success ending, exactly like the original loader: DONE header, then
    // "Injected successfully. Loader will close in 5s." counting down before
    // the window closes itself.
    ConsoleUI::FinishSuccess();

    ConsoleUI::Shutdown();
    return 0;
}
