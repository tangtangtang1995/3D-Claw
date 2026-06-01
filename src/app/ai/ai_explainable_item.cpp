// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_explainable_item.h"
#include "ai/ai_language.h"
#include "ai/ai_widget.h"
#include "ai/ai_context.h"

#include <sstream>
#include <cstring>

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/core/box.h>

namespace {

constexpr int kTotalCap = 6000;
constexpr int kFieldCap = 1000;

std::string clamp(const std::string& s, int cap = kFieldCap) {
    if ((int)s.size() <= cap) return s;
    return s.substr(0, cap) + "...";
}

std::string describe_model(const easy3d::Model* m) {
    if (!m) return "";
    std::ostringstream o;
    if (auto* pc = dynamic_cast<const easy3d::PointCloud*>(m)) {
        o << "- Type: PointCloud\n";
        o << "- Vertices: " << pc->n_vertices() << "\n";
        bool hn = pc->get_vertex_property<easy3d::vec3>("v:normal");
        bool hc = pc->get_vertex_property<easy3d::vec3>("v:color");
        o << "- Has normals: " << (hn ? "yes" : "**no**") << "\n";
        o << "- Has colors:  " << (hc ? "yes" : "no") << "\n";
    } else if (auto* sm = dynamic_cast<const easy3d::SurfaceMesh*>(m)) {
        o << "- Type: SurfaceMesh\n";
        o << "- Vertices: " << sm->n_vertices() << "\n";
        o << "- Faces: " << sm->n_faces() << "\n";
        o << "- Edges: " << sm->n_edges() << "\n";
    } else if (auto* g = dynamic_cast<const easy3d::Graph*>(m)) {
        o << "- Type: Graph\n";
        o << "- Vertices: " << g->n_vertices() << "\n";
        o << "- Edges: " << g->n_edges() << "\n";
    }
    if (m->bounding_box().is_valid())
        o << "- BBox diag: " << m->bounding_box().diagonal_length() << "\n";
    return o.str();
}

const char* panel_name(AIExplainPanel p) {
    switch (p) {
        case AIExplainPanel::ModelList:   return "Model List";
        case AIExplainPanel::Properties:  return "Properties";
        case AIExplainPanel::HealthReport:return "Health Report";
        case AIExplainPanel::History:     return "History";
        case AIExplainPanel::MenuBar:     return "Menu Bar";
    }
    return "unknown panel";
}

} // namespace


std::string build_ai_explain_prompt(const AIExplainableItem& item,
                                     const easy3d::Model* model)
{
    std::ostringstream o;
    o << "User clicked 'Ask AI' in the **" << panel_name(item.panel) << "** panel.\n\n";

    // Item details
    o << "## Panel item\n";
    if (!item.label.empty())   o << "- " << clamp(item.label) << "\n";
    if (!item.value.empty())   o << "- Value: " << clamp(item.value) << "\n";
    if (!item.detail.empty())  o << "- " << clamp(item.detail) << "\n";
    if (!item.severity.empty())o << "- Severity: " << clamp(item.severity) << "\n";
    if (!item.source_hint.empty()) o << "- Source: " << clamp(item.source_hint) << "\n";

    // Model context (liveness-checked)
    if (model) {
        o << "\n## Current model\n";
        o << describe_model(model);
    }

    // Task
    o << "\n## Task\n";
    switch (item.panel) {
        case AIExplainPanel::ModelList:
            o << "Explain what this model is (type + tree kind + size). "
                 "Suggest 2-3 next operations in 3D Claw based on the numbers "
                 "above (e.g. Poisson for a PC with normals, Hole Filling for "
                 "a mesh with boundaries).";
            break;
        case AIExplainPanel::Properties:
            o << "Explain this property/field - what it means for this model, "
                 "which 3D Claw algorithms use or produce it, and whether "
                 "missing/invalid values are a problem. Be specific to this model.";
            break;
        case AIExplainPanel::HealthReport:
            o << "Explain this health finding. Is it a real problem? "
                 "What 3D Claw operations can fix or mitigate it? "
                 "Suggest a concrete repair path with menu entries.";
            break;
        case AIExplainPanel::History:
            o << "Analyze this operation result. Was it successful? "
                 "What do the numbers (elapsed time, output size) tell us? "
                 "Suggest 1-2 reasonable next steps.";
            break;
        case AIExplainPanel::MenuBar:
            o << "Explain this 3D Claw menu entry in context. Say when the user "
                 "should use it, required model type or selection state, whether "
                 "it changes geometry, and the safest next step. If the entry "
                 "opens a dialog with its own AI help, avoid parameter details "
                 "and tell the user to use that panel help after opening it.";
            break;
    }
    o << " Be specific about this model's numbers (100-200 chars). "
         "No generic textbook explanations. "
      << ai_lang::directive();

    std::string s = o.str();
    if ((int)s.size() > kTotalCap)
        s = s.substr(0, kTotalCap) + "...(truncated)";
    return s;
}


std::string build_ai_explain_display_label(const AIExplainableItem& item) {
    std::ostringstream o;
    o << "Ask about ";
    if (!item.label.empty())
        o << item.label;
    else
        o << "(unlabeled item)";
    // Append a short type qualifier so it's clear which panel/kind it came
    // from when the user scans the chat history.
    const char* kind = nullptr;
    switch (item.item_type) {
        case AIExplainItemType::ModelNode:        kind = "model"; break;
        case AIExplainItemType::DrawableNode:     kind = "drawable"; break;
        case AIExplainItemType::PropertyRow:      kind = "property"; break;
        case AIExplainItemType::DisplayField:     kind = "display field"; break;
        case AIExplainItemType::ScalarAttribute:  kind = "attribute"; break;
        case AIExplainItemType::HealthFinding:    kind = "health finding"; break;
        case AIExplainItemType::HistoryEntry:     kind = "history entry"; break;
        case AIExplainItemType::MenuGroup:        kind = "menu group"; break;
        case AIExplainItemType::MenuCommand:      kind = "menu command"; break;
    }
    if (kind) o << " (" << kind;
    if (!item.severity.empty()) o << ", " << item.severity;
    if (kind) o << ")";
    return o.str();
}


bool ai_hover_for_item(const char* unique_id,
                       const AIExplainableItem& item,
                       const easy3d::Model* model)
{
    return ai_hover_tip(unique_id,
        [item, model]() { return build_ai_explain_prompt(item, model); },
        AICtx_CurrentModel | AICtx_ActivePanel,
        build_ai_explain_display_label(item));
}
