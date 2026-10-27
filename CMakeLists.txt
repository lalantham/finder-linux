cmake_minimum_required(VERSION 3.22)
project(FinderLinux VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Strict warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg Test)

# Optional Poppler for PDF QuickLook preview
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(PopplerQt6 poppler-qt6)
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/operations
    ${CMAKE_CURRENT_SOURCE_DIR}/src/preview
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/views
)

set(FINDER_CORE_SOURCES
    src/core/FileItem.cpp
    src/core/FileItem.h
    src/core/FileSystemModel.cpp
    src/core/FileSystemModel.h
    src/core/XdgTrash.cpp
    src/core/XdgTrash.h
    src/core/VolumeManager.cpp
    src/core/VolumeManager.h
    src/core/SearchEngine.cpp
    src/core/SearchEngine.h
    src/core/UndoManager.cpp
    src/core/UndoManager.h
    src/core/DesktopAppsManager.cpp
    src/core/DesktopAppsManager.h
    src/operations/FileOperation.h
    src/operations/FileOperationEngine.cpp
    src/operations/FileOperationEngine.h
    src/app/AppSettings.cpp
    src/app/AppSettings.h
    src/app/ThemeManager.cpp
    src/app/ThemeManager.h
)

set(FINDER_UI_SOURCES
    src/ui/StyleHelper.cpp
    src/ui/StyleHelper.h
    src/ui/BreadcrumbBar.cpp
    src/ui/BreadcrumbBar.h
    src/ui/Toolbar.cpp
    src/ui/Toolbar.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/TabPage.cpp
    src/ui/TabPage.h
    src/ui/TabManager.cpp
    src/ui/TabManager.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ProgressDialog.cpp
    src/ui/ProgressDialog.h
    src/operations/ConflictDialog.cpp
    src/operations/ConflictDialog.h
    src/preview/PreviewManager.cpp
    src/preview/PreviewManager.h
    src/preview/AppChooserDialog.cpp
    src/preview/AppChooserDialog.h
    src/preview/QuickLookDialog.cpp
    src/preview/QuickLookDialog.h
    src/preview/GetInfoDialog.cpp
    src/preview/GetInfoDialog.h
    src/ui/views/FileItemDelegate.cpp
    src/ui/views/FileItemDelegate.h
    src/ui/views/IconView.cpp
    src/ui/views/IconView.h
    src/ui/views/ListView.cpp
    src/ui/views/ListView.h
    src/ui/views/ColumnView.cpp
    src/ui/views/ColumnView.h
    src/ui/views/GalleryView.cpp
    src/ui/views/GalleryView.h
    src/resources/resources.qrc
)

add_library(finder_core STATIC
    ${FINDER_CORE_SOURCES}
)
target_link_libraries(finder_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
)

add_library(finder_ui STATIC
    ${FINDER_UI_SOURCES}
)
target_link_libraries(finder_ui PUBLIC
    finder_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
)

if(PopplerQt6_FOUND)
    target_compile_definitions(finder_ui PUBLIC HAVE_POPPLER)
    target_include_directories(finder_ui PUBLIC ${PopplerQt6_INCLUDE_DIRS})
    target_link_libraries(finder_ui PUBLIC ${PopplerQt6_LIBRARIES})
endif()

add_executable(finder-linux
    src/main.cpp
    src/resources/resources.qrc
)
target_link_libraries(finder-linux PRIVATE
    finder_ui
    finder_core
)

include(GNUInstallDirs)

install(TARGETS finder-linux
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES data/finder-linux.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

foreach(SIZE 16 24 32 48 64 128 256 512)
    install(FILES data/icons/hicolor/${SIZE}x${SIZE}/apps/finder-linux.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${SIZE}x${SIZE}/apps
    )
endforeach()

install(FILES data/icons/hicolor/scalable/apps/finder-linux.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

enable_testing()
add_subdirectory(tests)
