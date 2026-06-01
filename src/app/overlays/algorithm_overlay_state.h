// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ALGORITHM_OVERLAY_STATE_H
#define CLAW3D_ALGORITHM_OVERLAY_STATE_H

/// Shared state for temporary algorithm preview overlays in the viewport.

#include "common/alpha_wrap_contract.h"

#include <easy3d/core/types.h>

#include <deque>
#include <vector>

namespace easy3d {
class Graph;
class PointCloud;
class SurfaceMesh;
}

struct SimplTrailEntry {
    easy3d::vec3 p0;
    easy3d::vec3 p1;
    easy3d::vec3 placement;
};

struct SimplificationOverlayState {
    easy3d::SurfaceMesh* source_mesh = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;

    easy3d::Graph* trail_graph = nullptr;
    std::deque<SimplTrailEntry> trail_buffer;
    int trail_cap = 64;

    easy3d::SurfaceMesh* snapshot_mesh = nullptr;
};

struct VsaOverlayState {
    easy3d::SurfaceMesh* cluster_mesh = nullptr;
    easy3d::Graph* seed_graph = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct PlanarPatchRemeshingOverlayState {
    easy3d::SurfaceMesh* patch_mesh = nullptr;
    easy3d::Graph* constraint_graph = nullptr;
    easy3d::Graph* corner_graph = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct SmoothingOverlayState {
    easy3d::SurfaceMesh* current_mesh = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct FrontPropagationOverlayState {
    easy3d::SurfaceMesh* overlay_mesh = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_face_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct ArapInteractionOverlayState {
    easy3d::Graph* roi_graph = nullptr;
    easy3d::Graph* control_graph = nullptr;
    easy3d::Graph* arrow_graph = nullptr;
    easy3d::Graph* frame_graph = nullptr;
};

struct ArapPreviewOverlayState {
    easy3d::SurfaceMesh* preview_mesh = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_face_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct McfOverlayState {
    easy3d::SurfaceMesh* meso_overlay = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_face_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
    easy3d::Graph* correspondence_graph = nullptr;
};

struct Aw3OverlayState {
    easy3d::SurfaceMesh* live_surface_mesh = nullptr;
    easy3d::SurfaceMesh* gate_mesh = nullptr;
    easy3d::PointCloud* steiner_cloud = nullptr;
    std::vector<AW3_FrameEvent> steiner_events;
    std::vector<AW3_FrameEvent> gate_events;
    AW3_FrameEvent gate_event{};
    bool has_gate = false;
};

struct AcvdOverlayState {
    easy3d::SurfaceMesh* cluster_mesh = nullptr;
    easy3d::Graph* seed_graph = nullptr;
    easy3d::SurfaceMesh* source_ghost = nullptr;
    float source_saved_opacity = 1.0f;
    bool source_saved_visible = true;
    bool source_saved_edge_visible = false;
    int source_saved_edge_coloring_method = 0;
    easy3d::vec4 source_saved_edge_color =
        easy3d::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    float source_saved_edge_width = 1.0f;
};

struct AlgorithmOverlayState {
    easy3d::Graph* ransac_samples = nullptr;
    easy3d::SurfaceMesh* ransac_candidate = nullptr;

    easy3d::PointCloud* region_growing_source = nullptr;
    int region_growing_saved_coloring_method = 0;

    SimplificationOverlayState simplification;
    VsaOverlayState vsa;
    PlanarPatchRemeshingOverlayState planar_patch_remeshing;
    SmoothingOverlayState smoothing;
    FrontPropagationOverlayState front_propagation;
    ArapInteractionOverlayState arap_interaction;
    ArapPreviewOverlayState arap_preview;
    McfOverlayState mcf;
    Aw3OverlayState aw3;
    AcvdOverlayState acvd;
};

#endif // CLAW3D_ALGORITHM_OVERLAY_STATE_H
