cmake_minimum_required(VERSION 3.15)
project(runtime_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")
endif()

include(FetchContent)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

# Fetch Capstone disassembler
FetchContent_Declare(
  capstone
  GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
  GIT_TAG 5.0.1
)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(capstone)

set(CAPSTONE_LINK_TARGETS capstone)
set(CAPSTONE_INCLUDE_PATHS)

if(NOT TARGET GTest::gtest_main)
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

set(PLATFORM_LIBS)
if(WIN32)
    list(APPEND PLATFORM_LIBS ws2_32)
endif()

file(GLOB_RECURSE ALL_SOURCES
    "src/core/*.cpp"
    "src/vfs/*.cpp"
    "src/hw/*.cpp"
    "src/backends/*.cpp")

list(REMOVE_ITEM ALL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core/master_registration_fallback.cpp")

file(GLOB_RECURSE GENERATED_SOURCES "src/generated/*.cpp")
if(GENERATED_SOURCES)
    list(APPEND ALL_SOURCES ${GENERATED_SOURCES})
else()
    list(APPEND ALL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/core/master_registration_fallback.cpp")
endif()

add_library(runtime_core STATIC ${ALL_SOURCES})
target_include_directories(runtime_core
    PRIVATE
        src
        src/core
        src/hw
        src/vfs
        src/backends
        src/generated
        ../lifter/src
        ../tools/miniaudio
        ${CAPSTONE_INCLUDE_PATHS})
target_link_libraries(runtime_core
    PRIVATE
        Qt6::Core
        Qt6::Gui
        ${CAPSTONE_LINK_TARGETS}
        Threads::Threads
        Vulkan::Vulkan
        ${PLATFORM_LIBS})
if(WIN32)
    target_compile_definitions(runtime_core PRIVATE MA_USE_WASAPI=1)
endif()

add_executable(runtime_engine src/core/main.cpp)
set_target_properties(runtime_engine PROPERTIES OUTPUT_NAME recoded)
target_include_directories(runtime_engine
    PRIVATE
        src
        src/core
        src/hw
        src/vfs
        src/backends
        src/generated
        ../lifter/src
        ../tools/miniaudio
        )
target_link_libraries(runtime_engine
    PRIVATE
        runtime_core
        lifter_core
        ${CAPSTONE_LINK_TARGETS}
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Threads::Threads
        Vulkan::Vulkan
        ${PLATFORM_LIBS})
if(WIN32)
    target_compile_definitions(runtime_engine PRIVATE MA_USE_WASAPI=1)
endif()

set(TEST_SOURCES
    tests/test_cpu_context.cpp
    tests/test_memory_map.cpp
    tests/test_vfs.cpp
    tests/test_hw_gxengine.cpp
    tests/test_hw_2d_engine.cpp
    tests/test_hw_input.cpp
    tests/test_hw_audio.cpp
    tests/test_hw_timers.cpp
    tests/test_hw_dma.cpp
    tests/test_hw_math.cpp
    tests/test_hw_spi.cpp
    tests/test_hw_rtc.cpp
    tests/test_hw_bios.cpp
    tests/test_hw_irq.cpp
    tests/test_hw_ipc.cpp
    tests/test_hw_overlay.cpp
    tests/test_threading_engine.cpp
    tests/test_master_phases.cpp
    tests/test_ds_debug.cpp
    tests/test_dynamic_dispatcher.cpp
    tests/test_miniaudio_backend.cpp
    tests/test_hw_wifi.cpp
    tests/test_hw_mic.cpp
    ../mod_api/tests/test_mod_api.cpp)

if(TEST_SOURCES)
    add_executable(runtime_tests ${TEST_SOURCES})
    target_include_directories(runtime_tests
        PRIVATE
            src
            src/hw
            src/core
            src/vfs
            src/backends
            src/generated
            ../lifter/src
            ../tools/miniaudio
            ../mod_api/include
            ${CAPSTONE_INCLUDE_PATHS})
    target_link_libraries(runtime_tests
        PRIVATE
            runtime_core
            lifter_core
            mod_api
            ${CAPSTONE_LINK_TARGETS}
            GTest::gtest_main
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Threads::Threads
            Vulkan::Vulkan
            ${PLATFORM_LIBS})

    if(WIN32)
        target_compile_definitions(runtime_tests PRIVATE MA_USE_WASAPI=1)
    endif()

    include(GoogleTest)
    gtest_discover_tests(runtime_tests)
endif()
