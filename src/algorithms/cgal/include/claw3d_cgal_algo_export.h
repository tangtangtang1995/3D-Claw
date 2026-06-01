// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 3D Claw contributors
//
// This file is part of 3D Claw, an AI-assisted 3D geometry processing
// application. 3D Claw links Easy3D and CGAL (both GPLv3) and is distributed
// under the GNU General Public License v3. See the root LICENSE file.

#ifndef CLAW3D_CGAL_ALGO_EXPORT_H
#define CLAW3D_CGAL_ALGO_EXPORT_H

/// Export macro for the optional CGAL algorithm library boundary.

#if defined(_WIN32) || defined(_WIN64)
#  if defined(CLAW3D_CGAL_ALGO_EXPORTS)
#    define CLAW3D_CGAL_ALGO_API __declspec(dllexport)
#  else
#    define CLAW3D_CGAL_ALGO_API __declspec(dllimport)
#  endif
#else
#  define CLAW3D_CGAL_ALGO_API
#endif

#endif // CLAW3D_CGAL_ALGO_EXPORT_H
