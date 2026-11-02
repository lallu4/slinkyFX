#include "NativeBridge.hpp"
#include "JvmManager.hpp"
#include "Logger.hpp"

namespace SlinkyNative {

bool NativeBridge::Register(JNIEnv* env, jobject classLoader) {
    jclass bridgeClass = nullptr;

    // First try ClassLoader.loadClass("dev.slinky.inject.NativeBridge")
    if (classLoader) {
        jclass clClass = env->GetObjectClass(classLoader);
        jmethodID loadClassMethod = env->GetMethodID(clClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        if (loadClassMethod) {
            jstring className = env->NewStringUTF("dev.slinky.inject.NativeBridge");
            bridgeClass = (jclass)env->CallObjectMethod(classLoader, loadClassMethod, className);
            env->DeleteLocalRef(className);
            env->ExceptionClear();
        }
        env->DeleteLocalRef(clClass);
    }

    if (!bridgeClass) {
        bridgeClass = env->FindClass("dev/slinky/inject/NativeBridge");
        env->ExceptionClear();
    }

    if (!bridgeClass) {
        Logger::Log(L"could not find class dev.slinky.inject.NativeBridge");
        return false;
    }

    JNINativeMethod methods[] = {
        { (char*)"getClassBytes",    (char*)"(Ljava/lang/Class;)[B", (void*)&GetClassBytes },
        { (char*)"retransformClass", (char*)"(Ljava/lang/Class;[B)I", (void*)&RetransformClass },
        { (char*)"log",              (char*)"(Ljava/lang/String;)V", (void*)&Log }
    };

    jint res = env->RegisterNatives(bridgeClass, methods, sizeof(methods) / sizeof(methods[0]));
    if (res != JNI_OK) {
        Logger::Log(L"RegisterNatives dev.slinky.inject.NativeBridge failed: %d", res);
        return false;
    }

    Logger::Log(L"RegisterNatives dev.slinky.inject.NativeBridge succeeded");
    return true;
}

jbyteArray JNICALL NativeBridge::GetClassBytes(JNIEnv* env, jclass clazz, jclass targetClass) {
    if (!targetClass) return nullptr;

    jvmtiEnv* jvmti = JvmManager::GetJvmti();
    if (!jvmti) return nullptr;

    JvmManager::SetCaptureTarget(env, targetClass);

    // Enable ClassFileLoadHook
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);

    // Trigger ClassFileLoadHook via RetransformClasses
    jvmtiError err = jvmti->RetransformClasses(1, &targetClass);

    // Disable ClassFileLoadHook
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);

    std::vector<unsigned char> bytes = JvmManager::GetCapturedBytes();
    JvmManager::ClearCapturedBytes(env);

    if (err != JVMTI_ERROR_NONE || bytes.empty()) {
        Logger::Log(L"RetransformClasses failed: jvmti=%d, target=%p", err, targetClass);
        return nullptr;
    }

    jbyteArray result = env->NewByteArray((jsize)bytes.size());
    if (result) {
        env->SetByteArrayRegion(result, 0, (jsize)bytes.size(), (const jbyte*)bytes.data());
    }

    return result;
}

jint JNICALL NativeBridge::RetransformClass(JNIEnv* env, jclass clazz, jclass targetClass, jbyteArray newBytecode) {
    if (!targetClass || !newBytecode) return 0x74; // JVMTI_ERROR_NULL_POINTER (0x74 = 116)

    jvmtiEnv* jvmti = JvmManager::GetJvmti();
    if (!jvmti) return 0x6e; // JVMTI_ERROR_UNATTACHED_THREAD

    jsize len = env->GetArrayLength(newBytecode);
    jbyte* bytes = env->GetByteArrayElements(newBytecode, nullptr);
    if (!bytes) return 0x6e;

    JvmManager::SetReplacementData(env, targetClass, (const unsigned char*)bytes, len);

    // Enable ClassFileLoadHook
    jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);

    // Trigger retransformation
    jvmtiError err = jvmti->RetransformClasses(1, &targetClass);

    // Disable ClassFileLoadHook
    jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, nullptr);

    bool applied = JvmManager::WasReplacementApplied();
    JvmManager::ClearReplacementData(env);
    env->ReleaseByteArrayElements(newBytecode, bytes, JNI_ABORT);

    if (err != JVMTI_ERROR_NONE) {
        Logger::Log(L"RetransformClasses(apply) failed: jvmti=%d", err);
        return (jint)err;
    }

    if (!applied) {
        Logger::Log(L"RetransformClasses(apply): load hook never matched the target; class unchanged");
        return -1;
    }

    return 0;
}

void JNICALL NativeBridge::Log(JNIEnv* env, jclass clazz, jstring message) {
    if (!message) return;

    const char* utfChars = env->GetStringUTFChars(message, nullptr);
    if (utfChars) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, utfChars, -1, nullptr, 0);
        if (wlen > 0) {
            std::vector<wchar_t> wbuf(wlen);
            MultiByteToWideChar(CP_UTF8, 0, utfChars, -1, wbuf.data(), wlen);
            Logger::Log(L"[java] %ls", wbuf.data());
        }
        env->ReleaseStringUTFChars(message, utfChars);
    }
}

} // namespace SlinkyNative
