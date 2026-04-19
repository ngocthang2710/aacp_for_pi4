// ─────────────────────────────────────────────────────────────────────────────
// AacpSdk.cpp
// C API entry point — bọc C++ objects thành opaque handle
// ─────────────────────────────────────────────────────────────────────────────
#include "../include/aacp/AacpSdk.h"
#include "transport/UsbTransport.h"
#include "protocol/AacpProtocol.h"
#include "session/SessionManager.h"

#include <memory>
#include <mutex>
#include <string>

#ifdef ANDROID
    #include <android/log.h>
    #define TAG "AACP_SDK"
    #define LOGI(fmt,...) __android_log_print(ANDROID_LOG_INFO,  TAG, fmt, ##__VA_ARGS__)
    #define LOGE(fmt,...) __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    #define LOGI(fmt,...) fprintf(stdout, "[SDK INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOGE(fmt,...) fprintf(stderr, "[SDK ERR ] " fmt "\n", ##__VA_ARGS__)
#endif

// ── Internal context struct ───────────────────────────────────────────────────
struct AacpContext {
    std::unique_ptr<aacp::UsbTransport>   usb;
    std::unique_ptr<aacp::AacpProtocol>   protocol;
    std::unique_ptr<aacp::SessionManager> session;

    // Callbacks
    AacpVideoCallback videoCallback = nullptr;
    void*             videoUserdata = nullptr;

    AacpAudioCallback audioCallback = nullptr;
    void*             audioUserdata = nullptr;

    AacpStateCallback stateCallback = nullptr;
    void*             stateUserdata = nullptr;

    AacpLogCallback   logCallback   = nullptr;
    void*             logUserdata   = nullptr;

    std::mutex        mutex;

    void fireState(AacpState state, AacpError err) {
        if (stateCallback) stateCallback(state, err, stateUserdata);
    }
};

// Helper: cast handle → context (với null check)
static AacpContext* ctx(AacpHandle h) {
    return reinterpret_cast<AacpContext*>(h);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

extern "C" {

AACP_API AacpHandle aacp_create(const char* cert_path, const char* key_path) {
    auto* c = new AacpContext();

    c->usb      = std::make_unique<aacp::UsbTransport>();
    c->protocol = std::make_unique<aacp::AacpProtocol>();

    // Video channel handler
    c->protocol->setHandler(aacp::Channel::Video,
        [c](const aacp::AacpFrame& f) {
            if (!c->videoCallback) return;
            AacpVideoFrame vf;
            vf.data         = f.payload.data();
            vf.size         = (int)f.payload.size();
            vf.timestamp_us = f.timestamp_us;
            vf.is_key_frame = f.isKeyFrame() ? 1 : 0;
            c->videoCallback(&vf, c->videoUserdata);
        }
    );

    // Audio channel handler
    c->protocol->setHandler(aacp::Channel::Audio,
        [c](const aacp::AacpFrame& f) {
            if (!c->audioCallback) return;
            AacpAudioFrame af;
            af.data         = f.payload.data();
            af.size         = (int)f.payload.size();
            af.timestamp_us = f.timestamp_us;
            c->audioCallback(&af, c->audioUserdata);
        }
    );

    // Session manager — inject USB send function
    c->session = std::make_unique<aacp::SessionManager>(
        *c->protocol,
        [c](const uint8_t* data, size_t len) -> bool {
            return c->usb->send(data, len);
        }
    );

    // Session state → state callback
    c->session->setStateCallback(
        [c](AacpState state, AacpError err) {
            c->fireState(state, err);
        }
    );

    // Initialize SSL
    std::string cert = cert_path ? cert_path : "";
    std::string key  = key_path  ? key_path  : "";
    if (!c->session->initialize(cert, key)) {
        LOGE("SSL init failed");
        delete c;
        return AACP_INVALID_HANDLE;
    }

    LOGI("SDK context created");
    return reinterpret_cast<AacpHandle>(c);
}

AACP_API int aacp_start(AacpHandle handle) {
    auto* c = ctx(handle);
    if (!c) return AACP_ERROR_INVALID_PARAM;

    // Bước 1: USB init (scan + AOA switch)
    if (!c->usb->initialize()) {
        LOGE("USB init failed");
        c->fireState(AACP_STATE_ERROR, AACP_ERROR_USB);
        return AACP_ERROR_USB;
    }

    // Bước 2: Bắt đầu USB I/O loop
    bool started = c->usb->start(
        // Frame callback: raw USB bytes → protocol parser
        [c](const aacp::UsbFrame& f) {
            c->protocol->feedData(f.data.data(), f.data.size());
        },
        // Error callback: USB disconnect, etc.
        [c](int code, const std::string& msg) {
            LOGE("USB error %d: %s", code, msg.c_str());
            c->fireState(AACP_STATE_ERROR, AACP_ERROR_USB);
        }
    );

    if (!started) return AACP_ERROR_USB;

    // Bước 3: Bắt đầu protocol handshake
    c->session->startHandshake();
    return AACP_OK;
}

AACP_API void aacp_stop(AacpHandle handle) {
    auto* c = ctx(handle);
    if (!c) return;
    c->usb->stop();
    c->fireState(AACP_STATE_DISCONNECTED, AACP_OK);
    LOGI("SDK stopped");
}

AACP_API void aacp_destroy(AacpHandle handle) {
    auto* c = ctx(handle);
    if (!c) return;
    delete c;
    LOGI("SDK context destroyed");
}

// ── Callbacks ─────────────────────────────────────────────────────────────────

AACP_API void aacp_set_video_callback(AacpHandle h,
                                       AacpVideoCallback cb, void* ud) {
    auto* c = ctx(h); if (!c) return;
    c->videoCallback = cb;
    c->videoUserdata = ud;
}

AACP_API void aacp_set_audio_callback(AacpHandle h,
                                       AacpAudioCallback cb, void* ud) {
    auto* c = ctx(h); if (!c) return;
    c->audioCallback = cb;
    c->audioUserdata = ud;
}

AACP_API void aacp_set_state_callback(AacpHandle h,
                                       AacpStateCallback cb, void* ud) {
    auto* c = ctx(h); if (!c) return;
    c->stateCallback = cb;
    c->stateUserdata = ud;
}

AACP_API void aacp_set_log_callback(AacpHandle h,
                                     AacpLogCallback cb, void* ud) {
    auto* c = ctx(h); if (!c) return;
    c->logCallback = cb;
    c->logUserdata = ud;
}

// ── Input ─────────────────────────────────────────────────────────────────────

AACP_API int aacp_send_touch(AacpHandle h, const uint8_t* data, int size) {
    auto* c = ctx(h);
    if (!c || !data || size <= 0) return AACP_ERROR_INVALID_PARAM;
    if (c->session->getState() != AACP_STATE_CONNECTED) return AACP_ERROR_NOT_INIT;

    aacp::AacpFrame frame;
    frame.channel = aacp::Channel::Touch;
    frame.flags   = aacp::FLAG_NONE;
    frame.payload.assign(data, data + size);

    auto bytes = aacp::AacpProtocol::serialize(frame);
    return c->usb->send(bytes.data(), bytes.size()) ? size : AACP_ERROR_USB;
}

AACP_API int aacp_send_mic(AacpHandle h, const uint8_t* data, int size) {
    auto* c = ctx(h);
    if (!c || !data || size <= 0) return AACP_ERROR_INVALID_PARAM;
    if (c->session->getState() != AACP_STATE_CONNECTED) return AACP_ERROR_NOT_INIT;

    aacp::AacpFrame frame;
    frame.channel = aacp::Channel::MicAudio;
    frame.flags   = aacp::FLAG_NONE;
    frame.payload.assign(data, data + size);

    auto bytes = aacp::AacpProtocol::serialize(frame);
    return c->usb->send(bytes.data(), bytes.size()) ? size : AACP_ERROR_USB;
}

AACP_API int aacp_send_sensor(AacpHandle h, int sensor_type,
                               const uint8_t* data, int size) {
    auto* c = ctx(h);
    if (!c || !data || size <= 0) return AACP_ERROR_INVALID_PARAM;

    aacp::AacpFrame frame;
    frame.channel = aacp::Channel::Sensor;
    frame.flags   = aacp::FLAG_NONE;
    // First byte = sensor type
    frame.payload.push_back((uint8_t)sensor_type);
    frame.payload.insert(frame.payload.end(), data, data + size);

    auto bytes = aacp::AacpProtocol::serialize(frame);
    return c->usb->send(bytes.data(), bytes.size()) ? size : AACP_ERROR_USB;
}

// ── Diagnostics ───────────────────────────────────────────────────────────────

AACP_API AacpState aacp_get_state(AacpHandle h) {
    auto* c = ctx(h);
    if (!c) return AACP_STATE_ERROR;
    return c->session->getState();
}

AACP_API const char* aacp_version(void) {
    return "1.0.0";
}

} // extern "C"
