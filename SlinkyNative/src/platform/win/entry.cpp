// Windows entry of the client DLL: it is injected into the game process (LoadLibrary), so DllMain only starts the
// start-up thread (slinky_setup runs JNI and file I/O, which must not happen under the loader lock).
#include "slinky/platform.h"
#include "slinky/win32.h"

extern "C" void start_unload_thread(HMODULE self);   // protected/input.cpp: unload thread (teardown, then FreeLibraryAndExitThread)

// The DLL's own handle, so slinky-native.log lands next to the staged DLL and not next to javaw.exe:
// GetModuleFileNameW(NULL) returns the process executable, and the loader only reads the staging directory.
static HMODULE slinky_self;

static DWORD start_thread(LPVOID self) {
    int err = slinky_setup(self);
    if (err) {
        slinky_logf("FINAL STATUS: FAILED");
        start_unload_thread((HMODULE)self);   // failed: report was printed, take the DLL out again
    } else {
        slinky_logf("FINAL STATUS: RUNNING");
    }
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void *) {
    if (reason == 1 /* DLL_PROCESS_ATTACH */) {
        slinky_self = (HMODULE)self;
        DisableThreadLibraryCalls(self);
        CloseHandle(CreateThread(0, 0, start_thread, self, 0, 0));
    }
    return 1;
}

// ---- diagnostics: debugger output, the process's stderr handle and the DLL's own slinky-native.log ----
// (launchers do not keep the game's stderr, and an unload problem must stay visible after the exit)
extern "C" {
__declspec(dllimport) DWORD WINAPI GetModuleFileNameW(HMODULE m, wchar_t *buf, DWORD n);
}
static void get_dll_log_path(wchar_t *out, DWORD max_len) {
    out[0] = 0;
    DWORD len = GetModuleFileNameW(slinky_self, out, max_len);
    if (!len || len >= max_len) return;
    // Find last backslash and replace the file name with slinky-native.log.
    int last = -1;
    for (int i = 0; out[i]; i++) if (out[i] == L'\\' || out[i] == L'/') last = i;
    if (last < 0) return;
    const wchar_t name[] = L"slinky-native.log";
    int j = last + 1;
    for (int i = 0; name[i] && j + 1 < (int)max_len; i++) out[j++] = name[i];
    out[j] = 0;
}
extern "C" void slinky_log_write(const char *line) {
    char buf[600];
    int n = 0;
    for (const char *p = "slinkyFX: "; *p; p++) buf[n++] = *p;
    for (; *line && n < 597; line++) buf[n++] = *line;
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = 0;
    OutputDebugStringA(buf);
    DWORD done;
    HANDLE err = GetStdHandle((DWORD)-12 /* STD_ERROR_HANDLE */);
    if (err && err != INVALID_HANDLE) WriteFile(err, buf, (DWORD)n, &done, 0);
    wchar_t path[520];
    get_dll_log_path(path, 520);
    if (path[0]) {
        HANDLE f = CreateFileW(path, 4 /* FILE_APPEND_DATA */, FILE_SHARE_READ | 2 /* FILE_SHARE_WRITE */,
                              0, 4 /* OPEN_ALWAYS */, FILE_ATTRIBUTE_NORMAL, 0);
        if (f != INVALID_HANDLE) {
            WriteFile(f, buf, (DWORD)n, &done, 0);
            CloseHandle(f);
        }
    }
}

// ---- OpenGL: wglGetProcAddress for extensions, opengl32 itself for GL 1.1 ----
typedef void *(WINAPI *WglGetProcAddress)(const char *);
extern "C" void *slinky_gl_get_proc(const char *name) {
    static HMODULE gl;
    static WglGetProcAddress wgl;
    if (!gl) {
        gl = GetModuleHandleA("opengl32");
        if (!gl) gl = LoadLibraryA("opengl32.dll");
        if (!gl) return 0;
        wgl = (WglGetProcAddress)GetProcAddress(gl, "wglGetProcAddress");
    }
    void *p = wgl ? wgl(name) : 0;
    if (p && p != (void *)1 && p != (void *)2 && p != (void *)3 && p != (void *)-1) return p;   // wgl failure values
    return GetProcAddress(gl, name);
}
