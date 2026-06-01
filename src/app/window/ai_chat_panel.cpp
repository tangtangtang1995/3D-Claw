// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

// AI Chat panel rendering + send_ai_request + the file-static markdown
// helpers used to format AI messages. Extracted out of main_window.cpp
// during the 2024-2025 file split.

#include "window/main_window.h"
#include "ai/ai_chat.h"
#include "ai/ai_context.h"
#include "ai/ai_language.h"
#include "ui/layout_helpers.h"

#include "imgui.h"
#include "imgui_md.h"

#include <algorithm>   // std::count
#include <cfloat>      // FLT_MIN
#include <cstdio>
#include <cstring>
#include <string>


// -----------------------------------------------------------------------------
// Chat message rendering  - md4c + imgui_md for AI, InputTextMultiline for user.
//
// AI messages: parsed by md4c and drawn by imgui_md, so headings / bold /
//   italic / inline + fenced code / ordered + nested lists / tables / blockquotes
//   / strikethrough / links all render with real structure. imgui_md would
//   normally switch font faces for bold / italic, but our single CJK font has no
//   such variants, so the ChatMarkdown subclass below encodes structure with
//   a light-theme semantic palette. Drag-selection is NOT supported inside the
//   rendered Markdown
//   (it is TextUnformatted underneath); use the per-message "Copy" button to
//   grab the original text.
//
// User messages: rendered with ReadOnly InputTextMultiline so the user can
//   drag-select / Ctrl+C their own input naturally.
// -----------------------------------------------------------------------------

namespace {

class ScopedFontGlobalScale {
public:
    ScopedFontGlobalScale(ImGuiIO& io, float multiplier)
        : io_(io), saved_(io.FontGlobalScale) {
        io_.FontGlobalScale = saved_ * multiplier;
    }

    ~ScopedFontGlobalScale() {
        io_.FontGlobalScale = saved_;
    }

private:
    ImGuiIO& io_;
    float saved_;
};

// Markdown renderer for AI chat replies, built on md4c (parser) + imgui_md
// (Dear ImGui renderer). imgui_md normally distinguishes structure by swapping
// bold / italic / monospace font faces; this app loads a single CJK font
// (Microsoft YaHei) with no such variants, so we keep the one font and encode
// structure as color.
class ChatMarkdown : public imgui_md {
public:
    // Shadows the non-virtual imgui_md::print() so we can reset our per-document
    // table scratch state before each reply is parsed. chat_markdown() hands out
    // a ChatMarkdown&, so callers always resolve to this overload.
    int print(const char* str, const char* str_end) {
        md_table_id_ = 0;
        md_table_active_ = false;
        md_in_table_header_ = false;
        return imgui_md::print(str, str_end);
    }

protected:
    // Keep the single CJK font for every span; never fall back to imgui_md's
    // default (Latin-only) font, which would drop CJK glyphs.
    ImFont* get_font() const override { return ImGui::GetFont(); }

    // imgui_md only routes link text through get_color(); every other element
    // is colored by the ImGuiCol_Text pushed in the block / span overrides.
    ImVec4 get_color() const override {
        if (!m_href.empty())
            return ImVec4(0.12f, 0.39f, 0.68f, 1.0f);
        return ImGui::GetStyle().Colors[ImGuiCol_Text];
    }

    // No inline images in chat. Returning false suppresses imgui_md's built-in
    // placeholder, which would otherwise draw the font atlas as a broken image.
    bool get_image(image_info&) const override { return false; }

    void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool enter) override {
        if (enter) {
            m_hlevel = d->level;
            ImGui::NewLine();
            ImGui::PushStyleColor(ImGuiCol_Text, heading_color(d->level));
        } else {
            ImGui::PopStyleColor();
            m_hlevel = 0;
            if (d->level <= 2) {
                ImGui::NewLine();
                ImGui::Separator();
            }
        }
    }

    void SPAN_EM(bool enter) override {        // italic
        m_is_em = enter;
        push_text_color(enter, ImVec4(0.16f, 0.38f, 0.64f, 1.0f));
    }

    void SPAN_STRONG(bool enter) override {    // bold
        m_is_strong = enter;
        push_text_color(enter, ImVec4(0.50f, 0.31f, 0.03f, 1.0f));
    }

    void SPAN_CODE(bool enter) override {      // inline `code`
        push_text_color(enter, code_color());
    }

    void BLOCK_CODE(const MD_BLOCK_CODE_DETAIL*, bool enter) override { // fenced
        m_is_code = enter;
        push_text_color(enter, code_color());
    }

    // Tables. imgui_md draws tables by hand with absolute cursor positioning, so
    // every column ends up as wide as its header cell text and never adapts to
    // the body content or the available width. Re-implement the six table
    // callbacks on ImGui's native Tables API instead: SizingStretchProp sizes
    // columns proportionally to their content and fills the panel width, and
    // Resizable lets the user drag the dividers.
    //
    // We deliberately leave m_is_table_header / m_is_table_body false (the base
    // class sets them only to drive its manual layout). With both clear, the
    // inherited render_text() wraps each cell to GetContentRegionAvail().x, which
    // inside a native column is that column's width - exactly what we want.
    void BLOCK_TABLE(const MD_BLOCK_TABLE_DETAIL* d, bool enter) override {
        if (enter) {
            // Unique, frame-stable id per table so two tables in one message do
            // not share column state (and so Resizable widths persist).
            ImGui::PushID(md_table_id_++);
            md_table_active_ = ImGui::BeginTable(
                "md_table", (int)d->col_count,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable);
            if (!md_table_active_)
                ImGui::PopID();   // BeginTable failed: nothing to balance later
        } else if (md_table_active_) {
            ImGui::EndTable();
            ImGui::PopID();
            md_table_active_ = false;
        }
    }

    void BLOCK_THEAD(bool enter) override {
        md_in_table_header_ = enter;   // drives the header cell background only
    }

    void BLOCK_TBODY(bool) override {}  // native tables need no body bookkeeping

    void BLOCK_TR(bool enter) override {
        if (md_table_active_ && enter)
            ImGui::TableNextRow();
    }

    void BLOCK_TH(const MD_BLOCK_TD_DETAIL* d, bool enter) override {
        BLOCK_TD(d, enter);
    }

    void BLOCK_TD(const MD_BLOCK_TD_DETAIL*, bool enter) override {
        if (!md_table_active_ || !enter) return;
        ImGui::TableNextColumn();
        if (md_in_table_header_)
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                   ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
    }

    // imgui_md leaves a soft break (single source newline) as a no-op, which
    // glues the surrounding words together. Emit a space so paragraphs flow.
    void soft_break() override {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(" ");
        ImGui::SameLine(0.0f, 0.0f);
    }

private:
    static ImVec4 code_color() { return ImVec4(0.07f, 0.43f, 0.31f, 1.0f); }

    static ImVec4 heading_color(unsigned level) {
        if (level <= 1) return ImVec4(0.44f, 0.27f, 0.02f, 1.0f);
        if (level == 2) return ImVec4(0.54f, 0.27f, 0.08f, 1.0f);
        return ImVec4(0.28f, 0.24f, 0.56f, 1.0f);
    }

    static void push_text_color(bool enter, const ImVec4& c) {
        if (enter) ImGui::PushStyleColor(ImGuiCol_Text, c);
        else       ImGui::PopStyleColor();
    }

    // Native-table state (see the BLOCK_TABLE family above).
    bool md_table_active_ = false;     // a BeginTable() is currently open
    bool md_in_table_header_ = false;  // inside <thead>, so cells get header bg
    int  md_table_id_ = 0;             // per-table id, reset each print()
};

// One reusable renderer instance. imgui_md resets its per-parse scratch state
// at the start of every print(), so sharing avoids reallocating the internal
// list / table vectors on every frame.
ChatMarkdown& chat_markdown() {
    static ChatMarkdown s_md;
    return s_md;
}

float estimateMultilineHeight(const std::string& text) {
    int nl = (int)std::count(text.begin(), text.end(), '\n');
    float line_h = ImGui::GetTextLineHeight();
    float frame_pad = ImGui::GetStyle().FramePadding.y * 2;
    float h = line_h * (nl + 1) + frame_pad + 4.0f;
    float max_h = line_h * 30.0f;
    if (h > max_h) h = max_h;
    return h;
}

// AI message -> md4c + imgui_md with color-based structure.
void renderAIMessage(int msg_idx, const std::string& content) {
    if (content.empty()) return;
    ImGui::PushID(msg_idx);
    chat_markdown().print(content.c_str(), content.c_str() + content.size());
    ImGui::PopID();
}

// User message -> ReadOnly InputTextMultiline (drag-select + Ctrl+C).
void renderUserMessage(int msg_idx, const std::string& content) {
    if (content.empty()) return;
    ImGui::PushID(msg_idx);
    ImGui::InputTextMultiline("##u",
        const_cast<char*>(content.c_str()), content.size() + 1,
        ImVec2(-FLT_MIN, estimateMultilineHeight(content)),
        ImGuiInputTextFlags_ReadOnly);
    ImGui::PopID();
}

void renderChatHistory(AIChatController& chat) {
    ImGui::BeginChild("##chatscroll", ImVec2(0, -60), true);

    const auto hist = chat.HistorySnapshot();
    for (int i = 0; i < (int)hist.size(); ++i) {
        const auto& msg = hist[i];
        bool is_user = (msg.role == "user");
        ImVec4 color = is_user ? ImVec4(0.12f, 0.39f, 0.68f, 1.0f)
                               : ImVec4(0.12f, 0.45f, 0.28f, 1.0f);
        const char* label = is_user ? "[You]" : "[AI]";

        const bool has_display = !msg.display_content.empty();
        const std::string& display_text = has_display ? msg.display_content
                                                      : msg.content;

        ImGui::TextColored(color, "%s", label);
        if (is_user && has_display) {
            claw_ui::same_line_if_fits_text("(auto)");
            ImGui::TextDisabled("(auto)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "This is the short label shown for an auto-generated\n"
                    "request from a ? / Ask AI button. The full prompt\n"
                    "(metadata, task, language directive) was sent to the\n"
                    "AI  - click Copy to see it verbatim.");
        }
        char btn_id[32];
        snprintf(btn_id, sizeof(btn_id), "Copy##cp_%d", i);
        claw_ui::same_line_if_fits_button(btn_id);
        if (ImGui::SmallButton(btn_id))
            ImGui::SetClipboardText(msg.content.c_str());

        if (is_user) renderUserMessage(i, display_text);
        else         renderAIMessage(i, display_text);
        ImGui::Spacing();
    }

    std::string streaming = chat.GetStreamingContent();
    if (!streaming.empty()) {
        ImVec4 color(0.12f, 0.45f, 0.28f, 1.0f);
        ImGui::TextColored(color, "[AI]");
        claw_ui::same_line_if_fits_button("Copy##stream");
        if (ImGui::SmallButton("Copy##stream"))
            ImGui::SetClipboardText(streaming.c_str());
        renderAIMessage(-1, streaming);
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
        ImGui::SetScrollHereY(1.0f);

    ImGui::EndChild();
}

} // namespace


void MainWindow::renderAIPanel() {
    if (!dlg_ai_) return;
    ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);

    // The window stays freely movable. InputText / Button widgets internally
    // capture the mouse, so dragging inside an editable field or pressing a
    // Copy button does not move the window; an earlier guard that toggled
    // ImGuiWindowFlags_NoMove based on hover was too aggressive  - it also
    // disabled the title-bar drag.
    if (!ImGui::Begin("AI Chat", &dlg_ai_)) { ImGui::End(); return; }

    // Per-panel font scale. We use ImGuiIO::FontGlobalScale (set/restore
    // around the panel body) instead of SetWindowFontScale because
    // SetWindowFontScale is per-window and does NOT propagate into child
    // windows. The chat messages live inside a BeginChild scroll region and
    // the user-message InputTextMultiline opens its own internal child window,
    // so SetWindowFontScale would leave those at default size and only resize
    // the API-key / buttons row.
    //
    // FontGlobalScale is process-wide, but the save/restore pair below
    // means it only affects rendering inside this Begin/End block  - the
    // status bar and any later widgets in the frame go back to 1.0.
    static float chat_font_scale = 1.0f;
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) &&
        io.KeyCtrl && io.MouseWheel != 0.0f) {
        chat_font_scale += io.MouseWheel * 0.1f;
        if (chat_font_scale < 0.5f) chat_font_scale = 0.5f;
        if (chat_font_scale > 3.0f) chat_font_scale = 3.0f;
        // Consume the wheel so the chat scroll-region below doesn't
        // also fly up/down at the same time.
        io.MouseWheel = 0.0f;
    }
    // Multiply rather than overwrite: the global UIScale's font scale
    // has already been applied to io.FontGlobalScale at frame start.
    // Chat scale is a per-panel boost on top of whatever the global is.
    ScopedFontGlobalScale scoped_font_scale(io, chat_font_scale);

    // Visible text selection highlight (used inside InputText widgets)
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, ImVec4(0.23f, 0.48f, 0.78f, 0.32f));
    ImGui::PushTextWrapPos(0.0f);

    auto* chat = ai_chat();

    // -- API Key --
    static char key_buf[256] = "";
    static std::string key_buf_source;
    const std::string model_name = chat->GetModelName();
    const std::string stored_key = chat->GetApiKey();
    if (key_buf_source != stored_key && !ImGui::IsAnyItemActive()) {
        std::snprintf(key_buf, sizeof(key_buf), "%s", stored_key.c_str());
        key_buf_source = stored_key;
    }
    ImGui::Text("Model: %s", model_name.c_str());
    claw_ui::same_line_if_fits_button("Clear Chat");
    const bool chat_waiting = chat->IsWaiting();
    if (chat_waiting)
        ImGui::BeginDisabled();
    if (ImGui::SmallButton("Clear Chat"))
        chat->ClearHistory();
    if (chat_waiting) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Wait for the current AI response before clearing chat history.");
    }

    const float apply_key_w = claw_ui::button_width_for_label("Apply Key");
    const float api_key_avail = ImGui::GetContentRegionAvail().x;
    float api_key_input_w =
        api_key_avail - apply_key_w - ImGui::GetStyle().ItemSpacing.x;
    if (api_key_input_w < 160.0f)
        api_key_input_w = api_key_avail;
    ImGui::SetNextItemWidth(api_key_input_w);
    ImGui::InputTextWithHint("##apikey", "DeepSeek API Key", key_buf,
        sizeof(key_buf), ImGuiInputTextFlags_Password);
    claw_ui::same_line_if_fits_width(apply_key_w);
    if (ImGui::Button("Apply Key")) {
        chat->SetApiKey(key_buf);
        key_buf_source = chat->GetApiKey();
        std::snprintf(key_buf, sizeof(key_buf), "%s", key_buf_source.c_str());
    }

    // Font + language + status all on one row
    ImGui::PushButtonRepeat(true);
    if (ImGui::SmallButton("-##fontdec")) {
        chat_font_scale -= 0.1f;
        if (chat_font_scale < 0.5f) chat_font_scale = 0.5f;
    }
    claw_ui::same_line_if_fits_button("+##fontinc");
    if (ImGui::SmallButton("+##fontinc")) {
        chat_font_scale += 0.1f;
        if (chat_font_scale > 3.0f) chat_font_scale = 3.0f;
    }
    ImGui::PopButtonRepeat();
    claw_ui::same_line_if_fits_text("000%");
    ImGui::TextDisabled("%.0f%%", chat_font_scale * 100.0f);

    claw_ui::same_line_if_fits_width(75.0f);
    {
        const char* lang_options[] = {"English", "\xE4\xB8\xAD\xE6\x96\x87"};
        int cur = (ai_lang::current() == ai_lang::Language::Chinese) ? 1 : 0;
        ImGui::PushItemWidth(75.0f);
        if (ImGui::Combo("##ai_lang", &cur, lang_options, 2)) {
            ai_lang::set(cur == 1 ? ai_lang::Language::Chinese
                                  : ai_lang::Language::English);
        }
        ImGui::PopItemWidth();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Language the AI uses to reply.");
    }

    claw_ui::same_line_if_fits_text(chat_waiting ? "Thinking..." : "Ready");
    if (chat_waiting)
        ImGui::TextColored(claw_ui::status_warning_color(), "Thinking...");
    else
        ImGui::Text("Ready");

    // -- Context controls --
    ImGui::Checkbox("Use Current Model Context", &ai_use_context_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When on, every message is sent together with a\n"
                          "one-shot system block describing the current model\n"
                          "and the most recently focused dialog panel.");

    if (ImGui::SmallButton("Explain Current Panel")) {
        send_ai_request(
            std::string(
                "Briefly explain the algorithm panel listed in the [Panel] section:\n"
                "- what it does,\n"
                "- when to use it,\n"
                "- common prerequisites or pitfalls.\n"
                "If [Panel] is (none), say no algorithm panel is currently focused. ")
            + ai_lang::directive(),
            AICtx_ActivePanel | AICtx_CurrentModel,
            /*extra_context*/ std::string(),
            /*display_label*/ "Explain the currently focused panel");
    }
    claw_ui::same_line_if_fits_button("What should I do next?");
    if (ImGui::SmallButton("What should I do next?")) {
        send_ai_request(
            std::string(
                "Based on [Current Model], suggest 2-4 concrete next-step actions:\n"
                "- name the algorithm or menu entry,\n"
                "- explain why it fits this model,\n"
                "- describe the expected effect.\n"
                "Cover common goals: repair, simplify, remesh, UV/texture, "
                "measurement. If the model is already clean, it is fine to "
                "say no further processing is required. ")
            + ai_lang::directive(),
            AICtx_CurrentModel | AICtx_Scene | AICtx_Runtime,
            /*extra_context*/ std::string(),
            /*display_label*/ "What should I do next with this model?");
    }

    ImGui::Separator();

    // -- Chat history --
    renderChatHistory(*chat);

    // -- Input --
    static char input_buf[1024] = "";
    float btn_w = 60;
    float spc = ImGui::GetStyle().ItemSpacing.x;
    float input_avail = ImGui::GetContentRegionAvail().x;
    float input_w = input_avail - btn_w - spc;
    if (input_w < 160.0f)
        input_w = input_avail;
    ImGui::InputTextMultiline("##input", input_buf, sizeof(input_buf),
        ImVec2(input_w, ImGui::GetTextLineHeight() * 3));
    claw_ui::same_line_if_fits_width(btn_w);
    // Ctrl+Enter to send (check raw keys, not the InputText return value)
    bool ctrl_enter = ImGui::IsItemFocused() && ImGui::GetIO().KeyCtrl &&
                      ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetTextLineHeight());
    if (ImGui::Button("Send", ImVec2(btn_w, 0)) || ctrl_enter) {
        std::string msg(input_buf);
        if (!msg.empty() && !chat->IsWaiting()) {
            if (ai_use_context_) {
                // Default flags: model + panel. Scene/runtime kept out of
                // every message to keep token usage modest.
                std::string ctx = AIContext::instance().build_full_context(
                    &viewer_, AICtx_Default);
                if (!ctx.empty()) chat->InjectContext(ctx);
            }
            chat->SendUserMessage(msg);
            memset(input_buf, 0, sizeof(input_buf));
        }
    }

    // Check for responses
    ChatResponse resp;
    if (chat->TryGetResponse(resp)) {
        if (!resp.ok)
            LOG(ERROR) << "AI error: " << resp.content;
    }

    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::End();
}

void MainWindow::send_ai_request(const std::string& task_prompt,
                                 unsigned ctx_flags,
                                 const std::string& extra_context,
                                 const std::string& display_label) {
    if (ai_service_.send_request(&viewer_, task_prompt, ctx_flags,
                                 extra_context, display_label))
        show_ai_chat();
}
