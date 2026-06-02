// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_APP_PLATFORM_WINDOW_EVENTS_H
#define CLAW3D_APP_PLATFORM_WINDOW_EVENTS_H

/// UI-facing window/event-loop helpers that hide the GLFW dependency.

namespace claw3d::app {

/// Wake the main event loop after background work changes UI-visible state.
void wake_event_loop();

/// Request that the current application window close.
void request_window_close();

} // namespace claw3d::app

#endif // CLAW3D_APP_PLATFORM_WINDOW_EVENTS_H
