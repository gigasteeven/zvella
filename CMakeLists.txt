cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(LayoutOBSBypass VERSION 1.0.0 LANGUAGES CXX C)

# Main mod sources
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE src)

# ============================================
# Spout2 — fetch from GitHub and compile in
# ============================================
include(FetchContent)
FetchContent_Declare(
    spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spout2)

# Collect SpoutGL source files
set(SPOUT_DIR "${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL")
set(SPOUT_SOURCES
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
target_sources(${PROJECT_NAME} PRIVATE ${SPOUT_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${SPOUT_DIR})

# Windows libs needed by Spout
if (WIN32)
    target_link_libraries(${PROJECT_NAME} opengl32 d3d11 dxgi shlwapi version)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        SPOUT_BUILD_DLL  # Needed so SpoutGL exports work
    )
endif()

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
