// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_AI_LANGUAGE_H
#define CLAW3D_AI_LANGUAGE_H

/// User-selectable AI reply language state and prompt directives.

#include <string>

namespace ai_lang {

// User-selected output language for AI replies. Does not affect the UI
// (the UI stays English in this build); only changes the directive that
// gets appended to every AI prompt.
enum class Language {
    English,
    Chinese,
};

// Singleton state. Persisted to the product config file on shutdown.
Language current();
void     set(Language l);

// Returns a short directive suitable to drop into a prompt, e.g.
//   "Reply in English. Be concise and practical."
//   "Use Simplified Chinese. Be concise, professional, and data-aware."
// Always ends with a trailing newline-friendly period.
const char* directive();

// "english" / "chinese" for config serialization.
const char* to_id(Language l);
Language    from_id(const std::string& s, Language fallback = Language::English);

// Pretty name for UI display.
const char* display_name(Language l);

} // namespace ai_lang

#endif // CLAW3D_AI_LANGUAGE_H
