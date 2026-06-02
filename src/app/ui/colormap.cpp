// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#include "ui/colormap.h"

#include "services/resources/resource_paths.h"

#include <3rd_party/stb/stb_image.h>

#include <string>

namespace claw_ui {

ImU32 colormap_color(float t) {
    static unsigned char* cmap = nullptr;
    static int cmap_w = 0;
    static int cmap_h = 0;
    if (!cmap) {
        std::string path = claw3d::resources::easy3d_resource_path(
            "colormaps/default.png");
        int ch;
        cmap = stbi_load(path.c_str(), &cmap_w, &cmap_h, &ch, 4);
    }
    if (!cmap || cmap_w <= 0)
        return IM_COL32(128, 128, 128, 255);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const int x = static_cast<int>(t * (cmap_w - 1));
    const int y = cmap_h / 2;
    const int idx = (y * cmap_w + x) * 4;
    return IM_COL32(cmap[idx], cmap[idx + 1], cmap[idx + 2], cmap[idx + 3]);
}

} // namespace claw_ui
