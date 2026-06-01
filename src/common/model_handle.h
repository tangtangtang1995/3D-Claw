// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_MODEL_HANDLE_H
#define CLAW3D_COMMON_MODEL_HANDLE_H

/// Stable scene-model handle shared across UI and async service boundaries.

#include <cstdint>

/// Stable scene-model identity used across async job boundaries.
/// The generation field prevents stale pointers from matching reused slots.
struct ModelHandle {
    std::uint64_t id = 0;
    std::uint64_t generation = 0;

    /// Returns true when the handle names a live model generation.
    bool valid() const { return id != 0 && generation != 0; }
};

inline bool operator==(const ModelHandle& a, const ModelHandle& b) {
    return a.id == b.id && a.generation == b.generation;
}

inline bool operator!=(const ModelHandle& a, const ModelHandle& b) {
    return !(a == b);
}

#endif // CLAW3D_COMMON_MODEL_HANDLE_H
