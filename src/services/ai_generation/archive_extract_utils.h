// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SERVICES_ARCHIVE_EXTRACT_UTILS_H
#define CLAW3D_SERVICES_ARCHIVE_EXTRACT_UTILS_H

/// Safe archive extraction helpers for downloaded 3D generation results.

#include <string>

namespace claw_3dgen {

/// Converts user/API text into a path component safe for local output dirs.
std::string sanitize_path_component(const std::string& text);

/// Saves a ZIP archive body, extracts it safely, and returns the preferred model.
bool save_generation_archive_and_find_model(const std::string& job_id,
                                            const std::string& archive_body,
                                            std::string& model_path_out,
                                            std::string& error_out);

/// Returns the best supported model file from a generated output directory.
bool find_preferred_model_file(const std::string& directory,
                               std::string& model_path_out);

} // namespace claw_3dgen

#endif // CLAW3D_SERVICES_ARCHIVE_EXTRACT_UTILS_H
