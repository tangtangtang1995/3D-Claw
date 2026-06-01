# Building 3D Claw

This document describes how to build 3D Claw from the public source tree.

The recommended first validation path on a new machine is the **core build with
[CGAL](https://www.cgal.org/) disabled**. That path builds the application,
[Easy3D](https://github.com/LiangliangNan/Easy3D)-backed features, model IO, AI
chat HTTPS code, AI 3D generation import, and the bundled
[miniz](https://github.com/richgel999/miniz) ZIP backend. The CGAL visual
algorithm module can be enabled after the base build is green.

## Recommended Build Order

On a new machine, validate the project in this order:

1. Build and install [Easy3D 2.6.1](https://github.com/LiangliangNan/Easy3D)
   externally.
2. Configure 3D Claw with `CLAW3D_ENABLE_CGAL=OFF`.
3. Build the CGAL-OFF configuration.
4. Smoke-test the executable with a small point cloud and a small mesh.
5. Add [CGAL 6.1.1](https://www.cgal.org/), [Boost](https://www.boost.org/),
   [GMP](https://gmplib.org/), GMPXX, and [MPFR](https://www.mpfr.org/).
6. Reconfigure with `CLAW3D_ENABLE_CGAL=ON`.
7. Smoke-test one lightweight CGAL dialog before running heavier algorithms.

This staged path separates ordinary platform issues from CGAL/numeric
dependency issues. It is also the preferred route for CI and for first-time
Ubuntu/macOS validation.

## Dependency Policy

3D Claw intentionally uses a mixed dependency model:

- Small source dependencies needed by the product are bundled under `3rd_party/`.
- Large external dependencies are not vendored and must be installed or built by
  the user.
- Build trees, installed dependency copies, binary libraries, and local package
  manager outputs must not be committed.
- `3rd_party_overrides/` is reserved for product-owned compatibility overrides.
  Keep overrides small, documented, and separate from upstream dependency
  source trees.

Tracked bundled dependencies currently include:

- [Dear ImGui](https://github.com/ocornut/imgui)
- [GLFW](https://www.glfw.org/)
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)
- [nlohmann/json](https://github.com/nlohmann/json)
- [MD4C](https://github.com/mity/md4c)
- [imgui_md](https://github.com/mekhontsev/imgui_md)
- [miniz](https://github.com/richgel999/miniz)
- [tinygltf](https://github.com/syoyo/tinygltf)
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader)
- [fast_obj](https://github.com/thisistherk/fast_obj)
- [stb](https://github.com/nothings/stb)
- [Eigen](https://eigen.tuxfamily.org/)

External dependencies are:

- [Easy3D 2.6.1](https://github.com/LiangliangNan/Easy3D)
- [OpenSSL](https://www.openssl.org/)
- [CGAL 6.1.1](https://www.cgal.org/) for `CLAW3D_ENABLE_CGAL=ON`
- [Boost](https://www.boost.org/), [GMP](https://gmplib.org/), GMPXX,
  [MPFR](https://www.mpfr.org/), and [Eigen](https://eigen.tuxfamily.org/)
  include paths used by CGAL workflows

The following local dependency directories are intentionally ignored by Git:

```text
3rd_party/CGAL-6.1.1/
3rd_party/boost/
3rd_party/gmp/
_deps/
build*/
```

Do not upload local Easy3D installs, CGAL source trees, Boost copies, GMP/MPFR
libraries, OpenSSL binaries, build directories, generated Visual Studio/Ninja
files, or copied runtime DLLs/shared libraries. If a new small bundled source
library is added, include its license and add it to `THIRD_PARTY_NOTICES.md`.

Documentation media such as README GIFs may be committed under `docs/media/`.
They are not build inputs and should be replaced sparingly because large binary
history increases repository size.

## Dependency Layout

The public tree separates product code, small bundled source dependencies, and
large external dependencies:

- `3rd_party/` contains small source dependencies that are practical to build as
  part of 3D Claw, such as Dear ImGui, GLFW, miniz, tinygltf, tinyobjloader,
  fast_obj, stb, nlohmann/json, MD4C, imgui_md, cpp-httplib, and Eigen.
- Easy3D, CGAL, Boost, GMP/MPFR, and OpenSSL are treated as external
  dependencies because their build, binary, license, and platform constraints
  are better handled through an install step, package manager, or explicit CMake
  path.
- `3rd_party_overrides/` contains product-owned compatibility overrides for a
  small number of upstream headers. It is not a vendored copy of CGAL, Easy3D,
  or Boost.
- `docs/media/` contains documentation media only. It is not used by the build.

This split is not based only on file size. It is a maintenance boundary: small
header/source libraries may be bundled when that keeps builds predictable, while
large platform-sensitive dependencies remain external and are documented through
CMake variables.

## Dependency Overrides

`3rd_party_overrides/` exists for narrow, version-specific compatibility fixes
required by 3D Claw's visual algorithm workflows. The directory mirrors upstream
include paths so CMake can place the override include directory before the
official dependency include directory.

For CGAL-enabled builds, `3rd_party_overrides/cgal-6.1.1/include` is searched
before the official CGAL 6.1.1 include path. This lets the product carry small,
reviewable header-level fixes without editing the user's CGAL installation or
turning the repository into a private CGAL fork.

Override rules:

- Keep overrides small and tied to the upstream version they patch.
- Do not copy complete upstream source trees into `3rd_party_overrides/`.
- Do not edit installed third-party headers directly.
- When upgrading CGAL, review this directory first and remove overrides that are
  no longer needed.
- Preserve upstream license headers and document the dependency in
  `THIRD_PARTY_NOTICES.md`.

## Build Modes

### Core Build, CGAL OFF

Use this mode first on Linux/macOS and on CI while validating portability:

```bash
-DCLAW3D_ENABLE_CGAL=OFF
```

This keeps the base 3D Claw application, Easy3D-backed tools, AI chat, model IO,
viewport behavior, and AI 3D generation ZIP import enabled. CGAL-only visual
algorithm menus remain visible as unavailable items.

### Full Build, CGAL ON

This is the final full-feature target:

```bash
-DCLAW3D_ENABLE_CGAL=ON
```

It requires the CGAL dependency layout described below. On a fresh Ubuntu host,
start with CGAL OFF first; then enable CGAL after the base build passes.

## Ubuntu 22.04 Core Build

If you wrote Ubuntu 22.02, this project assumes Ubuntu 22.04 LTS or a similar
modern Linux distribution.

Install system packages:

```bash
sudo apt update
sudo apt install -y \
  git \
  cmake \
  ninja-build \
  build-essential \
  python3 \
  libgl1-mesa-dev \
  libglu1-mesa-dev \
  libssl-dev \
  xorg-dev
```

Clone 3D Claw and enter the source directory:

```bash
git clone <your-3d-claw-repository-url> 3D-Claw
cd 3D-Claw
```

Build and install [Easy3D 2.6.1](https://github.com/LiangliangNan/Easy3D) into a local ignored directory:

```bash
git clone --depth 1 --branch v2.6.1 \
  https://github.com/LiangliangNan/Easy3D.git _deps/Easy3D

# Easy3D 2.6.1 installs a mixed-case ReadMe.md file. Keep this Linux
# workaround until the upstream install rule is fixed.
test -e _deps/Easy3D/ReadMe.md || ln -s README.md _deps/Easy3D/ReadMe.md

cmake -S _deps/Easy3D -B _deps/Easy3D-build -G Ninja \
  -DCMAKE_INSTALL_PREFIX=$PWD/_deps/Easy3D-install \
  -DCMAKE_BUILD_TYPE=Release \
  -DEasy3D_BUILD_APPLICATIONS=OFF \
  -DEasy3D_BUILD_EXAMPLES=OFF \
  -DEasy3D_BUILD_TESTS=OFF \
  -DEasy3D_BUILD_TUTORIALS=OFF

cmake --build _deps/Easy3D-build --target install --parallel
```

Configure 3D Claw with CGAL disabled:

```bash
cmake --preset linux-gcc-cgal-off \
  -DEasy3D_DIR=$PWD/_deps/Easy3D-install/lib/CMake \
  -DCLAW3D_EASY3D_RESOURCE_DIR=$PWD/_deps/Easy3D-install/resources
```

Build:

```bash
cmake --build --preset linux-gcc-cgal-off-release --parallel
```

The executable is expected at:

```text
build/linux-gcc-cgal-off/bin/3DClaw
```

Run it from a graphical desktop session:

```bash
./build/linux-gcc-cgal-off/bin/3DClaw
```

If CMake cannot find [OpenSSL](https://www.openssl.org/) on your Linux
distribution, pass an explicit root
or rely on the package paths from `libssl-dev`:

```bash
-DOPENSSL_ROOT_DIR=/usr
```

## Windows Core Build

Install:

- Visual Studio 2019 or 2022 with the C++ desktop workload.
- CMake 3.21 or later when using `CMakePresets.json`.
- A compatible OpenSSL development package.
- Easy3D 2.6.1 built or installed separately.

Configure without CGAL:

```powershell
cmake --preset windows-msvc-cgal-off `
  -DEasy3D_DIR=<path-to-easy3d-install>/lib/CMake `
  -DCLAW3D_EASY3D_RESOURCE_DIR=<path-to-easy3d-resources> `
  -DOPENSSL_ROOT_DIR=<path-to-openssl>
```

Build:

```powershell
cmake --build --preset windows-msvc-cgal-off-release --parallel
```

The executable is expected at:

```text
build/windows-msvc-cgal-off/bin/Release/3DClaw.exe
```

On Windows, the build copies Easy3D and OpenSSL runtime DLLs next to the
executable when those imported targets and DLL paths are discoverable.

When preparing a portable Windows binary with CGAL enabled, copy Release
GMP/MPFR runtime DLLs only. Debug GMP/MPFR builds may depend on MSVC debug CRT
DLLs such as `vcruntime140D.dll` or `ucrtbased.dll`, which are not available on
normal end-user machines.

## macOS Core Build

Install command-line tools and Homebrew dependencies:

```bash
xcode-select --install
brew update
brew install cmake ninja openssl@3
```

Build [Easy3D](https://github.com/LiangliangNan/Easy3D), then configure 3D Claw with CGAL disabled:

```bash
git clone --depth 1 --branch v2.6.1 \
  https://github.com/LiangliangNan/Easy3D.git _deps/Easy3D

cmake -S _deps/Easy3D -B _deps/Easy3D-build -G Ninja \
  -DCMAKE_INSTALL_PREFIX=$PWD/_deps/Easy3D-install \
  -DCMAKE_BUILD_TYPE=Release \
  -DEasy3D_BUILD_APPLICATIONS=OFF \
  -DEasy3D_BUILD_EXAMPLES=OFF \
  -DEasy3D_BUILD_TESTS=OFF \
  -DEasy3D_BUILD_TUTORIALS=OFF

cmake --build _deps/Easy3D-build --target install --parallel

cmake --preset macos-clang-cgal-off \
  -DEasy3D_DIR=$PWD/_deps/Easy3D-install/lib/CMake \
  -DCLAW3D_EASY3D_RESOURCE_DIR=$PWD/_deps/Easy3D-install/resources \
  -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)

cmake --build --preset macos-clang-cgal-off-release --parallel
```

macOS application bundle packaging is not yet defined. Treat this as a source
build and smoke-test workflow.

## Enabling CGAL Visual Algorithms

[CGAL](https://www.cgal.org/) is not required for the core build. The public full-feature Linux build is
the `linux-gcc-cgal-on` preset:

```bash
cmake --preset linux-gcc-cgal-on \
  -DEasy3D_DIR=$PWD/_deps/Easy3D-install/lib/CMake \
  -DCLAW3D_EASY3D_RESOURCE_DIR=$PWD/_deps/Easy3D-install/resources \
  -DCLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR=$PWD/_deps/CGAL-6.1.1/include

cmake --build --preset linux-gcc-cgal-on-release --parallel
```

The current CMake integration expects CGAL 6.1.1 headers in one of these local
ignored directories, or in a path passed explicitly:

```text
3rd_party/CGAL-6.1.1/include/CGAL/version.h
_deps/CGAL-6.1.1/include/CGAL/version.h
```

The product-owned CGAL overrides under `3rd_party_overrides/cgal-6.1.1/` are
included before the official CGAL include directory. Keep those overrides
tracked; do not edit upstream CGAL headers directly.

For Ubuntu, install the standard numeric dependencies first:

```bash
sudo apt install -y libboost-dev libgmp-dev libmpfr-dev
```

Then either extract the official CGAL 6.1.1 release into `_deps/CGAL-6.1.1` or
pass the include directory explicitly:

```bash
-DCLAW3D_CGAL_ALGO_CGAL_INCLUDE_DIR=$PWD/_deps/CGAL-6.1.1/include
-DCLAW3D_CGAL_ALGO_BOOST_INCLUDE_DIR=/usr/include
-DCLAW3D_CGAL_ALGO_GMP_INCLUDE_DIR=/usr/include/x86_64-linux-gnu
-DCLAW3D_CGAL_ALGO_MPFR_INCLUDE_DIR=/usr/include
-DCLAW3D_CGAL_ALGO_GMP_LIBRARY=/usr/lib/x86_64-linux-gnu/libgmp.so
-DCLAW3D_CGAL_ALGO_GMPXX_LIBRARY=/usr/lib/x86_64-linux-gnu/libgmpxx.so
-DCLAW3D_CGAL_ALGO_MPFR_LIBRARY=/usr/lib/x86_64-linux-gnu/libmpfr.so
```

On Ubuntu 22.04, `gmp.h` commonly lives under
`/usr/include/x86_64-linux-gnu`, while `mpfr.h` lives under `/usr/include`.
The CMake checks support those directories separately.

Ubuntu 22.04 package repositories may not provide CGAL 6.1.1. Do not assume
`apt install libcgal-dev` gives the exact version expected by this project.
Use the official [CGAL 6.1.1](https://www.cgal.org/) source/header release when validating full-feature
parity.

## Runtime Resources and User Files

3D Claw uses Easy3D's runtime resources for shaders, fonts, colormaps, textures,
and sample-data defaults. For a portable binary layout, copy Easy3D's installed
`resources/` directory next to the 3D Claw executable:

```text
3DClaw.exe
resources/
  shaders/
  fonts/
  colormaps/
  textures/
```

The runtime checks this executable-adjacent `resources/` directory
automatically. For development or custom installation layouts, configure or run
with:

```bash
-DCLAW3D_EASY3D_RESOURCE_DIR=<path-to-easy3d-resources>
```

or:

```bash
export CLAW3D_EASY3D_RESOURCE_DIR=<path-to-easy3d-resources>
```

Most mutable runtime files are written to platform user locations, not beside
the binary:

- Windows: `%APPDATA%/3DClaw` and `%LOCALAPPDATA%/3DClaw/Cache`
- Linux: `$XDG_CONFIG_HOME/3DClaw` or `~/.config/3DClaw`, and
  `$XDG_CACHE_HOME/3DClaw` or `~/.cache/3DClaw`
- macOS: `~/Library/Application Support/3DClaw` and `~/Library/Caches/3DClaw`

Executable-adjacent `3DClaw_config.json` files are still read as migration
fallbacks for older portable layouts.

AI-generated model outputs are the intentional exception: 3D Claw first writes
them to a `generated_models/` directory next to the executable when that
location is writable, then falls back to the platform user-data directory.

CJK font lookup uses this order:

1. `CLAW3D_CJK_FONT_PATH`
2. common platform font locations
3. Dear ImGui default font fallback

## Smoke Test Checklist

After a successful build, start the app from a graphical desktop session and
check:

- The application starts without an API key.
- Fonts, icons, shaders, and dock panels load.
- A `.ply`, `.off`, `.obj`, textured `.obj`, and `.glb` can be imported.
- AI Chat opens and reports missing API credentials gracefully.
- AI 3D generation can save and extract a downloaded ZIP archive when valid API
  credentials are provided.
- Several representative algorithm dialogs open.
- At least one lightweight non-CGAL algorithm can run and produce a child model.
- With `CLAW3D_ENABLE_CGAL=OFF`, CGAL dialogs are shown as unavailable rather
  than crashing.

## Troubleshooting

### `Easy3DConfig.cmake` Not Found

Build and install Easy3D first, then pass either:

```bash
-DCMAKE_PREFIX_PATH=<path-to-easy3d-install>
```

or:

```bash
-DEasy3D_DIR=<directory-containing-Easy3DConfig.cmake>
```

Do not point `Easy3D_DIR` at a raw source directory unless it contains a valid
generated or installed `Easy3DConfig.cmake`.

### OpenSSL Not Found

Install the development package and pass `OPENSSL_ROOT_DIR` if needed:

```bash
sudo apt install -y libssl-dev
cmake --preset linux-gcc-cgal-off -DOPENSSL_ROOT_DIR=/usr ...
```

### OpenGL, GLU, X11, or GLFW Build Errors on Linux

Install the desktop development packages:

```bash
sudo apt install -y libgl1-mesa-dev libglu1-mesa-dev xorg-dev
```

### CGAL Dependency Errors

Start with `CLAW3D_ENABLE_CGAL=OFF`. After the core build passes, add CGAL 6.1.1
and the Boost/GMP/MPFR variables described above.

### Public Validation

The public source package does not include the internal automated test suite.
For public builds, use CMake configure plus a complete build as the baseline
validation path, then smoke-test the resulting executable with a small mesh and
one AI-disabled algorithm panel.
