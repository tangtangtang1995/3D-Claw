// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_REGION_GROWING_OVERLAY_TYPES_H
#define CLAW3D_REGION_GROWING_OVERLAY_TYPES_H

/// App-side color commands consumed by the region-growing live overlay.

struct RGColorCmd {
    int idx = -1;
    int region_id = -1;
};

#endif // CLAW3D_REGION_GROWING_OVERLAY_TYPES_H