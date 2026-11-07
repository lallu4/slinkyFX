// ---------------------------------------------------------------------------
// SoMain.cpp - Linux entry point for libslinky-native.so.
//
// Counterpart of DllMain.cpp: instead of DLL_PROCESS_ATTACH + CreateThread,
// the constructor attribute spawns a detached worker thread that runs the
// exact same sequence (Logger -> JvmManager -> MinecraftDetector ->
// PayloadLoader).  GCC has no SEH, so the __try/__except guard of the
// Windows build is not available here; the worker keeps its body fail-soft
// (every stage logs FINAL STATUS: FAILED instead of unwinding).
// ---------------------------------------------------------------------------
#include <dlfcn.h>
#include <pthread.h>
#include "Logger.hpp"
#include "JvmManager.hpp"
#include "MinecraftDetector.hpp"
#include "PayloadLoader.hpp"

namespace {

// The handle is this library's own base address, and every path the worker builds - the log file and the
// engine image - hangs off the folder it was loaded from. The loader stages both .so files into a folder of
// its own and watches the log inside that folder, so a worker that asked the running executable where it was
// would write beside java and the loader would wait out its whole budget for a line that is never coming.
void* SlinkyNativeWorker(void* imageHandle) {
    HMODULE self = static_cast<HMODULE>(imageHandle);
    SlinkyNative::Logger::Initialize(self);

    if (!SlinkyNative::JvmManager::Initialize()) {
        SlinkyNative::Logger::Log(L"FINAL STATUS: FAILED");
        return nullptr;
    }

    SlinkyNative::MinecraftInfo mcInfo;
    if (!SlinkyNative::MinecraftDetector::Detect(mcInfo)) {
        SlinkyNative::Logger::Log(L"FINAL STATUS: FAILED");
        return nullptr;
    }

    // Same return handling as RunWorker in DllMain.cpp: LoadAndStart logs the
    // FINAL STATUS line itself (RUNNING / ALREADY_RUNNING / FAILED).
    SlinkyNative::PayloadLoader::LoadAndStart(self, mcInfo);
    return nullptr;
}

} // namespace

__attribute__((constructor))
static void SlinkyNative_OnLoad() {
    Dl_info info;
    void* self = dladdr((void*)SlinkyNativeWorker, &info) ? info.dli_fbase : nullptr;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int rc = pthread_create(&thread, &attr, SlinkyNativeWorker, self);
    pthread_attr_destroy(&attr);
    (void)rc;  // Logger is not initialized yet here; failure is unrecoverable anyway
}

// Parity with DLL_PROCESS_DETACH.
__attribute__((destructor))
static void SlinkyNative_OnUnload() {
    SlinkyNative::JvmManager::Shutdown();
    SlinkyNative::Logger::Shutdown();
}
