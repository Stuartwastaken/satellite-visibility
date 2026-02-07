cmake_minimum_required(VERSION 3.16)
project(satellite_visibility CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -fsanitize=address,undefined -fno-omit-frame-pointer")

add_executable(satellite_visibility src/main.cpp)
add_executable(packet_router src/packet_router.cpp)
add_executable(handoff_scheduler src/handoff_scheduler.cpp)

# Tests
enable_testing()
add_executable(tests test/test_visibility.cpp)
add_test(NAME visibility_tests COMMAND tests)
