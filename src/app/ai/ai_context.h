// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_CONTEXT_H
#define CLAW3D_AI_CONTEXT_H

/// Builds compact scene, model, panel, and runtime context for AI prompts.

#include <string>
#include <mutex>

namespace easy3d { class Model; }
class ViewportCanvas;

// Bitflags for build_full_context().
enum AIContextFlag : unsigned {
    AICtx_Scene        = 1u << 0,
    AICtx_CurrentModel = 1u << 1,
    AICtx_Runtime      = 1u << 2,
    AICtx_ActivePanel  = 1u << 3,

    // Most common combo for "answer about the current scene".
    AICtx_Default = AICtx_CurrentModel | AICtx_ActivePanel,
    AICtx_All     = AICtx_Scene | AICtx_CurrentModel | AICtx_Runtime | AICtx_ActivePanel,
};

class AIContext {
public:
    static AIContext& instance();

    // Startup probe -- call once after OpenGL context is ready. Caches the
    // result; later build_runtime_context() reads the cache.
    void probe_runtime();

    // The "active panel" is whichever DIALOG_BODY-managed window was last
    // focused. The Chat panel uses this to answer "explain current panel".
    void set_active_panel(const std::string& panel_id);
    std::string active_panel() const;

    // Section builders. Each returns a small markdown-ish text block.
    std::string build_scene_context(ViewportCanvas* viewer) const;
    // Includes the model's lineage (root -> ... -> current) when viewer is
    // provided, by walking ModelTreeNodeInfo::parent.
    std::string build_model_context(ViewportCanvas* viewer,
                                    easy3d::Model* model) const;
    std::string build_runtime_context() const;     // uses cached probe
    std::string build_active_panel_context() const;

    // Compose. flags = bitwise OR of AIContextFlag.
    std::string build_full_context(ViewportCanvas* viewer, unsigned flags) const;

private:
    AIContext() = default;
    AIContext(const AIContext&) = delete;
    AIContext& operator=(const AIContext&) = delete;

    // Runtime cache
    int cpu_threads_ = 0;
    unsigned long long total_memory_mb_ = 0;
    std::string gpu_renderer_;
    std::string gl_version_;
    std::string os_name_;
    bool runtime_probed_ = false;

    mutable std::mutex mu_;
    std::string active_panel_;
};

// Free helper invoked from the DIALOG_BODY macro: if the just-begun window is
// focused, record its title as the active panel. Implementation in
// ai_context.cpp.
void claw_report_active_panel(const char* title);

#endif // CLAW3D_AI_CONTEXT_H
