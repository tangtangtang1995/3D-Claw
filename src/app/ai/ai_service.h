// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_SERVICE_H
#define CLAW3D_AI_SERVICE_H

/// Lightweight application service for requesting AI explanations from UI code.

#include <memory>
#include <string>

class AIChatController;
class ViewportCanvas;

class AIService {
public:
    AIService();
    ~AIService();

    AIChatController* chat();
    const AIChatController* chat() const;

    bool send_request(ViewportCanvas* viewer,
                      const std::string& task_prompt,
                      unsigned ctx_flags,
                      const std::string& extra_context,
                      const std::string& display_label);

private:
    std::unique_ptr<AIChatController> chat_;
};

#endif // CLAW3D_AI_SERVICE_H
