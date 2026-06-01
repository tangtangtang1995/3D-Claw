// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_PROMPT_UTILS_H
#define CLAW3D_AI_PROMPT_UTILS_H

/// Shared helpers for composing AI-visible prompt blocks.

#include <string>

class MainWindow;

namespace claw_ai {

// Keep prompt payloads ASCII-safe for the current Windows/MSVC codepage.
std::string ascii_only(const std::string& text);

// Common path for panel-side AI requests. It preserves the existing dialog
// behavior: open AI Chat, no-op when no key is configured, and send the prompt
// directly without injecting extra context.
bool send_panel_ai_prompt(MainWindow* win,
                          const std::string& prompt,
                          const std::string& display_label = std::string());

} // namespace claw_ai

#endif // CLAW3D_AI_PROMPT_UTILS_H
