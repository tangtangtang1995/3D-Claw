add_executable(claw3d_archive_extract_utils_test
    archive_extract_utils_test.cpp)

target_link_libraries(claw3d_archive_extract_utils_test
    PRIVATE
        claw3d_services
        3rd_miniz)

target_include_directories(claw3d_archive_extract_utils_test
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src")

set_target_properties(claw3d_archive_extract_utils_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    FOLDER "tests")

add_test(NAME claw3d_archive_extract_utils
    COMMAND claw3d_archive_extract_utils_test)