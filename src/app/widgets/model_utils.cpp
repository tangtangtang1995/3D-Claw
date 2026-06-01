// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "widgets/model_utils.h"

#include <easy3d/core/graph.h>
#include <easy3d/core/model.h>
#include <easy3d/core/surface_mesh.h>
#include <easy3d/renderer/drawable_lines.h>
#include <easy3d/renderer/drawable_points.h>
#include <easy3d/renderer/drawable_triangles.h>
#include <easy3d/renderer/renderer.h>


easy3d::PointsDrawable* model_points_drawable(easy3d::Model* model) {
    return model->renderer()->get_points_drawable("vertices");
}

easy3d::LinesDrawable* model_lines_drawable(easy3d::Model* model) {
    if (auto* graph = dynamic_cast<easy3d::Graph*>(model)) {
        if (graph->n_edges() == 0)
            return nullptr;
        return model->renderer()->get_lines_drawable("edges");
    }

    if (dynamic_cast<easy3d::SurfaceMesh*>(model)) {
        auto* drawable = model->renderer()->get_lines_drawable("edges");
        if (drawable)
            return drawable;
        return model->renderer()->get_lines_drawable("borders");
    }

    return nullptr;
}

easy3d::TrianglesDrawable* model_triangles_drawable(easy3d::Model* model) {
    if (dynamic_cast<easy3d::SurfaceMesh*>(model))
        return model->renderer()->get_triangles_drawable("faces");
    return nullptr;
}
