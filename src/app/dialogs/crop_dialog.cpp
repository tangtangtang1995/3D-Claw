// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/crop_dialog.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <easy3d/core/surface_mesh.h>
#include <easy3d/core/point_cloud.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/texture_manager.h>
#include <easy3d/util/logging.h>

#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "overlays/overlay_controller.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "ai/ai_context.h"
#include "ui/layout_helpers.h"


void CropState::box_min(float out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = box_center[i] - box_half[i];
}
void CropState::box_max(float out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = box_center[i] + box_half[i];
}
void CropState::box_basis(easy3d::vec3 out[3]) const {
    const float d2r = 3.14159265358979323846f / 180.0f;
    const float rx = box_rotation_deg[0] * d2r;
    const float ry = box_rotation_deg[1] * d2r;
    const float rz = box_rotation_deg[2] * d2r;
    const float sx = std::sin(rx), cx = std::cos(rx);
    const float sy = std::sin(ry), cy = std::cos(ry);
    const float sz = std::sin(rz), cz = std::cos(rz);

    // R = Rz * Ry * Rx. Columns are the rotated local X/Y/Z axes.
    out[0] = easy3d::vec3(cz * cy, sz * cy, -sy);
    out[1] = easy3d::vec3(cz * sy * sx - sz * cx,
                          sz * sy * sx + cz * cx,
                          cy * sx);
    out[2] = easy3d::vec3(cz * sy * cx + sz * sx,
                          sz * sy * cx - cz * sx,
                          cy * cx);
}
easy3d::vec3 CropState::box_local_to_world(const easy3d::vec3& p) const {
    easy3d::vec3 b[3];
    box_basis(b);
    return easy3d::vec3(box_center[0], box_center[1], box_center[2])
         + b[0] * p.x + b[1] * p.y + b[2] * p.z;
}
easy3d::vec3 CropState::box_world_to_local(const easy3d::vec3& p) const {
    easy3d::vec3 b[3];
    box_basis(b);
    const easy3d::vec3 q = p - easy3d::vec3(box_center[0], box_center[1], box_center[2]);
    return easy3d::vec3(easy3d::dot(q, b[0]), easy3d::dot(q, b[1]), easy3d::dot(q, b[2]));
}
void CropState::box_corners(easy3d::vec3 out[8]) const {
    int k = 0;
    for (int x = 0; x < 2; ++x)
        for (int y = 0; y < 2; ++y)
            for (int z = 0; z < 2; ++z)
                out[k++] = box_local_to_world(easy3d::vec3(
                    x ? box_half[0] : -box_half[0],
                    y ? box_half[1] : -box_half[1],
                    z ? box_half[2] : -box_half[2]));
}
easy3d::Box3 CropState::box_world_aabb() const {
    easy3d::vec3 corners[8];
    box_corners(corners);
    easy3d::Box3 bb;
    for (const auto& p : corners)
        bb.grow(p);
    return bb;
}
void CropState::set_box_from_bb(const easy3d::Box3& bb) {
    auto mn = bb.min_point(), mx = bb.max_point();
    for (int i = 0; i < 3; ++i) {
        box_center[i] = 0.5f * (mn[i] + mx[i]);
        box_half[i]   = std::max(0.001f, 0.5f * (mx[i] - mn[i]));
        box_rotation_deg[i] = 0.0f;
    }
}
void CropState::set_plane_from_bbox(const easy3d::Box3& bb) {
    auto mn = bb.min_point(), mx = bb.max_point();
    if (mode == CropMode::PlaneXY || mode == CropMode::PlaneCustom) {
        plane_normal[0]=0; plane_normal[1]=0; plane_normal[2]=1;
        plane_offset = -(0.5f * (mn.z + mx.z));
    } else if (mode == CropMode::PlaneYZ) {
        plane_normal[0]=1; plane_normal[1]=0; plane_normal[2]=0;
        plane_offset = -(0.5f * (mn.x + mx.x));
    } else if (mode == CropMode::PlaneXZ) {
        plane_normal[0]=0; plane_normal[1]=1; plane_normal[2]=0;
        plane_offset = -(0.5f * (mn.y + mx.y));
    }
}


namespace {

// ---- crop helpers ---------------------------------------------------

inline bool plane_side(const easy3d::vec3& p, const easy3d::vec3& n, float d) {
    return (n.x * p.x + n.y * p.y + n.z * p.z + d) > 0.0f;
}

struct MeshCropAttributes {
    easy3d::SurfaceMesh* src = nullptr;
    easy3d::SurfaceMesh* dst = nullptr;

    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3> snormal;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3> scolor;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec2> stexcoord;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3> fnormal;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3> fcolor;
    easy3d::SurfaceMesh::HalfedgeProperty<easy3d::vec2> htexcoord;

    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3> dnormal;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec3> dcolor;
    easy3d::SurfaceMesh::VertexProperty<easy3d::vec2> dtexcoord;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3> dfnormal;
    easy3d::SurfaceMesh::FaceProperty<easy3d::vec3> dfcolor;
    easy3d::SurfaceMesh::HalfedgeProperty<easy3d::vec2> dhtexcoord;

    MeshCropAttributes(easy3d::SurfaceMesh* s, easy3d::SurfaceMesh* d)
        : src(s), dst(d) {
        snormal = src->get_vertex_property<easy3d::vec3>("v:normal");
        scolor = src->get_vertex_property<easy3d::vec3>("v:color");
        stexcoord = src->get_vertex_property<easy3d::vec2>("v:texcoord");
        fnormal = src->get_face_property<easy3d::vec3>("f:normal");
        fcolor = src->get_face_property<easy3d::vec3>("f:color");
        htexcoord = src->get_halfedge_property<easy3d::vec2>("h:texcoord");

        if (snormal) dnormal = dst->add_vertex_property<easy3d::vec3>("v:normal");
        if (scolor) dcolor = dst->add_vertex_property<easy3d::vec3>("v:color");
        if (stexcoord) dtexcoord = dst->add_vertex_property<easy3d::vec2>("v:texcoord");
        if (fnormal) dfnormal = dst->add_face_property<easy3d::vec3>("f:normal");
        if (fcolor) dfcolor = dst->add_face_property<easy3d::vec3>("f:color");
        if (htexcoord) dhtexcoord = dst->add_halfedge_property<easy3d::vec2>("h:texcoord");

        auto diffuse = src->get_model_property<std::string>("m:texture_diffuse");
        if (diffuse) {
            auto ddiffuse = dst->add_model_property<std::string>("m:texture_diffuse", "");
            ddiffuse[0] = diffuse[0];
        }
        auto texture_data = src->get_model_property<std::vector<unsigned char>>("m:texture_data");
        if (texture_data) {
            auto ddata = dst->add_model_property<std::vector<unsigned char>>("m:texture_data");
            ddata[0] = texture_data[0];
        }
        auto texture_width = src->get_model_property<int>("m:texture_width");
        if (texture_width) {
            auto dwidth = dst->add_model_property<int>("m:texture_width", 0);
            dwidth[0] = texture_width[0];
        }
        auto texture_height = src->get_model_property<int>("m:texture_height");
        if (texture_height) {
            auto dheight = dst->add_model_property<int>("m:texture_height", 0);
            dheight[0] = texture_height[0];
        }
    }

    void copy_vertex(easy3d::SurfaceMesh::Vertex sv,
                     easy3d::SurfaceMesh::Vertex dv) {
        if (snormal) dnormal[dv] = snormal[sv];
        if (scolor) dcolor[dv] = scolor[sv];
        if (stexcoord) dtexcoord[dv] = stexcoord[sv];
    }

    void copy_face(easy3d::SurfaceMesh::Face sf,
                   easy3d::SurfaceMesh::Face df) {
        if (!df.is_valid())
            return;
        if (fnormal) dfnormal[df] = fnormal[sf];
        if (fcolor) dfcolor[df] = fcolor[sf];
        if (!htexcoord)
            return;

        std::vector<easy3d::SurfaceMesh::Halfedge> shalfedges;
        std::vector<easy3d::SurfaceMesh::Halfedge> dhalfedges;
        for (auto h : src->halfedges(sf))
            shalfedges.push_back(h);
        for (auto h : dst->halfedges(df))
            dhalfedges.push_back(h);
        if (shalfedges.size() != dhalfedges.size())
            return;
        for (std::size_t i = 0; i < shalfedges.size(); ++i)
            dhtexcoord[dhalfedges[i]] = htexcoord[shalfedges[i]];
    }
};

bool apply_surface_mesh_texture(easy3d::Model* model) {
    auto* mesh = dynamic_cast<easy3d::SurfaceMesh*>(model);
    if (!mesh)
        return false;
    auto* faces = mesh->renderer()->get_triangles_drawable("faces");
    if (!faces)
        return false;

    auto htexcoord = mesh->get_halfedge_property<easy3d::vec2>("h:texcoord");
    auto vtexcoord = mesh->get_vertex_property<easy3d::vec2>("v:texcoord");
    auto texture_path = mesh->get_model_property<std::string>("m:texture_diffuse");
    if (texture_path && !texture_path[0].empty() && (htexcoord || vtexcoord)) {
        auto* texture = easy3d::TextureManager::request(texture_path[0]);
        if (!texture)
            return false;
        if (htexcoord)
            faces->set_texture_coloring(easy3d::State::HALFEDGE, "h:texcoord", texture);
        else
            faces->set_texture_coloring(easy3d::State::VERTEX, "v:texcoord", texture);
        faces->update();
        mesh->renderer()->update();
        return true;
    }

    if (vtexcoord) {
        auto texture_data = mesh->get_model_property<std::vector<unsigned char>>("m:texture_data");
        auto texture_width = mesh->get_model_property<int>("m:texture_width");
        auto texture_height = mesh->get_model_property<int>("m:texture_height");
        if (texture_data && texture_width && texture_height
            && !texture_data[0].empty() && texture_width[0] > 0
            && texture_height[0] > 0) {
            auto* texture = easy3d::Texture::create(
                texture_data[0], texture_width[0], texture_height[0], 4);
            if (!texture)
                return false;
            faces->set_texture_coloring(easy3d::State::VERTEX, "v:texcoord", texture);
            faces->update();
            mesh->renderer()->update();
            return true;
        }
    }

    return false;
}

easy3d::PointCloud* crop_cloud_box(easy3d::PointCloud* src,
                                   const CropState& s,
                                   bool keep_inside) {
    auto* dst = new easy3d::PointCloud;
    auto sp = src->get_vertex_property<easy3d::vec3>("v:point");
    auto sn = src->get_vertex_property<easy3d::vec3>("v:normal");
    auto sc = src->get_vertex_property<easy3d::vec3>("v:color");
    auto dn = sn ? dst->add_vertex_property<easy3d::vec3>("v:normal") : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
    auto dc = sc ? dst->add_vertex_property<easy3d::vec3>("v:color") : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
    for (auto v : src->vertices()) {
        const auto& p = sp[v];
        const auto q = s.box_world_to_local(p);
        bool inside = std::abs(q.x) <= s.box_half[0]
                   && std::abs(q.y) <= s.box_half[1]
                   && std::abs(q.z) <= s.box_half[2];
        if (inside == keep_inside) {
            auto nv = dst->add_vertex(p);
            if (sn) dn[nv] = sn[v];
            if (sc) dc[nv] = sc[v];
        }
    }
    return dst;
}

easy3d::SurfaceMesh* crop_mesh_box(easy3d::SurfaceMesh* src,
                                   const CropState& s,
                                   bool keep_inside) {
    auto* dst = new easy3d::SurfaceMesh;
    auto sp = src->get_vertex_property<easy3d::vec3>("v:point");
    MeshCropAttributes attrs(src, dst);
    auto inside_box = [&](const easy3d::vec3& p) {
        const auto q = s.box_world_to_local(p);
        return std::abs(q.x) <= s.box_half[0]
            && std::abs(q.y) <= s.box_half[1]
            && std::abs(q.z) <= s.box_half[2];
    };
    std::unordered_map<int, easy3d::SurfaceMesh::Vertex> vmap;
    for (auto f : src->faces()) {
        bool keep = true;
        for (auto v : src->vertices(f))
            if (inside_box(sp[v]) != keep_inside) { keep = false; break; }
        if (!keep) continue;
        std::vector<easy3d::SurfaceMesh::Vertex> fv;
        for (auto v : src->vertices(f)) {
            auto it = vmap.find(v.idx());
            if (it == vmap.end()) {
                auto nv = dst->add_vertex(sp[v]);
                attrs.copy_vertex(v, nv);
                vmap[v.idx()] = nv;
                fv.push_back(nv);
            } else fv.push_back(it->second);
        }
        if (fv.size() >= 3) {
            auto nf = dst->add_face(fv);
            attrs.copy_face(f, nf);
        }
    }
    return dst;
}

easy3d::PointCloud* crop_cloud_plane(easy3d::PointCloud* src,
                                     const easy3d::vec3& n, float d, bool keep_positive) {
    auto* dst = new easy3d::PointCloud;
    auto sp = src->get_vertex_property<easy3d::vec3>("v:point");
    auto sn = src->get_vertex_property<easy3d::vec3>("v:normal");
    auto sc = src->get_vertex_property<easy3d::vec3>("v:color");
    auto dn = sn ? dst->add_vertex_property<easy3d::vec3>("v:normal") : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
    auto dc = sc ? dst->add_vertex_property<easy3d::vec3>("v:color") : easy3d::PointCloud::VertexProperty<easy3d::vec3>();
    for (auto v : src->vertices()) {
        if (plane_side(sp[v], n, d) == keep_positive) {
            auto nv = dst->add_vertex(sp[v]);
            if (sn) dn[nv] = sn[v];
            if (sc) dc[nv] = sc[v];
        }
    }
    return dst;
}

easy3d::SurfaceMesh* crop_mesh_plane(easy3d::SurfaceMesh* src,
                                     const easy3d::vec3& n, float d, bool keep_positive) {
    auto* dst = new easy3d::SurfaceMesh;
    auto sp = src->get_vertex_property<easy3d::vec3>("v:point");
    MeshCropAttributes attrs(src, dst);
    std::unordered_map<int, easy3d::SurfaceMesh::Vertex> vmap;
    for (auto f : src->faces()) {
        bool keep = true;
        for (auto v : src->vertices(f))
            if (plane_side(sp[v], n, d) != keep_positive) { keep = false; break; }
        if (!keep) continue;
        std::vector<easy3d::SurfaceMesh::Vertex> fv;
        for (auto v : src->vertices(f)) {
            auto it = vmap.find(v.idx());
            if (it == vmap.end()) {
                auto nv = dst->add_vertex(sp[v]);
                attrs.copy_vertex(v, nv);
                vmap[v.idx()] = nv;
                fv.push_back(nv);
            } else fv.push_back(it->second);
        }
        if (fv.size() >= 3) {
            auto nf = dst->add_face(fv);
            attrs.copy_face(f, nf);
        }
    }
    return dst;
}

void publish_result(easy3d::Model* src, easy3d::Model* result,
                    const char* suffix, ViewportCanvas* viewer) {
    if (!result) return;
    result->set_name(src->name() + "." + suffix);
    viewer->add_model(result);
    apply_surface_mesh_texture(result);
    auto* info = viewer->model_tree_info(src);
    viewer->register_model_tree_node(result, ModelTreeNodeInfo{
        info ? info->workspace_name : "Default", result->name(), src,
        ModelTreeNodeKind::Reconstruction, true});
    viewer->mark_dirty();
}

} // namespace


// =====================================================================
// Dialog
// =====================================================================

void renderDialogCrop(ViewportCanvas* viewer, MainWindow* win,
                      CropState& s, bool& open) {
    prepare_dialog_window(520, 580);
    DIALOG_BODY("Crop / Clip", open) {
        prereq_hint_only(prereq_any_model(viewer));
        if (s.gizmo_dirty) {
            win->overlays().update_crop_overlay(s);
            s.gizmo_dirty = false;
        }
        auto* model = viewer->current_model();
        if (!model) {
            ImGui::TextDisabled("No model selected.");
            return;
        }
        auto* mesh  = dynamic_cast<easy3d::SurfaceMesh*>(model);
        auto* cloud = dynamic_cast<easy3d::PointCloud*>(model);
        if (!mesh && !cloud) {
            ImGui::TextDisabled("Current model is neither a SurfaceMesh nor a PointCloud.");
            return;
        }

        ImGui::TextWrapped("Source: %s", model->name().c_str());

        if (!s.box_initialized) {
            s.set_box_from_bb(model->bounding_box());
            s.set_plane_from_bbox(model->bounding_box());
            s.box_initialized = true;
            if (s.mode == CropMode::Selection)
                win->overlays().clear_crop_overlay();
            else
                win->overlays().update_crop_overlay(s);
        }

        const char* modes[] = {"Box (AABB / OBB)", "Plane XY (z=const)", "Plane YZ (x=const)",
                               "Plane XZ (y=const)", "Plane (custom normal)",
                               "Selection Extract"};
        int cur = (int)s.mode;
        if (ImGui::Combo("Mode", &cur, modes, 6)) {
            if (cur != (int)s.mode) {
                s.mode = (CropMode)cur;
                if (!s.box_initialized) { s.set_box_from_bb(model->bounding_box()); s.box_initialized = true; }
                s.set_plane_from_bbox(model->bounding_box());
                if (s.mode == CropMode::Selection)
                    win->overlays().clear_crop_overlay();
                else
                    win->overlays().update_crop_overlay(s);
            }
        }

        ImGui::Separator();
        if (s.mode == CropMode::Selection)
            ImGui::TextDisabled("Use Select modes to pick faces / vertices / points, then extract them.");
        else
            ImGui::TextDisabled("Drag face arrows to resize; drag rings to rotate the crop box.");

        bool nums_changed = false;

        if (s.mode == CropMode::Box) {
            float bmin[3], bmax[3];
            s.box_min(bmin); s.box_max(bmax);
            if (ImGui::DragFloat3("Center", s.box_center, 0.01f)) nums_changed = true;
            if (ImGui::DragFloat3("Half Extent", s.box_half, 0.01f, 0.001f, 1e6f)) nums_changed = true;
            if (ImGui::DragFloat3("Rotation XYZ", s.box_rotation_deg, 0.25f, -360.0f, 360.0f)) nums_changed = true;
            ImGui::Separator();
            if (std::abs(s.box_rotation_deg[0]) < 1e-4f &&
                std::abs(s.box_rotation_deg[1]) < 1e-4f &&
                std::abs(s.box_rotation_deg[2]) < 1e-4f) {
                ImGui::Text("Min: %.4f, %.4f, %.4f", bmin[0], bmin[1], bmin[2]);
                ImGui::Text("Max: %.4f, %.4f, %.4f", bmax[0], bmax[1], bmax[2]);
            } else {
                const auto aabb = s.box_world_aabb();
                const auto mn = aabb.min_point();
                const auto mx = aabb.max_point();
                ImGui::Text("World AABB Min: %.4f, %.4f, %.4f", mn.x, mn.y, mn.z);
                ImGui::Text("World AABB Max: %.4f, %.4f, %.4f", mx.x, mx.y, mx.z);
            }
            if (ImGui::Button("Reset to Model BBox")) {
                s.set_box_from_bb(model->bounding_box());
                nums_changed = true;
            }
            claw_ui::same_line_if_fits_text("Keep Inside");
            ImGui::Checkbox("Keep Inside", &s.keep_inside);
        } else if (s.mode == CropMode::Selection) {
            ImGui::TextWrapped("Selected: %s", win->current_selection_summary().c_str());
            ImGui::TextDisabled("Face selection creates a SurfaceMesh. Vertex / point selection creates a PointCloud.");
            if (ImGui::Button("Clear Selection")) {
                win->clear_current_selection();
            }
        } else {
            if (s.mode == CropMode::PlaneXY) {
                s.plane_normal[0]=0; s.plane_normal[1]=0; s.plane_normal[2]=1;
            } else if (s.mode == CropMode::PlaneYZ) {
                s.plane_normal[0]=1; s.plane_normal[1]=0; s.plane_normal[2]=0;
            } else if (s.mode == CropMode::PlaneXZ) {
                s.plane_normal[0]=0; s.plane_normal[1]=1; s.plane_normal[2]=0;
            }
            if (s.mode != CropMode::PlaneCustom) {
                const char* lbl = s.mode==CropMode::PlaneXY ? "z offset"
                                : s.mode==CropMode::PlaneYZ ? "x offset" : "y offset";
                if (ImGui::DragFloat(lbl, &s.plane_offset, 0.01f)) nums_changed = true;
                ImGui::Text("%s = %.4f", lbl, s.plane_offset);
            } else {
                if (ImGui::DragFloat3("Normal", s.plane_normal, 0.01f)) nums_changed = true;
                if (ImGui::DragFloat("Offset", &s.plane_offset, 0.01f)) nums_changed = true;
            }
            ImGui::Checkbox("Keep positive half-space", &s.keep_positive);
            if (mesh)
                ImGui::TextDisabled("Mesh: keeps whole faces whose vertices are all on the kept side -- no cap fill.");
        }

        if (nums_changed && s.mode != CropMode::Selection)
            win->overlays().update_crop_overlay(s);

        ImGui::Separator();
        bool can_apply = s.mode != CropMode::Selection || win->has_current_selection();
        if (!can_apply)
            ImGui::BeginDisabled();
        if (ImGui::Button(s.mode == CropMode::Selection ? "Extract" : "Apply")) {
            if (s.mode == CropMode::Selection) {
                const std::string before = win->current_selection_summary();
                win->extract_selection();
                s.last_result_summary = "Selection extract requested.\nBefore: " + before;
                s.last_result_valid = true;
                return;
            }

            easy3d::Model* result = nullptr;
            const char* suffix = "cropped";
            std::size_t input = 0;
            if (cloud) input = cloud->n_vertices();
            else if (mesh) input = mesh->n_faces();

            if (s.mode == CropMode::Box) {
                if (cloud) result = crop_cloud_box(cloud, s, s.keep_inside);
                else if (mesh) result = crop_mesh_box(mesh, s, s.keep_inside);
                suffix = s.keep_inside ? "box-in" : "box-out";
            } else {
                easy3d::vec3 n(s.plane_normal[0], s.plane_normal[1], s.plane_normal[2]);
                float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
                if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
                float d = s.plane_offset;
                if (cloud) result = crop_cloud_plane(cloud, n, d, s.keep_positive);
                else if (mesh) result = crop_mesh_plane(mesh, n, d, s.keep_positive);
                suffix = s.keep_positive ? "plane-pos" : "plane-neg";
            }

            std::size_t out = 0;
            if (auto* rc = dynamic_cast<easy3d::PointCloud*>(result)) out = rc->n_vertices();
            else if (auto* rm = dynamic_cast<easy3d::SurfaceMesh*>(result)) out = rm->n_faces();
            if (out == 0) {
                delete result;
                std::ostringstream ss;
                ss << "Crop result was empty and was not added.\n"
                   << "Source: " << model->name() << "\n"
                   << "Input elements: " << input << "\n"
                   << "Output elements: 0";
                s.last_result_summary = ss.str();
                s.last_result_valid = true;
                LOG(WARNING) << "crop result is empty; not added to scene";
            } else {
                publish_result(model, result, suffix, viewer);
                win->overlays().save_crop_artifact(s, model, suffix);
                std::ostringstream ss;
                ss << "Crop result added.\n"
                   << "Source: " << model->name() << "\n"
                   << "Result: " << result->name() << "\n"
                   << "Mode: " << (s.mode == CropMode::Box ? "Box" : "Plane") << "\n"
                   << "Input elements: " << input << "\n"
                   << "Output elements: " << out << "\n"
                   << "Kept ratio: " << (input > 0 ? (100.0 * (double)out / (double)input) : 0.0) << "%";
                s.last_result_summary = ss.str();
                s.last_result_valid = true;
                LOG(INFO) << "crop result: " << out
                          << (dynamic_cast<easy3d::SurfaceMesh*>(result) ? " faces" : " points");
            }
        }
        if (!can_apply)
            ImGui::EndDisabled();
        claw_ui::same_line_if_fits_button(s.mode == CropMode::Selection ?
                                          "Delete Selected..." : "Close");
        if (s.mode == CropMode::Selection) {
            if (ImGui::Button("Delete Selected..."))
                win->request_delete_selection_confirmation();
            claw_ui::same_line_if_fits_button("Close");
        }
        if (ImGui::Button("Close")) open = false;

        ImGui::Separator();
        if (ImGui::Button("AI Parameter Advice")) {
            std::ostringstream extra;
            extra << "Crop / Clip panel state\n"
                  << "Mode: " << modes[(int)s.mode] << "\n"
                  << "Source: " << model->name() << "\n"
                  << "Selection: " << win->current_selection_summary() << "\n"
                  << "Keep inside: " << (s.keep_inside ? "true" : "false") << "\n"
                  << "Keep positive half-space: " << (s.keep_positive ? "true" : "false") << "\n";
            win->send_ai_request(
                "Given the current model and Crop / Clip panel state, recommend whether I should use box crop, plane clip, or selection extract. Suggest practical parameters and mention the main risks briefly.",
                AICtx_All, extra.str(),
                "Ask AI: Crop / Clip mode advice");
        }
        claw_ui::same_line_if_fits_button("AI Evaluate Result");
        if (!s.last_result_valid)
            ImGui::BeginDisabled();
        if (ImGui::Button("AI Evaluate Result")) {
            win->send_ai_request(
                "Evaluate the last Crop / Clip result. Tell me whether it looks too aggressive or too loose, and suggest the next cleanup or processing step.",
                AICtx_All, s.last_result_summary,
                "Ask AI: evaluate Crop / Clip result");
        }
        if (!s.last_result_valid)
            ImGui::EndDisabled();
    } DIALOG_END;
}
