cmake_minimum_required(VERSION 3.16)

project(ImageViewer VERSION 0.1.0 LANGUAGES CXX
    DESCRIPTION "Lightweight waterfall image viewer"
    HOMEPAGE_URL "https://github.com/gyl30/ImageViewer")

set(IMAGEVIEWER_VERSION "${PROJECT_VERSION}-rc.1")

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

find_package(Qt6 6.4.2 REQUIRED COMPONENTS Widgets Core Gui Concurrent Svg)

set(PROJECT_SOURCES
    main.cc
    image_loader.cc
    waterfall_item.cc
    waterfall_scene.cc
    waterfall_view.cc
    file_scanner.cc
    main_window.cc
    shortcut_dialog.cc
    image_viewer_window.cc
    window_title_bar.cc
    resources.qrc
)

if(WIN32)
    qt_add_executable(ImageViewer WIN32 ${PROJECT_SOURCES})
else()
    qt_add_executable(ImageViewer ${PROJECT_SOURCES})
endif()

target_link_libraries(ImageViewer PRIVATE Qt6::Widgets Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Svg)
target_compile_definitions(ImageViewer PRIVATE IMAGEVIEWER_VERSION="${IMAGEVIEWER_VERSION}")

include(GNUInstallDirs)
install(TARGETS ImageViewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
    configure_file(cmake/DeployQt.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/DeployQt.cmake @ONLY)
    install(SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/DeployQt.cmake)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES packaging/io.github.gyl30.ImageViewer.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES icons/app.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME io.github.gyl30.ImageViewer.svg)
    install(FILES packaging/copyright
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/imageviewer)
endif()

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
        shortcut_dialog.cc
        waterfall_view.cc
        window_title_bar.cc
        resources.qrc
    )
    target_include_directories(imageviewer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(imageviewer_tests PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test Qt6::Svg)
    add_test(NAME correctness COMMAND imageviewer_tests)
    set_tests_properties(correctness PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(imageviewer_benchmark
            tools/performance_probe.cc
            image_loader.cc waterfall_item.cc waterfall_scene.cc waterfall_view.cc
            file_scanner.cc main_window.cc image_viewer_window.cc
            shortcut_dialog.cc
            window_title_bar.cc
            resources.qrc
        )
        target_include_directories(imageviewer_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(imageviewer_benchmark PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Test Qt6::Svg)
    endif()
endif()

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(cmake/Packaging.cmake)
endif()
