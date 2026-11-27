cmake_minimum_required(VERSION 3.16)
project(fontviewer VERSION 1.4.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(FONTVIEWER_GIT_VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/gitversion.h")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${FONTVIEWER_GIT_VERSION_HEADER}"
        "-DVERSION=${PROJECT_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGitVersion.cmake"
)
add_custom_target(fontviewer_git_version ALL
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_FILE=${FONTVIEWER_GIT_VERSION_HEADER}"
        "-DVERSION=${PROJECT_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateGitVersion.cmake"
    BYPRODUCTS "${FONTVIEWER_GIT_VERSION_HEADER}"
    VERBATIM
)

if(WIN32)
    enable_language(RC)
    set(FONTVIEWER_RC_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
    configure_file(fontviewer.rc.in ${CMAKE_CURRENT_BINARY_DIR}/fontviewer.rc @ONLY)
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/fontviewer.rc PROPERTIES
        OBJECT_DEPENDS "${FONTVIEWER_GIT_VERSION_HEADER}"
    )
    set(FONTVIEWER_RC_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/fontviewer.rc)
endif()

# MSVC: Enable Debug Information in Release builds
# /Zi: Generates a PDB file
# /DEBUG: Linker flag to create the PDB
# /OPT:REF & /OPT:ICF: Optimize the binary despite debug info presence
if(MSVC)
    add_compile_options(/utf-8 "$<$<CONFIG:Release>:/Zi>")
    add_link_options(
        "$<$<CONFIG:Release>:/DEBUG>"
        "$<$<CONFIG:Release>:/OPT:REF>"
        "$<$<CONFIG:Release>:/OPT:ICF>"
    )
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg Test)

enable_testing()

include(FetchContent)
FetchContent_Declare(SeerSdk
    GIT_REPOSITORY https://github.com/ccseer/Seer-sdk.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SeerSdk)

set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG        VER-2-13-3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(freetype)

# The viewer sources are compiled once and shared by the shipping DLL and the
# test binary, so the tests exercise the exact objects that ship.
set(FONTVIEWER_SOURCES
    bars/opentypefeaturesbar.cpp
    bars/opentypefeaturesbar.h
    bars/variableaxesbar.cpp
    bars/variableaxesbar.h
    engine/fontinspectorengine.cpp
    engine/fontinspectorengine.h
    fontshortcuts.cpp
    fontshortcuts.h
    fontui.cpp
    fontui.h
    fontviewer.cpp
    fontviewer.h
    fontwidget.cpp
    fontwidget.h
    fontwidget.ui
    tabs/characterwidget.cpp
    tabs/characterwidget.h
    tabs/fontpreviewedit.cpp
    tabs/fontpreviewedit.h
    tabs/glyphinspectorwidget.cpp
    tabs/glyphinspectorwidget.h
    tabs/samplepreviewtab.cpp
    tabs/samplepreviewtab.h
    tabs/textpreviewtab.cpp
    tabs/textpreviewtab.h
    tabs/waterfallwidget.cpp
    tabs/waterfallwidget.h
    ${seersdk_SOURCE_DIR}/seer/viewerbase.h
)

add_library(fontviewer_core OBJECT ${FONTVIEWER_SOURCES})
add_dependencies(fontviewer_core fontviewer_git_version)
target_include_directories(fontviewer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(fontviewer_core PRIVATE
    SeerSdk::SeerSdk
    freetype
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)
target_compile_definitions(fontviewer_core PRIVATE
    COMPANY_NAME="1218.io"
    PRODUCT_NAME="Seer"
)

add_library(fontviewer SHARED $<TARGET_OBJECTS:fontviewer_core> ${FONTVIEWER_RC_SOURCES})
add_dependencies(fontviewer fontviewer_git_version)
target_include_directories(fontviewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(fontviewer PRIVATE
    SeerSdk::SeerSdk
    freetype
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

set_target_properties(fontviewer PROPERTIES
    LINKER_LANGUAGE CXX
    OUTPUT_NAME "fontviewer"
    SUFFIX ".dll"
)

add_executable(test_fontviewer WIN32 test.cpp)
target_link_libraries(test_fontviewer PRIVATE
    fontviewer
    SeerSdk::SeerSdk
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

# Offscreen regression suite for the viewer itself. Golden fonts live in
# tests/fixtures and are resolved at runtime from FONTVIEWER_FIXTURES_DIR.
add_executable(fontviewer_tests
    tests/viewer_tests.cpp
    $<TARGET_OBJECTS:fontviewer_core>
)
target_include_directories(fontviewer_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_compile_definitions(fontviewer_tests PRIVATE
    FONTVIEWER_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(fontviewer_tests PRIVATE
    SeerSdk::SeerSdk
    freetype
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::Test
)

add_test(NAME fontviewer_ui COMMAND fontviewer_tests)
set_tests_properties(fontviewer_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
if(WIN32)
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
        set_property(TEST fontviewer_ui APPEND PROPERTY
            ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    else()
        set_tests_properties(fontviewer_ui PROPERTIES
            ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;PATH=$<TARGET_FILE_DIR:Qt6::Core>\\;$ENV{PATH}"
        )
    endif()
endif()
