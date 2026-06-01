include_guard(GLOBAL)

function(claw3d_define_3rdparty_module)
    if(COMMAND add_3rdparty_module)
        return()
    endif()

    function(add_3rdparty_module module sources headers)
        add_library(${module} STATIC ${sources} ${headers})
        set_target_properties(${module} PROPERTIES
            FOLDER "3rd_party"
            POSITION_INDEPENDENT_CODE ON)
        if(WIN32 OR MSVC)
            target_compile_definitions(${module}
                PUBLIC
                    NOMINMAX
                    WIN32_LEAN_AND_MEAN
                    VC_EXTRALEAN
                    _CRT_SECURE_NO_WARNINGS
                    _CRT_SECURE_NO_DEPRECATE)
        endif()
    endfunction()
endfunction()

function(claw3d_add_local_glfw)
    if(TARGET 3rd_glfw)
        return()
    endif()
    if(NOT EXISTS "${CLAW3D_THIRD_PARTY_ROOT}/glfw/CMakeLists.txt")
        message(FATAL_ERROR "Missing GLFW dependency: ${CLAW3D_THIRD_PARTY_ROOT}/glfw")
    endif()
    add_subdirectory(
        "${CLAW3D_THIRD_PARTY_ROOT}/glfw"
        "${CMAKE_BINARY_DIR}/3rd_party/glfw")
endfunction()

function(claw3d_add_local_imgui)
    claw3d_define_3rdparty_module()
    if(TARGET 3rd_imgui)
        return()
    endif()
    if(NOT TARGET 3rd_glfw)
        claw3d_add_local_glfw()
    endif()
    if(NOT EXISTS "${CLAW3D_THIRD_PARTY_ROOT}/imgui/CMakeLists.txt")
        message(FATAL_ERROR "Missing ImGui dependency: ${CLAW3D_THIRD_PARTY_ROOT}/imgui")
    endif()
    add_subdirectory(
        "${CLAW3D_THIRD_PARTY_ROOT}/imgui"
        "${CMAKE_BINARY_DIR}/3rd_party/imgui")
endfunction()

function(claw3d_collect_easy3d_compat_includes out_var)
    set(_compat_includes "")

    # Easy3D 2.6.1 source-tree package exports may omit public include roots
    # for headers included by Easy3D's own public headers.
    foreach(_include_dir IN LISTS Easy3D_INCLUDE_DIRS)
        if(EXISTS "${_include_dir}/3rd_party/easyloggingpp/easylogging++.h")
            list(APPEND _compat_includes "${_include_dir}")
        endif()

        if(EXISTS "${_include_dir}/3rd_party/easy3d_deps/3rd_party/easyloggingpp/easylogging++.h")
            list(APPEND _compat_includes "${_include_dir}/3rd_party/easy3d_deps")
        endif()

        if(EXISTS "${_include_dir}/3rd_party/easy3d_deps/3rd_party/glew/include/GL/glew.h")
            list(APPEND _compat_includes "${_include_dir}/3rd_party/easy3d_deps/3rd_party/glew/include")
        endif()

        get_filename_component(_include_parent "${_include_dir}" DIRECTORY)
        if(EXISTS "${_include_parent}/easy3d_deps/3rd_party/easyloggingpp/easylogging++.h")
            list(APPEND _compat_includes "${_include_parent}/easy3d_deps")
        endif()

        if(EXISTS "${_include_parent}/easy3d_deps/3rd_party/glew/include/GL/glew.h")
            list(APPEND _compat_includes "${_include_parent}/easy3d_deps/3rd_party/glew/include")
        endif()
    endforeach()

    if(_compat_includes)
        list(REMOVE_DUPLICATES _compat_includes)
        message(STATUS "Easy3D compatibility include dirs: ${_compat_includes}")
    endif()

    set(${out_var} "${_compat_includes}" PARENT_SCOPE)
endfunction()
