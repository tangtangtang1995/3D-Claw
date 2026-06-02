// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_VIEWPORT_CANVAS_H
#define CLAW3D_VIEWPORT_CANVAS_H

/// Scene viewport, model ownership, picking, rendering, and camera interaction.

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <easy3d/core/types.h>

#include "common/model_handle.h"

enum class SelectionMode : int;
class SelectionManager;
struct MeasurementState;
struct CropState;
struct AlignState;

namespace easy3d {
    class Camera;
    class Model;
    class SurfaceMesh;
    class LinesDrawable;
    class TrianglesDrawable;
    class TextRenderer;
    class KeyFrameInterpolator;
    class WalkThrough;
}

// ============================================================================
// Model Tree (application layer; does NOT modify Easy3D core Model ownership)
// ============================================================================

enum class ModelTreeNodeKind {
    Data,           // imported file, point cloud, mesh
    Reconstruction, // AW3 output, Poisson, etc.
    Primitive,      // RANSAC plane/sphere/cylinder
    Annotation,     // user-picked / user-authored helper geometry
    Overlay,        // live preview temporary object
    Debug
};

struct ModelTreeNodeInfo {
    std::string workspace_name;     // e.g. "bunny"; groups models at root
    std::string display_name;       // shown in tree; defaults to model->name()
    easy3d::Model* parent = nullptr;// tree parent (null = workspace root)
    ModelTreeNodeKind kind = ModelTreeNodeKind::Data;
    bool visible_in_tree = true;    // false = hidden from Model List (overlays)
};

enum class SelectedDrawableType {
    None,       // model selected, no specific drawable
    Points,
    Lines,
    Triangles
};

class ViewportCanvas {
public:
    ViewportCanvas();
    ~ViewportCanvas();

    // Called every frame: creates the viewport window in ImGui, renders scene.
    void render();

    // Model management
    easy3d::Model* add_model(const std::string& file_name);
    easy3d::Model* add_model(easy3d::Model* model);
    bool delete_model(easy3d::Model* model);
    const std::vector<std::shared_ptr<easy3d::Model>>& models() const { return models_; }
    easy3d::Model* current_model();
    void set_current_model(easy3d::Model* m);
    // Same as set_current_model but does NOT touch selection_active_
    // / selected_drawable_type_ / dirty_. Used by code that briefly re-points
    // current after add_model(overlay) and must not look like a user click.
    void set_current_model_silent(easy3d::Model* m);
    ModelHandle model_handle(easy3d::Model* model) const;
    easy3d::Model* resolve_model(ModelHandle handle) const;
    bool is_model_live(ModelHandle handle) const { return resolve_model(handle) != nullptr; }

    // Selected drawable type within the current model.
    // None = whole model selected; Points/Lines/Triangles = a child sub-node was clicked.
    SelectedDrawableType selected_drawable_type() const { return selected_drawable_type_; }
    void set_selected_drawable_type(SelectedDrawableType t) { selected_drawable_type_ = t; }

    // Viewport overlay toggles (bound to View menu).
    bool& show_selection_bbox_ref() { return show_selection_bbox_; }
    bool& show_axes_gizmo_ref() { return show_axes_gizmo_; }
    void set_selection_bbox_suppressed(bool suppress);

    // --- Model tree ---
    void register_model_tree_node(easy3d::Model* model, const ModelTreeNodeInfo& info);
    void register_model_tree_overlay(easy3d::Model* model, easy3d::Model* source = nullptr,
                                     const std::string& display_name = "");
    void unregister_model_tree_node(easy3d::Model* model);
    const ModelTreeNodeInfo* model_tree_info(easy3d::Model* model) const;
    std::string model_tree_workspace_name(easy3d::Model* model) const;
    // Returns all unique workspace names, sorted.
    std::vector<std::string> workspace_names() const;
    // Returns models visible in tree under a workspace.
    std::vector<easy3d::Model*> workspace_models(const std::string& ws_name) const;

    // Camera
    easy3d::Camera* camera() { return camera_; }
    const easy3d::Camera* camera() const { return camera_; }
    void fit_screen(const easy3d::Model* model = nullptr);

    // Snapshot: save current FBO content to image file
    bool snapshot(const std::string& file_path, int w, int h, int samples, int background, bool expand);

    // Load a model file (thread-safe when Translator is DISABLED)
    static easy3d::Model* load_model_file(const std::string& file_name);

    // Clipboard: copy/paste camera state
    void copy_camera();
    void paste_camera();

    // Camera state save/restore
    void save_camera_state(const std::string& file_path);
    void restore_camera_state(const std::string& file_path);

    // Clear all models from the scene
    void clear_scene();

    // Background color
    const easy3d::vec4& background_color() const { return background_color_; }
    void set_background_color(const easy3d::vec4& c);

    // Mark scene as needing re-render (call after any change)
    void mark_dirty() { dirty_ = true; }
    bool is_dirty() const { return dirty_; }

    // Pick the screen-space-nearest mesh vertex under a viewport-local point.
    // This uses the same OpenGL viewport/FBO setup as the canvas selection tools,
    // so algorithm dialogs do not have to duplicate fragile picking state.
    bool pick_surface_vertex(easy3d::SurfaceMesh* mesh, int x, int y, int& vertex_id);

    // Selection system integration. Pointers are owned by MainWindow.
    void set_selection_state(SelectionMode* mode, SelectionManager* mgr) {
        selection_mode_ = mode; selection_manager_ = mgr;
    }
    void set_measurement_state(MeasurementState* ms) { measurement_state_ = ms; }
    void set_crop_state(CropState* cs) { crop_state_ = cs; }
    void set_align_state(struct AlignState* as) { align_state_ = as; }
    void set_walk_through(class WalkThrough* wt) { walk_through_ = wt; }

    // When set to true, handle_input() short-circuits: camera does not
    // react to any mouse / keyboard input. Used by tools that need
    // exclusive control of viewport mouse events (e.g. ARAP drag handle).
    // Always restore to false when the tool releases the mouse.
    bool input_locked_ = false;

    // OpenGL init
    void init_opengl();

private:
    // FBO management
    void create_fbo(int w, int h);
    void destroy_fbo();

    // Rendering stages
    std::uintptr_t render_scene_texture(int width, int height);
    void pre_draw();
    void draw_scene();
    void post_draw();

    // Mouse/Keyboard handling via ImGui IO
    void handle_input();
    void handle_mouse_press();
    void handle_mouse_release();
    void handle_mouse_drag();
    void handle_mouse_wheel();
    void handle_keyboard_shortcuts();
    void handle_walkthrough_keyframe();
    bool finish_rectangle_selection_on_release(int button);
    bool stop_active_gizmo_drag();
    void dispatch_viewport_release_click(bool is_left_click, bool is_right_click, int x, int y);
    bool dispatch_walkthrough_click(int x, int y);
    bool try_start_gizmo_drag_on_press(int button, int x, int y);
    bool update_rectangle_drag();
    void apply_camera_drag(float dx, float dy);

    // Corner axes
    void draw_corner_axes();

    int model_idx() const { return model_idx_; }

private:
    easy3d::Camera* camera_;
    easy3d::vec4 background_color_;

    std::vector<std::shared_ptr<easy3d::Model>> models_;
    int model_idx_;

    SelectedDrawableType selected_drawable_type_ = SelectedDrawableType::None;
    bool show_selection_bbox_ = true;
    bool show_axes_gizmo_ = true;
    bool selection_active_ = false;     // bbox visible only when user clicked a model
    bool selection_bbox_suppressed_ = false;

    // Overlay drawables (lazy-allocated, owned)
    easy3d::LinesDrawable* selection_bbox_drawable_ = nullptr;
    easy3d::Model* selection_bbox_last_model_ = nullptr;
public:
    void rebuild_selection_bbox(easy3d::Model* model);
private:
    void draw_selection_bbox();
    void draw_axes_gizmo();
    void try_pick_model(int x, int y);
    void try_pick_face(int x, int y);
    void try_deselect_face(int x, int y);
    void try_pick_vertex(int x, int y);
    void try_deselect_vertex(int x, int y);
    void try_pick_point(int x, int y);
    void try_deselect_point(int x, int y);
    void try_rect_pick_faces(const easy3d::Rect& rect);
    void try_rect_pick_points(const easy3d::Rect& rect, bool deselect);
    bool try_measurement_pick(int x, int y);

    // Selection state (owned by MainWindow)
    SelectionMode* selection_mode_ = nullptr;
    SelectionManager* selection_manager_ = nullptr;

    // Measurement state (owned by MainWindow)
    struct MeasurementState* measurement_state_ = nullptr;

    // Crop gizmo state (owned by MainWindow)
    struct CropState* crop_state_ = nullptr;
    bool try_crop_gizmo_pick(int x, int y);
    bool update_crop_gizmo_drag(int x, int y, float mouse_delta_x);

    // Align/Transform gizmo state (owned by MainWindow).
    // Lives only while the Transform tab is the active focus.
    struct AlignState* align_state_ = nullptr;
    class WalkThrough* walk_through_ = nullptr;
    bool try_align_gizmo_pick(int x, int y);
    bool update_align_gizmo_drag(int x, int y, float mouse_delta_x);

    // Rectangle selection state
    bool rect_dragging_ = false;
    float rect_start_x_ = 0, rect_start_y_ = 0;
    float rect_end_x_ = 0, rect_end_y_ = 0;

    // Model tree registry
    struct ModelHandleRecord {
        easy3d::Model* model = nullptr;
        std::uint64_t generation = 0;
    };
    ModelHandle assign_model_handle(easy3d::Model* model);
    void unregister_model_handle(easy3d::Model* model);
    std::uint64_t next_model_handle_id_ = 1;
    std::unordered_map<easy3d::Model*, ModelHandle> model_handles_;
    std::unordered_map<std::uint64_t, ModelHandleRecord> model_handle_records_;

    std::unordered_map<easy3d::Model*, ModelTreeNodeInfo> model_tree_registry_;
    std::set<std::string> workspace_set_;
    std::string unique_workspace_name(const std::string& desired,
                                      easy3d::Model* ignore = nullptr) const;
    void rebuild_workspace_set();

    // FBO
    unsigned int fbo_;
    unsigned int fbo_texture_;
    unsigned int fbo_depth_;
    int fbo_width_;
    int fbo_height_;

    // OpenGL
    bool opengl_initialized_;

    // High DPI
    float dpi_scaling_;

    // Corner axes
    easy3d::TrianglesDrawable* drawable_axes_;

    // Text renderer
    easy3d::TextRenderer* texter_;
    bool show_frame_rate_;

    // Dirty flag for on-demand rendering
    bool dirty_;

    // Input state
    int pressed_button_;   // 0=none, 1=left, 2=right, 3=middle
    int modifiers_;        // bitmask
    float mouse_x_, mouse_y_;
    float mouse_pressed_x_, mouse_pressed_y_;
    bool viewport_hovered_;
    bool viewport_focused_;

    // Viewport image screen rect (updated each frame after ImGui::Image).
    float viewport_min_x_ = 0, viewport_min_y_ = 0;
    float viewport_max_x_ = 0, viewport_max_y_ = 0;

public:
    float viewport_min_x() const { return viewport_min_x_; }
    float viewport_min_y() const { return viewport_min_y_; }
    float viewport_max_x() const { return viewport_max_x_; }
    float viewport_max_y() const { return viewport_max_y_; }
    bool is_in_viewport(float screen_x, float screen_y) const {
        return screen_x >= viewport_min_x_ && screen_x <= viewport_max_x_ &&
               screen_y >= viewport_min_y_ && screen_y <= viewport_max_y_;
    }

    // Runs `callback` with the OpenGL viewport matched to the docked viewport
    // panel size, then restores the previous OpenGL viewport. This keeps UI
    // panels from depending on OpenGL headers for one-shot picker operations.
    void run_with_panel_gl_viewport(int width, int height,
                                    const std::function<void()>& callback);
};

#endif // CLAW3D_VIEWPORT_CANVAS_H
