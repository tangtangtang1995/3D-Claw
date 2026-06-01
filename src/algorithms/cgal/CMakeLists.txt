set(CLAW3D_CGAL_ALGO_DEFAULT_CGAL_INCLUDE_DIR "")
foreach(_candidate
        "${CMAKE_SOURCE_DIR}/3rd_party/CGAL-6.1.1/include"
        "${CMAKE_SOURCE_DIR}/_deps/CGAL-6.1.1/include")
    if(EXISTS "${_candidate}/CGAL/version.h")
        set(CLAW3D_CGAL_ALGO_DEFAULT_CGAL_INCLUDE_DIR "${_candidate}")
        break()
    endif()
endforeach()

set(CLAW3D_CGAL_ALGO_DEFAULT_OVERLAY_INCLUDE_DIR "")
if(EXISTS "${CMAKE_SOURCE_DIR}/3rd_party_overrides/cgal-6.1.1/include/CGAL")
    set(CLAW3D_CGAL_ALGO_DEFAULT_OVERLAY_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/3rd_party_overrides/cgal-6.1.1/include")
endif()

set(CLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR
    "${CLAW3D_CGAL_ALGO_DEFAULT_CGAL_INCLUDE_DIR}"
    CACHE PATH "CGAL include directory for 3D Claw CGAL visual algorithm runners")
set(CLAW3D_CGAL_ALGO_CGAL_OVERLAY_INCLUDE_DIR
    "${CLAW3D_CGAL_ALGO_DEFAULT_OVERLAY_INCLUDE_DIR}"
    CACHE PATH "Optional CGAL header override include directory for 3D Claw CGAL visual algorithm runners")
if(NOT CLAW3D_CGAL_ALGO_CGAL_OVERLAY_INCLUDE_DIR
        AND CLAW3D_CGAL_ALGO_DEFAULT_OVERLAY_INCLUDE_DIR)
    set(CLAW3D_CGAL_ALGO_CGAL_OVERLAY_INCLUDE_DIR
        "${CLAW3D_CGAL_ALGO_DEFAULT_OVERLAY_INCLUDE_DIR}"
        CACHE PATH "Optional CGAL header override include directory for 3D Claw CGAL visual algorithm runners" FORCE)
endif()
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR}/CGAL/version.h")
    message(FATAL_ERROR
        "CGAL 6.1.1 headers were not found. Extract CGAL to "
        "3rd_party/CGAL-6.1.1 or set CLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR "
        "to the directory that contains CGAL/version.h.")
endif()

set(CLAW3D_CGAL_ALGO_DEFAULT_BOOST_INCLUDE_DIR "")
foreach(_candidate
        "${CMAKE_SOURCE_DIR}/3rd_party/boost"
        "${CMAKE_SOURCE_DIR}/_deps/boost")
    if(EXISTS "${_candidate}/boost/version.hpp")
        set(CLAW3D_CGAL_ALGO_DEFAULT_BOOST_INCLUDE_DIR "${_candidate}")
        break()
    endif()
endforeach()
set(CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR
    "${CLAW3D_CGAL_ALGO_DEFAULT_BOOST_INCLUDE_DIR}"
    CACHE PATH "Boost include directory for 3D Claw CGAL visual algorithm runners")
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR}/boost/version.hpp")
    find_package(Boost QUIET)
    if(Boost_FOUND)
        set(CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR "${Boost_INCLUDE_DIRS}"
            CACHE PATH "Boost include directory for 3D Claw CGAL visual algorithm runners" FORCE)
    endif()
endif()
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR}/boost/version.hpp")
    message(FATAL_ERROR
        "Boost headers were not found. Extract Boost to 3rd_party/boost, "
        "or set CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR to the directory that "
        "contains boost/version.hpp.")
endif()

set(CLAW3D_CGAL_ALGO_DEFAULT_GMP_ROOT "")
foreach(_candidate
        "${CMAKE_SOURCE_DIR}/3rd_party/gmp"
        "${CMAKE_SOURCE_DIR}/_deps/gmp")
    if(EXISTS "${_candidate}/include/gmp.h"
            OR EXISTS "${_candidate}/include/mpfr.h"
            OR EXISTS "${_candidate}/lib")
        set(CLAW3D_CGAL_ALGO_DEFAULT_GMP_ROOT "${_candidate}")
        break()
    endif()
endforeach()
set(CLAW3D_CGAL_ALGO_GMP_ROOT
    "${CLAW3D_CGAL_ALGO_DEFAULT_GMP_ROOT}"
    CACHE PATH "Optional GMP/MPFR root directory for 3D Claw CGAL visual algorithm runners")

set(_claw3d_gmp_include_hints)
set(_claw3d_gmp_library_hints)
if(CLAW3D_CGAL_ALGO_GMP_ROOT)
    list(APPEND _claw3d_gmp_include_hints
        "${CLAW3D_CGAL_ALGO_GMP_ROOT}/include")
    list(APPEND _claw3d_gmp_library_hints
        "${CLAW3D_CGAL_ALGO_GMP_ROOT}/lib"
        "${CLAW3D_CGAL_ALGO_GMP_ROOT}/lib64")
endif()
find_path(CLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR
    NAMES gmp.h
    HINTS ${_claw3d_gmp_include_hints}
    PATH_SUFFIXES
        x86_64-linux-gnu
        aarch64-linux-gnu
        arm-linux-gnueabihf
        i386-linux-gnu
    DOC "Directory containing gmp.h for 3D Claw CGAL visual algorithm runners")
find_path(CLAW3D_CGAL_ALGO_MPFR_INCLUDE_DIR
    NAMES mpfr.h
    HINTS ${_claw3d_gmp_include_hints}
    PATH_SUFFIXES
        x86_64-linux-gnu
        aarch64-linux-gnu
        arm-linux-gnueabihf
        i386-linux-gnu
    DOC "Directory containing mpfr.h for 3D Claw CGAL visual algorithm runners")
set(CLAW3D_CGAL_ALGO_GMP_LIB_DIR
    ""
    CACHE PATH "Optional GMP/MPFR library search directory for 3D Claw CGAL visual algorithm runners")
if(CLAW3D_CGAL_ALGO_GMP_LIB_DIR)
    list(PREPEND _claw3d_gmp_library_hints
        "${CLAW3D_CGAL_ALGO_GMP_LIB_DIR}")
endif()

if(NOT EXISTS "${CLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR}/gmp.h")
    message(FATAL_ERROR
        "GMP header gmp.h was not found. Install libgmp-dev or set "
        "CLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR to the directory that contains gmp.h.")
endif()
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_MPFR_INCLUDE_DIR}/mpfr.h")
    message(FATAL_ERROR
        "MPFR header mpfr.h was not found. Install libmpfr-dev or set "
        "CLAW3D_CGAL_ALGO_MPFR_INCLUDE_DIR to the directory that contains mpfr.h.")
endif()

find_library(CLAW3D_CGAL_ALGO_GMP_LIBRARY
    NAMES gmp libgmp
    PATHS ${_claw3d_gmp_library_hints}
    NO_DEFAULT_PATH)
find_library(CLAW3D_CGAL_ALGO_GMPXX_LIBRARY
    NAMES gmpxx libgmpxx
    PATHS ${_claw3d_gmp_library_hints}
    NO_DEFAULT_PATH)
find_library(CLAW3D_CGAL_ALGO_MPFR_LIBRARY
    NAMES mpfr libmpfr
    PATHS ${_claw3d_gmp_library_hints}
    NO_DEFAULT_PATH)
if(NOT CLAW3D_CGAL_ALGO_GMP_LIBRARY)
    find_library(CLAW3D_CGAL_ALGO_GMP_LIBRARY NAMES gmp libgmp)
endif()
if(NOT CLAW3D_CGAL_ALGO_GMPXX_LIBRARY)
    find_library(CLAW3D_CGAL_ALGO_GMPXX_LIBRARY NAMES gmpxx libgmpxx)
endif()
if(NOT CLAW3D_CGAL_ALGO_MPFR_LIBRARY)
    find_library(CLAW3D_CGAL_ALGO_MPFR_LIBRARY NAMES mpfr libmpfr)
endif()
foreach(_required_lib
        CLAW3D_CGAL_ALGO_GMP_LIBRARY
        CLAW3D_CGAL_ALGO_GMPXX_LIBRARY
        CLAW3D_CGAL_ALGO_MPFR_LIBRARY)
    if(NOT ${_required_lib} OR NOT EXISTS "${${_required_lib}}")
        message(FATAL_ERROR
            "Required GMP/MPFR library ${_required_lib} was not found. "
            "Set ${_required_lib} explicitly, or set CLAW3D_CGAL_ALGO_GMP_LIB_DIR "
            "to a directory containing gmp, gmpxx, and mpfr libraries.")
    endif()
endforeach()

set(CLAW3D_CGAL_ALGO_DEFAULT_EIGEN_INCLUDE_DIR
    "${CMAKE_SOURCE_DIR}/3rd_party/eigen")
set(CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR
    "${CLAW3D_CGAL_ALGO_DEFAULT_EIGEN_INCLUDE_DIR}"
    CACHE PATH "Eigen include directory for 3D Claw CGAL visual algorithm runners")
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR}/Eigen/Core"
        AND EXISTS "${CLAW3D_CGAL_ALGO_DEFAULT_EIGEN_INCLUDE_DIR}/Eigen/Core")
    set(CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR
        "${CLAW3D_CGAL_ALGO_DEFAULT_EIGEN_INCLUDE_DIR}"
        CACHE PATH "Eigen include directory for 3D Claw CGAL visual algorithm runners" FORCE)
endif()
if(NOT EXISTS "${CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR}/Eigen/Core")
    find_package(Eigen3 QUIET)
endif()

set(CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS)
if(CLAW3D_CGAL_ALGO_CGAL_OVERLAY_INCLUDE_DIR)
    list(APPEND CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS
        "${CLAW3D_CGAL_ALGO_CGAL_OVERLAY_INCLUDE_DIR}")
endif()
list(APPEND CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS
    "${CLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR}"
    "${CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR}")
if(EXISTS "${CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR}/Eigen/Core")
    list(APPEND CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS
        "${CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR}")
endif()
list(APPEND CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS
    "${CLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR}"
    "${CLAW3D_CGAL_ALGO_MPFR_INCLUDE_DIR}")

set(CLAW3D_CGAL_ALGO_NUMERIC_LIBS
    "${CLAW3D_CGAL_ALGO_GMP_LIBRARY}"
    "${CLAW3D_CGAL_ALGO_GMPXX_LIBRARY}"
    "${CLAW3D_CGAL_ALGO_MPFR_LIBRARY}")

include(CMakeSources.cmake)

add_library(claw3d_cgal_algorithms SHARED
    ${CLAW3D_CGAL_ALGORITHM_HEADERS}
    ${CLAW3D_CGAL_ALGORITHM_SOURCES}
)

target_include_directories(claw3d_cgal_algorithms
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        ${CLAW3D_CGAL_ALGO_COMMON_INCLUDE_DIRS})

target_link_libraries(claw3d_cgal_algorithms PRIVATE
    ${CLAW3D_CGAL_ALGO_NUMERIC_LIBS}
    Threads::Threads)

target_compile_definitions(claw3d_cgal_algorithms PRIVATE
    CLAW3D_CGAL_ALGO_EXPORTS
    NOMINMAX)

if(TARGET Eigen3::Eigen)
    target_link_libraries(claw3d_cgal_algorithms PRIVATE Eigen3::Eigen)
elseif(NOT EXISTS "${CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR}/Eigen/Core")
    message(FATAL_ERROR
        "Eigen is required by claw3d_cgal_algorithms. Set CLAW3D_CGAL_ALGO_EIGEN_INCLUDE_DIR.")
endif()

target_compile_definitions(claw3d_cgal_algorithms PRIVATE CGAL_EIGEN3_ENABLED)

if(MSVC)
    target_compile_options(claw3d_cgal_algorithms PRIVATE /bigobj)
endif()

set_target_properties(claw3d_cgal_algorithms PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "algorithms")
