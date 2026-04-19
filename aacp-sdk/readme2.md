# 1. Verify môi trường
./scripts/verify_layers.sh --layer=0

# 2. Chạy unit tests (không cần Android, không cần iPhone)
cmake -B build_test -DAACP_BUILD_TESTS=ON && cmake --build build_test
./build_test/sdk-core/tests/protocol_test

# 3. Build .so cho Pi4
./scripts/build_so.sh Release
./scripts/verify_layers.sh --layer=3   # kiểm tra symbols

# 4. Build .aar cho app
./scripts/build_aar.sh release

# 5. Cắm iPhone, monitor USB
sudo ./scripts/watch_usb.sh
./scripts/verify_layers.sh --layer=1   # lsusb AOA detection

# 6. Deploy app lên Pi4, xem logcat
adb logcat -s "AACP_*:*" "CarPlayManager:*"