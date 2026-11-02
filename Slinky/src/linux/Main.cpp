// ---------------------------------------------------------------------------
// Main.cpp (Linux) - same flow as the Windows loader:
//   extract slinky-native -> scan for the game (3 min budget, 20 s before the
//   lwjgl-less fallback kicks in) -> inject -> watch the log for FINAL STATUS.
// ---------------------------------------------------------------------------
#include "ConsoleUI.hpp"
#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include "Compat.hpp"

#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

using namespace Slinky;

// Titles used by the clients we support (lower-case compare) - same keywords
// as the Windows build.
static int TitleScore(const std::string& title) {
    std::string t = title;
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    int score = 0;
    if (t.find("minecraft") != std::string::npos) score += 4;
    if (t.find("lunar") != std::string::npos) score += 4;
    if (t.find("badlion") != std::string::npos) score += 4;
    if (t.find("1.8") != std::string::npos) score += 1;
    return score;
}

// Picks the candidate that really belongs to a Minecraft JVM: LWJGL must be
// loaded in the process (the Linux twin of the module check on Windows).
// After allowFallback we fall back to the best title match anyway.
static bool PickTarget(const std::vector<TargetWindow>& windows, bool allowFallback,
                       TargetWindow& out) {
    int best = -1;
    for (const TargetWindow& w : windows) {
        if (!ProcessUtil::IsModuleLoaded(w.pid, "lwjgl")) continue;
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
    return best >= 0;
}

int main() {
    ConsoleUI::Initialize();
    ConsoleUI::StartAnimation();

    std::string soPath;
    std::string logPath;
    if (!Injector::ExtractBundledSo(soPath, logPath)) {
        ConsoleUI::Shutdown();
        return 1;
    }

    ConsoleUI::SetStatusText("Waiting for Minecraft");

    unsigned long long startWait = GetTickCount64();
    const unsigned long long timeoutWait = 180000;
    TargetWindow target;
    bool found = false;

    while (GetTickCount64() - startWait < timeoutWait) {
        std::vector<TargetWindow> windows = ProcessUtil::FindMinecraftWindows();
        if (PickTarget(windows, GetTickCount64() - startWait > 20000, target)) {
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

    if (!Injector::InjectDll(target.pid, soPath)) {
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
