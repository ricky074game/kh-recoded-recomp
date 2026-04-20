cmake_minimum_required(VERSION 3.20)
project(ModAPI)

# ============================================================================
# Mod API — Dynamic hooking and asset replacement for runtime injection.
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(mod_api STATIC ${SOURCES})
target_include_directories(mod_api PUBLIC include PRIVATE src)

# Link dl on Linux for dlopen/dlsym
if(UNIX AND NOT APPLE)
    target_link_libraries(mod_api PRIVATE dl)
endif()
