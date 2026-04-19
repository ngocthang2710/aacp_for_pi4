cmake_minimum_required(VERSION 3.22)
project(aacp_tests)
set(CMAKE_CXX_STANDARD 17)

# Simple standalone test — không cần gtest
add_executable(protocol_test
    protocol_test.cpp
)

target_include_directories(protocol_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link chỉ C++ runtime — không cần libusb/openssl cho protocol test
target_link_libraries(protocol_test PRIVATE)

add_test(NAME ProtocolTest COMMAND protocol_test)
