// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.
//
// This loader was originally introduced as an Easy3D file-IO override in
// 3D Claw. It now lives in the product IO layer so official Easy3D can be
// consumed as an external dependency.

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#include <3rd_party/stb/stb_image.h>
#include <tiny_gltf.h>

#include "io/surface_mesh_io.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace claw3d::io {
namespace {

using easy3d::SurfaceMesh;
using easy3d::vec2;
using easy3d::vec3;

int component_count(int type);

bool valid_index(int index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}

bool checked_mul(std::size_t a, std::size_t b, std::size_t& out) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a)
        return false;
    out = a * b;
    return true;
}

bool checked_add(std::size_t a, std::size_t b, std::size_t& out) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
        return false;
    out = a + b;
    return true;
}

bool accessor_stride_and_size(const tinygltf::Accessor& acc,
                              const tinygltf::BufferView& view,
                              std::size_t& stride,
                              std::size_t& element_size) {
    const int component_size =
        tinygltf::GetComponentSizeInBytes(acc.componentType);
    const int components = component_count(acc.type);
    if (component_size <= 0 || components <= 0)
        return false;

    element_size =
        static_cast<std::size_t>(component_size) *
        static_cast<std::size_t>(components);
    const int byte_stride = acc.ByteStride(view);
    if (byte_stride < 0)
        return false;
    stride = byte_stride > 0 ? static_cast<std::size_t>(byte_stride)
                             : element_size;
    return stride >= element_size;
}

bool accessor_data(const tinygltf::Model& model,
                   const tinygltf::Accessor& acc,
                   const unsigned char*& data,
                   const tinygltf::BufferView*& view) {
    data = nullptr;
    view = nullptr;
    if (!valid_index(acc.bufferView, model.bufferViews.size()))
        return false;

    view = &model.bufferViews[acc.bufferView];
    if (!valid_index(view->buffer, model.buffers.size()))
        return false;

    const auto& buffer = model.buffers[view->buffer];
    if (acc.count == 0)
        return false;

    std::size_t stride = 0;
    std::size_t element_size = 0;
    if (!accessor_stride_and_size(acc, *view, stride, element_size))
        return false;

    std::size_t last_offset = 0;
    if (!checked_mul(acc.count - 1, stride, last_offset))
        return false;
    std::size_t required = 0;
    if (!checked_add(last_offset, element_size, required))
        return false;

    if (view->byteOffset > buffer.data.size())
        return false;
    if (view->byteLength > buffer.data.size() - view->byteOffset)
        return false;
    if (acc.byteOffset > view->byteLength)
        return false;
    if (required > view->byteLength - acc.byteOffset)
        return false;

    std::size_t offset = 0;
    if (!checked_add(view->byteOffset, acc.byteOffset, offset))
        return false;
    data = buffer.data.data() + offset;
    return true;
}

float read_float_component(const unsigned char* data,
                           std::size_t stride,
                           std::size_t index,
                           std::size_t component) {
    float value = 0.0f;
    std::memcpy(&value,
                data + index * stride + component * sizeof(float),
                sizeof(value));
    return value;
}

bool read_unsigned_index(const unsigned char* data,
                         int component_type,
                         std::size_t byte_offset,
                         std::size_t& value) {
    const auto* cursor = data + byte_offset;
    switch (component_type) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        value = cursor[0];
        return true;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
        std::uint16_t raw = 0;
        std::memcpy(&raw, cursor, sizeof(raw));
        value = raw;
        return true;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
        std::uint32_t raw = 0;
        std::memcpy(&raw, cursor, sizeof(raw));
        value = raw;
        return true;
    }
    default:
        return false;
    }
}

bool is_float_attribute(const tinygltf::Accessor& acc, int type) {
    return acc.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
        acc.type == type;
}

int component_count(int type) {
    switch (type) {
    case TINYGLTF_TYPE_SCALAR: return 1;
    case TINYGLTF_TYPE_VEC2:   return 2;
    case TINYGLTF_TYPE_VEC3:   return 3;
    case TINYGLTF_TYPE_VEC4:   return 4;
    default:                   return 0;
    }
}

std::vector<unsigned char> rgba_from_image(const tinygltf::Image& img) {
    const int w = img.width;
    const int h = img.height;
    int comp = img.component;
    if (w <= 0 || h <= 0 || img.image.empty())
        return {};

    const std::size_t pixel_count =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (comp <= 0)
        comp = static_cast<int>(img.image.size() / pixel_count);
    if (comp < 1 || comp > 4 ||
        img.image.size() < pixel_count * static_cast<std::size_t>(comp))
        return {};

    std::vector<unsigned char> rgba(pixel_count * 4, 255);
    for (int y = 0; y < h; ++y) {
        const int src_y = h - 1 - y;
        for (int x = 0; x < w; ++x) {
            const std::size_t src =
                (static_cast<std::size_t>(src_y) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(x)) * static_cast<std::size_t>(comp);
            const std::size_t dst =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(x)) * 4;
            rgba[dst + 0] = img.image[src + 0];
            rgba[dst + 1] = comp >= 3 ? img.image[src + 1] : img.image[src + 0];
            rgba[dst + 2] = comp >= 3 ? img.image[src + 2] : img.image[src + 0];
            rgba[dst + 3] = comp == 2 ? img.image[src + 1]
                : (comp >= 4 ? img.image[src + 3] : 255);
        }
    }
    return rgba;
}

bool load_gltf_mesh(const tinygltf::Model& model,
                    unsigned int mesh_idx,
                    SurfaceMesh* mesh) {
    if (mesh_idx >= model.meshes.size())
        return false;

    const auto& tm = model.meshes[mesh_idx];

    std::vector<SurfaceMesh::Vertex> vh;
    SurfaceMesh::VertexProperty<vec3> points;
    SurfaceMesh::VertexProperty<vec3> normals;
    SurfaceMesh::VertexProperty<vec2> texcoords;
    SurfaceMesh::VertexProperty<vec3> colors;

    points = mesh->vertex_property<vec3>("v:point");

    for (const auto& prim : tm.primitives) {
        if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) {
            LOG(WARNING) << "glTF primitive skipped: only TRIANGLES are supported";
            continue;
        }

        auto pos_it = prim.attributes.find("POSITION");
        if (pos_it == prim.attributes.end() ||
            !valid_index(pos_it->second, model.accessors.size())) {
            LOG(WARNING) << "glTF primitive skipped: missing POSITION";
            continue;
        }

        const auto* pos_acc = &model.accessors[pos_it->second];
        if (!is_float_attribute(*pos_acc, TINYGLTF_TYPE_VEC3)) {
            LOG(WARNING) << "glTF primitive skipped: POSITION must be float vec3";
            continue;
        }

        const unsigned char* pos_raw = nullptr;
        const tinygltf::BufferView* pos_bv = nullptr;
        if (!accessor_data(model, *pos_acc, pos_raw, pos_bv)) {
            LOG(WARNING) << "glTF primitive skipped: invalid POSITION buffer";
            continue;
        }
        std::size_t pos_stride = 0;
        std::size_t element_size = 0;
        if (!accessor_stride_and_size(*pos_acc, *pos_bv,
                                      pos_stride, element_size)) {
            LOG(WARNING) << "glTF primitive skipped: invalid POSITION stride";
            continue;
        }
        const std::size_t n_verts = pos_acc->count;

        const unsigned char* norm_data = nullptr;
        std::size_t norm_stride = 0;
        auto norm_it = prim.attributes.find("NORMAL");
        if (norm_it != prim.attributes.end() &&
            valid_index(norm_it->second, model.accessors.size())) {
            const auto* na = &model.accessors[norm_it->second];
            const unsigned char* raw = nullptr;
            const tinygltf::BufferView* nbv = nullptr;
            if (na->count >= n_verts &&
                is_float_attribute(*na, TINYGLTF_TYPE_VEC3) &&
                accessor_data(model, *na, raw, nbv) &&
                accessor_stride_and_size(*na, *nbv, norm_stride,
                                         element_size)) {
                norm_data = raw;
            } else {
                LOG(WARNING) << "glTF NORMAL attribute skipped: invalid buffer";
            }
        }

        const unsigned char* tex_data = nullptr;
        std::size_t tex_stride = 0;
        auto tex_it = prim.attributes.find("TEXCOORD_0");
        if (tex_it != prim.attributes.end() &&
            valid_index(tex_it->second, model.accessors.size())) {
            const auto* ta = &model.accessors[tex_it->second];
            const unsigned char* raw = nullptr;
            const tinygltf::BufferView* tbv = nullptr;
            if (ta->count >= n_verts &&
                is_float_attribute(*ta, TINYGLTF_TYPE_VEC2) &&
                accessor_data(model, *ta, raw, tbv) &&
                accessor_stride_and_size(*ta, *tbv, tex_stride,
                                         element_size)) {
                tex_data = raw;
            } else {
                LOG(WARNING) << "glTF TEXCOORD_0 attribute skipped: invalid buffer";
            }
        }

        const unsigned char* col_data = nullptr;
        std::size_t col_stride = 0;
        auto col_it = prim.attributes.find("COLOR_0");
        if (col_it != prim.attributes.end() &&
            valid_index(col_it->second, model.accessors.size())) {
            const auto* ca = &model.accessors[col_it->second];
            const unsigned char* raw = nullptr;
            const tinygltf::BufferView* cbv = nullptr;
            if (ca->count >= n_verts &&
                ca->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT &&
                (ca->type == TINYGLTF_TYPE_VEC3 ||
                 ca->type == TINYGLTF_TYPE_VEC4) &&
                accessor_data(model, *ca, raw, cbv) &&
                accessor_stride_and_size(*ca, *cbv, col_stride,
                                         element_size)) {
                col_data = raw;
            } else {
                LOG(WARNING) << "glTF COLOR_0 attribute skipped: invalid buffer";
            }
        }

        const bool has_norm = norm_data != nullptr;
        const bool has_tex = tex_data != nullptr;
        const bool has_col = col_data != nullptr;

        if (has_norm)
            normals = mesh->vertex_property<vec3>("v:normal");
        if (has_tex)
            texcoords = mesh->vertex_property<vec2>("v:texcoord");
        if (has_col)
            colors = mesh->vertex_property<vec3>("v:color");

        const std::size_t base_vert = vh.size();
        for (std::size_t i = 0; i < n_verts; ++i) {
            const float x = read_float_component(pos_raw, pos_stride, i, 0);
            const float y = read_float_component(pos_raw, pos_stride, i, 1);
            const float z = read_float_component(pos_raw, pos_stride, i, 2);
            auto v = mesh->add_vertex(vec3(x, y, z));
            vh.push_back(v);
        }
        if (has_norm) {
            for (std::size_t i = 0; i < n_verts; ++i) {
                const float x = read_float_component(norm_data, norm_stride, i, 0);
                const float y = read_float_component(norm_data, norm_stride, i, 1);
                const float z = read_float_component(norm_data, norm_stride, i, 2);
                normals[vh[base_vert + i]] = vec3(x, y, z);
            }
        }
        if (has_tex) {
            for (std::size_t i = 0; i < n_verts; ++i) {
                const float u = read_float_component(tex_data, tex_stride, i, 0);
                const float v = read_float_component(tex_data, tex_stride, i, 1);
                texcoords[vh[base_vert + i]] = vec2(u, v);
            }
        }
        if (has_col) {
            for (std::size_t i = 0; i < n_verts; ++i) {
                const float r = read_float_component(col_data, col_stride, i, 0);
                const float g = read_float_component(col_data, col_stride, i, 1);
                const float b = read_float_component(col_data, col_stride, i, 2);
                colors[vh[base_vert + i]] = vec3(r, g, b);
            }
        }

        if (prim.indices >= 0) {
            if (!valid_index(prim.indices, model.accessors.size()))
                continue;
            const auto* idx_acc = &model.accessors[prim.indices];
            const unsigned char* idx_raw = nullptr;
            const tinygltf::BufferView* idx_bv = nullptr;
            if (idx_acc->type != TINYGLTF_TYPE_SCALAR ||
                !accessor_data(model, *idx_acc, idx_raw, idx_bv))
                continue;
            const int component_type = idx_acc->componentType;
            std::size_t idx_stride = 0;
            if (!accessor_stride_and_size(*idx_acc, *idx_bv,
                                          idx_stride, element_size))
                continue;
            const std::size_t n_idx = idx_acc->count;
            const std::size_t tri_limit = n_idx - (n_idx % 3);
            for (std::size_t i = 0; i < tri_limit; i += 3) {
                std::size_t a = 0;
                std::size_t b = 0;
                std::size_t c = 0;
                if (!read_unsigned_index(idx_raw, component_type,
                                         i * idx_stride, a) ||
                    !read_unsigned_index(idx_raw, component_type,
                                         (i + 1) * idx_stride, b) ||
                    !read_unsigned_index(idx_raw, component_type,
                                         (i + 2) * idx_stride, c)) {
                    continue;
                }
                if (a < n_verts && b < n_verts && c < n_verts &&
                    a != b && b != c && a != c) {
                    mesh->add_triangle(vh[base_vert + a],
                                       vh[base_vert + b],
                                       vh[base_vert + c]);
                }
            }
        } else {
            const std::size_t tri_limit = n_verts - (n_verts % 3);
            for (std::size_t i = 0; i < tri_limit; i += 3) {
                mesh->add_triangle(vh[base_vert + i],
                                   vh[base_vert + i + 1],
                                   vh[base_vert + i + 2]);
            }
        }
    }

    return mesh->n_faces() > 0;
}

} // namespace

bool load_gltf_surface_mesh(const std::string& file_name, SurfaceMesh* mesh) {
    if (!mesh)
        return false;

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ok = false;
    const std::string ext = easy3d::file_system::extension(file_name, true);
    if (ext == "glb")
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, file_name);
    else if (ext == "gltf")
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, file_name);

    if (!warn.empty())
        LOG(WARNING) << "glTF: " << warn;
    if (!err.empty()) {
        LOG(ERROR) << "glTF: " << err;
        return false;
    }
    if (!ok || model.meshes.empty())
        return false;

    if (!load_gltf_mesh(model, 0, mesh))
        return false;

    for (const auto& mat : model.materials) {
        const int tex_idx = mat.pbrMetallicRoughness.baseColorTexture.index;
        if (tex_idx < 0 || tex_idx >= static_cast<int>(model.textures.size()))
            continue;
        const int img_idx = model.textures[tex_idx].source;
        if (img_idx < 0 || img_idx >= static_cast<int>(model.images.size()))
            continue;
        const auto& img = model.images[img_idx];
        std::vector<unsigned char> rgba = rgba_from_image(img);
        if (rgba.empty())
            continue;

        mesh->add_model_property<std::vector<unsigned char>>(
            "m:texture_data", rgba);
        mesh->add_model_property<int>("m:texture_width", img.width);
        mesh->add_model_property<int>("m:texture_height", img.height);
        break;
    }

    return true;
}

} // namespace claw3d::io
