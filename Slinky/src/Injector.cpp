#include "Injector.hpp"
#include "ProcessUtil.hpp"
#include "ConsoleUI.hpp"
#include "Resource.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Slinky {

bool Injector::ExtractBundledDll(std::wstring& outDllPath, std::wstring& outLogPath) {
    wchar_t tempPath[MAX_PATH] = { 0 };
    if (!GetTempPathW(MAX_PATH, tempPath)) {
        ConsoleUI::PrintError("Finding temp folder", GetLastError());
        return false;
    }

    wchar_t slinkyTempDir[MAX_PATH] = { 0 };
    swprintf_s(slinkyTempDir, L"%lsslinky-%lu", tempPath, GetCurrentProcessId());

    if (!CreateDirectoryW(slinkyTempDir, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        ConsoleUI::PrintError("Creating temp folder", GetLastError());
        return false;
    }

    outDllPath = std::wstring(slinkyTempDir) + L"\\slinky-native.dll";
    outLogPath = std::wstring(slinkyTempDir) + L"\\slinky-native.log";

    DeleteFileW(outLogPath.c_str());

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_BUNDLED_DLL), (LPCWSTR)10);
    if (hRes) {
        DWORD resSize = SizeofResource(nullptr, hRes);
        HGLOBAL hLoad = LoadResource(nullptr, hRes);
        if (hLoad && resSize > 0) {
            const void* pData = LockResource(hLoad);
            if (pData) {
                HANDLE hFile = CreateFileW(outDllPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    WriteFile(hFile, pData, resSize, &written, nullptr);
                    CloseHandle(hFile);
                    return true;
                }
            }
        }
    }

    wchar_t exePath[MAX_PATH] = { 0 };
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
            std::wstring localDll = std::wstring(exePath) + L"\\slinky-native.dll";
            if (GetFileAttributesW(localDll.c_str()) != INVALID_FILE_ATTRIBUTES) {
                if (CopyFileW(localDll.c_str(), outDllPath.c_str(), FALSE)) {
                    return true;
                }
                outDllPath = localDll;
                outLogPath = std::wstring(exePath) + L"\\slinky-native.log";
                DeleteFileW(outLogPath.c_str());
                return true;
            }
        }
    }

    ConsoleUI::PrintError("Reading bundled DLL", ERROR_FILE_NOT_FOUND);
    return false;
}

bool Injector::InjectDll(DWORD pid, const std::wstring& dllPath) {
    // A running client is taken out by its own Unload button, never from here: it owns a JVMTI environment,
    // a subclassed window procedure and Java classes that only its engine thread can release, so a remote
    // FreeLibrary would unmap code the game is still calling into.
    if (ProcessUtil::IsModuleLoaded(pid, L"slinky-native.dll")) {
        ConsoleUI::PrintFailure("Slinky is already running. Unload it from the menu, then inject again.");
        return false;
    }

    HANDLE hProcess = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
        FALSE, pid);

    if (!hProcess) {
        ConsoleUI::PrintError("OpenProcess", GetLastError());
        return false;
    }

    SIZE_T pathSizeBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemotePath = VirtualAllocEx(hProcess, nullptr, pathSizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!pRemotePath) {
        ConsoleUI::PrintError("Allocating DLL path in game", GetLastError());
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pRemotePath, dllPath.c_str(), pathSizeBytes, nullptr)) {
        ConsoleUI::PrintError("Copying DLL path into game", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE pLoadLibraryW = (LPTHREAD_START_ROUTINE)GetProcAddress(hKernel32, "LoadLibraryW");

    if (!pLoadLibraryW) {
        ConsoleUI::PrintError("Finding Windows DLL loader", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pLoadLibraryW, pRemotePath, 0, nullptr);
    if (!hThread) {
        ConsoleUI::PrintError("Starting DLL loader in game", GetLastError());
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    DWORD waitRes = WaitForSingleObject(hThread, 30000);
    if (waitRes != WAIT_OBJECT_0) {
        ConsoleUI::PrintFailure("Game did not finish loading the DLL.");
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    DWORD dwExitCode = 0;
    GetExitCodeThread(hThread, &dwExitCode);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemotePath, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    if (dwExitCode == 0) {
        ConsoleUI::PrintFailure("LoadLibraryW failed in game process.");
        return false;
    }

    if (!ProcessUtil::WaitForModuleLoad(pid, L"slinky-native.dll", 5000)) {
        ConsoleUI::PrintFailure("DLL not in game after load attempt.");
        return false;
    }

    return true;
}

namespace {

void PrintLogTail(const std::wstring& logPath) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, logPath.c_str(), L"r, ccs=UTF-8") != 0 || !fp) return;

    std::vector<std::wstring> lines;
    wchar_t line[1024];
    while (fgetws(line, 1024, fp)) {
        size_t len = wcslen(line);
        while (len > 0 && (line[len - 1] == L'\n' || line[len - 1] == L'\r')) line[--len] = L'\0';
        if (len > 0) {
            lines.push_back(line);
            if (lines.size() > 30) lines.erase(lines.begin());
        }
    }
    fclose(fp);

    if (!lines.empty()) {
        fprintf(stderr, "\n--- slinky-native.log tail ---\n");
        for (const std::wstring& l : lines) {
            // Best-effort narrow output for the console
            int n = WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (n > 0) {
                std::vector<char> buf(n);
                WideCharToMultiByte(CP_UTF8, 0, l.c_str(), -1, buf.data(), n, nullptr, nullptr);
                fprintf(stderr, "%s\n", buf.data());
            }
        }
        fprintf(stderr, "--- end log tail ---\n");
    }
}

} // namespace

bool Injector::MonitorStartup(DWORD pid, const std::wstring& logPath) {
    HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);

    // The status line stays on "Injecting into Minecraft..." while the client boots.
    DWORD startTime = GetTickCount();
    // Bootstrap.start can wait up to 2 minutes for the client thread to run the scheduled
    // init task (Bootstrap.java); the monitor must outlive that window plus margin.
    const DWORD timeoutMs = 150000;

    while (GetTickCount() - startTime < timeoutMs) {
        if (hProcess && WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0) {
            ConsoleUI::PrintFailure("Game process closed before startup finished.");
            PrintLogTail(logPath);
            CloseHandle(hProcess);
            return false;
        }

        FILE* fp = nullptr;
        if (_wfopen_s(&fp, logPath.c_str(), L"r, ccs=UTF-8") == 0 && fp) {
            wchar_t line[1024];
            while (fgetws(line, 1024, fp)) {
                if (wcsstr(line, L"FINAL STATUS: RUNNING")) {
                    fclose(fp);
                    if (hProcess) CloseHandle(hProcess);
                    return true;
                } else if (wcsstr(line, L"FINAL STATUS: RELOADED")) {
                    fclose(fp);
                    if (hProcess) CloseHandle(hProcess);
                    return true;
                } else if (wcsstr(line, L"FINAL STATUS: ALREADY_RUNNING")) {
                    ConsoleUI::PrintFailure("Already running. Unload from menu first.");
                    fclose(fp);
                    if (hProcess) CloseHandle(hProcess);
                    return false;
                } else if (wcsstr(line, L"FINAL STATUS: FAILED")) {
                    ConsoleUI::PrintFailure("Minecraft could not start the payload.");
                    fclose(fp);
                    if (hProcess) CloseHandle(hProcess);
                    return false;
                }
            }
            fclose(fp);
        }

        Sleep(150);
    }

    if (hProcess) CloseHandle(hProcess);
    ConsoleUI::PrintFailure("DLL loaded but the payload never confirmed startup.");
    PrintLogTail(logPath);
    return false;
}

} // namespace Slinky
