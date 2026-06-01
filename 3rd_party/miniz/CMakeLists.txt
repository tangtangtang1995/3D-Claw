# miniz: small ZIP/deflate library used for AI generation archive extraction.

add_library(3rd_miniz STATIC
    miniz.c
    miniz_tdef.c
    miniz_tinfl.c
    miniz_zip.c
    miniz.h
    miniz_export.h
    miniz_common.h
    miniz_tdef.h
    miniz_tinfl.h
    miniz_zip.h)

set_target_properties(3rd_miniz PROPERTIES
    FOLDER "3rd_party"
    POSITION_INDEPENDENT_CODE ON)

target_include_directories(3rd_miniz PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(3rd_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

if(WIN32 OR MSVC)
    target_compile_definitions(3rd_miniz
        PUBLIC
            NOMINMAX
            _CRT_SECURE_NO_WARNINGS
            _CRT_SECURE_NO_DEPRECATE)
endif()