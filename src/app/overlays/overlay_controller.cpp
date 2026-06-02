// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "overlays/overlay_controller.h"

#include "selection/selection_manager.h"

OverlayController::OverlayController(ViewportCanvas& viewer,
                                     SelectionManager& selection_manager)
    : viewer_(viewer), selection_manager_(selection_manager) {}

bool OverlayController::selection_visible() const {
    return interaction_overlay_.selection_visible;
}

void OverlayController::set_selection_visible(bool visible) {
    if (interaction_overlay_.selection_visible == visible)
        return;
    interaction_overlay_.selection_visible = visible;
    if (!visible)
        clear_selection_overlays();
    else
        selection_revision_ = -1;
}