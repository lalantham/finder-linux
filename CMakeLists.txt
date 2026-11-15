cmake_minimum_required(VERSION 3.22)
set(FINDER_VERSION "1.0.1" CACHE STRING "File Manager release version")
project(FinderLinux VERSION ${FINDER_VERSION} LANGUAGES CXX)

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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Svg Test Network DBus)

# Optional KDE window activation support (uses the Wayland xdg-activation
# protocol correctly on Plasma). The app still builds without KF6.
find_package(KF6WindowSystem QUIET)

# Optional Qt Multimedia for video/audio playback in Quick Look.
find_package(Qt6 6.4 QUIET COMPONENTS Multimedia MultimediaWidgets)

# Optional Poppler for PDF QuickLook preview and LibArchive for archive operations
find_package(PkgConfig REQUIRED)
pkg_check_modules(PopplerQt6 poppler-qt6)
pkg_check_modules(LibArchive REQUIRED libarchive)

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
    src/core/DiskImageManager.cpp
    src/core/DiskImageManager.h
    src/core/FileMetadataManager.cpp
    src/core/FileMetadataManager.h
    src/core/MediaMetadata.cpp
    src/core/MediaMetadata.h
    src/core/SearchEngine.cpp
    src/core/SearchEngine.h
    src/core/SearchQuery.cpp
    src/core/SearchQuery.h
    src/core/UndoManager.cpp
    src/core/UndoManager.h
    src/core/DesktopAppsManager.cpp
    src/core/DesktopAppsManager.h
    src/core/DesktopNotifier.cpp
    src/core/DesktopNotifier.h
    src/core/DbusActivation.cpp
    src/core/DbusActivation.h
    src/core/ServiceMenuManager.cpp
    src/core/ServiceMenuManager.h
    src/core/RecentFilesManager.cpp
    src/core/RecentFilesManager.h
    src/core/TagManager.cpp
    src/core/TagManager.h
    src/operations/FileOperation.h
    src/operations/FileOperationEngine.cpp
    src/operations/FileOperationEngine.h
    src/operations/ArchiveService.cpp
    src/operations/ArchiveService.h
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
    src/ui/SearchScopeBar.cpp
    src/ui/SearchScopeBar.h
    src/ui/Sidebar.cpp
    src/ui/Sidebar.h
    src/ui/SidebarSettingsDialog.cpp
    src/ui/SidebarSettingsDialog.h
    src/ui/ViewOptionsDialog.cpp
    src/ui/ViewOptionsDialog.h
    src/ui/PreviewPane.cpp
    src/ui/PreviewPane.h
    src/ui/SmartFolderDialog.cpp
    src/ui/SmartFolderDialog.h
    src/ui/TagManagementDialog.cpp
    src/ui/TagManagementDialog.h
    src/ui/TabPage.cpp
    src/ui/TabPage.h
    src/ui/TabManager.cpp
    src/ui/TabManager.h
    src/ui/TearOffTabBar.cpp
    src/ui/TearOffTabBar.h
    src/ui/WrappingLabel.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/FileManager1Service.cpp
    src/ui/FileManager1Service.h
    src/ui/ApplicationService.cpp
    src/ui/ApplicationService.h
    src/ui/ProgressDialog.cpp
    src/ui/ProgressDialog.h
    src/ui/BatchRenameDialog.cpp
    src/ui/BatchRenameDialog.h
    src/ui/CompressDialog.cpp
    src/ui/CompressDialog.h
    src/ui/TrashDialog.cpp
    src/ui/TrashDialog.h
    src/ui/TerminalPanel.cpp
    src/ui/TerminalPanel.h
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
target_include_directories(finder_core PUBLIC
    ${LibArchive_INCLUDE_DIRS}
)
target_link_libraries(finder_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::DBus
    Qt6::Widgets
    Qt6::Concurrent
    ${LibArchive_LIBRARIES}
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

if(KF6WindowSystem_FOUND)
    target_compile_definitions(finder_ui PUBLIC HAVE_KWINDOWSYSTEM)
    target_link_libraries(finder_ui PUBLIC KF6::WindowSystem)
endif()

if(Qt6Multimedia_FOUND AND Qt6MultimediaWidgets_FOUND)
    target_compile_definitions(finder_ui PUBLIC HAVE_QT_MULTIMEDIA)
    target_link_libraries(finder_ui PUBLIC Qt6::Multimedia Qt6::MultimediaWidgets)
endif()

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
    Qt6::Network
)
target_compile_definitions(finder-linux PRIVATE FINDER_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)

install(TARGETS finder-linux
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Generate the desktop entry with an absolute executable path. User-scoped
# installs live in ~/.local/bin, which Plasma's launcher does not always have
# on PATH; a relative Exec= line makes "Launching Finder" fail there.
configure_file(data/org.finderlinux.Finder.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.finderlinux.Finder.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.finderlinux.Finder.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# D-Bus service so browsers can activate Finder for "Show in folder"
# (org.freedesktop.FileManager1).
configure_file(data/org.freedesktop.FileManager1.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.FileManager1.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

# Standard org.freedesktop.Application activation (DBusActivatable=true).
configure_file(data/org.finderlinux.Finder.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.finderlinux.Finder.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.finderlinux.Finder.service
    DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services
)

install(FILES data/org.finderlinux.Finder.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)
install(FILES data/finder-linux.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)
install(FILES data/completions/finder-linux.bash
    DESTINATION ${CMAKE_INSTALL_DATADIR}/bash-completion/completions
    RENAME finder-linux
)
install(FILES data/completions/_finder-linux
    DESTINATION ${CMAKE_INSTALL_DATADIR}/zsh/site-functions
)
install(FILES data/completions/finder-linux.fish
    DESTINATION ${CMAKE_INSTALL_DATADIR}/fish/vendor_completions.d
)
install(FILES LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/finder-linux
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

# Release packaging -----------------------------------------------------------
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "finder-linux")
set(CPACK_PACKAGE_VENDOR "Finder Linux contributors")
set(CPACK_PACKAGE_CONTACT "Finder Linux contributors <lalantham@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "KDE-friendly file manager with a polished, familiar workflow")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "finder-linux")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/debian/postrm")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/lalantham/finder-linux")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "A KDE-friendly, brand-neutral file manager with tabs, split view, search, Quick Look previews, metadata-aware operations, and desktop integration.")
include(CPack)
