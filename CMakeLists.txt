cmake_minimum_required(VERSION 3.20)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(KHRecodedRecomp LANGUAGES CXX C)

# ============================================================================
# Kingdom Hearts Re:coded Static Recompilation
# Root build configuration — brings together all components.
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
	add_compile_options(/W3)
else()
	add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Sub-projects
add_subdirectory(lifter)
add_subdirectory(mod_api)
add_subdirectory(runtime)
