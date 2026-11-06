cmake_minimum_required(VERSION 3.16)

project(Frontend VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        src/Frontend/app/main.cpp
        src/Frontend/app/mainwindow.cpp
        src/Frontend/canvas/artworkcanvas.cpp
        src/Frontend/canvas/artworkcanvas_draw.cpp
        src/Frontend/home/homepage.cpp
        src/Frontend/project/projectgrid.cpp
        src/Frontend/project/project_serializer.cpp
        src/Frontend/common/dialog_helpers.cpp
        src/Frontend/app/simulationrunner.cpp
        src/Frontend/app/menubar_manager.cpp
        src/Frontend/common/frontendutils.cpp
        src/Frontend/app/export_mode.cpp
        src/Frontend/app/logpanel.cpp
        src/Frontend/workspace/workspace_ui.cpp
        src/Frontend/workspace/grid_tab.cpp
        src/Frontend/workspace/assembly_tab.cpp 
        src/Frontend/workspace/alignment_tab.cpp
        src/Frontend/workspace/masks_tab.cpp
        src/Frontend/workspace/bottom_tabs.cpp
        src/Frontend/workspace/workspace_status_bar.cpp
        src/Frontend/workspace/workspace_center.cpp
        src/Frontend/workspace/left_toolbar.cpp
        src/Frontend/workspace/right_panel.cpp
        src/Frontend/workspace/top_toolbars.cpp

        include/Frontend/app/mainwindow.h
        include/Frontend/home/homepage.h
        include/Frontend/project/projectgrid.h
        include/Frontend/project/project_serializer.h
        include/Frontend/common/dialog_helpers.h
        include/Frontend/app/simulationrunner.h
        include/Frontend/app/menubar_manager.h
        include/Frontend/common/frontendutils.h
        include/Frontend/app/logpanel.h
        include/Frontend/canvas/artworkcanvas.h
        include/Frontend/workspace/workspace_ui.h
        include/Frontend/workspace/grid_tab.h
        include/Frontend/workspace/assembly_tab.h
        include/Frontend/workspace/alignment_tab.h
        include/Frontend/workspace/masks_tab.h
        include/Frontend/workspace/bottom_tabs.h
        include/Frontend/workspace/workspace_status_bar.h
        include/Frontend/workspace/workspace_center.h
        include/Frontend/workspace/left_toolbar.h
        include/Frontend/workspace/right_panel.h
        include/Frontend/workspace/top_toolbars.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Frontend
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET Frontend APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(Frontend SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(Frontend
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(Frontend PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
if(WIN32)
    target_link_libraries(Frontend PRIVATE windowscodecs ole32)
endif()
if(MSVC)
    target_compile_options(Frontend PRIVATE /utf-8)
endif()
target_include_directories(Frontend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Frontend)
endif()
set_target_properties(Frontend PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Frontend
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Frontend)

    if(WIN32)
        get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)
        find_program(WINDEPLOYQT_EXECUTABLE
            NAMES windeployqt
            HINTS "${QT_INSTALL_PREFIX}/bin"
        )
        if(NOT WINDEPLOYQT_EXECUTABLE)
            message(FATAL_ERROR "windeployqt is required to deploy the Windows application.")
        endif()
        get_filename_component(CXX_COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        add_custom_command(TARGET Frontend POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E env
                "PATH=${CXX_COMPILER_BIN_DIR};$ENV{PATH}"
                "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --compiler-runtime
                "$<TARGET_FILE:Frontend>"
            COMMENT "Deploying Qt and compiler runtime beside Frontend"
            VERBATIM
        )
    endif()
endif()
