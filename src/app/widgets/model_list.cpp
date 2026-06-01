// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"
#include "widgets/model_utils.h"

#include "ai/ai_context.h"
#include "ai/ai_explainable_item.h"
#include "ai/ai_widget.h"
#include "ui/layout_helpers.h"
#include "viewport/viewport_canvas.h"
#include "window/main_window.h"

#include <easy3d/core/model.h>
#include <easy3d/renderer/drawable.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"


namespace {

bool has_model_name_suffix(const std::string& name, const char* suffix) {
    const std::size_t suffix_len = std::char_traits<char>::length(suffix);
    return name.size() >= suffix_len &&
        name.compare(name.size() - suffix_len, suffix_len, suffix) == 0;
}

bool is_ch_child_name(const std::string& name) {
    return has_model_name_suffix(name, ".ch") ||
        has_model_name_suffix(name, "_ch");
}

bool is_as_child_name(const std::string& name) {
    return has_model_name_suffix(name, ".as") ||
        has_model_name_suffix(name, "_as");
}

bool is_primitive_child_name(const std::string& name) {
    return is_ch_child_name(name) || is_as_child_name(name);
}

bool is_region_growing_root_name(const std::string& name) {
    return !is_primitive_child_name(name) &&
        name.rfind("rg_region_", 0) == 0;
}

bool is_ransac_root_name(const std::string& name) {
    return !is_primitive_child_name(name) &&
        name.rfind("plane_", 0) == 0;
}

enum class PrimitiveGroupKind {
    RegionGrowing,
    Ransac,
    Other
};

PrimitiveGroupKind primitive_group_kind(easy3d::Model* model) {
    if (!model)
        return PrimitiveGroupKind::Other;
    const std::string& name = model->name();
    if (is_region_growing_root_name(name))
        return PrimitiveGroupKind::RegionGrowing;
    if (is_ransac_root_name(name))
        return PrimitiveGroupKind::Ransac;
    return PrimitiveGroupKind::Other;
}

const char* primitive_group_label(PrimitiveGroupKind kind) {
    switch (kind) {
    case PrimitiveGroupKind::RegionGrowing:
        return "Region Growing Results";
    case PrimitiveGroupKind::Ransac:
        return "RANSAC Results";
    case PrimitiveGroupKind::Other:
        return "Primitive Results";
    }
    return "Primitive Results";
}

const char* primitive_group_primary_label(PrimitiveGroupKind kind) {
    switch (kind) {
    case PrimitiveGroupKind::RegionGrowing:
        return "Regions";
    case PrimitiveGroupKind::Ransac:
        return "Planes";
    case PrimitiveGroupKind::Other:
        return "Primitives";
    }
    return "Primitives";
}

void render_model_tree_node(ViewportCanvas* viewer,
                            easy3d::Model* m,
                            const ModelTreeNodeInfo& info,
                            int& node_id,
                            const std::unordered_map<easy3d::Model*,
                                std::vector<easy3d::Model*>>& children_map) {
    if (!info.visible_in_tree)
        return;

    bool is_current = (m == viewer->current_model());
    SelectedDrawableType cur_type = viewer->selected_drawable_type();

    bool has_model_children = false;
    auto cit = children_map.find(m);
    if (cit != children_map.end()) {
        for (auto* c : cit->second) {
            auto* cinfo = viewer->model_tree_info(c);
            if (cinfo && cinfo->visible_in_tree) {
                has_model_children = true;
                break;
            }
        }
    }

    const bool show_drawable_children =
        info.kind != ModelTreeNodeKind::Primitive;
    auto* p_drawable = show_drawable_children ? model_points_drawable(m) : nullptr;
    auto* l_drawable = show_drawable_children ? model_lines_drawable(m) : nullptr;
    auto* t_drawable = show_drawable_children ? model_triangles_drawable(m) : nullptr;
    bool has_drawables = p_drawable || l_drawable || t_drawable;
    bool has_children = has_model_children || has_drawables;

    ImGuiTreeNodeFlags flags = (has_children ? ImGuiTreeNodeFlags_OpenOnArrow
        : ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen)
        | ImGuiTreeNodeFlags_SpanAvailWidth
        | ImGuiTreeNodeFlags_AllowOverlap
        | ((is_current && cur_type == SelectedDrawableType::None)
            ? ImGuiTreeNodeFlags_Selected : 0);

    bool vis = m->renderer()->is_visible();
    if (ImGui::Checkbox(("##vis" + std::to_string(node_id)).c_str(), &vis)) {
        m->renderer()->set_visible(vis);
        std::function<void(easy3d::Model*)> set_annotation_children =
            [&](easy3d::Model* node) {
                auto it = children_map.find(node);
                if (it == children_map.end())
                    return;
                for (auto* child : it->second) {
                    auto* cinfo = viewer->model_tree_info(child);
                    if (cinfo && cinfo->kind == ModelTreeNodeKind::Annotation)
                        child->renderer()->set_visible(vis);
                    set_annotation_children(child);
                }
            };
        set_annotation_children(m);
        viewer->mark_dirty();
    }
    ImGui::SameLine();

    ImVec4 label_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (info.kind == ModelTreeNodeKind::Reconstruction)
        label_color = ImVec4(0.14f, 0.37f, 0.64f, 1.0f);
    else if (info.kind == ModelTreeNodeKind::Primitive)
        label_color = ImVec4(0.34f, 0.39f, 0.46f, 1.0f);
    else if (info.kind == ModelTreeNodeKind::Annotation)
        label_color = ImVec4(0.55f, 0.36f, 0.02f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, label_color);
    bool node_open = ImGui::TreeNodeEx((void*)(intptr_t)(++node_id), flags,
                                       "%s", info.display_name.c_str());
    ImGui::PopStyleColor();

    const bool node_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // Describe this tree node for the AI. Built once and shared by both the
    // right-click "Ask AI" menu item and the hover "?" anchor below.
    AIExplainableItem item;
    item.panel = AIExplainPanel::ModelList;
    item.item_type = AIExplainItemType::ModelNode;
    item.label = info.display_name;
    item.source_hint = info.display_name;
    switch (info.kind) {
        case ModelTreeNodeKind::Data:
            item.detail = "Tree kind: Data (imported)";
            break;
        case ModelTreeNodeKind::Reconstruction:
            item.detail = "Tree kind: Reconstruction (algorithm output)";
            break;
        case ModelTreeNodeKind::Primitive:
            item.detail = "Tree kind: Primitive (RANSAC)";
            break;
        case ModelTreeNodeKind::Annotation:
            item.detail = "Tree kind: Annotation";
            break;
        case ModelTreeNodeKind::Overlay:
            item.detail = "Tree kind: Overlay (preview)";
            break;
        case ModelTreeNodeKind::Debug:
            item.detail = "Tree kind: Debug";
            break;
    }
    const easy3d::Model* model_capture = m;

    bool deleted = false;
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Ask AI")) {
            // Same action as the hover "?" anchor: open AI Chat and send a
            // task prompt carrying this node + the current-model context.
            if (auto* win = MainWindow::instance()) {
                win->show_ai_chat();
                win->send_ai_request(
                    build_ai_explain_prompt(item, model_capture),
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    std::string(),
                    build_ai_explain_display_label(item));
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete")) {
            viewer->delete_model(m);
            viewer->mark_dirty();
            deleted = true;
        }
        ImGui::EndPopup();
    }
    if (deleted) {
        if (node_open && has_children)
            ImGui::TreePop();
        return;
    }

    char id_buf[32];
    std::snprintf(id_buf, sizeof id_buf, "ai_ml_%p", (void*)m);
    bool ai_clicked = ai_hover_tip(id_buf,
        [item, model_capture]() {
            return build_ai_explain_prompt(item, model_capture);
        },
        AICtx_CurrentModel | AICtx_ActivePanel,
        build_ai_explain_display_label(item));

    if (!ai_clicked && node_clicked) {
        viewer->set_current_model(m);
        viewer->set_selected_drawable_type(SelectedDrawableType::None);
    }

    if (node_open && has_children) {
        const float indent_amount = ImGui::GetStyle().IndentSpacing;
        const float child_row_x = ImGui::GetCursorScreenPos().x;
        const float vline_x = child_row_x - indent_amount * 0.5f;
        const float vline_top_y = ImGui::GetCursorScreenPos().y;
        const ImU32 line_col = IM_COL32(180, 180, 180, 220);
        const float line_thick = 1.5f;
        const float stub_len = indent_amount * 0.5f - 2.0f;
        ImDrawList* dlist = ImGui::GetWindowDrawList();
        float last_center_y = vline_top_y;

        const float row_h = ImGui::GetTextLineHeightWithSpacing();
        auto draw_connector_for_row = [&](float row_top_y) {
            float center_y = row_top_y + row_h * 0.5f;
            dlist->AddLine(ImVec2(vline_x, center_y),
                           ImVec2(vline_x + stub_len, center_y),
                           line_col, line_thick);
            last_center_y = center_y;
        };

        auto draw_subnode = [&](const char* name,
                                easy3d::Drawable* d,
                                SelectedDrawableType type) {
            if (!d)
                return;
            ++node_id;
            bool sub_selected = is_current && cur_type == type;
            ImGuiTreeNodeFlags sub_flags = ImGuiTreeNodeFlags_Leaf
                | ImGuiTreeNodeFlags_NoTreePushOnOpen
                | ImGuiTreeNodeFlags_SpanAvailWidth
                | (sub_selected ? ImGuiTreeNodeFlags_Selected : 0);
            float row_top = ImGui::GetCursorScreenPos().y;
            bool dv = d->is_visible();
            ImGui::PushID(node_id);
            if (ImGui::Checkbox("##v", &dv)) {
                d->set_visible(dv);
                viewer->mark_dirty();
            }
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::TreeNodeEx((void*)(intptr_t)node_id, sub_flags, "%s", name);
            if (ImGui::IsItemClicked()) {
                viewer->set_current_model(m);
                viewer->set_selected_drawable_type(type);
            }
            draw_connector_for_row(row_top);
        };
        draw_subnode("Points", p_drawable, SelectedDrawableType::Points);
        draw_subnode("Lines", l_drawable, SelectedDrawableType::Lines);
        draw_subnode("Triangles", t_drawable, SelectedDrawableType::Triangles);

        auto collect_primitive_sets =
            [&](const std::vector<easy3d::Model*>& roots,
                std::vector<easy3d::Model*>& primary,
                std::vector<easy3d::Model*>& ch,
                std::vector<easy3d::Model*>& as) {
                std::function<void(easy3d::Model*)> collect =
                    [&](easy3d::Model* node) {
                        auto* node_info = viewer->model_tree_info(node);
                        if (!node_info || !node_info->visible_in_tree)
                            return;
                        if (node_info->kind == ModelTreeNodeKind::Primitive) {
                            const std::string& name = node->name();
                            if (is_ch_child_name(name))
                                ch.push_back(node);
                            else if (is_as_child_name(name))
                                as.push_back(node);
                            else
                                primary.push_back(node);
                        }
                        auto child_it = children_map.find(node);
                        if (child_it == children_map.end())
                            return;
                        for (auto* child : child_it->second)
                            collect(child);
                    };
                for (auto* root : roots)
                    collect(root);
            };

        auto count_visible = [](const std::vector<easy3d::Model*>& models) {
            int count = 0;
            for (auto* model : models) {
                if (model && model->renderer()->is_visible())
                    ++count;
            }
            return count;
        };

        auto render_visibility_controls =
            [&](PrimitiveGroupKind group_kind,
                const std::vector<easy3d::Model*>& roots) {
                std::vector<easy3d::Model*> primary, ch, as;
                collect_primitive_sets(roots, primary, ch, as);

                auto master_check = [&](const char* label,
                                        const std::vector<easy3d::Model*>& models) {
                    if (models.empty())
                        return;
                    ImGui::PushID(label);
                    const int total = (int)models.size();
                    const int visible = count_visible(models);
                    bool all_visible = total > 0 && visible == total;
                    if (ImGui::Checkbox("##mc", &all_visible)) {
                        for (auto* model : models)
                            model->renderer()->set_visible(all_visible);
                        viewer->mark_dirty();
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", label);
                    ImGui::PopID();
                };

                bool wrote_any = false;
                if (!primary.empty()) {
                    master_check(primitive_group_primary_label(group_kind),
                                 primary);
                    wrote_any = true;
                }
                if (!ch.empty()) {
                    if (wrote_any)
                        claw_ui::same_line_if_fits_width(55.0f);
                    master_check("CH", ch);
                    wrote_any = true;
                }
                if (!as.empty()) {
                    if (wrote_any)
                        claw_ui::same_line_if_fits_width(55.0f);
                    master_check("AS", as);
                }
            };

        auto render_primitive_group =
            [&](PrimitiveGroupKind group_kind,
                const std::vector<easy3d::Model*>& roots) {
                if (roots.empty())
                    return;

                const float row_top = ImGui::GetCursorScreenPos().y;
                ImGuiTreeNodeFlags group_flags =
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                if (roots.size() <= 12)
                    group_flags |= ImGuiTreeNodeFlags_DefaultOpen;
                const int group_id = ++node_id;
                bool group_open = ImGui::TreeNodeEx(
                    (void*)(intptr_t)group_id,
                    group_flags,
                    "%s (%d)",
                    primitive_group_label(group_kind),
                    (int)roots.size());
                draw_connector_for_row(row_top);
                if (!group_open)
                    return;

                render_visibility_controls(group_kind, roots);

                const float group_indent = ImGui::GetStyle().IndentSpacing;
                const float group_child_x = ImGui::GetCursorScreenPos().x;
                const float group_vline_x = group_child_x - group_indent * 0.5f;
                const float group_top_y = ImGui::GetCursorScreenPos().y;
                float group_last_center_y = group_top_y;
                for (auto* root : roots) {
                    auto* root_info = viewer->model_tree_info(root);
                    if (!root_info || !root_info->visible_in_tree)
                        continue;
                    const float child_top = ImGui::GetCursorScreenPos().y;
                    render_model_tree_node(viewer, root, *root_info,
                                           node_id, children_map);
                    const float center_y = child_top + row_h * 0.5f;
                    dlist->AddLine(
                        ImVec2(group_vline_x, center_y),
                        ImVec2(group_vline_x + stub_len, center_y),
                        line_col, line_thick);
                    group_last_center_y = center_y;
                }
                if (group_last_center_y > group_top_y) {
                    dlist->AddLine(
                        ImVec2(group_vline_x, group_top_y),
                        ImVec2(group_vline_x, group_last_center_y),
                        line_col, line_thick);
                }
                ImGui::TreePop();
            };

        std::vector<easy3d::Model*> rg_roots;
        std::vector<easy3d::Model*> ransac_roots;
        std::vector<easy3d::Model*> other_primitive_roots;
        if (has_model_children && info.kind != ModelTreeNodeKind::Primitive
            && cit != children_map.end()) {
            for (auto* child : cit->second) {
                auto* child_info = viewer->model_tree_info(child);
                if (!child_info || !child_info->visible_in_tree ||
                    child_info->kind != ModelTreeNodeKind::Primitive)
                    continue;
                switch (primitive_group_kind(child)) {
                case PrimitiveGroupKind::RegionGrowing:
                    rg_roots.push_back(child);
                    break;
                case PrimitiveGroupKind::Ransac:
                    ransac_roots.push_back(child);
                    break;
                case PrimitiveGroupKind::Other:
                    other_primitive_roots.push_back(child);
                    break;
                }
            }
        }

        render_primitive_group(PrimitiveGroupKind::RegionGrowing, rg_roots);
        render_primitive_group(PrimitiveGroupKind::Ransac, ransac_roots);
        render_primitive_group(PrimitiveGroupKind::Other,
                               other_primitive_roots);

        if (has_model_children && cit != children_map.end()) {
            for (auto* c : cit->second) {
                auto* cinfo = viewer->model_tree_info(c);
                if (!cinfo || !cinfo->visible_in_tree)
                    continue;
                if (info.kind != ModelTreeNodeKind::Primitive &&
                    cinfo->kind == ModelTreeNodeKind::Primitive)
                    continue;
                float row_top = ImGui::GetCursorScreenPos().y;
                render_model_tree_node(viewer, c, *cinfo, node_id, children_map);
                draw_connector_for_row(row_top);
            }
        }

        if (last_center_y > vline_top_y) {
            dlist->AddLine(ImVec2(vline_x, vline_top_y),
                           ImVec2(vline_x, last_center_y),
                           line_col, line_thick);
        }

        ImGui::TreePop();
    }
}

} // namespace


void renderWidgetModelList(ViewportCanvas* viewer,
                           ModelListState&,
                           bool& open) {
    if (!open)
        return;

    ImGui::SetNextWindowSize(ImVec2(250, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Model List", &open)) {
        const auto& models = viewer->models();
        if (models.empty()) {
            ImGui::TextUnformatted("(no models)");
        } else {
            std::unordered_map<easy3d::Model*, std::vector<easy3d::Model*>>
                children_map;
            for (const auto& msp : models) {
                auto* m = msp.get();
                auto* info = viewer->model_tree_info(m);
                if (!info)
                    continue;
                auto* parent = info->parent;
                children_map[parent].push_back(m);
            }

            if (ImGui::SmallButton("Show All")) {
                for (const auto& msp : models)
                    msp->renderer()->set_visible(true);
                viewer->mark_dirty();
            }
            claw_ui::same_line_if_fits_button("Hide All");
            if (ImGui::SmallButton("Hide All")) {
                for (const auto& msp : models)
                    msp->renderer()->set_visible(false);
                viewer->mark_dirty();
            }
            ImGui::Separator();

            int node_id = 0;
            auto ws_names = viewer->workspace_names();
            for (const auto& ws : ws_names) {
                std::vector<easy3d::Model*> roots;
                for (const auto& msp : models) {
                    auto* m = msp.get();
                    auto* info = viewer->model_tree_info(m);
                    if (info && info->workspace_name == ws &&
                        info->parent == nullptr && info->visible_in_tree)
                        roots.push_back(m);
                }
                if (roots.empty())
                    continue;

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                ImGuiTreeNodeFlags ws_flags = ImGuiTreeNodeFlags_DefaultOpen
                    | ImGuiTreeNodeFlags_SpanAvailWidth;
                bool ws_open = ImGui::TreeNodeEx(ws.c_str(), ws_flags,
                                                 "%s", ws.c_str());
                if (ws_open) {
                    const float indent_amount = ImGui::GetStyle().IndentSpacing;
                    const float child_row_x = ImGui::GetCursorScreenPos().x;
                    const float vline_x = child_row_x - indent_amount * 0.5f;
                    const float vline_top_y = ImGui::GetCursorScreenPos().y;
                    const ImU32 line_col = IM_COL32(180, 180, 180, 220);
                    const float line_thick = 1.5f;
                    const float stub_len = indent_amount * 0.5f - 2.0f;
                    ImDrawList* dlist = ImGui::GetWindowDrawList();
                    float last_center_y = vline_top_y;
                    for (auto* m : roots) {
                        auto* info = viewer->model_tree_info(m);
                        if (!info)
                            continue;
                        float row_top = ImGui::GetCursorScreenPos().y;
                        render_model_tree_node(viewer, m, *info, node_id,
                                               children_map);
                        const float row_h = ImGui::GetTextLineHeightWithSpacing();
                        float center_y = row_top + row_h * 0.5f;
                        dlist->AddLine(ImVec2(vline_x, center_y),
                                       ImVec2(vline_x + stub_len, center_y),
                                       line_col, line_thick);
                        last_center_y = center_y;
                    }
                    if (last_center_y > vline_top_y) {
                        dlist->AddLine(ImVec2(vline_x, vline_top_y),
                                       ImVec2(vline_x, last_center_y),
                                       line_col, line_thick);
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
    ImGui::End();
}
