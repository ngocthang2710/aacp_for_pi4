cmake_minimum_required(VERSION 3.22)
project(aacp_sdk_root VERSION 1.0.0)

# ── Cấu hình chung ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # cho clangd / IDE

# Build type mặc định là RelWithDebInfo nếu không truyền vào
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ── Các option có thể bật/tắt ───────────────────────────────────────────────
option(AACP_BUILD_TESTS   "Build unit tests"         ON)
option(AACP_ENABLE_ASAN   "Enable AddressSanitizer"  OFF)
option(AACP_VERBOSE_LOG   "Enable verbose USB log"   OFF)

if(AACP_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(AACP_VERBOSE_LOG)
    add_compile_definitions(AACP_VERBOSE=1)
endif()

# ── Sub-modules ──────────────────────────────────────────────────────────────
add_subdirectory(sdk-core)

if(AACP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(sdk-core/tests EXCLUDE_FROM_ALL)
endif()

# ── Thông tin build ──────────────────────────────────────────────────────────
message(STATUS "==============================================")
message(STATUS "  AACP SDK ${PROJECT_VERSION}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Tests       : ${AACP_BUILD_TESTS}")
message(STATUS "  ASAN        : ${AACP_ENABLE_ASAN}")
message(STATUS "  Verbose log : ${AACP_VERBOSE_LOG}")
message(STATUS "==============================================")
