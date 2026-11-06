#pragma once

#include <windows.h>
#include <string>
#include "MinecraftDetector.hpp"

namespace SlinkyNative {

enum class InjectionStatus {
    Success = 0,
    AlreadyRunning,
    Reloaded,
    Failed
};

class PayloadLoader {
public:
    static InjectionStatus LoadAndStart(HMODULE hModule, const MinecraftInfo& mcInfo);

private:
    static bool CheckPreviousState(JNIEnv* env, jobject classLoader, bool& outAlreadyRunning);
    static bool ExtractPayloadJar(HMODULE hModule, const MinecraftInfo& mcInfo, std::wstring& outJarPath);
    static bool AddJarToClassLoader(JNIEnv* env, jobject classLoader, const std::wstring& jarPath);
    static bool DefinePayloadClasses(JNIEnv* env, jobject classLoader, const std::wstring& jarPath);
    static bool LaunchBootstrap(JNIEnv* env, jobject classLoader, const MinecraftInfo& mcInfo, const std::wstring& jarPath);

    // The C++ client, from the file the loader staged next to this one: slinky-engine.dll on Windows,
    // libslinky-engine.so on Linux. Both are what 1.7.10 runs; the jars stay the path for 1.8.9.
    static InjectionStatus LoadNativeEngine(HMODULE hModule, const MinecraftInfo& mcInfo);
};

} // namespace SlinkyNative
