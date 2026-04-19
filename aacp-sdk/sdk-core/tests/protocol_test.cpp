// ─────────────────────────────────────────────────────────────────────────────
// protocol_test.cpp
// Unit tests cho AacpProtocol — không cần gtest, chạy standalone trên Linux
// Build: cmake -B build_test -DAACP_BUILD_TESTS=ON && cmake --build build_test
// Run:   ./build_test/sdk-core/tests/protocol_test
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/protocol/AacpProtocol.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

// ── Test helpers ──────────────────────────────────────────────────────────────
static int passed = 0;
static int failed = 0;

#define TEST(name) void test_##name()
#define RUN(name)  do { \
    printf("  %-50s", #name); \
    try { test_##name(); printf("PASS\n"); passed++; } \
    catch (const std::exception& e) { printf("FAIL: %s\n", e.what()); failed++; } \
    catch (...) { printf("FAIL: unknown exception\n"); failed++; } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond \
        " at line " + std::to_string(__LINE__)); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Expected ") + std::to_string(b) + \
        " got " + std::to_string(a) + " at line " + std::to_string(__LINE__)); \
} while(0)

// ── Helper: build raw AACP frame bytes ────────────────────────────────────────
static std::vector<uint8_t> makeFrame(uint16_t channel, uint16_t flags,
                                       const std::vector<uint8_t>& payload) {
    uint32_t totalLen = 8 + (uint32_t)payload.size();
    std::vector<uint8_t> out(totalLen);

    // Big-endian
    out[0] = (totalLen >> 24) & 0xFF;
    out[1] = (totalLen >> 16) & 0xFF;
    out[2] = (totalLen >>  8) & 0xFF;
    out[3] =  totalLen        & 0xFF;
    out[4] = (channel >> 8)   & 0xFF;
    out[5] =  channel         & 0xFF;
    out[6] = (flags   >> 8)   & 0xFF;
    out[7] =  flags            & 0xFF;
    std::copy(payload.begin(), payload.end(), out.begin() + 8);
    return out;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST(single_frame_dispatch) {
    aacp::AacpProtocol proto;
    int callCount = 0;
    std::vector<uint8_t> receivedPayload;

    proto.setHandler(aacp::Channel::Control,
        [&](const aacp::AacpFrame& f) {
            callCount++;
            receivedPayload = f.payload;
        }
    );

    std::vector<uint8_t> payload = {0x00, 0x01, 0xAA, 0xBB};
    auto frame = makeFrame(0 /*Control*/, 0, payload);
    proto.feedData(frame.data(), frame.size());

    ASSERT_EQ(callCount, 1);
    ASSERT(receivedPayload == payload);
}

TEST(multiple_frames_in_one_feed) {
    aacp::AacpProtocol proto;
    int videoCount = 0, audioCount = 0;

    proto.setHandler(aacp::Channel::Video,
        [&](const aacp::AacpFrame&) { videoCount++; });
    proto.setHandler(aacp::Channel::Audio,
        [&](const aacp::AacpFrame&) { audioCount++; });

    // Concatenate 3 video + 2 audio frames
    std::vector<uint8_t> buf;
    std::vector<uint8_t> vPayload(100, 0xAA);
    std::vector<uint8_t> aPayload(200, 0xBB);
    for (int i = 0; i < 3; i++) {
        auto f = makeFrame(1 /*Video*/, 0x0001, vPayload);
        buf.insert(buf.end(), f.begin(), f.end());
    }
    for (int i = 0; i < 2; i++) {
        auto f = makeFrame(2 /*Audio*/, 0, aPayload);
        buf.insert(buf.end(), f.begin(), f.end());
    }

    proto.feedData(buf.data(), buf.size());

    ASSERT_EQ(videoCount, 3);
    ASSERT_EQ(audioCount, 2);
}

TEST(split_across_usb_packets) {
    // Giả lập frame bị split thành nhiều USB bulk transfers
    aacp::AacpProtocol proto;
    int callCount = 0;
    proto.setHandler(aacp::Channel::Control,
        [&](const aacp::AacpFrame&) { callCount++; });

    std::vector<uint8_t> payload(500, 0x42);
    auto frame = makeFrame(0, 0, payload);  // 508 bytes total

    // Gửi từng byte một (worst case)
    for (size_t i = 0; i < frame.size(); i++) {
        proto.feedData(&frame[i], 1);
    }
    ASSERT_EQ(callCount, 1);  // Phải dispatch đúng 1 lần

    // Gửi theo chunk 64 bytes (USB packet size)
    aacp::AacpProtocol proto2;
    int callCount2 = 0;
    proto2.setHandler(aacp::Channel::Control,
        [&](const aacp::AacpFrame&) { callCount2++; });

    for (size_t i = 0; i < frame.size(); i += 64) {
        size_t chunk = std::min((size_t)64, frame.size() - i);
        proto2.feedData(frame.data() + i, chunk);
    }
    ASSERT_EQ(callCount2, 1);
}

TEST(fragment_reassembly) {
    aacp::AacpProtocol proto;
    std::vector<uint8_t> reassembled;

    proto.setHandler(aacp::Channel::Video,
        [&](const aacp::AacpFrame& f) {
            reassembled = f.payload;
        }
    );

    // Fragment 1 (FLAG_FRAGMENTED = 0x04, not last)
    std::vector<uint8_t> frag1(256, 0x11);
    auto f1 = makeFrame(1 /*Video*/, 0x0004 /*FRAGMENTED*/, frag1);

    // Fragment 2 (FLAG_FRAGMENTED | FLAG_LAST_FRAG = 0x04 | 0x08 = 0x0C)
    std::vector<uint8_t> frag2(256, 0x22);
    auto f2 = makeFrame(1 /*Video*/, 0x000C /*FRAG+LAST*/, frag2);

    proto.feedData(f1.data(), f1.size());
    ASSERT(reassembled.empty());  // Chưa dispatch khi còn fragment

    proto.feedData(f2.data(), f2.size());
    ASSERT_EQ((int)reassembled.size(), 512);  // frag1 + frag2
    ASSERT(reassembled[0]   == 0x11);
    ASSERT(reassembled[255] == 0x11);
    ASSERT(reassembled[256] == 0x22);
    ASSERT(reassembled[511] == 0x22);
}

TEST(no_handler_no_crash) {
    // Frame đến khi không có handler → silently drop, không crash
    aacp::AacpProtocol proto;
    std::vector<uint8_t> payload = {1, 2, 3};
    auto frame = makeFrame(0xFF /*Unknown*/, 0, payload);

    // Không nên throw hay crash
    proto.feedData(frame.data(), frame.size());
    // Nếu đến đây không crash → PASS
}

TEST(corrupted_length_reset) {
    // Frame với length quá lớn → parser reset buffer, không crash
    aacp::AacpProtocol proto;
    int callCount = 0;
    proto.setHandler(aacp::Channel::Control,
        [&](const aacp::AacpFrame&) { callCount++; });

    // Corrupt frame: length = 999MB
    uint8_t corrupt[] = {
        0x3B, 0x9A, 0xCA, 0x00,  // 999,999,488 bytes — too large
        0x00, 0x00,
        0x00, 0x00,
        0xDE, 0xAD
    };
    proto.feedData(corrupt, sizeof(corrupt));

    // Parser phải reset — không crash
    // Gửi valid frame sau đó → phải hoạt động
    std::vector<uint8_t> payload = {0xAA};
    auto good = makeFrame(0, 0, payload);
    proto.feedData(good.data(), good.size());

    ASSERT_EQ(callCount, 1);  // Valid frame sau corrupt phải được dispatch
}

TEST(serialize_deserialize_roundtrip) {
    aacp::AacpFrame original;
    original.channel = aacp::Channel::Audio;
    original.flags   = aacp::FLAG_KEY_FRAME;
    original.payload = {0x12, 0x10, 0x00, 0x01, 0xFF, 0xEE};

    auto bytes = aacp::AacpProtocol::serialize(original);

    // Parse back
    aacp::AacpProtocol proto;
    aacp::AacpFrame parsed;
    proto.setHandler(aacp::Channel::Audio,
        [&](const aacp::AacpFrame& f) { parsed = f; });

    proto.feedData(bytes.data(), bytes.size());

    ASSERT(parsed.channel == aacp::Channel::Audio);
    ASSERT_EQ(parsed.flags, aacp::FLAG_KEY_FRAME);
    ASSERT(parsed.payload == original.payload);
}

TEST(make_control_frame) {
    auto frame = aacp::AacpProtocol::makeControlFrame(
        aacp::ControlMsg::VersionRequest,
        {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00}
    );

    ASSERT(frame.channel == aacp::Channel::Control);
    ASSERT(frame.flags   == aacp::FLAG_NONE);
    // First 2 bytes = msg_type = 0x0001
    ASSERT(frame.payload.size() >= 2);
    ASSERT_EQ(frame.payload[0], 0x00);
    ASSERT_EQ(frame.payload[1], 0x01);
}

TEST(stats_tracking) {
    aacp::AacpProtocol proto;
    proto.setHandler(aacp::Channel::Video, [](const aacp::AacpFrame&){});

    ASSERT_EQ((int)proto.getFramesReceived(), 0);
    ASSERT_EQ((int)proto.getBytesReceived(),  0);

    for (int i = 0; i < 5; i++) {
        auto f = makeFrame(1, 0, {0xAA, 0xBB});
        proto.feedData(f.data(), f.size());
    }

    ASSERT_EQ((int)proto.getFramesReceived(), 5);
    ASSERT((int)proto.getBytesReceived() > 0);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  AACP Protocol Parser — Unit Tests\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    RUN(single_frame_dispatch);
    RUN(multiple_frames_in_one_feed);
    RUN(split_across_usb_packets);
    RUN(fragment_reassembly);
    RUN(no_handler_no_crash);
    RUN(corrupted_length_reset);
    RUN(serialize_deserialize_roundtrip);
    RUN(make_control_frame);
    RUN(stats_tracking);

    printf("\n");
    printf("═══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed\n", passed, failed);
    printf("═══════════════════════════════════════════════════════\n\n");

    return (failed > 0) ? 1 : 0;
}
