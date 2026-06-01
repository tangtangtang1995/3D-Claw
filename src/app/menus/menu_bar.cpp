// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Top menu dispatcher for MainWindow.
//
// The individual menu sections live in focused *_menu.cpp files.

#include "window/main_window.h"

#include "imgui.h"


// =============================================================================
// Menu Bar
// =============================================================================
void MainWindow::render_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    render_menu_file();

    // Edit owns geometry edits plus the Align and Crop / Clip submenus.
    render_menu_edit();

    render_menu_select();

    // View owns display toggles and the Camera submenu; Window owns dock panels.
    render_menu_view();
    render_menu_window();

    render_menu_point_cloud();

    render_menu_surface_mesh();

    // Analyze merges the former Property, Measurement, and Analysis menus.
    render_menu_analyze();

    render_menu_ai();
    render_menu_help();

    ImGui::EndMainMenuBar();
}


// =============================================================================
// Menu Callbacks
// =============================================================================

// File menu actions (menu_file_open / load_files_async / menu_file_save)
// have moved to file_actions.cpp.


// All algorithm live-preview overlays moved to
// algorithm_overlays.cpp.
void MainWindow::menu_view_fit_screen()     { viewer_.fit_screen(); }
void MainWindow::menu_view_snapshot()       { open_dialog(dlg_snapshot_, st_snapshot_); }
