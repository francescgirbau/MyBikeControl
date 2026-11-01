cmake_minimum_required(VERSION 3.21)

project(MyBikeControl VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MYBC_WITH_BLE "Enable BLE support via SimpleBLE" ON)
option(MYBC_BUILD_TESTS "Build tests" ON)

find_package(Threads REQUIRED)

add_library(mybc_core
  src/log.cpp
  src/obc/protocol.cpp
  src/obc/server.cpp
  src/obc/mdns.cpp
)
target_include_directories(mybc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(mybc_core PUBLIC cxx_std_17)
target_link_libraries(mybc_core PUBLIC Threads::Threads)

add_executable(mybc
  src/main.cpp
  src/ble/scanner.cpp
)
target_link_libraries(mybc PRIVATE mybc_core)

if(MYBC_WITH_BLE)
  find_package(simpleble CONFIG QUIET)
  if(NOT simpleble_FOUND)
    include(FetchContent)
    FetchContent_Declare(simpleble
      URL https://github.com/OpenBluetoothToolbox/SimpleBLE/archive/refs/tags/v1.3.0.tar.gz
      SOURCE_SUBDIR simpleble
    )
    set(SIMPLEBLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SIMPLEBLE_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(simpleble)
  endif()

  target_link_libraries(mybc PRIVATE simpleble::simpleble)
  target_compile_definitions(mybc PRIVATE MYBC_WITH_BLE=1)
else()
  target_compile_definitions(mybc PRIVATE MYBC_WITH_BLE=0)
endif()

if(MYBC_BUILD_TESTS)
  enable_testing()
  add_executable(mybc_tests tests/test_protocol.cpp)
  target_link_libraries(mybc_tests PRIVATE mybc_core)
  add_test(NAME protocol COMMAND mybc_tests)
endif()
