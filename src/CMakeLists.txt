cmake_minimum_required(VERSION 3.16)
project(fontviewer VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# MSVC: Enable Debug Information in Release builds
# /Zi: Generates a PDB file
# /DEBUG: Linker flag to create the PDB
# /OPT:REF & /OPT:ICF: Optimize the binary despite debug info presence
if(MSVC)
    add_compile_options("$<$<CONFIG:Release>:/Zi>")
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

# The viewer sources are compiled once and shared by the shipping DLL and the
# test binary, so the tests exercise the exact objects that ship.
set(FONTVIEWER_SOURCES
    characterwidget.cpp
    fontpreviewedit.cpp
    fontpreviewedit.h
    fontviewer.cpp
    fontwidget.cpp
    fontwidget.ui
    fontinfo.cpp
    glyphinspectorwidget.cpp
    waterfallwidget.cpp
    waterfallwidget.h
    ${seersdk_SOURCE_DIR}/seer/viewerbase.h
)

add_library(fontviewer_core OBJECT ${FONTVIEWER_SOURCES})
target_include_directories(fontviewer_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fontviewer_core PRIVATE
    SeerSdk::SeerSdk
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)
target_compile_definitions(fontviewer_core PRIVATE
    COMPANY_NAME="1218.io"
    PRODUCT_NAME="Seer"
)

add_library(fontviewer SHARED $<TARGET_OBJECTS:fontviewer_core>)
target_link_libraries(fontviewer PRIVATE
    SeerSdk::SeerSdk
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

# Offscreen regression suite for the viewer itself. Tests that need a real font
# read its path from FONTVIEWER_TEST_FONT and skip when it is unset.
add_executable(fontviewer_tests
    tests/viewer_tests.cpp
    $<TARGET_OBJECTS:fontviewer_core>
)
target_include_directories(fontviewer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fontviewer_tests PRIVATE
    SeerSdk::SeerSdk
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
