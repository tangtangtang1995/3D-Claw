// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors

#ifndef CLAW3D_COMMON_HTTP_STATUS_H
#define CLAW3D_COMMON_HTTP_STATUS_H

/// HTTP status constants shared by app and service networking code.
namespace claw3d::http_status {

constexpr int kOk = 200;
constexpr int kUnauthorized = 401;

} // namespace claw3d::http_status

#endif // CLAW3D_COMMON_HTTP_STATUS_H
