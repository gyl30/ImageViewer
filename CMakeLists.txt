cmake_minimum_required(VERSION 3.16)

project(ImageViewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui Concurrent Svg)

set(PROJECT_SOURCES
    main.cc
    image_loader.cc
    waterfall_item.cc
    waterfall_scene.cc
    waterfall_view.cc
    file_scanner.cc
    main_window.cc
    image_viewer_window.cc
    resources.qrc
)

add_executable(ImageViewer ${PROJECT_SOURCES})

target_link_libraries(ImageViewer PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Svg)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(imageviewer_tests
        tests/correctness_test.cc
        image_loader.cc
        waterfall_item.cc
        waterfall_scene.cc
        image_viewer_window.cc
        file_scanner.cc
        main_window.cc
        waterfall_view.cc
        resources.qrc
    )
    target_include_directories(imageviewer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(imageviewer_tests PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test Qt6::Svg)
    add_test(NAME correctness COMMAND imageviewer_tests)
    set_tests_properties(correctness PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

    add_executable(imageviewer_benchmark
        tools/performance_probe.cc
        image_loader.cc waterfall_item.cc waterfall_scene.cc waterfall_view.cc
        file_scanner.cc main_window.cc image_viewer_window.cc
        resources.qrc
    )
    target_include_directories(imageviewer_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(imageviewer_benchmark PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test Qt6::Svg)
endif()
