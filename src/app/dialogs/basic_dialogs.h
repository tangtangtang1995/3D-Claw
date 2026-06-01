// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_BASIC_DIALOGS_H
#define CLAW3D_BASIC_DIALOGS_H

/// Render entry points for core mesh and point-cloud operation dialogs.

#include <string>
#include <vector>

#include <easy3d/core/types.h>

class ViewportCanvas;

// Dialog state structs - each dialog gets its own, reset on OpenPopup
// =============================================================================

struct GaussianNoiseState {
    float sigma = 0.001f;
};

struct PointCloudSimplifyState {
    float radius = 0.01f;
    bool found_points = false;
};

struct SnapshotState {
    int width = 1920;
    int height = 1080;
    int samples = 4;
    int background = 1; // 0=current, 1=white, 2=transparent
    bool expand = true;
};

struct WalkThroughState {
    // managed by WalkThrough class directly
};

struct SurfaceMeshCurvatureState {
    int smooth_iters = 3;
    bool two_ring = true;
};

struct SurfaceMeshSamplingState {
    int num_points = 100000;
};

struct SurfaceMeshSimplificationState {
    int target_vertices = 1000;
};

struct SurfaceMeshSmoothingState {
    int scheme = 0; // 0=explicit, 1=implicit
    int iterations = 10;
    bool uniform = false;
};

struct SurfaceMeshFairingState {
    int criterion = 0; // 0=area, 1=curvature, 2=curvature_variation
};

struct SurfaceMeshHoleFillingState {
    int method = 0; // 0=simple
};

struct SurfaceMeshParameterizationState {
    int method = 0; // 0=LSCM, 1=harmonic
};

struct SurfaceMeshRemeshingState {
    int scheme = 0; // 0=uniform, 1=adaptive
    float edge_length = 0.01f;
    bool use_features = false;
    int feature_angle = 60;
};

struct PointCloudNormalEstimationState {
    int k = 16;
    bool reorient = true;
    bool normalize = false;
};

struct PoissonReconstructionState {
    int depth = 8;
    float samples_per_node = 1.5f;
    float iso_div = 4.0f;
    int cg_depth = 0;
    float scale = 1.1f;
};

struct PropertiesState {
    char command[256] = "";
    bool use_manip = true;
};

// =============================================================================
// Dialog render functions
// =============================================================================

void renderDialogGaussianNoise(ViewportCanvas* viewer, GaussianNoiseState& s, bool& open);
void renderDialogPointCloudSimplify(ViewportCanvas* viewer, PointCloudSimplifyState& s, bool& open);
void renderDialogSnapshot(ViewportCanvas* viewer, SnapshotState& s, bool& open);
void renderDialogWalkThrough(ViewportCanvas* viewer, WalkThroughState& s, bool& open);
void renderDialogSurfaceMeshCurvature(ViewportCanvas* viewer, SurfaceMeshCurvatureState& s, bool& open);
void renderDialogSurfaceMeshSampling(ViewportCanvas* viewer, SurfaceMeshSamplingState& s, bool& open);
void renderDialogSurfaceMeshSimplification(ViewportCanvas* viewer, SurfaceMeshSimplificationState& s, bool& open);
void renderDialogSurfaceMeshSmoothing(ViewportCanvas* viewer, SurfaceMeshSmoothingState& s, bool& open);
void renderDialogSurfaceMeshFairing(ViewportCanvas* viewer, SurfaceMeshFairingState& s, bool& open);
void renderDialogSurfaceMeshHoleFilling(ViewportCanvas* viewer, SurfaceMeshHoleFillingState& s, bool& open);
void renderDialogSurfaceMeshParameterization(ViewportCanvas* viewer, SurfaceMeshParameterizationState& s, bool& open);
void renderDialogSurfaceMeshRemeshing(ViewportCanvas* viewer, SurfaceMeshRemeshingState& s, bool& open);
void renderDialogPointCloudNormalEstimation(ViewportCanvas* viewer, PointCloudNormalEstimationState& s, bool& open);
void renderDialogPoissonReconstruction(ViewportCanvas* viewer, PoissonReconstructionState& s, bool& open);

struct PointCloudMeshDistState {
    int source_idx = -1;
    int target_idx = -1;
    bool computed = false;
    float max_dist = 0, mean_dist = 0, rms_dist = 0, stddev_dist = 0;
    float disp_min = 0, disp_max = 0; // display range (absolute values)
};
struct PointCloudPointCloudDistState {
    int source_idx = -1;
    int target_idx = -1;
    bool computed = false;
    float max_dist = 0, mean_dist = 0, rms_dist = 0, stddev_dist = 0;
    float disp_min = 0, disp_max = 0; // display range (absolute values)
};
void renderDialogPointCloudMeshDistance(ViewportCanvas* viewer, PointCloudMeshDistState& s, bool& open);
void renderDialogPointCloudPointCloudDistance(ViewportCanvas* viewer, PointCloudPointCloudDistState& s, bool& open);
void renderDialogProperties(ViewportCanvas* viewer, PropertiesState& s, bool& open);



#endif // CLAW3D_BASIC_DIALOGS_H
