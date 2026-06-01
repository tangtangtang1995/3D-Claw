set(CLAW3D_SERVICES_HEADERS
    # Core service infrastructure.
    core/algorithm_controller.h
    core/algorithm_id.h
    core/algorithm_mesh_bridge.h
    core/job_handle_detail.h

    # Easy3D-backed jobs and synchronous model operations.
    jobs/easy3d/easy3d_point_cloud_jobs.h
    jobs/easy3d/easy3d_surface_mesh_jobs.h
    jobs/easy3d/geodesic_distance_job.h
    jobs/easy3d/poisson_reconstruction_job.h
    operations/easy3d_model_operations.h

    # AI generation and application IO/resource services.
    ai_generation/ai_3d_generation_client.h
    ai_generation/ai_3d_generation_job.h
    ai_generation/ai_3d_generation_service.h
    ai_generation/archive_extract_utils.h
    io/file_load_controller.h
    resources/resource_paths.h

    # Cross-platform runtime helpers.
    platform/platform_paths.h
    platform/platform_runtime.h

    # CGAL-backed jobs. Sources are compiled only when CGAL is enabled.
    jobs/cgal/acvd_remeshing_job.h
    jobs/cgal/alpha_wrap_job.h
    jobs/cgal/arap_deformation_job.h
    jobs/cgal/cgal_simplification_job.h
    jobs/cgal/cgal_smoothing_job.h
    jobs/cgal/mcf_skeletonization_job.h
    jobs/cgal/parameterization_job.h
    jobs/cgal/planar_patch_remeshing_job.h
    jobs/cgal/ransac_detection_job.h
    jobs/cgal/region_growing_job.h
    jobs/cgal/vsa_approximation_job.h
)

set(CLAW3D_SERVICES_SOURCES
    core/algorithm_controller.cpp

    jobs/easy3d/easy3d_point_cloud_jobs.cpp
    jobs/easy3d/easy3d_surface_mesh_jobs.cpp
    jobs/easy3d/geodesic_distance_job.cpp
    jobs/easy3d/poisson_reconstruction_job.cpp
    operations/easy3d_model_operations.cpp

    ai_generation/ai_3d_generation_client.cpp
    ai_generation/ai_3d_generation_job.cpp
    ai_generation/ai_3d_generation_service.cpp
    ai_generation/archive_extract_utils.cpp
    io/file_load_controller.cpp
    resources/resource_paths.cpp
    platform/platform_paths.cpp
    platform/platform_runtime.cpp
)

set(CLAW3D_SERVICES_CGAL_SOURCES
    jobs/cgal/acvd_remeshing_job.cpp
    jobs/cgal/alpha_wrap_job.cpp
    jobs/cgal/arap_deformation_job.cpp
    jobs/cgal/cgal_simplification_job.cpp
    jobs/cgal/cgal_smoothing_job.cpp
    jobs/cgal/mcf_skeletonization_job.cpp
    jobs/cgal/parameterization_job.cpp
    jobs/cgal/planar_patch_remeshing_job.cpp
    jobs/cgal/ransac_detection_job.cpp
    jobs/cgal/region_growing_job.cpp
    jobs/cgal/vsa_approximation_job.cpp
)

if(CLAW3D_ENABLE_CGAL)
    list(APPEND CLAW3D_SERVICES_SOURCES
        ${CLAW3D_SERVICES_CGAL_SOURCES})
endif()

add_library(claw3d_services STATIC
    ${CLAW3D_SERVICES_HEADERS}
    ${CLAW3D_SERVICES_SOURCES}
)

find_package(OpenSSL REQUIRED)

target_include_directories(claw3d_services
    PUBLIC
        "${CMAKE_SOURCE_DIR}/src"
    PRIVATE
        "${CLAW3D_ROOT}")

target_compile_definitions(claw3d_services PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

if(CLAW3D_EASY3D_RESOURCE_DIR)
    file(TO_CMAKE_PATH "${CLAW3D_EASY3D_RESOURCE_DIR}" _claw3d_easy3d_resource_dir)
    target_compile_definitions(claw3d_services PRIVATE
        "CLAW3D_EASY3D_RESOURCE_DIR=\"${_claw3d_easy3d_resource_dir}\"")
endif()

if(WIN32 OR MSVC)
    target_compile_definitions(claw3d_services PRIVATE _CRT_SECURE_NO_DEPRECATE)
endif()

target_link_libraries(claw3d_services
    PUBLIC
        easy3d::core
    PRIVATE
        claw3d_io
        claw3d_easy3d_algorithms
        claw3d_geometry_algorithms
        claw3d_analysis_algorithms
        3rd_miniz
        easy3d::algo
        easy3d::fileio
        easy3d::kdtree
        easy3d::util
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)

if(CLAW3D_ENABLE_CGAL)
    target_include_directories(claw3d_services PRIVATE
        "${CMAKE_SOURCE_DIR}/src/algorithms/cgal/include")
    target_link_libraries(claw3d_services PRIVATE claw3d_cgal_algorithms)
    target_compile_definitions(claw3d_services PRIVATE CLAW3D_HAS_CGAL)
endif()

set_target_properties(claw3d_services PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "services")
