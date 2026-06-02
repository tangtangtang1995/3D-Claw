// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_OVERLAY_CONTROLLER_H
#define CLAW3D_OVERLAY_CONTROLLER_H

/// Owns temporary viewport overlay state and lifecycle for MainWindow.

#include "common/acvd_contract.h"
#include "common/alpha_wrap_contract.h"
#include "common/arap_deformation_contract.h"
#include "common/geodesic_contract.h"
#include "common/mcf_skeletonization_contract.h"
#include "common/planar_patch_remeshing_contract.h"
#include "common/simplification_contract.h"
#include "common/smoothing_contract.h"
#include "common/vsa_contract.h"
#include "overlays/algorithm_overlay_state.h"
#include "overlays/interaction_overlay_state.h"
#include "overlays/overlay_palette.h"
#include "overlays/region_growing_overlay_types.h"

#include <easy3d/core/types.h>

#include <string>
#include <vector>

struct AlignState;
struct CropState;
struct MeasurementState;
class SelectionManager;
class ViewportCanvas;

namespace easy3d {
class Model;
class PointCloud;
class SurfaceMesh;
}


struct Aw3OverlayOptions {
    int live_display_mode = 1;
    int live_recent_count = 300;
    bool live_show_gate = true;
    int live_gate_trail_count = 32;
    float live_surface_opacity = 0.28f;
    bool live_surface_wireframe = true;
};
class OverlayController {
public:
    OverlayController(ViewportCanvas& viewer, SelectionManager& selection_manager);

    bool selection_visible() const;
    void set_selection_visible(bool visible);
    void update_selection_overlays();
    void clear_selection_overlays();

    void reset_aw3_process_overlay();
    void clear_aw3_live_surface_overlay();
    void refresh_aw3_live_surface_style(const Aw3OverlayOptions& options);
    void update_aw3_live_overlay(const std::vector<AW3_FrameEvent>& events,
                                 const Aw3OverlayOptions& options,
                                 bool force = false);
    void update_aw3_live_surface_overlay(const std::vector<AW3_Point3d>& verts,
                                         const std::vector<AW3_Triangle>& faces,
                                         const Aw3OverlayOptions& options);

    void update_ransac_samples_overlay(const double pts[3][3]);
    void update_ransac_candidate_overlay(const double plane_eq[4],
                                         const double sample_pts[3][3],
                                         float bbox_diag);
    void clear_ransac_live_overlays();

    void init_rg_overlay(easy3d::PointCloud* src);
    void update_rg_overlay(std::vector<RGColorCmd>& cmds);
    void clear_rg_overlays();

    void init_simpl_overlay(easy3d::SurfaceMesh* src);
    void update_simpl_overlay(const std::vector<SimplTrailEntry>& new_entries);
    void clear_simpl_overlay();
    void update_simpl_snapshot_mesh(const std::vector<SIMPL_Point3d>& verts,
                                    const std::vector<SIMPL_Triangle>& tris,
                                    float face_opacity = 1.0f);
    void set_simpl_snapshot_opacity(float opacity);
    void clear_simpl_snapshot_mesh();

    void init_acvd_overlay(easy3d::SurfaceMesh* src);
    void update_acvd_cluster_overlay(const std::vector<ACVD_Point3d>& verts,
                                     const std::vector<ACVD_Triangle>& tris,
                                     const std::vector<int>& face_cluster_ids);
    void update_acvd_seed_overlay(const std::vector<ACVD_Point3d>& seeds);
    void clear_acvd_overlay();

    void init_vsa_overlay(easy3d::SurfaceMesh* src);
    void update_vsa_cluster_overlay(const std::vector<int>& face_proxy_ids);
    void update_vsa_seed_overlay(const std::vector<VSA_Point3d>& seeds);
    void clear_vsa_overlay();
    void promote_vsa_overlay_to_child(easy3d::SurfaceMesh* source,
                                      const std::string& new_name);

    void init_ppr_overlay(easy3d::SurfaceMesh* src);
    void update_ppr_patch_overlay(const std::vector<int>& face_patch_ids);
    void update_ppr_constraint_overlay(const std::vector<PPR_Point3d>& edge_endpoints);
    void update_ppr_corner_overlay(const std::vector<PPR_Point3d>& corner_points);
    void clear_ppr_overlay();

    void init_smoothing_overlay(easy3d::SurfaceMesh* src);
    void update_smoothing_overlay(const SMOOTH_Snapshot& snap);
    void clear_smoothing_overlay();

    void update_geo_source_overlay(easy3d::SurfaceMesh* source,
                                   const std::vector<easy3d::vec3>& points);
    void update_geo_target_overlay(easy3d::SurfaceMesh* source,
                                   const easy3d::vec3* p);
    void clear_geo_overlay();
    void init_front_overlay(easy3d::SurfaceMesh* src);
    void update_front_overlay(const GEO_FrontSnapshot& snap);
    void clear_front_overlay();
    void update_geo_front_path_overlay(easy3d::SurfaceMesh* source,
                                       const std::vector<float>& xyz_flat);
    void update_geo_exact_path_overlay(easy3d::SurfaceMesh* source,
                                       const std::vector<float>& xyz_flat);
    void clear_geo_path_overlays();

    void init_mcf_overlay(easy3d::SurfaceMesh* src);
    void update_mcf_overlay(const MCF_Snapshot& snap);
    void clear_mcf_overlay(bool restore_source = true);
    void paint_mcf_sdf_on_source(easy3d::SurfaceMesh* src,
                                 const std::vector<double>& sdf,
                                 bool on);
    void update_mcf_correspondence_overlay(const std::vector<MCF_Line>& lines);
    void clear_mcf_correspondence_overlay();
    bool set_mcf_source_ghost_visible(bool visible);
    bool set_mcf_meso_overlay_visible(bool visible);

    bool has_arap_preview_overlay();
    void init_arap_preview_overlay(easy3d::SurfaceMesh* src);
    void update_arap_preview_overlay(const ARAP_Snapshot& snap);
    void clear_arap_preview_overlay(bool restore_source);
    void update_arap_roi_overlay(const std::vector<easy3d::vec3>& pts);
    void update_arap_ctrl_overlay(const std::vector<easy3d::vec3>& pts,
                                  const std::vector<int>& group_ids);
    void update_arap_arrow_overlay(const std::vector<easy3d::vec3>& from,
                                   const std::vector<easy3d::vec3>& to);
    void update_arap_frame_overlay(const easy3d::vec3& origin_world,
                                   double tx, double ty, double tz,
                                   double rx_deg, double ry_deg, double rz_deg,
                                   float axis_length);
    void clear_arap_overlay();

    void update_measurement_overlay(const MeasurementState& s);
    void clear_measurement_overlay();
    void update_crop_overlay(const CropState& s);
    void clear_crop_overlay();
    void save_crop_artifact(const CropState& s, easy3d::Model* source,
                            const std::string& suffix);
    void update_align_gizmo(const AlignState& s);
    void clear_align_gizmo();
    void apply_transform_preview(easy3d::Model* m, AlignState& s);
    void reset_transform_preview(easy3d::Model* m, AlignState& s);

private:
    ViewportCanvas& viewer_;
    SelectionManager& selection_manager_;
    int selection_revision_ = 0;
    AlgorithmOverlayState algorithm_overlay_;
    InteractionOverlayState interaction_overlay_;
};

#endif // CLAW3D_OVERLAY_CONTROLLER_H