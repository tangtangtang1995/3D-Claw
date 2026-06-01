add_library(claw3d_analysis_algorithms STATIC
    include/mesh_quality.h
    include/model_health.h
    src/mesh_quality.cpp
    src/model_health.cpp
)

target_include_directories(claw3d_analysis_algorithms
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(claw3d_analysis_algorithms
    PUBLIC
        easy3d::core
    PRIVATE
        easy3d::algo
        easy3d::kdtree
        easy3d::util)

set_target_properties(claw3d_analysis_algorithms PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "algorithms")
