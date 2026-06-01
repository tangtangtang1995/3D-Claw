get_filename_component(module ${CMAKE_CURRENT_SOURCE_DIR} NAME)

if(NOT CLAW3D_ROOT)
        get_filename_component(CLAW3D_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT CLAW3D_THIRD_PARTY_ROOT)
        set(CLAW3D_THIRD_PARTY_ROOT "${CLAW3D_ROOT}/3rd_party")
endif()

set(${module}_HEADERS
        imgui.h
        backends/imgui_impl_glfw.h
        backends/imgui_impl_opengl3.h
        backends/imgui_impl_opengl3_loader.h
        )

set(${module}_SOURCES
        imgui.cpp
        imgui_demo.cpp
        imgui_draw.cpp
        imgui_widgets.cpp
        imgui_tables.cpp
        backends/imgui_impl_glfw.cpp
        backends/imgui_impl_opengl3.cpp
        )

add_3rdparty_module(3rd_${module} "${${module}_SOURCES}" "${${module}_HEADERS}")
set(_claw3d_imgui_include_dirs
        ${CMAKE_CURRENT_LIST_DIR}
        ${CLAW3D_ROOT}/3rd_party/glfw/include
        )
target_include_directories(3rd_${module} PUBLIC
        ${_claw3d_imgui_include_dirs}
        )
target_link_libraries(3rd_${module} PRIVATE 3rd_glfw)
