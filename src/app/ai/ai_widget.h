// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_WIDGET_H
#define CLAW3D_AI_WIDGET_H

/// ImGui widgets and state for the embedded AI chat panel.

#include <functional>
#include <string>

#include "imgui.h"

// One-line "Ask AI" affordance for a panel item.
//
// Call this right after submitting an ImGui item such as a TreeNode,
// Selectable, button, row, or property field. It draws a small dim "?"
// anchor on the right side of the row only while the row is hovered.
//
// Clicking the anchor opens AI Chat with a task prompt built by the
// caller-supplied lambda. The lambda is invoked lazily on click, not every
// frame, so it can assemble richer model or panel context.
//
// This is intended for dock panels such as Model List, Properties, History,
// Log, and Health Report. Algorithm dialogs should keep using their own
// dedicated AI controls.
//
// unique_id must be unique per item in the current ImGui ID stack.
// Returns true when the AI anchor consumed a click.
bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder);
bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder,
                  unsigned ctx_flags);
// Variant with a short display label for the AI Chat history (the full
// prompt still goes to the API verbatim). Empty label = fall back to
// showing the full prompt in the panel.
bool ai_hover_tip(const char* unique_id,
                  std::function<std::string()> prompt_builder,
                  unsigned ctx_flags,
                  const std::string& display_label);

#endif // CLAW3D_AI_WIDGET_H
