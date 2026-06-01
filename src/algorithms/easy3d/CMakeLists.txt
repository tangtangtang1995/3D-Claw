add_library(claw3d_easy3d_algorithms STATIC
    observable_surface_mesh_geodesic.h
    observable_surface_mesh_geodesic.cpp
)

target_include_directories(claw3d_easy3d_algorithms
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(claw3d_easy3d_algorithms
    PUBLIC
        easy3d::core
    PRIVATE
        easy3d::util)

set_target_properties(claw3d_easy3d_algorithms PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "algorithms")
