cmake_minimum_required(VERSION 3.12)

set(CLAW3D_VERSION_MAJOR 0)
set(CLAW3D_VERSION_MINOR 1)
set(CLAW3D_VERSION_PATCH 0)
set(CLAW3D_VERSION_STRING
    ${CLAW3D_VERSION_MAJOR}.${CLAW3D_VERSION_MINOR}.${CLAW3D_VERSION_PATCH})

project(3DClaw VERSION ${CLAW3D_VERSION_STRING})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CLAW3D_ENABLE_CGAL "Enable 3D Claw CGAL visual algorithm modules" ON)
option(CLAW3D_ENABLE_LAYERING_CHECK
    "Run the src layering guard before building the 3D Claw executable" OFF)
option(CLAW3D_BUILD_TESTS "Build 3D Claw test executables" OFF)
set(CLAW3D_EASY3D_RESOURCE_DIR
    ""
    CACHE PATH "Optional Easy3D resource directory containing shaders, fonts, colormaps, and textures")
set(CLAW3D_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(CLAW3D_THIRD_PARTY_ROOT
    "${CLAW3D_ROOT}/3rd_party"
    CACHE PATH "Root third-party dependency directory for 3D Claw")
set(Easy3D_THIRD_PARTY
    "${CLAW3D_THIRD_PARTY_ROOT}"
    CACHE PATH "Root third-party dependency directory for 3D Claw")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(Threads REQUIRED)

find_package(Easy3D QUIET COMPONENTS
    util core fileio kdtree algo renderer gui viewer)
if(NOT Easy3D_FOUND)
    message(FATAL_ERROR
        "Easy3D 2.6.1 was not found. Build or install Easy3D and pass "
        "-DEasy3D_DIR=<directory-containing-Easy3DConfig.cmake> or add that "
        "directory to CMAKE_PREFIX_PATH. Do not point Easy3D_DIR at the raw "
        "source tree unless it contains a generated Easy3DConfig.cmake.")
endif()

include(cmake/Claw3DThirdParty.cmake)
claw3d_collect_easy3d_compat_includes(CLAW3D_EASY3D_COMPAT_INCLUDE_DIRS)
claw3d_add_local_imgui()

# Markdown rendering stack for the AI chat panel: MD4C (parser) plus imgui_md
# (Dear ImGui renderer). imgui_md depends on both MD4C and ImGui.
add_subdirectory(
    "${CLAW3D_THIRD_PARTY_ROOT}/md4c"
    "${CMAKE_BINARY_DIR}/3rd_party/md4c")
add_subdirectory(
    "${CLAW3D_THIRD_PARTY_ROOT}/imgui_md"
    "${CMAKE_BINARY_DIR}/3rd_party/imgui_md")
add_subdirectory(
    "${CLAW3D_THIRD_PARTY_ROOT}/miniz"
    "${CMAKE_BINARY_DIR}/3rd_party/miniz")

add_subdirectory(src/algorithms/easy3d)
add_subdirectory(src/algorithms/geometry)
add_subdirectory(src/algorithms/analysis)
if(CLAW3D_ENABLE_CGAL)
    add_subdirectory(src/algorithms/cgal)
endif()

add_subdirectory(src/io)
add_subdirectory(src/services)
add_subdirectory(src/app)
if(CLAW3D_BUILD_TESTS)
    if(EXISTS "${CLAW3D_ROOT}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING
            "CLAW3D_BUILD_TESTS=ON was requested, but this source tree does "
            "not include tests/. Continuing without 3D Claw tests.")
    endif()
endif()

message(STATUS "****************************************************************************")
message(STATUS "    Package               : 3D Claw")
message(STATUS "    Version               : ${CLAW3D_VERSION_STRING}")
message(STATUS "    Easy3D backend        : external")
message(STATUS "    3D Claw CGAL modules  : ${CLAW3D_ENABLE_CGAL}")
message(STATUS "    Build directory       : ${CMAKE_BINARY_DIR}")
message(STATUS "****************************************************************************")
