add_library(claw3d_geometry_algorithms STATIC
    include/icp.h
    include/plane_analysis.h
    src/icp.cpp
)

target_include_directories(claw3d_geometry_algorithms
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(claw3d_geometry_algorithms
    PUBLIC
        easy3d::core
    PRIVATE
        easy3d::kdtree)

set_target_properties(claw3d_geometry_algorithms PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "algorithms")
