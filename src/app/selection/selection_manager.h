// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SELECTION_MANAGER_H
#define CLAW3D_SELECTION_MANAGER_H

/// Selection state and commands shared by the viewport and selection UI.

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace easy3d {
    class Model;
    class SurfaceMesh;
    class PointCloud;
}

enum class SelectionElementType {
    None,
    SurfaceVertex,
    SurfaceFace,
    SurfaceEdge,
    PointCloudPoint
};

enum class SelectionMode {
    View,
    PickSurfaceVertex,
    PickSurfaceFace,
    PickPointCloudPoint,
    RectangleSurfaceFace,
    RectanglePointCloudPoint,
    LassoSurfaceFace,       // future
    LassoPointCloudPoint    // future
};

struct PerModelSelection {
    std::vector<uint8_t> surface_vertices;
    std::vector<uint8_t> surface_faces;
    std::vector<uint8_t> surface_edges;
    std::vector<uint8_t> pointcloud_points;
    uint64_t geometry_revision = 0;

    bool empty() const;
    void clear();
};

class SelectionManager {
public:
    SelectionManager() = default;

    void add_model(easy3d::Model* m);
    void remove_model(easy3d::Model* m);
    void sync_with_viewer(const std::vector<easy3d::Model*>& models);

    PerModelSelection* get(easy3d::Model* m);
    const PerModelSelection* get(easy3d::Model* m) const;

    void clear_selection(easy3d::Model* m, SelectionElementType type);
    void clear_all_selections(easy3d::Model* m);
    void clear_all();

    bool is_selected(easy3d::Model* m, SelectionElementType type, int index) const;
    void set_selected(easy3d::Model* m, SelectionElementType type, int index, bool sel);
    void toggle_selected(easy3d::Model* m, SelectionElementType type, int index);

    int selected_count(easy3d::Model* m, SelectionElementType type) const;

    bool has_any_selection(easy3d::Model* m) const;
    bool has_any_selection() const;

    int revision() const { return revision_; }
    void bump_revision() { ++revision_; }

private:
    void resize_if_needed(easy3d::Model* m, PerModelSelection& s);

    std::unordered_map<easy3d::Model*, PerModelSelection> selections_;
    int revision_ = 0;
};

#endif // CLAW3D_SELECTION_MANAGER_H
