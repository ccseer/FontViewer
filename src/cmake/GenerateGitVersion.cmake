if(NOT DEFINED SOURCE_DIR OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED VERSION)
    message(FATAL_ERROR "GenerateGitVersion.cmake requires SOURCE_DIR, OUTPUT_FILE, and VERSION")
endif()

set(_git_hash "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short HEAD
        RESULT_VARIABLE _git_res
        OUTPUT_VARIABLE _git_out
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_git_res EQUAL 0 AND _git_out MATCHES "^[0-9A-Za-z._-]+$")
        set(_git_hash "${_git_out}")
    endif()
endif()

set(_tmp_file "${OUTPUT_FILE}.tmp")
file(WRITE "${_tmp_file}"
"#pragma once

#define FONTVIEWER_VERSION_STR \"${VERSION}\"
#define FONTVIEWER_GIT_HASH \"${_git_hash}\"
#define FONTVIEWER_PRODUCT_VERSION_STRING \"${VERSION}+${_git_hash}\"
")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_tmp_file}" "${OUTPUT_FILE}"
    RESULT_VARIABLE _copy_res
)
file(REMOVE "${_tmp_file}")
if(NOT _copy_res EQUAL 0)
    message(FATAL_ERROR "Failed to update git version header: ${OUTPUT_FILE}")
endif()
