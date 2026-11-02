#pragma once

#include <windows.h>
#include <string>
#include "jdk/jni.h"
#include "jdk/jvmti.h"

namespace SlinkyNative {

enum class MappingType {
    Unknown = 0,
    Named = 101,        // MCP mappings (resource 101)
    Srg = 102,          // SRG mappings (resource 102)
    Official = 103,     // Notch/Vanilla/Lunar/Badlion obfuscated mappings (resource 103)
    Intermediary = 104, // Legacy Fabric intermediary names (resource 104)
    Legacy1710 = 105    // 1.7.10 obfuscated/MCP - routed to the native engine (resource 105)
};

struct MinecraftInfo {
    MappingType mapping = MappingType::Unknown;
    std::string mappingName;
    int resourceId = 0;
    jclass minecraftClass = nullptr;
    jobject classLoader = nullptr;
};

class MinecraftDetector {
public:
    static bool Detect(MinecraftInfo& outInfo);
};

} // namespace SlinkyNative
