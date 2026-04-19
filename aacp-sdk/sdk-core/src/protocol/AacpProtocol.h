#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AacpProtocol.h
// Parse raw USB bytes thành AACP frames và dispatch theo channel.
// Frame format (big-endian):
//   [4B: total_length][2B: channel][2B: flags][payload...]
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <vector>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <string>

namespace aacp {

// ── Channel IDs ───────────────────────────────────────────────────────────────
enum class Channel : uint16_t {
    Control   = 0x0000,  // Session management, version, auth
    Video     = 0x0001,  // H.264 Annex-B video stream
    Audio     = 0x0002,  // AAC-LC audio từ iPhone
    MicAudio  = 0x0003,  // PCM mic lên iPhone
    Touch     = 0x0004,  // Touch / button events
    Sensor    = 0x0005,  // GPS, speed, gear, night mode
    Bluetooth = 0x0006,  // BT pairing handoff
    Unknown   = 0xFFFF,
};

// ── Frame flags (bitfield) ────────────────────────────────────────────────────
enum FrameFlags : uint16_t {
    FLAG_NONE       = 0x0000,
    FLAG_KEY_FRAME  = 0x0001,  // Video IDR frame
    FLAG_ENCRYPTED  = 0x0002,  // TLS payload
    FLAG_FRAGMENTED = 0x0004,  // Đây là fragment, còn tiếp
    FLAG_LAST_FRAG  = 0x0008,  // Fragment cuối
};

// ── Frame header (packed — 8 bytes) ──────────────────────────────────────────
#pragma pack(push, 1)
struct AacpHeader {
    uint32_t total_length;  // big-endian: kích thước toàn frame (header + payload)
    uint16_t channel;       // big-endian: Channel enum
    uint16_t flags;         // big-endian: FrameFlags
};
#pragma pack(pop)

constexpr size_t HEADER_SIZE = sizeof(AacpHeader);  // = 8

// ── Parsed frame ──────────────────────────────────────────────────────────────
struct AacpFrame {
    Channel              channel;
    uint16_t             flags;
    std::vector<uint8_t> payload;
    int64_t              timestamp_us = 0;

    bool isKeyFrame()  const { return (flags & FLAG_KEY_FRAME)  != 0; }
    bool isEncrypted() const { return (flags & FLAG_ENCRYPTED)  != 0; }
};

// ── Control message types (trên Channel::Control) ─────────────────────────────
// Đây là sub-protocol bên trong control channel payload
// [2B: msg_type][payload...]
enum class ControlMsg : uint16_t {
    VersionRequest  = 0x0001,
    VersionResponse = 0x0002,
    SslHandshake    = 0x0003,
    AuthRequest     = 0x0004,
    AuthResponse    = 0x0005,
    AuthComplete    = 0x0006,
    Ping            = 0x0007,
    Pong            = 0x0008,
};

// ─────────────────────────────────────────────────────────────────────────────
class AacpProtocol {
public:
    using FrameHandler = std::function<void(const AacpFrame&)>;

    AacpProtocol() = default;
    ~AacpProtocol() = default;

    /**
     * Đẩy raw USB bytes vào parser.
     * Parser tự accumulate và extract complete frames.
     * Thread-safe (từ readLoop thread).
     */
    void feedData(const uint8_t* data, size_t length);

    /**
     * Đăng ký handler cho channel cụ thể.
     * Phải gọi trước start().
     */
    void setHandler(Channel ch, FrameHandler handler);

    /**
     * Serialize một AacpFrame thành bytes để gửi qua USB.
     * Static — không cần instance.
     */
    static std::vector<uint8_t> serialize(const AacpFrame& frame);

    /**
     * Helper: tạo control message frame.
     */
    static AacpFrame makeControlFrame(ControlMsg type,
                                       const std::vector<uint8_t>& payload = {});

    // Stats — để debug
    uint64_t getFramesReceived() const { return framesReceived_; }
    uint64_t getBytesReceived()  const { return bytesReceived_;  }
    uint64_t getParseErrors()   const { return parseErrors_;    }

private:
    void processBuffer();
    void dispatchFrame(AacpFrame& frame);
    static Channel toChannel(uint16_t raw);

    std::vector<uint8_t>  rxBuf_;
    std::mutex            rxMutex_;

    std::unordered_map<uint16_t, FrameHandler> handlers_;

    // Fragment reassembly: channel → accumulated bytes
    std::unordered_map<uint16_t, std::vector<uint8_t>> fragBufs_;

    // Stats
    std::atomic<uint64_t> framesReceived_{0};
    std::atomic<uint64_t> bytesReceived_{0};
    std::atomic<uint64_t> parseErrors_{0};
};

} // namespace aacp
