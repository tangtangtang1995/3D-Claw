// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/main_window.h"

#include "window/window_helpers.h"
#include "model/operation_history.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/util/logging.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct AlgorithmBatchKind {
    bool ransac = false;
    bool region_growing = false;
    bool primitive = false;
    bool simplification = false;
    bool acvd = false;
    bool vsa = false;
    bool ppr = false;
    bool smoothing = false;
    bool mcf_skeleton = false;
    bool legacy_in_place_surface = false;
    bool legacy_in_place_point_cloud = false;
};

AlgorithmBatchKind classify_batch(AlgorithmId id) {
    AlgorithmBatchKind kind;
    switch (id) {
    case AlgorithmId::RansacPrimitive:
        kind.ransac = true;
        kind.primitive = true;
        break;
    case AlgorithmId::RegionGrowing:
        kind.region_growing = true;
        kind.primitive = true;
        break;
    case AlgorithmId::CgalSimplification:
        kind.simplification = true;
        break;
    case AlgorithmId::AcvdRemeshing:
        kind.acvd = true;
        break;
    case AlgorithmId::VsaApproximation:
        kind.vsa = true;
        break;
    case AlgorithmId::PlanarPatchRemeshing:
        kind.ppr = true;
        break;
    case AlgorithmId::CgalSmoothing:
        kind.smoothing = true;
        break;
    case AlgorithmId::MeanCurvatureFlowSkeleton:
        kind.mcf_skeleton = true;
        break;
    case AlgorithmId::SurfaceMeshSimplification:
    case AlgorithmId::SurfaceMeshSmoothing:
    case AlgorithmId::SurfaceMeshFairing:
    case AlgorithmId::SurfaceMeshHoleFilling:
    case AlgorithmId::SurfaceMeshRemeshing:
        kind.legacy_in_place_surface = true;
        break;
    case AlgorithmId::PointCloudNormalEstimation:
        kind.legacy_in_place_point_cloud = true;
        break;
    default:
        break;
    }
    return kind;
}

bool is_overlay_result(const AlgorithmBatchKind& kind,
                       ResultDisposition disposition,
                       bool has_face_color) {
    return has_face_color || disposition == ResultDisposition::AddPrimitiveChildren
        || kind.primitive || kind.simplification || kind.acvd
        || kind.vsa || kind.ppr || kind.smoothing || kind.mcf_skeleton;
}

bool is_final_mesh_batch(const AlgorithmBatchKind& kind,
                         ResultDisposition disposition) {
    return disposition == ResultDisposition::HideSourceAndAddChild
        || kind.simplification || kind.acvd || kind.vsa || kind.ppr
        || kind.smoothing;
}

std::vector<easy3d::Model*> collect_result_ptrs(
        const std::vector<std::unique_ptr<easy3d::Model>>& results) {
    std::vector<easy3d::Model*> out;
    for (const auto& m : results) {
        if (m)
            out.push_back(m.get());
    }
    return out;
}

std::vector<std::string> collect_result_names(
        const std::vector<std::unique_ptr<easy3d::Model>>& results) {
    std::vector<std::string> names;
    for (const auto& m : results) {
        if (m)
            names.push_back(m->name());
    }
    return names;
}

const char* history_detail_for_disposition(ResultDisposition disposition) {
    switch (disposition) {
    case ResultDisposition::AddAsChild:
        return "Added output model";
    case ResultDisposition::ReplaceSource:
        return "Updated source model";
    case ResultDisposition::HideSourceAndAddChild:
        return "Added output model and hid source model";
    case ResultDisposition::AddPrimitiveChildren:
        return "Added primitive child models";
    }
    return "Applied output";
}

bool has_face_color_property(easy3d::Model* model) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    return mesh && mesh->get_face_property<easy3d::vec3>("f:color");
}

bool has_name_suffix(const std::string& name, const char* suffix) {
    const std::size_t suffix_len = std::char_traits<char>::length(suffix);
    return name.size() >= suffix_len &&
        name.compare(name.size() - suffix_len, suffix_len, suffix) == 0;
}

bool is_primitive_ch_name(const std::string& name) {
    return has_name_suffix(name, ".ch") || has_name_suffix(name, "_ch");
}

bool is_primitive_as_name(const std::string& name) {
    return has_name_suffix(name, ".as") || has_name_suffix(name, "_as");
}

bool is_primitive_child_name(const std::string& name) {
    return is_primitive_ch_name(name) || is_primitive_as_name(name);
}

bool model_in_scene(const ViewportCanvas& viewer, easy3d::Model* model) {
    if (!model)
        return false;
    const auto& models = viewer.models();
    return std::any_of(models.begin(), models.end(),
        [model](const std::shared_ptr<easy3d::Model>& candidate) {
            return candidate.get() == model;
        });
}

easy3d::Model* primitive_parent_for_result(
        const std::vector<easy3d::Model*>& batch,
        easy3d::Model* source_model,
        const std::string& node_name) {
    for (auto& suffix : {".ch", ".as", "_ch", "_as"}) {
        if (has_name_suffix(node_name, suffix)) {
            const auto pos = node_name.size() -
                std::char_traits<char>::length(suffix);
            std::string parent_name = node_name.substr(0, pos);
            for (auto* other : batch) {
                if (other && other->name() == parent_name)
                    return other;
            }
            break;
        }
    }
    return source_model;
}

int primitive_shape_id(const std::string& node_name) {
    int shape_id = parse_ransac_shape_id(node_name);
    if (shape_id >= 0 || node_name.rfind("rg_region_", 0) != 0)
        return shape_id;

    std::string base = node_name;
    for (auto& suffix : {".ch", ".as", "_ch", "_as"}) {
        if (has_name_suffix(base, suffix)) {
            const auto pos = base.size() -
                std::char_traits<char>::length(suffix);
            base = base.substr(0, pos);
        }
    }
    auto pos = base.rfind('_');
    if (pos != std::string::npos)
        shape_id = std::atoi(base.c_str() + pos + 1);
    return shape_id;
}

void apply_primitive_visibility(easy3d::Model* model,
                                const std::string& node_name,
                                const AlgorithmBatchKind& kind) {
    if (!kind.primitive)
        return;

    const bool name_is_ch = is_primitive_ch_name(node_name);
    const bool name_is_as = is_primitive_as_name(node_name);
    const bool is_plane_patch = !is_primitive_child_name(node_name);
    if (is_plane_patch || name_is_ch || (kind.region_growing && name_is_as))
        model->renderer()->set_visible(false);
}

void style_primitive_node(easy3d::Model* model, const std::string& node_name) {
    const bool is_ch = is_primitive_ch_name(node_name);
    const bool is_as = is_primitive_as_name(node_name);
    const int shape_id = primitive_shape_id(node_name);
    const easy3d::vec3 shape_color = (shape_id >= 0)
        ? ransac_shape_color(shape_id)
        : easy3d::vec3(0.5f, 0.5f, 0.5f);
    const easy3d::vec4 color(shape_color.x, shape_color.y, shape_color.z, 1.0f);

    if (auto* graph = dynamic_cast<easy3d::Graph*>(model)) {
        auto* lines = graph->renderer()->get_lines_drawable("edges", false);
        if (lines) {
            lines->set_uniform_coloring(color);
            lines->set_line_width(1.0f);
        }
        auto* points = graph->renderer()->get_points_drawable("vertices", false);
        if (points) {
            points->set_uniform_coloring(color);
            points->set_point_size(3.0f);
        }
    } else if (auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        if (!is_ch && !is_as)
            return;
        auto* faces = mesh->renderer()->get_triangles_drawable("faces", false);
        if (faces) {
            faces->set_uniform_coloring(color);
            faces->set_opacity(1.0f);
            faces->set_lighting(false);
            faces->set_distinct_back_color(false);
        }
        auto* edges = mesh->renderer()->get_lines_drawable("edges", false);
        if (edges) {
            edges->set_uniform_coloring(color);
            edges->set_line_width(1.0f);
        }
        auto* points = mesh->renderer()->get_points_drawable("vertices", false);
        if (points) {
            points->set_uniform_coloring(color);
            points->set_point_size(3.0f);
        }
    }
}

std::string register_result_node(ViewportCanvas& viewer,
                                 easy3d::Model* source_model,
                                 easy3d::Model* result,
                                 const std::vector<easy3d::Model*>& batch,
                                 const AlgorithmBatchKind& kind) {
    auto* source_info = viewer.model_tree_info(source_model);
    auto* result_info = viewer.model_tree_info(result);
    std::string workspace = source_info ? source_info->workspace_name
        : result_info ? result_info->workspace_name : "Default";
    ModelTreeNodeKind node_kind = kind.primitive
        ? ModelTreeNodeKind::Primitive
        : ModelTreeNodeKind::Reconstruction;

    std::string node_name = result->name();
    easy3d::Model* node_parent = source_model;
    if (kind.primitive) {
        node_parent = primitive_parent_for_result(batch, source_model, node_name);
        if (!node_parent)
            node_parent = source_model;
    }

    viewer.register_model_tree_node(result, ModelTreeNodeInfo{
        workspace, node_name, node_parent, node_kind, true});
    apply_primitive_visibility(result, node_name, kind);
    style_primitive_node(result, node_name);
    return node_name;
}

void style_face_colored_mesh(easy3d::Model* model) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh)
        return;

    auto* faces = mesh->renderer()->get_triangles_drawable("faces", false);
    if (!faces)
        return;
    faces->set_property_coloring(easy3d::State::FACE, "f:color");
    faces->set_opacity(1.0f);
    faces->set_lighting(false);
    faces->set_distinct_back_color(false);
    faces->update();
}

void style_mcf_skeleton(easy3d::Model* model) {
    auto* graph = dynamic_cast<easy3d::Graph*>(model);
    if (!graph)
        return;

    const easy3d::vec4 purple(0.78f, 0.20f, 0.98f, 1.0f);
    if (auto* lines = graph->renderer()->get_lines_drawable("edges", false)) {
        lines->set_uniform_coloring(purple);
        lines->set_impostor_type(easy3d::LinesDrawable::CYLINDER);
        lines->set_line_width(3.0f);
        lines->set_visible(true);
        lines->update();
    }
    if (auto* points = graph->renderer()->get_points_drawable("vertices", false)) {
        points->set_uniform_coloring(purple);
        points->set_point_size(8.0f);
        points->set_impostor_type(easy3d::PointsDrawable::SPHERE);
        points->set_visible(true);
        points->update();
    }
    graph->renderer()->update();
}

void style_final_mesh(easy3d::Model* model) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh)
        return;

    if (auto* faces = mesh->renderer()->get_triangles_drawable("faces", false)) {
        faces->set_opacity(1.0f);
        faces->update();
    }
    if (auto* edges = mesh->renderer()->get_lines_drawable("edges", false)) {
        edges->set_visible(true);
        edges->set_line_width(1.0f);
        edges->update();
    }
}

bool should_hide_source(easy3d::Model* source_model,
                        const std::vector<easy3d::Model*>& results,
                        const AlgorithmBatchKind& kind,
                        ResultDisposition disposition) {
    bool hide_source =
        disposition == ResultDisposition::HideSourceAndAddChild
        && source_model && !results.empty();
    if (!hide_source || !kind.vsa)
        return hide_source;

    bool any_manifold = false;
    for (const auto* result : results) {
        if (!result)
            continue;
        const std::string& name = result->name();
        if (name.size() < 3 || name.substr(name.size() - 3) != "_nm") {
            any_manifold = true;
            break;
        }
    }
    return any_manifold;
}

bool apply_legacy_in_place_result(easy3d::Model* source_model,
                                  easy3d::Model* result,
                                  const AlgorithmBatchKind& kind) {
    if (!source_model || !result)
        return false;

    if (kind.legacy_in_place_surface) {
        auto* source_mesh = dynamic_cast<easy3d::SurfaceMesh*>(source_model);
        auto* result_mesh = dynamic_cast<easy3d::SurfaceMesh*>(result);
        if (!source_mesh || !result_mesh)
            return false;
        *source_mesh = *result_mesh;
        apply_surface_mesh_texture(source_mesh);
        if (source_mesh->renderer())
            source_mesh->renderer()->update();
        return true;
    }

    if (kind.legacy_in_place_point_cloud) {
        auto* source_cloud = dynamic_cast<easy3d::PointCloud*>(source_model);
        auto* result_cloud = dynamic_cast<easy3d::PointCloud*>(result);
        if (!source_cloud || !result_cloud)
            return false;
        *source_cloud = *result_cloud;
        if (source_cloud->renderer())
            source_cloud->renderer()->update();
        return true;
    }

    return false;
}

} // namespace

void MainWindow::process_algorithm_completion() {
    AlgorithmController::CompletionBatch batch;
    if (!algorithm_controller().try_consume_completion(batch))
        return;

    easy3d::Model* source_model = batch.source_handle.valid()
        ? viewer_.resolve_model(batch.source_handle)
        : nullptr;
    easy3d::Model* current_before = viewer_.current_model();
    const AlgorithmBatchKind kind = classify_batch(batch.id);
    const auto planned_output_names = collect_result_names(batch.results);

    auto finish_history = [&](OperationStatus status, const std::string& detail) {
        if (history_pending_id_ < 0)
            return;
        std::vector<std::string> outputs = planned_output_names;
        if (outputs.empty() && source_model)
            outputs.push_back(source_model->name());
        OperationHistory::instance().finish_operation(
            history_pending_id_, outputs, status, detail);
        history_pending_id_ = -1;
    };

    if (batch.disposition == ResultDisposition::ReplaceSource) {
        bool applied_any = false;
        for (auto& result : batch.results) {
            applied_any =
                apply_legacy_in_place_result(source_model, result.get(), kind)
                || applied_any;
        }
        if (applied_any && source_model) {
            if (source_model->renderer())
                source_model->renderer()->update();
            viewer_.mark_dirty();
            finish_history(OperationStatus::Success,
                           history_detail_for_disposition(batch.disposition));
            return;
        }
        LOG(WARNING) << "Legacy in-place algorithm '" << batch.label
                     << "' could not update its source model; adding result "
                        "models to the scene instead";
    }

    const auto result_ptrs = collect_result_ptrs(batch.results);
    std::size_t added_count = 0;

    for (auto& result_holder : batch.results) {
        auto* result = result_holder.get();
        if (!result)
            continue;
        bool has_face_color = has_face_color_property(result);
        bool color_overlay =
            is_overlay_result(kind, batch.disposition, has_face_color);

        viewer_.add_model(result_holder.release());
        ++added_count;
        if (apply_surface_mesh_texture(result)) {
            has_face_color = false;
            color_overlay =
                is_overlay_result(kind, batch.disposition, has_face_color);
        }

        register_result_node(viewer_, source_model, result, result_ptrs, kind);

        if (!color_overlay)
            viewer_.fit_screen(result);
        if (has_face_color)
            style_face_colored_mesh(result);
        if (kind.mcf_skeleton)
            style_mcf_skeleton(result);
        if (is_final_mesh_batch(kind, batch.disposition))
            style_final_mesh(result);
    }

    if (should_hide_source(source_model, result_ptrs, kind, batch.disposition))
        source_model->renderer()->set_visible(false);
    if (kind.primitive) {
        if (model_in_scene(viewer_, current_before))
            viewer_.set_current_model_silent(current_before);
        else if (source_model)
            viewer_.set_current_model_silent(source_model);
    }

    if (auto* model = viewer_.current_model())
        model->renderer()->update();
    viewer_.mark_dirty();

    if (batch.id == AlgorithmId::AlphaWrap3D && st_alpha_wrap_.live_clear_on_finish)
        reset_aw3_process_overlay();

    if (added_count > 0) {
        const bool replace_fallback =
            batch.disposition == ResultDisposition::ReplaceSource;
        finish_history(
            OperationStatus::Success,
            replace_fallback
                ? "Source update failed; added output models instead"
                : history_detail_for_disposition(batch.disposition));
    } else {
        finish_history(OperationStatus::Skipped, "No output model was produced");
    }
}

void MainWindow::record_algorithm_start_if_needed() {
    const bool algo_busy = algorithm_controller().is_running();
    if (!history_prev_busy_ && algo_busy) {
        easy3d::Model* source = resolve_current_algorithm_source(this);
        std::string src_name = source ? source->name() : "";
        history_pending_id_ = OperationHistory::instance().start_operation(
            algorithm_controller().current_label(), src_name);
    }
    history_prev_busy_ = algo_busy;
}
