// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ai/ai_language.h"

namespace ai_lang {

namespace { Language g_current = Language::English; }

Language current()      { return g_current; }
void     set(Language l){ g_current = l; }

const char* directive() {
    switch (g_current) {
    case Language::Chinese:
        // UTF-8 hex bytes for the Chinese directive (project rule disallows
        // raw CJK in source files, and MSVC mis-decodes raw UTF-8 without
        // /utf-8). The literal decodes to:
        // "Use Simplified Chinese. Be concise, professional, ground every
        //  recommendation in the numbers above; do not pad."
        return "\xE7\x94\xA8\xE4\xB8\xAD\xE6\x96\x87\xE5\x9B\x9E\xE7\xAD\x94"
               "\xEF\xBC\x8C\xE7\xAE\x80\xE6\xB4\x81\xE3\x80\x81\xE4\xB8\x93"
               "\xE4\xB8\x9A\xE3\x80\x81\xE8\xB4\xB4\xE8\xBF\x91\xE4\xBB\xA5"
               "\xE4\xB8\x8A\xE6\x95\xB0\xE6\x8D\xAE\xEF\xBC\x8C\xE4\xB8\x8D"
               "\xE8\xA6\x81\xE6\xB3\x9B\xE6\xB3\x9B\xE8\x80\x8C\xE8\xB0\x88"
               "\xE3\x80\x82";
    case Language::English:
    default:
        return "Reply in English. Be concise, practical, and ground every "
               "recommendation in the numbers above. Do not pad with "
               "textbook explanations.";
    }
}

const char* to_id(Language l) {
    return l == Language::Chinese ? "chinese" : "english";
}

Language from_id(const std::string& s, Language fallback) {
    if (s == "chinese") return Language::Chinese;
    if (s == "english") return Language::English;
    return fallback;
}

const char* display_name(Language l) {
    // UTF-8 bytes for "Chinese (localized)" display; see directive() for rationale.
    return l == Language::Chinese
        ? "\xE4\xB8\xAD\xE6\x96\x87 (Chinese)"
        : "English";
}

} // namespace ai_lang
