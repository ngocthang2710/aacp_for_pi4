// ─────────────────────────────────────────────────────────────────────────────
// AacpProtocol.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "AacpProtocol.h"
#include <arpa/inet.h>   // ntohl, ntohs, htonl, htons
#include <chrono>
#include <stdexcept>

#ifdef ANDROID
    #include <android/log.h>
    #define TAG "AACP_PROTO"
    #define LOGI(fmt,...) __android_log_print(ANDROID_LOG_INFO,  TAG, fmt, ##__VA_ARGS__)
    #define LOGE(fmt,...) __android_log_print(ANDROID_LOG_ERROR, TAG, fmt, ##__VA_ARGS__)
    #define LOGD(fmt,...) __android_log_print(ANDROID_LOG_DEBUG, TAG, fmt, ##__VA_ARGS__)
#else
    #include <cstdio>
    #define LOGI(fmt,...) fprintf(stdout, "[PROTO INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOGE(fmt,...) fprintf(stderr, "[PROTO ERR ] " fmt "\n", ##__VA_ARGS__)
    #define LOGD(fmt,...) do { if(getenv("AACP_VERBOSE")) fprintf(stdout, "[PROTO DBG ] " fmt "\n", ##__VA_ARGS__); } while(0)
#endif

namespace aacp {

static int64_t nowUs() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

// ── Public API ────────────────────────────────────────────────────────────────

void AacpProtocol::feedData(const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(rxMutex_);
    rxBuf_.insert(rxBuf_.end(), data, data + length);
    bytesReceived_.fetch_add(length, std::memory_order_relaxed);
    processBuffer();
}

void AacpProtocol::setHandler(Channel ch, FrameHandler handler) {
    handlers_[static_cast<uint16_t>(ch)] = std::move(handler);
}

// ── Parse loop ────────────────────────────────────────────────────────────────

void AacpProtocol::processBuffer() {
    // rxMutex_ đã held bởi caller

    while (rxBuf_.size() >= HEADER_SIZE) {
        // Peek header
        const auto* hdr = reinterpret_cast<const AacpHeader*>(rxBuf_.data());
        uint32_t frameLen = ntohl(hdr->total_length);

        // Sanity check: frame quá lớn → corrupted stream
        if (frameLen < HEADER_SIZE || frameLen > 10 * 1024 * 1024 /* 10MB */) {
            LOGE("Invalid frame length: %u — resetting buffer", frameLen);
            rxBuf_.clear();
            parseErrors_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // Chưa đủ dữ liệu cho frame này — chờ thêm
        if (rxBuf_.size() < frameLen) break;

        // Extract frame
        AacpFrame frame;
        frame.channel      = toChannel(ntohs(hdr->channel));
        frame.flags        = ntohs(hdr->flags);
        frame.timestamp_us = nowUs();
        frame.payload.assign(
            rxBuf_.begin() + HEADER_SIZE,
            rxBuf_.begin() + frameLen
        );

        // Consume bytes
        rxBuf_.erase(rxBuf_.begin(), rxBuf_.begin() + frameLen);
        framesReceived_.fetch_add(1, std::memory_order_relaxed);

        LOGD("Frame: ch=%u flags=0x%04x payload=%zu bytes",
             static_cast<uint16_t>(frame.channel), frame.flags, frame.payload.size());

        // Fragment reassembly
        bool isFragmented = (frame.flags & FLAG_FRAGMENTED) != 0;
        bool isLastFrag   = (frame.flags & FLAG_LAST_FRAG)  != 0;

        if (isFragmented) {
            uint16_t chKey = static_cast<uint16_t>(frame.channel);
            auto& fragBuf = fragBufs_[chKey];
            fragBuf.insert(fragBuf.end(), frame.payload.begin(), frame.payload.end());

            if (isLastFrag) {
                // Reassembly complete
                frame.payload = std::move(fragBuf);
                fragBufs_.erase(chKey);
                frame.flags &= ~(FLAG_FRAGMENTED | FLAG_LAST_FRAG);
                dispatchFrame(frame);
            }
            // Nếu không phải last frag: tiếp tục accumulate, không dispatch
        } else {
            dispatchFrame(frame);
        }
    }
}

void AacpProtocol::dispatchFrame(AacpFrame& frame) {
    uint16_t key = static_cast<uint16_t>(frame.channel);
    auto it = handlers_.find(key);
    if (it != handlers_.end()) {
        it->second(frame);
    } else {
        LOGD("No handler for channel %u — dropping frame", key);
    }
}

Channel AacpProtocol::toChannel(uint16_t raw) {
    switch (raw) {
        case 0: return Channel::Control;
        case 1: return Channel::Video;
        case 2: return Channel::Audio;
        case 3: return Channel::MicAudio;
        case 4: return Channel::Touch;
        case 5: return Channel::Sensor;
        case 6: return Channel::Bluetooth;
        default: return Channel::Unknown;
    }
}

// ── Serialization ─────────────────────────────────────────────────────────────

std::vector<uint8_t> AacpProtocol::serialize(const AacpFrame& frame) {
    size_t totalLen = HEADER_SIZE + frame.payload.size();
    std::vector<uint8_t> out(totalLen);

    auto* hdr = reinterpret_cast<AacpHeader*>(out.data());
    hdr->total_length = htonl(static_cast<uint32_t>(totalLen));
    hdr->channel      = htons(static_cast<uint16_t>(frame.channel));
    hdr->flags        = htons(frame.flags);

    if (!frame.payload.empty()) {
        std::copy(frame.payload.begin(), frame.payload.end(),
                  out.begin() + HEADER_SIZE);
    }
    return out;
}

AacpFrame AacpProtocol::makeControlFrame(ControlMsg type,
                                           const std::vector<uint8_t>& payload) {
    AacpFrame frame;
    frame.channel = Channel::Control;
    frame.flags   = FLAG_NONE;

    uint16_t t = static_cast<uint16_t>(type);
    frame.payload.push_back((t >> 8) & 0xFF);
    frame.payload.push_back(t & 0xFF);
    frame.payload.insert(frame.payload.end(), payload.begin(), payload.end());
    return frame;
}

} // namespace aacp
