// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_CHAT_H
#define CLAW3D_AI_CHAT_H

/// Chat-controller API for the docked assistant panel and persisted settings.

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <cstdint>

namespace httplib { class Client; }

struct ChatMessage {
    std::string role;    // "user" or "assistant"
    // The full text sent to / received from the API. Always populated.
    // For "user" messages this often contains long auto-built metadata
    // (## Model context, ## Task, etc.) -- this is what the AI sees and
    // what conversation history sends back on follow-up turns.
    std::string content;
    // Optional short label shown in the AI Chat panel instead of content.
    // Empty means the UI displays content. Never sent to the API.
    std::string display_content;
};

struct ChatResponse {
    bool ok = false;
    std::string content;
};

class AIChatController {
public:
    AIChatController();
    ~AIChatController();

    void SetApiKey(const std::string& key);
    bool HasApiKey() const;
    // Plain accessor so the launcher can persist the key to disk between runs.
    // Stored verbatim; save_config in main.cpp does not encrypt it.
    std::string GetApiKey() const;
    void SetModelName(const std::string& name);
    std::string GetModelName() const;
    // userMessage is what the API sees. displayLabel is UI-only.
    // Empty displayLabel means the UI displays the full user message.
    void SendUserMessage(const std::string& userMessage,
                         const std::string& displayLabel = std::string());
    bool TryGetResponse(ChatResponse& out);
    std::vector<ChatMessage> HistorySnapshot() const;
    void ClearHistory();
    bool IsWaiting() const { return waiting_.load(); }
    std::string GetStreamingContent();

    // Inject algorithm result context (silent, prepended to next request)
    void InjectContext(const std::string& ctx) {
        std::lock_guard<std::mutex> lock(history_mutex_);
        injected_context_ = ctx;
    }
private:
    ChatResponse PostDeepSeek(const std::vector<ChatMessage>& messages,
                              const std::string& api_key,
                              const std::string& model_name);
    void set_active_client(std::shared_ptr<httplib::Client> client);
    void clear_active_client(const std::shared_ptr<httplib::Client>& client);
    void stop_active_client();

    std::string api_key_;
    std::string model_name_ = "deepseek-chat";
    std::vector<ChatMessage> history_;
    std::string injected_context_;
    std::uint64_t history_generation_ = 0;
    std::string system_prompt_ = R"(
You are the AI assistant embedded in 3D Claw, an interactive 3D processing
studio. Users come to you with point clouds, meshes
(SurfaceMesh / PolyMesh / Graph), AI-generated 3D assets, scanner output,
and teaching models, and they want help cleaning, repairing, simplifying,
remeshing, measuring, analyzing, and exporting that data.

How to behave:
- Treat any "[Current Model]", "[Scene]", "[Panel]", "[Runtime]" blocks
  injected as separate system messages as authoritative facts about
  what the user is looking at *right now*. Prefer those numbers over
  anything you might guess. If those blocks are absent, say so instead
  of inventing model details.
- Recommend concrete next actions from the algorithm catalog when you
  can: simplification, smoothing, hole-filling, remeshing (ACVD / VSA /
  planar patch), alpha-wrap reconstruction, Poisson reconstruction,
  parameterization, geodesic distance, ARAP deformation, etc. Mention
  the exact menu / panel name when relevant.
- Be honest about uncertainty. If a result depends on info that isn't in
  the injected context (manifoldness, self-intersections, UV quality,
  exact memory headroom, etc.), say what you'd need to check.
- Never claim to have already executed something. 3D Claw's AI does not
  trigger destructive operations on its own -- recommendations only.
- Keep answers concise and structured. Reference specific numbers when
  the context provides them.
- The user's preferred reply language is appended at the end of every user
  message via an ai_lang::directive(). Obey that directive precisely;
  do not assume Chinese or English from the conversation alone.

Algorithm-specific deep knowledge (Poisson parameter ranges, alpha-wrap
trade-offs, etc.) is injected by the relevant panel as a separate
system message when needed -- do not assume it from this prompt.
)";

    std::atomic<bool> waiting_ = false;
    std::atomic<bool> shutdown_requested_ = false;
    std::thread worker_;
    mutable std::mutex history_mutex_;
    mutable std::mutex config_mutex_;
    std::mutex response_mutex_;
    std::mutex stream_mutex_;
    std::mutex client_mutex_;
    std::string streaming_content_;
    std::shared_ptr<httplib::Client> active_client_;
    std::vector<ChatResponse> response_queue_;
};

#endif // CLAW3D_AI_CHAT_H
