// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "viewport/viewport_canvas.h"

#include "io/surface_mesh_io.h"
#include <model_health.h>
#include "selection/selection_manager.h"
#include "viewport/scene_lighting.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/fileio/graph_io.h>
#include <easy3d/fileio/ply_reader_writer.h>
#include <easy3d/fileio/point_cloud_io.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/file_system.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace {

bool has_authored_surface_appearance(easy3d::SurfaceMesh* mesh) {
    return mesh &&
        (mesh->get_vertex_property<easy3d::vec3>("v:color") ||
         mesh->get_face_property<easy3d::vec3>("f:color") ||
         mesh->get_vertex_property<easy3d::vec2>("v:texcoord") ||
         mesh->get_halfedge_property<easy3d::vec2>("h:texcoord"));
}

void apply_default_surface_mesh_style(easy3d::Model* model) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh || !mesh->renderer())
        return;

    if (auto* faces = mesh->renderer()->get_triangles_drawable("faces", false)) {
        if (!has_authored_surface_appearance(mesh)) {
            faces->set_uniform_coloring(
                easy3d::vec4(0.62f, 0.70f, 0.78f, 1.0f));
        }
        faces->set_opacity(1.0f);
        faces->set_lighting(claw3d::scene_lighting_enabled());
        faces->set_lighting_two_sides(true);
        faces->set_distinct_back_color(false);
        faces->set_visible(true);
        faces->update();
    }

    if (auto* edges = mesh->renderer()->get_lines_drawable("edges", false)) {
        edges->set_uniform_coloring(easy3d::vec4(0.13f, 0.16f, 0.21f, 1.0f));
        edges->set_line_width(1.0f);
        edges->set_visible(true);
        edges->update();
    }

    if (auto* points = mesh->renderer()->get_points_drawable("vertices", false)) {
        points->set_visible(false);
        points->update();
    }

    mesh->renderer()->update();
}

} // namespace

easy3d::Model* ViewportCanvas::load_model_file(const std::string& file_name) {
    easy3d::Model* model = nullptr;

    std::string ext = easy3d::file_system::extension(file_name);
    bool is_ply_mesh = false, is_ply_graph = false;
    if (ext == "ply") {
        is_ply_mesh =
            (easy3d::io::PlyReader::num_instances(file_name, "face") > 0);
        is_ply_graph = !is_ply_mesh &&
            (easy3d::io::PlyReader::num_instances(file_name, "edge") > 0);
    }

    if ((ext == "ply" && is_ply_mesh) || ext == "obj" || ext == "stl"
        || ext == "off" || ext == "glb" || ext == "gltf") {
        model = claw3d::io::load_surface_mesh(file_name);
    } else if (ext == "ply" && is_ply_graph) {
        model = easy3d::GraphIO::load(file_name);
    } else if (ext == "xyz" || ext == "bin"
               || (ext == "ply" && !is_ply_mesh && !is_ply_graph)) {
        model = easy3d::PointCloudIO::load(file_name);
    } else {
        model = claw3d::io::load_surface_mesh(file_name);
        if (!model)
            model = easy3d::PointCloudIO::load(file_name);
        if (!model)
            model = easy3d::GraphIO::load(file_name);
    }

    if (model)
        model->set_name(easy3d::file_system::base_name(file_name));
    return model;
}

easy3d::Model* ViewportCanvas::add_model(const std::string& file_name) {
    auto* model = load_model_file(file_name);
    if (model) {
        auto* added = add_model(model);
        std::string workspace =
            unique_workspace_name(easy3d::file_system::base_name(file_name), added);
        register_model_tree_node(added, ModelTreeNodeInfo{
            workspace, model->name(), nullptr, ModelTreeNodeKind::Data, true});
        fit_screen(model);
        dirty_ = true;
        return added;
    }
    return nullptr;
}

easy3d::Model* ViewportCanvas::add_model(easy3d::Model* model) {
    if (!model)
        return nullptr;
    for (std::size_t i = 0; i < models_.size(); ++i) {
        if (models_[i].get() == model) {
            model_idx_ = static_cast<int>(i);
            return model;
        }
    }
    model->set_renderer(std::make_shared<easy3d::Renderer>(model, true));
    apply_default_surface_mesh_style(model);
    models_.push_back(std::shared_ptr<easy3d::Model>(model));
    assign_model_handle(model);
    model_idx_ = static_cast<int>(models_.size()) - 1;
    if (model_tree_registry_.find(model) == model_tree_registry_.end()) {
        std::string workspace = model->name();
        auto dot = workspace.find_last_of('.');
        if (dot != std::string::npos)
            workspace = workspace.substr(0, dot);
        if (workspace.empty())
            workspace = "Default";
        workspace = unique_workspace_name(workspace, model);
        register_model_tree_node(model, ModelTreeNodeInfo{
            workspace, model->name(), nullptr, ModelTreeNodeKind::Data, true});
    }
    return model;
}

bool ViewportCanvas::delete_model(easy3d::Model* model) {
    std::vector<easy3d::Model*> children;
    for (auto& entry : model_tree_registry_) {
        if (entry.second.parent == model)
            children.push_back(entry.first);
    }
    for (auto* child : children)
        delete_model(child);

    unregister_model_tree_node(model);
    ModelHealthRegistry::instance().invalidate(model);
    if (selection_manager_)
        selection_manager_->remove_model(model);
    for (auto it = models_.begin(); it != models_.end(); ++it) {
        if (it->get() == model) {
            if (selection_bbox_last_model_ == model) {
                selection_bbox_last_model_ = nullptr;
                delete selection_bbox_drawable_;
                selection_bbox_drawable_ = nullptr;
            }
            unregister_model_handle(model);
            models_.erase(it);
            if (model_idx_ >= static_cast<int>(models_.size()))
                model_idx_ = static_cast<int>(models_.size()) - 1;
            dirty_ = true;
            return true;
        }
    }
    return false;
}

ModelHandle ViewportCanvas::assign_model_handle(easy3d::Model* model)
{
    if (!model)
        return {};
    auto existing = model_handles_.find(model);
    if (existing != model_handles_.end())
        return existing->second;

    ModelHandle handle;
    handle.id = next_model_handle_id_++;
    handle.generation = 1;
    model_handles_[model] = handle;
    model_handle_records_[handle.id] = ModelHandleRecord{model, handle.generation};
    return handle;
}

void ViewportCanvas::unregister_model_handle(easy3d::Model* model)
{
    auto it = model_handles_.find(model);
    if (it == model_handles_.end())
        return;
    model_handle_records_.erase(it->second.id);
    model_handles_.erase(it);
}

ModelHandle ViewportCanvas::model_handle(easy3d::Model* model) const
{
    auto it = model_handles_.find(model);
    return it == model_handles_.end() ? ModelHandle{} : it->second;
}

easy3d::Model* ViewportCanvas::resolve_model(ModelHandle handle) const
{
    if (!handle.valid())
        return nullptr;
    auto it = model_handle_records_.find(handle.id);
    if (it == model_handle_records_.end())
        return nullptr;
    if (it->second.generation != handle.generation)
        return nullptr;
    easy3d::Model* model = it->second.model;
    for (const auto& current : models_) {
        if (current.get() == model)
            return model;
    }
    return nullptr;
}

void ViewportCanvas::register_model_tree_node(
    easy3d::Model* model, const ModelTreeNodeInfo& info)
{
    if (!model)
        return;
    ModelTreeNodeInfo fixed = info;
    if (fixed.workspace_name.empty())
        fixed.workspace_name = "Default";
    if (fixed.display_name.empty())
        fixed.display_name = model->name();
    model_tree_registry_[model] = fixed;
    rebuild_workspace_set();
}

void ViewportCanvas::register_model_tree_overlay(
    easy3d::Model* model, easy3d::Model* source, const std::string& display_name)
{
    if (!model)
        return;
    std::string workspace = model_tree_workspace_name(source);
    if (workspace.empty())
        workspace = "Process Overlays";
    register_model_tree_node(model, ModelTreeNodeInfo{
        workspace,
        display_name.empty() ? model->name() : display_name,
        source,
        ModelTreeNodeKind::Overlay,
        false});
}

void ViewportCanvas::unregister_model_tree_node(easy3d::Model* model)
{
    model_tree_registry_.erase(model);
    rebuild_workspace_set();
}

const ModelTreeNodeInfo* ViewportCanvas::model_tree_info(
    easy3d::Model* model) const
{
    auto it = model_tree_registry_.find(model);
    return (it != model_tree_registry_.end()) ? &it->second : nullptr;
}

std::string ViewportCanvas::model_tree_workspace_name(easy3d::Model* model) const
{
    auto* info = model_tree_info(model);
    return info ? info->workspace_name : std::string();
}

std::vector<std::string> ViewportCanvas::workspace_names() const
{
    std::vector<std::string> out(workspace_set_.begin(), workspace_set_.end());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<easy3d::Model*> ViewportCanvas::workspace_models(
    const std::string& workspace_name) const
{
    std::vector<easy3d::Model*> out;
    for (const auto& entry : model_tree_registry_) {
        if (entry.second.workspace_name == workspace_name
            && entry.second.visible_in_tree) {
            out.push_back(entry.first);
        }
    }
    return out;
}

easy3d::Model* ViewportCanvas::current_model() {
    if (models_.empty() || model_idx_ < 0
        || model_idx_ >= static_cast<int>(models_.size()))
        return nullptr;
    return models_[model_idx_].get();
}

void ViewportCanvas::set_current_model(easy3d::Model* model) {
    for (int i = 0; i < static_cast<int>(models_.size()); ++i) {
        if (models_[i].get() == model) {
            if (model_idx_ != i) {
                selected_drawable_type_ = SelectedDrawableType::None;
                dirty_ = true;
            }
            model_idx_ = i;
            selection_active_ = true;
            return;
        }
    }
}

void ViewportCanvas::set_current_model_silent(easy3d::Model* model) {
    for (int i = 0; i < static_cast<int>(models_.size()); ++i) {
        if (models_[i].get() == model) {
            model_idx_ = i;
            return;
        }
    }
}

void ViewportCanvas::clear_scene() {
    models_.clear();
    model_handles_.clear();
    model_handle_records_.clear();
    model_tree_registry_.clear();
    workspace_set_.clear();
    model_idx_ = -1;
    selected_drawable_type_ = SelectedDrawableType::None;
    selection_bbox_last_model_ = nullptr;
    delete selection_bbox_drawable_;
    selection_bbox_drawable_ = nullptr;
    ModelHealthRegistry::instance().clear();
    if (selection_manager_)
        selection_manager_->clear_all();
    dirty_ = true;
}

std::string ViewportCanvas::unique_workspace_name(
    const std::string& desired, easy3d::Model* ignore) const
{
    const std::string base = desired.empty() ? "Default" : desired;
    std::string candidate = base;
    int suffix = 2;
    while (true) {
        bool used = false;
        for (const auto& entry : model_tree_registry_) {
            if (entry.first != ignore && entry.second.workspace_name == candidate) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
        candidate = base + " " + std::to_string(suffix++);
    }
}

void ViewportCanvas::rebuild_workspace_set()
{
    workspace_set_.clear();
    for (const auto& entry : model_tree_registry_) {
        if (!entry.second.workspace_name.empty())
            workspace_set_.insert(entry.second.workspace_name);
    }
}
