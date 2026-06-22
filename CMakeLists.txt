cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(LayoutOBSBypass VERSION 1.0.0 LANGUAGES CXX C)

# Main mod sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)

# SpoutWrapper.cpp must NOT use the Geode PCH and must NOT see Geode/GLEW
# headers, because Spout2's SpoutGLextensions.h declares GL entry points that
# conflict with Geode's bundled GLEW macros (glShaderSource redeclaration).
# It is a plain TU that talks to Spout only. The Geode PCH (which pulls in
# GLEW) must be skipped for it.
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/SpoutWrapper.cpp
    PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        COMPILE_OPTIONS -U_GLEW_H)

# ============================================
# Spout2 — fetch sources only (NOT MakeAvailable)
# We build Spout2 into its own STATIC library so that:
#   - Spout files don't see Geode/Cocos/GLEW headers (avoids glShaderSource
#     redeclaration conflicts between SpoutGLextensions.h and cocos GLEW)
#   - We can enable -mssse3 for SpoutCopy.cpp without conflicting with the
#     pre-compiled header (PCH) that Geode generates for the main mod target
# ============================================
include(FetchContent)
FetchContent_Declare(
    spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(spout2)
if(NOT spout2_POPULATED)
    FetchContent_Populate(spout2)
endif()

set(SPOUT_DIR "${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL")

# Separate static library for SpoutGL — no Geode headers, no PCH
add_library(SpoutGL_static STATIC
    ${SPOUT_DIR}/Spout.cpp
    ${SPOUT_DIR}/SpoutCopy.cpp
    ${SPOUT_DIR}/SpoutDirectX.cpp
    ${SPOUT_DIR}/SpoutFrameCount.cpp
    ${SPOUT_DIR}/SpoutGL.cpp
    ${SPOUT_DIR}/SpoutGLextensions.cpp
    ${SPOUT_DIR}/SpoutSender.cpp
    ${SPOUT_DIR}/SpoutSenderNames.cpp
    ${SPOUT_DIR}/SpoutSharedMemory.cpp
    ${SPOUT_DIR}/SpoutUtils.cpp
)
target_include_directories(SpoutGL_static PUBLIC ${SPOUT_DIR})

# SpoutCopy.cpp uses SSSE3 intrinsics (_mm_shuffle_epi8).
# Clang needs -mssse3 to compile that file. Safe: all x86_64 CPUs support SSSE3.
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(SpoutGL_static PRIVATE -mssse3)
endif()

if (WIN32)
    target_link_libraries(SpoutGL_static PUBLIC opengl32 d3d11 dxgi shlwapi version)
    target_compile_definitions(SpoutGL_static PRIVATE
        _CRT_SECURE_NO_WARNINGS
        SPOUT_BUILD_DLL
    )
endif()

# Link Spout into the mod — only the mod's .cpp includes Spout headers.
# IMPORTANT: use PLAIN signature (no PRIVATE/PUBLIC keyword). Geode's
# setup_geode_mod() calls target_link_libraries with plain signature, and
# CMake forbids mixing keyword/plain on the same target.
target_link_libraries(${PROJECT_NAME} SpoutGL_static)

# GitHub Actions config
if (DEFINED ENV{GITHUB_ACTIONS})
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    message(STATUS "GitHub Actions build detected")
endif()

# Geode SDK
if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()
add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
