// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_PRODUCT_IDENTITY_H
#define CLAW3D_PRODUCT_IDENTITY_H

/// Product-facing names and identifiers used by windows, logs, and exports.
namespace product_identity {

static constexpr const char* kDisplayName = "3D Claw";
static constexpr const char* kExecutableName = "3DClaw";
static constexpr const char* kConfigFileName = "3DClaw_config.json";
static constexpr const char* kImGuiIniFileName = "3DClaw_imgui.ini";

} // namespace product_identity

#endif // CLAW3D_PRODUCT_IDENTITY_H
