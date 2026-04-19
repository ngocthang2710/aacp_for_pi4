# AACP SDK — Apple CarPlay over USB for Android

SDK này implement lớp transport và protocol để kết nối iPhone qua USB AOA 2.0
và thiết lập phiên CarPlay trên Android (Raspberry Pi 4 + AOSP 15).

---

## Cấu trúc thư mục

```
aacp-sdk/
│
├── CMakeLists.txt                  ← Root CMake (build sdk-core + tests)
├── settings.gradle.kts             ← Gradle multi-module config
├── build.gradle.kts                ← Root Gradle
├── gradle/libs.versions.toml       ← Dependency version catalog
│
├── sdk-core/                       ← C++ native core → libaacp_core.so
│   ├── CMakeLists.txt
│   ├── include/aacp/
│   │   ├── AacpSdk.h               ← PUBLIC C API (duy nhất file consumer cần include)
│   │   ├── Types.h                 ← Enums, structs, constants
│   │   └── Callbacks.h             ← Callback function pointer types
│   ├── src/
│   │   ├── transport/
│   │   │   ├── UsbTransport.h      ← libusb-1.0 AOA 2.0 implementation
│   │   │   └── UsbTransport.cpp
│   │   ├── protocol/
│   │   │   ├── AacpProtocol.h      ← Frame parser + serializer
│   │   │   └── AacpProtocol.cpp
│   │   ├── session/
│   │   │   ├── SessionManager.h    ← TLS handshake + session state machine
│   │   │   └── SessionManager.cpp
│   │   └── AacpSdk.cpp             ← C API entry point (AacpHandle)
│   └── tests/
│       ├── CMakeLists.txt
│       └── protocol_test.cpp       ← Standalone unit tests (no gtest needed)
│
├── sdk-android/                    ← Kotlin wrapper → aacp-sdk-1.0.0.aar
│   ├── CMakeLists.txt              ← JNI bridge CMake (links sdk-core)
│   ├── build.gradle.kts
│   ├── proguard-rules.pro
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── cpp/
│       │   └── jni_bridge.cpp      ← JNI glue (C API ↔ Kotlin lambdas)
│       └── kotlin/com/aacp/sdk/
│           ├── CarPlayManager.kt   ← Main SDK class (public API)
│           ├── media/
│           │   ├── VideoDecoder.kt ← H.264 via MediaCodec (low-latency)
│           │   └── AudioPlayer.kt  ← AAC-LC via MediaCodec + AudioTrack
│           └── input/
│               ├── TouchForwarder.kt ← MotionEvent → binary packet
│               └── MicCapture.kt     ← PCM 16kHz mono → iPhone (Siri)
│
├── sdk-test/                       ← Demo app (dùng SDK, cài lên Pi4)
│   ├── build.gradle.kts
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── res/
│       │   ├── layout/activity_carplay.xml
│       │   └── xml/device_filter.xml   ← USB auto-launch filter
│       └── kotlin/com/aacp/demo/
│           └── CarPlayActivity.kt
│
├── scripts/
│   ├── build_so.sh         ← Build libaacp_core.so (standalone, no Gradle)
│   ├── build_aar.sh        ← Build aacp-sdk-1.0.0.aar via Gradle
│   ├── verify_layers.sh    ← Verify từng layer: env/usb/protocol/so/logcat
│   └── watch_usb.sh        ← Real-time USB event monitor
│
└── outputs/                ← Build artifacts (git-ignored)
    ├── aacp-sdk-1.0.0.aar
    └── jniLibs/arm64-v8a/
        ├── libaacp_core.so
        └── libaacp_jni.so
```

---

## Yêu cầu hệ thống

### Build machine (Linux x86_64)
- CMake >= 3.22
- Android NDK r27 (`ANDROID_NDK_HOME=/path/to/ndk`)
- JDK 17 (cho Gradle/AAR build)
- libusb-1.0-dev (để test trên Linux, cross-compile cho ARM64)
- libssl-dev (OpenSSL)

```bash
# Ubuntu/Debian
sudo apt install cmake ninja-build libusb-1.0-0-dev libssl-dev openjdk-17-jdk
```

### Target (Pi4 AOSP Android 15)
- Android 10+ (API 29+)
- USB Host mode enabled trong kernel
- Permission: `android.permission.RECORD_AUDIO`

---

## Build

### Option A — Build .so (native only, cho AOSP integration)

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r27

./scripts/build_so.sh Release
# Output: outputs/jniLibs/arm64-v8a/libaacp_core.so
```

### Option B — Build .aar (cho Android app)

```bash
./scripts/build_aar.sh release
# Output: outputs/aacp-sdk-1.0.0.aar
```

### Option C — Build và chạy unit tests trên Linux

```bash
cmake -B build_test \
    -DAACP_BUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE=Debug \
    -S .

cmake --build build_test --target protocol_test -j$(nproc)
./build_test/sdk-core/tests/protocol_test
```

---

## Dùng SDK trong app

### Bước 1: Thêm .aar vào project

```
app/
└── libs/
    └── aacp-sdk-1.0.0.aar
```

```kotlin
// app/build.gradle.kts
dependencies {
    implementation(files("libs/aacp-sdk-1.0.0.aar"))
}
```

### Bước 2: Layout (activity_main.xml)

```xml
<SurfaceView
    android:id="@+id/surface_view"
    android:layout_width="match_parent"
    android:layout_height="match_parent" />
```

### Bước 3: Activity code

```kotlin
class MainActivity : AppCompatActivity(), SurfaceHolder.Callback {

    private val carPlay by lazy { CarPlayManager(this) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<SurfaceView>(R.id.surface_view).holder.addCallback(this)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        carPlay.initialize(holder.surface)   // null cert = dev mode
        carPlay.listener = object : CarPlayManager.Listener {
            override fun onConnected()              { /* show UI */ }
            override fun onDisconnected()           { /* show waiting */ }
            override fun onError(code: Int, msg: String) { /* show error */ }
        }
        carPlay.start()
    }

    override fun onTouchEvent(e: MotionEvent) =
        carPlay.onTouchEvent(e) || super.onTouchEvent(e)

    override fun onDestroy() { super.onDestroy(); carPlay.release() }
    override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) {}
    override fun surfaceDestroyed(holder: SurfaceHolder) { carPlay.release() }
}
```

---

## Verify từng layer

```bash
# Kiểm tra toàn bộ (không cần device)
./scripts/verify_layers.sh

# Layer cụ thể
./scripts/verify_layers.sh --layer=0   # Environment
./scripts/verify_layers.sh --layer=1   # USB (lsusb)
./scripts/verify_layers.sh --layer=2   # Protocol parser (Python test)
./scripts/verify_layers.sh --layer=3   # .so symbols
./scripts/verify_layers.sh --layer=4   # ADB logcat (cần device)

# Monitor USB real-time
sudo ./scripts/watch_usb.sh

# Capture USB packets với Wireshark
sudo modprobe usbmon
sudo tshark -i usbmon1 -f 'usb.idVendor == 0x05ac' -w /tmp/carplay.pcap
```

---

## Protocol layers và expected logs

```
iPhone cắm vào
│
├─ [AACP_USB] Found Apple device: VID=0x05ac PID=0x12a8
├─ [AACP_USB] AOA protocol version: 2
├─ [AACP_USB] Switching to AOA mode...
├─ [AACP_USB] Device in AOA mode (PID=0x2d01)        ← USB layer OK ✓
├─ [AACP_USB] Interface 0 claimed
│
├─ [AACP_Session] Version request sent               ← Protocol layer OK ✓
├─ [AACP_Session] Peer version: 1.0
├─ [AACP_Session] SSL context initialized (TLS 1.2)
├─ [AACP_Session] State: CONNECTING → AUTHENTICATING
│
├─ [AACP_Session] TLS handshake complete             ← TLS layer OK ✓
│
└─ *** STOPS HERE without MFi certificate ***
   [AACP_Session] State: AUTHENTICATING → ERROR (AACP_ERROR_AUTH=-4)
   iPhone disconnects or shows "Accessory not supported"
```

---

## Giới hạn hiện tại

| Layer | Trạng thái | Ghi chú |
|-------|-----------|---------|
| USB AOA 2.0 | ✅ Complete | lsusb verify được |
| AACP framing | ✅ Complete | Unit tests pass |
| TLS handshake | ✅ Complete | Dùng memory BIO |
| iAP2 auth | ❌ Blocked | Cần MFi hardware chip |
| Video decode | ✅ Complete | MediaCodec H.264 |
| Audio decode | ✅ Complete | MediaCodec AAC-LC |
| Touch input | ✅ Complete | Binary serialization |
| Mic capture | ✅ Complete | PCM 16kHz mono |

---

## License

MIT License — xem LICENSE file.
Không bảo đảm tương thích với Apple CarPlay chính thức khi không có MFi certificate.
