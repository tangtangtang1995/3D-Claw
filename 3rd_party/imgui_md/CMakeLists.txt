# imgui_md: Dear ImGui renderer for MD4C output (MIT).
# Bridges MD4C parse callbacks to ImGui draw calls for the AI chat Markdown view.

claw3d_define_3rdparty_module()

add_3rdparty_module(3rd_imgui_md "imgui_md.cpp" "imgui_md.h")

target_include_directories(3rd_imgui_md PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(3rd_imgui_md PUBLIC 3rd_md4c 3rd_imgui)
