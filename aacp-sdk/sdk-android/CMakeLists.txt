cmake_minimum_required(VERSION 3.22)
project(aacp_jni)

set(CMAKE_CXX_STANDARD 17)

# Thêm sdk-core như sub-project để dùng target aacp_core
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk-core
    ${CMAKE_BINARY_DIR}/sdk-core
)

# JNI bridge — shared library được load bởi Android app
add_library(aacp_jni SHARED
    src/main/cpp/jni_bridge.cpp
)

target_link_libraries(aacp_jni
    PRIVATE aacp_core   # link vào C++ core
    PRIVATE android
    PRIVATE log
)

target_include_directories(aacp_jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk-core/include
)
