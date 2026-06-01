# MD4C: Markdown parser for C (MIT).
# Used by imgui_md to render AI chat replies as GitHub-flavored Markdown.

claw3d_define_3rdparty_module()

add_3rdparty_module(3rd_md4c "md4c.c" "md4c.h")

target_include_directories(3rd_md4c PUBLIC ${CMAKE_CURRENT_LIST_DIR})
