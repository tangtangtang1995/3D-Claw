// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MODEL_UTILS_H
#define CLAW3D_MODEL_UTILS_H

/// Widget-facing helpers for inspecting and mutating Easy3D models.

namespace easy3d {
class LinesDrawable;
class Model;
class PointsDrawable;
class TrianglesDrawable;
}

easy3d::PointsDrawable* model_points_drawable(easy3d::Model* model);
easy3d::LinesDrawable* model_lines_drawable(easy3d::Model* model);
easy3d::TrianglesDrawable* model_triangles_drawable(easy3d::Model* model);

#endif // CLAW3D_MODEL_UTILS_H
