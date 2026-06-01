// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_prompt_utils.h"

#include "ai/ai_chat.h"
#include "window/main_window.h"

#ifdef SendMessage
#  undef SendMessage
#endif

namespace claw_ai {

std::string ascii_only(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        if (c == '\n' || c == '\r' || c == '\t' ||
            (c >= 32 && c <= 126))
        {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('?');
        }
    }
    return out;
}

bool send_panel_ai_prompt(MainWindow* win,
                          const std::string& prompt,
                          const std::string& display_label)
{
    if (!win || !win->ai_chat())
        return false;
    win->show_ai_chat();
    if (!win->ai_chat()->HasApiKey())
        return false;
    win->send_ai_request(ascii_only(prompt), 0, std::string(), display_label);
    return true;
}

} // namespace claw_ai
