// ─────────────────────────────────────────────────────────────────────────────
// UsbTransport.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "UsbTransport.h"
#include <chrono>
#include <thread>
#include <cstring>
#include <mutex>

// Logging — nếu build cho Android thì dùng __android_log_print,
// nếu không thì dùng fprintf để test trên Linux
#ifdef ANDROID
    #include <android/log.h>
    #define LOG_TAG "AACP_USB"
    #define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, fmt, ##__VA_ARGS__)
    #define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, fmt, ##__VA_ARGS__)
    #define LOGE(fmt, ...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
    #define LOGD(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    #define LOGI(fmt, ...) fprintf(stdout, "[USB INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOGW(fmt, ...) fprintf(stdout, "[USB WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOGE(fmt, ...) fprintf(stderr, "[USB ERR ] " fmt "\n", ##__VA_ARGS__)
    #define LOGD(fmt, ...) do { if(getenv("AACP_VERBOSE")) fprintf(stdout, "[USB DBG ] " fmt "\n", ##__VA_ARGS__); } while(0)
#endif

namespace aacp {

UsbTransport::UsbTransport(AoaStrings strings)
    : strings_(strings) {
    int ret = libusb_init(&ctx_);
    if (ret != 0) {
        LOGE("libusb_init failed: %s", libusb_error_name(ret));
        ctx_ = nullptr;
    }
#ifndef ANDROID
    // Trên Linux dev machine: bật warning log
    libusb_set_option(ctx_, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
#endif
}

UsbTransport::~UsbTransport() {
    stop();
    if (ctx_) {
        libusb_exit(ctx_);
        ctx_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
bool UsbTransport::initialize() {
    if (!ctx_) return false;
    return scanAndOpenDevice();
}

bool UsbTransport::scanAndOpenDevice() {
    libusb_device** list = nullptr;
    ssize_t count = libusb_get_device_list(ctx_, &list);
    if (count < 0) {
        LOGE("get_device_list: %s", libusb_error_name((int)count));
        return false;
    }

    bool result = false;
    for (ssize_t i = 0; i < count; i++) {
        libusb_device_descriptor desc{};
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (!isAppleDevice(desc)) continue;

        LOGI("Found Apple device: VID=0x%04x PID=0x%04x", desc.idVendor, desc.idProduct);

        libusb_device_handle* handle = nullptr;
        int ret = libusb_open(list[i], &handle);
        if (ret != 0) {
            LOGW("Cannot open device: %s", libusb_error_name(ret));
            continue;
        }

        if (isAoaMode(desc.idProduct)) {
            // Device đã ở AOA mode — có thể do lần connect trước chưa reset
            LOGI("Device already in AOA mode (PID=0x%04x)", desc.idProduct);
            devHandle_ = handle;
            productId_ = desc.idProduct;
            result = true;
        } else {
            // Switch sang AOA mode
            result = switchToAoaMode(handle);
            // switchToAoaMode đóng handle và gọi lại scanAndOpenDevice
        }
        break;
    }

    libusb_free_device_list(list, 1);
    return result;
}

bool UsbTransport::isAppleDevice(const libusb_device_descriptor& desc) const {
    return desc.idVendor == APPLE_VENDOR_ID;
}

bool UsbTransport::isAoaMode(uint16_t pid) const {
    return (pid == AOA_MODE_PID_AUDIO || pid == AOA_MODE_PID_PLAIN);
}

// ─────────────────────────────────────────────────────────────────────────────
// AOA Mode Switch — 4 bước theo AOA 2.0 spec
// ─────────────────────────────────────────────────────────────────────────────
bool UsbTransport::switchToAoaMode(libusb_device_handle* handle) {
    // Bước 1: GET_PROTOCOL — verify device hỗ trợ AOA
    uint8_t protocol[2] = {0};
    int ret = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_REQ_GET_PROTOCOL,
        0, 0,
        protocol, sizeof(protocol),
        USB_CTRL_TIMEOUT_MS
    );
    if (ret < 0) {
        LOGE("GET_PROTOCOL failed: %s", libusb_error_name(ret));
        libusb_close(handle);
        return false;
    }
    uint16_t ver = (uint16_t)(protocol[0] | (protocol[1] << 8));
    LOGI("AOA protocol version: %u", ver);
    if (ver < 1) {
        LOGE("Device does not support AOA (version=%u)", ver);
        libusb_close(handle);
        return false;
    }

    // Bước 2: Gửi identification strings (6 strings, index 0..5)
    if (!sendAoaStrings(handle)) {
        libusb_close(handle);
        return false;
    }

    // Bước 3: Enable audio channel (AOA 2.0 — cần cho CarPlay audio)
    uint8_t audioMode = 1;
    libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_REQ_ENABLE_AUDIO,
        audioMode, 0,
        nullptr, 0,
        USB_CTRL_TIMEOUT_MS
    );
    LOGI("Audio enabled");

    // Bước 4: START_ACCESSORY — device sẽ disconnect và reconnect
    ret = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
        AOA_REQ_START,
        0, 0, nullptr, 0,
        USB_CTRL_TIMEOUT_MS
    );
    // ret có thể là error vì device disconnect ngay — bỏ qua
    libusb_close(handle);
    LOGI("AOA START sent, waiting for re-enumerate...");

    // Chờ device re-enumerate (~1.5-2 giây)
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    return scanAndOpenDevice();
}

bool UsbTransport::sendAoaStrings(libusb_device_handle* handle) {
    struct { uint16_t index; const char* value; } entries[] = {
        { 0, strings_.manufacturer },
        { 1, strings_.model        },
        { 2, strings_.description  },
        { 3, strings_.version      },
        { 4, strings_.uri          },
        { 5, strings_.serial       },
    };
    for (auto& e : entries) {
        size_t len = strlen(e.value) + 1;  // +1 để include null terminator
        int ret = libusb_control_transfer(
            handle,
            LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR,
            AOA_REQ_SEND_STRING,
            0, e.index,
            (uint8_t*)e.value, (uint16_t)len,
            USB_CTRL_TIMEOUT_MS
        );
        if (ret < 0) {
            LOGE("SEND_STRING[%u] '%s' failed: %s", e.index, e.value, libusb_error_name(ret));
            return false;
        }
        LOGD("SEND_STRING[%u] = '%s' OK", e.index, e.value);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
bool UsbTransport::claimInterface() {
    // Detach kernel driver nếu có (Linux)
    libusb_set_auto_detach_kernel_driver(devHandle_, 1);

    int ret = libusb_claim_interface(devHandle_, 0);
    if (ret != 0) {
        LOGE("claim_interface(0) failed: %s", libusb_error_name(ret));
        return false;
    }
    LOGI("Interface 0 claimed");
    return true;
}

bool UsbTransport::start(FrameCallback onFrame, ErrorCallback onError) {
    if (!devHandle_) {
        LOGE("start() called but device not open");
        return false;
    }
    frameCallback_ = std::move(onFrame);
    errorCallback_ = std::move(onError);

    if (!claimInterface()) return false;

    running_.store(true, std::memory_order_release);
    connected_.store(true, std::memory_order_release);
    readThread_ = std::thread(&UsbTransport::readLoop, this);
    LOGI("USB transport started — PID=0x%04x", productId_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Read loop — chạy trên background thread
// ─────────────────────────────────────────────────────────────────────────────
void UsbTransport::readLoop() {
    std::vector<uint8_t> buf(READ_BUFFER_BYTES);

    while (running_.load(std::memory_order_acquire)) {
        int transferred = 0;
        int ret = libusb_bulk_transfer(
            devHandle_,
            ENDPOINT_BULK_IN,
            buf.data(),
            (int)buf.size(),
            &transferred,
            USB_BULK_TIMEOUT_MS
        );

        if (ret == LIBUSB_ERROR_TIMEOUT) {
            // Timeout bình thường — tiếp tục vòng lặp
            continue;
        }

        if (ret != LIBUSB_SUCCESS) {
            LOGE("bulk_transfer(IN) error: %s", libusb_error_name(ret));
            connected_.store(false, std::memory_order_release);
            if (errorCallback_) {
                errorCallback_(ret, libusb_error_name(ret));
            }
            break;
        }

        if (transferred > 0 && frameCallback_) {
            UsbFrame frame;
            frame.data.assign(buf.begin(), buf.begin() + transferred);
            frame.timestamp_us = nowUs();
            frameCallback_(frame);
        }
    }
    LOGI("readLoop exited");
}

bool UsbTransport::send(const uint8_t* data, size_t length) {
    if (!connected_.load(std::memory_order_acquire)) return false;
    std::lock_guard<std::mutex> lock(writeMutex_);

    int transferred = 0;
    int ret = libusb_bulk_transfer(
        devHandle_,
        ENDPOINT_BULK_OUT,
        const_cast<uint8_t*>(data),
        (int)length,
        &transferred,
        USB_BULK_TIMEOUT_MS
    );
    if (ret != LIBUSB_SUCCESS) {
        LOGE("bulk_transfer(OUT) error: %s", libusb_error_name(ret));
        return false;
    }
    return transferred == (int)length;
}

void UsbTransport::stop() {
    running_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);

    if (readThread_.joinable()) {
        readThread_.join();
    }
    if (devHandle_) {
        libusb_release_interface(devHandle_, 0);
        libusb_close(devHandle_);
        devHandle_ = nullptr;
    }
    LOGI("USB transport stopped");
}

int64_t UsbTransport::nowUs() const {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

} // namespace aacp
