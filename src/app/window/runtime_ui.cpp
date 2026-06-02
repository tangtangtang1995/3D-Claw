// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// Runtime welcome and status overlay UI.

#include "window/main_window.h"

#include "ai/ai_chat.h"
#include "ai/ai_language.h"
#include "product_identity.h"
#include "ui/layout_helpers.h"

#include <easy3d/util/logging.h>

#include "imgui.h"

#include <cstring>
#include <string>

void MainWindow::render_welcome_dialog() {
    auto* chat = ai_chat();
    if (show_welcome_api_dialog_ && chat && !welcome_buf_seeded_) {
        if (chat->HasApiKey()) {
            std::strncpy(welcome_api_key_buf_,
                         chat->GetApiKey().c_str(),
                         sizeof(welcome_api_key_buf_) - 1);
            welcome_api_key_buf_[sizeof(welcome_api_key_buf_) - 1] = '\0';
        }
        welcome_buf_seeded_ = true;
    }
    if (show_welcome_api_dialog_ && chat) {
        ImGui::OpenPopup("Welcome##FirstLaunch");
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    }
    if (ImGui::BeginPopupModal("Welcome##FirstLaunch", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool had_key = chat && chat->HasApiKey();
        if (had_key) {
            ImGui::TextWrapped(
                "Welcome back to 3D Claw!\n\n"
                "Your saved API key and language preference were loaded "
                "from 3DClaw_config.json. You can edit them below, or "
                "just click Activate to continue.");
        } else {
            ImGui::TextWrapped(
                "Welcome to 3D Claw!\n\n"
                "To enable AI-assisted features (model advice, parameter "
                "suggestions, health analysis), enter your DeepSeek API key.\n"
                "You can skip this and configure it later in the AI Chat panel.");
        }
        ImGui::Spacing();
        ImGui::InputText("API Key", welcome_api_key_buf_,
                         sizeof(welcome_api_key_buf_),
                         ImGuiInputTextFlags_Password);
        if (had_key) {
            ImGui::TextColored(claw_ui::status_success_color(),
                "(loaded from 3DClaw_config.json)");
        }

        ImGui::Spacing();
        const char* lang_options[] = {
            ai_lang::display_name(ai_lang::Language::English),
            ai_lang::display_name(ai_lang::Language::Chinese),
        };
        int cur = (ai_lang::current() == ai_lang::Language::Chinese) ? 1 : 0;
        if (ImGui::Combo("AI Reply Language", &cur, lang_options, 2)) {
            ai_lang::set(cur == 1 ? ai_lang::Language::Chinese
                                  : ai_lang::Language::English);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Language the AI uses to answer your questions.\n"
                              "The UI itself stays English. Change later via\n"
                              "the AI Chat panel header.");

        ImGui::Spacing();
        if (ImGui::Button("Activate AI Chat")) {
            if (welcome_api_key_buf_[0]) {
                const std::string prev_key = had_key ? chat->GetApiKey()
                                                     : std::string();
                const std::string new_key  = welcome_api_key_buf_;
                chat->SetApiKey(new_key);
                dlg_ai_ = true;
                if (prev_key != new_key) {
                    std::string intro =
                        std::string("You are now the AI assistant inside 3D Claw. ") +
                        "Start by introducing the main panel layout:\n"
                        "- Left side: Model List (model tree, all loaded models) "
                        "and the bottom-left tab group: Properties (model stats/attributes), "
                        "Health Report (model quality findings), Log (warnings/errors), "
                        "History (operation timeline).\n"
                        "- Center: 3D Viewport for interactive model viewing.\n"
                        "- Right side: AI Chat (this panel) and AI 3D Generation.\n"
                        "After describing the layout, briefly explain what each panel "
                        "does and how the user can interact with it. Keep the tone "
                        "welcoming and practical. Mention that hovering rows in Model "
                        "List, Properties, Health Report, and History shows a blue "
                        "'?' button to ask context-aware AI questions.\n\n"
                        "Total: ~300 words. " + ai_lang::directive();
                    chat->SendUserMessage(intro,
                        "Introduce 3D Claw's panel layout");
                }
                LOG(INFO) << "AI Chat activated via welcome dialog, language="
                          << ai_lang::to_id(ai_lang::current())
                          << (prev_key == new_key ? " (key unchanged)" : " (new key)");
            }
            show_welcome_api_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        claw_ui::same_line_if_fits_button("Skip");
        if (ImGui::Button("Skip")) {
            show_welcome_api_dialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MainWindow::render_runtime_status_overlays() {
    if (file_loader_.is_busy()) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(320, 100), ImGuiCond_Always);
        ImGui::Begin("##loading_overlay", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings);
        static int dots = 0;
        static float timer = 0;
        timer += ImGui::GetIO().DeltaTime;
        if (timer > 0.3f) {
            timer = 0;
            dots = (dots + 1) % 4;
        }
        const bool uploading = file_loader_.is_uploading();
        const char* label = uploading ? "Uploading to GPU"
                                      : "Loading file";
        ImGui::SetCursorPosX(40);
        ImGui::Text("%s%.*s", label, dots, "....");
        if (uploading) {
            ImGui::SetCursorPosX(40);
            ImGui::TextDisabled("(main thread blocks briefly)");
        }
        ImGui::End();
    }
    if (algorithm_controller().is_running()) {
        static int dots = 0;
        static float timer = 0;
        timer += ImGui::GetIO().DeltaTime;
        if (timer > 0.3f) {
            timer = 0;
            dots = (dots + 1) % 4;
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 pos(vp->WorkPos.x + 14.0f,
                         vp->WorkPos.y + vp->WorkSize.y - 14.0f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("##algorithm_status_toast", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(claw_ui::status_running_color(),
                           "%s: %s%.*s",
                           algorithm_controller().state_label(),
                           algorithm_controller().current_label().c_str(),
                           dots, "....");
        ImGui::End();
    }
}
