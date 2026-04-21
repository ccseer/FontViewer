cmake_minimum_required(VERSION 3.16)
project(fontviewer VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

include(FetchContent)
FetchContent_Declare(SeerSdk
    GIT_REPOSITORY https://github.com/ccseer/Seer-sdk.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SeerSdk)

add_library(fontviewer SHARED
    characterwidget.cpp
    fontviewer.cpp
    fontwidget.cpp
    fontwidget.ui
    fontinfo.cpp
    ${seersdk_SOURCE_DIR}/seer/viewerbase.h
)

target_link_libraries(fontviewer PRIVATE
    SeerSdk::SeerSdk
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

set_target_properties(fontviewer PROPERTIES
    OUTPUT_NAME "fontviewer"
    SUFFIX ".dll"
)

target_compile_definitions(fontviewer PRIVATE
    COMPANY_NAME="1218.io"
    PRODUCT_NAME="Seer"
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
