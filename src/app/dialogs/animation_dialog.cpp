// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/animation_dialog.h"

#include <easy3d/core/model.h>
#include <easy3d/renderer/camera.h>
#include <easy3d/renderer/manipulated_camera_frame.h>
#include <easy3d/renderer/key_frame_interpolator.h>
#include <easy3d/renderer/frame.h>
#include <easy3d/util/logging.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/dialog.h>

#include <cstring>
#include <cstdio>

#include "viewport/viewport_canvas.h"
#include "window/main_window.h"
#include "ui/walk_through.h"
#include "ui/layout_helpers.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"


// =====================================================================
// Dialog
// =====================================================================

void renderDialogAnimation(ViewportCanvas* viewer, MainWindow* win,
                           AnimationState& s, bool& open) {
    auto* wt = win->walk_through();
    auto* kfi = wt ? wt->interpolator() : nullptr;
    auto* camera = viewer ? viewer->camera() : nullptr;

    prepare_dialog_window(560, 700);
    DIALOG_BODY("Animation / Walk Through", open) {
        if (!wt || !kfi || !camera) {
            ImGui::TextDisabled("Internal error: no WalkThrough/KeyFrameInterpolator.");
            return;
        }

        // Force the WalkThrough's status to match the dialog's current mode
        // every frame. The constructor leaves status=STOPPED, so without this
        // the K-key handler in the viewport canvas would never see FREE_MODE on
        // dialog open and silently drop keypresses.
        {
            WalkThrough::Status desired =
                (s.mode == AnimationMode::Free)    ? WalkThrough::FREE_MODE :
                (s.mode == AnimationMode::Walking) ? WalkThrough::WALKING_MODE :
                                                     WalkThrough::ROTATE_AROUND_AXIS;
            if (wt->status() != desired)
                wt->set_status(desired);
        }

        // On the very first frame after open, enable Show Path / Show Cameras
        // so the user sees visual feedback after pressing K. Subsequent
        // user toggles are respected because we only do this once.
        if (s.first_render) {
            s.first_render = false;
            wt->set_path_visible(true);
            wt->set_cameras_visible(true);
            // Initialize scene_box_ so generate_camera_path() / character_height()
            // have valid geometry to work with. Without this, Rotate Around Axis
            // would collapse the camera frustum to radius=0 and nothing renders.
            if (viewer) wt->set_scene(viewer->models());
            // Seed a sensible default output path if user has not picked one.
            if (s.output_file[0] == 0) {
                std::string def = "frames.png";
                if (viewer && viewer->current_model()) {
                    def = easy3d::file_system::replace_extension(
                              viewer->current_model()->name(), "png");
                }
                std::strncpy(s.output_file, def.c_str(), sizeof(s.output_file) - 1);
                s.output_file[sizeof(s.output_file) - 1] = 0;
            }
            viewer->mark_dirty();
        }

        // ---- Mode ----
        const char* mode_names[] = {"Free", "Walking", "Rotate Around Axis"};
        int cur_mode = (int)s.mode;
        if (ImGui::Combo("Mode", &cur_mode, mode_names, 3)) {
            s.mode = (AnimationMode)cur_mode;
            wt->set_status(s.mode == AnimationMode::Free ? WalkThrough::FREE_MODE
                         : s.mode == AnimationMode::Walking ? WalkThrough::WALKING_MODE
                         : WalkThrough::ROTATE_AROUND_AXIS);
            viewer->mark_dirty();
        }

        ImGui::Separator();

        // ---- Mode-specific controls ----
        if (s.mode == AnimationMode::Free) {
            ImGui::Text("K: record current viewpoint as a keyframe");
            ImGui::TextDisabled("The camera path will pass through all keyframes.");
        } else if (s.mode == AnimationMode::Walking) {
            ImGui::Text("Alt + Click on model: walk to point (add keyframe)");
            ImGui::TextDisabled("Simulates a character walking through the scene.");
            bool fu = s.follow_up;
            if (ImGui::Checkbox("Follow Up", &fu)) {
                s.follow_up = fu;
                wt->set_follow_up(fu);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Automatically move the camera to show the latest keyframe.");
            float hf = s.height_factor;
            if (ImGui::DragFloat("Character Height Factor", &hf, 0.01f, 0.01f, 10.0f)) {
                s.height_factor = hf;
                wt->set_height_factor(hf);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Character height as a fraction of scene height.");
            float ff = s.forward_factor;
            if (ImGui::DragFloat("Forward Distance Factor", &ff, 0.1f, 0.1f, 50.0f)) {
                s.forward_factor = ff;
                wt->set_third_person_forward_factor(ff);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("3rd-person forward distance as a multiple of character height.");
        } else { // Rotate Around Axis
            float zo = s.zoom_out_factor;
            if (ImGui::DragFloat("Zoom Out Factor", &zo, 0.01f, -10.0f, 10.0f)) {
                s.zoom_out_factor = zo;
                wt->set_zoom_out_factor(zo, true);
            }
            float vo = s.vert_offset_factor;
            if (ImGui::DragFloat("Vert Offset Factor", &vo, 0.01f, -10.0f, 10.0f)) {
                s.vert_offset_factor = vo;
                wt->set_vertical_offset_factor(vo, true);
            }
            float pa = s.pitch_angle;
            if (ImGui::DragFloat("Pitch Angle (deg)", &pa, 0.5f, -90.0f, 90.0f)) {
                s.pitch_angle = pa;
                wt->set_pitch_angle(pa, true);
            }
            int ks = s.keyframe_samples;
            if (ImGui::DragInt("Keyframes Per Loop", &ks, 1, 2, 360)) {
                s.keyframe_samples = ks;
                wt->set_keyframe_samples(ks, true);
            }
            int nl = s.num_loops;
            if (ImGui::DragInt("Number of Loops", &nl, 1, 1, 100)) {
                s.num_loops = nl;
                wt->set_num_loops(nl, true);
            }
        }

        ImGui::Separator();

        // ---- Path management ----
        int nk = (int)kfi->number_of_keyframes();
        ImGui::Text("Keyframes: %d", nk);

        int cur_kf = wt->current_keyframe_index();
        int slider_val = cur_kf >= 0 ? cur_kf : 0;
        if (nk > 1) {
            if (ImGui::SliderInt("##kf_slider", &slider_val, 0, nk - 1)) {
                wt->move_to(slider_val, false);
                viewer->mark_dirty();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::SliderInt("##kf_slider", &slider_val, 0, 0);
            ImGui::EndDisabled();
        }

        if (ImGui::SmallButton("< Prev")) {
            int pos = wt->current_keyframe_index();
            wt->move_to(pos <= 0 ? 0 : pos - 1);
            viewer->mark_dirty();
        }
        claw_ui::same_line_if_fits_button("Next >");
        if (ImGui::SmallButton("Next >")) {
            int pos = wt->current_keyframe_index();
            if (pos >= 0 && pos >= nk - 1) wt->move_to(nk - 1);
            else wt->move_to(pos + 1);
            viewer->mark_dirty();
        }
        claw_ui::same_line_if_fits_button("Remove Last");
        if (ImGui::SmallButton("Remove Last")) {
            if (nk > 0) {
                wt->delete_last_keyframe();
                viewer->mark_dirty();
            }
        }
        claw_ui::same_line_if_fits_button("Clear Path");
        if (ImGui::Button("Clear Path")) {
            if (nk > 0) {
                wt->delete_path();
                viewer->mark_dirty();
                LOG(INFO) << "camera path cleared";
            }
        }

        ImGui::Separator();

        // ---- Interpolation settings ----
        const char* interp_names[] = {"Spline Interpolation", "Spline Fitting"};
        int im = s.interp_method;
        if (ImGui::Combo("Method", &im, interp_names, 2)) {
            s.interp_method = im;
            kfi->set_interpolation_method(im == 0
                ? easy3d::KeyFrameInterpolator::INTERPOLATION
                : easy3d::KeyFrameInterpolator::FITTING);
        }
        float spd = s.interp_speed;
        if (ImGui::DragFloat("Speed", &spd, 0.1f, 0.01f, 100.0f)) {
            s.interp_speed = spd;
            kfi->set_interpolation_speed(spd);
        }
        int fps = s.frame_rate;
        if (ImGui::DragInt("Frame Rate", &fps, 1, 1, 120)) {
            s.frame_rate = fps;
            kfi->set_frame_rate(fps);
        }

        ImGui::Separator();

        // ---- Preview / Record ----
        bool is_playing = kfi->is_interpolation_started();
        if (is_playing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        }
        if (ImGui::Button(is_playing ? "Stop" : "Preview")) {
            if (is_playing) {
                kfi->stop_interpolation();
            } else {
                if (nk == 0) {
                    LOG(WARNING) << "nothing to preview (camera path is empty)";
                } else {
                    wt->set_scene(viewer->models());
                    kfi->start_interpolation();
                    LOG(INFO) << "preview started";
                }
            }
        }
        if (is_playing)
            ImGui::PopStyleColor();

        claw_ui::same_line_if_fits_button(s.rec_active ? "Cancel" : "Record");
        // Record writes a PNG sequence one frame per ImGui tick so the UI
        // stays responsive (synchronous capture froze for ~10s on a typical
        // 5-second animation). easy3d::video is compiled out
        // (backend FFMPEG support is disabled) so MP4 is not available; stitch the
        // PNG frames externally with ffmpeg if you need an mp4.
        const bool can_record = (nk > 0) && (s.output_file[0] != 0)
                                && !kfi->is_interpolation_started()
                                && !s.rec_active;
        if (s.rec_active) {
            // Recording in progress; button becomes Cancel.
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Cancel")) s.rec_cancel = true;
            ImGui::PopStyleColor();
        } else {
            if (!can_record) ImGui::BeginDisabled();
            if (ImGui::Button("Record")) {
                // Make sure scene radius is up to date so the path doesn't
                // get clipped by the camera's near/far planes.
                wt->set_scene(viewer->models());

                // Snapshot the interpolated path so it survives the state
                // machine (kfi->interpolate() returns a reference into the
                // KFI's internal cache that may be invalidated later).
                const auto& path = kfi->interpolate();
                if (path.empty()) {
                    LOG(WARNING) << "interpolation produced no frames";
                } else {
                    s.rec_frames.assign(path.begin(), path.end());
                    s.rec_stem = easy3d::file_system::name_less_extension(s.output_file);
                    s.rec_ext  = easy3d::file_system::extension(s.output_file);
                    if (s.rec_ext.empty()) s.rec_ext = "png";
                    s.rec_w = (int)camera->screenWidth();
                    s.rec_h = (int)camera->screenHeight();
                    s.rec_frame = 0; s.rec_ok = 0; s.rec_fail = 0;
                    s.rec_cancel = false;

                    // Hide path / cameras overlays while capturing; we don't
                    // want the yellow guide line baked into the frames.
                    s.rec_path_was_visible = wt->path_visible();
                    s.rec_cams_was_visible = wt->cameras_visible();
                    wt->set_path_visible(false);
                    wt->set_cameras_visible(false);

                    s.rec_active = true;
                    LOG(INFO) << "recording started: " << s.rec_frames.size()
                              << " frames at " << s.rec_w << "x" << s.rec_h
                              << " -> " << s.rec_stem << "_NNNN." << s.rec_ext;
                    viewer->mark_dirty();
                }
            }
            if (!can_record) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) {
                if (nk == 0)
                    ImGui::SetTooltip("Add keyframes first (press K to add).");
                else if (s.output_file[0] == 0)
                    ImGui::SetTooltip("Choose an output file via 'Browse...' first.");
                else
                    ImGui::SetTooltip("Render the interpolated path as a PNG sequence.\n"
                                      "One frame is captured per ImGui tick, so the UI\n"
                                      "stays responsive and you can hit Cancel anytime.\n"
                                      "(FFMPEG is disabled in this build; stitch the PNGs\n"
                                      "with ffmpeg externally for MP4 output.)");
            }
        }

        // Drive the recording state machine: one snapshot per dialog tick.
        // We do this AFTER the button so an in-progress recording continues
        // while the user can still see/hit Cancel.
        if (s.rec_active) {
            const int total = (int)s.rec_frames.size();
            if (s.rec_cancel || s.rec_frame >= total) {
                wt->set_path_visible(s.rec_path_was_visible);
                wt->set_cameras_visible(s.rec_cams_was_visible);
                LOG(INFO) << "recording " << (s.rec_cancel ? "cancelled" : "finished")
                          << ": " << s.rec_ok << " ok, " << s.rec_fail << " failed";
                s.rec_active = false;
                s.rec_cancel = false;
                s.rec_frames.clear();
                viewer->mark_dirty();
            } else {
                const auto& f = s.rec_frames[s.rec_frame];
                camera->frame()->setPositionAndOrientation(f.position(), f.orientation());
                char fname[768];
                std::snprintf(fname, sizeof fname, "%s_%04d.%s",
                              s.rec_stem.c_str(), s.rec_frame, s.rec_ext.c_str());
                if (viewer->snapshot(fname, s.rec_w, s.rec_h, 0, 0, false))
                    ++s.rec_ok;
                else
                    ++s.rec_fail;
                ++s.rec_frame;
                viewer->mark_dirty(); // keep the loop spinning at vsync
            }
        }
        // Progress bar appears in place of the disabled controls during
        // recording. Helps the user understand the dialog is alive.
        if (s.rec_active) {
            const float prog = s.rec_frames.empty() ? 0.0f
                : (float)s.rec_frame / (float)s.rec_frames.size();
            char overlay[64];
            std::snprintf(overlay, sizeof overlay, "%d / %d",
                          s.rec_frame, (int)s.rec_frames.size());
            ImGui::ProgressBar(prog, ImVec2(-FLT_MIN, 0.0f), overlay);
        }

        claw_ui::same_line_if_fits_text("Show Path");
        bool show_path = wt->path_visible();
        if (ImGui::Checkbox("Show Path", &show_path)) {
            wt->set_path_visible(show_path);
            viewer->mark_dirty();
        }
        claw_ui::same_line_if_fits_text("Show Cameras");
        bool show_cam = wt->cameras_visible();
        if (ImGui::Checkbox("Show Cameras", &show_cam)) {
            wt->set_cameras_visible(show_cam);
            viewer->mark_dirty();
        }

        // ---- Output file ----
        const float browse_w = 90.0f;
        const float row_avail = ImGui::GetContentRegionAvail().x;
        float output_input_w = row_avail - browse_w - 70.0f;
        if (output_input_w < 160.0f)
            output_input_w = row_avail;
        ImGui::PushItemWidth(output_input_w);
        ImGui::InputText("##output_file", s.output_file, sizeof(s.output_file));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Output file. Recording writes <stem>_0000.png,\n"
                              "<stem>_0001.png, ... in the same directory.");
        ImGui::PopItemWidth();
        claw_ui::same_line_if_fits_width(browse_w);
        if (ImGui::Button("Browse...", ImVec2(browse_w, 0))) {
            std::string default_name = s.output_file[0] ? s.output_file : std::string("frames.png");
            auto picked = easy3d::dialog::save(
                "Choose output file",
                default_name,
                { "PNG (*.png)",   "*.png",
                  "JPEG (*.jpg)",  "*.jpg",
                  "All Files",     "*"  });
            if (!picked.empty()) {
                std::strncpy(s.output_file, picked.c_str(), sizeof(s.output_file) - 1);
                s.output_file[sizeof(s.output_file) - 1] = 0;
            }
        }
        claw_ui::same_line_if_fits_text("Output");
        ImGui::TextUnformatted("Output");

        ImGui::Separator();
        if (ImGui::Button("Close")) open = false;
    } DIALOG_END;
}
