#pragma once

#include "jdk/jni.h"

namespace SlinkyNative {

class NativeBridge {
public:
    static bool Register(JNIEnv* env, jobject classLoader);

    // JNI native method implementations
    static jbyteArray JNICALL GetClassBytes(JNIEnv* env, jclass clazz, jclass targetClass);
    static jint JNICALL RetransformClass(JNIEnv* env, jclass clazz, jclass targetClass, jbyteArray newBytecode);
    static void JNICALL Log(JNIEnv* env, jclass clazz, jstring message);
};

} // namespace SlinkyNative
