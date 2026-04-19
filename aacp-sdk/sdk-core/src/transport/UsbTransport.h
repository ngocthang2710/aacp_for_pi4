#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// UsbTransport.h
// Layer thấp nhất: giao tiếp trực tiếp với iPhone qua USB AOA 2.0
// Dùng libusb-1.0 — không phụ thuộc Android USB API để dễ test trên Linux
// ─────────────────────────────────────────────────────────────────────────────
#include <libusb-1.0/libusb.h>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>

namespace aacp {

// Apple USB vendor ID — tất cả iPhone/iPad
constexpr uint16_t APPLE_VENDOR_ID = 0x05AC;

// Product IDs sau khi switch sang AOA mode
// 0x1281 = AOA 1.0,  0x2D01 = AOA 2.0 (với audio)
constexpr uint16_t AOA_MODE_PID_AUDIO = 0x2D01;
constexpr uint16_t AOA_MODE_PID_PLAIN = 0x1281;

// Bulk endpoints — cố định theo AOA spec
constexpr uint8_t ENDPOINT_BULK_OUT = 0x02;  // host → device
constexpr uint8_t ENDPOINT_BULK_IN  = 0x81;  // device → host

// AOA vendor control request codes
constexpr uint8_t AOA_REQ_GET_PROTOCOL  = 51;
constexpr uint8_t AOA_REQ_SEND_STRING   = 52;
constexpr uint8_t AOA_REQ_START         = 53;
constexpr uint8_t AOA_REQ_ENABLE_AUDIO  = 58;

// USB timeouts
constexpr int USB_CTRL_TIMEOUT_MS  = 5000;
constexpr int USB_BULK_TIMEOUT_MS  = 100;   // ngắn để không block readLoop quá lâu
constexpr int READ_BUFFER_BYTES    = 65536; // 64 KB

// Accessory identification strings — iPhone dùng để nhận diện head unit
struct AoaStrings {
    const char* manufacturer = "RaspberryPi";
    const char* model        = "CarHeadUnit";
    const char* description  = "Car Head Unit with CarPlay";
    const char* version      = "1.0";
    const char* uri          = "https://example.com/carplay";
    const char* serial       = "AACP-PI4-001";
};

struct UsbFrame {
    std::vector<uint8_t> data;
    int64_t              timestamp_us;  // monotonic clock, microseconds
};

class UsbTransport {
public:
    using FrameCallback = std::function<void(const UsbFrame&)>;
    using ErrorCallback = std::function<void(int code, const std::string& msg)>;

    explicit UsbTransport(AoaStrings strings = {});
    ~UsbTransport();

    // Non-copyable, non-movable
    UsbTransport(const UsbTransport&)            = delete;
    UsbTransport& operator=(const UsbTransport&) = delete;

    /**
     * Khởi tạo libusb context, scan USB bus, tìm Apple device.
     * Nếu device chưa ở AOA mode: gửi identification strings + switch mode,
     * chờ re-enumerate, sau đó mở lại.
     * @return true nếu device ở AOA mode và sẵn sàng I/O
     */
    bool initialize();

    /**
     * Claim USB interface, bắt đầu readLoop thread.
     * @param onFrame   Callback khi có raw USB bytes đến
     * @param onError   Callback khi có USB error (device disconnect, etc.)
     */
    bool start(FrameCallback onFrame, ErrorCallback onError);

    /**
     * Gửi data xuống iPhone (bulk OUT).
     * Thread-safe — có thể gọi từ bất kỳ thread nào.
     */
    bool send(const uint8_t* data, size_t length);

    void stop();

    bool isConnected() const { return connected_.load(std::memory_order_acquire); }

    // Lấy USB product ID của device đang kết nối (để verify AOA mode)
    uint16_t getProductId() const { return productId_; }

private:
    bool scanAndOpenDevice();
    bool isAppleDevice(const libusb_device_descriptor& desc) const;
    bool isAoaMode(uint16_t pid) const;
    bool switchToAoaMode(libusb_device_handle* handle);
    bool sendAoaStrings(libusb_device_handle* handle);
    bool claimInterface();
    void readLoop();
    int64_t nowUs() const;

    libusb_context*       ctx_       = nullptr;
    libusb_device_handle* devHandle_ = nullptr;
    uint16_t              productId_ = 0;

    AoaStrings            strings_;
    FrameCallback         frameCallback_;
    ErrorCallback         errorCallback_;

    std::thread           readThread_;
    std::atomic<bool>     running_{false};
    std::atomic<bool>     connected_{false};

    // Mutex chỉ cho bulk write (read là single-thread)
    std::mutex            writeMutex_;
};

} // namespace aacp
