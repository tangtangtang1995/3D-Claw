// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/imgui_widgets.h"
#include "widgets/attribute_manager.h"
#include "widgets/model_utils.h"
#include "viewport/viewport_canvas.h"
#include "ai/ai_context.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/graph.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/state.h>
#include <easy3d/renderer/texture.h>
#include <easy3d/renderer/texture_manager.h>

#include "ai/ai_widget.h"
#include "ai/ai_explainable_item.h"

#include <cstdio>
#include <vector>

#include "imgui.h"


struct BakedTransformInfo {
    bool available = false;
    easy3d::mat4 last = easy3d::mat4::identity();
    easy3d::mat4 cumulative = easy3d::mat4::identity();
    std::string operation;
    int count = 0;
};

template <typename ModelT>
bool read_baked_transform_properties(ModelT* model,
                                     BakedTransformInfo& info) {
    if (!model)
        return false;
    auto last = model->template get_model_property<easy3d::mat4>(
        "m:transform_last_baked");
    if (!last)
        return false;

    info.available = true;
    info.last = last[0];
    if (auto cumulative = model->template get_model_property<easy3d::mat4>(
            "m:transform_cumulative_baked"))
        info.cumulative = cumulative[0];
    else
        info.cumulative = info.last;
    if (auto operation = model->template get_model_property<std::string>(
            "m:transform_last_operation"))
        info.operation = operation[0];
    if (auto count = model->template get_model_property<int>(
            "m:transform_baked_count"))
        info.count = count[0];
    else
        info.count = 1;
    return true;
}

BakedTransformInfo baked_transform_info(easy3d::Model* model) {
    BakedTransformInfo info;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model))
        read_baked_transform_properties(mesh, info);
    else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model))
        read_baked_transform_properties(cloud, info);
    else if (auto* graph = dynamic_cast<easy3d::Graph*>(model))
        read_baked_transform_properties(graph, info);
    return info;
}

void render_matrix4(const easy3d::mat4& m) {
    for (int r = 0; r < 4; ++r) {
        ImGui::Text("%.4f  %.4f  %.4f  %.4f",
                    m(r, 0), m(r, 1), m(r, 2), m(r, 3));
    }
}

// =============================================================================
// Common drawable controls
// =============================================================================
namespace {

void uniform_color_edit(const char* label, easy3d::Drawable* d, ViewportCanvas* viewer) {
    auto c = d->color();
    float arr[4] = {c[0], c[1], c[2], c[3]};
    if (ImGui::ColorEdit4(label, arr)) {
        d->set_color(easy3d::vec4(arr[0], arr[1], arr[2], arr[3]));
        viewer->mark_dirty();
    }
}

easy3d::Texture* texture_for_surface_mesh(easy3d::SurfaceMesh* mesh) {
    if (!mesh)
        return nullptr;
    auto texture_path =
        mesh->get_model_property<std::string>("m:texture_diffuse");
    if (texture_path && !texture_path[0].empty())
        return easy3d::TextureManager::request(texture_path[0]);

    auto texture_data =
        mesh->get_model_property<std::vector<unsigned char>>("m:texture_data");
    auto texture_width = mesh->get_model_property<int>("m:texture_width");
    auto texture_height = mesh->get_model_property<int>("m:texture_height");
    if (texture_data && texture_width && texture_height &&
        !texture_data[0].empty() &&
        texture_width[0] > 0 && texture_height[0] > 0) {
        return easy3d::Texture::create(
            texture_data[0], texture_width[0], texture_height[0], 4);
    }
    return nullptr;
}

bool apply_surface_mesh_texture_coloring(easy3d::Model* model,
                                         easy3d::Drawable* drawable) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    auto* faces = dynamic_cast<easy3d::TrianglesDrawable*>(drawable);
    if (!mesh || !faces)
        return false;

    auto htexcoord = mesh->get_halfedge_property<easy3d::vec2>("h:texcoord");
    auto vtexcoord = mesh->get_vertex_property<easy3d::vec2>("v:texcoord");
    auto* texture = texture_for_surface_mesh(mesh);
    if (!texture || (!htexcoord && !vtexcoord))
        return false;

    if (htexcoord) {
        faces->set_texture_coloring(easy3d::State::HALFEDGE,
                                    "h:texcoord", texture);
    } else {
        faces->set_texture_coloring(easy3d::State::VERTEX,
                                    "v:texcoord", texture);
    }
    faces->update();
    mesh->renderer()->update();
    return true;
}

void render_coloring_controls(easy3d::Model* model,
                              easy3d::Drawable* d,
                              ViewportCanvas* viewer) {
    ImGui::Separator();
    ImGui::Text("Coloring");

    const char* methods[] = {"Uniform", "Scalar Field", "Color Property", "Textured"};
    int cm = (int)d->coloring_method();
    if (cm > 3) cm = 0;
    if (ImGui::Combo("Method", &cm, methods, 4)) {
        auto m = static_cast<easy3d::State::Method>(cm);
        if (m == easy3d::State::UNIFORM_COLOR) {
            d->set_uniform_coloring(d->color());
        } else if (m == easy3d::State::TEXTURED &&
                   apply_surface_mesh_texture_coloring(model, d)) {
            // Rebind the model texture. Switching to uniform clears the
            // drawable property name, so set_coloring_method(TEXTURED) alone
            // cannot restore the texture state.
        } else {
            // Non-uniform methods read a property by name. Switching the
            // method here without a valid name yields buffer.cpp warnings
            // ("scalar field '' not found" / "color property 'uniform color'
            // not found"). Stay on uniform until the user enters a name and
            // clicks Apply below -- the combo will snap back next frame.
            const std::string& pn = d->property_name();
            const bool bad_name = pn.empty() || pn == "uniform color";
            if (bad_name) {
                d->set_uniform_coloring(d->color());
            } else {
                d->set_coloring_method(m);
            }
        }
        viewer->mark_dirty();
    }

    if (d->coloring_method() == easy3d::State::UNIFORM_COLOR) {
        uniform_color_edit("##uniform", d, viewer);
    } else if (d->coloring_method() == easy3d::State::SCALAR_FIELD) {
        static char scalar_name[128] = "";
        ImGui::InputText("Property", scalar_name, sizeof(scalar_name));
        claw_ui::same_line_if_fits_button("Apply##s");
        if (ImGui::Button("Apply##s")) {
            d->set_scalar_coloring(easy3d::State::VERTEX, scalar_name);
            viewer->mark_dirty();
        }
    } else if (d->coloring_method() == easy3d::State::COLOR_PROPERTY) {
        static char color_name[128] = "";
        ImGui::InputText("Property", color_name, sizeof(color_name));
        claw_ui::same_line_if_fits_button("Apply##c");
        if (ImGui::Button("Apply##c")) {
            d->set_property_coloring(easy3d::State::VERTEX, color_name);
            viewer->mark_dirty();
        }
    }
}

} // anonymous namespace


// =============================================================================
// Drawable section helpers (used by renderWidgetProperties)
// =============================================================================
namespace {

void render_points_section(easy3d::Model* model, ViewportCanvas* viewer) {
    auto* d = model_points_drawable(model);
    if (!d) { ImGui::TextDisabled("(no points drawable)"); return; }
    bool vis = d->is_visible();
    if (ImGui::Checkbox("Visible##P", &vis)) { d->set_visible(vis); viewer->mark_dirty(); }
    float sz = d->point_size();
    if (ImGui::SliderFloat("Point Size##P", &sz, 1.0f, 20.0f)) {
        d->set_point_size(sz);
        viewer->mark_dirty();
    }
    const char* imps[] = {"Plain", "Sphere", "Surfel"};
    int imp = (int)d->impostor_type();
    if (ImGui::Combo("Impostor##P", &imp, imps, 3)) {
        d->set_impostor_type(static_cast<easy3d::PointsDrawable::ImposterType>(imp));
        viewer->mark_dirty();
    }
    render_coloring_controls(model, d, viewer);
}

void render_lines_section(easy3d::Model* model, ViewportCanvas* viewer) {
    auto* d = model_lines_drawable(model);
    if (!d) { ImGui::TextDisabled("(no lines drawable)"); return; }
    bool vis = d->is_visible();
    if (ImGui::Checkbox("Visible##L", &vis)) { d->set_visible(vis); viewer->mark_dirty(); }
    float w = d->line_width();
    if (ImGui::SliderFloat("Line Width##L", &w, 1.0f, 20.0f)) {
        d->set_line_width(w);
        viewer->mark_dirty();
    }
    render_coloring_controls(model, d, viewer);
}

void render_triangles_section(easy3d::Model* model, ViewportCanvas* viewer) {
    auto* d = model_triangles_drawable(model);
    if (!d) { ImGui::TextDisabled("(no triangles drawable)"); return; }
    bool vis = d->is_visible();
    if (ImGui::Checkbox("Visible##T", &vis)) { d->set_visible(vis); viewer->mark_dirty(); }
    float op = d->opacity();
    if (ImGui::SliderFloat("Opacity##T", &op, 0.0f, 1.0f)) {
        d->set_opacity(op);
        viewer->mark_dirty();
    }
    bool sm = d->smooth_shading();
    if (ImGui::Checkbox("Smooth Shading##T", &sm)) {
        d->set_smooth_shading(sm);
        viewer->mark_dirty();
    }
    render_coloring_controls(model, d, viewer);
}

} // anonymous namespace


// =============================================================================
// 4.3 Properties (General + Display)
// =============================================================================
void renderWidgetProperties(ViewportCanvas* viewer, PropertiesPanelState& s, bool& open) {
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(280, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Properties", &open)) {
        auto* model = viewer->current_model();
        if (!model) {
            ImGui::TextDisabled("(no model selected)");
            ImGui::End();
            return;
        }

        // -------- General --------
        ImGui::SetNextItemOpen(s.general_open, ImGuiCond_Always);
        bool gen_hdr = ImGui::CollapsingHeader("General");
        s.general_open = gen_hdr;
        if (gen_hdr) {
            ImGui::TextWrapped("Name: %s", model->name().c_str());
            {
                AIExplainableItem item;
                item.panel = AIExplainPanel::Properties;
                item.item_type = AIExplainItemType::PropertyRow;
                item.label = "Model name";
                item.value = model->name();
                ai_hover_tip("prop_name",
                    [item, model]() { return build_ai_explain_prompt(item, model); },
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    build_ai_explain_display_label(item));
            }

            const char* type_str = "Unknown";
            int nv = 0, ne = 0, nf = 0;
            if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
                type_str = "SurfaceMesh";
                nv = (int)sm->n_vertices();
                ne = (int)sm->n_edges();
                nf = (int)sm->n_faces();
            } else if (auto* pc = dynamic_cast<easy3d::PointCloud*>(model)) {
                type_str = "PointCloud";
                nv = (int)pc->n_vertices();
            } else if (auto* g = dynamic_cast<easy3d::Graph*>(model)) {
                type_str = "Graph";
                nv = (int)g->n_vertices();
                ne = (int)g->n_edges();
            }
            ImGui::Text("Type: %s", type_str);
            ImGui::Text("Vertices: %d", nv);
            if (ne > 0) ImGui::Text("Edges: %d", ne);
            if (nf > 0) ImGui::Text("Faces: %d", nf);
            // Combined AI for all counts (always visible on the last count line)
            {
                AIExplainableItem item;
                item.panel = AIExplainPanel::Properties;
                item.item_type = AIExplainItemType::PropertyRow;
                item.label = "Model size";
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%dV / %dE / %dF", nv, ne, nf);
                item.value = buf;
                const easy3d::Model* cm = model;
                ai_hover_tip("prop_counts",
                    [item, cm]() { return build_ai_explain_prompt(item, cm); },
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    build_ai_explain_display_label(item));
            }

            const easy3d::Box3& bbox = model->bounding_box();
            if (bbox.is_valid()) {
                easy3d::vec3 center = bbox.center();
                easy3d::vec3 size = bbox.max_point() - bbox.min_point();
                float diag = easy3d::length(size);
                ImGui::Text("BBox center: %.3f, %.3f, %.3f", center[0], center[1], center[2]);
                ImGui::Text("BBox size:   %.3f, %.3f, %.3f", size[0], size[1], size[2]);
                ImGui::Text("BBox diag:   %.3f", diag);
                AIExplainableItem item;
                item.panel = AIExplainPanel::Properties;
                item.item_type = AIExplainItemType::PropertyRow;
                item.label = "Bounding box";
                item.value = "diag " + std::to_string(diag);
                ai_hover_tip("prop_bbox",
                    [item, model]() { return build_ai_explain_prompt(item, model); },
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    build_ai_explain_display_label(item));
            } else {
                ImGui::TextDisabled("(no valid bbox)");
            }

            const auto transform_info = baked_transform_info(model);
            if (transform_info.available) {
                ImGui::SeparatorText("Transform");
                ImGui::Text("Baked operations: %d", transform_info.count);
                if (!transform_info.operation.empty())
                    ImGui::TextWrapped("Last operation: %s",
                                       transform_info.operation.c_str());
                if (ImGui::TreeNode("Last baked matrix")) {
                    render_matrix4(transform_info.last);
                    ImGui::TreePop();
                }
                if (ImGui::TreeNode("Cumulative baked matrix")) {
                    render_matrix4(transform_info.cumulative);
                    ImGui::TreePop();
                }
                ImGui::TextDisabled("Stored as model properties; geometry has already been baked.");
                AIExplainableItem item;
                item.panel = AIExplainPanel::Properties;
                item.item_type = AIExplainItemType::PropertyRow;
                item.label = "Baked transform";
                item.value = std::to_string(transform_info.count) + " operation(s)";
                if (!transform_info.operation.empty())
                    item.detail = "Last: " + transform_info.operation;
                ai_hover_tip("prop_xform",
                    [item, model]() { return build_ai_explain_prompt(item, model); },
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    build_ai_explain_display_label(item));
            }
        }

        // -------- Display --------
        ImGui::SetNextItemOpen(s.display_open, ImGuiCond_Always);
        bool disp_hdr = ImGui::CollapsingHeader("Display");
        s.display_open = disp_hdr;
        if (disp_hdr) {
            auto t = viewer->selected_drawable_type();
            if (t == SelectedDrawableType::Points) {
                ImGui::SeparatorText("Points");
                render_points_section(model, viewer);
            } else if (t == SelectedDrawableType::Lines) {
                ImGui::SeparatorText("Lines");
                render_lines_section(model, viewer);
            } else if (t == SelectedDrawableType::Triangles) {
                ImGui::SeparatorText("Triangles");
                render_triangles_section(model, viewer);
            } else {
                if (model_points_drawable(model)) {
                    if (ImGui::TreeNodeEx("Points##sec", ImGuiTreeNodeFlags_DefaultOpen)) {
                        render_points_section(model, viewer);
                        ImGui::TreePop();
                    }
                }
                if (model_lines_drawable(model)) {
                    if (ImGui::TreeNodeEx("Lines##sec")) {
                        render_lines_section(model, viewer);
                        ImGui::TreePop();
                    }
                }
                if (model_triangles_drawable(model)) {
                    if (ImGui::TreeNodeEx("Triangles##sec", ImGuiTreeNodeFlags_DefaultOpen)) {
                        render_triangles_section(model, viewer);
                        ImGui::TreePop();
                    }
                }
            }
        }

        // -------- Attributes / Scalar Fields --------
        ImGui::SetNextItemOpen(s.attributes_open, ImGuiCond_Always);
        bool attr_hdr = ImGui::CollapsingHeader("Attributes / Scalar Fields");
        s.attributes_open = attr_hdr;
        if (attr_hdr)
            render_attribute_manager(viewer, s);
    }
    ImGui::End();
}
