// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_service.h"

#include "ai/ai_chat.h"
#include "ai/ai_context.h"

#include <easy3d/util/logging.h>

AIService::AIService()
    : chat_(std::make_unique<AIChatController>())
{
}

AIService::~AIService() = default;

AIChatController* AIService::chat() {
    return chat_.get();
}

const AIChatController* AIService::chat() const {
    return chat_.get();
}

bool AIService::send_request(ViewportCanvas* viewer,
                             const std::string& task_prompt,
                             unsigned ctx_flags,
                             const std::string& extra_context,
                             const std::string& display_label) {
    if (!chat_)
        return false;
    if (!chat_->HasApiKey()) {
        LOG(WARNING) << "AI request ignored: no API key configured.";
        return false;
    }
    if (chat_->IsWaiting())
        return false;

    std::string ctx;
    if (ctx_flags)
        ctx = AIContext::instance().build_full_context(viewer, ctx_flags);
    if (!extra_context.empty()) {
        if (!ctx.empty())
            ctx += "\n\n";
        ctx += extra_context;
    }
    if (!ctx.empty())
        chat_->InjectContext(ctx);

    chat_->SendUserMessage(task_prompt, display_label);
    return true;
}
