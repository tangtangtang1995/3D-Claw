// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "dialogs/3d_generation_dialog.h"
#include "dialogs/scope.h"
#include "dialogs/prerequisites.h"
#include "window/main_window.h"
#include "viewport/viewport_canvas.h"
#include "model/model_health.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "services/platform/platform_paths.h"
#include "ui/layout_helpers.h"

#include <easy3d/core/surface_mesh.h>
#include <easy3d/util/dialog.h>
#include <easy3d/util/file_system.h>
#include <easy3d/util/logging.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <GLFW/glfw3.h>

namespace {

void copy_cstr(char* dst, size_t dst_size, const std::string& text) {
    if (!dst || dst_size == 0)
        return;
    std::snprintf(dst, dst_size, "%s", text.c_str());
}

Gen3DStatus read_status(const Gen3DDialogState& state) {
    std::lock_guard<std::mutex> lock(state.status_mutex);
    return state.status;
}

std::string read_last_job_id(const Gen3DDialogState& state) {
    std::lock_guard<std::mutex> lock(state.status_mutex);
    return state.last_job_id;
}

std::string read_last_model_path(const Gen3DDialogState& state) {
    std::lock_guard<std::mutex> lock(state.status_mutex);
    return state.last_model_path;
}

void write_status(Gen3DDialogState* state,
                  int phase,
                  const std::string& message,
                  int credits = 0,
                  const std::string& job_id = std::string(),
                  const std::string& model_path = std::string()) {
    if (!state)
        return;
    std::lock_guard<std::mutex> lock(state->status_mutex);
    state->status.state = phase;
    state->status.credits_consumed = credits;
    copy_cstr(state->status.message, sizeof(state->status.message), message);
    if (!job_id.empty())
        copy_cstr(state->last_job_id, sizeof(state->last_job_id), job_id);
    if (!model_path.empty())
        copy_cstr(state->last_model_path, sizeof(state->last_model_path),
                  model_path);
}

int map_job_phase(claw_3dgen::GenerationJobPhase phase) {
    using claw_3dgen::GenerationJobPhase;
    switch (phase) {
    case GenerationJobPhase::Submitting:
        return GEN3D_Submitting;
    case GenerationJobPhase::Polling:
        return GEN3D_Polling;
    case GenerationJobPhase::Downloading:
        return GEN3D_Downloading;
    case GenerationJobPhase::Done:
        return GEN3D_Done;
    case GenerationJobPhase::Cancelled:
    case GenerationJobPhase::Error:
        return GEN3D_Error;
    }
    return GEN3D_Error;
}

claw_3dgen::Hunyuan3DRequest make_generation_request(
        const Gen3DDialogState& state) {
    claw_3dgen::Hunyuan3DRequest request;
    request.secret_id = state.secret_id;
    request.secret_key = state.secret_key;
    request.input_mode = state.input_mode == GEN3D_INPUT_Text ?
        claw_3dgen::HunyuanInputMode::Text :
        claw_3dgen::HunyuanInputMode::Image;
    request.image_path = state.image_path;
    request.text_prompt = state.text_prompt;
    request.generate_type = state.generate_type;
    request.enable_pbr = state.enable_pbr;
    request.face_count = state.face_count;
    return request;
}

} // namespace

void renderDialog3DGeneration(ViewportCanvas* viewer, Gen3DDialogState& s, bool& open) {
    // NOTE: do NOT call prepare_dialog_window() here. This panel is wired
    // into setup_dockspace_layout() (dock_ai_bottom) and defaults to open,
    // so it's treated as a permanent docked panel like AI Chat, not a
    // pop-up. A SetNextWindowPos call would override the dock position
    // and yank it to screen center on every launch.
    ImGui::SetNextWindowSizeConstraints(ImVec2(300, 200), ImVec2(FLT_MAX, FLT_MAX));
    DIALOG_BODY("AI 3D Generation (Hunyuan)", open) {
        auto* win = MainWindow::instance();
        Gen3DStatus current_status = read_status(s);
        const bool busy = current_status.state == GEN3D_Submitting ||
                          current_status.state == GEN3D_Polling ||
                          current_status.state == GEN3D_Downloading;

        if (!open && busy) {
            open = true;
            s.close_requested = true;
            s.cancelled.store(true);
            glfwPostEmptyEvent();
        }

        // Input mode
        ImGui::RadioButton("Image##inp", &s.input_mode, GEN3D_INPUT_Image);
        claw_ui::same_line_if_fits_text("Text##inp");
        ImGui::RadioButton("Text##inp", &s.input_mode, GEN3D_INPUT_Text);
        ImGui::Spacing();

        // Move labels above the field and use full-width inputs. The dock
        // for this panel is narrow (dock_ai_bottom), so the default ImGui
        // layout, with the label on the right of the field, gets the label clipped
        // off the visible area. Browse button gets its own line below the
        // input for the same reason.
        if (s.input_mode == GEN3D_INPUT_Image) {
            ImGui::TextUnformatted("Image Path");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##image_path", s.image_path, sizeof(s.image_path));
            if (ImGui::Button("Browse...##img", ImVec2(-FLT_MIN, 0))) {
                auto paths = easy3d::dialog::open("Select image",
                    easy3d::file_system::current_working_directory(),
                    {"Image Files (*.png *.jpg *.jpeg)", "*.png *.jpg *.jpeg"},
                    false);
                if (!paths.empty())
                    copy_cstr(s.image_path, sizeof(s.image_path), paths[0]);
            }
        } else {
            ImGui::TextUnformatted("Prompt");
            ImGui::InputTextMultiline("##prompt", s.text_prompt,
                sizeof(s.text_prompt), ImVec2(-FLT_MIN, 60));
        }
        ImGui::Spacing();

        // Options. Same narrow-dock treatment: labels on top, fields full
        // width. Combo / InputInt still need labels on the right by design,
        // but we cap item width so they don't push the label off-screen.
        if (ImGui::CollapsingHeader("Options")) {
            const char* gtypes[] = {"Normal", "LowPoly", "Geometry"};
            ImGui::TextUnformatted("Generate Type");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##gen_type", &s.generate_type, gtypes, 3);
            ImGui::Checkbox("Enable PBR (+10 credits)", &s.enable_pbr);
            ImGui::TextUnformatted("Face Count");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputInt("##face_count", &s.face_count, 10000, 50000);
            if (s.face_count < 3000) s.face_count = 3000;
            if (s.face_count > 1500000) s.face_count = 1500000;
        }
        ImGui::Spacing();

        // API credentials
        if (ImGui::CollapsingHeader("API Credentials")) {
            ImGui::TextUnformatted("SecretId");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##secret_id", s.secret_id, sizeof(s.secret_id));
            ImGui::TextUnformatted("SecretKey");
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##secret_key", s.secret_key,
                sizeof(s.secret_key), ImGuiInputTextFlags_Password);
        }
        ImGui::Spacing();

        // Auto-evaluate toggle. When on (default), the AI Chat panel will
        // automatically receive an evaluation prompt as soon as the new
        // mesh appears in the viewport, quoting the original input,
        // generation params, and mesh quality stats.
        ImGui::Checkbox("Auto-evaluate with AI on completion", &s.auto_evaluate);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "When enabled, the AI Chat will be sent a one-shot prompt\n"
                "asking it to judge the generated mesh given the original\n"
                "image / prompt + the mesh stats (vertices, faces, UV,\n"
                "manifoldness, etc). Requires an API key configured in\n"
                "the AI Chat panel.");
        }
        ImGui::Spacing();

        // Generate / Cancel
        if (!busy) {
            bool can_gen = s.secret_id[0] && s.secret_key[0] &&
                ((s.input_mode == GEN3D_INPUT_Text && s.text_prompt[0]) ||
                 (s.input_mode == GEN3D_INPUT_Image && s.image_path[0]));
            ImGui::BeginDisabled(!can_gen);
            if (ImGui::Button("Generate")) {
                write_status(&s, GEN3D_Submitting, "Submitting job...");
                s.close_requested = false;
                {
                    std::lock_guard<std::mutex> lock(s.status_mutex);
                    s.last_job_id[0] = '\0';
                    s.last_model_path[0] = '\0';
                }

                // Freeze the inputs that will be quoted by the auto-eval
                // prompt later, then clear the one-shot dispatch guard so
                // this job's completion will trigger exactly one AI eval.
                s.submitted_input_mode    = s.input_mode;
                s.submitted_generate_type = s.generate_type;
                s.submitted_enable_pbr    = s.enable_pbr;
                s.submitted_face_count    = s.face_count;
                copy_cstr(s.submitted_image_path, sizeof(s.submitted_image_path),
                          s.image_path);
                copy_cstr(s.submitted_text_prompt, sizeof(s.submitted_text_prompt),
                          s.text_prompt);
                s.ai_eval_done.store(false, std::memory_order_release);

                Gen3DDialogState* st_ptr = &s;
                claw3d::services::ThreeDGenerationJobStart job;
                job.request = make_generation_request(s);
                job.cancelled = &s.cancelled;
                job.on_status = [st_ptr](
                        const claw_3dgen::GenerationJobStatus& status) {
                        write_status(st_ptr, map_job_phase(status.phase),
                                     status.message,
                                     status.credits_consumed,
                                     status.job_id,
                                     status.model_path);
                };
                job.wake_ui = []() {
                    glfwPostEmptyEvent();
                };
                if (!win || !claw3d::services::start_three_d_generation_job(
                        win->algorithm_controller(), s.worker, job)) {
                    write_status(&s, GEN3D_Error, "Cannot start generation job");
                }
            }
            ImGui::EndDisabled();
            if (!s.secret_id[0] || !s.secret_key[0])
                ImGui::TextDisabled("Set SecretId/SecretKey in API Credentials");
        } else {
            ImGui::TextColored(claw_ui::status_success_color(),
                "%s", current_status.message);
            if (ImGui::Button("Cancel##gen")) {
                s.cancelled.store(true);
                glfwPostEmptyEvent();
            }
        }

        current_status = read_status(s);
        const std::string last_job_id = read_last_job_id(s);
        const std::string last_model_path = read_last_model_path(s);

        // Result display
        if (current_status.state == GEN3D_Done ||
            current_status.state == GEN3D_Error) {
            ImGui::Spacing();
            if (current_status.state == GEN3D_Done)
                ImGui::TextColored(claw_ui::status_success_color(),
                    "%s", current_status.message);
            else
                ImGui::TextColored(claw_ui::status_error_color(),
                    "%s", current_status.message);
            if (!last_job_id.empty())
                ImGui::TextDisabled("Job ID: %s", last_job_id.c_str());
            if (!last_model_path.empty()) {
                const std::string output_dir =
                    easy3d::file_system::parent_directory(last_model_path);
                ImGui::Spacing();
                ImGui::TextUnformatted("Output Folder");
                ImGui::TextWrapped("%s", output_dir.c_str());
                ImGui::TextUnformatted("Model File");
                ImGui::TextWrapped("%s", last_model_path.c_str());
                if (ImGui::Button("Open Folder")) {
                    if (!output_dir.empty() &&
                        !claw3d::platform::open_directory(output_dir)) {
                        LOG(WARNING) << "Failed to open generated model folder: "
                                     << output_dir;
                    }
                }
                claw_ui::same_line_if_fits_button("Copy Model Path");
                if (ImGui::Button("Copy Model Path"))
                    ImGui::SetClipboardText(last_model_path.c_str());
                claw_ui::same_line_if_fits_button("Copy Folder");
                if (ImGui::Button("Copy Folder"))
                    ImGui::SetClipboardText(output_dir.c_str());
            }
            if (ImGui::Button("Close##done")) open = false;
        }

        // Auto-evaluation dispatch. Runs every frame after Done, but the
        // ai_eval_done one-shot guard ensures we only fire the request once
        // per generation. We wait until the new mesh is actually visible
        // in the scene (the worker pushes through AlgorithmController, the main thread
        // then add_model()s it on a subsequent frame). When found, we make
        // it current so AICtx_CurrentModel picks it up automatically.
        if (current_status.state == GEN3D_Done && s.auto_evaluate &&
            !s.ai_eval_done.load(std::memory_order_acquire) &&
            win && win->ai_chat() && win->ai_chat()->HasApiKey() &&
            !last_job_id.empty())
        {
            // Locate the generated mesh by name ("gen3d.<jobId>"). If it is
            // not yet in viewer->models() this frame, skip and retry next.
            const std::string expected_name =
                std::string("gen3d.") + last_job_id;
            // sanitize_path_component(job_id) may strip chars from the
            // job_id used in the actual mesh name, so match by prefix
            // tolerantly: any model whose name starts with "gen3d." and
            // contains last_job_id is OK.
            easy3d::Model* generated = nullptr;
            if (viewer) {
                for (const auto& msp : viewer->models()) {
                    auto* m = msp.get();
                    const std::string& nm = m->name();
                    if (nm.rfind("gen3d.", 0) == 0 &&
                        (nm == expected_name ||
                         nm.find(last_job_id) != std::string::npos)) {
                        generated = m;
                        break;
                    }
                }
            }
            if (generated) {
                viewer->set_current_model(generated);
                // Kick off async health analysis so AICtx_CurrentModel can
                // pick up manifoldness / watertight / boundary stats by the
                // time the AI starts streaming its reply (a few seconds).
                ModelHealthRegistry::instance().request_compute(generated);

                // Build a stats sketch directly off the mesh so the prompt
                // includes immediate info even if the health analyzer
                // hasn't finished yet.
                std::ostringstream ctx;
                ctx << "[Generation Input]\n";
                ctx << "- mode: "
                    << (s.submitted_input_mode == GEN3D_INPUT_Text ? "Text" : "Image")
                    << "\n";
                if (s.submitted_input_mode == GEN3D_INPUT_Text) {
                    ctx << "- prompt: \""
                        << s.submitted_text_prompt << "\"\n";
                } else {
                    ctx << "- image_path: " << s.submitted_image_path << "\n";
                }
                const char* gtypes[] = {"Normal", "LowPoly", "Geometry"};
                int gt = s.submitted_generate_type;
                if (gt < 0 || gt > 2) gt = 0;
                ctx << "- generate_type: " << gtypes[gt] << "\n"
                    << "- enable_pbr: " << (s.submitted_enable_pbr ? "yes" : "no") << "\n"
                    << "- requested_face_count: " << s.submitted_face_count << "\n";
                ctx << "- credits_consumed: "
                    << current_status.credits_consumed << "\n";

                ctx << "\n[Generated Mesh Stats]\n";
                ctx << "- name: " << generated->name() << "\n";
                if (auto* sm = dynamic_cast<easy3d::SurfaceMesh*>(generated)) {
                    ctx << "- vertices: " << sm->n_vertices() << "\n";
                    ctx << "- faces: "    << sm->n_faces()    << "\n";
                    ctx << "- edges: "    << sm->n_edges()    << "\n";
                    ctx << "- triangle_mesh: "
                        << (sm->is_triangle_mesh() ? "yes" : "no") << "\n";
                    ctx << "- closed: "
                        << (sm->is_closed() ? "yes" : "no") << "\n";
                    bool has_n = sm->get_vertex_property<easy3d::vec3>("v:normal");
                    bool has_uv =
                        sm->get_halfedge_property<easy3d::vec2>("h:texcoord") ||
                        sm->get_vertex_property<easy3d::vec2>("v:texcoord");
                    bool has_vcol = sm->get_vertex_property<easy3d::vec3>("v:color");
                    bool has_fcol = sm->get_face_property<easy3d::vec3>("f:color");
                    ctx << "- vertex_normals: " << (has_n   ? "yes" : "no") << "\n"
                        << "- uv: "             << (has_uv  ? "yes" : "no") << "\n"
                        << "- vertex_color: "   << (has_vcol? "yes" : "no") << "\n"
                        << "- face_color: "     << (has_fcol? "yes" : "no") << "\n";
                    auto tex = sm->get_model_property<std::string>("m:texture");
                    if (tex) {
                        ctx << "- texture_file: " << tex[0] << "\n";
                    }
                }
                if (generated->bounding_box().is_valid()) {
                    ctx << "- bbox_diagonal: "
                        << generated->bounding_box().diagonal_length() << "\n";
                }
                ctx << "\nNote: a full Health Report (manifoldness, boundary "
                       "edges, components) was just requested; the [Current "
                       "Model] block injected above may already include those "
                       "numbers depending on timing.\n";

                std::string prompt = std::string(
                    "The AI 3D Generation panel just finished a Tencent "
                    "Hunyuan3D job. Evaluate the result for the user.\n\n"
                    "Cover:\n"
                    "1. **Faithfulness** - does the mesh plausibly match the "
                    "image / prompt above?\n"
                    "2. **Mesh quality** - vertex/face count vs. requested, "
                    "triangulation, watertightness, manifoldness, presence of "
                    "UV / normals / colors / texture.\n"
                    "3. **Recommended next operations in 3D Claw** - for "
                    "example Surface > Hole Filling if non-watertight, "
                    "ACVD / CGAL Simplification if too dense, Parameterization "
                    "if UV missing, etc. Name the actual menu / panel.\n"
                    "4. **Warnings**, if any (degenerate faces, multiple "
                    "components, missing texture file, etc.).\n\n"
                    "Keep the tone practical (~250 words). ")
                    + ai_lang::directive();

                win->send_ai_request(
                    prompt,
                    AICtx_CurrentModel | AICtx_ActivePanel,
                    ctx.str(),
                    "Auto-eval: AI-generated 3D model");

                s.ai_eval_done.store(true, std::memory_order_release);
            }
        }
    } DIALOG_END;
}
