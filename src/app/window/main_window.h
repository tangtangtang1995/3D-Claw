// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_MAIN_WINDOW_H
#define CLAW3D_MAIN_WINDOW_H

/// Main application window, dock layout, panel state, and command dispatch.

#include <string>
#include <vector>
#include <memory>

#include <easy3d/core/model.h>
#include <easy3d/util/logging.h>

namespace easy3d { class SurfaceMesh; class PointCloud; class Graph; }

#include "services/core/algorithm_id.h"
#include "viewport/viewport_canvas.h"
#include "dialogs/imgui_dialogs.h"
#include "widgets/imgui_widgets.h"
#include "dialogs/3d_generation_dialog.h"
#include "selection/selection_manager.h"
#include "dialogs/measurement_dialog.h"
#include "dialogs/crop_dialog.h"
#include "dialogs/align_dialog.h"
#include "dialogs/animation_dialog.h"
#include "services/core/algorithm_controller.h"
#include "services/io/file_load_controller.h"
#include "ai/ai_service.h"

class WalkThrough;
class AIChatController;
class OverlayController;

class MainWindow : public easy3d::logging::Logger {
public:
    MainWindow();
    ~MainWindow();

    static MainWindow* instance() { return s_instance_; }

    void render();

    ViewportCanvas* viewer() { return &viewer_; }
    AIChatController* ai_chat() { return ai_service_.chat(); }
    void show_ai_chat() { dlg_ai_ = true; }
    AlgorithmController& algorithm_controller() { return algorithm_; }
    const AlgorithmController& algorithm_controller() const { return algorithm_; }
    OverlayController& overlays();
    const OverlayController& overlays() const;

    // True while an async file load is in progress OR while the just-loaded
    // models are being uploaded to GPU on the main thread. Main loop uses
    // this to keep ticking (animate the overlay) instead of blocking on events.
    bool is_loading() const {
        return file_loader_.is_busy() || algorithm_.is_running();
    }

    // Kick off an async file load on a worker thread. Used by both the
    // File>Open menu and the GLFW drag-and-drop callback. Safe to call
    // from the main thread only. No-op if a load is already in progress.
    void load_files_async(std::vector<std::string> filenames);

private:
    // --- Async loading / algorithm state ---
    FileLoadController file_loader_;
    AlgorithmController algorithm_;

private:
    void render_menu_bar();
    void render_menu_file();
    void render_menu_edit();
    void render_menu_select();
    void render_menu_property();
    void render_menu_point_cloud();
    void render_menu_point_cloud_triangulation();
    void render_menu_surface_mesh();
    void render_menu_surface_mesh_topology();
    void render_menu_surface_mesh_repair();
    void render_menu_surface_mesh_processing();
    void render_menu_view();
    void render_menu_camera();
    void render_menu_ai();
    void render_menu_help();
    void render_menu_window();
    void render_menu_analyze();
    void render_menu_measurement();
    void render_menu_crop_clip();
    void render_menu_align();
    void render_dialogs();
    void render_widgets();
    void setup_dockspace_layout();
    bool process_pending_file_upload();
    void record_algorithm_start_if_needed();
    void process_algorithm_completion();
    void sync_selection_manager_with_viewer();
    void wire_viewer_interaction_state();
    void handle_global_keyboard_shortcuts();
    void handle_global_mouse_wheel_scale();
    void render_welcome_dialog();
    void render_runtime_status_overlays();
    void render_delete_selection_confirmation();

    void menu_file_open();
    void menu_file_save();

    void renderAboutDialog();
    void renderAIPanel();

public:
    // Unified entry point used by panel-side AI buttons. Builds the
    // requested AIContext sections, optionally appends extra_context
    // (e.g. a Poisson/AW3 quality report), injects the combined block
    // as a one-shot system message, and sends task_prompt as the user
    // message. Safe to call when chat is missing / API key absent /
    // waiting (no-op).
    // `display_label` is the short text shown in the AI Chat panel. The
    // long task_prompt + injected context is what the AI actually receives,
    // unchanged. Empty display_label falls back to showing task_prompt.
    void send_ai_request(const std::string& task_prompt, unsigned ctx_flags,
                         const std::string& extra_context = std::string(),
                         const std::string& display_label = std::string());

    // Exposed for the Align dialog: read selected vertex/point indices to
    // build correspondence sets without going through the picking pipeline.
    SelectionManager& selection_manager() { return selection_manager_; }

    // Open an algorithm dialog by stable id. Used by the Health Report panel
    // to wire its Suggested-Actions buttons without hard-coding access to
    // each dlg_*_ / st_*_ pair. Unknown ids are silently ignored.
    void open_named_dialog(const char* id);
private:

    void menu_view_fit_screen();
    void menu_view_snapshot();

    void render_status_bar();
    void update_status_bar();

    // Logger override - captures log messages into g_log_entries
    void send(el::Level level, const std::string& msg) override;

    // Helper: open a dialog and reset its state
    template<typename T>
    void open_dialog(bool& flag, T& state) {
        flag = true;
        state = T{}; // reset to defaults
    }

    // --- Viewer ---
    ViewportCanvas viewer_;
    std::unique_ptr<WalkThrough> walk_through_;
    AIService ai_service_;
public:
    WalkThrough* walk_through() { return walk_through_.get(); }

    // --- View flags ---
    bool show_backend_logo_ = true;
    bool show_frame_rate_ = false;
    bool show_coordinates_under_mouse_ = false;
    bool show_primitive_id_under_mouse_ = false;

    // --- Dialog open flags ---
    bool dlg_ai_ = true;        // AI Chat - docked right-top by default
    bool dlg_about_ = false;

    // AI Chat: inject current model + active panel context into the next
    // user message. Defaults on so brand-new users get scene-aware answers.
    bool ai_use_context_ = true;

    // --- Selection system ---
    SelectionManager selection_manager_;
private:
    std::unique_ptr<OverlayController> overlays_;
public:
    SelectionMode selection_mode_ = SelectionMode::View;
    void delete_selection();    // Destructive, callers should confirm first

public:
    std::string current_selection_summary();
    bool has_current_selection();
    void clear_current_selection();
    void extract_selection();
    void request_delete_selection_confirmation();
    int select_scalar_range(const std::string& property_name,
                            bool face_property,
                            double min_value,
                            double max_value,
                            bool clear_first);


private:
    bool dlg_gaussian_noise_ = false;
    bool dlg_pc_simplify_ = false;
    bool dlg_snapshot_ = false;
    bool dlg_animation_ = false;
    bool dlg_animation_was_open_ = false;  // edge detect: reset walk_through status on close
    bool dlg_sm_curvature_ = false;
    bool dlg_sm_sampling_ = false;
    bool dlg_sm_simplification_ = false;
    bool dlg_sm_smoothing_ = false;
    bool dlg_sm_fairing_ = false;
    bool dlg_sm_hole_filling_ = false;
    bool dlg_sm_parameterization_ = false;
    bool dlg_sm_remeshing_ = false;
    bool dlg_pc_normals_ = false;
    bool dlg_poisson_ = false;
    bool dlg_properties_ = false;
    bool dlg_alpha_wrap_ = false;
    bool dlg_ransac_ = false;
    bool dlg_region_growing_ = false;
    bool dlg_cgal_simpl_ = false;
    bool dlg_acvd_ = false;
    bool dlg_vsa_ = false;
    bool dlg_ppr_ = false;
    bool dlg_cgal_smoothing_ = false;
    bool dlg_geo_ = false;
    bool dlg_mcf_ = false;
    bool dlg_pc_mesh_dist_ = false;
    bool dlg_pc_pc_dist_ = false;
    bool dlg_arap_ = false;
    bool dlg_param_ = false;
    bool dlg_3dgen_ = true;     // AI 3D Generation - docked right-bottom by default
    bool dlg_measure_ = false;
    bool dlg_crop_ = false;             // Crop / Clip dialog
    bool dlg_confirm_delete_sel_ = false; // Delete Selection confirm popup
    bool dlg_align_ = false;            // Transform / Align / ICP dialog
    bool wgt_history_ = true;           // Operation History panel - shown by default

    // History: id of the currently-running operation, or -1 when idle.
    // Set when an algorithm starts and cleared when completion is consumed.
    // Independent of the running flag because completion resets that flag
    // before UI can observe "finished".
    int history_pending_id_ = -1;
    bool history_prev_busy_ = false;

    // --- Dialog states ---
    GaussianNoiseState              st_gaussian_noise_;
    PointCloudSimplifyState         st_pc_simplify_;
    SnapshotState                   st_snapshot_;
    AnimationState                  st_animation_;
    SurfaceMeshCurvatureState       st_sm_curvature_;
    SurfaceMeshSamplingState        st_sm_sampling_;
    SurfaceMeshSimplificationState  st_sm_simplification_;
    SurfaceMeshSmoothingState       st_sm_smoothing_;
    SurfaceMeshFairingState         st_sm_fairing_;
    SurfaceMeshHoleFillingState     st_sm_hole_filling_;
    SurfaceMeshParameterizationState st_sm_parameterization_;
    SurfaceMeshRemeshingState       st_sm_remeshing_;
    PointCloudNormalEstimationState st_pc_normals_;
    PoissonReconstructionState      st_poisson_;
    PropertiesState                 st_properties_;
    AlphaWrappingState             st_alpha_wrap_;
    RansacState                    st_ransac_;
    RegionGrowingState              st_region_growing_;
    CGALSimplificationState         st_cgal_simpl_;
    ACVDState                       st_acvd_;
    VSAState                        st_vsa_;
    PPRState                        st_ppr_;
    SmoothingState                  st_cgal_smoothing_;
    GeodesicState                   st_geo_;
    MCFSkelState                    st_mcf_;
    PointCloudMeshDistState         st_pc_mesh_dist_;
    PointCloudPointCloudDistState   st_pc_pc_dist_;
    ARAPDeformationState            st_arap_;
    ParameterizationState           st_param_;
    Gen3DDialogState                st_3dgen_;
    MeasurementState                st_measure_;
    CropState                       st_crop_;
    AlignState                      st_align_;

    // --- Widget panel open flags ---
    bool wgt_model_list_ = true;
    bool wgt_properties_ = true;
    bool wgt_log_ = true;             // Tab in same dock as Properties; shown by default
    bool wgt_health_ = true;          // Health Report - visible by default
    bool dlg_settings_ = false;       // Display Settings popup

    bool first_frame_ = true;

    // Welcome / settings popup that runs on every launch. The first time
    // the popup opens within a process we copy the persisted API key (if
    // any) into welcome_api_key_buf_ so the user sees what was loaded and
    // can confirm / edit / replace it.
    bool show_welcome_api_dialog_ = true;
    bool welcome_buf_seeded_ = false;
    char welcome_api_key_buf_[256] = "";

    // --- Widget panel states ---
    ModelListState       st_model_list_;
    LogState             st_log_;
    PropertiesPanelState st_properties_panel_;
    HealthReportState    st_health_;
    HistoryPanelState    st_history_;

    static MainWindow* s_instance_;
};

#endif // CLAW3D_MAIN_WINDOW_H
