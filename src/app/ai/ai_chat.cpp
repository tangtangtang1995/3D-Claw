// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include <3rd_party/httplib/httplib.h>

#include "ai/ai_chat.h"
#include "common/http_status.h"
#include <3rd_party/json/json.hpp>

#include <easy3d/util/logging.h>

#include <exception>
#include <cctype>
#include <memory>
#include <mutex>
#include <thread>

using json = nlohmann::json;

namespace {
constexpr double kDefaultTemperature = 0.7;
constexpr int kReadTimeoutSec = 120;
constexpr int kWriteTimeoutSec = 30;
constexpr const char* kSseDoneMarker = "[DONE]";

std::string trim_ascii(std::string text) {
    auto is_space = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    while (!text.empty() && is_space(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && is_space(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

bool starts_with_bearer_prefix(const std::string& text) {
    constexpr const char* kBearerPrefix = "Bearer ";
    for (std::size_t i = 0; kBearerPrefix[i] != '\0'; ++i) {
        if (i >= text.size())
            return false;
        const auto a = static_cast<unsigned char>(text[i]);
        const auto b = static_cast<unsigned char>(kBearerPrefix[i]);
        if (std::tolower(a) != std::tolower(b))
            return false;
    }
    return true;
}

std::string normalize_api_key(std::string key) {
    key = trim_ascii(std::move(key));
    if (starts_with_bearer_prefix(key))
        key = trim_ascii(key.substr(7));
    return key;
}
} // namespace

AIChatController::AIChatController() {}
AIChatController::~AIChatController() {
    shutdown_requested_ = true;
    stop_active_client();
    if (worker_.joinable())
        worker_.join();
}

void AIChatController::SetApiKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    api_key_ = normalize_api_key(key);
}


bool AIChatController::HasApiKey() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return !api_key_.empty();
}


std::string AIChatController::GetApiKey() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return api_key_;
}


void AIChatController::SetModelName(const std::string& name) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    model_name_ = name.empty() ? "deepseek-chat" : name;
}


std::string AIChatController::GetModelName() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return model_name_;
}


void AIChatController::set_active_client(
        std::shared_ptr<httplib::Client> client) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    active_client_ = std::move(client);
}


void AIChatController::clear_active_client(
        const std::shared_ptr<httplib::Client>& client) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (active_client_ == client)
        active_client_.reset();
}


void AIChatController::stop_active_client() {
    std::shared_ptr<httplib::Client> client;
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        client = active_client_;
    }
    if (client)
        client->stop();
}


void AIChatController::SendUserMessage(const std::string& userMessage,
                                       const std::string& displayLabel) {
    if (waiting_.load() || shutdown_requested_.load())
        return;

    if (worker_.joinable())
        worker_.join();

    std::string api_key;
    std::string request_model_name;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        api_key = api_key_;
        request_model_name = model_name_;
    }
    if (api_key.empty())
        return;

    std::uint64_t request_generation = 0;

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        // content goes to the API verbatim; display_content is UI-only.
        ChatMessage m;
        m.role = "user";
        m.content = userMessage;
        m.display_content = displayLabel;
        history_.push_back(std::move(m));
        request_generation = history_generation_;
    }

    waiting_ = true;
    worker_ = std::thread([this, api_key, request_model_name,
                           request_generation]() {
        ChatResponse resp;
        try {
            std::vector<ChatMessage> msgs_copy;
            {
                std::lock_guard<std::mutex> lock(history_mutex_);
                msgs_copy = history_;
            }

            resp = PostDeepSeek(msgs_copy, api_key, request_model_name);

            if (shutdown_requested_.load()) {
                waiting_ = false;
                return;
            }
        } catch (const std::exception& e) {
            resp.ok = false;
            resp.content = std::string("AI request failed: ") + e.what();
        } catch (...) {
            resp.ok = false;
            resp.content = "AI request failed: unknown exception.";
        }

        if (shutdown_requested_.load()) {
            waiting_ = false;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            if (history_generation_ != request_generation) {
                waiting_ = false;
                return;
            }

            std::string assistant_content = resp.content;
            if (!resp.ok) {
                if (assistant_content.empty())
                    assistant_content = "unknown error.";
                if (assistant_content.rfind("AI request failed:", 0) != 0)
                    assistant_content = "AI request failed: " + assistant_content;
            }
            resp.content = assistant_content;
            if (!assistant_content.empty())
                history_.push_back({"assistant", assistant_content});
        }

        {
            std::lock_guard<std::mutex> lock(response_mutex_);
            response_queue_.push_back(resp);
        }

        waiting_ = false;
    });
}


ChatResponse AIChatController::PostDeepSeek(const std::vector<ChatMessage>& messages,
                                            const std::string& api_key,
                                            const std::string& model_name) {
    ChatResponse resp;

    json body;
    body["model"] = model_name;
    body["stream"] = true;
    body["temperature"] = kDefaultTemperature;

    json msgs = json::array();
    msgs.push_back({{"role", "system"}, {"content", system_prompt_}});

    // Inject algorithm context as second system message (if present)
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        if (!injected_context_.empty()) {
            msgs.push_back({{"role", "system"}, {"content", injected_context_}});
            injected_context_.clear(); // consume once
        }
    }

    for (const auto& m : messages)
        msgs.push_back({{"role", m.role}, {"content", m.content}});
    body["messages"] = msgs;

    auto cli = std::make_shared<httplib::Client>("https://api.deepseek.com");
    set_active_client(cli);
    struct ActiveClientCleanup {
        AIChatController* owner = nullptr;
        std::shared_ptr<httplib::Client> client;
        ~ActiveClientCleanup() {
            if (owner)
                owner->clear_active_client(client);
        }
    } active_client_cleanup{this, cli};

    cli->set_read_timeout(kReadTimeoutSec);
    cli->set_write_timeout(kWriteTimeoutSec);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key},
        {"Content-Type", "application/json"}
    };

    // Clear streaming buffer
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        streaming_content_.clear();
    }

    std::string full_content;
    std::string sse_buffer;

    const std::string payload = body.dump(
        -1, ' ', false, json::error_handler_t::replace);

    auto result = cli->Post("/chat/completions", headers, payload, "application/json",
        [&](const char* data, size_t len) -> bool {
            if (shutdown_requested_.load())
                return false;

            sse_buffer.append(data, len);

            // Parse SSE frames: "data: {...}\n\n"
            size_t pos = 0;
            while ((pos = sse_buffer.find("\n\n")) != std::string::npos) {
                std::string frame = sse_buffer.substr(0, pos);
                sse_buffer.erase(0, pos + 2);

                // Strip "data: " prefix
                const char* prefix = "data: ";
                if (frame.compare(0, 6, prefix) == 0) {
                    std::string json_str = frame.substr(6);
                    if (json_str == kSseDoneMarker) continue;

                    try {
                        auto j = json::parse(json_str);
                        if (j.contains("choices") && !j["choices"].empty()) {
                            auto& delta = j["choices"][0]["delta"];
                            if (delta.contains("content")) {
                                std::string chunk = delta["content"].get<std::string>();
                                full_content += chunk;
                                {
                                    std::lock_guard<std::mutex> lock(stream_mutex_);
                                    streaming_content_ = full_content;
                                }
                            }
                        }
                    } catch (const std::exception& e) {
                        LOG(WARNING) << "Ignoring malformed AI stream chunk: " << e.what();
                    } catch (...) {
                        LOG(WARNING) << "Ignoring malformed AI stream chunk: unknown error";
                    }
                }
            }
            return !shutdown_requested_.load();
        });

    // Clear streaming buffer
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        streaming_content_.clear();
    }

    if (shutdown_requested_.load()) {
        resp.ok = false;
        resp.content = "AI request cancelled.";
        return resp;
    }

    if (!result || result->status != claw3d::http_status::kOk) {
        resp.ok = false;
        if (result &&
            result->status == claw3d::http_status::kUnauthorized) {
            resp.content =
                "HTTP 401 Unauthorized. Check the DeepSeek API key in "
                "AI Chat, and paste only the raw key value without a "
                "'Bearer ' prefix or extra spaces.";
        } else {
            resp.content = result
                ? "HTTP " + std::to_string(result->status)
                : "Connection failed. Check network or API key.";
        }
        if (!full_content.empty()) resp.content = full_content; // partial success
        return resp;
    }

    resp.ok = true;
    resp.content = full_content;
    return resp;
}


std::vector<ChatMessage> AIChatController::HistorySnapshot() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return history_;
}


bool AIChatController::TryGetResponse(ChatResponse& out) {
    std::lock_guard<std::mutex> lock(response_mutex_);
    if (response_queue_.empty()) return false;
    out = response_queue_.front();
    response_queue_.erase(response_queue_.begin());
    return true;
}


std::string AIChatController::GetStreamingContent() {
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return streaming_content_;
}


void AIChatController::ClearHistory() {
    std::lock(history_mutex_, response_mutex_, stream_mutex_);
    std::lock_guard<std::mutex> history_lock(history_mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> response_lock(response_mutex_, std::adopt_lock);
    std::lock_guard<std::mutex> stream_lock(stream_mutex_, std::adopt_lock);
    history_.clear();
    injected_context_.clear();
    ++history_generation_;
    response_queue_.clear();
    streaming_content_.clear();
}
