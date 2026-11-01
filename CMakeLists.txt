cmake_minimum_required(VERSION 3.21)

project(MyBikeControl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MYBC_BUILD_TESTS "Build the unit tests" ON)

find_package(Threads REQUIRED)

# Protocol codec, parser, logging and the OpenBikeControl TCP server.
# Everything here is platform-independent except the mDNS advertiser, which
# uses dns_sd on macOS and falls back to a no-op stub elsewhere.
add_library(mybc_core
  src/log.cpp
  src/obc/protocol.cpp
  src/obc/server.cpp
  src/obc/mdns.cpp
)

target_include_directories(mybc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(mybc_core PUBLIC cxx_std_17)
target_link_libraries(mybc_core PUBLIC Threads::Threads)

add_executable(mybc src/main.cpp)
target_link_libraries(mybc PRIVATE mybc_core)

if(MYBC_BUILD_TESTS)
  enable_testing()

  add_executable(mybc_tests tests/test_protocol.cpp)
  target_link_libraries(mybc_tests PRIVATE mybc_core)

  add_test(NAME protocol COMMAND mybc_tests)
endif()
