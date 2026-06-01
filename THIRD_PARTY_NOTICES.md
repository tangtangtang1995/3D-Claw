# Third-Party Notices

3D Claw is built on top of [Easy3D](https://github.com/LiangliangNan/Easy3D) and several third-party open-source
components. This file is a practical attribution index for the public source
package. The authoritative license text for each bundled component remains in
that component's own directory. External dependencies are obtained separately by
the user and remain governed by their own upstream licenses.

This notice is intentionally not a replacement for the original license files.
When redistributing source or binaries, keep the root `LICENSE`, all bundled
third-party license files, and any license files required by externally supplied
dependencies.

## Dependency Scope

3D Claw uses three dependency categories:

| Category | Policy |
| --- | --- |
| Bundled source dependencies | Small product dependencies tracked under `3rd_party/` with their own license files. |
| External dependencies | Large or standard platform packages, such as [Easy3D](https://github.com/LiangliangNan/Easy3D), [CGAL](https://www.cgal.org/), [Boost](https://www.boost.org/), [GMP](https://gmplib.org/)/[MPFR](https://www.mpfr.org/), and [OpenSSL](https://www.openssl.org/), provided by the user or package manager. |
| Product overrides | Compatibility overrides under `3rd_party_overrides/`, kept separate from upstream source trees. |

Local ignored folders may exist during development, for example local CGAL,
Boost, GMP, or build directories. Their presence in a developer checkout does
not make them part of the public bundled source package.

## Bundled Foundation

| Component | Location | Role |
|-----------|----------|------|
| [Eigen](https://eigen.tuxfamily.org/) | `3rd_party/eigen/` | Linear algebra dependency used by the product and CGAL runners. |

## External Geometry/Numeric Dependencies

These large standard dependencies are intentionally not tracked in the public
repository. See `BUILDING.md` for the supported local layout and CMake variables.

| Component | Expected by CMake | Role |
|-----------|-------------------|------|
| [Easy3D 2.6.1](https://github.com/LiangliangNan/Easy3D) | `Easy3D_DIR` or `CMAKE_PREFIX_PATH` | Core geometry types, file IO, renderer, viewer, utilities. |
| [CGAL 6.1.1](https://www.cgal.org/) | `CLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR` | Geometry algorithms used by the visual CGAL runners. |
| [Boost headers](https://www.boost.org/) | `CLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR` | Header dependencies used by CGAL workflows. |
| [GMP](https://gmplib.org/) / [MPFR](https://www.mpfr.org/) | `CLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR`, `CLAW3D_CGAL_ALGO_GMP_LIBRARY`, `CLAW3D_CGAL_ALGO_GMPXX_LIBRARY`, `CLAW3D_CGAL_ALGO_MPFR_LIBRARY` | Numeric dependencies used by CGAL workflows. |
| [OpenSSL](https://www.openssl.org/) | `OPENSSL_ROOT_DIR` | TLS backend for cpp-httplib. |

## UI and Runtime

| Component | Location | Role |
|-----------|----------|------|
| [Dear ImGui](https://github.com/ocornut/imgui) | `3rd_party/imgui/` | Immediate-mode desktop UI. |
| [GLFW](https://www.glfw.org/) | `3rd_party/glfw/` | Windowing and input backend. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | `3rd_party/httplib/` | HTTPS client used by AI chat and 3D generation services. |
| [nlohmann/json](https://github.com/nlohmann/json) | `3rd_party/json/` | JSON parsing and serialization. |
| [MD4C](https://github.com/mity/md4c) | `3rd_party/md4c/` | Markdown (CommonMark/GFM) parser used to render AI chat replies. |
| [imgui_md](https://github.com/mekhontsev/imgui_md) | `3rd_party/imgui_md/` | Dear ImGui renderer bridge that draws MD4C output in the AI chat panel. |
| [miniz](https://github.com/richgel999/miniz) | `3rd_party/miniz/` | Cross-platform ZIP archive extraction for AI 3D generation downloads. |

## Model IO and Geometry Utilities

| Component | Location | Role |
|-----------|----------|------|
| [tinygltf](https://github.com/syoyo/tinygltf) | `3rd_party/tinygltf/` | GLB/GLTF loading support. |
| [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) | `3rd_party/tinyobjloader/` | OBJ parsing support. |
| [fast_obj](https://github.com/thisistherk/fast_obj) | `3rd_party/fastobj/` | Fast OBJ parsing support. |
| [stb](https://github.com/nothings/stb) | `3rd_party/stb/` | Image loading utilities. |

## Product Overrides

`3rd_party_overrides/` contains small compatibility overrides that mirror
selected upstream header paths. These files are kept separate from official
third-party source trees so the product-specific patch surface remains visible
and reviewable.

When an override is derived from an upstream header, preserve the original
license header and attribution. Treat the override as version-bound to the
upstream dependency named in its path, and review it whenever that dependency is
upgraded.

## Easy3D-Derived Application Components

The application currently retains `src/app/ui/walk_through.cpp` and
`src/app/ui/walk_through.h` from the original Easy3D camera-path / walkthrough
tool. These files are kept because the 3D Claw animation and camera
path UI still depends on that implementation. Future product work may rename or
rewrite this component, but redistribution should continue to preserve Easy3D
attribution and license terms for it.

## Product-Owned Integration Code

The CGAL visual runner layer under `src/algorithms/cgal/` is product-owned
integration code. It links Easy3D model/viewer concepts with CGAL algorithms and
adds process visualization events, live-preview snapshots, and UI-facing result
contracts.

The AI integration, algorithm dialogs, model-tree workflows, health report,
history panel, result-evaluation prompts, and AI 3D generation workflow are also
product-owned 3D Claw code unless noted otherwise in the corresponding source
file headers.

## License Reminder

The root `LICENSE` file and the license files inside bundled third-party
directories control redistribution terms. Keep those files when redistributing
source or binary packages. External dependencies are not redistributed by this
repository; users should follow each upstream package's license and installation
instructions.
