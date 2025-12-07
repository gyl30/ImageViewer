cmake_minimum_required(VERSION 3.16)

project(ImageViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Concurrent)

set(PROJECT_SOURCES
    main.cc
    image_loader.cc
    waterfall_item.cc
    waterfall_scene.cc
    waterfall_view.cc
    file_scanner.cc
    main_window.cc
    image_viewer_window.cc
)

add_executable(ImageViewer ${PROJECT_SOURCES})

target_link_libraries(ImageViewer PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Concurrent)
