add_library(claw3d_io STATIC
    surface_mesh_io.h
    surface_mesh_io.cpp
    surface_mesh_io_gltf.cpp
    stb_runtime.cpp
)

target_include_directories(claw3d_io
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
    PRIVATE
        "${CLAW3D_ROOT}"
        "${CLAW3D_THIRD_PARTY_ROOT}/tinygltf")

target_link_libraries(claw3d_io
    PUBLIC
        easy3d::core
    PRIVATE
        easy3d::fileio
        easy3d::util)

set_target_properties(claw3d_io PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "io")
