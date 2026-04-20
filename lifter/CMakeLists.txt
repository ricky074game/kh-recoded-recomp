cmake_minimum_required(VERSION 3.20)
project(Lifter)

# ============================================================================
# Binary Lifter — Offline ARM→C++ translator using Capstone.
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch Capstone disassembler
FetchContent_Declare(
  capstone
  GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
  GIT_TAG 5.0.1
)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(capstone)

# Fetch Google Test
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Core library (everything except main.cpp) ---
file(GLOB_RECURSE SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(lifter_core STATIC ${SOURCES})
# The lifter needs access to runtime headers (cpu_context.h) for flag calculations
target_include_directories(lifter_core PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/core)
target_link_libraries(lifter_core PUBLIC capstone)

# --- Main executable ---
add_executable(lifter_engine src/main.cpp)
target_include_directories(lifter_engine PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/core)
target_link_libraries(lifter_engine PRIVATE lifter_core)

# --- Tests ---
enable_testing()

file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
if(TEST_SOURCES)
    add_executable(lifter_tests ${TEST_SOURCES})
    target_include_directories(lifter_tests PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/core)
    target_link_libraries(lifter_tests PRIVATE lifter_core GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(lifter_tests)
endif()
