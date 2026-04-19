cmake_minimum_required(VERSION 3.22)
project(aacp_core VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)
find_package(OpenSSL REQUIRED)

# ── Source files ─────────────────────────────────────────────────────────────
set(AACP_SOURCES
    src/transport/UsbTransport.cpp
    src/protocol/AacpProtocol.cpp
    src/session/SessionManager.cpp
    src/AacpSdk.cpp
)

# ── Shared library target ─────────────────────────────────────────────────────
add_library(aacp_core SHARED ${AACP_SOURCES})

# Public include: consumer chỉ thấy include/aacp/
target_include_directories(aacp_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${LIBUSB_INCLUDE_DIRS}
    PRIVATE ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(aacp_core
    PRIVATE ${LIBUSB_LIBRARIES}
    PRIVATE OpenSSL::SSL
    PRIVATE OpenSSL::Crypto
    # Android-specific (chỉ link khi cross-compile)
    $<$<BOOL:${ANDROID}>:android>
    $<$<BOOL:${ANDROID}>:log>
)

# Ẩn internal symbols — chỉ export symbols được đánh dấu AACP_API
set_target_properties(aacp_core PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 1
)

# Compile definitions
target_compile_definitions(aacp_core PRIVATE
    AACP_VERSION_MAJOR=1
    AACP_VERSION_MINOR=0
    AACP_VERSION_PATCH=0
    $<$<CONFIG:Debug>:AACP_DEBUG=1>
)

# Compile options
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
message(STATUS "[aacp_core] libusb: ${LIBUSB_LIBRARIES}")
message(STATUS "[aacp_core] OpenSSL: ${OPENSSL_VERSION}")
