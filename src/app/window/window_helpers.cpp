// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "window/window_helpers.h"
#include "window/main_window.h"

#include <easy3d/core/model.h>
#include <easy3d/renderer/renderer.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/state.h>
#include <easy3d/renderer/texture.h>
#include <easy3d/renderer/texture_manager.h>

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
            faces->set_texture_coloring(easy3d::State::HALFEDGE,
                                        "h:texcoord", texture);
        else
            faces->set_texture_coloring(easy3d::State::VERTEX,
                                        "v:texcoord", texture);
        faces->update();
        mesh->renderer()->update();
        return true;
    }

    if (vtexcoord) {
        auto texture_data = mesh->get_model_property<std::vector<unsigned char>>(
            "m:texture_data");
        auto texture_width = mesh->get_model_property<int>("m:texture_width");
        auto texture_height = mesh->get_model_property<int>("m:texture_height");
        if (texture_data && texture_width && texture_height
            && !texture_data[0].empty() && texture_width[0] > 0
            && texture_height[0] > 0) {
            auto* texture = easy3d::Texture::create(
                texture_data[0], texture_width[0], texture_height[0], 4);
            if (!texture)
                return false;

            faces->set_texture_coloring(easy3d::State::VERTEX,
                                        "v:texcoord", texture);
            faces->update();
            mesh->renderer()->update();
            return true;
        }
    }

    return false;
}


easy3d::Model* resolve_current_algorithm_source(MainWindow* win) {
    if (!win)
        return nullptr;

    const auto handle = win->algorithm_controller().current_source_handle();
    if (handle.valid())
        return win->viewer()->resolve_model(handle);

    return nullptr;
}

void mark_algorithm_done(MainWindow* win) {
    if (win)
        win->algorithm_controller().mark_ready_for_ui_commit();
}


SurfaceMeshAttributeCopier::SurfaceMeshAttributeCopier(
        easy3d::SurfaceMesh* s, easy3d::SurfaceMesh* d)
    : src(s), dst(d)
{
    snormal   = src->get_vertex_property<easy3d::vec3>("v:normal");
    scolor    = src->get_vertex_property<easy3d::vec3>("v:color");
    stexcoord = src->get_vertex_property<easy3d::vec2>("v:texcoord");
    fnormal   = src->get_face_property<easy3d::vec3>("f:normal");
    fcolor    = src->get_face_property<easy3d::vec3>("f:color");
    htexcoord = src->get_halfedge_property<easy3d::vec2>("h:texcoord");

    if (snormal)   dnormal   = dst->add_vertex_property<easy3d::vec3>("v:normal");
    if (scolor)    dcolor    = dst->add_vertex_property<easy3d::vec3>("v:color");
    if (stexcoord) dtexcoord = dst->add_vertex_property<easy3d::vec2>("v:texcoord");
    if (fnormal)   dfnormal  = dst->add_face_property<easy3d::vec3>("f:normal");
    if (fcolor)    dfcolor   = dst->add_face_property<easy3d::vec3>("f:color");
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


void SurfaceMeshAttributeCopier::copy_vertex(
        easy3d::SurfaceMesh::Vertex sv,
        easy3d::SurfaceMesh::Vertex dv)
{
    if (snormal)   dnormal[dv]   = snormal[sv];
    if (scolor)    dcolor[dv]    = scolor[sv];
    if (stexcoord) dtexcoord[dv] = stexcoord[sv];
}


void SurfaceMeshAttributeCopier::copy_face(
        easy3d::SurfaceMesh::Face sf,
        easy3d::SurfaceMesh::Face df)
{
    if (!df.is_valid())
        return;
    if (fnormal) dfnormal[df] = fnormal[sf];
    if (fcolor)  dfcolor[df]  = fcolor[sf];
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
