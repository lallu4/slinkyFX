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

#ifdef _WIN32
    static InjectionStatus LoadNativeEngine(HMODULE hModule, const MinecraftInfo& mcInfo);
#endif
};

} // namespace SlinkyNative
