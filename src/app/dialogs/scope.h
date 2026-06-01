// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_DIALOGS_SCOPE_H
#define CLAW3D_DIALOGS_SCOPE_H

/// Small RAII helpers for consistent dialog begin/end behavior.

#include "imgui.h"
#include "ai/ai_context.h"

class ScopedDialogWindow {
public:
    ScopedDialogWindow(const char* title, bool& open)
        : began_(false), active_(false), wrap_pushed_(false) {
        if (!open)
            return;
        began_ = true;
        active_ = ImGui::Begin(title, &open);
        if (active_) {
            ImGui::PushTextWrapPos(0.0f);
            wrap_pushed_ = true;
            claw_report_active_panel(title);
        }
    }

    ~ScopedDialogWindow() {
        if (wrap_pushed_)
            ImGui::PopTextWrapPos();
        if (began_)
            ImGui::End();
    }

    bool active() const { return active_; }
    void dismiss() { active_ = false; }

private:
    bool began_;
    bool active_;
    bool wrap_pushed_;
};

#define DIALOG_BODY(title, open_ref) \
    if (!(open_ref)) return; \
    for (ScopedDialogWindow dialog_scope__(title, open_ref); \
         dialog_scope__.active(); dialog_scope__.dismiss())

#define DIALOG_END

#endif // CLAW3D_DIALOGS_SCOPE_H
