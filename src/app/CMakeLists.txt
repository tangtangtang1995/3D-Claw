set(PROJECT_NAME 3DClaw)
project(${PROJECT_NAME})

include(CMakeSources.cmake)

if(NOT CLAW3D_ENABLE_CGAL)
    list(REMOVE_ITEM CLAW3D_APP_SOURCES
        dialogs/alpha_wrap_dialog.cpp
        dialogs/ransac_dialog.cpp
        dialogs/region_growing_dialog.cpp
        dialogs/cgal_simplification_dialog.cpp
        dialogs/acvd_dialog.cpp
        dialogs/vsa_dialog.cpp
        dialogs/planar_patch_remeshing_dialog.cpp
        dialogs/cgal_smoothing_dialog.cpp
        dialogs/mcf_skeletonization_dialog.cpp
        dialogs/arap_deformation_dialog.cpp
        dialogs/parameterization_dialog.cpp)
    list(APPEND CLAW3D_APP_SOURCES
        dialogs/cgal_unavailable_dialog.cpp)
endif()
add_executable(${PROJECT_NAME}
    ${CLAW3D_APP_HEADERS}
    ${CLAW3D_APP_SOURCES})

if(CLAW3D_ENABLE_LAYERING_CHECK)
    set(CLAW3D_LAYERING_CHECK_ATTACHED OFF)
    set(CLAW3D_LAYERING_CHECK_PY
        "${CLAW3D_ROOT}/tools/check_layering.py")
    set(CLAW3D_LAYERING_CHECK_PS1
        "${CLAW3D_ROOT}/tools/check_layering.ps1")

    if(EXISTS "${CLAW3D_LAYERING_CHECK_PY}")
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_EXECUTABLE)
            add_custom_target(claw3d_layering_check
                COMMAND "${Python3_EXECUTABLE}"
                        "${CLAW3D_LAYERING_CHECK_PY}"
                        --repo-root "${CLAW3D_ROOT}"
                WORKING_DIRECTORY "${CLAW3D_ROOT}"
                COMMENT "Checking src layering boundaries")
            add_dependencies(${PROJECT_NAME} claw3d_layering_check)
            set(CLAW3D_LAYERING_CHECK_ATTACHED ON)
        endif()
    endif()

    if(NOT CLAW3D_LAYERING_CHECK_ATTACHED
       AND EXISTS "${CLAW3D_LAYERING_CHECK_PS1}")
        find_program(CLAW3D_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
        if(CLAW3D_POWERSHELL_EXECUTABLE)
            add_custom_target(claw3d_layering_check
                COMMAND "${CLAW3D_POWERSHELL_EXECUTABLE}"
                        -NoProfile
                        -ExecutionPolicy Bypass
                        -File "${CLAW3D_LAYERING_CHECK_PS1}"
                WORKING_DIRECTORY "${CLAW3D_ROOT}"
                COMMENT "Checking src layering boundaries")
            add_dependencies(${PROJECT_NAME} claw3d_layering_check)
            set(CLAW3D_LAYERING_CHECK_ATTACHED ON)
        endif()
    endif()

    if(NOT CLAW3D_LAYERING_CHECK_ATTACHED)
        message(WARNING
            "CLAW3D_ENABLE_LAYERING_CHECK=ON was requested, but no usable "
            "tools/check_layering script was found. Continuing without the "
            "src layering check.")
    endif()
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    FOLDER "applications"
    NO_SYSTEM_FROM_IMPORTED ON)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CLAW3D_ROOT}
    ${CMAKE_SOURCE_DIR}/src
    ${CLAW3D_THIRD_PARTY_ROOT}/httplib
    ${CLAW3D_THIRD_PARTY_ROOT}/tinygltf
    ${CMAKE_CURRENT_LIST_DIR}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${Easy3D_INCLUDE_DIRS}
    ${CLAW3D_EASY3D_COMPAT_INCLUDE_DIRS})

if(WIN32 OR MSVC)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_DEPRECATE)
endif()

target_compile_definitions(${PROJECT_NAME} PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)

if(CLAW3D_ENABLE_CGAL)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CLAW3D_HAS_CGAL)
endif()

find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    claw3d_services
    claw3d_io
    claw3d_easy3d_algorithms
    claw3d_geometry_algorithms
    claw3d_analysis_algorithms
    3rd_imgui
    3rd_imgui_md
    3rd_glfw
    easy3d::core
    easy3d::util
    easy3d::renderer
    easy3d::fileio
    easy3d::algo
    easy3d::gui
    OpenGL::GL
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE Shell32)
endif()

if(WIN32)
    foreach(_easy3d_runtime IN ITEMS
            easy3d::util
            easy3d::core
            easy3d::fileio
            easy3d::kdtree
            easy3d::algo
            easy3d::renderer
            easy3d::gui
            easy3d::viewer)
        if(TARGET ${_easy3d_runtime})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:${_easy3d_runtime}>"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        endif()
    endforeach()
endif()

if(WIN32)
    find_file(CLAW3D_OPENSSL_SSL_DLL
        NAMES libssl-3-x64.dll libssl-3.dll libssl-1_1-x64.dll libssl-1_1.dll ssleay32.dll
        HINTS "${OPENSSL_ROOT_DIR}/bin" "${OPENSSL_ROOT_DIR}"
        NO_DEFAULT_PATH)
    find_file(CLAW3D_OPENSSL_CRYPTO_DLL
        NAMES libcrypto-3-x64.dll libcrypto-3.dll libcrypto-1_1-x64.dll libcrypto-1_1.dll libeay32.dll
        HINTS "${OPENSSL_ROOT_DIR}/bin" "${OPENSSL_ROOT_DIR}"
        NO_DEFAULT_PATH)
    foreach(_openssl_dll IN ITEMS "${CLAW3D_OPENSSL_SSL_DLL}" "${CLAW3D_OPENSSL_CRYPTO_DLL}")
        if(_openssl_dll)
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_openssl_dll}"
                    "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
        endif()
    endforeach()
endif()
