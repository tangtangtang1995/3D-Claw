// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Shared helpers for "Ask AI" affordances in main menu sections.
//
// These helpers are intentionally small and UI-facing. They let menu sections
// live in separate .cpp files without keeping all menu AI prompt code inside
// menu_bar.cpp.

#ifndef CLAW3D_AI_MENU_UTILS_H
#define CLAW3D_AI_MENU_UTILS_H

/// Shared menu helpers for triggering AI assistance from application menus.

namespace easy3d { class Model; }

class MainWindow;

bool menu_command_ai_tip(const char* path,
                         const char* summary,
                         const char* requirements,
                         const char* risks,
                         bool opens_dialog,
                         bool has_dialog_ai,
                         const easy3d::Model* model);

void menu_group_ai_item(MainWindow* win,
                        const char* label,
                        const char* path,
                        const char* summary,
                        const char* requirements,
                        const char* risks,
                        const easy3d::Model* model);

void menu_cgal_required_item(const char* label, const char* tooltip);

#endif // CLAW3D_AI_MENU_UTILS_H
