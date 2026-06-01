// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_IMGUI_WIDGETS_H
#define CLAW3D_IMGUI_WIDGETS_H

/// Reusable ImGui widgets shared across dock panels and dialogs.

#include <string>
#include <vector>
#include <mutex>

class ViewportCanvas;

// =============================================================================
// Panel states
// =============================================================================

struct ModelListState {
    // No extra state needed; reads directly from viewer.models().
};

struct LogState {
    bool auto_scroll = true;
};

struct PropertiesPanelState {
    bool general_open = true;
    bool display_open = true;
    bool attributes_open = true;
    std::string active_attribute_key;
    float filter_min = 0.0f;
    float filter_max = 0.0f;
    bool filter_clear_first = true;
};

struct HealthReportState {
    // Remembers the last model the panel rendered for, so it can re-trigger
    // background analysis when the user selects a different model.
    void* last_model = nullptr;
};

struct HistoryPanelState {
    bool auto_scroll = true;
};

// =============================================================================
// Panel render functions
// =============================================================================

void renderWidgetModelList(ViewportCanvas* viewer, ModelListState& s, bool& open);
void renderWidgetLog(ViewportCanvas* viewer, LogState& s, bool& open);
void renderWidgetProperties(ViewportCanvas* viewer, PropertiesPanelState& s, bool& open);
void renderWidgetHealthReport(ViewportCanvas* viewer, HealthReportState& s, bool& open);
void renderWidgetHistory(ViewportCanvas* viewer, HistoryPanelState& s, bool& open);
void renderSettingsDialog(ViewportCanvas* viewer, bool& open);

// =============================================================================
// Log capture called by Easy3D logger to feed the log panel.
// =============================================================================
struct LogEntry {
    std::string level; // INFO, WARNING, ERROR
    std::string message;
};
extern std::vector<LogEntry> g_log_entries;
extern std::mutex g_log_mutex;

#endif // CLAW3D_IMGUI_WIDGETS_H
