// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_PLATFORM_CLIPBOARD_H
#define CLAW3D_APP_PLATFORM_CLIPBOARD_H

/// Clipboard helpers that hide the current window backend from viewport code.

#include <string>

namespace claw3d::app {

void set_clipboard_text(const std::string& text);
std::string clipboard_text();

} // namespace claw3d::app

#endif // CLAW3D_APP_PLATFORM_CLIPBOARD_H
