// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_AI_3D_GENERATION_CLIENT_H
#define CLAW3D_SERVICES_AI_3D_GENERATION_CLIENT_H

/// HTTP client contract for Tencent Hunyuan 3D generation.

#include <atomic>
#include <string>
#include <vector>

namespace claw_3dgen {

constexpr int kDefaultHunyuanFaceCount = 500000;

enum class HunyuanInputMode {
    Image,
    Text
};

enum class HunyuanQueryState {
    Polling,
    Done,
    Error
};

/// Request sent to Tencent Hunyuan 3D generation.
struct Hunyuan3DRequest {
    std::string secret_id;
    std::string secret_key;
    HunyuanInputMode input_mode = HunyuanInputMode::Image;
    std::string image_path;
    std::string text_prompt;
    int generate_type = 0;
    bool enable_pbr = false;
    int face_count = kDefaultHunyuanFaceCount;
};

/// Parsed polling response for a submitted generation job.
struct Hunyuan3DQueryResult {
    HunyuanQueryState state = HunyuanQueryState::Polling;
    std::vector<std::string> result_urls;
    std::string error_message;
    int credits_consumed = 0;
};

/// Thin HTTP client for submit, poll, and result download calls.
class Hunyuan3DClient {
public:
    bool submit_job(const Hunyuan3DRequest& request,
                    std::string& job_id_out,
                    std::string& error_out,
                    const std::atomic<bool>* cancelled = nullptr) const;

    bool query_job(const Hunyuan3DRequest& request,
                   const std::string& job_id,
                   Hunyuan3DQueryResult& result_out,
                   std::string& error_out,
                   const std::atomic<bool>* cancelled = nullptr) const;

    bool download_url(const std::string& url,
                      std::string& body_out,
                      std::string& error_out,
                      const std::atomic<bool>* cancelled = nullptr) const;
};

} // namespace claw_3dgen

#endif // CLAW3D_SERVICES_AI_3D_GENERATION_CLIENT_H
