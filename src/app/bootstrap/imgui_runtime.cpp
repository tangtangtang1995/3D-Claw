// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "bootstrap/imgui_runtime.h"

#include "product_identity.h"
#include "services/platform/platform_paths.h"
#include "ui/ui_scale.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <string>

namespace {

ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

#ifdef _WIN32
HANDLE g_imgui_heap = nullptr;

void* imgui_heap_alloc(size_t size, void*) {
    return g_imgui_heap ? HeapAlloc(g_imgui_heap, 0, size) : std::malloc(size);
}

void imgui_heap_free(void* ptr, void*) {
    if (!ptr)
        return;
    if (g_imgui_heap)
        HeapFree(g_imgui_heap, 0, ptr);
    else
        std::free(ptr);
}

void install_imgui_allocator() {
    g_imgui_heap = HeapCreate(HEAP_NO_SERIALIZE, 1 * 1024 * 1024, 0);
    if (g_imgui_heap)
        ImGui::SetAllocatorFunctions(imgui_heap_alloc, imgui_heap_free);
}

void shutdown_imgui_allocator() {
    if (g_imgui_heap) {
        HeapDestroy(g_imgui_heap);
        g_imgui_heap = nullptr;
    }
}
#else
void install_imgui_allocator() {}
void shutdown_imgui_allocator() {}
#endif

void setup_cjk_font(ImGuiIO& io) {
    static const ImWchar cjk_ranges[] = {
        0x0020, 0x00FF, 0x2000, 0x206F, 0x3000, 0x30FF,
        0x31F0, 0x31FF, 0xFF00, 0xFFEF, 0x4e00, 0x9FAF, 0,
    };
    const std::string font_path = claw3d::platform::locate_cjk_font();
    if (!font_path.empty() &&
        io.Fonts->AddFontFromFileTTF(font_path.c_str(), 15.0f,
                                     nullptr, cjk_ranges))
        return;
    io.Fonts->AddFontDefault();
}

void setup_imgui_style() {
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::StyleColorsLight(&style);

    style.WindowPadding     = ImVec2(8.0f, 7.0f);
    style.FramePadding      = ImVec2(7.0f, 4.0f);
    style.CellPadding       = ImVec2(6.0f, 3.0f);
    style.ItemSpacing       = ImVec2(7.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(5.0f, 4.0f);
    style.ScrollbarSize     = 13.0f;
    style.GrabMinSize       = 9.0f;
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 1.0f;
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.PopupRounding     = 3.0f;
    style.FrameRounding     = 2.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;
    style.TreeLinesSize = 2.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                  = rgba(31, 38, 48);
    colors[ImGuiCol_TextDisabled]          = rgba(102, 113, 129);
    colors[ImGuiCol_WindowBg]              = rgba(239, 242, 246);
    colors[ImGuiCol_ChildBg]               = rgba(246, 248, 251);
    colors[ImGuiCol_PopupBg]               = rgba(255, 255, 255);
    colors[ImGuiCol_Border]                = rgba(158, 170, 186);
    colors[ImGuiCol_BorderShadow]          = rgba(0, 0, 0, 0.0f);

    colors[ImGuiCol_FrameBg]               = rgba(255, 255, 255);
    colors[ImGuiCol_FrameBgHovered]        = rgba(229, 239, 251);
    colors[ImGuiCol_FrameBgActive]         = rgba(212, 229, 249);
    colors[ImGuiCol_TitleBg]               = rgba(220, 226, 235);
    colors[ImGuiCol_TitleBgActive]         = rgba(207, 218, 232);
    colors[ImGuiCol_TitleBgCollapsed]      = rgba(229, 234, 241);
    colors[ImGuiCol_MenuBarBg]             = rgba(230, 235, 242);

    colors[ImGuiCol_ScrollbarBg]           = rgba(232, 236, 242);
    colors[ImGuiCol_ScrollbarGrab]         = rgba(170, 181, 195);
    colors[ImGuiCol_ScrollbarGrabHovered]  = rgba(145, 160, 178);
    colors[ImGuiCol_ScrollbarGrabActive]   = rgba(117, 135, 157);
    colors[ImGuiCol_CheckMark]             = rgba(36, 105, 190);
    colors[ImGuiCol_SliderGrab]            = rgba(54, 128, 214);
    colors[ImGuiCol_SliderGrabActive]      = rgba(32, 103, 190);

    colors[ImGuiCol_Button]                = rgba(224, 231, 240);
    colors[ImGuiCol_ButtonHovered]         = rgba(207, 224, 246);
    colors[ImGuiCol_ButtonActive]          = rgba(184, 208, 239);
    colors[ImGuiCol_Header]                = rgba(219, 230, 245);
    colors[ImGuiCol_HeaderHovered]         = rgba(202, 221, 246);
    colors[ImGuiCol_HeaderActive]          = rgba(181, 207, 239);

    colors[ImGuiCol_Separator]             = rgba(154, 167, 184);
    colors[ImGuiCol_SeparatorHovered]      = rgba(88, 142, 209);
    colors[ImGuiCol_SeparatorActive]       = rgba(50, 115, 196);
    colors[ImGuiCol_ResizeGrip]            = rgba(87, 142, 209, 0.25f);
    colors[ImGuiCol_ResizeGripHovered]     = rgba(87, 142, 209, 0.55f);
    colors[ImGuiCol_ResizeGripActive]      = rgba(50, 115, 196, 0.85f);
    colors[ImGuiCol_InputTextCursor]       = rgba(36, 92, 170);

    colors[ImGuiCol_Tab]                   = rgba(213, 223, 236);
    colors[ImGuiCol_TabHovered]            = rgba(188, 213, 244);
    colors[ImGuiCol_TabSelected]           = rgba(190, 215, 246);
    colors[ImGuiCol_TabSelectedOverline]   = rgba(30, 91, 170);
    colors[ImGuiCol_TabDimmed]             = rgba(219, 226, 236);
    colors[ImGuiCol_TabDimmedSelected]     = rgba(195, 211, 233);
    colors[ImGuiCol_TabDimmedSelectedOverline] = rgba(80, 129, 190);
    colors[ImGuiCol_DockingPreview]        = rgba(52, 126, 214, 0.35f);
    colors[ImGuiCol_DockingEmptyBg]        = rgba(210, 216, 224);

    colors[ImGuiCol_PlotLines]             = rgba(56, 116, 190);
    colors[ImGuiCol_PlotLinesHovered]      = rgba(188, 122, 36);
    colors[ImGuiCol_PlotHistogram]         = rgba(60, 150, 101);
    colors[ImGuiCol_PlotHistogramHovered]  = rgba(188, 122, 36);
    colors[ImGuiCol_TableHeaderBg]         = rgba(220, 228, 238);
    colors[ImGuiCol_TableBorderStrong]     = rgba(148, 163, 181);
    colors[ImGuiCol_TableBorderLight]      = rgba(188, 199, 213);
    colors[ImGuiCol_TableRowBg]            = rgba(0, 0, 0, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]         = rgba(58, 107, 170, 0.055f);
    colors[ImGuiCol_TextLink]              = rgba(36, 105, 190);
    colors[ImGuiCol_TextSelectedBg]        = rgba(69, 132, 210, 0.35f);
    colors[ImGuiCol_TreeLines]             = rgba(140, 151, 165);
    colors[ImGuiCol_DragDropTarget]        = rgba(219, 142, 39, 0.90f);
    colors[ImGuiCol_DragDropTargetBg]      = rgba(219, 142, 39, 0.15f);
    colors[ImGuiCol_UnsavedMarker]         = rgba(208, 132, 33);
    colors[ImGuiCol_NavCursor]             = rgba(36, 105, 190);
    colors[ImGuiCol_NavWindowingHighlight] = rgba(31, 38, 48, 0.35f);
    colors[ImGuiCol_NavWindowingDimBg]     = rgba(74, 88, 108, 0.28f);
    colors[ImGuiCol_ModalWindowDimBg]      = rgba(74, 88, 108, 0.40f);
}

} // namespace

namespace claw3d {

bool initialize_imgui_runtime(GLFWwindow* window) {
    IMGUI_CHECKVERSION();
    install_imgui_allocator();

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    static std::string imgui_ini_path =
        platform::app_config_path(product_identity::kImGuiIniFileName);
    io.IniFilename = imgui_ini_path.empty() ?
        product_identity::kImGuiIniFileName : imgui_ini_path.c_str();

    setup_cjk_font(io);
    setup_imgui_style();
    UIScale::instance().capture_baseline();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");
    return true;
}

void shutdown_imgui_runtime() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    shutdown_imgui_allocator();
}

void begin_imgui_frame() {
    UIScale::instance().apply_for_next_frame();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(
        ImGui::GetID("MainDockSpace"), ImGui::GetMainViewport());
}

void end_imgui_frame(GLFWwindow* window) {
    ImGui::Render();

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
}

} // namespace claw3d
