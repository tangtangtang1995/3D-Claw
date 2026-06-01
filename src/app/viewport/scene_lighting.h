// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_SCENE_LIGHTING_H
#define CLAW3D_SCENE_LIGHTING_H

#include <easy3d/core/types.h>
#include <easy3d/util/setting.h>

/// Process-wide defaults for scene surface lighting and material response.

namespace claw3d {

inline bool& scene_lighting_enabled_ref() {
    static bool enabled = true;
    return enabled;
}

inline bool scene_lighting_enabled() {
    return scene_lighting_enabled_ref();
}

inline void set_scene_lighting_enabled(bool enabled) {
    scene_lighting_enabled_ref() = enabled;
}

inline easy3d::vec4 default_scene_light_position() {
    return easy3d::vec4(0.27f, 0.27f, 0.92f, 0.0f);
}

inline easy3d::vec4 default_scene_material_ambient() {
    return easy3d::vec4(0.05f, 0.05f, 0.05f, 1.0f);
}

inline easy3d::vec4 default_scene_material_specular() {
    return easy3d::vec4(0.0f, 0.0f, 0.0f, 1.0f);
}

inline float default_scene_material_shininess() {
    return 1.0f;
}

inline void apply_default_scene_material_settings() {
    easy3d::setting::light_position = default_scene_light_position();
    easy3d::setting::material_ambient = default_scene_material_ambient();
    easy3d::setting::material_specular = default_scene_material_specular();
    easy3d::setting::material_shininess = default_scene_material_shininess();
}

} // namespace claw3d

#endif // CLAW3D_SCENE_LIGHTING_H
