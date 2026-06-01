// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/attribute_manager.h"

#include "ai/ai_context.h"
#include "ai/ai_explainable_item.h"
#include "ai/ai_language.h"
#include "ai/ai_widget.h"
#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "widgets/imgui_widgets.h"
#include "widgets/model_utils.h"
#include "services/platform/platform_paths.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/state.h>
#include <easy3d/renderer/texture.h>
#include <easy3d/renderer/texture_manager.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"


namespace {

enum class AttrLocation {
    Vertex,
    Face,
    Halfedge,
    Model
};

enum class AttrKind {
    Scalar,
    Color,
    TexCoord,
    Texture
};

struct AttrInfo {
    std::string name;
    std::string label;
    AttrLocation location = AttrLocation::Vertex;
    AttrKind kind = AttrKind::Scalar;
    int count = 0;
    double min_value = 0.0;
    double max_value = 0.0;
    std::vector<float> histogram;
};

std::string attr_key(const AttrInfo& a) {
    const char loc = a.location == AttrLocation::Vertex ? 'v'
                   : a.location == AttrLocation::Face ? 'f'
                   : a.location == AttrLocation::Halfedge ? 'h' : 'm';
    const char kind = a.kind == AttrKind::Scalar ? 's'
                    : a.kind == AttrKind::Color ? 'c'
                    : a.kind == AttrKind::TexCoord ? 'u' : 't';
    return std::string(1, loc) + ":" + std::string(1, kind) + ":" + a.name;
}

const char* attr_location_name(AttrLocation loc) {
    switch (loc) {
        case AttrLocation::Vertex: return "Vertex";
        case AttrLocation::Face: return "Face";
        case AttrLocation::Halfedge: return "Halfedge";
        case AttrLocation::Model: return "Model";
    }
    return "?";
}

const char* attr_kind_name(AttrKind kind) {
    switch (kind) {
        case AttrKind::Scalar: return "Scalar";
        case AttrKind::Color: return "Color";
        case AttrKind::TexCoord: return "UV";
        case AttrKind::Texture: return "Texture";
    }
    return "?";
}

bool is_internal_property_name(const std::string& name) {
    return name == "v:connectivity" || name == "h:connectivity"
        || name == "f:connectivity" || name == "v:point"
        || name == "v:normal" || name == "f:normal"
        || name == "v:deleted" || name == "e:deleted"
        || name == "f:deleted";
}

template <typename Prop, typename Range>
void fill_scalar_stats(AttrInfo& info, Prop prop, const Range& range) {
    constexpr int kBins = 20;
    info.count = 0;
    info.min_value = std::numeric_limits<double>::max();
    info.max_value = -std::numeric_limits<double>::max();
    std::vector<double> values;
    for (auto h : range) {
        const double v = (double)prop[h];
        if (!std::isfinite(v))
            continue;
        values.push_back(v);
        info.min_value = std::min(info.min_value, v);
        info.max_value = std::max(info.max_value, v);
        ++info.count;
    }
    if (info.count == 0) {
        info.min_value = 0.0;
        info.max_value = 0.0;
        return;
    }
    info.histogram.assign(kBins, 0.0f);
    const double span = info.max_value - info.min_value;
    for (double v : values) {
        int bin = 0;
        if (span > 1e-30) {
            bin = std::min(kBins - 1,
                           std::max(0, (int)(((v - info.min_value) / span) *
                                             kBins)));
        }
        info.histogram[bin] += 1.0f;
    }
}

template <typename ModelT, typename Range>
bool try_vertex_scalar(ModelT* model,
                       const std::string& name,
                       const Range& range,
                       AttrInfo& info) {
    if (auto p = model->template get_vertex_property<float>(name)) {
        fill_scalar_stats(info, p, range);
        return true;
    }
    if (auto p = model->template get_vertex_property<double>(name)) {
        fill_scalar_stats(info, p, range);
        return true;
    }
    if (auto p = model->template get_vertex_property<int>(name)) {
        fill_scalar_stats(info, p, range);
        return true;
    }
    if (auto p = model->template get_vertex_property<unsigned int>(name)) {
        fill_scalar_stats(info, p, range);
        return true;
    }
    return false;
}

bool try_face_scalar(easy3d::SurfaceMesh* model,
                     const std::string& name,
                     AttrInfo& info) {
    if (auto p = model->get_face_property<float>(name)) {
        fill_scalar_stats(info, p, model->faces());
        return true;
    }
    if (auto p = model->get_face_property<double>(name)) {
        fill_scalar_stats(info, p, model->faces());
        return true;
    }
    if (auto p = model->get_face_property<int>(name)) {
        fill_scalar_stats(info, p, model->faces());
        return true;
    }
    if (auto p = model->get_face_property<unsigned int>(name)) {
        fill_scalar_stats(info, p, model->faces());
        return true;
    }
    return false;
}

std::vector<AttrInfo> collect_attributes(easy3d::Model* model) {
    std::vector<AttrInfo> attrs;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        for (const auto& name : mesh->vertex_properties()) {
            if (is_internal_property_name(name))
                continue;
            AttrInfo a;
            a.name = name;
            a.location = AttrLocation::Vertex;
            if (try_vertex_scalar(mesh, name, mesh->vertices(), a)) {
                a.kind = AttrKind::Scalar;
                a.label = "Vertex scalar";
                attrs.push_back(std::move(a));
            } else if (mesh->get_vertex_property<easy3d::vec3>(name)) {
                a.kind = AttrKind::Color;
                a.label = "Vertex color";
                a.count = (int)mesh->n_vertices();
                attrs.push_back(std::move(a));
            } else if (mesh->get_vertex_property<easy3d::vec2>(name)) {
                a.kind = AttrKind::TexCoord;
                a.label = "Vertex UV";
                a.count = (int)mesh->n_vertices();
                attrs.push_back(std::move(a));
            }
        }
        for (const auto& name : mesh->face_properties()) {
            if (is_internal_property_name(name))
                continue;
            AttrInfo a;
            a.name = name;
            a.location = AttrLocation::Face;
            if (try_face_scalar(mesh, name, a)) {
                a.kind = AttrKind::Scalar;
                a.label = "Face scalar";
                attrs.push_back(std::move(a));
            } else if (mesh->get_face_property<easy3d::vec3>(name)) {
                a.kind = AttrKind::Color;
                a.label = "Face color";
                a.count = (int)mesh->n_faces();
                attrs.push_back(std::move(a));
            }
        }
        for (const auto& name : mesh->halfedge_properties()) {
            if (is_internal_property_name(name))
                continue;
            if (mesh->get_halfedge_property<easy3d::vec2>(name)) {
                AttrInfo a;
                a.name = name;
                a.location = AttrLocation::Halfedge;
                a.kind = AttrKind::TexCoord;
                a.label = "Halfedge UV";
                a.count = (int)mesh->n_halfedges();
                attrs.push_back(std::move(a));
            }
        }
        auto texture_path =
            mesh->get_model_property<std::string>("m:texture_diffuse");
        auto texture_data =
            mesh->get_model_property<std::vector<unsigned char>>("m:texture_data");
        if ((texture_path && !texture_path[0].empty()) ||
            (texture_data && !texture_data[0].empty())) {
            AttrInfo a;
            a.name = texture_path && !texture_path[0].empty()
                ? texture_path[0] : "embedded texture";
            a.location = AttrLocation::Model;
            a.kind = AttrKind::Texture;
            a.label = "Texture";
            a.count = 1;
            attrs.push_back(std::move(a));
        }
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        for (const auto& name : cloud->vertex_properties()) {
            if (is_internal_property_name(name))
                continue;
            AttrInfo a;
            a.name = name;
            a.location = AttrLocation::Vertex;
            if (try_vertex_scalar(cloud, name, cloud->vertices(), a)) {
                a.kind = AttrKind::Scalar;
                a.label = "Point scalar";
                attrs.push_back(std::move(a));
            } else if (cloud->get_vertex_property<easy3d::vec3>(name)) {
                a.kind = AttrKind::Color;
                a.label = "Point color";
                a.count = (int)cloud->n_vertices();
                attrs.push_back(std::move(a));
            } else if (cloud->get_vertex_property<easy3d::vec2>(name)) {
                a.kind = AttrKind::TexCoord;
                a.label = "Point UV";
                a.count = (int)cloud->n_vertices();
                attrs.push_back(std::move(a));
            }
        }
    }
    return attrs;
}

easy3d::Texture* texture_for_model(easy3d::SurfaceMesh* mesh) {
    auto texture_path = mesh->get_model_property<std::string>("m:texture_diffuse");
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

void apply_attribute_display(ViewportCanvas* viewer,
                             easy3d::Model* model,
                             const AttrInfo& attr) {
    if (!viewer || !model)
        return;
    if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        auto* faces = model_triangles_drawable(mesh);
        if (!faces)
            return;
        if (attr.kind == AttrKind::Scalar) {
            faces->set_scalar_coloring(
                attr.location == AttrLocation::Face
                    ? easy3d::State::FACE : easy3d::State::VERTEX,
                attr.name, nullptr, 0.02f, 0.02f);
        } else if (attr.kind == AttrKind::Color) {
            faces->set_property_coloring(
                attr.location == AttrLocation::Face
                    ? easy3d::State::FACE : easy3d::State::VERTEX,
                attr.name);
        } else if (attr.kind == AttrKind::TexCoord) {
            faces->set_texture_coloring(
                attr.location == AttrLocation::Halfedge
                    ? easy3d::State::HALFEDGE : easy3d::State::VERTEX,
                attr.name, texture_for_model(mesh));
        } else if (attr.kind == AttrKind::Texture) {
            if (mesh->get_halfedge_property<easy3d::vec2>("h:texcoord"))
                faces->set_texture_coloring(
                    easy3d::State::HALFEDGE, "h:texcoord",
                    texture_for_model(mesh));
            else if (mesh->get_vertex_property<easy3d::vec2>("v:texcoord"))
                faces->set_texture_coloring(
                    easy3d::State::VERTEX, "v:texcoord",
                    texture_for_model(mesh));
        }
        faces->update();
        mesh->renderer()->update();
    } else if (auto* cloud = dynamic_cast<easy3d::PointCloud*>(model)) {
        auto* points = model_points_drawable(cloud);
        if (!points)
            return;
        if (attr.kind == AttrKind::Scalar)
            points->set_scalar_coloring(
                easy3d::State::VERTEX, attr.name, nullptr, 0.02f, 0.02f);
        else if (attr.kind == AttrKind::Color)
            points->set_property_coloring(easy3d::State::VERTEX, attr.name);
        else if (attr.kind == AttrKind::TexCoord)
            points->set_texture_coloring(easy3d::State::VERTEX, attr.name);
        points->update();
        cloud->renderer()->update();
    }
    viewer->mark_dirty();
}

} // namespace


void render_attribute_manager(ViewportCanvas* viewer,
                              PropertiesPanelState& s) {
    auto* model = viewer->current_model();
    auto attrs = collect_attributes(model);
    if (attrs.empty()) {
        ImGui::TextDisabled(
            "No user-visible scalar, color, UV, or texture attributes.");
        return;
    }

    const AttrInfo* active = nullptr;
    for (const auto& a : attrs) {
        if (attr_key(a) == s.active_attribute_key) {
            active = &a;
            break;
        }
    }

    if (ImGui::BeginTable("attr_table", 4,
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Scope");
        ImGui::TableSetupColumn("Count");
        ImGui::TableHeadersRow();
        for (const auto& a : attrs) {
            const std::string key = attr_key(a);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool attr_clicked = ImGui::Selectable(
                (a.name + "##" + key).c_str(), key == s.active_attribute_key,
                ImGuiSelectableFlags_SpanAllColumns |
                ImGuiSelectableFlags_AllowOverlap);
            bool attr_ai_clicked = false;
            {
                AIExplainableItem item;
                item.panel = AIExplainPanel::Properties;
                item.item_type = AIExplainItemType::ScalarAttribute;
                item.label = a.name;
                item.value = attr_kind_name(a.kind);
                item.detail = std::string(attr_location_name(a.location)) +
                    ", count=" + std::to_string(a.count);
                const easy3d::Model* mc = model;
                attr_ai_clicked = ai_hover_tip(("attr_" + key).c_str(),
                    [item, mc]() { return build_ai_explain_prompt(item, mc); },
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    build_ai_explain_display_label(item));
            }
            if (attr_clicked && !attr_ai_clicked) {
                s.active_attribute_key = key;
                if (a.kind == AttrKind::Scalar) {
                    s.filter_min = (float)a.min_value;
                    s.filter_max = (float)a.max_value;
                }
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", attr_kind_name(a.kind));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", attr_location_name(a.location));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", a.count);
        }
        ImGui::EndTable();
    }

    if (!active)
        return;

    ImGui::SeparatorText("Active Attribute");
    ImGui::TextWrapped("%s (%s, %s)", active->name.c_str(),
                       attr_kind_name(active->kind),
                       attr_location_name(active->location));
    if (ImGui::Button("Show Attribute"))
        apply_attribute_display(viewer, model, *active);
    claw_ui::same_line_if_fits_button("Ask AI About This");
    if (ImGui::Button("Ask AI About This")) {
        std::ostringstream extra;
        extra << "Active attribute\n"
              << "Name: " << active->name << "\n"
              << "Kind: " << attr_kind_name(active->kind) << "\n"
              << "Location: " << attr_location_name(active->location) << "\n"
              << "Count: " << active->count << "\n";
        if (active->kind == AttrKind::Scalar)
            extra << "Range: [" << active->min_value << ", "
                  << active->max_value << "]\n";
        MainWindow::instance()->send_ai_request(
            std::string("Explain the active scalar/color/UV attribute and "
                        "suggest how to use it for visualization, filtering, "
                        "or cleanup. ") + ai_lang::directive(),
            AICtx_All, extra.str(),
            std::string("Ask about active attribute: ") + active->name);
    }

    if (active->kind == AttrKind::Scalar) {
        ImGui::Text("Range: %.6g .. %.6g",
                    active->min_value, active->max_value);
        if (!active->histogram.empty()) {
            float max_bin = 0.0f;
            for (float v : active->histogram)
                max_bin = std::max(max_bin, v);
            ImGui::PlotHistogram("Histogram", active->histogram.data(),
                                 (int)active->histogram.size(), 0, nullptr,
                                 0.0f, std::max(1.0f, max_bin),
                                 ImVec2(0, 70));
        }

        ImGui::SeparatorText("Filter By Value");
        ImGui::Checkbox("Clear previous selection", &s.filter_clear_first);
        ImGui::DragFloatRange2(
            "Range", &s.filter_min, &s.filter_max,
            (float)((active->max_value - active->min_value) / 200.0 + 1e-6),
            (float)active->min_value, (float)active->max_value,
            "%.6g", "%.6g");
        if (ImGui::Button("Select Range")) {
            const bool face_prop = active->location == AttrLocation::Face;
            int n = MainWindow::instance()->select_scalar_range(
                active->name, face_prop, s.filter_min, s.filter_max,
                s.filter_clear_first);
            LOG(INFO) << "attribute range selected " << n << " element(s)";
        }
        claw_ui::same_line_if_fits_button("Extract Selection");
        if (ImGui::Button("Extract Selection"))
            MainWindow::instance()->extract_selection();
        claw_ui::same_line_if_fits_button("Delete Selection...");
        if (ImGui::Button("Delete Selection..."))
            MainWindow::instance()->request_delete_selection_confirmation();
    } else if (active->kind == AttrKind::Texture) {
        auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
        if (!mesh) {
            ImGui::TextDisabled("(texture only supported on SurfaceMesh)");
        } else {
            auto path_prop =
                mesh->get_model_property<std::string>("m:texture_diffuse");
            auto data_prop =
                mesh->get_model_property<std::vector<unsigned char>>(
                    "m:texture_data");
            auto w_prop = mesh->get_model_property<int>("m:texture_width");
            auto h_prop = mesh->get_model_property<int>("m:texture_height");

            const bool has_path = path_prop && !path_prop[0].empty();
            const bool has_data = data_prop && !data_prop[0].empty()
                && w_prop && h_prop && w_prop[0] > 0 && h_prop[0] > 0;

            if (has_path) {
                ImGui::TextWrapped("Source: file");
                ImGui::TextWrapped("Path: %s", path_prop[0].c_str());
                bool exists = easy3d::file_system::is_file(path_prop[0]);
                if (exists) {
                    ImGui::TextColored(claw_ui::status_success_color(),
                                       "File: exists on disk");
                } else {
                    ImGui::TextColored(claw_ui::status_error_color(),
                        "File: MISSING (was the file moved or the model exported standalone?)");
                }
            } else if (has_data) {
                ImGui::TextWrapped("Source: embedded in model file (%d x %d)",
                                   w_prop[0], h_prop[0]);
            } else {
                ImGui::TextColored(claw_ui::status_warning_color(),
                    "No texture path or embedded pixel data found.");
            }

            auto* tex = texture_for_model(mesh);
            if (tex) {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "Loaded: %d x %d, %d channels",
                    tex->width(), tex->height(), tex->channels());
                double bytes =
                    double(tex->width()) * tex->height() * tex->channels();
                ImGui::Text("Approx GPU memory: ~%.2f MB",
                            bytes / (1024.0 * 1024.0));
            } else {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "Loaded: NO -- TextureManager::request returned null");
            }

            bool has_h_uv =
                (bool)mesh->get_halfedge_property<easy3d::vec2>("h:texcoord");
            bool has_v_uv =
                (bool)mesh->get_vertex_property<easy3d::vec2>("v:texcoord");
            if (has_h_uv || has_v_uv) {
                ImGui::TextColored(claw_ui::status_success_color(),
                    "UV: %s coords present", has_h_uv ? "halfedge" : "vertex");
            } else {
                ImGui::TextColored(claw_ui::status_error_color(),
                    "UV: missing -- texture cannot be sampled without UV coordinates");
            }

            auto* faces = mesh->renderer()->get_triangles_drawable("faces");
            if (faces) {
                const char* method_name = "?";
                switch (faces->coloring_method()) {
                    case easy3d::State::UNIFORM_COLOR:
                        method_name = "uniform";
                        break;
                    case easy3d::State::COLOR_PROPERTY:
                        method_name = "color_property";
                        break;
                    case easy3d::State::SCALAR_FIELD:
                        method_name = "scalar_field";
                        break;
                    case easy3d::State::TEXTURED:
                        method_name = "texture";
                        break;
                }
                if (faces->coloring_method() == easy3d::State::TEXTURED) {
                    ImGui::TextColored(claw_ui::status_success_color(),
                        "Active coloring: texture");
                } else {
                    ImGui::TextColored(claw_ui::status_warning_color(),
                        "Active coloring: %s (texture not applied -- click Apply Texture)",
                        method_name);
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Apply Texture"))
                apply_attribute_display(viewer, model, *active);
            claw_ui::same_line_if_fits_button("Reload from Disk");
            if (ImGui::Button("Reload from Disk")) {
                if (tex)
                    easy3d::TextureManager::release(tex);
                apply_attribute_display(viewer, model, *active);
            }
            if (has_path) {
                claw_ui::same_line_if_fits_button("Open Folder");
                if (ImGui::Button("Open Folder")) {
                    std::string dir =
                        easy3d::file_system::parent_directory(path_prop[0]);
                    if (!dir.empty() && !claw3d::platform::open_directory(dir))
                        LOG(WARNING) << "Failed to open texture folder: " << dir;
                }
            }
        }
    }
}
