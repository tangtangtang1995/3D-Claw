// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PANEL_HELP_H
#define CLAW3D_PANEL_HELP_H

/// Compact help text snippets for algorithm and utility panels.

#include <functional>

// Standard one-screen header drawn at the top of an algorithm dialog body.
// Renders 2 short text lines ("Purpose:" / "Best for:") plus a trailing "?"
// button that fires either a custom callback (if provided) or a default
// "explain this panel" AI request via MainWindow::send_ai_request().
//
// Returns immediately after rendering -- always call before the rest of the
// dialog body. Both strings should fit on one line each; line wrapping is
// enabled but more than ~120 chars looks crowded in the docked panel.
void render_panel_header(const char* purpose,
                         const char* best_for,
                         std::function<void()> on_ai_help = nullptr);

#endif // CLAW3D_PANEL_HELP_H
