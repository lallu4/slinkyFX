#include "ProcessUtil.hpp"
#include <tlhelp32.h>
#include <algorithm>

namespace Slinky {

std::vector<DWORD> ProcessUtil::GetJavaProcessIds() {
    std::vector<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"javaw.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"java.exe") == 0)
            {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return pids;
}

struct EnumData {
    std::vector<DWORD> javaPids;
    std::vector<TargetWindow> windows;
};

static BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    if (!IsWindowVisible(hWnd)) return TRUE;
    if (GetWindowTextLengthW(hWnd) == 0) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);

    EnumData* data = (EnumData*)lParam;
    if (std::find(data->javaPids.begin(), data->javaPids.end(), pid) == data->javaPids.end()) {
        return TRUE;
    }

    wchar_t titleBuf[512] = { 0 };
    if (GetWindowTextW(hWnd, titleBuf, 512) > 0) {
        TargetWindow tw;
        tw.hWnd = hWnd;
        tw.pid = pid;
        tw.title = titleBuf;
        data->windows.push_back(tw);
    }

    return TRUE;
}

std::vector<TargetWindow> ProcessUtil::FindMinecraftWindows() {
    EnumData data;
    data.javaPids = GetJavaProcessIds();
    if (data.javaPids.empty()) return {};

    EnumWindows(EnumWindowsProc, (LPARAM)&data);
    return data.windows;
}

bool ProcessUtil::IsModuleLoaded(DWORD pid, const wchar_t* moduleName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    bool found = false;
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0 ||
                wcsstr(me.szExePath, moduleName) != nullptr)
            {
                found = true;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }

    CloseHandle(hSnap);
    return found;
}

bool ProcessUtil::WaitForModuleLoad(DWORD pid, const wchar_t* moduleName, DWORD timeoutMs) {
    DWORD elapsed = 0;
    while (elapsed < timeoutMs) {
        if (IsModuleLoaded(pid, moduleName)) return true;
        Sleep(50);
        elapsed += 50;
    }
    return false;
}

bool ProcessUtil::UnloadModule(DWORD pid, const wchar_t* moduleName) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    MODULEENTRY32W me;
    me.dwSize = sizeof(me);

    HMODULE hTargetModule = nullptr;
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName) == 0 ||
                wcsstr(me.szExePath, moduleName) != nullptr)
            {
                hTargetModule = me.hModule;
                break;
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);

    if (!hTargetModule) return true;

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
        FALSE, pid);
    if (!hProcess) return false;

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE pFreeLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "FreeLibrary");
    if (!pFreeLibrary) {
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pFreeLibrary, (LPVOID)hTargetModule, 0, nullptr);
    if (hThread) {
        WaitForSingleObject(hThread, 5000);
        CloseHandle(hThread);
    }
    CloseHandle(hProcess);

    DWORD elapsed = 0;
    while (elapsed < 3000) {
        if (!IsModuleLoaded(pid, moduleName)) return true;
        Sleep(50);
        elapsed += 50;
    }

    return !IsModuleLoaded(pid, moduleName);
}

} // namespace Slinky
