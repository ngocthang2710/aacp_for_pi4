// ─────────────────────────────────────────────────────────────────────────────
// jni_bridge.cpp
// JNI glue giữa Kotlin CarPlayManager và C API (AacpSdk.h)
//
// Quy ước tên JNI:
//   Java_<packageName>_<ClassName>_<methodName>
//   com.aacp.sdk.CarPlayManager → Java_com_aacp_sdk_CarPlayManager_
// ─────────────────────────────────────────────────────────────────────────────
#include <jni.h>
#include "aacp/AacpSdk.h"
#include <android/log.h>
#include <string>
#include <unordered_map>
#include <mutex>

#define TAG "AACP_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── Global callback registry ──────────────────────────────────────────────────
// Mỗi AacpHandle có một set callbacks tương ứng trên Kotlin side
struct JniCallbacks {
    JavaVM* jvm       = nullptr;
    jobject videoObj  = nullptr;   // Kotlin lambda object
    jmethodID videoInvoke = nullptr;
    jobject audioObj  = nullptr;
    jmethodID audioInvoke = nullptr;
    jobject stateObj  = nullptr;
    jmethodID stateInvoke = nullptr;
};

static std::unordered_map<jlong, JniCallbacks*> gCallbacks;
static std::mutex gMutex;

// Helper: attach current thread đến JVM (để callback từ native thread)
static JNIEnv* getEnv(JavaVM* jvm) {
    JNIEnv* env = nullptr;
    if (jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        jvm->AttachCurrentThreadAsDaemon(&env, nullptr);
    }
    return env;
}

// ── JNI Functions ─────────────────────────────────────────────────────────────

extern "C" {

// fun nativeCreate(certPath: String, keyPath: String): Long
JNIEXPORT jlong JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeCreate(
        JNIEnv* env, jobject /*thiz*/,
        jstring jCertPath, jstring jKeyPath) {

    const char* cert = (jCertPath) ? env->GetStringUTFChars(jCertPath, nullptr) : nullptr;
    const char* key  = (jKeyPath)  ? env->GetStringUTFChars(jKeyPath,  nullptr) : nullptr;

    AacpHandle handle = aacp_create(cert, key);

    if (cert) env->ReleaseStringUTFChars(jCertPath, cert);
    if (key)  env->ReleaseStringUTFChars(jKeyPath,  key);

    if (handle == AACP_INVALID_HANDLE) {
        LOGE("aacp_create returned INVALID_HANDLE");
        return 0L;
    }

    // Tạo JniCallbacks entry
    auto* cb = new JniCallbacks();
    env->GetJavaVM(&cb->jvm);

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gCallbacks[(jlong)handle] = cb;
    }

    LOGI("nativeCreate → handle=%ld", (long)handle);
    return (jlong)handle;
}

// fun nativeStart(handle: Long): Boolean
JNIEXPORT jboolean JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeStart(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    int ret = aacp_start((AacpHandle)handle);
    LOGI("nativeStart → %d", ret);
    return (ret == AACP_OK) ? JNI_TRUE : JNI_FALSE;
}

// fun nativeStop(handle: Long)
JNIEXPORT void JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeStop(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    aacp_stop((AacpHandle)handle);
}

// fun nativeDestroy(handle: Long)
JNIEXPORT void JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeDestroy(
        JNIEnv* env, jobject /*thiz*/, jlong handle) {
    aacp_destroy((AacpHandle)handle);

    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gCallbacks.find(handle);
    if (it != gCallbacks.end()) {
        JniCallbacks* cb = it->second;
        // Release global refs
        if (cb->videoObj)  env->DeleteGlobalRef(cb->videoObj);
        if (cb->audioObj)  env->DeleteGlobalRef(cb->audioObj);
        if (cb->stateObj)  env->DeleteGlobalRef(cb->stateObj);
        delete cb;
        gCallbacks.erase(it);
    }
    LOGI("nativeDestroy done");
}

// ── Callback registration ─────────────────────────────────────────────────────

// fun nativeSetVideoCallback(handle: Long, callback: (ByteArray, Long) -> Unit)
JNIEXPORT void JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeSetVideoCallback(
        JNIEnv* env, jobject /*thiz*/,
        jlong handle, jobject callback) {

    JniCallbacks* cb;
    { std::lock_guard<std::mutex> lock(gMutex); cb = gCallbacks[handle]; }
    if (!cb) return;

    if (cb->videoObj) env->DeleteGlobalRef(cb->videoObj);
    cb->videoObj = env->NewGlobalRef(callback);

    // Kotlin lambda implements Function2<ByteArray, Long, Unit>
    jclass cls = env->GetObjectClass(callback);
    cb->videoInvoke = env->GetMethodID(cls, "invoke", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    aacp_set_video_callback((AacpHandle)handle,
        [](const AacpVideoFrame* frame, void* ud) {
            auto* cb = static_cast<JniCallbacks*>(ud);
            JNIEnv* env = getEnv(cb->jvm);
            if (!env || !cb->videoObj) return;

            jbyteArray arr = env->NewByteArray(frame->size);
            env->SetByteArrayRegion(arr, 0, frame->size,
                reinterpret_cast<const jbyte*>(frame->data));

            // Box Long
            jclass longClass = env->FindClass("java/lang/Long");
            jmethodID longCtor = env->GetMethodID(longClass, "<init>", "(J)V");
            jobject tsObj = env->NewObject(longClass, longCtor, (jlong)frame->timestamp_us);

            env->CallObjectMethod(cb->videoObj, cb->videoInvoke, arr, tsObj);
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(tsObj);
        },
        cb  // userdata
    );
}

// fun nativeSetAudioCallback(handle: Long, callback: (ByteArray, Long) -> Unit)
JNIEXPORT void JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeSetAudioCallback(
        JNIEnv* env, jobject /*thiz*/,
        jlong handle, jobject callback) {

    JniCallbacks* cb;
    { std::lock_guard<std::mutex> lock(gMutex); cb = gCallbacks[handle]; }
    if (!cb) return;

    if (cb->audioObj) env->DeleteGlobalRef(cb->audioObj);
    cb->audioObj = env->NewGlobalRef(callback);

    jclass cls = env->GetObjectClass(callback);
    cb->audioInvoke = env->GetMethodID(cls, "invoke",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    aacp_set_audio_callback((AacpHandle)handle,
        [](const AacpAudioFrame* frame, void* ud) {
            auto* cb = static_cast<JniCallbacks*>(ud);
            JNIEnv* env = getEnv(cb->jvm);
            if (!env || !cb->audioObj) return;

            jbyteArray arr = env->NewByteArray(frame->size);
            env->SetByteArrayRegion(arr, 0, frame->size,
                reinterpret_cast<const jbyte*>(frame->data));

            jclass longClass = env->FindClass("java/lang/Long");
            jmethodID longCtor = env->GetMethodID(longClass, "<init>", "(J)V");
            jobject tsObj = env->NewObject(longClass, longCtor, (jlong)frame->timestamp_us);

            env->CallObjectMethod(cb->audioObj, cb->audioInvoke, arr, tsObj);
            env->DeleteLocalRef(arr);
            env->DeleteLocalRef(tsObj);
        },
        cb
    );
}

// fun nativeSetStateCallback(handle: Long, callback: (Int, Int) -> Unit)
JNIEXPORT void JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeSetStateCallback(
        JNIEnv* env, jobject /*thiz*/,
        jlong handle, jobject callback) {

    JniCallbacks* cb;
    { std::lock_guard<std::mutex> lock(gMutex); cb = gCallbacks[handle]; }
    if (!cb) return;

    if (cb->stateObj) env->DeleteGlobalRef(cb->stateObj);
    cb->stateObj = env->NewGlobalRef(callback);

    jclass cls = env->GetObjectClass(callback);
    cb->stateInvoke = env->GetMethodID(cls, "invoke",
        "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");

    aacp_set_state_callback((AacpHandle)handle,
        [](AacpState state, AacpError err, void* ud) {
            auto* cb = static_cast<JniCallbacks*>(ud);
            JNIEnv* env = getEnv(cb->jvm);
            if (!env || !cb->stateObj) return;

            jclass intClass = env->FindClass("java/lang/Integer");
            jmethodID intCtor = env->GetMethodID(intClass, "<init>", "(I)V");
            jobject stateObj = env->NewObject(intClass, intCtor, (jint)state);
            jobject errObj   = env->NewObject(intClass, intCtor, (jint)err);

            env->CallObjectMethod(cb->stateObj, cb->stateInvoke, stateObj, errObj);
            env->DeleteLocalRef(stateObj);
            env->DeleteLocalRef(errObj);
        },
        cb
    );
}

// ── Input ─────────────────────────────────────────────────────────────────────

// fun nativeSendTouch(handle: Long, data: ByteArray): Boolean
JNIEXPORT jboolean JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeSendTouch(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jbyteArray data) {
    jsize len = env->GetArrayLength(data);
    jbyte* buf = env->GetByteArrayElements(data, nullptr);
    int ret = aacp_send_touch((AacpHandle)handle,
                               reinterpret_cast<uint8_t*>(buf), (int)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
    return ret >= 0 ? JNI_TRUE : JNI_FALSE;
}

// fun nativeSendMic(handle: Long, data: ByteArray): Boolean
JNIEXPORT jboolean JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeSendMic(
        JNIEnv* env, jobject /*thiz*/, jlong handle, jbyteArray data) {
    jsize len = env->GetArrayLength(data);
    jbyte* buf = env->GetByteArrayElements(data, nullptr);
    int ret = aacp_send_mic((AacpHandle)handle,
                             reinterpret_cast<uint8_t*>(buf), (int)len);
    env->ReleaseByteArrayElements(data, buf, JNI_ABORT);
    return ret >= 0 ? JNI_TRUE : JNI_FALSE;
}

// fun nativeGetState(handle: Long): Int
JNIEXPORT jint JNICALL
Java_com_aacp_sdk_CarPlayManager_nativeGetState(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
    return (jint)aacp_get_state((AacpHandle)handle);
}

} // extern "C"
