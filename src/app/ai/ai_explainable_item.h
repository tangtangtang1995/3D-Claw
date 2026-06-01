// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_EXPLAINABLE_ITEM_H
#define CLAW3D_AI_EXPLAINABLE_ITEM_H

/// Stable identifiers for UI panels that can expose context to the AI assistant.

#include <string>

namespace easy3d { class Model; }

enum class AIExplainPanel {
    ModelList,
    Properties,
    HealthReport,
    History,
    MenuBar
};

enum class AIExplainItemType {
    ModelNode,
    DrawableNode,
    PropertyRow,
    DisplayField,
    ScalarAttribute,
    HealthFinding,
    HistoryEntry,
    MenuGroup,
    MenuCommand
};

struct AIExplainableItem {
    // Do NOT store raw Model* across frames. Callers should only pass a model
    // pointer that is known to be live in the current UI frame. The prompt
    // builder only treats nullptr as "no model context"; it cannot validate a
    // dangling pointer after the model has been deleted.
    AIExplainPanel panel = AIExplainPanel::ModelList;
    AIExplainItemType item_type = AIExplainItemType::ModelNode;

    std::string label;           // human-readable row name / title
    std::string value;           // displayed value (e.g. "35947", "v:normal")
    std::string detail;          // extra info (tree kind, attr location, severity)
    std::string severity;        // ""=neutral, "warning", "error"
    std::string source_hint;     // where the item came from (e.g. "bunny.ply")
};

// Build a consistent AI prompt from the item + optional model context.
// `model` may be nullptr (e.g. History entries not tied to a live model).
std::string build_ai_explain_prompt(const AIExplainableItem& item,
                                     const easy3d::Model* model);

// Short one-liner shown in the AI Chat panel for an item-based ask, so
// the chat history stays readable. Examples:
//   "Ask about bunny.ply (Model List > ModelNode)"
//   "Ask about attribute v:normal"
//   "Ask about Health finding: non-watertight (warning)"
// The full prompt from build_ai_explain_prompt is still what the API sees.
std::string build_ai_explain_display_label(const AIExplainableItem& item);

// One-line helper that wires an explainable item to a hover Ask-AI anchor.
// Lambdas capture `item` by value and `model` by raw pointer (re-evaluated
// every frame, so the dangling-pointer window is just the current frame).
// Returns true if the user clicked the anchor this frame.
bool ai_hover_for_item(const char* unique_id,
                       const AIExplainableItem& item,
                       const easy3d::Model* model);

#endif // CLAW3D_AI_EXPLAINABLE_ITEM_H
