cmake_minimum_required(VERSION 3.22)
project(aacp_core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
if(NOT ANDROID)
    # Trên Linux/Host thì dùng pkg-config và find_package tiêu chuẩn
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
    find_package(OpenSSL REQUIRED)
else()
    # ── Android: Cấu hình "Cứng" để không phải truyền tham số -D nữa ──────────
    
    # 1. Khai báo libusb (Android)
    set(LIBUSB_ROOT "/home/thangnn/libusb/output/android-arm64")
    # Trỏ include về thư mục cha của libusb-1.0 để khớp với #include <libusb-1.0/libusb.h>
    set(LIBUSB_INCLUDE_DIRS "${LIBUSB_ROOT}/include/libusb-1.0") 
    
    add_library(libusb_android STATIC IMPORTED)
    set_target_properties(libusb_android PROPERTIES
        IMPORTED_LOCATION "${LIBUSB_ROOT}/lib/libusb-1.0.a")

    # 2. Khai báo OpenSSL (Android)
    set(OPENSSL_ROOT "/home/thangnn/openssl/output/android-arm64")
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT}/include")
    
    add_library(openssl_crypto STATIC IMPORTED)
    set_target_properties(openssl_crypto PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_ROOT}/lib/libcrypto.a")

    add_library(openssl_ssl STATIC IMPORTED)
    set_target_properties(openssl_ssl PROPERTIES
        IMPORTED_LOCATION "${OPENSSL_ROOT}/lib/libssl.a")
endif()

# ── Source files ─────────────────────────────────────────────────────────────
set(AACP_SOURCES
    src/transport/UsbTransport.cpp
    src/protocol/AacpProtocol.cpp
    src/session/SessionManager.cpp
    src/AacpSdk.cpp
)

# ── Shared library target ─────────────────────────────────────────────────────
add_library(aacp_core SHARED ${AACP_SOURCES})

# ── Include Directories ───────────────────────────────────────────────────────
target_include_directories(aacp_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    # Include cho libusb
    PRIVATE ${LIBUSB_INCLUDE_DIRS}
    # Include cho OpenSSL
    PRIVATE $<$<BOOL:${ANDROID}>:${OPENSSL_INCLUDE_DIR}>
)

# ── Link Libraries ────────────────────────────────────────────────────────────
target_link_libraries(aacp_core
    PRIVATE
    # Link libusb
    $<$<BOOL:${ANDROID}>:libusb_android>
    $<$<NOT:$<BOOL:${ANDROID}>>:${LIBUSB_LIBRARIES}>

    # Link OpenSSL
    $<$<BOOL:${ANDROID}>:openssl_ssl>
    $<$<BOOL:${ANDROID}>:openssl_crypto>
    $<$<NOT:$<BOOL:${ANDROID}>>:OpenSSL::SSL>
    $<$<NOT:$<BOOL:${ANDROID}>>:OpenSSL::Crypto>

    # Android-specific System Libs
    $<$<BOOL:${ANDROID}>:android>
    $<$<BOOL:${ANDROID}>:log>
    $<$<BOOL:${ANDROID}>:atomic>
    $<$<BOOL:${ANDROID}>:m>
)

# ── Properties & Visibility ──────────────────────────────────────────────────
set_target_properties(aacp_core PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 1
)

# ── Compile Definitions & Options ─────────────────────────────────────────────
target_compile_definitions(aacp_core PRIVATE
    AACP_VERSION_MAJOR=1
    AACP_VERSION_MINOR=0
    AACP_VERSION_PATCH=0
    $<$<CONFIG:Debug>:AACP_DEBUG=1>
)

target_compile_options(aacp_core PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g3 -O0>
)

# ── Install rules ─────────────────────────────────────────────────────────────
install(TARGETS aacp_core
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "[aacp_core] Target: aacp_core")
if(ANDROID)
    message(STATUS "[aacp_core] Using Android Prebuilt Libs (libusb & OpenSSL)")
else()
    message(STATUS "[aacp_core] Using System Libs")
endif()