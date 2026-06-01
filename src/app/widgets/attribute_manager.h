// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_ATTRIBUTE_MANAGER_H
#define CLAW3D_ATTRIBUTE_MANAGER_H

/// Attribute and scalar-field management widgets for the selected model.

class ViewportCanvas;
struct PropertiesPanelState;

void render_attribute_manager(ViewportCanvas* viewer, PropertiesPanelState& state);

#endif // CLAW3D_ATTRIBUTE_MANAGER_H
